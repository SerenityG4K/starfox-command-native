/* L'habillage du lanceur : la fenêtre dessinée (habillage.c). Sous licence MIT. */
#pragma once
#include <windows.h>

#define HAB_LARGEUR 1000
#define HAB_HAUTEUR 600

/* Ce que l'on peut viser à la souris. */
enum {
    E_RIEN = -1,
    E_JOUER, E_REGLAGES, E_CONTROLES, E_MODS, E_QUITTER,
    E_REDUIRE, E_FERMER, E_CARTOUCHE,
    E_LANGUE_EN, E_LANGUE_FR, E_LANGUE_JA,     /* la langue du lanceur, dans l'ordre de textes.h */
    NB_ELEMENTS
};

/* L'état que l'habillage montre ; le lanceur le tient à jour. */
typedef struct {
    int page;                       /* 0 l'accueil, 1 Réglages, 2 Contrôles, 3 Mods */
    int survol, appui;              /* l'élément sous la souris, celui qu'on presse */
    int pret;                       /* le jeu est construit : « Jouer », sinon « Préparer le jeu » */
    int prepare;                    /* une préparation est en cours */
    int avance;                     /* son avancement, en millièmes */
    const wchar_t *etat;            /* l'étape en cours */
    const wchar_t *cartouche;       /* le nom de la cartouche choisie, ou NULL */
    int cartouche_bonne;            /* elle a été reconnue */
} Scene;

void habillage_creer(HWND fenetre, const wchar_t *version);
void habillage_detruire(void);
/* La langue du lanceur a changé (g_interface) : les polices et le texte qui défile suivent. */
void habillage_langue(void);
/* Les illustrations et l'icône, tirées de la source désignée (une cartouche ou un dossier de données) ; sans
   source reconnue, l'habillage se passe d'elles. Rend l'icône de la cartouche, ou NULL. */
HICON habillage_source(const wchar_t *source);
/* Le temps passe : étoiles, grille, texte qui défile, illustrations qui se fondent. */
void habillage_avancer(double secondes);
void habillage_peindre(HDC dc, const Scene *s);
/* L'élément sous ce point, ou E_RIEN. */
int habillage_cible(int x, int y, const Scene *s);
/* Où se pose la page des réglages ouverte. */
RECT habillage_zone_page(void);
/* Les couleurs des pages : fond, champs, texte, texte discret, titres, accent. */
COLORREF habillage_couleur(int quelle);
enum { C_PANNEAU, C_CHAMP, C_TEXTE, C_DISCRET, C_TITRE, C_ACCENT, C_BORD };
