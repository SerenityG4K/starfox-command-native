/* Mode de vérification : empreintes par image, enregistrement et rejeu des entrées. Sous licence MIT. */
#pragma once
#include <stdint.h>
#include "manette.h"

#ifdef __cplusplus
extern "C" {
#endif

void verify_frame(uint64_t frame);
void verify_input_headless(uint64_t frame);   /* rejeu sans fenêtre (SFC_NO_VIDEO) */
/* touches, stylet, et la manette : remplacés par le rejeu, notés par l'enregistrement */
int  verify_input(uint64_t frame, uint32_t *held, int *touch_down, int *touch_x, int *touch_y, Manette *m);

#ifdef __cplusplus
}
#endif
