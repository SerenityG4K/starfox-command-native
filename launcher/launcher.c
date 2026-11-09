/*
 * Lanceur du portage : une fenêtre où choisir la langue, le rendu et les touches, puis démarrer le jeu.
 *
 * Trois pages : « Réglages » (cartouche, langue du jeu, rendu, cadence, distance, son), « Contrôles » (les
 * touches du clavier, commande par commande) et « Mods » (des changements de règles, éteints par défaut). Le
 * bouton Jouer reste à gauche, sous le titre.
 *
 * Le lanceur parle anglais, français ou japonais (textes.c) : anglais par défaut, le choix se fait en haut à
 * droite et se retient dans lanceur.ini. La langue du jeu est autre chose : le jeu n'a pas de menu pour elle —
 * sur la console il la lit dans les réglages du firmware —, et c'est le lanceur qui la fixe, par SFC_LANG,
 * parmi celles que porte la cartouche. Le reste des réglages passe par les mêmes variables d'environnement que
 * celles documentées dans le README.
 *
 * Écrit en Win32 pur : pas de dépendance, et l'exécutable tient en quelques centaines de
 * kilooctets à côté du jeu, qui en pèse plus de deux cents.
 *
 *   gcc -O2 -municode -mwindows launcher/launcher.c launcher/habillage.c launcher/images.c launcher/textes.c -o lanceur.exe -lcomctl32 -lcomdlg32 -lgdi32
 *
 * Il vit à la racine du projet : on le lance d'un double clic, on lui désigne sa cartouche, il trouve le jeu
 * (dans build/runtime8 pendant le développement, dans jeu/ chez le joueur) et le fait tourner à côté de lui,
 * où vit la sauvegarde. Tant que le jeu n'est pas construit, « Préparer le jeu » le construit sur la machine
 * du joueur, depuis sa cartouche (tools/preparer.py) : le portage ne distribue rien du jeu.
 *
 * Sous licence MIT.
 */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "habillage.h"
#include "images.h"
#include "textes.h"

#define VERSION L"1.2"

#define ID_LANGUE   1000
#define ID_ECHELLE  1001
#define ID_FILTRE   1002
#define ID_SON      1003
#define ID_SOURCE   1004
#define ID_PARCOURIR 1005
#define ID_JOUER    1006
#define ID_CADENCE  1007
#define ID_PORTEE   1008
#define ID_ONGLETS  1009
#define ID_ORIGINE  1010           /* Contrôles : « Touches d'origine » */
#define ID_REACTION 1013           /* Réglages : la réaction de l'affichage */
#define ID_AXE      1014           /* Contrôles : le sens du stick en hauteur */
#define ID_TOUCHE   1300           /* Contrôles : le bouton de chaque commande, 1300 + son rang */
#define ID_MOD      1400           /* Mods : chaque réglage, 1400 + son rang dans MODS */
#define ID_SANS_MOD 1011           /* Mods : « Aucun mod » */
#define ID_PILOTE_MOD 1012         /* Mods : le pilote dont on règle le vaisseau */
#define ID_REGLAGE_MOD 1500        /* Mods : les réglages du vaisseau, 1500 + leur rang dans VAISSEAU_MOD */

/* Les langues du jeu. Chacune a son archive de textes à la racine de la cartouche (Uk.srl pour l'anglais,
   Fr.srl…) : le lanceur ne propose que celles que la cartouche du joueur porte. La version européenne, la seule
   sur laquelle le portage est construit et éprouvé, en porte cinq ; sans cartouche reconnue, ce sont ces cinq.
   Le code est celui que le jeu lit dans SFC_LANG (runtime/arm7_run.c). */
static const struct { const char *code; const wchar_t *nom; const char *archive; } LANGUES[] = {
    {"en", L"English", "Uk.srl"},
    {"fr", L"Français", "Fr.srl"},
    {"de", L"Deutsch", "De.srl"},
    {"es", L"Español", "Es.srl"},
    {"it", L"Italiano", "It.srl"},
};
#define NB_LANGUES ((int)(sizeof LANGUES / sizeof *LANGUES))
/* l'ordre des versions 1.0 à 1.2, dont lanceur.ini retenait le rang */
static const char *ANCIENNES_LANGUES[] = {"fr", "en", "de", "es", "it"};
static int g_langues_vues[NB_LANGUES];    /* celles de la cartouche, rangs dans LANGUES */
static int g_nb_langues_vues;

static HWND g_langue, g_echelle, g_filtre, g_son, g_source, g_cadence, g_portee;
static HWND g_pages[3];
static HWND g_liste_reaction;          /* Réglages > Réaction */
static int g_reaction;                 /* le choix, rang dans REACTIONS */
static HWND g_liste_axe;               /* Contrôles > le stick en hauteur */
static int g_axe;                      /* le choix, rang dans AXES */
static HWND g_titres[8];               /* les intitulés des pages, en couleur */
static int g_nb_titres;
static Scene g_scene = {0, E_RIEN, E_RIEN, 0, 0, 0, NULL, NULL, 0};
static wchar_t g_nom_cartouche[MAX_PATH];
static wchar_t g_dossier[MAX_PATH];

/* La cadence d'affichage. Le jeu pense à son rythme — soixante balayages par seconde, trente tours en
   vol — et ne peut pas penser plus vite sans s'accélérer. L'affichage s'en détache (SFC_AFFICHAGE=libre) :
   il montre autant d'images que demandé, chacune placée entre deux images du jeu. Au-delà de 120 images
   par seconde, cette interpolation est moins stable : depuis le 8 novembre 2026, le lanceur n'offre que
   60, 90 et 120 (SFC_IPS reste libre pour qui lance le jeu à la main). */
static const wchar_t *CADENCES[] = {L"60", L"90", L"120"};
static const int NB_CADENCES = sizeof CADENCES / sizeof *CADENCES;
#define CADENCE_DEFAUT 2                  /* 120 */

/* Une cadence retenue par une version précédente — un nombre, « Celle de l'écran », « Sans limite »,
   « Comme la console » — ramenée à l'un des trois choix : au plus près sans dépasser, 120 au-delà. */
static int rang_cadence(const wchar_t *retenue) {
    const int n = _wtoi(retenue);
    if (n > 0) return n >= 120 ? 2 : n >= 90 ? 1 : 0;
    if (!wcscmp(retenue, L"Comme la console")) return 0;
    return CADENCE_DEFAUT;
}

/* La portée d'affichage (SFC_PORTEE, depuis la version 1.2). La console ne dessine rien au-delà de
   4 000 unités, et un anneau d'horizon peint cache le reste : le portage dessine deux fois plus loin,
   l'anneau reculant d'autant. Les règles et le rythme du jeu restent les siens ; dessiner davantage
   coûte seulement un peu de temps à la console simulée, comme il en coûterait à la vraie, et ce que le
   jeu tire de l'horloge fine peut en différer. Les tirs suivent : lasers d'Arwing et tirs ennemis vivent
   deux fois plus longtemps, et les ennemis dont le tir dépend de la distance tirent de deux fois plus
   loin. Au-delà du double, un élément d'arrière-plan qui passe tout près de la caméra déborde : le
   lanceur s'en tient au double. */
static const struct { int nom; const char *valeur; } PORTEES[] = {
    {T_DISTANCE_1, NULL},
    {T_DISTANCE_2, "2"},
};
static const int NB_PORTEES = sizeof PORTEES / sizeof *PORTEES;
#define PORTEE_DEFAUT 1                   /* double */

/* Quand l'affichage est détaché du jeu, il peut attendre l'image suivante du jeu pour se placer entre deux
   (fluide) ou montrer la plus récente et prolonger son mouvement (immédiat). Le jeu publiant ses images dès
   qu'il les a finies, le mode fluide paraît déjà avant la console : c'est le défaut (SFC_LATENCE). C'est un
   réglage de spécialiste : il vit en bas des Réglages. */
static const struct { int nom; const char *valeur; } REACTIONS[] = {
    {T_REACTION_1, NULL},
    {T_REACTION_2, "0"},
    {T_REACTION_3, "auto"},
};
static const int NB_REACTIONS = sizeof REACTIONS / sizeof *REACTIONS;

/* Le stick de la manette en hauteur : pousser fait monter l'Arwing, comme le stylet glissé vers le haut. Star
   Fox 64 fait l'inverse, comme un manche : pousser pique, tirer vers soi monte — qui y est habitué le retrouve
   ici (SFC_MANETTE_INVERSE). Le looping et le demi-tour se demandent le stick vers la montée. */
static const struct { int nom; const char *valeur; } AXES[] = {
    {T_AXE_1, NULL},
    {T_AXE_2, "1"},
};
static const int NB_AXES = sizeof AXES / sizeof *AXES;

/* Les touches du clavier (onglet Contrôles), une par commande, par son code de touche virtuelle Windows. Le
   nom est celui que le jeu lit dans SFC_TOUCHES (runtime/video.cpp, même table) ; la touche d'origine, celle
   qu'il prend sans réglage. Les huit premières forment la colonne des boutons de la console, les quatre
   suivantes la croix, les deux dernières sont au portage : l'avance rapide et la note d'un défaut d'image
   (les cent vingt dernières images montrées, écrites dans journal.txt). */
static const struct { const char *nom; const wchar_t *libelle; int texte; int origine; } COMMANDES[] = {
    {"a", L"A", -1, 'Z'}, {"b", L"B", -1, 'E'}, {"x", L"X", -1, 'A'}, {"y", L"Y", -1, 'S'},
    {"l", L"L", -1, 'Q'}, {"r", L"R", -1, 'W'}, {"start", L"Start", -1, VK_SPACE}, {"select", L"Select", -1, VK_TAB},
    {"haut", NULL, T_HAUT, VK_UP}, {"bas", NULL, T_BAS, VK_DOWN}, {"gauche", NULL, T_GAUCHE, VK_LEFT},
    {"droite", NULL, T_DROITE, VK_RIGHT}, {"rapide", NULL, T_RAPIDE, 'F'}, {"journal", NULL, T_JOURNAL, 'P'},
};
#define NB_COMMANDES ((int)(sizeof COMMANDES / sizeof *COMMANDES))

/* le nom d'une commande : celui du bouton de la console, ou sa traduction */
static const wchar_t *libelle(int i) { return COMMANDES[i].libelle ? COMMANDES[i].libelle : T(COMMANDES[i].texte); }

static int g_touches[NB_COMMANDES];
static HWND g_bouton_touche[NB_COMMANDES];
static HWND g_note_touches;
static int g_capture = -1;             /* la commande qui attend sa touche, ou -1 */
static int g_relache;                  /* la touche qui vient d'être prise : ses répétitions et son relâchement */

/* Les mods (onglet Mods) : des changements de règles, chacun éteint par défaut — éteints, le jeu est celui de la
   cartouche. Le lanceur les passe au jeu par SFC_MOD_… (runtime/mods.c). Le vaisseau se règle pilote par pilote :
   son laser, son verrouillage, la taille de sa jauge et son apparence ; le reste vaut pour le pilote que l'on
   joue. Une liste a pour premier choix « rien », une case cochée vaut « 1 ». */

/* Les pilotes, par leur rang dans le jeu (1 à 16), et ce que SetupCharacters.txt leur donne : laser, verrouillage,
   taille de la jauge (les rangs des listes ci-dessous). Le laser et le verrouillage de Fox changent avec
   l'histoire. */
static const struct { const char *cle; int laser, verrou, jauge; } PILOTES_MOD[] = {
    {NULL, 0, 0, 0},
    {"fox", 1, 1, 2}, {"krystal", 2, 1, 1}, {"falco", 1, 2, 2},
    {"slippy", 3, 0, 0}, {"ash", 2, 1, 1}, {"wolf", 2, 2, 2},
    {"leon", 0, 3, 0}, {"panther", 5, 0, 2}, {"octman", 4, 1, 1},
    {"bill", 2, 0, 1}, {"lucy", 3, 1, 1}, {"amanda", 1, 2, 0},
    {"cat", 4, 1, 1}, {"peppy", 2, 1, 2}, {"james", 2, 1, 2},
    {"jodie", 2, 0, 1},
};
#define NB_PILOTES_MOD 17
#define FOX_MOD 1
static const int NOMS_LASERS[] = {T_LASER_0, T_LASER_1, T_LASER_2, T_LASER_3, T_LASER_4, T_LASER_5};
static const int NOMS_VERROUS[] = {T_VERROU_0, T_VERROU_1, T_VERROU_2, T_VERROU_3};
static const int NOMS_JAUGES[] = {T_JAUGE_0, T_JAUGE_1, T_JAUGE_2};

/* Les listes de choix, par leurs textes, jusqu'à FIN. Le premier choix des listes du vaisseau (PROPRE) est récrit
   pour le pilote montré (« Le sien (plasma) »). */
#define FIN -1
#define PROPRE -2
static const int LASERS_MOD[] = {PROPRE, T_CHOIX_LASER_1, T_CHOIX_LASER_2, T_CHOIX_LASER_3, T_CHOIX_LASER_4,
                                 T_CHOIX_LASER_5, FIN};
static const char *LASERS_VAL[] = {NULL, "simple", "double", "plasma", "courbe", "zapper"};
static const int VERROUS_MOD[] = {PROPRE, T_CHOIX_VERROU_1, T_CHOIX_VERROU_2, T_CHOIX_VERROU_3, FIN};
static const char *VERROUS_VAL[] = {NULL, "simple", "multiple", "large"};
static const int JAUGES_MOD[] = {PROPRE, T_CHOIX_JAUGE_1, T_CHOIX_JAUGE_2, T_CHOIX_JAUGE_3, FIN};
static const char *JAUGES_VAL[] = {NULL, "petite", "moyenne", "grande"};
static const int VAISSEAUX_MOD[] = {
    PROPRE, T_VAISSEAU_1, T_VAISSEAU_2, T_VAISSEAU_3, T_VAISSEAU_4, T_VAISSEAU_5, T_VAISSEAU_6, T_VAISSEAU_7,
    T_VAISSEAU_8, T_VAISSEAU_9, T_VAISSEAU_10, T_VAISSEAU_11, T_VAISSEAU_12, T_VAISSEAU_13, T_VAISSEAU_14,
    T_VAISSEAU_15, T_VAISSEAU_16, FIN};
static const char *VAISSEAUX_VAL[] = {NULL, "fox", "krystal", "falco", "slippy", "ash", "wolf", "leon", "panther",
                                      "octman", "bill", "lucy", "amanda", "cat", "peppy", "james", "jodie"};
static const struct {
    const char *cle;               /* dans lanceur.ini, section [mods] : « slippy_laser » */
    const char *variable;          /* pour le jeu : « fox:plasma,slippy:double » */
    int libelle;
    const int *choix;
    const char **valeurs;
} VAISSEAU_MOD[] = {
    {"laser", "SFC_MOD_LASER", T_M_LASER, LASERS_MOD, LASERS_VAL},
    {"verrouillage", "SFC_MOD_VERROUILLAGE", T_M_VERROU, VERROUS_MOD, VERROUS_VAL},
    {"jauge", "SFC_MOD_JAUGE", T_M_JAUGE, JAUGES_MOD, JAUGES_VAL},
    {"apparence", "SFC_MOD_VAISSEAU", T_M_APPARENCE, VAISSEAUX_MOD, VAISSEAUX_VAL},
};
#define NB_REGLAGES 4
static int g_vaisseau_choix[NB_REGLAGES][NB_PILOTES_MOD];   /* rang dans la liste, pour chaque pilote */
static int g_pilote_vu = FOX_MOD;                            /* le pilote que l'onglet montre */
static HWND g_pilote_liste, g_reglage[NB_REGLAGES];

static const int INFLIGES_MOD[] = {T_INFLIGES_1, T_INFLIGES_2, T_INFLIGES_3, T_INFLIGES_4, T_INFLIGES_5, FIN};
static const char *INFLIGES_VAL[] = {NULL, "2", "4", "10", "un-coup"};
static const int RECUS_MOD[] = {T_RECUS_1, T_RECUS_2, T_RECUS_3, FIN};
static const char *RECUS_VAL[] = {NULL, "0.5", "2"};
static const struct {
    const char *cle;               /* dans lanceur.ini, section [mods] */
    const char *variable;          /* pour le jeu */
    int libelle;
    const int *choix;              /* une liste, ou NULL pour une case à cocher */
    const char **valeurs;
} MODS[] = {
    {"degats", "SFC_MOD_DEGATS", T_DEGATS, INFLIGES_MOD, INFLIGES_VAL},
    {"degats_recus", "SFC_MOD_DEGATS_RECUS", T_DEGATS_RECUS, RECUS_MOD, RECUS_VAL},
    {"invincible", "SFC_MOD_INVINCIBLE", T_INVINCIBLE, NULL, NULL},
    {"turbo", "SFC_MOD_TURBO", T_TURBO, NULL, NULL},
    {"bombes", "SFC_MOD_BOMBES", T_BOMBES, NULL, NULL},
    {"charge", "SFC_MOD_CHARGE", T_CHARGE, NULL, NULL},
};
#define NB_MODS ((int)(sizeof MODS / sizeof *MODS))
#define MOD_RECUS 1
#define MOD_INVINCIBLE 2
static HWND g_mod[NB_MODS];

/* Les réglages se retrouvent d'une fois sur l'autre, dans un fichier à côté du lanceur. */
static wchar_t g_reglages[MAX_PATH];

static int nb_choix(const int *choix) { int n = 0; while (choix[n] != FIN) n++; return n; }
static const wchar_t *texte_choix(int t) { return t == PROPRE ? L"" : T(t); }

/* la valeur d'un mod : son rang dans la liste, ou 0/1 pour une case */
static int valeur_mod(int i) {
    if (MODS[i].choix) {
        int v = (int)SendMessageW(g_mod[i], CB_GETCURSEL, 0, 0);
        return v > 0 && v < nb_choix(MODS[i].choix) ? v : 0;
    }
    return SendMessageW(g_mod[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void cle_mod(int i, wchar_t *cle, int n) { _snwprintf(cle, n, L"%hs", MODS[i].cle); }

static void retenir_mod(int i) {
    wchar_t cle[32], t[16];
    cle_mod(i, cle, 32);
    _snwprintf(t, 16, L"%d", valeur_mod(i));
    WritePrivateProfileStringW(L"mods", cle, t, g_reglages);
}

/* Invincible rend « Dégâts reçus » sans objet : la liste se grise. */
static void accorder_mods(void) {
    EnableWindow(g_mod[MOD_RECUS], !valeur_mod(MOD_INVINCIBLE));
}

/* Le vaisseau, pilote par pilote : une ligne de lanceur.ini par réglage qui n'est pas « rien ». */
static void cle_reglage(int r, int pilote, wchar_t *cle, int n) {
    _snwprintf(cle, n, L"%hs_%hs", PILOTES_MOD[pilote].cle, VAISSEAU_MOD[r].cle);
}

static void retenir_reglage(int r, int pilote) {
    wchar_t cle[48], t[16];
    cle_reglage(r, pilote, cle, 48);
    _snwprintf(t, 16, L"%d", g_vaisseau_choix[r][pilote]);
    WritePrivateProfileStringW(L"mods", cle, g_vaisseau_choix[r][pilote] ? t : NULL, g_reglages);
}

static int pilote_modifie(int pilote) {
    for (int r = 0; r < NB_REGLAGES; r++) if (g_vaisseau_choix[r][pilote]) return 1;
    return 0;
}

/* Les listes du vaisseau montrent le pilote choisi ; leur premier choix dit ce qu'il a de lui-même. */
static void montrer_pilote(void) {
    const int p = g_pilote_vu;
    for (int r = 0; r < NB_REGLAGES; r++) {
        wchar_t t[96];
        if (r <= 1 && p == FOX_MOD) _snwprintf(t, 96, L"%ls", T(T_CELUI_DE_LA_PARTIE));
        else if (r == 0) _snwprintf(t, 96, T(T_LE_SIEN_DE), T(NOMS_LASERS[PILOTES_MOD[p].laser]));
        else if (r == 1) _snwprintf(t, 96, T(T_LE_SIEN_DE), T(NOMS_VERROUS[PILOTES_MOD[p].verrou]));
        else if (r == 2) _snwprintf(t, 96, T(T_LA_SIENNE_DE), T(NOMS_JAUGES[PILOTES_MOD[p].jauge]));
        else _snwprintf(t, 96, L"%ls", T(T_LE_SIEN));
        t[95] = 0;
        SendMessageW(g_reglage[r], CB_DELETESTRING, 0, 0);
        SendMessageW(g_reglage[r], CB_INSERTSTRING, 0, (LPARAM)t);
        SendMessageW(g_reglage[r], CB_SETCURSEL, g_vaisseau_choix[r][p], 0);
    }
}

/* la liste des pilotes marque ceux dont le vaisseau a un réglage */
static void marquer_pilote(int pilote) {
    wchar_t t[48];
    _snwprintf(t, 48, pilote_modifie(pilote) ? T(T_MODIFIE) : L"%ls", nom_pilote(pilote));
    t[47] = 0;
    const int vu = (int)SendMessageW(g_pilote_liste, CB_GETCURSEL, 0, 0);
    SendMessageW(g_pilote_liste, CB_DELETESTRING, pilote - 1, 0);
    SendMessageW(g_pilote_liste, CB_INSERTSTRING, pilote - 1, (LPARAM)t);
    SendMessageW(g_pilote_liste, CB_SETCURSEL, vu, 0);
}

static void retenir(const wchar_t *cle, const wchar_t *valeur) {
    WritePrivateProfileStringW(L"lanceur", cle, valeur, g_reglages);
}

static void retenir_nombre(const wchar_t *cle, int valeur) {
    wchar_t t[16];
    _snwprintf(t, 16, L"%d", valeur);
    retenir(cle, t);
}

/* Les touches ont leur section, [touches], une ligne par commande : son nom, et le code de sa touche. */
static void cle_touche(int i, wchar_t *cle, int n) {
    _snwprintf(cle, n, L"%hs", COMMANDES[i].nom);
}

static void retenir_touche(int i) {
    wchar_t cle[16], t[16];
    cle_touche(i, cle, 16);
    _snwprintf(t, 16, L"%d", g_touches[i]);
    WritePrivateProfileStringW(L"touches", cle, t, g_reglages);
}

/* Le nom d'une touche, tel que Windows le donne pour le clavier de la machine (« Z », « ESPACE », « HAUT »…),
   mis en minuscules sauf la première lettre. Les touches dites étendues — flèches, Inser, Suppr, Début, Fin,
   pages — partagent leur code de balayage avec le pavé numérique : sans le bit 24, la flèche du haut
   s'appellerait « 8 (pavé num.) ». */
static void nom_touche(int vk, wchar_t *nom, int n) {
    UINT balayage = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
    LONG lp = (LONG)(balayage << 16);
    switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT:
    case VK_HOME: case VK_END: case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
    case VK_RCONTROL: case VK_RMENU: case VK_APPS:
        lp |= 1 << 24;
        break;
    }
    if (!vk || !balayage || !GetKeyNameTextW(lp, nom, n)) {
        _snwprintf(nom, n, vk ? T(T_TOUCHE_N) : T(T_AUCUNE_TOUCHE), vk);
        return;
    }
    if (nom[0] && nom[1]) CharLowerW(nom + 1);
}

static void afficher_touche(int i) {
    wchar_t nom[64];
    nom_touche(g_touches[i], nom, 64);
    SetWindowTextW(g_bouton_touche[i], nom);
}

static void annuler_capture(void) {
    if (g_capture < 0) return;
    afficher_touche(g_capture);
    g_capture = -1;
    SetWindowTextW(g_note_touches, T(T_NOTE_TOUCHES));
}

static void commencer_capture(int i) {
    annuler_capture();
    g_capture = i;
    SetWindowTextW(g_bouton_touche[i], L"…");
    wchar_t note[160];
    _snwprintf(note, 160, T(T_CAPTURE), libelle(i));
    note[159] = 0;
    SetWindowTextW(g_note_touches, note);
}

/* La touche choisie pour la commande qui l'attendait. Si une autre commande l'avait, elles échangent : celle-ci
   prend l'ancienne touche de la première, et aucune commande ne se retrouve sans touche. */
static void capturer(int vk) {
    int i = g_capture;
    g_capture = -1;
    g_relache = vk;
    if (vk == VK_ESCAPE) {
        afficher_touche(i);
        SetWindowTextW(g_note_touches, T(T_NOTE_TOUCHES));
        return;
    }
    if (vk == VK_LWIN || vk == VK_RWIN) {
        afficher_touche(i);
        SetWindowTextW(g_note_touches, T(T_TOUCHE_WINDOWS));
        return;
    }
    wchar_t note[256];
    wcsncpy(note, T(T_NOTE_TOUCHES), 256);
    note[255] = 0;
    for (int j = 0; j < NB_COMMANDES; j++) {
        if (j == i || g_touches[j] != vk) continue;
        wchar_t prise[64], rendue[64];
        nom_touche(vk, prise, 64);
        nom_touche(g_touches[i], rendue, 64);
        _snwprintf(note, 256, T(T_ECHANGE), prise, libelle(j), libelle(j), rendue);
        note[255] = 0;
        g_touches[j] = g_touches[i];
        afficher_touche(j);
        retenir_touche(j);
    }
    g_touches[i] = vk;
    afficher_touche(i);
    retenir_touche(i);
    SetWindowTextW(g_note_touches, note);
}

static void touches_d_origine(void) {
    annuler_capture();
    for (int i = 0; i < NB_COMMANDES; i++) {
        g_touches[i] = COMMANDES[i].origine;
        afficher_touche(i);
        retenir_touche(i);
    }
    SetWindowTextW(g_note_touches, T(T_ORIGINE_REVENUE));
}

/* Le jeu se sert dans la cartouche du joueur — sa propre copie, que le portage ne fournit pas : le chemin
   choisi est retenu dans lanceur.ini. Un dossier de données (la cartouche découpée en fichiers par
   tools/build_data.py, pour en modifier les fichiers) reste accepté. Rien n'est copié. */
static int chercher_donnees_dans(const wchar_t *dossier) {
    wchar_t index[MAX_PATH], chemin[MAX_PATH];
    _snwprintf(chemin, MAX_PATH, L"%s\\data", dossier);
    _snwprintf(index, MAX_PATH, L"%s\\index.bin", chemin);
    if (GetFileAttributesW(index) == INVALID_FILE_ATTRIBUTES) return 0;
    SetWindowTextW(g_source, chemin);
    return 1;
}

static int chercher_rom_dans(const wchar_t *dossier) {
    wchar_t motif[MAX_PATH];
    _snwprintf(motif, MAX_PATH, L"%s\\*.nds", dossier);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(motif, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    wchar_t chemin[MAX_PATH];
    _snwprintf(chemin, MAX_PATH, L"%s\\%s", dossier, fd.cFileName);
    SetWindowTextW(g_source, chemin);
    FindClose(h);
    return 1;
}

/* Celle qu'on a choisie la dernière fois ; sinon une cartouche à côté du lanceur, puis dans le dossier
   parent ; un dossier de données en dernier. */
static void chercher_source(void) {
    wchar_t retenue[MAX_PATH];
    GetPrivateProfileStringW(L"lanceur", L"cartouche", L"", retenue, MAX_PATH, g_reglages);
    if (retenue[0] && GetFileAttributesW(retenue) != INVALID_FILE_ATTRIBUTES) {
        SetWindowTextW(g_source, retenue);
        return;
    }
    wchar_t parent[MAX_PATH];
    wcsncpy(parent, g_dossier, MAX_PATH);
    parent[MAX_PATH - 1] = 0;
    wchar_t *sep = wcsrchr(parent, L'\\');
    if (sep) *sep = 0;
    if (chercher_rom_dans(g_dossier)) return;
    if (sep && chercher_rom_dans(parent)) return;
    if (chercher_donnees_dans(g_dossier)) return;
    if (sep) chercher_donnees_dans(parent);
}

/* Le portage a été recompilé depuis la cartouche européenne : son code de jeu, ASFP, est écrit en
   0x0C de l'en-tête. Une autre version (américaine ASFE, japonaise ASFJ) ne place pas son code aux
   mêmes adresses et planterait au démarrage : on le dit plutôt. Rend 1 si c'est la bonne, 0 sinon,
   -1 si le fichier ne se lit pas. */
static int bonne_cartouche(const wchar_t *chemin, char code[5]) {
    HANDLE f = CreateFileW(chemin, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    unsigned char entete[0x10];
    DWORD lu = 0;
    const BOOL ok = ReadFile(f, entete, sizeof entete, &lu, NULL);
    CloseHandle(f);
    if (!ok || lu < sizeof entete) return -1;
    for (int i = 0; i < 4; i++) code[i] = (entete[0x0C + i] >= 0x20 && entete[0x0C + i] < 0x7F) ? (char)entete[0x0C + i] : '?';
    code[4] = 0;
    return memcmp(entete + 0x0C, "ASFP", 4) == 0;
}

static int est_dossier(const wchar_t *chemin) {
    DWORD a = GetFileAttributesW(chemin);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void parcourir(HWND parent) {
    wchar_t fichier[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = T(T_FILTRE);
    ofn.lpstrFile = fichier;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = T(T_TITRE_CHOIX);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    /* Désigner l'index revient à désigner le dossier qui le contient. */
    wchar_t *sep = wcsrchr(fichier, L'\\');
    if (sep && !_wcsicmp(sep + 1, L"index.bin")) *sep = 0;
    SetWindowTextW(g_source, fichier);
    retenir(L"cartouche", fichier);
}

/* La source choisie, vérifiée : rend 1 si elle convient (et dit dans `natif` si c'est un dossier de
   données), 0 après avoir dit pourquoi elle ne convient pas. */
static int source_valable(HWND parent, wchar_t source[MAX_PATH], int *natif) {
    GetWindowTextW(g_source, source, MAX_PATH);
    if (!source[0]) {
        MessageBoxW(parent, T(T_CHOISIR_DABORD), L"Star Fox Command", MB_ICONINFORMATION);
        return 0;
    }
    if (GetFileAttributesW(source) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(parent, T(T_INTROUVABLE), L"Star Fox Command", MB_ICONWARNING);
        return 0;
    }
    *natif = est_dossier(source);
    if (*natif) {
        wchar_t index[MAX_PATH];
        _snwprintf(index, MAX_PATH, L"%s\\index.bin", source);
        if (GetFileAttributesW(index) == INVALID_FILE_ATTRIBUTES) {
            MessageBoxW(parent, T(T_PAS_DONNEES), L"Star Fox Command", MB_ICONWARNING);
            return 0;
        }
    } else {
        char code[5];
        const int bonne = bonne_cartouche(source, code);
        if (bonne < 0) {
            MessageBoxW(parent, T(T_ILLISIBLE), L"Star Fox Command", MB_ICONWARNING);
            return 0;
        }
        if (!bonne) {
            wchar_t t[600];
            _snwprintf(t, 600, T(T_AUTRE_VERSION), code);
            t[599] = 0;
            MessageBoxW(parent, t, L"Star Fox Command", MB_ICONWARNING);
            return 0;
        }
    }
    retenir(L"cartouche", source);
    return 1;
}

/* Le jeu construit : à côté du lanceur ; pendant le développement, dans son répertoire de compilation — le
   plus récent d'abord ; chez le joueur, celui que la préparation a posé dans jeu\. */
static const wchar_t *OU[] = {
    L"%s\\sfc.exe", L"%s\\build\\runtime8\\sfc.exe", L"%s\\runtime8\\sfc.exe",
    L"%s\\build\\runtime7\\sfc.exe", L"%s\\runtime7\\sfc.exe", L"%s\\jeu\\sfc.exe",
};

static int trouver_jeu(wchar_t jeu[MAX_PATH]) {
    for (size_t k = 0; k < sizeof OU / sizeof *OU; k++) {
        _snwprintf(jeu, MAX_PATH, OU[k], g_dossier);
        if (GetFileAttributesW(jeu) != INVALID_FILE_ATTRIBUTES) return 1;
    }
    return 0;
}

/* La version du portage qui a préparé jeu\sfc.exe (jeu\preparation.txt, « version=… »), ou une chaîne vide. */
static void version_preparee(wchar_t version[32]) {
    version[0] = 0;
    wchar_t chemin[MAX_PATH];
    _snwprintf(chemin, MAX_PATH, L"%s\\jeu\\preparation.txt", g_dossier);
    FILE *f = _wfopen(chemin, L"rb");
    if (!f) return;
    char ligne[256];
    while (fgets(ligne, sizeof ligne, f)) {
        if (strncmp(ligne, "version=", 8)) continue;
        ligne[strcspn(ligne, "\r\n")] = 0;
        MultiByteToWideChar(CP_UTF8, 0, ligne + 8, -1, version, 32);
        break;
    }
    fclose(f);
}

/* Le jeu est-il prêt ? Construit, et — s'il l'a été chez le joueur — par cette version du portage : une plus
   récente change le code généré, il faut le préparer à nouveau. */
static int jeu_pret(void) {
    wchar_t jeu[MAX_PATH];
    if (!trouver_jeu(jeu)) return 0;
    const size_t n = wcslen(jeu), m = wcslen(L"\\jeu\\sfc.exe");
    if (n >= m && !_wcsicmp(jeu + n - m, L"\\jeu\\sfc.exe")) {
        wchar_t version[32];
        version_preparee(version);
        return !wcscmp(version, VERSION);
    }
    return 1;
}

/* La langue du jeu choisie : son rang dans LANGUES. */
static int langue_choisie(void) {
    const int k = (int)SendMessageW(g_langue, CB_GETCURSEL, 0, 0);
    return k >= 0 && k < g_nb_langues_vues ? g_langues_vues[k] : 0;
}

static int langue_du_code(const wchar_t *code) {
    for (int i = 0; i < NB_LANGUES; i++) {
        wchar_t c[8];
        _snwprintf(c, 8, L"%hs", LANGUES[i].code);
        if (!wcscmp(c, code)) return i;
    }
    return -1;
}

/* La langue du jeu retenue ; sinon le rang qu'écrivaient les versions d'avant ; sinon celle du lanceur, s'il est
   en français, l'anglais autrement (la cartouche européenne ne porte pas le japonais). */
static int langue_retenue(void) {
    wchar_t code[8];
    GetPrivateProfileStringW(L"lanceur", L"langue_du_jeu", L"", code, 8, g_reglages);
    if (!code[0]) {
        const int v = (int)GetPrivateProfileIntW(L"lanceur", L"langue", -1, g_reglages);
        _snwprintf(code, 8, L"%hs", v >= 0 && v < 5 ? ANCIENNES_LANGUES[v] : g_interface == INTERFACE_FR ? "fr" : "en");
    }
    const int l = langue_du_code(code);
    return l >= 0 ? l : 0;
}

/* La liste des langues du jeu : celles dont la cartouche porte l'archive de textes — toutes les cinq tant
   qu'aucune cartouche n'est reconnue. Le choix `voulue` reste choisi s'il y est. */
static void remplir_langues(const wchar_t *source, int voulue) {
    const char *archives[NB_LANGUES];
    for (int i = 0; i < NB_LANGUES; i++) archives[i] = LANGUES[i].archive;
    const unsigned vues = source && source[0] ? images_archives(source, archives, NB_LANGUES) : 0;
    g_nb_langues_vues = 0;
    for (int i = 0; i < NB_LANGUES; i++)
        if (!vues || (vues >> i & 1)) g_langues_vues[g_nb_langues_vues++] = i;
    SendMessageW(g_langue, CB_RESETCONTENT, 0, 0);
    int rang = 0;
    for (int k = 0; k < g_nb_langues_vues; k++) {
        SendMessageW(g_langue, CB_ADDSTRING, 0, (LPARAM)LANGUES[g_langues_vues[k]].nom);
        if (g_langues_vues[k] == voulue) rang = k;
    }
    SendMessageW(g_langue, CB_SETCURSEL, rang, 0);
}

/* Les réglages de la page Réglages, retenus dans lanceur.ini : en jouant, et avant de changer la langue du
   lanceur (les pages sont alors refaites). Les autres se retiennent dès qu'on les change. */
static void retenir_reglages(void) {
    wchar_t code[8];
    _snwprintf(code, 8, L"%hs", LANGUES[langue_choisie()].code);
    retenir(L"langue_du_jeu", code);
    WritePrivateProfileStringW(L"lanceur", L"langue", NULL, g_reglages);      /* l'ancien rang */
    const int e = (int)SendMessageW(g_echelle, CB_GETCURSEL, 0, 0);
    retenir_nombre(L"finesse", e < 0 ? 0 : e);
    const int f = (int)SendMessageW(g_filtre, CB_GETCURSEL, 0, 0);
    retenir_nombre(L"textures", f < 0 ? 0 : f);
    retenir_nombre(L"son", SendMessageW(g_son, BM_GETCHECK, 0, 0) == BST_CHECKED);
    int rc = (int)SendMessageW(g_cadence, CB_GETCURSEL, 0, 0);
    if (rc < 0 || rc >= NB_CADENCES) rc = CADENCE_DEFAUT;
    retenir(L"cadence", CADENCES[rc]);
    int po = (int)SendMessageW(g_portee, CB_GETCURSEL, 0, 0);
    if (po < 0 || po >= NB_PORTEES) po = PORTEE_DEFAUT;
    retenir_nombre(L"portee", po);
}

/* Le jeu est lancé avec son environnement, hérité de celui-ci : on y pose les réglages. */
static void jouer(HWND parent) {
    wchar_t source[MAX_PATH];
    int natif = 0;
    if (!source_valable(parent, source, &natif)) return;
    SetEnvironmentVariableW(L"SFC_DATA", natif ? source : NULL);

    const int l = langue_choisie();
    SetEnvironmentVariableA("SFC_LANG", LANGUES[l].code);

    int e = (int)SendMessageW(g_echelle, CB_GETCURSEL, 0, 0);
    char echelle[8];
    _snprintf(echelle, sizeof echelle, "%d", e < 0 ? 1 : e + 1);
    SetEnvironmentVariableA("SFC_SCALE", echelle);

    int f = (int)SendMessageW(g_filtre, CB_GETCURSEL, 0, 0);
    char filtre[8];
    _snprintf(filtre, sizeof filtre, "%d", f < 0 ? 0 : f);
    SetEnvironmentVariableA("SFC_FILTER", filtre);

    /* la cadence : 60, 90 ou 120 images par seconde, l'affichage détaché du jeu */
    int rc = (int)SendMessageW(g_cadence, CB_GETCURSEL, 0, 0);
    if (rc < 0 || rc >= NB_CADENCES) rc = CADENCE_DEFAUT;
    const wchar_t *cadence = CADENCES[rc];
    {
        char t[16];
        _snprintf(t, sizeof t, "%ls", cadence);
        SetEnvironmentVariableA("SFC_AFFICHAGE", "libre");
        SetEnvironmentVariableA("SFC_IPS", t);
    }

    int po = (int)SendMessageW(g_portee, CB_GETCURSEL, 0, 0);
    if (po < 0 || po >= NB_PORTEES) po = PORTEE_DEFAUT;
    SetEnvironmentVariableA("SFC_PORTEE", PORTEES[po].valeur);

    /* L'anticipation, toujours : pendant que le jeu attend l'heure de son prochain tour, le portage le calcule
       d'avance avec les commandes du moment, et l'affichage glisse vers un tour déjà connu, sans retard. Le
       tour d'avance est effacé ensuite — le jeu, lui, se joue comme sur la console. C'est elle qui fait tenir
       ensemble la fluidité et la réactivité ; on ne propose plus de la couper. */
    SetEnvironmentVariableA("SFC_APERCUS", "1");

    int rx = g_reaction >= 0 && g_reaction < NB_REACTIONS ? g_reaction : 0;
    SetEnvironmentVariableA("SFC_LATENCE", REACTIONS[rx].valeur);

    int ax = g_axe >= 0 && g_axe < NB_AXES ? g_axe : 0;
    SetEnvironmentVariableA("SFC_MANETTE_INVERSE", AXES[ax].valeur);

    /* Les touches du clavier, commande par commande : « a=0x5A,b=0x45,… » (onglet Contrôles). */
    char touches[512];
    int n = 0;
    for (int i = 0; i < NB_COMMANDES; i++)
        n += _snprintf(touches + n, sizeof touches - (size_t)n, "%s%s=0x%02X", i ? "," : "", COMMANDES[i].nom, g_touches[i]);
    SetEnvironmentVariableA("SFC_TOUCHES", touches);

    /* Les mods : chacun sa variable, absente quand il est éteint ; celles du vaisseau disent pilote et réglage,
       « fox:plasma,slippy:double ». */
    for (int i = 0; i < NB_MODS; i++) {
        const int v = valeur_mod(i);
        SetEnvironmentVariableA(MODS[i].variable, !v ? NULL : MODS[i].valeurs ? MODS[i].valeurs[v] : "1");
        retenir_mod(i);
    }
    for (int r = 0; r < NB_REGLAGES; r++) {
        char v[512];
        int n = 0;
        for (int pi = 1; pi < NB_PILOTES_MOD; pi++) {
            const int k = g_vaisseau_choix[r][pi];
            if (k > 0) n += _snprintf(v + n, sizeof v - (size_t)n, "%s%s:%s", n ? "," : "", PILOTES_MOD[pi].cle,
                                      VAISSEAU_MOD[r].valeurs[k]);
        }
        SetEnvironmentVariableA(VAISSEAU_MOD[r].variable, n > 0 ? v : NULL);
    }

    /* Le coprocesseur porte le son : sans lui le jeu tourne, mais muet. */
    int son = SendMessageW(g_son, BM_GETCHECK, 0, 0) == BST_CHECKED;
    SetEnvironmentVariableA("SFC_ARM7", son ? "1" : "0");
    SetEnvironmentVariableA("SFC_NO_AUDIO", son ? NULL : "1");

    /* Ce que le jeu dit, et surtout pourquoi il s'arrête s'il s'arrête, reste dans journal.txt, à côté
       de la sauvegarde : c'est ce qu'il faut regarder — ou envoyer — après un plantage. */
    SetEnvironmentVariableA("SFC_JOURNAL", "journal.txt");

    retenir_reglages();
    retenir_nombre(L"reaction", rx);
    retenir_nombre(L"manette_axe", ax);
    for (int i = 0; i < NB_COMMANDES; i++) retenir_touche(i);
    WritePrivateProfileStringW(L"lanceur", L"manette_monter", NULL, g_reglages);   /* l'ancien réglage, de sens inverse */
    WritePrivateProfileStringW(L"lanceur", L"anticipation", NULL, g_reglages);   /* un ancien réglage, retiré */

    wchar_t jeu[MAX_PATH];
    if (!trouver_jeu(jeu)) {
        MessageBoxW(parent, T(T_PAS_ENCORE), L"Star Fox Command", MB_ICONWARNING);
        return;
    }

    wchar_t ligne[MAX_PATH * 2 + 8];
    if (natif) _snwprintf(ligne, MAX_PATH * 2 + 8, L"\"%s\"", jeu);
    else _snwprintf(ligne, MAX_PATH * 2 + 8, L"\"%s\" \"%s\"", jeu, source);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    /* Le jeu tourne à côté du lanceur : sa sauvegarde (sauvegarde/) et son journal y restent, où que soit
       rangée la cartouche — et non à côté d'elle, ni dans le répertoire de compilation de sfc.exe. */
    /* sans fenêtre de console : le jeu écrit dans journal.txt */
    if (!CreateProcessW(NULL, ligne, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_dossier, &si, &pi)) {
        MessageBoxW(parent, T(T_PAS_DEMARRE), L"Star Fox Command", MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    PostQuitMessage(0);   /* le lanceur s'efface une fois le jeu parti */
}

/* ---------------------------------------------------------------------------------------------------------
   Préparer le jeu. Le portage ne distribue rien du jeu, pas même son code recompilé : chez le joueur, le jeu
   se construit sur sa machine, depuis sa cartouche, une fois. C'est tools/preparer.py qui le fait — traduire
   le code des deux processeurs, le réécrire en C natif, compiler — ; le lanceur le lance, suit sa sortie
   (« ÉTAPE k/n : … », « AVANCE p », « PRÊT », « ERREUR … ») et la recopie dans preparation.log. Python,
   CMake et GCC sont cherchés dans outils\ à côté du lanceur, puis dans le PATH.
   --------------------------------------------------------------------------------------------------------- */
#define WM_PREPARATION_LIGNE (WM_APP + 1)
#define WM_PREPARATION_FIN   (WM_APP + 2)

static HWND g_fenetre, g_parcourir;
static HANDLE g_travail;                 /* l'objet de tâche : le fermer arrête la préparation et ses enfants */
static int g_prepare;                    /* une préparation est en cours */
static int g_etape, g_nb_etapes = 6;
static wchar_t g_erreur[600];
static wchar_t g_texte_etat[300];

static void accorder_bouton(void) {
    g_scene.pret = jeu_pret();
    if (g_fenetre) InvalidateRect(g_fenetre, NULL, FALSE);
}

/* Le Python livré à côté du lanceur, sinon celui du PATH — sauf l'alias de Windows qui ouvre le Store —,
   sinon le lanceur « py ». Rend 0 s'il n'y en a pas, 2 pour « py » (qui veut -3). */
static int trouver_python(wchar_t py[MAX_PATH]) {
    _snwprintf(py, MAX_PATH, L"%s\\outils\\python\\python.exe", g_dossier);
    if (GetFileAttributesW(py) != INVALID_FILE_ATTRIBUTES) return 1;
    if (SearchPathW(NULL, L"python.exe", NULL, MAX_PATH, py, NULL) && !wcsstr(py, L"\\WindowsApps\\")) return 1;
    if (SearchPathW(NULL, L"py.exe", NULL, MAX_PATH, py, NULL)) return 2;
    return 0;
}

typedef struct { HANDLE lecture, processus; } Suivi;

/* Le fil qui lit la sortie de la préparation : ligne par ligne, recopiée dans preparation.log et envoyée à la
   fenêtre ; à la fin, le code de sortie. */
static DWORD WINAPI suivre_preparation(LPVOID p) {
    Suivi s = *(Suivi *)p;
    free(p);
    wchar_t chemin[MAX_PATH];
    _snwprintf(chemin, MAX_PATH, L"%s\\preparation.log", g_dossier);
    FILE *journal = _wfopen(chemin, L"wb");
    char tampon[4096], ligne[2048];
    size_t n = 0;
    DWORD lu;
    while (ReadFile(s.lecture, tampon, sizeof tampon, &lu, NULL) && lu) {
        if (journal) { fwrite(tampon, 1, lu, journal); fflush(journal); }
        for (DWORD i = 0; i < lu; i++) {
            const char o = tampon[i];
            if (o == '\n' || n + 1 >= sizeof ligne) {
                ligne[n] = 0;
                if (n && ligne[n - 1] == '\r') ligne[n - 1] = 0;
                const int w = MultiByteToWideChar(CP_UTF8, 0, ligne, -1, NULL, 0);
                wchar_t *texte = malloc((size_t)w * sizeof *texte);
                if (texte) {
                    MultiByteToWideChar(CP_UTF8, 0, ligne, -1, texte, w);
                    if (!PostMessageW(g_fenetre, WM_PREPARATION_LIGNE, 0, (LPARAM)texte)) free(texte);
                }
                n = 0;
                if (o != '\n') ligne[n++] = o;
            } else {
                ligne[n++] = o;
            }
        }
    }
    if (journal) fclose(journal);
    CloseHandle(s.lecture);
    WaitForSingleObject(s.processus, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(s.processus, &code);
    CloseHandle(s.processus);
    PostMessageW(g_fenetre, WM_PREPARATION_FIN, code, 0);
    return 0;
}

static void montrer_avancement(int oui) {
    g_scene.prepare = oui;
    EnableWindow(g_source, !oui);
    EnableWindow(g_parcourir, !oui);
    InvalidateRect(g_fenetre, NULL, FALSE);
}

static void preparer(HWND parent) {
    wchar_t source[MAX_PATH];
    int natif = 0;
    if (!source_valable(parent, source, &natif)) return;
    if (natif) {
        MessageBoxW(parent, T(T_IL_FAUT_NDS), L"Star Fox Command", MB_ICONINFORMATION);
        return;
    }
    wchar_t script[MAX_PATH], py[MAX_PATH];
    _snwprintf(script, MAX_PATH, L"%s\\tools\\preparer.py", g_dossier);
    if (GetFileAttributesW(script) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(parent, T(T_SANS_PREPARER), L"Star Fox Command", MB_ICONWARNING);
        return;
    }
    const int sorte = trouver_python(py);
    if (!sorte) {
        MessageBoxW(parent, T(T_SANS_PYTHON), L"Star Fox Command", MB_ICONWARNING);
        return;
    }
    wchar_t version[32];
    version_preparee(version);
    wchar_t question[800];
    _snwprintf(question, 800, T(T_QUESTION), version[0] ? T(T_VERSION_PRECEDENTE) : L"");
    question[799] = 0;
    if (MessageBoxW(parent, question, T(T_TITRE_PREPARER), MB_ICONQUESTION | MB_OKCANCEL) != IDOK) return;

    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE lecture, ecriture;
    if (!CreatePipe(&lecture, &ecriture, &sa, 0)) return;
    SetHandleInformation(lecture, HANDLE_FLAG_INHERIT, 0);
    HANDLE rien = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = rien;
    si.hStdOutput = ecriture;
    si.hStdError = ecriture;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    wchar_t ligne[MAX_PATH * 3 + 48];
    _snwprintf(ligne, sizeof ligne / sizeof *ligne, L"\"%s\"%s -X utf8 \"%s\" \"%s\" --langue %hs",
               py, sorte == 2 ? L" -3" : L"", script, source, code_interface(g_interface));
    /* en dessous de la priorité normale, que ses enfants héritent : la machine reste utilisable pendant ce temps */
    const BOOL parti = CreateProcessW(NULL, ligne, NULL, NULL, TRUE,
                                      CREATE_NO_WINDOW | CREATE_SUSPENDED | BELOW_NORMAL_PRIORITY_CLASS,
                                      NULL, g_dossier, &si, &pi);
    CloseHandle(ecriture);
    if (rien != INVALID_HANDLE_VALUE) CloseHandle(rien);
    if (!parti) {
        CloseHandle(lecture);
        MessageBoxW(parent, T(T_PREPARATION_PAS_DEMARREE), L"Star Fox Command", MB_ICONERROR);
        return;
    }
    /* Une tâche Windows réunit Python et tout ce qu'il lance (CMake, le compilateur) : la fermer — en quittant
       le lanceur — les arrête tous. */
    g_travail = CreateJobObjectW(NULL, NULL);
    if (g_travail) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limite;
        memset(&limite, 0, sizeof limite);
        limite.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_travail, JobObjectExtendedLimitInformation, &limite, sizeof limite);
        AssignProcessToJobObject(g_travail, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    Suivi *s = malloc(sizeof *s);
    s->lecture = lecture;
    s->processus = pi.hProcess;
    HANDLE fil = CreateThread(NULL, 0, suivre_preparation, s, 0, NULL);
    if (fil) CloseHandle(fil);

    g_prepare = 1;
    g_etape = 0;
    g_erreur[0] = 0;
    _snwprintf(g_texte_etat, 300, L"%ls", T(T_EN_PREPARATION));
    g_scene.etat = g_texte_etat;
    g_scene.avance = 0;
    montrer_avancement(1);
}

/* Où en est la barre au début de chaque étape, en millièmes : la traduction et la compilation sont les
   plus longues ; la compilation avance d'elle-même, par AVANCE. */
static const int DEBUT_ETAPE[] = {0, 0, 10, 40, 250, 270, 350, 1000};

/* L'étape en cours, dans la langue du lanceur : le libellé de preparer.py n'est que pour qui le lance à la main. */
static void ecrire_etape(void) {
    if (g_etape >= 1 && g_etape <= 6) _snwprintf(g_texte_etat, 300, T(T_ETAPE), g_etape, g_nb_etapes, T(T_ETAPE_1 + g_etape - 1));
    else _snwprintf(g_texte_etat, 300, L"%ls", T(T_EN_PREPARATION));
    g_texte_etat[299] = 0;
}

static void ligne_de_preparation(const wchar_t *t) {
    int k, n;
    if (swscanf(t, L"ÉTAPE %d/%d", &k, &n) == 2 && k >= 1 && k <= 6) {
        /* l'étape se dit dans la langue du lanceur : le libellé de preparer.py n'est que pour qui le lance à la main */
        g_etape = k;
        g_nb_etapes = n;
        ecrire_etape();
        g_scene.avance = DEBUT_ETAPE[k];
    } else if (swscanf(t, L"AVANCE %d", &k) == 1 && g_etape == 6) {
        g_scene.avance = DEBUT_ETAPE[6] + (DEBUT_ETAPE[7] - DEBUT_ETAPE[6]) * k / 100;
    } else if (!wcsncmp(t, L"ERREUR ", 7)) {
        wcsncpy(g_erreur, t + 7, sizeof g_erreur / sizeof *g_erreur - 1);
    }
}

static void fin_de_preparation(HWND h, DWORD code) {
    g_prepare = 0;
    if (g_travail) { CloseHandle(g_travail); g_travail = NULL; }
    montrer_avancement(0);
    accorder_bouton();
    if (!code && jeu_pret()) {
        MessageBoxW(h, T(T_REUSSI), L"Star Fox Command", MB_ICONINFORMATION);
        return;
    }
    wchar_t t[900];
    _snwprintf(t, 900, T(T_ECHEC), g_erreur[0] ? T(T_ECHEC_DEUX_POINTS) : T(T_ECHEC_POINT), g_erreur);
    t[899] = 0;
    MessageBoxW(h, t, L"Star Fox Command", MB_ICONERROR);
}

/* ---------------------------------------------------------------------------------------------------------
   La fenêtre. Elle est sans cadre et dessinée de bout en bout par l'habillage (habillage.c) : le fond, le
   titre, le menu, l'état de la cartouche, le texte qui défile. Les réglages restent des contrôles Windows,
   posés sur trois pages à fond sombre qui s'ouvrent dans le panneau de droite.
   --------------------------------------------------------------------------------------------------------- */

static HWND etiquette(HWND parent, const wchar_t *texte, int x, int y, int w) {
    return CreateWindowExW(0, L"STATIC", texte, WS_CHILD | WS_VISIBLE,
                           x, y, w, 18, parent, NULL, NULL, NULL);
}

static HWND titre(HWND parent, const wchar_t *texte, int x, int y, int w) {
    HWND t = etiquette(parent, texte, x, y, w);
    if (g_nb_titres < (int)(sizeof g_titres / sizeof *g_titres)) g_titres[g_nb_titres++] = t;
    return t;
}

static HWND liste(HWND parent, int id, int x, int y, int w) {
    return CreateWindowExW(0, L"COMBOBOX", NULL,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                           x, y, w, 200, parent, (HMENU)(INT_PTR)id, NULL, NULL);
}

static HWND liste_longue(HWND parent, int id, int x, int y, int w) {
    return CreateWindowExW(0, L"COMBOBOX", NULL,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                           x, y, w, 320, parent, (HMENU)(INT_PTR)id, NULL, NULL);
}

/* Un bouton dessiné par la page (WM_DRAWITEM) : sombre, bordé de bleu, orange quand il attend une touche. */
static HWND bouton(HWND parent, const wchar_t *texte, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", texte, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
}

static void creer_page_reglages(HWND p) {
    etiquette(p, T(T_L_CARTOUCHE), 16, 16, 90);
    g_source = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                               110, 14, 300, 22, p, (HMENU)ID_SOURCE, NULL, NULL);
    g_parcourir = bouton(p, T(T_PARCOURIR), ID_PARCOURIR, 418, 14, 92, 22);

    etiquette(p, T(T_L_LANGUE), 16, 56, 92);
    g_langue = liste(p, ID_LANGUE, 110, 54, 150);           /* remplie d'après la cartouche (remplir_langues) */
    etiquette(p, T(T_NOTE_LANGUE), 110, 80, 410);

    etiquette(p, T(T_L_FINESSE), 16, 112, 92);
    g_echelle = liste(p, ID_ECHELLE, 110, 110, 150);
    for (int t = T_FINESSE_1; t <= T_FINESSE_4; t++) SendMessageW(g_echelle, CB_ADDSTRING, 0, (LPARAM)T(t));
    SendMessageW(g_echelle, CB_SETCURSEL, 0, 0);
    etiquette(p, T(T_NOTE_FINESSE), 270, 112, 254);

    etiquette(p, T(T_L_TEXTURES), 16, 148, 92);
    g_filtre = liste(p, ID_FILTRE, 110, 146, 150);
    for (int t = T_TEXTURES_1; t <= T_TEXTURES_3; t++) SendMessageW(g_filtre, CB_ADDSTRING, 0, (LPARAM)T(t));
    SendMessageW(g_filtre, CB_SETCURSEL, 0, 0);

    etiquette(p, T(T_L_CADENCE), 16, 184, 92);
    g_cadence = liste(p, ID_CADENCE, 110, 182, 150);
    for (int i = 0; i < NB_CADENCES; i++)
        SendMessageW(g_cadence, CB_ADDSTRING, 0, (LPARAM)CADENCES[i]);
    SendMessageW(g_cadence, CB_SETCURSEL, CADENCE_DEFAUT, 0);
    etiquette(p, T(T_NOTE_CADENCE), 270, 184, 254);

    etiquette(p, T(T_L_DISTANCE), 16, 220, 92);
    g_portee = liste(p, ID_PORTEE, 110, 218, 150);
    for (int i = 0; i < NB_PORTEES; i++)
        SendMessageW(g_portee, CB_ADDSTRING, 0, (LPARAM)T(PORTEES[i].nom));
    SendMessageW(g_portee, CB_SETCURSEL, PORTEE_DEFAUT, 0);
    etiquette(p, T(T_NOTE_DISTANCE), 270, 220, 254);

    g_son = CreateWindowExW(0, L"BUTTON", T(T_SON),
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                            110, 254, 150, 22, p, (HMENU)ID_SON, NULL, NULL);
    SendMessageW(g_son, BM_SETCHECK, BST_CHECKED, 0);

    titre(p, T(T_SPECIALISTES), 16, 300, 300);
    etiquette(p, T(T_L_REACTION), 16, 330, 92);
    g_liste_reaction = liste(p, ID_REACTION, 110, 328, 410);
    for (int i = 0; i < NB_REACTIONS; i++) SendMessageW(g_liste_reaction, CB_ADDSTRING, 0, (LPARAM)T(REACTIONS[i].nom));
    SendMessageW(g_liste_reaction, CB_SETCURSEL, 0, 0);
}

/* Deux colonnes : les boutons de la console à gauche ; la croix, puis les raccourcis du portage à droite.
   Chaque commande a son bouton, qui montre sa touche ; un clic, et la touche suivante devient la sienne. */
static void creer_page_controles(HWND p) {
    titre(p, T(T_BOUTONS_CONSOLE), 16, 10, 230);
    titre(p, T(T_CROIX), 280, 10, 230);
    titre(p, T(T_RACCOURCIS), 280, 150, 230);
    for (int i = 0; i < NB_COMMANDES; i++) {
        int x, y;
        if (i < 8) { x = 16; y = 34 + i * 26; }
        else if (i < 12) { x = 280; y = 34 + (i - 8) * 26; }
        else { x = 280; y = 172 + (i - 12) * 26; }
        int large = i < 8 ? 100 : 150;
        etiquette(p, libelle(i), x, y + 4, large);
        g_bouton_touche[i] = bouton(p, L"", ID_TOUCHE + i, x + large + 4, y, i < 8 ? 120 : 76, 24);
    }
    g_note_touches = CreateWindowExW(0, L"STATIC", T(T_NOTE_TOUCHES), WS_CHILD | WS_VISIBLE,
                                     16, 256, 370, 40, p, NULL, NULL, NULL);
    bouton(p, T(T_ORIGINE), ID_ORIGINE, 396, 258, 124, 26);
    titre(p, T(T_MANETTE), 16, 312, 300);
    etiquette(p, T(T_STICK), 16, 342, 110);
    g_liste_axe = liste(p, ID_AXE, 130, 340, 390);
    for (int i = 0; i < NB_AXES; i++) SendMessageW(g_liste_axe, CB_ADDSTRING, 0, (LPARAM)T(AXES[i].nom));
    SendMessageW(g_liste_axe, CB_SETCURSEL, 0, 0);
}

/* Deux colonnes : à gauche les listes — le vaisseau du pilote choisi, puis les dégâts ; à droite les cases,
   valables pour le pilote que l'on joue. Dessous, ce que les mods changent à la sauvegarde, et « Aucun mod ». */
static void creer_page_mods(HWND p) {
    titre(p, T(T_VAISSEAU_DE), 16, 10, 110);
    g_pilote_liste = liste_longue(p, ID_PILOTE_MOD, 130, 6, 210);
    for (int i = 1; i < NB_PILOTES_MOD; i++) SendMessageW(g_pilote_liste, CB_ADDSTRING, 0, (LPARAM)nom_pilote(i));
    SendMessageW(g_pilote_liste, CB_SETCURSEL, g_pilote_vu - 1, 0);
    for (int r = 0; r < NB_REGLAGES; r++) {
        const int y = 38 + r * 28;
        etiquette(p, T(VAISSEAU_MOD[r].libelle), 16, y + 4, 110);
        g_reglage[r] = liste_longue(p, ID_REGLAGE_MOD + r, 130, y, 210);
        for (int k = 0; VAISSEAU_MOD[r].choix[k] != FIN; k++)
            SendMessageW(g_reglage[r], CB_ADDSTRING, 0, (LPARAM)texte_choix(VAISSEAU_MOD[r].choix[k]));
    }
    titre(p, T(T_TITRE_DEGATS), 16, 158, 300);
    titre(p, T(T_TOUS_PILOTES), 362, 10, 160);
    for (int i = 0; i < NB_MODS; i++) {
        const int id = ID_MOD + i;
        if (MODS[i].choix) {
            const int y = 182 + i * 28;
            etiquette(p, T(MODS[i].libelle), 16, y + 4, 110);
            g_mod[i] = liste(p, id, 130, y, 210);
            for (int k = 0; MODS[i].choix[k] != FIN; k++)
                SendMessageW(g_mod[i], CB_ADDSTRING, 0, (LPARAM)texte_choix(MODS[i].choix[k]));
            SendMessageW(g_mod[i], CB_SETCURSEL, 0, 0);
        } else {
            const int y = 34 + (i - MOD_INVINCIBLE) * 28;
            g_mod[i] = CreateWindowExW(0, L"BUTTON", T(MODS[i].libelle), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                       362, y, 166, 22, p, (HMENU)(INT_PTR)id, NULL, NULL);
        }
    }
    CreateWindowExW(0, L"STATIC", T(T_NOTE_MODS), WS_CHILD | WS_VISIBLE, 16, 256, 370, 40, p, NULL, NULL, NULL);
    bouton(p, T(T_AUCUN_MOD), ID_SANS_MOD, 396, 258, 124, 26);
}

static void aucun_mod(void) {
    for (int i = 0; i < NB_MODS; i++) {
        if (MODS[i].choix) SendMessageW(g_mod[i], CB_SETCURSEL, 0, 0);
        else SendMessageW(g_mod[i], BM_SETCHECK, BST_UNCHECKED, 0);
        retenir_mod(i);
    }
    for (int r = 0; r < NB_REGLAGES; r++)
        for (int pi = 1; pi < NB_PILOTES_MOD; pi++) {
            g_vaisseau_choix[r][pi] = 0;
            retenir_reglage(r, pi);
        }
    for (int pi = 1; pi < NB_PILOTES_MOD; pi++) marquer_pilote(pi);
    montrer_pilote();
    accorder_mods();
}

/* 0 l'accueil ; 1, 2, 3 une page ouverte dans le panneau. */
static void montrer_page(int k) {
    annuler_capture();
    g_scene.page = k;
    for (int i = 0; i < 3; i++) ShowWindow(g_pages[i], i + 1 == k ? SW_SHOW : SW_HIDE);
    InvalidateRect(g_fenetre, NULL, FALSE);
}

static BOOL CALLBACK poser_police(HWND e, LPARAM police) {
    SendMessageW(e, WM_SETFONT, (WPARAM)police, TRUE);
    return TRUE;
}

static HBRUSH g_pinceau_panneau, g_pinceau_champ;

static int est_titre(HWND w) {
    for (int i = 0; i < g_nb_titres; i++)
        if (g_titres[i] == w) return 1;
    return 0;
}

static void dessiner_bouton(const DRAWITEMSTRUCT *d) {
    const int capture = g_capture >= 0 && (int)d->CtlID == ID_TOUCHE + g_capture;
    const int presse = d->itemState & ODS_SELECTED, grise = d->itemState & ODS_DISABLED;
    HBRUSH fond = CreateSolidBrush(presse ? habillage_couleur(C_BORD) : habillage_couleur(C_CHAMP));
    FillRect(d->hDC, &d->rcItem, fond);
    DeleteObject(fond);
    HBRUSH bord = CreateSolidBrush(capture ? habillage_couleur(C_ACCENT) : habillage_couleur(C_BORD));
    FrameRect(d->hDC, &d->rcItem, bord);
    if (capture) {
        RECT r = d->rcItem;
        InflateRect(&r, -1, -1);
        FrameRect(d->hDC, &r, bord);
    }
    DeleteObject(bord);
    wchar_t t[96];
    GetWindowTextW(d->hwndItem, t, 96);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, grise ? habillage_couleur(C_DISCRET) : capture ? habillage_couleur(C_ACCENT) : habillage_couleur(C_TEXTE));
    RECT r = d->rcItem;
    DrawTextW(d->hDC, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (d->itemState & ODS_FOCUS) {
        RECT f = d->rcItem;
        InflateRect(&f, -3, -3);
        DrawFocusRect(d->hDC, &f);
    }
}

/* Une page : fond sombre, textes clairs, intitulés en bleu ; ce que ses contrôles annoncent revient à la fenêtre. */
static LRESULT CALLBACK proc_page(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
    case WM_NOTIFY:
        return SendMessageW(GetParent(h), msg, wp, lp);
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)wp, &r, g_pinceau_panneau);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HWND c = (HWND)lp;
        wchar_t t[4] = L"";
        GetWindowTextW(c, t, 4);
        SetTextColor((HDC)wp, est_titre(c) ? habillage_couleur(C_TITRE) :
                              t[0] == L'(' || t[0] == L'（' ? habillage_couleur(C_DISCRET) :
                              IsWindowEnabled(c) ? habillage_couleur(C_TEXTE) : habillage_couleur(C_DISCRET));
        SetBkColor((HDC)wp, habillage_couleur(C_PANNEAU));
        return (LRESULT)g_pinceau_panneau;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wp, habillage_couleur(C_TEXTE));
        SetBkColor((HDC)wp, habillage_couleur(C_CHAMP));
        return (LRESULT)g_pinceau_champ;
    case WM_DRAWITEM:
        dessiner_bouton((const DRAWITEMSTRUCT *)lp);
        return TRUE;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* La source a changé (ou vient d'être trouvée) : son nom, si elle est reconnue, ses images et son icône. */
static void source_changee(void) {
    wchar_t source[MAX_PATH];
    GetWindowTextW(g_source, source, MAX_PATH);
    g_scene.cartouche = NULL;
    g_scene.cartouche_bonne = 0;
    if (source[0]) {
        const wchar_t *nom = wcsrchr(source, L'\\');
        wcsncpy(g_nom_cartouche, nom ? nom + 1 : source, MAX_PATH - 1);
        g_nom_cartouche[MAX_PATH - 1] = 0;
        g_scene.cartouche = g_nom_cartouche;
        char code[5];
        if (est_dossier(source)) {
            wchar_t index[MAX_PATH];
            _snwprintf(index, MAX_PATH, L"%s\\index.bin", source);
            g_scene.cartouche_bonne = GetFileAttributesW(index) != INVALID_FILE_ATTRIBUTES;
        } else {
            g_scene.cartouche_bonne = bonne_cartouche(source, code) == 1;
        }
    }
    /* les langues du jeu, d'après la cartouche ; le choix en cours reste s'il y est */
    remplir_langues(g_scene.cartouche_bonne ? source : NULL, g_nb_langues_vues ? langue_choisie() : langue_retenue());
    static HICON icone;
    HICON nouvelle = habillage_source(g_scene.cartouche_bonne ? source : NULL);
    if (nouvelle) {
        SendMessageW(g_fenetre, WM_SETICON, ICON_BIG, (LPARAM)nouvelle);
        SendMessageW(g_fenetre, WM_SETICON, ICON_SMALL, (LPARAM)nouvelle);
        if (icone) DestroyIcon(icone);
        icone = nouvelle;
    }
    accorder_bouton();
}

/* Les réglages de la dernière fois, posés dans les pages. */
static void charger_reglages(void) {
    int v = (int)GetPrivateProfileIntW(L"lanceur", L"finesse", 0, g_reglages);
    SendMessageW(g_echelle, CB_SETCURSEL, v >= 0 && v < 4 ? v : 0, 0);
    v = (int)GetPrivateProfileIntW(L"lanceur", L"textures", 0, g_reglages);
    SendMessageW(g_filtre, CB_SETCURSEL, v >= 0 && v < 3 ? v : 0, 0);
    SendMessageW(g_son, BM_SETCHECK, GetPrivateProfileIntW(L"lanceur", L"son", 1, g_reglages) ? BST_CHECKED : BST_UNCHECKED, 0);
    wchar_t cadence[64];
    GetPrivateProfileStringW(L"lanceur", L"cadence", CADENCES[CADENCE_DEFAUT], cadence, 64, g_reglages);
    SendMessageW(g_cadence, CB_SETCURSEL, rang_cadence(cadence), 0);
    v = (int)GetPrivateProfileIntW(L"lanceur", L"portee", PORTEE_DEFAUT, g_reglages);
    SendMessageW(g_portee, CB_SETCURSEL, v >= 0 && v < NB_PORTEES ? v : PORTEE_DEFAUT, 0);
    v = (int)GetPrivateProfileIntW(L"lanceur", L"reaction", 0, g_reglages);
    g_reaction = v >= 0 && v < NB_REACTIONS ? v : 0;
    SendMessageW(g_liste_reaction, CB_SETCURSEL, g_reaction, 0);
    v = (int)GetPrivateProfileIntW(L"lanceur", L"manette_axe", 0, g_reglages);
    g_axe = v >= 0 && v < NB_AXES ? v : 0;
    SendMessageW(g_liste_axe, CB_SETCURSEL, g_axe, 0);
    for (int i = 0; i < NB_COMMANDES; i++) {
        wchar_t cle[16];
        cle_touche(i, cle, 16);
        v = (int)GetPrivateProfileIntW(L"touches", cle, COMMANDES[i].origine, g_reglages);
        g_touches[i] = v > 0 && v < 256 ? v : COMMANDES[i].origine;
        afficher_touche(i);
    }
    for (int i = 0; i < NB_MODS; i++) {
        wchar_t cle[32];
        cle_mod(i, cle, 32);
        v = (int)GetPrivateProfileIntW(L"mods", cle, 0, g_reglages);
        if (MODS[i].choix) SendMessageW(g_mod[i], CB_SETCURSEL, v > 0 && v < nb_choix(MODS[i].choix) ? v : 0, 0);
        else SendMessageW(g_mod[i], BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    accorder_mods();
    for (int r = 0; r < NB_REGLAGES; r++)
        for (int pi = 1; pi < NB_PILOTES_MOD; pi++) {
            wchar_t cle[48];
            cle_reglage(r, pi, cle, 48);
            v = (int)GetPrivateProfileIntW(L"mods", cle, 0, g_reglages);
            g_vaisseau_choix[r][pi] = v > 0 && v < nb_choix(VAISSEAU_MOD[r].choix) ? v : 0;
        }
    /* les réglages d'avant les pilotes, qui étaient ceux de Fox (l'apparence ne comptait pas son vaisseau à
       lui : un rang de plus) */
    static const wchar_t *ANCIENS[NB_REGLAGES] = {L"laser", L"verrouillage", NULL, L"apparence"};
    for (int r = 0; r < NB_REGLAGES; r++) {
        if (!ANCIENS[r]) continue;
        v = (int)GetPrivateProfileIntW(L"mods", ANCIENS[r], 0, g_reglages);
        const int k = r == 3 && v > 0 ? v + 1 : v;
        if (k > 0 && k < nb_choix(VAISSEAU_MOD[r].choix) && !g_vaisseau_choix[r][FOX_MOD]) {
            g_vaisseau_choix[r][FOX_MOD] = k;
            retenir_reglage(r, FOX_MOD);
        }
        WritePrivateProfileStringW(L"mods", ANCIENS[r], NULL, g_reglages);
    }
    for (int pi = 1; pi < NB_PILOTES_MOD; pi++) marquer_pilote(pi);
    montrer_pilote();
}

static HFONT g_police, g_grasse;

/* Les trois pages, dans le panneau de droite, cachées tant qu'on ne les ouvre pas, avec les réglages retenus. Leur
   police est lisible plutôt que celle d'origine, qui date de Windows 3 ; en japonais, une police japonaise. */
static void creer_pages(HWND h) {
    if (g_police) DeleteObject(g_police);
    if (g_grasse) DeleteObject(g_grasse);
    g_police = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, police_interface());
    g_grasse = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, police_interface());
    g_nb_titres = 0;
    const RECT zone = habillage_zone_page();
    for (int i = 0; i < 3; i++)
        g_pages[i] = CreateWindowExW(WS_EX_CONTROLPARENT, L"SfcPage", NULL, WS_CHILD | WS_CLIPSIBLINGS,
                                     zone.left, zone.top, zone.right - zone.left, zone.bottom - zone.top,
                                     h, NULL, NULL, NULL);
    creer_page_reglages(g_pages[0]);
    creer_page_controles(g_pages[1]);
    creer_page_mods(g_pages[2]);
    charger_reglages();
    for (int i = 0; i < 3; i++) EnumChildWindows(g_pages[i], poser_police, (LPARAM)g_police);
    for (int i = 0; i < g_nb_titres; i++) SendMessageW(g_titres[i], WM_SETFONT, (WPARAM)g_grasse, TRUE);
    wchar_t titre_fenetre[128];
    _snwprintf(titre_fenetre, 128, T(T_TITRE_FENETRE), VERSION);
    titre_fenetre[127] = 0;
    SetWindowTextW(h, titre_fenetre);
}

/* La langue du lanceur change : retenue, puis les pages refaites dans la nouvelle — ce qu'elles montraient retenu
   d'abord, la cartouche gardée telle quelle —, l'habillage avec ses polices. */
static void changer_interface(HWND h, int langue) {
    if (langue == g_interface || langue < 0 || langue >= NB_INTERFACES) return;
    annuler_capture();
    retenir_reglages();
    wchar_t source[MAX_PATH];
    GetWindowTextW(g_source, source, MAX_PATH);
    const BOOL actifs = IsWindowEnabled(g_source);
    for (int i = 0; i < 3; i++) DestroyWindow(g_pages[i]);
    g_interface = langue;
    wchar_t code[8];
    _snwprintf(code, 8, L"%hs", code_interface(langue));
    retenir(L"interface", code);
    habillage_langue();
    g_nb_langues_vues = 0;                  /* la langue du jeu se relit dans lanceur.ini */
    creer_pages(h);
    SetWindowTextW(g_source, source);
    EnableWindow(g_source, actifs);
    EnableWindow(g_parcourir, actifs);
    source_changee();
    if (g_prepare) ecrire_etape();
    montrer_page(g_scene.page);
}

static void agir(HWND h, int e) {
    switch (e) {
    case E_JOUER: {
        if (g_prepare) return;
        wchar_t source[MAX_PATH];
        GetWindowTextW(g_source, source, MAX_PATH);
        if (!source[0]) {                       /* pas encore de cartouche : on la demande d'abord */
            parcourir(h);
            source_changee();
            GetWindowTextW(g_source, source, MAX_PATH);
            if (!source[0]) return;
        }
        if (jeu_pret()) jouer(h);
        else preparer(h);
        return;
    }
    case E_REGLAGES:
    case E_CONTROLES:
    case E_MODS:
        montrer_page(g_scene.page == e ? 0 : e);
        return;
    case E_QUITTER:
    case E_FERMER:
        PostMessageW(h, WM_CLOSE, 0, 0);
        return;
    case E_REDUIRE:
        ShowWindow(h, SW_MINIMIZE);
        return;
    case E_CARTOUCHE:
        if (g_prepare) return;
        parcourir(h);
        source_changee();
        return;
    case E_LANGUE_EN:
    case E_LANGUE_FR:
    case E_LANGUE_JA:
        changer_interface(h, e - E_LANGUE_EN);
        return;
    }
}

static LARGE_INTEGER g_horloge, g_frequence;

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_fenetre = h;
        habillage_creer(h, VERSION);
        g_pinceau_panneau = CreateSolidBrush(habillage_couleur(C_PANNEAU));
        g_pinceau_champ = CreateSolidBrush(habillage_couleur(C_CHAMP));
        creer_pages(h);
        chercher_source();
        source_changee();
        QueryPerformanceFrequency(&g_frequence);
        QueryPerformanceCounter(&g_horloge);
        SetTimer(h, 1, 16, NULL);
        return 0;
    }
    case WM_TIMER: {
        LARGE_INTEGER maintenant;
        QueryPerformanceCounter(&maintenant);
        const double dt = (double)(maintenant.QuadPart - g_horloge.QuadPart) / (double)g_frequence.QuadPart;
        g_horloge = maintenant;
        if (IsIconic(h)) return 0;
        habillage_avancer(dt);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        habillage_peindre(dc, &g_scene);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int e = habillage_cible((short)LOWORD(lp), (short)HIWORD(lp), &g_scene);
        if (e != g_scene.survol) {
            g_scene.survol = e;
            TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_scene.survol = E_RIEN;
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && g_scene.survol != E_RIEN && (HWND)wp == h) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        const int e = habillage_cible((short)LOWORD(lp), (short)HIWORD(lp), &g_scene);
        if (e == E_RIEN) {                       /* ailleurs, la fenêtre se déplace : elle n'a pas de barre de titre */
            ReleaseCapture();
            SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        g_scene.appui = e;
        SetCapture(h);
        return 0;
    }
    case WM_LBUTTONUP: {
        const int e = habillage_cible((short)LOWORD(lp), (short)HIWORD(lp), &g_scene), appui = g_scene.appui;
        g_scene.appui = E_RIEN;
        ReleaseCapture();
        if (appui != E_RIEN && e == appui) agir(h, e);
        return 0;
    }
    case WM_PREPARATION_LIGNE:
        ligne_de_preparation((const wchar_t *)lp);
        free((void *)lp);
        return 0;
    case WM_PREPARATION_FIN:
        fin_de_preparation(h, (DWORD)wp);
        return 0;
    case WM_CLOSE:
        if (g_prepare) {
            if (MessageBoxW(h, T(T_ARRETER), L"Star Fox Command", MB_ICONQUESTION | MB_YESNO) != IDYES)
                return 0;
            if (g_travail) TerminateJobObject(g_travail, 1);
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) annuler_capture();
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) agir(h, E_JOUER);                     /* Entrée */
        else if (LOWORD(wp) == IDCANCEL) { if (g_scene.page) montrer_page(0); }   /* Échap : on referme la page */
        else if (LOWORD(wp) == ID_PARCOURIR) { parcourir(h); source_changee(); }
        else if (LOWORD(wp) == ID_SOURCE) { if (HIWORD(wp) == EN_KILLFOCUS) source_changee(); }
        else if (LOWORD(wp) == ID_ORIGINE) touches_d_origine();
        else if (LOWORD(wp) == ID_SANS_MOD) aucun_mod();
        else if (LOWORD(wp) == ID_PILOTE_MOD) {
            if (HIWORD(wp) == CBN_SELCHANGE) {
                const int i = (int)SendMessageW(g_pilote_liste, CB_GETCURSEL, 0, 0);
                if (i >= 0 && i + 1 < NB_PILOTES_MOD) {
                    g_pilote_vu = i + 1;
                    montrer_pilote();
                }
            }
        } else if (LOWORD(wp) >= ID_REGLAGE_MOD && LOWORD(wp) < ID_REGLAGE_MOD + NB_REGLAGES) {
            if (HIWORD(wp) == CBN_SELCHANGE) {
                const int r = LOWORD(wp) - ID_REGLAGE_MOD;
                const int k = (int)SendMessageW(g_reglage[r], CB_GETCURSEL, 0, 0);
                g_vaisseau_choix[r][g_pilote_vu] = k > 0 ? k : 0;
                retenir_reglage(r, g_pilote_vu);
                marquer_pilote(g_pilote_vu);
            }
        }
        else if (LOWORD(wp) >= ID_MOD && LOWORD(wp) < ID_MOD + NB_MODS) {
            if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == CBN_SELCHANGE) {
                retenir_mod(LOWORD(wp) - ID_MOD);
                accorder_mods();
            }
        }
        else if (LOWORD(wp) >= ID_TOUCHE && LOWORD(wp) < ID_TOUCHE + NB_COMMANDES) {
            int i = LOWORD(wp) - ID_TOUCHE;
            if (g_capture == i) annuler_capture();
            else commencer_capture(i);
            for (int k = 0; k < NB_COMMANDES; k++) InvalidateRect(g_bouton_touche[k], NULL, FALSE);
        } else if (LOWORD(wp) == ID_REACTION && HIWORD(wp) == CBN_SELCHANGE) {
            const int k = (int)SendMessageW(g_liste_reaction, CB_GETCURSEL, 0, 0);
            g_reaction = k >= 0 && k < NB_REACTIONS ? k : 0;
            retenir_nombre(L"reaction", g_reaction);
        } else if (LOWORD(wp) == ID_AXE && HIWORD(wp) == CBN_SELCHANGE) {
            const int k = (int)SendMessageW(g_liste_axe, CB_GETCURSEL, 0, 0);
            g_axe = k >= 0 && k < NB_AXES ? k : 0;
            retenir_nombre(L"manette_axe", g_axe);
            WritePrivateProfileStringW(L"lanceur", L"manette_monter", NULL, g_reglages);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        habillage_detruire();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR ligne, int montrer) {
    (void)prev; (void)ligne;
    GetModuleFileNameW(NULL, g_dossier, MAX_PATH);
    wchar_t *fin = wcsrchr(g_dossier, L'\\');
    if (fin) *fin = 0;
    _snwprintf(g_reglages, MAX_PATH, L"%s\\lanceur.ini", g_dossier);
    /* Un fichier de réglages neuf commence par la marque UTF-16 : Windows y écrit alors en Unicode, et le
       chemin d'une cartouche rangée dans un dossier au nom non latin se relit tel quel. Un ancien fichier
       garde son codage. */
    if (GetFileAttributesW(g_reglages) == INVALID_FILE_ATTRIBUTES) {
        HANDLE f = CreateFileW(g_reglages, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            static const unsigned char MARQUE[2] = {0xFF, 0xFE};
            DWORD ecrit;
            WriteFile(f, MARQUE, sizeof MARQUE, &ecrit, NULL);
            CloseHandle(f);
        }
    }

    /* la langue du lanceur : anglais tant qu'on n'en a pas choisi d'autre */
    {
        wchar_t code[8];
        GetPrivateProfileStringW(L"lanceur", L"interface", L"en", code, 8, g_reglages);
        const int i = interface_du_code(code);
        g_interface = i >= 0 ? i : INTERFACE_EN;
    }

    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"SfcLauncher";
    RegisterClassExW(&wc);
    wc.style = 0;
    wc.lpfnWndProc = proc_page;
    wc.lpszClassName = L"SfcPage";
    RegisterClassExW(&wc);

    /* sans cadre ni barre de titre, au centre de l'écran : l'habillage dessine tout */
    RECT ecran;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &ecran, 0);
    const int x = ecran.left + (ecran.right - ecran.left - HAB_LARGEUR) / 2;
    const int y = ecran.top + (ecran.bottom - ecran.top - HAB_HAUTEUR) / 2;
    HWND h = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, L"SfcLauncher",
                             L"Star Fox Command " VERSION,               /* récrit dans sa langue par creer_pages */
                             WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                             x, y, HAB_LARGEUR, HAB_HAUTEUR, NULL, NULL, inst, NULL);
    if (!h) return 1;
    ShowWindow(h, montrer);
    UpdateWindow(h);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        /* La touche à lier se prend avant que le dialogue n'en fasse un geste (Tab, Entrée, Espace, Alt…) ;
           ses répétitions et son relâchement ne lui reviennent pas non plus. */
        if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            if (g_capture >= 0) {
                capturer((int)msg.wParam);
                for (int k = 0; k < NB_COMMANDES; k++) InvalidateRect(g_bouton_touche[k], NULL, FALSE);
                continue;
            }
            if (g_relache && (int)msg.wParam == g_relache) continue;
        } else if ((msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) && g_relache && (int)msg.wParam == g_relache) {
            g_relache = 0;
            continue;
        }
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
