/*
 * Threads du coprocesseur, sur des fibres.
 *
 * Le SDK fait tourner sur l'ARM7 le même ordonnanceur que sur l'ARM9 : des
 * threads, une file d'attente d'interruption, une commutation à la fin du
 * gestionnaire. Et il la réalise de la même manière, par trois primitives qui
 * manipulent physiquement les registres — que du C traduit ne peut pas
 * exécuter, puisqu'elles rechargent le processeur entier et sautent ailleurs.
 * On les remplace donc ici, exactement comme threads.c le fait pour l'ARM9 :
 *
 *   OS_SaveContext (0x037FCAF4)      écrit l'état dans l'OSContext, rend 0 ;
 *                                    la reprise rendra 1 ;
 *   OS_LoadContext (0x037FCB28)      charge un OSContext et y saute ;
 *   OSi_IrqThreadSwitch (0x037FB948) fin du gestionnaire d'interruption :
 *                                    réveil des threads en attente,
 *                                    réordonnancement, commutation.
 *
 * L'OSContext du coprocesseur a la même disposition que celui de l'ARM9 moins
 * sa partie coprocesseur 15, qui n'existe pas de ce côté : +0 cpsr, +4 r0..r12,
 * +0x38 sp, +0x3C lr, +0x40 pc+4, +0x44 sp superviseur. L'OSThread commence par
 * son contexte ; son état est donc à +0x48 et non +0x64, et ses liens d'attente
 * d'interruption à +0x5C/+0x60/+0x64.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include "arm7_run.h"
#include "threads.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OS7_SAVE_CONTEXT      0x037FCAF4u
#define OS7_LOAD_CONTEXT      0x037FCB28u
#define OS7_SAVE_RESUME_PC    0x037FCB24u   /* le `bx lr` où la reprise atterrit */
#define OS7_SAVE_RESUME_PC4   0x037FCB28u   /* ce que la primitive écrit en +0x40 */
#define OSI7_IRQ_SWITCH       0x037FB948u
#define OSI7_THREAD_INFO      0x038082D8u
#define OSI7_IRQ_THREAD_QUEUE 0x03808248u

/* décalages dans l'OSThread du coprocesseur */
#define TH7_STATE   0x48
#define TH7_NEXT    0x4C
#define TH7_LINK0   0x5C
#define TH7_LINK1   0x60
#define TH7_LINK2   0x64

typedef struct GuestThread7 {
    void *fiber;
    Cpu *cpu;
    Cpu storage;
    uint32_t ctx;
    uint32_t expected_pc;
    struct GuestThread7 *next;
} GuestThread7;

static GuestThread7 *g_threads7;
static GuestThread7 *g_current7;

static int g_trace = -1;
static void trace(const char *fmt, ...) {
    if (g_trace < 0) g_trace = getenv("SFC_TRACE_THREADS") != NULL;
    if (!g_trace) return;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc(10, stderr);
}

/* Le contexte du thread que le coprocesseur exécute en ce moment : c'est lui qu'il faut
   regarder quand tout s'arrête, pas celui de son fil principal. */
Cpu *arm7_current_cpu(void) { return g_current7 ? g_current7->cpu : NULL; }

static GuestThread7 *thread_of_cpu(Cpu *c) {
    for (GuestThread7 *t = g_threads7; t; t = t->next)
        if (t->fiber == c->fiber) return t;
    return NULL;
}

static GuestThread7 *thread_for_ctx(uint32_t ctx) {
    for (GuestThread7 *t = g_threads7; t; t = t->next)
        if (t->ctx == ctx) return t;
    return NULL;
}

/* ------------------------------------------------------------------ contexte <-> registres */
static void store_context(Cpu *c, uint32_t ctx, uint32_t cpsr, const uint32_t *regs, uint32_t sp,
                          uint32_t lr, uint32_t pc_plus4, uint32_t sp_svc, uint32_t r0_value) {
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
    return c->mode == MODE_SVC ? c->r[13] : c->bank_svc[0];
}

/* ------------------------------------------------------------------ fibres */
static void switch_to(GuestThread7 *t) {
    GuestThread7 *prev = g_current7;
    if (t == prev) return;
    trace("coproc thread : %p (ctx 0x%08X) -> %p (ctx 0x%08X, pc 0x%08X, sp 0x%08X)",
          (void *)prev, prev ? prev->ctx : 0, (void *)t, t->ctx, t->cpu->r[15], t->cpu->r[13]);
    g_current7 = t;
    arm7_set_fiber(t->fiber);
    SwitchToFiber(t->fiber);
    g_current7 = prev;
    arm7_set_fiber(prev->fiber);
}

static void CALLBACK fiber_main(void *param) {
    GuestThread7 *t = param;
    Cpu *c = t->cpu;
    for (;;) {
        rt_call(c, c->r[15] | c->thumb);
        c->r[15] = c->r[14];
        c->thumb = c->r[14] & 1u;
    }
}

/* Un processeur neuf pour un thread qui (re)commence, et sa fibre. */
static void repartir_a_neuf(GuestThread7 *t, Cpu *creator) {
    uint32_t bank_irq[3];
    memcpy(bank_irq, creator->mode == MODE_IRQ ? (uint32_t[3]){creator->r[13], creator->r[14], creator->spsr}
                                               : creator->bank_irq, sizeof bank_irq);
    rt_init(t->cpu);
    t->cpu->is_arm7 = 1;
    memcpy(t->cpu->bank_irq, bank_irq, sizeof bank_irq);
    t->fiber = CreateFiber(1 << 20, fiber_main, t);
    t->cpu->fiber = t->fiber;
}

static GuestThread7 *new_thread(Cpu *creator, uint32_t ctx) {
    GuestThread7 *t = calloc(1, sizeof *t);
    t->cpu = &t->storage;
    repartir_a_neuf(t, creator);
    t->ctx = ctx;
    t->next = g_threads7;
    g_threads7 = t;
    return t;
}

/* ------------------------------------------------------------------ les trois primitives */
static void hle7_save_context(Cpu *c) {
    uint32_t ctx = c->r[0];
    trace("coproc OS_SaveContext(0x%08X) depuis lr=0x%08X sp=0x%08X", ctx, c->r[14], c->r[13]);
    GuestThread7 *self = thread_of_cpu(c);
    if (self) self->ctx = ctx;
    store_context(c, ctx, cpsr_read(c), c->r, c->r[13], c->r[14], OS7_SAVE_RESUME_PC4, sp_svc_of(c), 1u);
    c->r[0] = 0;
}

static void resume_thread(Cpu *c, uint32_t ctx) {
    GuestThread7 *self = thread_of_cpu(c);
    if (self) self->expected_pc = mem_read32(c, self->ctx + 0x40);
    GuestThread7 *target = thread_for_ctx(ctx);
    if (target != NULL && target != g_current7 && target->expected_pc != mem_read32(c, ctx + 0x40)) {
        /* l'OSThread a été réutilisé : la fibre parquée ne correspond plus */
        rt_log("coproc thread : contexte 0x%08X réutilisé (attendu pc 0x%08X, trouvé 0x%08X)",
               ctx, target->expected_pc, mem_read32(c, ctx + 0x40));
        DeleteFiber(target->fiber);
        /* …et un processeur neuf, comme à la création : l'ancien pouvait porter le drapeau « interruption
           en cours » d'une fibre parquée pendant une interruption (voir threads.c) */
        repartir_a_neuf(target, c);
    }
    if (target == NULL) target = new_thread(c, ctx);
    load_context(target->cpu, ctx);
    switch_to(target);
}

static void hle7_load_context(Cpu *c) {
    uint32_t ctx = c->r[0];
    trace("coproc OS_LoadContext(0x%08X) : pc+4=0x%08X sp=0x%08X", ctx,
          mem_read32(c, ctx + 0x40), mem_read32(c, ctx + 0x38));
    resume_thread(c, ctx);
    /* Repris : notre contexte vient d'être rechargé. Si le pc de reprise est le `bx lr` de
       OS_SaveContext, on continue après l'appel, avec r0 = 1 déjà en place. */
    uint32_t pc = c->r[15];
    if (pc == OS7_SAVE_RESUME_PC) {
        c->redirect = c->r[14] & ~1u;
        c->thumb = c->r[14] & 1u;
    } else {
        c->redirect = pc;
    }
}

/* Fin du gestionnaire d'interruption : appelé en mode IRQ, la pile IRQ portant le lr du
   gestionnaire puis les {r0, r1, r2, r3, r12, lr} que le vecteur du BIOS y a poussés. */
static void hle7_irq_thread_switch(Cpu *c) {
    uint32_t sp_irq = c->r[13];
    c->r[13] = sp_irq + 4u;              /* le gestionnaire dépile son propre lr en revenant */

    /* 1. réveil de tous les threads qui attendaient une interruption */
    uint32_t queue = mem_read32(c, OSI7_IRQ_THREAD_QUEUE);
    if (queue) {
        uint32_t t = queue;
        while (t) {
            mem_write32(c, t + TH7_STATE, 1);
            mem_write32(c, t + TH7_LINK0, 0);
            mem_write32(c, t + TH7_LINK1, 0);
            uint32_t next = mem_read32(c, t + TH7_LINK2);
            mem_write32(c, t + TH7_LINK2, 0);
            t = next;
        }
        mem_write32(c, OSI7_IRQ_THREAD_QUEUE, 0);
        mem_write32(c, OSI7_IRQ_THREAD_QUEUE + 4, 0);
        mem_write16(c, OSI7_THREAD_INFO, 1);
    }

    /* 2. réordonnancement demandé ? */
    if (mem_read16(c, OSI7_THREAD_INFO) == 0) return;
    mem_write16(c, OSI7_THREAD_INFO, 0);
    uint32_t next = mem_read32(c, OSI7_THREAD_INFO + 8);
    while (next && mem_read16(c, next + TH7_STATE) != 1) next = mem_read32(c, next + TH7_NEXT);
    if (next == 0) return;
    uint32_t current = mem_read32(c, OSI7_THREAD_INFO + 4);
    if (next == current) return;

    /* 3. rappel de commutation (r0 = courant, r1 = suivant) */
    uint32_t callback = mem_read32(c, OSI7_THREAD_INFO + 0xC);
    if (callback) {
        uint32_t saved_lr = c->r[14];
        c->r[0] = current;
        c->r[1] = next;
        RT_CALL(c, saved_lr, rt_call(c, callback));
        c->r[14] = saved_lr;
    }
    mem_write32(c, OSI7_THREAD_INFO + 4, next);

    /* 4. sauvegarde du contexte interrompu, pris sur la pile IRQ */
    uint32_t pushed[6];
    for (int i = 0; i < 6; i++) pushed[i] = mem_read32(c, sp_irq + 4 + 4 * i);
    uint32_t regs[13];
    regs[0] = pushed[0]; regs[1] = pushed[1]; regs[2] = pushed[2]; regs[3] = pushed[3];
    for (int i = 4; i < 12; i++) regs[i] = c->r[i];
    regs[12] = pushed[4];
    uint32_t lr_irq = pushed[5];
    uint32_t user_sp = user_reg_read(c, 13), user_lr = user_reg_read(c, 14);
    GuestThread7 *self = thread_of_cpu(c);
    if (self) self->ctx = current;
    store_context(c, current, c->spsr, regs, user_sp, user_lr, lr_irq, sp_svc_of(c), regs[0]);

    /* 5. et chargement du suivant */
    resume_thread(c, next);
    /* Repris : `load_context` vient de rendre à cette fibre les registres et le `cpsr` de son
       propre thread. Le vecteur, en remontant, restaurera `spsr` : on y met donc ce `cpsr`-là,
       et on repasse en mode IRQ comme il s'y attend. Sans cela le thread reprenait avec le
       masque d'interruption d'un autre, parfois fermé — et ne traitait plus rien. */
    uint32_t cpsr_now = cpsr_read(c);
    cpu_switch_mode(c, MODE_IRQ);
    c->spsr = cpsr_now;
    c->redirect = 0;
    c->irq_resumed = 1;
}

void threads7_init(Cpu *cpu7) {
    GuestThread7 *t = calloc(1, sizeof *t);
    t->fiber = GetCurrentFiber();
    t->cpu = cpu7;
    t->next = g_threads7;
    g_threads7 = t;
    g_current7 = t;
    cpu7->fiber = t->fiber;
    rt_hook7(OS7_SAVE_CONTEXT, hle7_save_context, "OS_SaveContext (coprocesseur)");
    rt_hook7(OS7_LOAD_CONTEXT, hle7_load_context, "OS_LoadContext (coprocesseur)");
    rt_hook7(OSI7_IRQ_SWITCH, hle7_irq_thread_switch, "OSi_IrqThreadSwitch (coprocesseur)");
}
