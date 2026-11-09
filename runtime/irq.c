/*
 * Interruptions, temps virtuel, minuteurs, entrées.
 *
 * Le code recompilé n'a pas de notion de cycles : le temps avance quand le
 * jeu attend (attente d'interruption par CP15, boucles sur VCOUNT) et par
 * petits pas à chaque livraison. Le VBlank tombe toutes les 560 190 cycles
 * (33,513982 MHz / 59,8261 Hz) ; un HBlank toutes les 2 130.
 *
 * Une interruption est livrée en appelant le gestionnaire recompilé installé
 * par le crt0 en 0x027E3FFC (mode IRQ, banques commutées, spsr sauvé) : c'est
 * exactement ce que fait le vecteur du BIOS.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include "arm7.h"
#include "arm7_run.h"
#include <setjmp.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
void ipc_stats(uint64_t *sent9, uint64_t *sent7, uint64_t *full7, unsigned *queued9, unsigned *queued7);
#include "verify.h"
#include <stdlib.h>
#include <string.h>

/* Le dernier changement d'overlay, en images. Un tour d'aperçu joué là-dessus rejoue du code qui
   vient d'être remplacé : il saute dans le décor, et son abandon laisse le coprocesseur simulé dans
   un état que la restitution ne reprend pas — la file de messages déborde ensuite sans fin. */
uint64_t g_overlay_dernier_change;
void rt_dump_dispatch(void);
int rt_overlay_loaded(uint32_t addr);

#define CYCLES_PER_FRAME 560190ull
#define CYCLES_PER_LINE  2130ull
#define LINES_PER_FRAME  263u
#define VISIBLE_LINES    192u

static uint64_t g_cycles;
static uint64_t g_frames;
/* Diagnostic : qui fait avancer l'horloge, et de combien. */
static uint64_t g_advance_calls, g_advance_cycles, g_wait_calls;
static uint64_t g_chrono_arm7;                 /* SFC_CHRONO_ARM7 : tics de QueryPerformanceCounter */
uint64_t hw_frames(void) { return g_frames; }
static uint32_t g_keys = 0x03FF;       /* aucun bouton : bits à 1 */
static uint32_t g_ext_keys = 0x007F;   /* X, Y, écran tactile relâché (bit 6) */

static struct { uint32_t reload, control; uint64_t start; uint32_t value; } g_timers[4];

uint64_t hw_now(void) { return g_cycles; }

int g_headless;                 /* SFC_NO_VIDEO : personne ne lit les entrées à notre place */

void hw_init(Cpu *c) {
    (void)c;
    g_headless = getenv("SFC_NO_VIDEO") != NULL;
    g_cycles = 0;
    memset(g_timers, 0, sizeof g_timers);
    /* Le firmware laisse les 32 Ko de WRAM partagée au coprocesseur : c'est ce qui rend
       contiguë, de 0x037F8000 à 0x0380FFFF, la zone d'où il exécute son pilote son. Il y
       recopie ses sections dès son crt0, avant que l'ARM9 n'ait touché à quoi que ce soit. */
    mmio_poke(0x244, (mmio_peek(0x244) & 0x00FFFFFFu) | 0x03000000u);   /* WRAMCNT = 3, octet 0x247 */
}

/* ------------------------------------------------------------------ balayage */
uint32_t hw_vcount(void) {
    return (uint32_t)((g_cycles % CYCLES_PER_FRAME) / CYCLES_PER_LINE);
}

uint32_t hw_dispstat_bits(void) {
    uint32_t line = hw_vcount();
    uint32_t in_line = (uint32_t)((g_cycles % CYCLES_PER_FRAME) % CYCLES_PER_LINE);
    uint32_t bits = 0;
    if (line >= VISIBLE_LINES && line != LINES_PER_FRAME - 1) bits |= 1u;   /* VBlank */
    if (in_line >= 1606) bits |= 2u;                                         /* HBlank */
    uint32_t dispstat = mmio_peek(0x004);
    uint32_t vcount_target = ((dispstat >> 8) & 0xFF) | ((dispstat & 0x80) << 1);
    if (line == vcount_target) bits |= 4u;
    return bits;
}

uint32_t hw_keyinput(void) { return g_keys; }
void hw_set_keys(uint32_t held) {
    g_keys = (~held) & 0x03FFu;
    g_ext_keys = (g_ext_keys & ~3u) | ((~held >> 10) & 3u);
}
void hw_set_pen(int down) {
    if (down) g_ext_keys &= ~0x40u; else g_ext_keys |= 0x40u;
}
uint32_t hw_extkeys(void) { return g_ext_keys; }

/* ------------------------------------------------------------------ minuteurs */
static const uint32_t prescaler[4] = {1, 64, 256, 1024};

static uint32_t timer_current(int t) {
    uint32_t ctl = g_timers[t].control;
    if (!(ctl & 0x80)) return g_timers[t].value;
    if (ctl & 0x04) return g_timers[t].value;      /* cascade : approximé statique */
    uint64_t elapsed = (g_cycles - g_timers[t].start) / prescaler[ctl & 3];
    uint64_t span = 0x10000ull - g_timers[t].reload;
    if (elapsed < span) return (uint32_t)(g_timers[t].reload + elapsed);
    /* débordé : repart de reload */
    return (uint32_t)(g_timers[t].reload + (elapsed - span) % span);
}

uint32_t hw_timer_read(Cpu *c, uint32_t off) {
    (void)c;
    int t = (int)((off - 0x100) / 4);
    return timer_current(t) | (g_timers[t].control << 16);
}

void hw_timer_write(Cpu *c, uint32_t off, uint32_t v) {
    (void)c;
    int t = (int)((off - 0x100) / 4);
    uint32_t ctl = (v >> 16) & 0xFF;
    g_timers[t].reload = v & 0xFFFF;
    if ((ctl & 0x80) && !(g_timers[t].control & 0x80)) {
        g_timers[t].start = g_cycles;
        g_timers[t].value = g_timers[t].reload;
    }
    g_timers[t].control = ctl;
}

static void timers_check(Cpu *c, uint64_t before) {
    for (int t = 0; t < 4; t++) {
        uint32_t ctl = g_timers[t].control;
        if (!(ctl & 0x80) || !(ctl & 0x40) || (ctl & 0x04)) continue;
        uint64_t span = (0x10000ull - g_timers[t].reload) * prescaler[ctl & 3];
        uint64_t first = g_timers[t].start + span;
        if (first > g_cycles) continue;
        uint64_t k_before = before < first ? 0 : (before - first) / span + 1;
        uint64_t k_now = (g_cycles - first) / span + 1;
        if (k_now > k_before) hw_raise_irq(c, IRQ_TIMER0 + t);
    }
}

/* ------------------------------------------------------------------ interruptions */
static uint64_t g_irq_count[32];
void hw_raise_irq(Cpu *c, int irq) {
    g_irq_count[irq & 31]++;
    mmio_poke(0x214, mmio_peek(0x214) | (1u << irq));
    hw_check_irq(c);
}

static int irq_pending(void) {
    return mmio_peek(0x208) & 1u && (mmio_peek(0x210) & mmio_peek(0x214));
}

volatile uint32_t g_irq_line;

void hw_check_irq(Cpu *c) {
    (void)c;
    /* la livraison se fait aux points sûrs : attentes explicites et arêtes arrière */
    g_irq_line = irq_pending() ? 1u : 0u;
    /* Une interruption levée remet le budget à zéro, même masquée : pour le banc, qui compare ce
       budget, le monde a bougé — la réécriture, rejouée dans un monde figé, ne la reverra pas. */
    if (g_irq_line) { g_edge_budget = 0; g_perturbations++; }   /* livrée à la prochaine arête arrière */
}

/* Un tour d'aperçu ne reçoit aucune interruption — sauf celle du balayage, quand on la lui donne
   exprès : le vrai tour, lui, l'a reçue, et ce que son gestionnaire compte (les tours du jeu) mène des
   animations que l'aperçu doit prédire juste. */
int g_apercu_vblank_a_livrer;

void rt_irq_check(Cpu *c) {
    extern int g_apercu_en_cours;
    if (g_apercu_en_cours && !g_apercu_vblank_a_livrer) return;
    if (g_stuck_dump) rt_stuck_report(c);   /* le chien de garde demande l'état du thread bloqué */
    if (c->irq_off || c->in_irq) return;
    hw_deliver_pending(c);
}

/* Le temps que consomme le calcul : une arête arrière vaut quelques instructions. Sans cela,
   une boucle qui attend un événement (compteur d'images, file remplie par une interruption)
   n'aboutirait jamais, le temps virtuel n'avançant qu'aux attentes explicites. */
/* Arêtes du code traduit entre deux avancées de l'horloge. Plus c'est haut, moins on
 * interrompt la simulation — et chaque interruption entraîne un passage chez le coprocesseur —
 * mais plus le temps avance par à-coups. SFC_EDGE_BUDGET permet d'en juger. */
static int edge_budget(void) {
    static int v;
    if (!v) {
        const char *e = getenv("SFC_EDGE_BUDGET");
        v = e ? atoi(e) : 256;
        if (v < 16) v = 16;
    }
    return v;
}
#define EDGE_BUDGET      edge_budget()
#define CYCLES_PER_EDGE  4
volatile int32_t g_edge_budget = 256;

static uint64_t g_edges9;
uint64_t hw_edges(void) { return g_edges9; }

/* SFC_PROFILE_ARM9=1 : profilage par échantillonnage du processeur principal, relevé à chaque
   tranche d'arêtes. Faute de symboles, les adresses de retour les plus fréquentes sont la seule
   façon de savoir quelles fonctions du jeu coûtent. */
static struct { uint32_t addr; uint64_t hits; } g_prof9[512];
static int g_prof9_n;

static void prof9_sample(Cpu *c) {
    static int on = -1;
    static uint64_t depuis;
    if (on < 0) {
        on = getenv("SFC_PROFILE_ARM9") != NULL;
        /* SFC_PROFILE_FROM=image : ne relever qu'à partir de là — un vol se profile mal quand
           deux mille images de menus le précèdent et noient ses fonctions dans les leurs */
        const char *d = getenv("SFC_PROFILE_FROM");
        depuis = d ? strtoull(d, NULL, 0) : 0;
    }
    if (!on || g_frames < depuis) return;
    uint32_t a = c->depth ? c->shadow[c->depth - 1] : c->r[15];
    for (int i = 0; i < g_prof9_n; i++)
        if (g_prof9[i].addr == a) { g_prof9[i].hits++; return; }
    if (g_prof9_n < 512) { g_prof9[g_prof9_n].addr = a; g_prof9[g_prof9_n].hits = 1; g_prof9_n++; }
}

void rt_profile9_report(void) {
    if (!g_prof9_n) return;
    for (int i = 0; i < g_prof9_n; i++)
        for (int j = i + 1; j < g_prof9_n; j++)
            if (g_prof9[j].hits > g_prof9[i].hits) {
                uint32_t ta = g_prof9[i].addr; uint64_t th = g_prof9[i].hits;
                g_prof9[i] = g_prof9[j];
                g_prof9[j].addr = ta; g_prof9[j].hits = th;
            }
    uint64_t total = 0;
    for (int i = 0; i < g_prof9_n; i++) total += g_prof9[i].hits;
    rt_log("processeur principal : où passe son temps (%llu relevés, %d adresses)",
           (unsigned long long)total, g_prof9_n);
    for (int i = 0; i < g_prof9_n && i < 14; i++)
        rt_log("   0x%08X : %5.1f %%", g_prof9[i].addr, 100.0 * g_prof9[i].hits / total);
}

/* Le banc à froid donne à chaque essai un nombre d'arêtes au-delà duquel il l'abandonne :
   un compteur tiré au sort fait tourner une boucle des milliards de fois, et les deux versions
   y tourneraient pareil. Il pose le point de reprise ; on y saute. */
jmp_buf g_essai_reprise;
uint64_t g_essai_aretes, g_essai_aretes_max;

void rt_edge(Cpu *c) {
    if (g_essai_aretes_max && ++g_essai_aretes > g_essai_aretes_max) longjmp(g_essai_reprise, 1);
    { extern void apercu_arete(void); apercu_arete(); }
    if (c->is_arm7) { arm7_edge(c); return; }   /* le coprocesseur n'avance pas l'horloge lui-même */
    prof9_sample(c);
    g_edges9++;
    g_edge_budget = EDGE_BUDGET;
    if (g_stuck_dump) rt_stuck_report(c);
    hw_advance(c, EDGE_BUDGET * CYCLES_PER_EDGE);
    if (g_irq_line && !g_temps_fige) rt_irq_check(c);   /* à froid, rien n'arrive de l'extérieur */
}

/* Laisser passer du temps simulé sans exécuter de code du jeu : les mêmes arêtes, au même
   rythme, avec les mêmes livraisons d'interruptions — mais rien entre deux. Sert à une attente
   active dont on connaît la fin : le jeu relit son horloge en boucle, et tout ce que cette
   boucle produit d'utile, c'est que le temps passe. */
void rt_laisser_passer(Cpu *c, uint64_t cycles) {
    uint64_t but = g_cycles + cycles;
    while (g_cycles < but) rt_edge(c);
}

/* Joue le rôle du vecteur IRQ du BIOS : empile {r0-r3, r12, lr_irq}, appelle le
   gestionnaire installé en 0x027E3FFC, dépile, revient au code interrompu. */
void hw_deliver_pending(Cpu *c) {
    extern int g_apercu_en_cours;
    if (g_apercu_en_cours && !g_apercu_vblank_a_livrer) return;
    if (c->in_irq || c->irq_off || !irq_pending()) return;
    uint32_t handler = mem_read32(c, 0x027E3FFCu);    /* installé par le crt0 */
    if (!handler) return;
    g_perturbations++;
    c->in_irq = 1;
    /* entrée en exception IRQ : spsr_irq = cpsr, lr_irq = pc + 4, mode IRQ, IRQ masquées, ARM */
    uint32_t cpsr = cpsr_read(c);
    uint32_t saved_pc = c->r[15];
    cpu_switch_mode(c, MODE_IRQ);
    c->spsr = cpsr;
    c->irq_off = 1;
    c->thumb = 0;
    c->r[14] = saved_pc + 4;
    uint32_t sp = c->r[13] - 24u;
    c->r[13] = sp;
    mem_write32(c, sp + 0, c->r[0]);  mem_write32(c, sp + 4, c->r[1]);
    mem_write32(c, sp + 8, c->r[2]);  mem_write32(c, sp + 12, c->r[3]);
    mem_write32(c, sp + 16, c->r[12]); mem_write32(c, sp + 20, c->r[14]);
    uint32_t depth = c->depth;
    c->irq_resumed = 0;
    /* Le gestionnaire revient au vecteur BIOS simulé, pas directement au code
       interrompu. Son lr doit correspondre au retour de la pile fantôme. */
    c->r[14] = 0xFFFFFFFEu;
    RT_CALL(c, 0xFFFFFFFEu, rt_call(c, handler));
    c->depth = depth;
    /* Le gestionnaire est revenu (pop {pc} vers le vecteur). Si un thread a été commuté,
       cette fibre a été reprise avec ses registres rechargés depuis son contexte : on ne
       dépile pas par-dessus. */
    if (c->mode != MODE_IRQ) cpu_switch_mode(c, MODE_IRQ);
    sp = c->r[13];
    if (!c->irq_resumed) {
        c->r[0] = mem_read32(c, sp + 0);  c->r[1] = mem_read32(c, sp + 4);
        c->r[2] = mem_read32(c, sp + 8);  c->r[3] = mem_read32(c, sp + 12);
        c->r[12] = mem_read32(c, sp + 16);
        c->r[15] = saved_pc;
    }
    c->r[13] = sp + 24u;
    uint32_t restore = c->irq_resumed ? c->spsr : cpsr;
    c->irq_resumed = 0;
    cpsr_write(c, restore, 0xFu);
    c->in_irq = 0;
    g_irq_line = irq_pending() ? 1u : 0u;
    if (g_irq_line) g_edge_budget = 0;   /* livrée à la prochaine arête arrière */
}

/* ------------------------------------------------------------------ temps */
static void advance_to(Cpu *c, uint64_t target) {
    while (g_cycles < target) {
        uint64_t frame_pos = g_cycles % CYCLES_PER_FRAME;
        uint64_t next_line = g_cycles + (CYCLES_PER_LINE - frame_pos % CYCLES_PER_LINE);
        uint64_t before = g_cycles;
        g_cycles = next_line < target ? next_line : target;
        timers_check(c, before);
        /* Le coprocesseur avance du même temps que nous venons de consommer. */
        if (arm7_enabled()) {
            /* SFC_CHRONO_ARM7=1 : le temps de la machine hôte passé dans le coprocesseur */
            static int chrono = -1;
            if (chrono < 0) chrono = getenv("SFC_CHRONO_ARM7") != NULL;
            if (chrono) {
                LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
                arm7_tick(c, g_cycles - before);
                QueryPerformanceCounter(&b);
                g_chrono_arm7 += (uint64_t)(b.QuadPart - a.QuadPart);
            } else arm7_tick(c, g_cycles - before);
        }
        if (g_cycles == next_line) {
            uint32_t line = hw_vcount();
            uint32_t dispstat = mmio_peek(0x004);
            if (dispstat & 0x10) hw_raise_irq(c, IRQ_HBLANK);
            if (arm7_enabled()) arm7_scanline(line);
            dma_trigger(c, 2);
            if (line == VISIBLE_LINES) {
                g_frames++;
                /* SFC_MAX_FRAMES=n : arrêt propre après n images (runs reproductibles) */
                static uint64_t max_frames; static int max_read;
                if (!max_read) { const char *m = getenv("SFC_MAX_FRAMES"); max_frames = m ? strtoull(m, NULL, 10) : 0; max_read = 1; }
                /* SFC_DUMP_DISPATCH_AT=n : les derniers appels indirects aux images n et n+1 */
                static uint64_t dump_at; static int dump_read;
                if (!dump_read) { const char *d = getenv("SFC_DUMP_DISPATCH_AT"); dump_at = d ? strtoull(d, NULL, 10) : 0; dump_read = 1; }
                if (dump_at && (g_frames == dump_at || g_frames == dump_at + 1)) {
                    rt_log("image %llu : derniers appels indirects", (unsigned long long)g_frames);
                    rt_dump_dispatch();
                    rt_log("  IRQ levées : vblank=%llu timer0=%llu timer1=%llu ipc_recv=%llu carte=%llu gxfifo=%llu ; IE=%08X GXSTAT=%08X DMA0..3CNT=%08X %08X %08X %08X TM0=%08X TM1=%08X",
                           (unsigned long long)g_irq_count[0], (unsigned long long)g_irq_count[3], (unsigned long long)g_irq_count[4],
                           (unsigned long long)g_irq_count[18], (unsigned long long)g_irq_count[19], (unsigned long long)g_irq_count[21],
                           mmio_peek(0x210), gx_read(c, 0x600, 4), mmio_peek(0x0B8), mmio_peek(0x0C4), mmio_peek(0x0D0), mmio_peek(0x0DC),
                           hw_timer_read(c, 0x100), hw_timer_read(c, 0x104));
                }
                if (max_frames && g_frames > max_frames) {
                    rt_log("arrêt : %llu images atteintes (SFC_MAX_FRAMES)", (unsigned long long)max_frames);
                    arm7_profile_report();
                    rt_profile9_report();
                    rt_sortir(0);
                }
                if (g_frames <= 3 || g_frames % 600 == 0) {
                    if (arm7_enabled())
                        rt_log("image %llu (IME=%u IE=%08X IF=%08X DISPSTAT=%04X ; arêtes %llu / coproc %llu, son IME=%u IE=%08X IF=%08X)",
                               (unsigned long long)g_frames, mmio_peek(0x208) & 1u, mmio_peek(0x210), mmio_peek(0x214),
                               dispstat & 0xFFFF, (unsigned long long)g_edges9, (unsigned long long)arm7_edges(),
                               mmio7_peek(0x208) & 1u, mmio7_peek(0x210), mmio7_peek(0x214)),
                        rt_log("   coproc : vblank=%llu vcount=%llu hblank=%llu timer0=%llu timer1=%llu ipc_recv=%llu ipc_sync=%llu spi=%llu dma=%llu",
                               (unsigned long long)arm7_irq_count(IRQ7_VBLANK), (unsigned long long)arm7_irq_count(IRQ7_VCOUNT),
                               (unsigned long long)arm7_irq_count(IRQ7_HBLANK), (unsigned long long)arm7_irq_count(IRQ7_TIMER0),
                               (unsigned long long)arm7_irq_count(IRQ7_TIMER0 + 1), (unsigned long long)arm7_irq_count(IRQ7_IPC_RECV),
                               (unsigned long long)arm7_irq_count(IRQ7_IPC_SYNC), (unsigned long long)arm7_irq_count(IRQ7_SPI),
                               (unsigned long long)arm7_irq_count(IRQ7_DMA0)),
                        ({ uint64_t s9, s7, f7; unsigned q9, q7;
                           ipc_stats(&s9, &s7, &f7, &q9, &q7);
                           rt_log("   son : file de sortie %d tampons, %llu fois à sec, %llu échantillons perdus,"
                               " %llu produits pour tenir le temps réel",
                               audio_queued(), (unsigned long long)audio_underruns(),
                               (unsigned long long)snd_dropped(), (unsigned long long)snd_late_samples()),
                        rt_log("   liaison : %llu mots 9->7 (%u en attente), %llu mots 7->9 (%u en attente), %llu refus de file pleine",
                                  (unsigned long long)s9, q9, (unsigned long long)s7, q7, (unsigned long long)f7); }),
                        ({ LARGE_INTEGER f; QueryPerformanceFrequency(&f);
                           rt_log("   horloge : %llu appels, %llu cycles, %llu attentes ; coprocesseur %.1f ms de machine hôte depuis le dernier relevé",
                               (unsigned long long)g_advance_calls, (unsigned long long)g_advance_cycles,
                               (unsigned long long)g_wait_calls, g_chrono_arm7 * 1000.0 / (double)f.QuadPart);
                           g_chrono_arm7 = 0; });
                    else
                        rt_log("image %llu (IME=%u IE=%08X IF=%08X DISPSTAT=%04X)", (unsigned long long)g_frames,
                               mmio_peek(0x208) & 1u, mmio_peek(0x210), mmio_peek(0x214), dispstat & 0xFFFF);
                }
                { extern void rt_suivre_objet(Cpu *c); rt_suivre_objet(c); }   /* SFC_SUIVRE_OBJET */
                if (dispstat & 0x08) hw_raise_irq(c, IRQ_VBLANK);
                dma_trigger(c, 1);
                gx_vblank(c);
                if (arm7_enabled()) arm7_vblank(c); else arm7_on_vblank(c);
                verify_frame(g_frames);
                { extern void rt_watch_tick(uint64_t); rt_watch_tick(g_frames); }   /* SFC_WATCH_DEPUIS */
                {   /* chronologie des overlays : les chargements après le démarrage ne passent pas
                       par la carte (cache en RAM) ; on relit la RAM des trois emplacements partagés */
                    static int last[3] = {-2, -2, -2};
                    static const uint32_t probe[3] = {0x021F0000u, 0x02228000u, 0x0225A000u};
                    static const char *names[3] = {"2-4", "5-22", "23-25"};
                    for (int s = 0; s < 3; s++) {
                        int now = rt_overlay_loaded(probe[s]);
                        if (now != last[s]) {
                            rt_log("overlay : image %llu, emplacement %s : %d -> %d", (unsigned long long)g_frames, names[s], last[s], now);
                            last[s] = now;
                            g_overlay_dernier_change = g_frames;
                        }
                    }
                }
                gx_dump_polys(g_frames);
                gx_geom_clip_stats(g_frames);
                gx_geom_cell_stats(g_frames);
                rt_find_value(g_frames);
                rt_dump_mem(g_frames);
                { extern void rt_ram_images(uint64_t); rt_ram_images(g_frames); }     /* SFC_RAM_IMAGES */
                { extern void rt_2d_images(uint64_t); rt_2d_images(g_frames); }       /* SFC_2D_IMAGES */
                { extern void rt_diag_objets(uint64_t); rt_diag_objets(g_frames); }   /* SFC_DIAG_OBJETS */
                {   /* SFC_PROFILE : la part de chaque fil, toutes les six cents images */
                    static int prof = -1;
                    if (prof < 0) prof = getenv("SFC_PROFILE") != NULL;
                    if (prof && g_frames % 600 == 0) { extern void rt_fils_rapport(void); rt_fils_rapport(); }
                }
                rt_poke();
                rt_appel_programme(c);
                natif_banc_froid(c);
                if (g_headless) verify_input_headless(g_frames);
                video_vblank();
                gx_vblank_present();
            }
            uint32_t vcount_target = ((dispstat >> 8) & 0xFF) | ((dispstat & 0x80) << 1);
            if (line == vcount_target && (dispstat & 0x20)) hw_raise_irq(c, IRQ_VCOUNT);
        }
    }
}

/* Le banc à froid fige le temps : lire DISPSTAT ou un minuteur n'y fait plus avancer
   l'horloge, sans quoi deux lectures identiques rendraient deux valeurs. */
int g_temps_fige;

void hw_advance(Cpu *c, uint64_t cycles) {
    if (g_temps_fige) return;
    g_advance_calls++;
    g_advance_cycles += cycles;
    advance_to(c, g_cycles + cycles);
}

/* CP15 c7,c0,4 : attente d'interruption. On avance jusqu'à la prochaine et on la livre. */
uint64_t rt_nb_attentes(void) { return g_wait_calls; }

/* Le fil qui attend une interruption : son tour est fini, son image échangée, et il ne fera rien jusqu'au
   balayage. C'est de là que partent les tours d'aperçu hors du vol (apercu.c, natif.c). */
Cpu *g_cpu_en_halte;

void rt_cpu_wait_interrupt(Cpu *c) {
    if (c->is_arm7) { arm7_halt(c); return; }   /* lui, il rend la main : c'est l'ARM9 qui mène */
    { extern void apercu_abandon(int); apercu_abandon(4); }   /* un tour d'aperçu n'attend rien */
    Cpu *halte_avant = g_cpu_en_halte;
    g_cpu_en_halte = c;
    g_wait_calls++;
    int guard = 0;
    static uint64_t idle_calls, last_delivery_frame;
    idle_calls++;
    if (idle_calls > 200000 && g_frames == last_delivery_frame) {
        rt_log("attente d'interruption sans livraison : IME=%u IE=%08X IF=%08X mode=%02X I=%u in_irq=%u ligne=%u",
               mmio_peek(0x208) & 1u, mmio_peek(0x210), mmio_peek(0x214), c->mode, c->irq_off, c->in_irq, hw_vcount());
        rt_fatal(c, "livelock d'attente d'interruption");
    }
    /* Une attente qui ne livre plus rien, le temps avançant quand même : le jeu est bloqué sans que le
       chien de garde le voie. SFC_DIAG_ATTENTE=1 en donne l'état des deux processeurs, une fois. */
    {
        static int diag = -1, fait;
        if (diag < 0) diag = getenv("SFC_DIAG_ATTENTE") != NULL;
        if (diag && !fait && idle_calls > 100000) {
            extern void rt_stuck_report(Cpu *c);
            extern void rt_rapport_arm7(void);
            fait = 1;
            rt_log("attente sans livraison depuis %llu appels, image %llu : I=%u in_irq=%u mode=%02X",
                   (unsigned long long)idle_calls, (unsigned long long)g_frames, c->irq_off, c->in_irq, c->mode);
            rt_stuck_report(c);
            rt_rapport_arm7();
        }
    }
    rt_watchdog_kick(c);
    while (!irq_pending() && guard++ < 1000) {
        /* prochain évènement : ligne suivante */
        hw_advance(c, CYCLES_PER_LINE);
    }
    if (!irq_pending()) rt_fatal(c, "attente d'interruption sans interruption possible (IME=%u IE=%08X IF=%08X)",
                                 mmio_peek(0x208), mmio_peek(0x210), mmio_peek(0x214));
    if (!c->irq_off && !c->in_irq) { idle_calls = 0; last_delivery_frame = g_frames; }
    g_cpu_en_halte = halte_avant;
    hw_deliver_pending(c);
}

/* ------------------------------------------------------------------ tours d'aperçu (apercu.c) */
/* Ce qu'un tour d'aperçu peut changer ici : les minuteries qu'il reprogramme, les touches qu'on lui prête. */
static struct { uint32_t reload, control; uint64_t start; uint32_t value; } g_timers_sauves[4];
static uint32_t g_keys_sauve, g_ext_keys_sauve, g_irq_line_sauve;
void hw_apercu_sauver(void) {
    memcpy(g_timers_sauves, g_timers, sizeof g_timers);
    g_keys_sauve = g_keys; g_ext_keys_sauve = g_ext_keys; g_irq_line_sauve = g_irq_line;
}
void hw_apercu_rendre(void) {
    memcpy(g_timers, g_timers_sauves, sizeof g_timers);
    g_keys = g_keys_sauve; g_ext_keys = g_ext_keys_sauve;
    g_irq_line = irq_pending() ? 1u : 0u;
    (void)g_irq_line_sauve;
}
