/*
 * IPC entre l'ARM9 et l'ARM7 : IPCSYNC et les deux FIFO.
 *
 * Deux coprocesseurs possibles au bout du fil. Par défaut arm7_hle.c tient son
 * rôle de l'extérieur : il répond aux messages du protocole PXI du NitroSDK
 * sans exécuter une instruction du binaire. Avec SFC_ARM7=1, c'est le vrai
 * coprocesseur traduit qui est là, et la liaison redevient ce qu'elle est sur
 * la console : deux files de seize mots et deux moitiés de registre de
 * synchronisation, chacun lisant ce que l'autre a écrit.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include "arm7.h"
#include "arm7_run.h"
#include "threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t g_sync9;            /* IPCSYNC vu de l'ARM9 : bits 0-3 entrée (ARM7), 8-11 sortie */
static uint32_t g_sync7_out;        /* ce que l'ARM7 présente */
static uint32_t g_fifocnt9 = 0x0101;
static uint32_t g_fifocnt7 = 0x0101;

#define FIFO_DEPTH 16
typedef struct { uint32_t data[FIFO_DEPTH]; uint32_t head, count; } Fifo;
extern int g_apercu_en_cours;           /* un tour d'aperçu n'éveille pas le coprocesseur (natif.c) */
static Fifo g_to7, g_to9;            /* ARM9 -> ARM7, ARM7 -> ARM9 */
static uint32_t g_last_recv, g_last_recv7;

/* SFC_TRACE_IPC=1 : chaque mot qui passe, et les valeurs de synchronisation. Le protocole du
   SDK met l'étiquette du sous-système dans les cinq bits bas et la donnée à partir du bit 6. */
static int ipc_trace(void) {
    static int t = -1;
    if (t < 0) t = getenv("SFC_TRACE_IPC") != NULL;
    return t;
}

/* Combien de mots ont traversé, et combien attendent : un blocage entre les deux processeurs
   se lit d'abord là. */
static uint64_t g_sent9, g_sent7, g_full7;
void ipc_stats(uint64_t *sent9, uint64_t *sent7, uint64_t *full7, unsigned *queued9, unsigned *queued7);

static int fifo_push(Fifo *f, uint32_t v) {
    if (f->count >= FIFO_DEPTH) return 0;
    f->data[(f->head + f->count) % FIFO_DEPTH] = v;
    f->count++;
    return 1;
}
static int fifo_pop(Fifo *f, uint32_t *v) {
    if (f->count == 0) return 0;
    *v = f->data[f->head];
    f->head = (f->head + 1) % FIFO_DEPTH;
    f->count--;
    return 1;
}

/* ------------------------------------------------------------------ côté ARM9 */
uint32_t ipc_read_sync(Cpu *c) {
    (void)c;
    return (g_sync7_out & 0xF) | (g_sync9 & 0x4F00);
}

void ipc_write_sync(Cpu *c, uint32_t v) {
    if (ipc_trace() && ((v ^ g_sync9) & 0x4F00u))
        rt_log("IPCSYNC 9 : sortie %u (irq vers 7 : %u)", (v >> 8) & 0xFu, (v >> 13) & 1u);
    g_sync9 = (g_sync9 & 0xF) | (v & 0x4F00);
    if (arm7_enabled()) {
        /* Le coprocesseur lit ces quatre bits dans les siens, et l'ARM9 peut lui demander
           une interruption par le bit 13. */
        if ((v & 0x2000) && (mmio7_peek(0x180) & 0x4000) && !g_apercu_en_cours) arm7_raise_irq(arm7_cpu(), IRQ7_IPC_SYNC);
        return;
    }
    arm7_on_sync(c, (v >> 8) & 0xF);
    if (v & 0x2000) arm7_on_sync_irq(c);
}

void ipc_arm7_set_sync(Cpu *c, uint32_t v) {
    g_sync7_out = v & 0xF;
    (void)c;
}

uint32_t ipc_read_fifocnt(Cpu *c) {
    (void)c;
    uint32_t v = g_fifocnt9 & 0xC404;      /* bit 14 compris : l'erreur se lit */
    if (g_to7.count == 0) v |= 0x0001;          /* send fifo vide */
    if (g_to7.count >= FIFO_DEPTH) v |= 0x0002; /* send fifo pleine */
    if (g_to9.count == 0) v |= 0x0100;          /* recv fifo vide */
    if (g_to9.count >= FIFO_DEPTH) v |= 0x0200;
    return v;
}

void ipc_write_fifocnt(Cpu *c, uint32_t v) {
    if (v & 0x0008) { g_to7.count = 0; g_to7.head = 0; }   /* vide la fifo d'envoi */
    if (v & 0x4000) g_fifocnt9 &= ~0x4000u;                /* acquitte l'erreur */
    g_fifocnt9 = (g_fifocnt9 & ~0x8404u) | (v & 0x8404u);
    /* IRQ « send fifo vide » : levée aussitôt si activée et vide */
    if ((v & 0x0004) && g_to7.count == 0) hw_raise_irq(c, IRQ_IPC_SEND_EMPTY);
}

/* SFC_TRACE_BACKUP=1 : les seules requêtes de cartouche, sans noyer le journal. Le composant
   carte du SDK porte l'étiquette 11 ; c'est par là que passent lectures et écritures de la
   puce de sauvegarde. */
static int card_trace(void) {
    static int t = -1;
    if (t < 0) t = getenv("SFC_TRACE_BACKUP") != NULL;
    return t;
}

void ipc_write_fifosend(Cpu *c, uint32_t v) {
    if (ipc_trace()) rt_log("IPC 9->7 : %08X (étiquette %u, drapeau %u, donnée %08X)",
                            v, v & 0x1Fu, (v >> 5) & 1u, v >> 6);
    if (card_trace() && (v & 0x1Fu) == 11)
        rt_log("carte : image %llu, requête de l'ARM9 : donnée %08X",
               (unsigned long long)hw_frames(), v >> 6);
    if (!(g_fifocnt9 & 0x8000)) return;
    if (!fifo_push(&g_to7, v)) { g_fifocnt9 |= 0x4000; return; }
    g_sent9++;
    if (arm7_enabled()) {
        /* le coprocesseur viendra la lire : on lui signale seulement que sa file s'est remplie */
        if ((g_fifocnt7 & 0x0400) && !g_apercu_en_cours) arm7_raise_irq(arm7_cpu(), IRQ7_IPC_RECV);
    } else {
        uint32_t w;
        /* Un tour d'aperçu ne doit rien laisser derrière lui. Or ce chemin-ci sert le coprocesseur
           simulé sur-le-champ : une requête de carte écrit en mémoire, une liste de commandes de son
           change l'état du mixeur — et rien de cela n'est repris quand l'aperçu est rendu. On compte
           d'abord ce que le jeu envoie pendant un aperçu, par étiquette, avant d'en tirer une garde. */
        if (g_apercu_en_cours) {
            static uint64_t par_etiquette[32], total;
            par_etiquette[v & 0x1Fu]++;
            if (++total % 256 == 1) {
                char liste[256]; int n = 0;
                for (int t = 0; t < 32; t++)
                    if (par_etiquette[t]) n += snprintf(liste + n, sizeof liste - (size_t)n, " %d:%llu", t,
                                                        (unsigned long long)par_etiquette[t]);
                rt_log("aperçu : %llu messages au coprocesseur simulé (étiquette:compte)%s",
                       (unsigned long long)total, liste);
            }
            /* et on s'arrête là : servir le coprocesseur simulé pendant un aperçu écrirait en mémoire
               (une requête de carte) ou changerait l'état du mixeur, que la restitution ne reprend pas */
            { extern void apercu_abandon(int); apercu_abandon(6); }
        }
        while (fifo_pop(&g_to7, &w)) arm7_on_fifo(c, w);
    }
    if (g_fifocnt9 & 0x0004) hw_raise_irq(c, IRQ_IPC_SEND_EMPTY);
}

uint32_t ipc_read_fiforecv(Cpu *c) {
    (void)c;
    uint32_t v;
    if (!(g_fifocnt9 & 0x8000)) return g_last_recv;
    if (!fifo_pop(&g_to9, &v)) {
        g_fifocnt9 |= 0x4000;
        if (ipc_trace()) rt_log("IPC 9 lit à vide (erreur levée)");
        return g_last_recv;
    }
    if (ipc_trace()) rt_log("IPC 9 lit : %08X (étiquette %u, reste %u)", v, v & 0x1Fu, g_to9.count);
    g_last_recv = v;
    if (arm7_enabled() && g_to9.count == 0 && (g_fifocnt7 & 0x0004))
        if (!g_apercu_en_cours) arm7_raise_irq(arm7_cpu(), IRQ7_IPC_SEND_EMPTY);
    return v;
}

void ipc_arm7_send(Cpu *c, uint32_t v) {
    if (ipc_trace()) rt_log("IPC 7->9 (simulé) : %08X (étiquette %u)", v, v & 0x1Fu);
    if (!fifo_push(&g_to9, v)) { rt_log("IPC : fifo vers l'ARM9 pleine, mot perdu 0x%08X", v); return; }
    if (g_fifocnt9 & 0x0400) hw_raise_irq(c, IRQ_IPC_RECV);
}

/* ------------------------------------------------------------------ côté coprocesseur */
uint32_t ipc7_read_sync(Cpu *c) {
    (void)c;
    /* Il lit en bits 0-3 ce que l'ARM9 présente, et relit ses propres bits 8-11. */
    return ((g_sync9 >> 8) & 0xF) | (mmio7_peek(0x180) & 0x4F00);
}

void ipc7_write_sync(Cpu *c, uint32_t v) {
    (void)c;
    if (ipc_trace() && ((v ^ mmio7_peek(0x180)) & 0x4F00u))
        rt_log("IPCSYNC 7 : sortie %u (irq vers 9 : %u)", (v >> 8) & 0xFu, (v >> 13) & 1u);
    mmio7_poke(0x180, v & 0x4F00);
    g_sync7_out = (v >> 8) & 0xF;
    if ((v & 0x2000) && (g_sync9 & 0x4000)) hw_raise_irq(rt_main_cpu(), IRQ_IPC_SYNC);
}

uint32_t ipc7_read_fifocnt(Cpu *c) {
    (void)c;
    uint32_t v = g_fifocnt7 & 0xC404;
    if (g_to9.count == 0) v |= 0x0001;
    if (g_to9.count >= FIFO_DEPTH) v |= 0x0002;
    if (g_to7.count == 0) v |= 0x0100;
    if (g_to7.count >= FIFO_DEPTH) v |= 0x0200;
    return v;
}

void ipc7_write_fifocnt(Cpu *c, uint32_t v) {
    if (v & 0x0008) { g_to9.count = 0; g_to9.head = 0; }
    if (v & 0x4000) g_fifocnt7 &= ~0x4000u;
    g_fifocnt7 = (g_fifocnt7 & ~0x8404u) | (v & 0x8404u);
    if ((v & 0x0004) && g_to9.count == 0) arm7_raise_irq(c, IRQ7_IPC_SEND_EMPTY);
}

void ipc_stats(uint64_t *sent9, uint64_t *sent7, uint64_t *full7, unsigned *queued9, unsigned *queued7) {
    *sent9 = g_sent9; *sent7 = g_sent7; *full7 = g_full7;
    *queued9 = g_to7.count; *queued7 = g_to9.count;
}

void ipc7_write_fifosend(Cpu *c, uint32_t v) {
    (void)c;
    if (ipc_trace()) rt_log("IPC 7->9 : %08X (étiquette %u, drapeau %u, donnée %08X)",
                            v, v & 0x1Fu, (v >> 5) & 1u, v >> 6);
    if (!(g_fifocnt7 & 0x8000)) return;
    if (!fifo_push(&g_to9, v)) { g_fifocnt7 |= 0x4000; g_full7++; return; }
    g_sent7++;
    if (g_fifocnt9 & 0x0400) hw_raise_irq(rt_main_cpu(), IRQ_IPC_RECV);
}

uint32_t ipc7_read_fiforecv(Cpu *c) {
    uint32_t v;
    if (!(g_fifocnt7 & 0x8000)) return g_last_recv7;
    if (!fifo_pop(&g_to7, &v)) {
        g_fifocnt7 |= 0x4000;
        if (ipc_trace()) rt_log("IPC 7 lit à vide (erreur levée)");
        return g_last_recv7;
    }
    g_last_recv7 = v;
    if (ipc_trace()) rt_log("IPC 7 lit : %08X (étiquette %u, reste %u)", v, v & 0x1Fu, g_to7.count);
    if (g_to7.count == 0 && (g_fifocnt9 & 0x0004)) hw_raise_irq(rt_main_cpu(), IRQ_IPC_SEND_EMPTY);
    (void)c;
    return v;
}

/* ------------------------------------------------------------------ tours d'aperçu (apercu.c) */
static uint32_t g_sync9_s, g_sync7_out_s, g_fifocnt9_s, g_fifocnt7_s, g_last_recv_s, g_last_recv7_s;
static Fifo g_to7_s, g_to9_s;
static uint64_t g_sent9_s, g_sent7_s, g_full7_s;
void ipc_apercu_sauver(void) {
    g_sync9_s = g_sync9; g_sync7_out_s = g_sync7_out; g_fifocnt9_s = g_fifocnt9; g_fifocnt7_s = g_fifocnt7;
    g_last_recv_s = g_last_recv; g_last_recv7_s = g_last_recv7; g_to7_s = g_to7; g_to9_s = g_to9;
    g_sent9_s = g_sent9; g_sent7_s = g_sent7; g_full7_s = g_full7;
}
void ipc_apercu_rendre(void) {
    g_sync9 = g_sync9_s; g_sync7_out = g_sync7_out_s; g_fifocnt9 = g_fifocnt9_s; g_fifocnt7 = g_fifocnt7_s;
    g_last_recv = g_last_recv_s; g_last_recv7 = g_last_recv7_s; g_to7 = g_to7_s; g_to9 = g_to9_s;
    g_sent9 = g_sent9_s; g_sent7 = g_sent7_s; g_full7 = g_full7_s;
}
