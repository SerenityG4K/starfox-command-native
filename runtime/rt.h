/* Couche d'exécution : interface interne. Sous licence MIT. */
#pragma once
#include "cpu.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Table de dispatch générée : adresse -> fonction recompilée (generated/tables.c) */
typedef struct { uint32_t start, end, entry; RecompFn fn; const char *region; uint8_t thumb; } RecompEntry;
extern const RecompEntry g_recomp_table[];
extern const size_t g_recomp_table_size;
typedef void (*NativeHook)(Cpu *c);
extern NativeHook g_fn_hooks[];        /* un remplacement natif par fonction générée (même ordre) */
#ifdef SFC_HAVE_ARM7
/* La traduction du coprocesseur, liée à côté de celle de l'ARM9 (generated7, suffixe 7). */
extern const RecompEntry g_recomp7_table[];
extern const size_t g_recomp7_table_size;
extern NativeHook g_fn7_hooks[];
#endif

/* Overlays : le jeu en charge un par emplacement ; plusieurs se partagent les mêmes adresses. */
typedef struct {
    int id;
    uint32_t ram_address, ram_size, bss_size;
    const uint8_t *image;              /* octets de l'overlay (pour reconnaître lequel est en RAM) */
    uint32_t rom_start, rom_end;       /* son étendue dans la cartouche */
} OverlayInfo;

void rt_init(Cpu *c);
void rt_set_overlays(const OverlayInfo *overlays, size_t count);
const RecompEntry *rt_find(Cpu *c, uint32_t addr);       /* fonction contenant addr (bit 0 : Thumb), ou NULL */
int  rt_overlay_loaded(uint32_t addr);                    /* id de l'overlay présent en RAM à addr, ou -1 */
int  rt_overlay_at_rom(uint32_t offset);                  /* id de l'overlay qui commence à cet offset de cartouche, ou -1 */
void rt_sortir(int code);                                 /* exit, le fil d'affichage arrêté d'abord */

/* Crochets natifs : remplacent une fonction recompilée (HLE du SDK, code décompilé) */
void rt_diag_hooks(void);   /* SFC_DIAG_ANNEAU=1 : diagnostic du passage d'anneau */
void rt_hook(uint32_t addr, NativeHook fn, const char *name);
void rt_hook7(uint32_t addr, NativeHook fn, const char *name);   /* côté coprocesseur */
NativeHook rt_hook_for(uint32_t addr);
NativeHook *rt_hook_region(const char *region, uint32_t addr, NativeHook fn);  /* un crochet pour un seul overlay */
const RecompEntry *rt_entree_region(const char *region, uint32_t addr);        /* sa fonction traduite */
NativeHook rt_hook_present(const char *region, uint32_t addr);                  /* le crochet que le runtime y a déjà mis */
size_t rt_hook_emplacements(uint32_t addr, NativeHook **out, size_t max);  /* pour ôter un crochet le temps d'un appel */
/* Compte ce qui, pendant qu'une fonction tourne, fait bouger le monde autour d'elle : une
   interruption livrée, un changement de thread, un passage au coprocesseur. Le banc en jeu ne
   compare que les exécutions où rien de tout cela n'est arrivé : remettre la mémoire en arrière
   après coup casserait ce qui a avancé entre-temps. */
extern volatile uint64_t g_perturbations;
extern int g_temps_fige;                                  /* le banc à froid arrête l'horloge */
#include <setjmp.h>
extern jmp_buf g_essai_reprise;                           /* le banc à froid abandonne un essai interminable */
extern uint64_t g_essai_aretes, g_essai_aretes_max;
extern uint64_t g_acces_mmio;                             /* accès aux registres matériels (mmio.c) */
extern uint32_t (*g_mmio_bouchon)(Cpu *c, uint32_t addr, uint32_t valeur, int octets, int ecriture);
extern int g_jump_intercepte;                             /* le banc note le prochain saut sans le suivre */
extern uint32_t g_jump_cible, g_jump_profondeur;
void rt_laisser_passer(Cpu *c, uint64_t cycles);         /* du temps simulé, sans code du jeu */
void rt_appel_programme(Cpu *c);                          /* SFC_APPEL */
void natif_init(void);                                    /* réécritures natives (natif.c) */
void natif_rapport(void);
void natif_banc_froid(Cpu *c);                            /* SFC_VERIF_FROID (natif.c) */
int rt_overlay_installer(const char *region);             /* pose l'image d'un overlay en mémoire */
void rt_bouchon_partout(NativeHook fn);                   /* le banc à froid remplace toutes les appelées */
extern NativeHook g_bouchon;
/* Un saut dynamique au milieu d'une fonction : rend 1 si une réécriture secondaire l'a pris. */
extern int (*g_dispatch_milieu)(Cpu *c, const RecompEntry *e, uint32_t adresse);

/* Journal */
void rt_log(const char *fmt, ...);
void rt_fatal(Cpu *c, const char *fmt, ...);
void rt_dump_stack(Cpu *c);

/* Chien de garde : sans progression (attente d'interruption) pendant N secondes, on s'arrête
   en imprimant la pile fantôme du contexte courant. */
void rt_watchdog_start(unsigned seconds);
void rt_watchdog_kick(Cpu *c);
void rt_watchdog_set_cpu(Cpu *c);
/* Sans progression, le chien de garde lève ce drapeau et force `g_irq_line` : le thread bloqué
   imprime lui-même son état à sa prochaine arête arrière (état cohérent) et s'arrête. */
extern volatile int g_stuck_dump;
void rt_stuck_report(Cpu *c);

#ifdef __cplusplus
}
#endif
