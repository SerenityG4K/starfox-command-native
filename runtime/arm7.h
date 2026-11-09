/* ARM7 simulé : réponses au protocole PXI du NitroSDK. Sous licence MIT. */
#pragma once
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

void arm7_init(Cpu *c);
void arm7_on_sync(Cpu *c, uint32_t value);       /* l'ARM9 a écrit sa valeur de synchronisation */
void arm7_on_sync_irq(Cpu *c);
void arm7_on_fifo(Cpu *c, uint32_t word);        /* mot reçu de l'ARM9 */
void arm7_on_vblank(Cpu *c);                     /* une fois par image : tactile, boutons X/Y */
void arm7_touch_adc(int *down, uint32_t *adc_x, uint32_t *adc_y);   /* mesure du convertisseur */

#ifdef __cplusplus
}
#endif
