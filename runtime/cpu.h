/*
 * Modèle de processeur pour le code recompilé (ARM946E-S de la DS).
 *
 * Le code C généré par tools/recomp manipule cette structure : les seize
 * registres, les drapeaux à part (N, Z, C, V, Q), le mode et les banques de
 * registres. Les accès mémoire passent par mem_*, qui connaît la carte de la
 * DS (RAM principale, ITCM, DTCM, VRAM, registres matériels).
 *
 * Sous licence MIT. Ne contient rien du jeu.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- modes du processeur (bits 4..0 de cpsr) ---- */
enum {
    MODE_USR = 0x10, MODE_FIQ = 0x11, MODE_IRQ = 0x12, MODE_SVC = 0x13,
    MODE_ABT = 0x17, MODE_UND = 0x1B, MODE_SYS = 0x1F
};

typedef struct Cpu {
    uint32_t r[16];
    uint32_t nf, zf, cf, vf, qf;       /* drapeaux, 0 ou 1 */
    uint32_t mode;                     /* MODE_* */
    uint32_t irq_off, fiq_off;         /* bits I et F */
    uint32_t thumb;                    /* bit T : informatif */
    uint32_t spsr;                     /* spsr du mode courant (composé) */
    /* banques : r13, r14 (et spsr) des modes qui ne sont pas actifs */
    uint32_t bank_usr[2];
    uint32_t bank_irq[3];
    uint32_t bank_svc[3];
    uint32_t bank_abt[3];
    uint32_t bank_und[3];
    uint32_t bank_fiq[8];              /* r8..r14 + spsr */
    uint32_t usr_r8_r12[5];            /* r8..r12 hors FIQ */
    /* Pile fantôme des adresses de retour : un saut par registre vers le sommet
     * est un retour de fonction (ldr pc, [sp], #4 ; bx rN…), pas un appel. */
    uint32_t shadow[512];
    uint32_t depth;
    /* Redirection après appel : une primitive native (reprise de thread) demande
     * au code recompilé de continuer à cette adresse (étiquette locale). */
    uint32_t redirect;
    uint32_t in_irq;                   /* livraison d'interruption en cours dans ce contexte */
    uint32_t irq_resumed;              /* le gestionnaire s'est terminé par une reprise de thread */
    void *fiber;                       /* fibre hôte de ce contexte (threads.c) */
    void *rt;                          /* état de la couche d'exécution */
    /* Le contexte appartient au coprocesseur : il ne voit ni ITCM ni DTCM, sa WRAM de 64 Ko
     * répond en 0x03800000, et le même 0x04000000 ne désigne pas les mêmes registres. */
    uint32_t is_arm7;
} Cpu;

/* registres de la banque utilisateur vus depuis un mode privilégié (ldm/stm ^) */
uint32_t user_reg_read(Cpu *c, int n);
void     user_reg_write(Cpu *c, int n, uint32_t v);

/* appel d'une fonction recompilée avec suivi de l'adresse de retour */
extern int g_check_sp;                 /* SFC_CHECK_SP=1 : un appel doit rendre sp intact */
void rt_sp_mismatch(Cpu *c, uint32_t ret, uint32_t sp_before);
void rt_trop_profond(Cpu *c);          /* le banc à froid coupe une récursion sans fin */
#define RT_CALL(c, ret, call) do { \
    uint32_t rt_sp0_ = (c)->r[13]; \
    if ((c)->depth < 512) (c)->shadow[(c)->depth] = (ret); \
    if (++(c)->depth > 400) rt_trop_profond(c); \
    call; (c)->depth--; \
    if (g_check_sp && (c)->r[13] != rt_sp0_) rt_sp_mismatch((c), (ret), rt_sp0_); } while (0)

/* ---- mémoire (runtime/memory.c) ---- */
extern uint8_t *g_main_ram;            /* 4 Mo en 0x02000000 */
extern uint8_t *g_itcm;                /* 32 Ko en 0x01FF8000 (miroirs sous 0x02000000) */
extern uint8_t *g_dtcm;                /* 16 Ko en 0x027E0000 */

uint32_t mem_read32_slow(Cpu *c, uint32_t addr);
uint16_t mem_read16_slow(Cpu *c, uint32_t addr);
uint8_t  mem_read8_slow(Cpu *c, uint32_t addr);
void     mem_write32_slow(Cpu *c, uint32_t addr, uint32_t v);
void     mem_write16_slow(Cpu *c, uint32_t addr, uint16_t v);
void     mem_write8_slow(Cpu *c, uint32_t addr, uint8_t v);

/* Ce que les accès en ligne lisent et écrivent directement : la RAM principale (4 Mo en 0x02000000),
 * commune aux deux processeurs, et la mémoire où vit la pile de chacun — la DTCM de l'ARM9 (16 Ko en
 * 0x027E0000 ; le coprocesseur, qui n'en a pas, y lit la RAM), la WRAM propre du coprocesseur (64 Ko
 * en 0x03800000, en miroir jusqu'à 0x03FFFFFF). La pile est remplie et vidée à chaque appel : c'était
 * la moitié des passages par memory.c, qui y rendait exactement ces mêmes octets. */
extern uint8_t *g_wram7;
static inline uint8_t *mem_fast_ptr(const Cpu *c, uint32_t addr) {
    uint32_t off = addr - 0x02000000u;
    if (off < 0x00400000u) return g_main_ram + off;
    if (!c->is_arm7) return (addr & 0xFFFFC000u) == 0x027E0000u ? g_dtcm + (addr & 0x3FFFu) : NULL;
    return (addr & 0xFF800000u) == 0x03800000u ? g_wram7 + (addr & 0xFFFFu) : NULL;
}

static inline uint32_t mem_read32(Cpu *c, uint32_t addr) {
    uint8_t *p = mem_fast_ptr(c, addr & ~3u);
    if (p) { uint32_t v; __builtin_memcpy(&v, p, 4); return v; }
    return mem_read32_slow(c, addr & ~3u);
}
static inline uint16_t mem_read16(Cpu *c, uint32_t addr) {
    uint8_t *p = mem_fast_ptr(c, addr & ~1u);
    if (p) { uint16_t v; __builtin_memcpy(&v, p, 2); return v; }
    return mem_read16_slow(c, addr & ~1u);
}
static inline uint8_t mem_read8(Cpu *c, uint32_t addr) {
    uint8_t *p = mem_fast_ptr(c, addr);
    if (p) return *p;
    return mem_read8_slow(c, addr);
}
/* SFC_WATCH_VALUE=0x… : qui écrit cette valeur ? (hors banc différentiel, qui ne lie pas rt.c) */
#ifdef SFC_NO_WATCH_VALUE
#define SFC_CHECK_WATCH_VALUE(c, addr, v) ((void)0)
#else
extern uint32_t g_watch_value;
void rt_value_written(Cpu *c, uint32_t addr, uint32_t v);
#define SFC_CHECK_WATCH_VALUE(c, addr, v)     do { if (g_watch_value && (v) == g_watch_value) rt_value_written((c), (addr), (v)); } while (0)
#endif
/* SFC_SURVEILLE_ADR=0x… : qui écrit ce mot ? (la pile de la machine hôte, symbolisée par tools/echantillons.py) */
extern uint32_t g_adr_surveillee;
void rt_adresse_ecrite(Cpu *c, uint32_t addr, uint32_t v);

static inline void mem_write32(Cpu *c, uint32_t addr, uint32_t v) {
    SFC_CHECK_WATCH_VALUE(c, addr & ~3u, v);
    if (__builtin_expect(g_adr_surveillee == (addr & ~3u), 0)) rt_adresse_ecrite(c, addr & ~3u, v);
    uint8_t *p = mem_fast_ptr(c, addr & ~3u);
    if (p) { __builtin_memcpy(p, &v, 4); return; }
    mem_write32_slow(c, addr & ~3u, v);
}
static inline void mem_write16(Cpu *c, uint32_t addr, uint16_t v) {
    uint8_t *p = mem_fast_ptr(c, addr & ~1u);
    if (p) { __builtin_memcpy(p, &v, 2); return; }
    mem_write16_slow(c, addr & ~1u, v);
}
static inline void mem_write8(Cpu *c, uint32_t addr, uint8_t v) {
    uint8_t *p = mem_fast_ptr(c, addr);
    if (p) { *p = v; return; }
    mem_write8_slow(c, addr, v);
}

/* Les écritures qui repartent de leur propre valeur — x = x + v, un compteur, une position qu'on intègre —,
 * marquées par le générateur (tools/decomp_feuilles.py). Pendant un tour d'aperçu (runtime/apercu.c) leur
 * pas est mis à l'échelle de la part de tour qu'on montre ; le reste du temps elles sont l'écriture même. */
extern int g_echelle_active;             /* 1 : aperçu (mise à l'échelle) ; 2 : étude des sites */
uint32_t echelle_acc(uint32_t vieux, uint32_t neuf, unsigned bits, uint32_t ea, uint32_t site);
#define ECRIT_ACC32(c, ea, v, vieux, site) \
    mem_write32((c), (ea), __builtin_expect(g_echelle_active, 0) ? echelle_acc((vieux), (v), 32, (ea), (site)) : (uint32_t)(v))
#define ECRIT_ACC16(c, ea, v, vieux, site) \
    mem_write16((c), (ea), __builtin_expect(g_echelle_active, 0) ? (uint16_t)echelle_acc((vieux), (v), 16, (ea), (site)) : (uint16_t)(v))
#define ECRIT_ACC8(c, ea, v, vieux, site) \
    mem_write8((c), (ea), __builtin_expect(g_echelle_active, 0) ? (uint8_t)echelle_acc((vieux), (v), 8, (ea), (site)) : (uint8_t)(v))
/* La même chose quand la lecture et l'écriture passent par deux pointeurs — VEC_Add(&pos, &v, &pos) — :
 * ce n'est une accumulation que si, à l'exécution, les deux adresses sont la même. Deux lectures candidates. */
static inline uint32_t acc_dyn(uint32_t ea, uint32_t v, uint32_t t1, uint32_t a1, uint32_t t2, uint32_t a2,
                               unsigned bits, uint32_t site) {
    if (ea == a1) return echelle_acc(t1, v, bits, ea, site);
    if (ea == a2) return echelle_acc(t2, v, bits, ea, site);
    return v;
}
#define ECRIT_ACCD32(c, ea, v, t1, a1, t2, a2, site) do { uint32_t ea_ = (ea); \
    mem_write32((c), ea_, __builtin_expect(g_echelle_active, 0) ? acc_dyn(ea_, (v), (t1), (a1), (t2), (a2), 32, (site)) : (uint32_t)(v)); } while (0)
#define ECRIT_ACCD16(c, ea, v, t1, a1, t2, a2, site) do { uint32_t ea_ = (ea); \
    mem_write16((c), ea_, __builtin_expect(g_echelle_active, 0) ? (uint16_t)acc_dyn(ea_, (v), (t1), (a1), (t2), (a2), 16, (site)) : (uint16_t)(v)); } while (0)
#define ECRIT_ACCD8(c, ea, v, t1, a1, t2, a2, site) do { uint32_t ea_ = (ea); \
    mem_write8((c), ea_, __builtin_expect(g_echelle_active, 0) ? (uint8_t)acc_dyn(ea_, (v), (t1), (a1), (t2), (a2), 8, (site)) : (uint8_t)(v)); } while (0)

/* ldr non aligné : le mot aligné tourné (ARM9) */
static inline uint32_t ror32(uint32_t v, unsigned n) {
    n &= 31u;
    return n ? ((v >> n) | (v << (32u - n))) : v;
}
static inline uint32_t ldr_word(Cpu *c, uint32_t addr) {
    return ror32(mem_read32(c, addr), (addr & 3u) * 8u);
}
static inline uint32_t ldr_sh(Cpu *c, uint32_t addr) {
    /* ldrsh non aligné sur ARM9 : octet signé */
    if (addr & 1u) return (uint32_t)(int32_t)(int8_t)mem_read8(c, addr);
    return (uint32_t)(int32_t)(int16_t)mem_read16(c, addr);
}
static inline uint32_t ldr_h(Cpu *c, uint32_t addr) {
    /* ldrh non aligné : demi-mot aligné tourné de 8 (ARM9) */
    uint32_t v = mem_read16(c, addr);
    return (addr & 1u) ? ror32(v, 8) : v;
}

/* ---- drapeaux ---- */
#define SET_NZ(c, res) do { (c)->nf = (uint32_t)(res) >> 31; (c)->zf = ((uint32_t)(res) == 0u); } while (0)

static inline void flags_add(Cpu *c, uint32_t a, uint32_t b, uint32_t cin, uint32_t res) {
    uint64_t wide = (uint64_t)a + (uint64_t)b + (uint64_t)cin;
    c->cf = (uint32_t)(wide >> 32);
    c->vf = (((a ^ res) & (b ^ res)) >> 31) & 1u;
    SET_NZ(c, res);
}
static inline void flags_sub(Cpu *c, uint32_t a, uint32_t b, uint32_t cin, uint32_t res) {
    /* res = a - b - (1 - cin) ; C = pas d'emprunt */
    uint64_t wide = (uint64_t)a - (uint64_t)b - (uint64_t)(1u - cin);
    c->cf = ((wide >> 32) & 1u) ^ 1u;
    c->vf = (((a ^ b) & (a ^ res)) >> 31) & 1u;
    SET_NZ(c, res);
}

/* ---- shifter par registre (montant = 8 bits bas de rs) ---- */
static inline uint32_t sh_lsl_reg(uint32_t v, uint32_t s, uint32_t *cout) {
    s &= 0xFFu;
    if (s == 0) return v;
    if (s < 32) { *cout = (v >> (32u - s)) & 1u; return v << s; }
    if (s == 32) { *cout = v & 1u; return 0; }
    *cout = 0; return 0;
}
static inline uint32_t sh_lsr_reg(uint32_t v, uint32_t s, uint32_t *cout) {
    s &= 0xFFu;
    if (s == 0) return v;
    if (s < 32) { *cout = (v >> (s - 1u)) & 1u; return v >> s; }
    if (s == 32) { *cout = v >> 31; return 0; }
    *cout = 0; return 0;
}
static inline uint32_t sh_asr_reg(uint32_t v, uint32_t s, uint32_t *cout) {
    s &= 0xFFu;
    if (s == 0) return v;
    if (s < 32) { *cout = (v >> (s - 1u)) & 1u; return (uint32_t)((int32_t)v >> s); }
    *cout = v >> 31; return (uint32_t)((int32_t)v >> 31);
}
static inline uint32_t sh_ror_reg(uint32_t v, uint32_t s, uint32_t *cout) {
    s &= 0xFFu;
    if (s == 0) return v;
    s &= 31u;
    if (s == 0) { *cout = v >> 31; return v; }
    *cout = (v >> (s - 1u)) & 1u;
    return ror32(v, s);
}

/* ---- saturation (Q) ---- */
static inline uint32_t sat_add(Cpu *c, uint32_t a, uint32_t b) {
    int64_t s = (int64_t)(int32_t)a + (int64_t)(int32_t)b;
    if (s > INT32_MAX) { c->qf = 1; return 0x7FFFFFFFu; }
    if (s < INT32_MIN) { c->qf = 1; return 0x80000000u; }
    return (uint32_t)s;
}
static inline uint32_t sat_sub(Cpu *c, uint32_t a, uint32_t b) {
    int64_t s = (int64_t)(int32_t)a - (int64_t)(int32_t)b;
    if (s > INT32_MAX) { c->qf = 1; return 0x7FFFFFFFu; }
    if (s < INT32_MIN) { c->qf = 1; return 0x80000000u; }
    return (uint32_t)s;
}
static inline uint32_t sat_double(Cpu *c, uint32_t a) { return sat_add(c, a, a); }
/* addition qui lève Q en cas de débordement signé mais ne sature pas (smla) */
static inline uint32_t add_setq(Cpu *c, uint32_t a, uint32_t b) {
    uint32_t res = a + b;
    if ((((a ^ res) & (b ^ res)) >> 31) & 1u) c->qf = 1;
    return res;
}

static inline uint32_t clz32(uint32_t v) { return v ? (uint32_t)__builtin_clz(v) : 32u; }

/* ---- couche d'exécution (runtime/rt.c) ---- */
void     rt_call(Cpu *c, uint32_t target);            /* appel indirect : bit 0 = Thumb */
void     rt_jump(Cpu *c, uint32_t target);            /* saut indirect (appel de queue) */
void     rt_return_from_exception(Cpu *c);            /* subs pc, lr, #4 / ldm ^ avec pc */
void     rt_swi(Cpu *c, uint32_t n);
void     rt_bkpt(Cpu *c, uint32_t imm, uint32_t addr);
void     rt_undefined(Cpu *c, uint32_t addr, uint32_t raw);
void     rt_unlabeled(Cpu *c, uint32_t entry);        /* entrée dans une fonction sans étiquette à cette adresse */
void     rt_cp15_write(Cpu *c, uint32_t opc1, uint32_t crn, uint32_t crm, uint32_t opc2, uint32_t v);
uint32_t rt_cp15_read(Cpu *c, uint32_t opc1, uint32_t crn, uint32_t crm, uint32_t opc2);
uint32_t cpsr_read(Cpu *c);
void     cpsr_write(Cpu *c, uint32_t v, uint32_t fields);   /* fields : bit0 c, bit1 x, bit2 s, bit3 f */
uint32_t spsr_read(Cpu *c);
void     spsr_write(Cpu *c, uint32_t v, uint32_t fields);
void     cpu_switch_mode(Cpu *c, uint32_t mode);
void     rt_trace(Cpu *c, uint32_t addr);

#ifdef RT_TRACE
#define PC(c, a) rt_trace((c), (a))
#else
#define PC(c, a) ((void)0)
#endif

/* Interruption en attente : testé aux arêtes arrière des boucles du code généré. */
extern volatile uint32_t g_irq_line;
void rt_irq_check(Cpu *c);
/* Budget d'arêtes arrière : décrémenté par le code généré à chaque boucle ; à zéro, `rt_edge`
   fait avancer le temps virtuel (le calcul consomme du temps, les attentes actives aboutissent)
   et livre les interruptions en attente. Une interruption qui survient le met à zéro. */
/* Un `pop {pc}` est presque toujours un vrai retour ; il arrive pourtant qu'il serve de saut,
 * quand une petite fonction intermédiaire passe la main à une autre. On les distingue par la
 * pile fantôme. Le test tient ici pour que le cas courant — le retour — n'ait pas à passer par
 * un appel de fonction : il est fait des millions de fois par image. */
static inline int rt_is_return(const Cpu *c, uint32_t target) {
    return c->depth > 0 && c->depth <= 512 && (c->shadow[c->depth - 1] & ~1u) == (target & ~1u);
}

extern uint32_t g_hook_entree;         /* l'adresse que remplace le crochet en train d'être appelé */
/* Option de construction SFC_COMPTE_TRADUIT : chaque fonction traduite de l'ARM9 compte ses
   exécutions — [0] par une entrée en son milieu, [1] par son entrée. SFC_TRADUIT=fichier les rend. */
#ifdef SFC_COMPTE_TRADUIT
extern uint32_t g_traduit[][2];
void rt_traduit_milieu(uint32_t adresse);          /* par où l'on entre, quand ce n'est pas par l'entrée */
#define RT_TRADUIT(i, propre) do { if (propre) g_traduit[i][1]++; else { g_traduit[i][0]++; rt_traduit_milieu(entry); } } while (0)
extern uint32_t g_traduit7[][2];                   /* les mêmes comptes, côté coprocesseur */
void rt_traduit_milieu7(uint32_t adresse);
#define RT_TRADUIT7(i, propre) do { if (propre) g_traduit7[i][1]++; else { g_traduit7[i][0]++; rt_traduit_milieu7(entry); } } while (0)
#else
#define RT_TRADUIT(i, propre) ((void)0)
#define RT_TRADUIT7(i, propre) ((void)0)
#endif
extern volatile int32_t g_edge_budget;
void rt_edge(Cpu *c);

/* Signature d'une fonction recompilée : entrée = adresse du bloc où entrer. */
typedef void (*RecompFn)(Cpu *c, uint32_t entry);

#ifdef __cplusplus
}
#endif
