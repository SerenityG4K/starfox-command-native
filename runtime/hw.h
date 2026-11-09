/* Matériel de la DS côté ARM9 : interruptions, temps, IPC, carte, GX. Sous licence MIT. */
#pragma once
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t g_io[];
uint32_t mmio_peek(uint32_t off);
void     mmio_poke(uint32_t off, uint32_t v);

/* interruptions et temps (irq.c) */
void     rt_cpu_wait_interrupt(Cpu *c);   /* CP15 c7,c0,4 et svc Halt : attend la prochaine interruption */
enum { IRQ_VBLANK = 0, IRQ_HBLANK = 1, IRQ_VCOUNT = 2, IRQ_TIMER0 = 3, IRQ_DMA0 = 8, IRQ_IPC_SYNC = 16,
       IRQ_IPC_SEND_EMPTY = 17, IRQ_IPC_RECV = 18, IRQ_CARD = 19, IRQ_CARD_IREQ = 20, IRQ_GXFIFO = 21 };
void     hw_init(Cpu *c);
void     hw_raise_irq(Cpu *c, int irq);
void     hw_check_irq(Cpu *c);
void     hw_deliver_pending(Cpu *c);           /* exécute le gestionnaire recompilé si IME/IE/IF le permettent */
void     hw_advance(Cpu *c, uint64_t cycles);  /* fait avancer l'horloge virtuelle */
uint64_t hw_now(void);
uint64_t hw_frames(void);
uint32_t hw_dispstat_bits(void);
uint32_t hw_vcount(void);
uint32_t hw_keyinput(void);
uint32_t hw_timer_read(Cpu *c, uint32_t off);
void     hw_timer_write(Cpu *c, uint32_t off, uint32_t v);
void     dma_trigger(Cpu *c, int mode);
void     hw_set_keys(uint32_t held);           /* bits KEYINPUT (0 = pressé) et X/Y dans EXTKEYIN */
void     hw_set_touch(int down, int x, int y); /* arm7_hle.c */
void     hw_set_pen(int down);                 /* bit 6 d'EXTKEYIN */

/* IPC (ipc.c) : l'ARM7 est simulé (arm7_hle.c) */
uint32_t ipc_read_sync(Cpu *c);
void     ipc_write_sync(Cpu *c, uint32_t v);
uint32_t ipc_read_fifocnt(Cpu *c);
void     ipc_write_fifocnt(Cpu *c, uint32_t v);
void     ipc_write_fifosend(Cpu *c, uint32_t v);
uint32_t ipc_read_fiforecv(Cpu *c);
void     ipc_arm7_send(Cpu *c, uint32_t v);    /* l'ARM7 simulé pousse un mot vers l'ARM9 */
void     ipc_arm7_set_sync(Cpu *c, uint32_t v);

/* dossier de données (data.c) : la cartouche servie depuis des fichiers séparés */
int      data_init(const char *racine);                /* 0 si le dossier n'est pas là */
int      data_disponible(void);
uint32_t data_taille(void);                            /* étendue occupée dans l'espace de la cartouche */
void     data_read(uint32_t adresse, uint8_t *dest, uint32_t taille);
const char *data_nom_a(uint32_t offset);               /* le fichier qui commence là, ou NULL */
uint8_t *data_preambule(size_t *taille);               /* en-tête, binaires, tables et overlays, d'un bloc */

/* carte (card.c) */
void     card_init(const uint8_t *rom, size_t size);
uint32_t card_read_romctrl(Cpu *c);
void     card_write_romctrl(Cpu *c, uint32_t v);
void     card_write_auxspi(Cpu *c, uint32_t v);        /* backup.c */
void     card_write_auxspidata(Cpu *c, uint32_t v);
uint32_t card_read_auxspidata(Cpu *c);
uint32_t backup_type(void);                            /* CARDBackupType simulé (SFC_BACKUP_TYPE) */
uint32_t backup_size(void);
uint8_t *backup_bytes(void);                           /* la puce, chargée à la demande */
void     backup_commit(void);                          /* après une écriture directe : persiste */
uint32_t card_chip_id(void);
const uint8_t *card_rom_bytes(size_t *size);
void     card_rom_read(uint32_t addr, uint8_t *dst, uint32_t len);   /* octets de la cartouche, dossier de données ou image */
uint32_t card_read_data(Cpu *c);
int      card_data_ready(void);                        /* ROMCTRL bit 23 : un mot attend en 0x04100010 */
void     card_feed_dma(Cpu *c);                        /* relance les DMA « carte » tant que des données sont prêtes */
int      card_tracing(void);                           /* SFC_TRACE_CARD */

/* moteur 3D (gx.c) */
uint32_t gx_read(Cpu *c, uint32_t off, uint32_t size);
void     gx_write(Cpu *c, uint32_t off, uint32_t v, uint32_t size);
void     gx_vblank(Cpu *c);
struct GxFrame;
void     gx_present(const struct GxFrame *f);   /* video.cpp : image 3D echangee */
void     gx_vblank_present(void);               /* gx.c : applique l'echange en attente (au VBlank, apres la composition) */
void     gx_dump_polys(uint64_t frame);         /* gx.c : SFC_DUMP_POLYS=n, journal des polygones affiches */
void     gx_geom_clip_stats(uint64_t frame);   /* gx_geom.c : SFC_STAT_CLIP=1, decoupage au plan lointain */
void     gx_geom_cell_stats(uint64_t frame);   /* gx_geom.c : SFC_TRACE_TRANS=1, cellules par image */
void     rt_find_value(uint64_t frame);         /* rt.c : SFC_FIND_VALUE, balayage de la RAM */
void     rt_dump_mem(uint64_t frame);           /* rt.c : SFC_DUMP_MEM=0xadr:n, vidage periodique */
void     rt_poke(void);                        /* rt.c : SFC_POKE=0xadr:0xval, force un mot */
void     video_vblank(void);                     /* video.cpp : presentation des deux ecrans */
uint32_t hw_extkeys(void);

#ifdef __cplusplus
}
#endif
