/*
 * Threads du NitroSDK sur des fibres Windows.
 *
 * Le code recompilé garde tout l'ordonnanceur du SDK (files, priorités,
 * réveil sur interruption) ; seules les trois primitives qui manipulent
 * physiquement les registres sont remplacées :
 *
 *   OS_SaveContext (0x0202E644)  écrit l'état dans l'OSContext, rend 0 ; la
 *                                reprise rendra 1 (comme setjmp/longjmp) ;
 *   OS_LoadContext (0x0202E690)  charge un OSContext et y saute ;
 *   OSi_IrqThreadSwitch (0x01FF8214)  fin du gestionnaire d'IRQ : réveil des
 *                                threads en attente, réordonnancement,
 *                                commutation depuis l'exception.
 *
 * Chaque thread du jeu a sa fibre et son propre `Cpu`. Une commutation charge
 * les registres du contexte cible dans le Cpu de sa fibre, puis bascule. Une
 * fibre reprise dans OS_LoadContext demande au code recompilé de continuer
 * après son OS_SaveContext (redirection).
 *
 * Disposition d'OSContext : +0 cpsr, +4 r0..r12, +0x38 sp, +0x3C lr, +0x40 pc+4,
 * +0x44 sp_svc, +0x48 contexte diviseur/racine (0x24 octets).
 * OSThread commence par son OSContext ; état à +0x64 (1 = prêt), suivant à +0x68,
 * liens d'attente d'IRQ à +0x78/+0x7C/+0x80.
 * OSi_ThreadInfo (0x020B41B0) : +0 réordonnancement demandé (u16), +4 thread
 * courant, +8 tête de liste, +0xC rappel de commutation.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include "threads.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OS_SAVE_CONTEXT     0x0202E644u
#define OS_LOAD_CONTEXT     0x0202E690u
#define OSI_IRQ_SWITCH      0x01FF8214u
#define CP_SAVE_CONTEXT     0x0202009Cu
#define CP_RESTORE_CONTEXT  0x02020030u
#define OSI_THREAD_INFO     0x020B41B0u
#define OSI_IRQ_THREAD_QUEUE 0x027E00C0u

typedef struct GuestThread {
    void *fiber;
    Cpu *cpu;              /* registres de ce thread (celui de main() pour le thread principal) */
    Cpu storage;
    uint32_t ctx;          /* OSContext de ce thread (0 tant qu'inconnu) */
    uint32_t expected_pc;  /* pc de reprise attendu (pc+4 du contexte à la suspension) */
    int alive;
    struct GuestThread *next;
} GuestThread;

static GuestThread *g_threads;
static GuestThread *g_current;
static Cpu *g_main_cpu_ptr;

/* le processeur du thread en cours d'exécution (diagnostics) */
Cpu *rt_current_cpu(void) {
    return g_current ? g_current->cpu : g_main_cpu_ptr;
}

/* Le contexte du fil principal de l'ARM9 : ce que le coprocesseur vise quand il lui lève
   une interruption. Il ne sert qu'à porter l'appel ; l'état des registres n'y entre pas. */
Cpu *rt_main_cpu(void) { return g_main_cpu_ptr; }

static GuestThread *thread_of_cpu(Cpu *c) {
    for (GuestThread *t = g_threads; t; t = t->next)
        if (t->fiber == c->fiber) return t;
    return NULL;
}

static GuestThread *thread_for_ctx(uint32_t ctx) {
    for (GuestThread *t = g_threads; t; t = t->next)
        if (t->ctx == ctx) return t;
    return NULL;
}

/* ------------------------------------------------------------------ contexte <-> registres */
static void store_context(Cpu *c, uint32_t ctx, uint32_t cpsr, const uint32_t *regs, uint32_t sp, uint32_t lr,
                          uint32_t pc_plus4, uint32_t sp_svc, uint32_t r0_value) {
    mem_write32(c, ctx + 0x00, cpsr);
    for (int i = 0; i < 13; i++) mem_write32(c, ctx + 4 + 4 * i, i == 0 ? r0_value : regs[i]);
    mem_write32(c, ctx + 0x38, sp);
    mem_write32(c, ctx + 0x3C, lr);
    mem_write32(c, ctx + 0x40, pc_plus4);
    mem_write32(c, ctx + 0x44, sp_svc);
}

static void load_context(Cpu *c, uint32_t ctx) {
    uint32_t cpsr = mem_read32(c, ctx);
    uint32_t regs[13];
    for (int i = 0; i < 13; i++) regs[i] = mem_read32(c, ctx + 4 + 4 * i);
    uint32_t sp = mem_read32(c, ctx + 0x38), lr = mem_read32(c, ctx + 0x3C);
    uint32_t pc = mem_read32(c, ctx + 0x40) - 4;
    uint32_t sp_svc = mem_read32(c, ctx + 0x44);
    /* banques : sp/lr utilisateur, sp superviseur ; puis le mode du contexte */
    cpu_switch_mode(c, MODE_SVC);
    c->r[13] = sp_svc;
    cpu_switch_mode(c, MODE_SYS);
    c->r[13] = sp;
    c->r[14] = lr;
    for (int i = 0; i < 13; i++) c->r[i] = regs[i];
    c->r[15] = pc;
    cpsr_write(c, cpsr, 0xFu);
    c->thumb = (cpsr >> 5) & 1u;
}

static uint32_t sp_svc_of(Cpu *c) {
    if (c->mode == MODE_SVC) return c->r[13];
    return c->bank_svc[0];
}

/* ------------------------------------------------------------------ fibres */
static int g_trace_threads = -1;
static void trace(const char *fmt, ...) {
    if (g_trace_threads < 0) g_trace_threads = getenv("SFC_TRACE_THREADS") != NULL;
    if (!g_trace_threads) return;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc(10, stderr);
}

uint64_t g_nb_commutations;        /* combien de fois un thread a pris la main (mesure) */
/* Le fil principal du jeu quand il dort — il attend le balayage (0x0202C760) : son état est celui d'un
   entre-deux-tours, et c'est de là que partent les tours d'aperçu hors du vol (natif.c). */
Cpu *g_cpu_principal_endormi;

static void switch_to(GuestThread *t) {
    GuestThread *prev = g_current;
    if (t == prev) return;
    { extern void apercu_abandon(int); apercu_abandon(3); }   /* un tour d'aperçu ne change pas de thread */
    g_nb_commutations++;
    trace("thread : %p (ctx 0x%08X) -> %p (ctx 0x%08X, pc 0x%08X, sp 0x%08X)", (void *)prev, prev ? prev->ctx : 0,
          (void *)t, t->ctx, t->cpu->r[15], t->cpu->r[13]);
    g_current = t;
    g_perturbations++;
    if (prev && prev->cpu == g_main_cpu_ptr) g_cpu_principal_endormi = g_main_cpu_ptr;
    if (t->cpu == g_main_cpu_ptr) g_cpu_principal_endormi = NULL;
    SwitchToFiber(t->fiber);
    g_cpu_principal_endormi = NULL;   /* nous voici de retour : le fil principal tourne à nouveau */
    /* reprise de `prev` : quelqu'un a chargé son contexte */
    g_current = prev;
}

static void CALLBACK fiber_main(void *param) {
    GuestThread *t = param;
    Cpu *c = t->cpu;
    for (;;) {
        rt_call(c, c->r[15] | c->thumb);
        /* la fonction du thread a rendu la main : elle « revient » à lr (OS_ExitThread) */
        c->r[15] = c->r[14];
        c->thumb = c->r[14] & 1u;
    }
}

/* Un processeur neuf pour un thread qui (re)commence, et sa fibre. */
static void repartir_a_neuf(GuestThread *t, Cpu *creator) {
    uint32_t bank_irq[3];
    /* la pile IRQ est unique : sa banque vient du contexte créateur */
    memcpy(bank_irq, creator->mode == MODE_IRQ ? (uint32_t[3]){creator->r[13], creator->r[14], creator->spsr}
                                               : creator->bank_irq, sizeof bank_irq);
    rt_init(t->cpu);
    memcpy(t->cpu->bank_irq, bank_irq, sizeof bank_irq);
    t->fiber = CreateFiber(1 << 20, fiber_main, t);
    t->cpu->fiber = t->fiber;
}

static GuestThread *new_thread(Cpu *creator, uint32_t ctx) {
    GuestThread *t = calloc(1, sizeof *t);
    t->cpu = &t->storage;
    repartir_a_neuf(t, creator);
    t->ctx = ctx;
    t->alive = 1;
    t->next = g_threads;
    g_threads = t;
    return t;
}

void threads_init(Cpu *main_cpu) {
    GuestThread *t = calloc(1, sizeof *t);
    t->fiber = ConvertThreadToFiber(t);
    t->cpu = main_cpu;
    t->alive = 1;
    t->next = g_threads;
    g_threads = t;
    g_current = t;
    main_cpu->fiber = t->fiber;
    g_main_cpu_ptr = main_cpu;
    rt_diag_hooks();
    { extern void rt_etude_objets_init(void); rt_etude_objets_init(); }
    rt_hook(OS_SAVE_CONTEXT, hle_os_save_context, "OS_SaveContext");
    rt_hook(OS_LOAD_CONTEXT, hle_os_load_context, "OS_LoadContext");
    rt_hook(OSI_IRQ_SWITCH, hle_irq_thread_switch, "OSi_IrqThreadSwitch");
}

/* ------------------------------------------------------------------ OS_SaveContext */
void hle_os_save_context(Cpu *c) {
    uint32_t ctx = c->r[0];
    trace("OS_SaveContext(0x%08X) depuis lr=0x%08X sp=0x%08X", ctx, c->r[14], c->r[13]);
    if (g_trace_threads > 0) rt_dump_stack(c);   /* d'où ce thread s'endort : sa pile d'appels */
    GuestThread *self = thread_of_cpu(c);
    if (self) self->ctx = ctx;
    uint32_t cpsr = cpsr_read(c);
    /* l'original écrit pc+4 = adresse de son propre `bx lr` ; la reprise y saute avec r0 = 1 */
    store_context(c, ctx, cpsr, c->r, c->r[13], c->r[14], 0x0202E68Cu, sp_svc_of(c), 1u);
    /* reprise : continuer après l'appel (lr), r0 = 1 */
    c->r[0] = 0;
}

/* ------------------------------------------------------------------ OS_LoadContext */
static void resume_thread(Cpu *c, uint32_t ctx) {
    GuestThread *self = thread_of_cpu(c);
    GuestThread *target = thread_for_ctx(ctx);
    /* Un tour d'aperçu ne change pas de thread — et il faut y renoncer ici, avant de toucher aux fibres :
       plus bas, un contexte réutilisé fait détruire la fibre parquée du thread visé. Un aperçu qui allait
       jusque-là effaçait pour de bon l'état d'un thread du vrai jeu. */
    if (target != g_current) { extern void apercu_abandon(int); apercu_abandon(3); }
    if (self) self->expected_pc = mem_read32(c, self->ctx + 0x40);
    if (target != NULL && target != g_current && target->expected_pc != mem_read32(c, ctx + 0x40)) {
        /* l'OSThread a été réinitialisé (thread terminé puis recréé au même endroit) :
           la fibre parquée ne correspond plus, on en crée une neuve */
        rt_log("thread : contexte 0x%08X réutilisé (attendu pc 0x%08X, trouvé 0x%08X)",
               ctx, target->expected_pc, mem_read32(c, ctx + 0x40));
        DeleteFiber(target->fiber);
        /* …et un processeur neuf, comme à la création. L'ancienne fibre avait pu être parquée au milieu
           d'une interruption (le thread d'attente, préempté pendant son `halt`) : le thread recréé
           héritait du drapeau « interruption en cours », ne recevait plus aucune interruption, et le jeu
           restait figé dans l'attente — en quittant une mission. */
        repartir_a_neuf(target, c);
    }
    if (target == NULL) target = new_thread(c, ctx);
    load_context(target->cpu, ctx);
    switch_to(target);
}

void hle_os_load_context(Cpu *c) {
    uint32_t ctx = c->r[0];
    trace("OS_LoadContext(0x%08X) depuis lr=0x%08X : pc+4=0x%08X sp=0x%08X", ctx, c->r[14],
          mem_read32(c, ctx + 0x40), mem_read32(c, ctx + 0x38));
    /* CP_RestoreContext(&ctx->cp) : l'original le fait d'abord */
    uint32_t saved_r0 = c->r[0], saved_lr = c->r[14];
    c->r[0] = ctx + 0x48;
    RT_CALL(c, saved_lr, rt_call(c, CP_RESTORE_CONTEXT));
    c->r[0] = saved_r0;
    c->r[14] = saved_lr;
    resume_thread(c, ctx);
    /* Nous voici repris : notre contexte vient d'être chargé dans `c` par le commutateur.
       Le pc chargé est celui de la reprise ; s'il est le `bx lr` d'OS_SaveContext, on
       continue après l'appel à OS_SaveContext avec r0 = 1 (déjà dans les registres). */
    uint32_t pc = c->r[15];
    if (pc == 0x0202E688u) {
        c->redirect = c->r[14] & ~1u;
        c->thumb = c->r[14] & 1u;
    } else {
        c->redirect = pc;
    }
}

/* ------------------------------------------------------------------ fin du gestionnaire d'IRQ */
/* Appelé dans la fibre interrompue, en mode IRQ, avec sur la pile IRQ les registres
   poussés par le vecteur du BIOS : {r0, r1, r2, r3, r12, lr} puis le lr du gestionnaire. */
void hle_irq_thread_switch(Cpu *c) {
    /* le gestionnaire avait empilé son lr : l'original le dépile en revenant (ldm sp!, {pc}) */
    uint32_t sp_irq = c->r[13];
    c->r[13] = sp_irq + 4u;
    /* 1. réveil des threads en attente de cette interruption */
    uint32_t queue = mem_read32(c, OSI_IRQ_THREAD_QUEUE);
    if (queue) {
        uint32_t t = queue;
        while (t) {
            mem_write32(c, t + 0x64, 1);
            mem_write32(c, t + 0x78, 0);
            mem_write32(c, t + 0x7C, 0);
            uint32_t next = mem_read32(c, t + 0x80);
            mem_write32(c, t + 0x80, 0);
            t = next;
        }
        mem_write32(c, OSI_IRQ_THREAD_QUEUE, 0);
        mem_write32(c, OSI_IRQ_THREAD_QUEUE + 4, 0);
        mem_write16(c, OSI_THREAD_INFO, 1);
    }
    /* 2. réordonnancement demandé ? */
    if (mem_read16(c, OSI_THREAD_INFO) == 0) return;
    mem_write16(c, OSI_THREAD_INFO, 0);
    uint32_t next = mem_read32(c, OSI_THREAD_INFO + 8);
    while (next && mem_read32(c, next + 0x64) != 1) next = mem_read32(c, next + 0x68);
    if (next == 0) return;
    uint32_t current = mem_read32(c, OSI_THREAD_INFO + 4);
    if (next == current) return;
    /* 3. rappel de commutation (r0 = courant, r1 = suivant) */
    uint32_t callback = mem_read32(c, OSI_THREAD_INFO + 0xC);
    if (callback) {
        uint32_t saved_lr = c->r[14];
        c->r[0] = current;
        c->r[1] = next;
        RT_CALL(c, saved_lr, rt_call(c, callback));
        c->r[14] = saved_lr;
    }
    mem_write32(c, OSI_THREAD_INFO + 4, next);
    /* 4. sauvegarde du contexte interrompu (depuis la pile IRQ) */
    uint32_t pushed[6];
    for (int i = 0; i < 6; i++) pushed[i] = mem_read32(c, sp_irq + 4 + 4 * i);   /* après le lr du gestionnaire */
    uint32_t regs[13];
    regs[0] = pushed[0]; regs[1] = pushed[1]; regs[2] = pushed[2]; regs[3] = pushed[3];
    for (int i = 4; i < 12; i++) regs[i] = c->r[i];
    regs[12] = pushed[4];
    uint32_t lr_irq = pushed[5];
    uint32_t interrupted_cpsr = c->spsr;
    uint32_t user_sp = user_reg_read(c, 13), user_lr = user_reg_read(c, 14);
    GuestThread *self = thread_of_cpu(c);
    if (self) self->ctx = current;
    store_context(c, current, interrupted_cpsr, regs, user_sp, user_lr, lr_irq, sp_svc_of(c), regs[0]);
    /* CP_SaveContext(&current->cp) ; CP_RestoreContext(&next->cp) */
    uint32_t saved_lr = c->r[14];
    c->r[0] = current + 0x48;
    RT_CALL(c, saved_lr, rt_call(c, CP_SAVE_CONTEXT));
    c->r[0] = next + 0x48;
    RT_CALL(c, saved_lr, rt_call(c, CP_RESTORE_CONTEXT));
    c->r[14] = saved_lr;
    /* 5. commutation : la fibre du suivant reprend là où elle s'était arrêtée */
    resume_thread(c, next);
    /* Repris : notre contexte a été rechargé dans `c` (registres de l'instant interrompu),
       et nous sommes à la fin du gestionnaire d'IRQ. On repasse en mode IRQ avec ce
       qu'attend hw_deliver_pending : elle restaurera le cpsr interrompu. */
    uint32_t cpsr_now = cpsr_read(c);
    cpu_switch_mode(c, MODE_IRQ);
    c->spsr = cpsr_now;
    c->redirect = 0;
    c->irq_resumed = 1;
}

/* ------------------------------------------------------------------ tours d'aperçu (apercu.c) */
/* Ce qu'un aperçu peut changer ici sans que la mémoire suivie le dise : le contexte que chaque thread
   déclare, le pc de reprise qu'on attend de lui, et le thread courant. Les fibres, elles, ne bougent pas :
   un aperçu qui tenterait d'en changer est abandonné. */
static struct { GuestThread *t; Cpu *cpu; uint32_t ctx, expected_pc; int alive; } g_threads_sauves[64];
static size_t g_threads_sauves_n;
static GuestThread *g_current_sauve;

void threads_apercu_sauver(void) {
    g_threads_sauves_n = 0;
    for (GuestThread *t = g_threads; t && g_threads_sauves_n < 64; t = t->next) {
        g_threads_sauves[g_threads_sauves_n].t = t;
        g_threads_sauves[g_threads_sauves_n].cpu = t->cpu;
        g_threads_sauves[g_threads_sauves_n].ctx = t->ctx;
        g_threads_sauves[g_threads_sauves_n].expected_pc = t->expected_pc;
        g_threads_sauves[g_threads_sauves_n].alive = t->alive;
        g_threads_sauves_n++;
    }
    g_current_sauve = g_current;
}

void threads_apercu_rendre(void) {
    for (size_t i = 0; i < g_threads_sauves_n; i++) {
        GuestThread *t = g_threads_sauves[i].t;
        t->cpu = g_threads_sauves[i].cpu;
        t->ctx = g_threads_sauves[i].ctx;
        t->expected_pc = g_threads_sauves[i].expected_pc;
        t->alive = g_threads_sauves[i].alive;
    }
    g_current = g_current_sauve;
}
