/* Moteurs 2D de la DS (A et B) : rendu logiciel d'une image. Sous licence MIT. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_W 256
#define DS_H 192

/* Sortie d'un moteur pour une image : deux couches RGBA8 (0xAABBGGRR).
   `below` est opaque (fond compris) et contient tout ce qui passe sous la
   couche 3D ; `above` contient ce qui passe au-dessus, transparent ailleurs.
   Sans 3D (moteur B, ou BG0 3D désactivé), tout est dans `below`. */
typedef struct {
    uint32_t below[DS_W * DS_H];
    uint32_t above[DS_W * DS_H];
    int has_3d;           /* BG0 de A est la sortie 3D et affichée */
    int display_mode;     /* 0 éteint, 1 graphique, 2 VRAM, 3 RAM */
    int brightness;       /* MASTER_BRIGHT : -16..+16 */
} Engine2dOutput;

void video2d_render(int engine, Engine2dOutput *out);
/* Les lignes [y0, y1) seulement : des fils peuvent se partager une image par bandes (même résultat). */
void video2d_render_lignes(int engine, Engine2dOutput *out, int y0, int y1);
/* POWCNT1 bit 15 : le moteur A est sur l'écran du haut (1) ou du bas (0) */
int video2d_a_on_top(void);

#ifdef __cplusplus
}
#endif
