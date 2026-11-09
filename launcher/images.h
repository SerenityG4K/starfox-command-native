/* Les images du lanceur, tirées de la cartouche du joueur (images.c). Sous licence MIT. */
#pragma once
#include <windows.h>
#include <stdint.h>

typedef struct {
    int largeur, hauteur;
    uint32_t *pixels;               /* BGRA, ligne par ligne depuis le haut ; alpha 0 = transparent */
} Image;

/* La source désignée dans le lanceur : une cartouche (.nds, version ASFP) ou un dossier de données. */
int images_ouvrir(const wchar_t *source);
void images_fermer(void);
/* Une illustration de 256×192 (« 3D/Textures/Adv/Opening/02_team.ntfq-cmp ») ; 0 si elle manque. */
int image_illustration(const char *chemin, Image *img);
/* L'icône de la cartouche, 32×32. */
int image_icone(Image *img);
void image_liberer(Image *img);
/* Lesquels de ces fichiers la source porte à sa racine (« Fr.srl »…) : le bit i pour noms[i]. Ouvre et referme la
   source elle-même ; 0 si elle ne se lit pas. */
unsigned images_archives(const wchar_t *source, const char *const *noms, int n);
