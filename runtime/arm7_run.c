/*
 * Le coprocesseur ARM7 traduit.
 *
 * Jusqu'ici son rôle était tenu de l'extérieur : arm7_hle.c répondait aux
 * messages du protocole PXI sans exécuter une seule de ses instructions. Ici
 * c'est son propre binaire, traduit comme celui de l'ARM9, qui tourne — et
 * avec lui le pilote son du SDK, que personne ne sait réécrire fidèlement.
 *
 * Les deux processeurs se partagent le fil d'exécution de l'hôte par des
 * fibres, comme les threads du jeu. L'ARM9 mène : il avance l'horloge, et à
 * chaque ligne de balayage il cède au coprocesseur les cycles qu'il vient de
 * consommer. Le coprocesseur rend la main quand il se met en attente
 * d'interruption (HALTCNT) ou quand son budget d'exécution est épuisé.
 *
 * Ses interruptions sont les siennes : son IME, son IE, son IF, et son vecteur
 * installé en 0x0380FFFC — la fin de sa WRAM, exactement comme l'ARM9 installe
 * le sien en fin de DTCM.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include "arm7_run.h"
#include "arm7.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARM7_VECTOR      0x0380FFFCu    /* le gestionnaire d'interruption qu'il installe */
/* Adresse de retour que le vecteur de son BIOS met dans lr avant d'appeler le gestionnaire :
   celui-ci l'empile à son entrée et la redépile dans pc pour revenir. Elle désigne le BIOS,
   qu'on n'exécute pas — la pile fantôme la reconnaît comme un retour. */
#define ARM7_IRQ_RETURN  0x0000013Cu
/* Le coprocesseur avance au même rythme que l'ARM9 : une arête arrière vaut le même nombre
   de cycles des deux côtés, sinon l'un affame l'autre. Son horloge est deux fois plus lente,
   d'où le double. */
/* Combien de cycles du coprocesseur vaut une arête du code traduit. C'est une estimation de la
 * densité de branchements de son code : trop bas, on lui donne plus de temps qu'il n'en a sur la
 * console et il coûte d'autant plus cher à simuler ; trop haut, son pilote sonore prend du retard.
 * SFC_ARM7_CPE la règle, pour éprouver la valeur sur le son enregistré. */
static uint64_t g_switches;        /* passages de fibre vers le coprocesseur */
static int g_cpe = 8, g_min_edges = 8;      /* figés au démarrage : lus à chaque appel d'horloge */

static void arm7_read_settings(void) {
    const char *e = getenv("SFC_ARM7_CPE");
    g_cpe = e ? atoi(e) : 8;
    if (g_cpe < 1) g_cpe = 1;
    e = getenv("SFC_ARM7_MIN");
    g_min_edges = e ? atoi(e) : 8;
    if (g_min_edges < 1) g_min_edges = 1;
}

static inline int arm7_cycles_per_edge(void) { return g_cpe; }
#define ARM7_CYCLES_PER_EDGE arm7_cycles_per_edge()

/* Combien d'arêtes le coprocesseur doit avoir accumulées avant qu'on lui passe la main. Plus
 * c'est haut, moins il y a de changements de contexte — et ils dominaient le coût — mais plus il
 * réagit tard. SFC_ARM7_MIN le règle. */
/* Huit arêtes : le son reste identique à 96,6 % contre un passage à chaque appel d'horloge, et
   l'on gagne sept pour cent. Seize le fait déjà diverger, trente-deux le change entièrement. */
static inline int arm7_min_edges(void) { return g_min_edges; }
#define ARM7_EDGE_SLICE      256        /* arêtes entre deux passages par arm7_edge */
#define ARM7_EDGE_MAX        65536      /* plafond : son sommeil ne lui fait pas de réserve */

static Cpu g_cpu7;
static void *g_fiber7, *g_fiber9;
static int g_enabled = -1;
static int g_started;
static int g_halted;
static int g_in_arm7;                   /* la fibre du coprocesseur est celle qui tourne */
static int32_t g_edge7;                 /* arêtes qui lui restent dues */
static int32_t g_budget7;               /* son compte à rebours, mis de côté quand l'ARM9 tourne */
static uint64_t g_cycles7;              /* son horloge, esclave de celle de l'ARM9 */
static uint32_t g_entry7;
static uint64_t g_instr7;               /* arêtes parcourues : mesure de son activité */
static int g_trace7;                    /* SFC_TRACE_ARM7 : suivre son avancement */

Cpu *arm7_cpu(void) { return &g_cpu7; }

/* Le coprocesseur a lui aussi des threads (threads7.c) : la fibre qui le porte change avec eux,
   et c'est toujours la courante que l'ARM9 doit reprendre quand il lui cède la main. */
void arm7_set_fiber(void *fiber) { if (fiber) g_fiber7 = fiber; }

int arm7_enabled(void) {
#ifdef SFC_HAVE_ARM7
    if (g_enabled < 0) {
        const char *e = getenv("SFC_ARM7");
        g_enabled = e && *e && *e != '0';
    }
    return g_enabled;
#else
    return 0;
#endif
}

/* ------------------------------------------------------------------ interruptions */
static inline uint32_t io7(uint32_t off) { uint32_t v; memcpy(&v, g_io7 + off, 4); return v; }
static inline void io7w(uint32_t off, uint32_t v) { memcpy(g_io7 + off, &v, 4); }

static int irq7_pending(void) { return (io7(0x210) & io7(0x214)) != 0; }
static int irq7_deliverable(void) { return (io7(0x208) & 1u) && irq7_pending(); }

static uint64_t g_irq7_count[32];
uint64_t arm7_irq_count(int irq) { return g_irq7_count[irq & 31]; }

void arm7_raise_irq(Cpu *c, int irq) {
    g_irq7_count[irq & 31]++;
    io7w(0x214, io7(0x214) | (1u << (irq & 31)));
    (void)c;
}

void arm7_check_irq(Cpu *c) { (void)c; }

/* Le vecteur d'interruption du BIOS du coprocesseur : il passe en mode IRQ, empile
   {r0-r3, r12, lr}, met dans lr l'adresse de son propre code de retour, saute au gestionnaire
   que le SDK a installé en fin de WRAM, puis dépile et revient au code interrompu. */
static void arm7_deliver(Cpu *c) {
    if (c->in_irq || c->irq_off || !irq7_deliverable()) return;
    uint32_t handler = mem_read32(c, ARM7_VECTOR);
    if (!handler) return;
    c->in_irq = 1;
    uint32_t cpsr = cpsr_read(c);
    uint32_t saved_pc = c->r[15];
    cpu_switch_mode(c, MODE_IRQ);
    c->spsr = cpsr;
    c->irq_off = 1;
    c->thumb = 0;
    c->r[14] = saved_pc + 4;
    uint32_t sp = c->r[13] - 24u;
    c->r[13] = sp;
    mem_write32(c, sp + 0, c->r[0]);   mem_write32(c, sp + 4, c->r[1]);
    mem_write32(c, sp + 8, c->r[2]);   mem_write32(c, sp + 12, c->r[3]);
    mem_write32(c, sp + 16, c->r[12]); mem_write32(c, sp + 20, c->r[14]);
    c->r[14] = ARM7_IRQ_RETURN;          /* le gestionnaire revient là, et la pile fantôme le sait */
    uint32_t depth = c->depth;
    c->irq_resumed = 0;
    /* Comme RT_CALL, mais sans le contrôle d'invariant de pile : le gestionnaire peut commuter
       de thread, et la fibre reprise a légitimement une autre pile. */
    if (c->depth < 512) c->shadow[c->depth] = ARM7_IRQ_RETURN;
    c->depth++;
    rt_call(c, handler);
    c->depth = depth;
    if (c->mode != MODE_IRQ) cpu_switch_mode(c, MODE_IRQ);
    sp = c->r[13];
    /* Si un thread a été commuté pendant le gestionnaire, cette fibre a été reprise avec ses
       propres registres : on ne dépile pas par-dessus. */
    if (!c->irq_resumed) {
        c->r[0] = mem_read32(c, sp + 0);  c->r[1] = mem_read32(c, sp + 4);
        c->r[2] = mem_read32(c, sp + 8);  c->r[3] = mem_read32(c, sp + 12);
        c->r[12] = mem_read32(c, sp + 16);
    }
    c->r[13] = sp + 24u;
    uint32_t restore = c->irq_resumed ? c->spsr : cpsr;
    c->irq_resumed = 0;
    cpsr_write(c, restore, 0xFu);
    c->in_irq = 0;
}

/* ------------------------------------------------------------------ minuteurs
 *
 * Quatre compteurs de seize bits. Chacun compte l'horloge divisée par son prédiviseur, sauf
 * s'il est en cascade (bit 2) : il compte alors les débordements du minuteur précédent. Le
 * pilote son s'en sert pour cadencer ses alarmes, et une paire en cascade lui donne une
 * période bien plus longue que seize bits — l'ignorer, c'est ne jamais le réveiller. */
static const uint32_t prescaler7[4] = {1, 64, 256, 1024};
/* `due` : la prochaine échéance connue. Sans elle, on refaisait deux divisions par minuteur à
 * chacun des treize mille appels d'horloge d'une image, pour n'y rien trouver la plupart du
 * temps. On ne recalcule donc que lorsque le moment est venu. */
static struct { uint32_t reload, control; uint64_t start, due; } g_timers7[4];

/* Combien de cycles entre deux débordements du minuteur `t`, cascade comprise. Zéro s'il est
 * arrêté, ou si sa chaîne de cascade l'est. */
static uint64_t timer7_span(int t) {
    uint64_t span = 1;
    for (int k = t; k >= 0; k--) {
        uint32_t ctl = g_timers7[k].control;
        if (!(ctl & 0x80)) return 0;
        span *= 0x10000ull - g_timers7[k].reload;
        if (!(ctl & 0x04)) return span * prescaler7[ctl & 3];
        if (k == 0) return 0;            /* le minuteur 0 ne peut pas être en cascade */
    }
    return 0;
}

uint32_t arm7_timer_read(uint32_t off) {
    int t = (int)((off - 0x100) / 4);
    uint32_t ctl = g_timers7[t].control;
    uint32_t cur = g_timers7[t].reload;
    if (ctl & 0x80) {
        /* Un minuteur en cascade avance d'une unité par débordement de son prédécesseur. */
        uint64_t unit = (ctl & 0x04) ? timer7_span(t - 1) : prescaler7[ctl & 3];
        uint64_t span = 0x10000ull - g_timers7[t].reload;
        if (unit && span) {
            uint64_t elapsed = (g_cycles7 - g_timers7[t].start) / unit;
            cur = (uint32_t)(g_timers7[t].reload + elapsed % span);
        }
    }
    return (cur & 0xFFFFu) | (ctl << 16);
}

void arm7_timer_write(Cpu *c, uint32_t off, uint32_t v) {
    (void)c;
    int t = (int)((off - 0x100) / 4);
    uint32_t ctl = (v >> 16) & 0xFFu;
    g_timers7[t].reload = v & 0xFFFFu;
    g_timers7[t].due = 0;
    if ((ctl & 0x80) && !(g_timers7[t].control & 0x80)) g_timers7[t].start = g_cycles7;
    g_timers7[t].control = ctl;
    g_timers7[t].due = 0;                                /* reprogrammé : l'échéance est à revoir */
    if (g_trace7) { static int n; if (n++ < 40)
        rt_log("minuteur %d : recharge %u, contrôle %02X (prédiviseur %u, cascade %u, irq %u) -> %llu cycles",
               t, g_timers7[t].reload, ctl, prescaler7[ctl & 3], (ctl >> 2) & 1u, (ctl >> 6) & 1u,
               (unsigned long long)timer7_span(t)); }
}

static void timers7_check(Cpu *c, uint64_t before) {
    for (int t = 0; t < 4; t++) {
        uint32_t ctl = g_timers7[t].control;
        if (!(ctl & 0x80) || !(ctl & 0x40)) continue;
        if (g_cycles7 < g_timers7[t].due) continue;      /* pas encore l'heure : rien à calculer */
        uint64_t span = timer7_span(t);
        if (!span) continue;
        uint64_t first = g_timers7[t].start + span;
        if (first > g_cycles7) { g_timers7[t].due = first; continue; }
        uint64_t k_before = before < first ? 0 : (before - first) / span + 1;
        uint64_t k_now = (g_cycles7 - first) / span + 1;
        if (k_now > k_before) arm7_raise_irq(c, IRQ7_TIMER0 + t);
        g_timers7[t].due = first + k_now * span;         /* prochaine échéance */
    }
}

/* ------------------------------------------------------------------ écran tactile et firmware */
uint32_t arm7_touch_channel(uint32_t chan) {
    /* Canaux du convertisseur : 1 la position Y, 5 la position X, 3 et 4 les deux mesures de
       pression dont le pilote déduit le contact. Ce sont des valeurs de convertisseur, pas des
       pixels : le pilote les repassera par la calibration qu'il a lue dans le firmware. */
    int down; uint32_t adc_x, adc_y;
    arm7_touch_adc(&down, &adc_x, &adc_y);
    if (g_trace7 && down) { static int n; if (n++ < 40)
        rt_log("tactile : canal %u, contact, adc (%u, %u)", chan, adc_x, adc_y); }
    switch (chan) {
    case 0:  return 0x800u;                     /* température : une valeur médiane suffit */
    case 1:  return down ? adc_y : 0xFFFu;      /* Y */
    case 2:  return 0xFFFu;                     /* tension de la pile : pleine */
    case 3:  return down ? 0x100u : 0x000u;     /* Z1 : nulle sans contact */
    case 4:  return down ? 0xF00u : 0xFFFu;     /* Z2 : saturée sans contact */
    case 5:  return down ? adc_x : 0x000u;      /* X */
    case 6:  return 0x000u;                     /* entrée auxiliaire : rien de branché */
    default: return 0x800u;
    }
}

/* Image de la puce de firmware : 256 Ko. Nous n'en avons pas, et il n'y a rien à en copier —
   le seul contenu dont le jeu a besoin est le bloc des réglages de l'utilisateur, que nous
   fabriquons de la même main que main.c le déposait en RAM. */
static uint8_t *g_firmware;
#define FIRMWARE_SIZE   0x40000u
#define FIRMWARE_USER   0x3FE00u

uint16_t bios_crc16(uint16_t crc, const uint8_t *p, size_t n);
static uint16_t fw_crc16(const uint8_t *p, size_t n) { return bios_crc16(0xFFFFu, p, n); }

static void firmware_build(void) {
    g_firmware = calloc(FIRMWARE_SIZE, 1);
    /* En-tête : le mot de l'offset 0x20 donne, en unités de huit octets, où commencent les
       réglages de l'utilisateur — c'est là que le pilote va chercher la calibration de l'écran
       tactile et la langue. */
    uint16_t part1 = (uint16_t)(FIRMWARE_USER / 8u);
    memcpy(g_firmware + 0x20, &part1, 2);
    memcpy(g_firmware + 0x22, &part1, 2);
    memcpy(g_firmware + 0x24, &part1, 2);
    memcpy(g_firmware + 0x26, &part1, 2);
    g_firmware[0x08] = 'M'; g_firmware[0x09] = 'A'; g_firmware[0x0A] = 'C';
    g_firmware[0x1D] = 0xFF;                      /* type de console : DS */

    uint8_t s[0x100];
    memset(s, 0, sizeof s);
    s[0x00] = 5;                                   /* version */
    s[0x02] = 3;                                   /* couleur favorite */
    s[0x03] = 1; s[0x04] = 1;                      /* anniversaire : 1er janvier */
    const char *nick = "Fox";
    for (int i = 0; nick[i]; i++) { s[0x06 + 2 * i] = (uint8_t)nick[i]; s[0x07 + 2 * i] = 0; }
    s[0x1A] = 3;                                   /* longueur du pseudo */
    s[0x50] = 0;                                   /* longueur du message */
    uint16_t adc_x1 = 0x0100, adc_y1 = 0x0100, adc_x2 = 0x0F00, adc_y2 = 0x0B80;
    memcpy(s + 0x58, &adc_x1, 2); memcpy(s + 0x5A, &adc_y1, 2); s[0x5C] = 16; s[0x5D] = 16;
    memcpy(s + 0x5E, &adc_x2, 2); memcpy(s + 0x60, &adc_y2, 2); s[0x62] = 240; s[0x63] = 176;
    /* La langue du jeu est celle de la console : il la lit dans ces réglages, il n'a pas de
       menu pour la choisir. SFC_LANG la fixe — les trois premiers bits de l'octet 0x64.
       Cette cartouche porte l'anglais, le français, l'allemand, l'espagnol et l'italien ;
       demander le japonais, absent, renverrait le jeu à l'anglais. */
    uint16_t langue = 2;                           /* français par défaut */
    const char *choix = getenv("SFC_LANG");
    if (choix && *choix) {
        static const struct { const char *nom; uint16_t code; } langues[] = {
            {"jp", 0}, {"en", 1}, {"fr", 2}, {"de", 3}, {"it", 4}, {"es", 5},
        };
        for (size_t i = 0; i < sizeof langues / sizeof *langues; i++)
            if (!strncmp(choix, langues[i].nom, 2)) { langue = langues[i].code; break; }
        rt_log("langue : %s (code %u)", choix, langue);
    }
    uint16_t flags = langue | (3 << 4) | (0 << 6);
    memcpy(s + 0x64, &flags, 2);
    uint16_t year = 6;                             /* 2006 */
    memcpy(s + 0x66, &year, 2);
    uint16_t count = 1;                            /* compteur de mises à jour : le plus grand gagne */
    memcpy(s + 0x70, &count, 2);
    uint16_t crc = fw_crc16(s, 0x70);
    memcpy(s + 0x72, &crc, 2);
    /* Les deux exemplaires que le pilote compare : le second plus vieux d'un cran. */
    memcpy(g_firmware + FIRMWARE_USER, s, 0x100);
    count = 0; memcpy(s + 0x70, &count, 2);
    crc = fw_crc16(s, 0x70); memcpy(s + 0x72, &crc, 2);
    memcpy(g_firmware + FIRMWARE_USER + 0x100, s, 0x100);
}

uint8_t arm7_firmware_byte(uint32_t addr) {
    if (!g_firmware) firmware_build();
    static int trace = -1;
    if (trace < 0) trace = getenv("SFC_TRACE_FIRMWARE") != NULL;
    if (trace) { static int n; if (n++ < 4000) rt_log("firmware : lecture 0x%05X", addr); }
    return g_firmware[addr & (FIRMWARE_SIZE - 1)];
}

/* ------------------------------------------------------------------ fibres */
static void arm7_yield(void) {
    g_in_arm7 = 0;
    SwitchToFiber(g_fiber9);
    g_in_arm7 = 1;
}

void arm7_halt(Cpu *c) {
    /* Sur la console, l'attente se lève dès qu'une interruption activée est en instance,
       que le masque global la laisse passer ou non ; le gestionnaire, lui, ne s'exécute
       qu'ensuite et seulement s'il a le droit. */
    g_halted = 1;
    unsigned guard = 0;
    while (!irq7_pending()) {
        if (++guard > 100000u) {
            rt_log("coprocesseur : attente sans interruption possible (IME=%u IE=%08X IF=%08X)",
                   io7(0x208) & 1u, io7(0x210), io7(0x214));
            g_halted = 0;
            return;
        }
        arm7_yield();
    }
    g_halted = 0;
    arm7_deliver(c);
}

uint64_t arm7_edges(void) { return g_instr7; }
uint64_t arm7_switches(void) { return g_switches; }

/* Attente active (WaitByLoop du BIOS) : elle consomme le temps du coprocesseur, pas celui de
   l'ARM9. Sans cela, ses boucles d'attente feraient défiler les images sans que personne ne
   travaille — l'horloge est à l'ARM9, le coprocesseur ne fait qu'y puiser. */
void arm7_spin(Cpu *c, uint64_t cycles) {
    (void)c;
    g_edge7 -= (int32_t)(cycles / ARM7_CYCLES_PER_EDGE) + 1;
    while (g_edge7 <= 0) arm7_yield();
}

/* SFC_PROFILE_ARM7=1 : profilage par échantillonnage du coprocesseur. À chaque tranche d'arêtes
   on relève l'appelant le plus récent ; les adresses qui reviennent le plus sont celles où il
   passe son temps. C'est la seule façon de savoir ce qu'il fait, faute de symboles. */
static struct { uint32_t addr; uint64_t hits; } g_prof7[512];
static int g_prof7_n;
static uint64_t g_prof7_depth_sum;
static unsigned g_prof7_depth_max;

static void prof7_sample(Cpu *c) {
    static int on = -1;
    if (on < 0) on = getenv("SFC_PROFILE_ARM7") != NULL;
    if (!on) return;
    /* Deux niveaux de pile : l'appelant seul ne dit pas quelle fonction consomme, puisque le
       temps est passé dans ce qu'elle appelle à son tour. */
    uint32_t a = c->depth ? c->shadow[c->depth - 1] : c->r[15];
    if (c->depth >= 2) a ^= c->shadow[c->depth - 2] << 1;
    for (int i = 0; i < g_prof7_n; i++)
        if (g_prof7[i].addr == a) {
            g_prof7[i].hits++;
            g_prof7_depth_sum += c->depth;
            if (c->depth > g_prof7_depth_max) g_prof7_depth_max = c->depth;
            return;
        }
    g_prof7_depth_sum += c->depth;
    if (c->depth > g_prof7_depth_max) g_prof7_depth_max = c->depth;
    if (g_prof7_n < 512) { g_prof7[g_prof7_n].addr = a; g_prof7[g_prof7_n].hits = 1; g_prof7_n++; }
}

void arm7_profile_report(void) {
    rt_log("coprocesseur : %llu passages de fibre, %llu arêtes",
           (unsigned long long)g_switches, (unsigned long long)g_instr7);
    if (!g_prof7_n) return;
    for (int i = 0; i < g_prof7_n; i++)
        for (int j = i + 1; j < g_prof7_n; j++)
            if (g_prof7[j].hits > g_prof7[i].hits) {
                uint32_t ta = g_prof7[i].addr; uint64_t th = g_prof7[i].hits;
                g_prof7[i] = g_prof7[j];
                g_prof7[j].addr = ta; g_prof7[j].hits = th;
            }
    uint64_t total = 0;
    for (int i = 0; i < g_prof7_n; i++) total += g_prof7[i].hits;
    rt_log("coprocesseur : où passe son temps (%llu relevés, %d adresses)",
           (unsigned long long)total, g_prof7_n);
    for (int i = 0; i < g_prof7_n && i < 12; i++)
        rt_log("   %08X : %5.1f %% (profondeur mêlée)", g_prof7[i].addr,
               100.0 * g_prof7[i].hits / total);
    rt_log("coprocesseur : profondeur moyenne %.1f, maximale %u",
           g_prof7_depth_sum / (double)(total ? total : 1), g_prof7_depth_max);
}

void arm7_edge(Cpu *c) {
    /* Le banc à froid essaie ses fonctions hors de sa fibre, horloge figée : ni interruption,
       ni bascule — le compte à rebours repart, comme pour la traduction qu'on lui compare. */
    if (g_temps_fige) { g_edge_budget = ARM7_EDGE_SLICE; return; }
    prof7_sample(c);
    /* Le compte à rebours du code généré est le même des deux côtés : chacun le remet pour
       lui-même, et la bascule de fibre le met de côté (voir arm7_tick). */
    g_edge_budget = ARM7_EDGE_SLICE;
    g_instr7 += ARM7_EDGE_SLICE;
    if (g_trace7 && (g_instr7 % 50000000ull) < ARM7_EDGE_SLICE)
        rt_log("coprocesseur : %llu arêtes, IME=%u IE=%08X IF=%08X",
               (unsigned long long)g_instr7, io7(0x208) & 1u, io7(0x210), io7(0x214));
    if (irq7_deliverable() && !c->irq_off && !c->in_irq) arm7_deliver(c);
    g_edge7 -= ARM7_EDGE_SLICE;
    if (g_edge7 > 0) return;
    arm7_yield();          /* son temps est consommé : l'ARM9 reprend, et lui en redonnera */
}

static void CALLBACK arm7_fiber_proc(void *param) {
    (void)param;
    Cpu *c = &g_cpu7;
    c->fiber = GetCurrentFiber();
    threads7_init(c);          /* ses threads vivent sur des fibres, comme ceux de l'ARM9 */
    rt_log("coprocesseur : démarrage à 0x%08X", g_entry7);
    rt_call(c, g_entry7);
    rt_log("coprocesseur : son point d'entrée a rendu la main (pc=0x%08X) ; mise en sommeil",
           c->r[15]);
    for (;;) arm7_yield();
}

/* ------------------------------------------------------------------ démarrage */
static uint32_t rd32(const uint8_t *p, size_t off) { uint32_t v; memcpy(&v, p + off, 4); return v; }

void arm7_boot(const uint8_t *rom, size_t rom_size, Cpu *c9) {
    (void)rom_size; (void)c9;
    if (!arm7_enabled()) return;
    uint32_t off = rd32(rom, 0x30), entry = rd32(rom, 0x34), addr = rd32(rom, 0x38), size = rd32(rom, 0x3C);
    Cpu *c = &g_cpu7;
    memset(c, 0, sizeof *c);
    c->is_arm7 = 1;
    c->mode = MODE_SVC;
    c->irq_off = 1;
    c->fiq_off = 1;
    /* Le firmware place ses piles en haut de sa WRAM avant de sauter au binaire de la carte. */
    c->r[13] = 0x0380FFC0u;
    c->bank_irq[0] = 0x0380FF80u;
    c->bank_svc[0] = 0x0380FFC0u;
    c->bank_usr[0] = 0x0380FF00u;
    c->r[15] = entry;
    g_entry7 = entry;
    /* Son binaire, à son adresse : la copie se fait par sa propre vue de la mémoire. */
    for (uint32_t i = 0; i < size; i++) mem_write8(c, addr + i, rom[off + i]);
    rt_log("coprocesseur : %u octets chargés en 0x%08X, entrée 0x%08X", size, addr, entry);
    snd_init();
    g_trace7 = getenv("SFC_TRACE_ARM7") != NULL;
    g_budget7 = ARM7_EDGE_SLICE;
    g_fiber7 = CreateFiber(1 << 20, arm7_fiber_proc, NULL);
    if (!g_fiber7) { rt_log("coprocesseur : impossible de créer sa fibre"); g_enabled = 0; return; }
    arm7_read_settings();
    g_started = 1;
}

/* ------------------------------------------------------------------ ordonnancement */
/* L'ARM9 vient de consommer `cycles` : le coprocesseur a droit au même temps. */
void arm7_tick(Cpu *c9, uint64_t cycles) {
    (void)c9;
    if (!g_started) return;
    uint64_t before = g_cycles7;
    g_cycles7 += cycles;
    timers7_check(&g_cpu7, before);
    g_edge7 += (int32_t)(cycles / ARM7_CYCLES_PER_EDGE);
    if (g_edge7 > ARM7_EDGE_MAX) g_edge7 = ARM7_EDGE_MAX;
    /* Rien à faire tant qu'il dort sans raison de se réveiller — et le temps de ce sommeil
       ne lui est pas dû : on ne le lui garde pas en réserve. */
    if (g_halted && !irq7_pending()) { g_edge7 = 0; return; }
    /* L'horloge avance par très petits pas — treize mille appels par image — et basculer de
     * fibre à chacun coûtait bien plus que le code du coprocesseur lui-même : deux changements
     * de contexte pour quelques arêtes. On attend donc qu'il ait de quoi travailler, sauf s'il
     * a une interruption à prendre, auquel cas il passe tout de suite. */
    if (g_edge7 < arm7_min_edges() && !irq7_pending()) return;
    if (g_edge7 <= 0) return;
    /* L'ARM9 nous cède la main depuis la fibre du thread qui tourne chez lui — laquelle change
       au gré de son ordonnanceur. C'est à celle-là qu'il faudra rendre la main. */
    g_switches++;
    g_fiber9 = GetCurrentFiber();
    int32_t saved = g_edge_budget;
    g_edge_budget = g_budget7;
    g_in_arm7 = 1;
    g_perturbations++;
    SwitchToFiber(g_fiber7);
    g_in_arm7 = 0;
    g_budget7 = g_edge_budget;
    g_edge_budget = saved;
}

/* Le balayage est commun aux deux processeurs, mais chacun décide des interruptions qu'il en
   tire : le coprocesseur cadence son échantillonnage de l'écran tactile sur une ligne précise,
   par l'interruption de comparaison de VCOUNT. Sans elle, il ne mesure jamais rien et le jeu
   attend un toucher qui n'arrive pas. */
void arm7_scanline(uint32_t line) {
    if (!g_started) return;
    /* Le son se mixe au fil du balayage, pas d'un bloc à la fin de l'image : le pilote change
       ses canaux plusieurs fois entre deux images. */
    snd_mix_cycles(&g_cpu7, 2130);

    uint32_t dispstat = mmio7_peek(0x004);
    if (dispstat & 0x10) arm7_raise_irq(&g_cpu7, IRQ7_HBLANK);
    uint32_t target = ((dispstat >> 8) & 0xFFu) | ((dispstat & 0x80u) << 1);
    if (line == target && (dispstat & 0x20)) arm7_raise_irq(&g_cpu7, IRQ7_VCOUNT);
}

void arm7_vblank(Cpu *c9) {
    if (!g_started) return;
    uint32_t dispstat = mmio7_peek(0x004);
    if (dispstat & 0x08) arm7_raise_irq(&g_cpu7, IRQ7_VBLANK);
    dma7_trigger(&g_cpu7, 1);
    snd_mix_frame(&g_cpu7);
    (void)c9;
}
