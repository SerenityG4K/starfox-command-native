/* Coprocesseur ARM7 traduit : interface interne. Sous licence MIT. */
#pragma once
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ses interruptions ne sont pas celles de l'ARM9 : pas de file géométrique, mais la liaison
 * série, le sans-fil et l'ouverture du couvercle. */
enum {
    IRQ7_VBLANK = 0, IRQ7_HBLANK = 1, IRQ7_VCOUNT = 2, IRQ7_TIMER0 = 3,
    IRQ7_SIO = 7, IRQ7_DMA0 = 8, IRQ7_KEYPAD = 12, IRQ7_GBA = 13,
    IRQ7_IPC_SYNC = 16, IRQ7_IPC_SEND_EMPTY = 17, IRQ7_IPC_RECV = 18,
    IRQ7_CARD = 19, IRQ7_CARD_IREQ = 20, IRQ7_LID = 22, IRQ7_SPI = 23, IRQ7_WIFI = 24
};

int      arm7_enabled(void);                  /* SFC_ARM7=1 et traduction liée */
void     arm7_boot(const uint8_t *rom, size_t rom_size, Cpu *c9);
void     arm7_tick(Cpu *c9, uint64_t cycles); /* l'ARM9 lui cède le temps qu'il vient de consommer */
void     arm7_vblank(Cpu *c9);
void     arm7_scanline(uint32_t line);       /* fin d'une ligne de balayage */
void     arm7_halt(Cpu *c);                   /* HALTCNT : attend la prochaine interruption */
void     arm7_edge(Cpu *c);                   /* arête arrière du code traduit du coprocesseur */
void     arm7_spin(Cpu *c, uint64_t cycles);  /* attente active : sur son temps à lui */
void     arm7_raise_irq(Cpu *c, int irq);
void     arm7_check_irq(Cpu *c);
Cpu     *arm7_cpu(void);
void     arm7_set_fiber(void *fiber);  /* threads7.c : la fibre qui porte le coprocesseur */
void     threads7_init(Cpu *cpu7);     /* ses threads, sur des fibres (threads7.c) */
Cpu     *arm7_current_cpu(void);       /* le thread qu'il exécute en ce moment */
uint64_t arm7_edges(void);            /* arêtes qu'il a parcourues : mesure de son activité */
uint64_t arm7_irq_count(int irq);     /* combien de fois cette interruption lui a été levée */

/* minuteurs propres au coprocesseur (le pilote son y cadence ses alarmes) */
uint32_t arm7_timer_read(uint32_t off);
void     arm7_timer_write(Cpu *c, uint32_t off, uint32_t v);

/* liaison série : l'écran tactile et la puce de firmware */
uint32_t arm7_touch_channel(uint32_t chan);
uint8_t  arm7_firmware_byte(uint32_t addr);

/* registres du coprocesseur (mmio7.c) */
extern uint8_t g_io7[];
uint32_t mmio7_peek(uint32_t off);
void     mmio7_poke(uint32_t off, uint32_t v);
uint32_t mmio7_read32(Cpu *c, uint32_t addr);
uint16_t mmio7_read16(Cpu *c, uint32_t addr);
uint8_t  mmio7_read8(Cpu *c, uint32_t addr);
void     mmio7_write32(Cpu *c, uint32_t addr, uint32_t v);
void     mmio7_write16(Cpu *c, uint32_t addr, uint16_t v);
void     mmio7_write8(Cpu *c, uint32_t addr, uint8_t v);
void     dma7_trigger(Cpu *c, int mode);

/* liaison entre processeurs, vue du coprocesseur (ipc.c) */
uint32_t ipc7_read_sync(Cpu *c);
void     ipc7_write_sync(Cpu *c, uint32_t v);
uint32_t ipc7_read_fifocnt(Cpu *c);
void     ipc7_write_fifocnt(Cpu *c, uint32_t v);
void     ipc7_write_fifosend(Cpu *c, uint32_t v);
uint32_t ipc7_read_fiforecv(Cpu *c);

/* seize canaux sonores et leur mixage (sound.c) */
uint32_t snd_read(Cpu *c, uint32_t off);
void     snd_write(Cpu *c, uint32_t off, uint32_t v, uint32_t size);
void     snd_init(void);
void     snd_mix_frame(Cpu *c);
void     snd_mix_cycles(Cpu *c, uint64_t cycles);   /* mixe le temps qui vient de passer */

/* sortie audio de l'hôte (audio.c) */
void     audio_push(const int16_t *samples, int frames);
int      audio_queued(void);          /* tampons en attente dans la file de sortie */
uint64_t audio_underruns(void);       /* combien de fois la carte a manqué de données */
int      audio_target(void);          /* marge visée, en tampons */
uint64_t snd_dropped(void);           /* échantillons perdus au mixage */
uint64_t snd_late_samples(void);       /* echantillons produits en plus pour tenir le temps reel */
uint64_t snd_mix_ns(void);             /* temps passe a mixer, remis a zero a la lecture */
void     arm7_profile_report(void);     /* SFC_PROFILE_ARM7=1 : ou passe le temps du coproc */
uint64_t arm7_switches(void);          /* passages de fibre vers le coprocesseur */

#ifdef __cplusplus
}
#endif
