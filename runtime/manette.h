/* La manette (runtime/manette.c). Sous licence MIT. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Les boutons d'une manette, par leur place — comme sur une manette Xbox : A en bas, B à droite, X à gauche,
   Y en haut ; ✕ ○ □ △ sur une PlayStation, B A Y X sur une Switch. Les gâchettes analogiques comptent comme
   enfoncées à mi-course. */
enum {
    MANETTE_SUD = 1u << 0, MANETTE_EST = 1u << 1, MANETTE_OUEST = 1u << 2, MANETTE_NORD = 1u << 3,
    MANETTE_L1 = 1u << 4, MANETTE_R1 = 1u << 5, MANETTE_L2 = 1u << 6, MANETTE_R2 = 1u << 7,
    MANETTE_SELECT = 1u << 8, MANETTE_START = 1u << 9, MANETTE_L3 = 1u << 10, MANETTE_R3 = 1u << 11,
    MANETTE_HAUT = 1u << 12, MANETTE_BAS = 1u << 13, MANETTE_GAUCHE = 1u << 14, MANETTE_DROITE = 1u << 15,
};

/* Ce que la manette donne à un instant ; `branchee` vaut 0 sans manette. Sticks de -1 à 1, zone morte
   retirée, y vers le haut. */
typedef struct {
    int branchee;
    uint32_t boutons;
    float gx, gy, dx, dy;
} Manette;

/* La manette du moment — XInput, DirectInput, ou le script d'essai (SFC_MANETTE_ESSAI). Lue par un fil à
   elle ; cet appel ne fait que recopier son dernier état. */
void manette_lire(Manette *m);

/* Les touches de la console que la manette tient, selon que le jeu est en vol ou non. */
uint32_t manette_touches(const Manette *m);

/* Une fois par image du jeu, sur son fil, après le rejeu ou l'enregistrement : les touches de la console que
   la manette tient, et ce qu'elle demande au cockpit en vol. `stylet` : le stylet de la souris (ou d'un
   rejeu) est posé à cette image — il a alors la main. */
void manette_appliquer(const Manette *m, uint32_t *touches, int stylet);

/* Le script d'essai à l'image courante du jeu, s'il y en a un (rend 0 sinon) : il remplace la manette,
   avec ou sans fenêtre, pour des essais qui se rejouent à l'identique. */
int manette_script(Manette *m);

#ifdef __cplusplus
}
#endif
