/*
 * L'habillage du lanceur, façon lanceur d'autrefois : une fenêtre sans cadre, dessinée point par point dans
 * une image en mémoire, puis posée d'un bloc à l'écran.
 *
 * Le fond spatial, ses nébuleuses, ses étoiles qui défilent sur trois plans et une étoile filante de temps à
 * autre ; une grille d'horizon qui file vers le joueur ; le titre, lumineux ; le menu ; à droite, les
 * illustrations de l'introduction du jeu, qui se fondent l'une dans l'autre ; en bas, un texte qui ondule en
 * défilant. Tout est dessin original — sauf les illustrations et l'emblème, que le lanceur tire de la cartouche
 * du joueur (images.c) : sans cartouche, il s'en passe.
 *
 * Sous licence MIT.
 */
#include "habillage.h"
#include "images.h"
#include "textes.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define W HAB_LARGEUR
#define H HAB_HAUTEUR
#define RVB(r, v, b) ((uint32_t)(r) << 16 | (uint32_t)(v) << 8 | (uint32_t)(b))

/* La palette : l'espace profond, le bleu des écrans de bord, l'orange de l'emblème. */
static const uint32_t PANNEAU = RVB(11, 20, 44), CHAMP = RVB(20, 34, 70), TEXTE = RVB(226, 236, 255),
                      DISCRET = RVB(124, 146, 188), CYAN = RVB(92, 206, 255), ORANGE = RVB(255, 138, 36),
                      ROUGE = RVB(226, 64, 36), BORD = RVB(56, 112, 176);

static HDC g_dc;
static HBITMAP g_image, g_image_avant;
static uint32_t *g_px;                  /* l'image de la fenêtre, BGRA, ligne par ligne depuis le haut */
static uint32_t *g_fond;                /* le fond fixe : dégradé, nébuleuses, vignettage */
static HFONT f_titre, f_sous, f_jouer, f_menu, f_page, f_petit, f_petit_gras, f_defile, f_langue;
static wchar_t g_version[32];
static double g_t;

/* ---- l'image ---------------------------------------------------------------------------------------- */

static uint32_t melange(uint32_t d, uint32_t s, int a) {          /* a de 0 à 256 */
    const uint32_t rb = ((d & 0xFF00FF) * (uint32_t)(256 - a) + (s & 0xFF00FF) * (uint32_t)a) >> 8 & 0xFF00FF;
    const uint32_t v = ((d & 0x00FF00) * (uint32_t)(256 - a) + (s & 0x00FF00) * (uint32_t)a) >> 8 & 0x00FF00;
    return rb | v;
}

static void poser(int x, int y, uint32_t c, int a) {
    if ((unsigned)x < W && (unsigned)y < H) g_px[y * W + x] = melange(g_px[y * W + x], c, a);
}

static void ajouter(int x, int y, int r, int v, int b) {          /* lumière : on additionne */
    if ((unsigned)x >= W || (unsigned)y >= H) return;
    const uint32_t d = g_px[y * W + x];
    int R = (int)(d >> 16 & 255) + r, V = (int)(d >> 8 & 255) + v, B = (int)(d & 255) + b;
    g_px[y * W + x] = RVB(R > 255 ? 255 : R, V > 255 ? 255 : V, B > 255 ? 255 : B);
}

static void remplir(int x0, int y0, int x1, int y1, uint32_t c, int a) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) g_px[y * W + x] = a >= 256 ? c : melange(g_px[y * W + x], c, a);
}

static void ligne_h(int x0, int x1, int y, uint32_t c, int a) { remplir(x0, y, x1, y + 1, c, a); }
static void ligne_v(int x, int y0, int y1, uint32_t c, int a) { remplir(x, y0, x + 1, y1, c, a); }

static void cadre(int x0, int y0, int x1, int y1, uint32_t c, int a) {
    ligne_h(x0, x1, y0, c, a);
    ligne_h(x0, x1, y1 - 1, c, a);
    ligne_v(x0, y0 + 1, y1 - 1, c, a);
    ligne_v(x1 - 1, y0 + 1, y1 - 1, c, a);
}

/* Les équerres d'un viseur, aux quatre coins d'un rectangle. */
static void equerres(int x0, int y0, int x1, int y1, int l, uint32_t c) {
    for (int k = 0; k < 2; k++) {
        ligne_h(x0, x0 + l, y0 + k, c, 256);         ligne_v(x0 + k, y0, y0 + l, c, 256);
        ligne_h(x1 - l, x1, y0 + k, c, 256);         ligne_v(x1 - 1 - k, y0, y0 + l, c, 256);
        ligne_h(x0, x0 + l, y1 - 1 - k, c, 256);     ligne_v(x0 + k, y1 - l, y1, c, 256);
        ligne_h(x1 - l, x1, y1 - 1 - k, c, 256);     ligne_v(x1 - 1 - k, y1 - l, y1, c, 256);
    }
}

static void segment(double x0, double y0, double x1, double y1, uint32_t c, int a) {
    const double dx = x1 - x0, dy = y1 - y0;
    const int n = (int)(fabs(dx) > fabs(dy) ? fabs(dx) : fabs(dy)) + 1;
    for (int i = 0; i <= n; i++) poser((int)(x0 + dx * i / n + 0.5), (int)(y0 + dy * i / n + 0.5), c, a);
}

static COLORREF gdi(uint32_t c) { return RGB(c >> 16 & 255, c >> 8 & 255, c & 255); }

COLORREF habillage_couleur(int quelle) {
    static const uint32_t *const T[] = {&PANNEAU, &CHAMP, &TEXTE, &DISCRET, &CYAN, &ORANGE, &BORD};
    return quelle >= 0 && quelle < (int)(sizeof T / sizeof *T) ? gdi(*T[quelle]) : 0;
}

/* ---- le texte ---------------------------------------------------------------------------------------- */

static HFONT police(const wchar_t *nom, const wchar_t *repli, int taille, int poids) {
    HFONT f = CreateFontW(-taille, 0, 0, 0, poids, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, nom);
    HDC dc = GetDC(NULL);
    HGDIOBJ avant = SelectObject(dc, f);
    wchar_t vu[LF_FACESIZE];
    GetTextFaceW(dc, LF_FACESIZE, vu);
    SelectObject(dc, avant);
    ReleaseDC(NULL, dc);
    if (_wcsnicmp(vu, nom, 6)) {           /* Bahnschrift manque (avant Windows 10) : une police sûre */
        DeleteObject(f);
        f = CreateFontW(-taille, 0, 0, 0, poids, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, repli);
    }
    return f;
}

static void texte(HFONT f, const wchar_t *t, int x, int y, uint32_t c, int espace) {
    SelectObject(g_dc, f);
    SetTextColor(g_dc, gdi(c));
    SetTextCharacterExtra(g_dc, espace);
    TextOutW(g_dc, x, y, t, (int)wcslen(t));
}

static int largeur(HFONT f, const wchar_t *t, int espace) {
    SelectObject(g_dc, f);
    SetTextCharacterExtra(g_dc, espace);
    SIZE s = {0, 0};
    GetTextExtentPoint32W(g_dc, t, (int)wcslen(t), &s);
    return s.cx;
}

static void texte_borne(HFONT f, const wchar_t *t, RECT r, uint32_t c) {
    SelectObject(g_dc, f);
    SetTextColor(g_dc, gdi(c));
    SetTextCharacterExtra(g_dc, 0);
    DrawTextW(g_dc, t, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

/* ---- le titre et sa lueur ---------------------------------------------------------------------------- */

#define TITRE_MARGE 14
static uint8_t *g_lueur;
static int g_lueur_l, g_lueur_h;
static const wchar_t *TITRE = L"STAR FOX";

/* Le titre écrit en blanc sur noir dans une image à part, puis flouté : son halo, que l'on ajoute en orange. */
static void preparer_lueur(void) {
    g_lueur_l = largeur(f_titre, TITRE, 2) + 2 * TITRE_MARGE;
    g_lueur_h = 72 + 2 * TITRE_MARGE;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = g_lueur_l;
    bi.bmiHeader.biHeight = -g_lueur_h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP b = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ avant = SelectObject(dc, b);
    memset(bits, 0, (size_t)g_lueur_l * g_lueur_h * 4);
    SelectObject(dc, f_titre);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetTextCharacterExtra(dc, 2);
    TextOutW(dc, TITRE_MARGE, TITRE_MARGE, TITRE, (int)wcslen(TITRE));
    GdiFlush();
    const uint32_t *p = bits;
    float *a = malloc(sizeof(float) * g_lueur_l * g_lueur_h), *b2 = malloc(sizeof(float) * g_lueur_l * g_lueur_h);
    for (int i = 0; i < g_lueur_l * g_lueur_h; i++) a[i] = (float)(p[i] >> 8 & 255);
    for (int passe = 0; passe < 3; passe++) {               /* trois flous en boîte : presque une gaussienne */
        const int r = 5;
        for (int y = 0; y < g_lueur_h; y++)
            for (int x = 0; x < g_lueur_l; x++) {
                float s = 0;
                for (int k = -r; k <= r; k++) {
                    const int xx = x + k;
                    if (xx >= 0 && xx < g_lueur_l) s += a[y * g_lueur_l + xx];
                }
                b2[y * g_lueur_l + x] = s / (2 * r + 1);
            }
        for (int y = 0; y < g_lueur_h; y++)
            for (int x = 0; x < g_lueur_l; x++) {
                float s = 0;
                for (int k = -r; k <= r; k++) {
                    const int yy = y + k;
                    if (yy >= 0 && yy < g_lueur_h) s += b2[yy * g_lueur_l + x];
                }
                a[y * g_lueur_l + x] = s / (2 * r + 1);
            }
    }
    g_lueur = malloc((size_t)g_lueur_l * g_lueur_h);
    for (int i = 0; i < g_lueur_l * g_lueur_h; i++) {
        const float v = a[i] * 2.2f;
        g_lueur[i] = (uint8_t)(v > 255 ? 255 : v);
    }
    free(a);
    free(b2);
    SelectObject(dc, avant);
    DeleteObject(b);
    DeleteDC(dc);
}

/* ---- le ciel ----------------------------------------------------------------------------------------- */

#define NB_ETOILES 280
typedef struct { float x, y, vitesse, eclat, phase, frequence; int grande; } Etoile;
static Etoile g_etoiles[NB_ETOILES];
static uint32_t g_hasard = 0x5F0C0DE5u;
static float hasard(void) {                 /* xorshift : le même ciel à chaque lancement */
    g_hasard ^= g_hasard << 13;
    g_hasard ^= g_hasard >> 17;
    g_hasard ^= g_hasard << 5;
    return (g_hasard & 0xFFFFFF) / (float)0x1000000;
}

static struct { int active; double x, y, vx, vy, vie; } g_filante;
static double g_prochaine_filante = 4.0;

static void preparer_fond(void) {
    g_fond = malloc(sizeof *g_fond * W * H);
    static const struct { float x, y, r, R, V, B; } NEBULEUSES[] = {
        {760, 170, 420, 70, 40, 150}, {180, 560, 360, 20, 70, 140}, {930, 560, 300, 140, 45, 60},
        {420, 40, 260, 30, 50, 110},
    };
    for (int y = 0; y < H; y++) {
        const float t = powf(y / (float)(H - 1), 1.3f);
        for (int x = 0; x < W; x++) {
            float r = 5 + 9 * t, v = 9 + 17 * t, b = 19 + 40 * t;
            for (size_t k = 0; k < sizeof NEBULEUSES / sizeof *NEBULEUSES; k++) {
                const float dx = (x - NEBULEUSES[k].x) / NEBULEUSES[k].r, dy = (y - NEBULEUSES[k].y) / NEBULEUSES[k].r;
                const float e = expf(-(dx * dx + dy * dy)) * 0.42f;
                r += NEBULEUSES[k].R * e;
                v += NEBULEUSES[k].V * e;
                b += NEBULEUSES[k].B * e;
            }
            const float vx = (x - W / 2.0f) / (W / 2.0f), vy = (y - H / 2.0f) / (H / 2.0f);
            const float vignette = 1.0f - 0.32f * (vx * vx + vy * vy) / 2;
            r *= vignette;
            v *= vignette;
            b *= vignette;
            g_fond[y * W + x] = RVB(r > 255 ? 255 : (int)r, v > 255 ? 255 : (int)v, b > 255 ? 255 : (int)b);
        }
    }
    for (int i = 0; i < NB_ETOILES; i++) {
        Etoile *e = &g_etoiles[i];
        e->x = hasard() * W;
        e->y = hasard() * H;
        e->phase = hasard() * 6.2832f;
        e->frequence = 0.6f + hasard() * 2.2f;
        if (i < 160) { e->vitesse = 3 + hasard() * 5;   e->eclat = 60 + hasard() * 70;  e->grande = 0; }
        else if (i < 250) { e->vitesse = 12 + hasard() * 10; e->eclat = 120 + hasard() * 80; e->grande = 0; }
        else { e->vitesse = 34 + hasard() * 20; e->eclat = 200 + hasard() * 55; e->grande = 1; }
    }
}

static void dessiner_ciel(void) {
    memcpy(g_px, g_fond, sizeof *g_px * W * H);
    for (int i = 0; i < NB_ETOILES; i++) {
        const Etoile *e = &g_etoiles[i];
        const float b = e->eclat * (0.72f + 0.28f * sinf((float)g_t * e->frequence + e->phase));
        const int x = (int)e->x, y = (int)e->y, R = (int)(b * 0.82f), V = (int)(b * 0.9f), B = (int)b;
        ajouter(x, y, R, V, B);
        if (e->grande) {
            ajouter(x + 1, y, R, V, B);
            ajouter(x, y + 1, R, V, B);
            ajouter(x + 1, y + 1, R, V, B);
            ajouter(x - 1, y, R / 3, V / 3, B / 3);
            ajouter(x + 2, y, R / 3, V / 3, B / 3);
            ajouter(x, y - 1, R / 3, V / 3, B / 3);
            ajouter(x, y + 2, R / 3, V / 3, B / 3);
        }
    }
    if (g_filante.active) {
        const double vie = g_filante.vie;
        for (int k = 0; k < 60; k++) {
            const double x = g_filante.x - g_filante.vx * k * 0.004, y = g_filante.y - g_filante.vy * k * 0.004;
            const int a = (int)((60 - k) * 3.2 * (vie < 0.25 ? vie / 0.25 : 1));
            ajouter((int)x, (int)y, a, a, a);
        }
    }
}

/* La grille d'horizon : des lignes qui filent vers le joueur, sous le menu, presque effacées. */
static void dessiner_grille(void) {
    const int y0 = 470, y1 = 552, x0 = 250;
    const double phase = fmod(g_t * 0.55, 1.0);
    for (int k = 0; k < 14; k++) {
        const double z = 1.0 + k + (1.0 - phase);
        const int y = y0 + (int)((y1 - y0) / z);
        const int a = (int)(84 / sqrt(z));
        for (int x = 0; x < W; x++) {
            const double bord = x < 120 ? x / 120.0 : x > W - 120 ? (W - x) / 120.0 : 1.0;
            poser(x, y, CYAN, (int)(a * bord));
        }
    }
    for (int i = -18; i <= 18; i++) {
        const double xb = x0 + i * 64.0;
        for (int y = y0; y < y1; y++) {
            const double f = (y - y0) / (double)(y1 - y0);
            const int x = (int)(x0 + (xb - x0) * f + 0.5);
            const double bord = x < 120 ? x / 120.0 : x > W - 120 ? (W - x) / 120.0 : 1.0;
            if (bord > 0) poser(x, y, CYAN, (int)(78 * f * bord));
        }
    }
}

/* ---- les illustrations de la cartouche -------------------------------------------------------------- */

#define ART_X (W - ART_L)
#define ART_L 512
#define ART_H 384
static const char *ILLUSTRATIONS[] = {
    "3D/Textures/Adv/Opening/02_team.ntfq-cmp",
    "3D/Textures/Adv/Opening/11_KrystalANDfox.ntfq-cmp",
    "3D/Textures/Adv/Opening/09_falco.ntfq-cmp",
    "3D/Textures/Adv/Opening/12_GFandAW.ntfq-cmp",
    "3D/Textures/Adv/Opening/10_slippyANDamanda.ntfq-cmp",
    "3D/Textures/Adv/Opening/14_krystal.ntfq-cmp",
    "3D/Textures/Adv/Opening/07_foxandnus.ntfq-cmp",
};
#define NB_ILLUSTRATIONS ((int)(sizeof ILLUSTRATIONS / sizeof *ILLUSTRATIONS))
static uint32_t *g_art[NB_ILLUSTRATIONS];   /* 512×384, couleurs prémultipliées, l'alpha en haut */
static int g_nb_art, g_art_vu;
static double g_art_t;
static uint32_t g_embleme[64 * 64];
static int g_a_embleme;

/* Sans cartouche reconnue, pas d'illustration : une planète dessinée ici, éclairée de côté, ceinte d'une
   atmosphère bleue et d'un anneau — un dessin original, rien du jeu. */
#define PLANETE_X 780
#define PLANETE_Y 250
#define PLANETE_R 150
static uint32_t *g_planete;                 /* W×H, prémultipliée, l'alpha en haut */

static void preparer_planete(void) {
    g_planete = calloc((size_t)W * H, sizeof *g_planete);
    if (!g_planete) return;
    const double lx = -0.62, ly = -0.42, lz = 0.66;           /* la lumière vient d'en haut à gauche */
    for (int y = PLANETE_Y - PLANETE_R - 40; y < PLANETE_Y + PLANETE_R + 40; y++)
        for (int x = PLANETE_X - 2 * PLANETE_R; x < PLANETE_X + 2 * PLANETE_R; x++) {
            if ((unsigned)x >= W || (unsigned)y >= H) continue;
            const double dx = (x - PLANETE_X) / (double)PLANETE_R, dy = (y - PLANETE_Y) / (double)PLANETE_R;
            const double d2 = dx * dx + dy * dy;
            double R = 0, V = 0, B = 0, A = 0;
            if (d2 < 1) {
                const double dz = sqrt(1 - d2);
                double eclaire = dx * lx + dy * ly + dz * lz;
                eclaire = eclaire < 0 ? 0 : eclaire;
                /* des bandes, comme une géante gazeuse, un peu ondulées */
                const double bande = 0.5 + 0.5 * sin(dy * 13 + sin(dx * 3.1) * 0.9);
                const double fond = 0.10 + 0.90 * pow(eclaire, 0.9);
                R = (38 + 46 * bande) * fond;
                V = (52 + 40 * bande) * fond;
                B = (96 + 60 * bande) * fond;
                const double bord = pow(1 - dz, 3);                  /* le limbe s'éclaire de l'atmosphère */
                R += 40 * bord;
                V += 120 * bord;
                B += 190 * bord;
                A = d2 > 0.985 ? (1 - d2) / 0.015 : 1;
            }
            /* le halo de l'atmosphère, au-delà du bord */
            const double d = sqrt(d2);
            if (d >= 1 && d < 1.18) {
                const double h = pow(1 - (d - 1) / 0.18, 2) * 0.55;
                R += 50 * h; V += 140 * h; B += 230 * h;
                A = A > h ? A : h;
            }
            /* l'anneau, incliné, qui passe devant la planète en bas et derrière en haut */
            const double ax = dx, ay = (dy - dx * 0.22) / 0.26;
            const double ra = sqrt(ax * ax + ay * ay);
            if (ra > 1.35 && ra < 1.85 && (d2 >= 1 || ay > 0)) {
                const double s = 0.55 + 0.45 * sin(ra * 40);
                const double cote = ax < 0 ? 1.0 : 0.55;
                const double ea = 0.62 * s * cote * (1 - fabs(ra - 1.6) / 0.25 * 0.6);
                R = R * (1 - ea) + 170 * ea;
                V = V * (1 - ea) + 160 * ea;
                B = B * (1 - ea) + 150 * ea;
                A = A + ea * (1 - A);
            }
            if (A <= 0) continue;
            const int a = (int)(A * 255);
            g_planete[y * W + x] = (uint32_t)a << 24 | RVB(R > 255 ? 255 : (int)R, V > 255 ? 255 : (int)V, B > 255 ? 255 : (int)B);
        }
}

static void dessiner_planete(int page) {
    if (!g_planete) return;
    const int attenue = page ? 90 : 256;
    for (int y = PLANETE_Y - PLANETE_R - 40; y < PLANETE_Y + PLANETE_R + 40; y++)
        for (int x = PLANETE_X - 2 * PLANETE_R; x < PLANETE_X + 2 * PLANETE_R; x++) {
            if ((unsigned)x >= W || (unsigned)y >= H) continue;
            const uint32_t s = g_planete[y * W + x];
            const int A = (int)(s >> 24) * attenue >> 8;
            if (A) g_px[y * W + x] = melange(g_px[y * W + x], s & 0xFFFFFF, A);
        }
}

static void oublier_images(void) {
    for (int i = 0; i < NB_ILLUSTRATIONS; i++) { free(g_art[i]); g_art[i] = NULL; }
    g_nb_art = g_art_vu = 0;
    g_art_t = 0;
    g_a_embleme = 0;
}

/* L'illustration doublée point pour point — le pixel d'origine, net —, fondue vers la gauche et vers le bas. */
static uint32_t *agrandir(const Image *img) {
    uint32_t *d = malloc(sizeof *d * ART_L * ART_H);
    if (!d) return NULL;
    for (int y = 0; y < ART_H; y++)
        for (int x = 0; x < ART_L; x++) {
            const double gauche = x < 150 ? x / 150.0 : 1.0, bas = y > ART_H - 120 ? (ART_H - y) / 120.0 : 1.0;
            double a = gauche * bas;
            a = a * a * (3 - 2 * a);
            const uint32_t s = img->pixels[(y / 2) * img->largeur + x / 2];
            const int A = (int)(a * 255);
            d[y * ART_L + x] = (uint32_t)A << 24 | ((s >> 16 & 255) * A / 255) << 16 | ((s >> 8 & 255) * A / 255) << 8 |
                               (s & 255) * A / 255;
        }
    return d;
}

HICON habillage_source(const wchar_t *source) {
    oublier_images();
    if (!source || !source[0] || !images_ouvrir(source)) return NULL;
    for (int i = 0; i < NB_ILLUSTRATIONS; i++) {
        Image img;
        if (!image_illustration(ILLUSTRATIONS[i], &img)) continue;
        uint32_t *a = agrandir(&img);
        image_liberer(&img);
        if (a) g_art[g_nb_art++] = a;
    }
    HICON icone = NULL;
    Image ic;
    if (image_icone(&ic)) {
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) g_embleme[y * 64 + x] = ic.pixels[(y / 2) * 32 + x / 2];
        g_a_embleme = 1;
        /* l'icône de la fenêtre : l'emblème de la cartouche, avec sa transparence */
        uint32_t couleurs[32 * 32];
        uint8_t masque[32 * 4] = {0};
        for (int i = 0; i < 32 * 32; i++) couleurs[i] = ic.pixels[i] ? (ic.pixels[i] | 0xFF000000u) : 0;
        ICONINFO ii;
        memset(&ii, 0, sizeof ii);
        ii.fIcon = TRUE;
        BITMAPV5HEADER bh;
        memset(&bh, 0, sizeof bh);
        bh.bV5Size = sizeof bh;
        bh.bV5Width = 32;
        bh.bV5Height = -32;
        bh.bV5Planes = 1;
        bh.bV5BitCount = 32;
        bh.bV5Compression = BI_BITFIELDS;
        bh.bV5RedMask = 0x00FF0000;
        bh.bV5GreenMask = 0x0000FF00;
        bh.bV5BlueMask = 0x000000FF;
        bh.bV5AlphaMask = 0xFF000000;
        void *bits = NULL;
        HDC dc = GetDC(NULL);
        ii.hbmColor = CreateDIBSection(dc, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, NULL, 0);
        ReleaseDC(NULL, dc);
        if (ii.hbmColor && bits) {
            memcpy(bits, couleurs, sizeof couleurs);
            ii.hbmMask = CreateBitmap(32, 32, 1, 1, masque);
            icone = CreateIconIndirect(&ii);
            DeleteObject(ii.hbmMask);
        }
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        image_liberer(&ic);
    }
    images_fermer();
    return icone;
}

static void dessiner_art(int page) {
    if (!g_nb_art) { dessiner_planete(page); return; }
    const double MONTRE = 7.0, FONDU = 1.6;
    const int suivant = (g_art_vu + 1) % g_nb_art;
    const double f = g_nb_art > 1 && g_art_t > MONTRE ? (g_art_t - MONTRE) / FONDU : 0.0;
    const int m = (int)(f * f * (3 - 2 * f) * 256);
    const int attenue = page ? 70 : 256;                 /* une page ouverte : l'image s'efface derrière */
    const uint32_t *a = g_art[g_art_vu], *b = g_art[suivant];
    for (int y = 0; y < ART_H; y++) {
        uint32_t *d = g_px + y * W + ART_X;
        for (int x = 0; x < ART_L; x++) {
            uint32_t s = a[y * ART_L + x];
            if (m) {
                const uint32_t t = b[y * ART_L + x];
                const int A = (int)((s >> 24) * (256 - m) + (t >> 24) * m) >> 8;
                s = (uint32_t)A << 24 | melange(s & 0xFFFFFF, t & 0xFFFFFF, m);
            }
            const int A = (int)(s >> 24) * attenue >> 8;
            if (!A) continue;
            /* d·(1 − α) + s, la couleur étant prémultipliée */
            const uint32_t fond = d[x];
            const int R = (int)(fond >> 16 & 255) * (255 - A) / 255 + (int)(s >> 16 & 255) * attenue / 256;
            const int V = (int)(fond >> 8 & 255) * (255 - A) / 255 + (int)(s >> 8 & 255) * attenue / 256;
            const int B = (int)(fond & 255) * (255 - A) / 255 + (int)(s & 255) * attenue / 256;
            d[x] = RVB(R > 255 ? 255 : R, V > 255 ? 255 : V, B > 255 ? 255 : B);
        }
    }
}

/* ---- la mise en page --------------------------------------------------------------------------------- */

static RECT rect(int x0, int y0, int x1, int y1) { RECT r = {x0, y0, x1, y1}; return r; }

static RECT zone(int e) {
    switch (e) {
    case E_JOUER: return rect(40, 196, 448, 250);
    case E_REGLAGES: return rect(40, 270, 300, 306);
    case E_CONTROLES: return rect(40, 308, 300, 344);
    case E_MODS: return rect(40, 346, 300, 382);
    case E_QUITTER: return rect(40, 396, 300, 432);
    case E_REDUIRE: return rect(W - 90, 6, W - 52, 34);
    case E_FERMER: return rect(W - 48, 6, W - 10, 34);
    case E_CARTOUCHE: return rect(148, 446, 270, 466);
    case E_LANGUE_EN: return rect(W - 222, 8, W - 188, 32);
    case E_LANGUE_FR: return rect(W - 186, 8, W - 152, 32);
    case E_LANGUE_JA: return rect(W - 150, 8, W - 98, 32);
    default: return rect(0, 0, 0, 0);
    }
}

#define PANNEAU_X0 440
#define PANNEAU_Y0 58
#define PANNEAU_X1 988
#define PANNEAU_Y1 536

RECT habillage_zone_page(void) { return rect(PANNEAU_X0 + 12, PANNEAU_Y0 + 52, PANNEAU_X1 - 12, PANNEAU_Y1 - 12); }

int habillage_cible(int x, int y, const Scene *s) {
    (void)s;
    for (int e = 0; e < NB_ELEMENTS; e++) {
        const RECT r = zone(e);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return e;
    }
    return E_RIEN;
}

/* Un triangle plein, pointe à droite. */
static void fleche(int x, int y, int h, uint32_t c) {
    for (int k = 0; k < h; k++) {
        const int l = (k < h / 2 ? k : h - 1 - k) + 1;
        ligne_h(x, x + l, y + k, c, 256);
    }
}

static void dessiner_titre(const Scene *s) {
    (void)s;
    int x = 44;
    if (g_a_embleme) {
        for (int y = 0; y < 64; y++)
            for (int i = 0; i < 64; i++) {
                const uint32_t p = g_embleme[y * 64 + i];
                if (p) g_px[(40 + y) * W + 44 + i] = p & 0xFFFFFF;
            }
        x = 124;
    }
    /* le halo, qui respire doucement */
    const double souffle = 0.78 + 0.22 * sin(g_t * 1.3);
    const int gx = x - TITRE_MARGE, gy = 26 - TITRE_MARGE;
    for (int y = 0; y < g_lueur_h; y++)
        for (int i = 0; i < g_lueur_l; i++) {
            const int a = (int)(g_lueur[y * g_lueur_l + i] * souffle);
            if (a) ajouter(gx + i, gy + y, a * 255 / 255, a * 120 / 255, a * 30 / 255);
        }
    GdiFlush();
    texte(f_titre, TITRE, x, 26, TEXTE, 2);
    texte(f_sous, L"COMMAND", x + 4, 100, CYAN, 14);
    wchar_t ligne[96];
    _snwprintf(ligne, 96, T(T_SOUS_TITRE), g_version);
    ligne[95] = 0;
    texte(f_petit_gras, ligne, x + 5, 136, DISCRET, 3);
}

static void dessiner_menu(const Scene *s) {
    /* le bouton principal : un panneau incliné, orange comme l'emblème */
    {
        const RECT r = zone(E_JOUER);
        const int vise = s->survol == E_JOUER && !s->prepare, presse = vise && s->appui == E_JOUER;
        const int h = r.bottom - r.top;
        GdiFlush();
        if (vise) {                                  /* au survol, un halo orange autour du panneau */
            for (int y = -8; y < h + 8; y++) {
                const int decale = (h - y) * 2 / 5;
                for (int x = r.left + decale - 10; x < r.right + decale - 8; x++) {
                    const int dy = y < 0 ? -y : y >= h ? y - h + 1 : 0;
                    const int a = 70 - dy * 8;
                    if (a > 0) ajouter(x, r.top + y, a, a * 45 / 100, a / 8);
                }
            }
        }
        for (int y = 0; y < h; y++) {
            const int decale = (h - y) * 2 / 5;
            for (int x = r.left + decale; x < r.right + decale - 18; x++) {
                const double t = (x - r.left) / (double)(r.right - r.left);
                uint32_t c = s->prepare ? RVB(60, 80, 120) : melange(ORANGE, ROUGE, (int)(t * 200));
                if (presse) c = melange(c, RVB(0, 0, 0), 50);
                const int a = s->prepare ? 150 : vise ? 250 : 205;
                poser(x, r.top + y, c, (int)(a * (t > 0.7 ? (1 - t) / 0.3 * 0.6 + 0.4 : 1.0)));
            }
        }
        for (int x = r.left + h * 2 / 5; x < r.right - 18; x++) poser(x, r.top, RVB(255, 220, 170), vise ? 220 : 120);
        fleche(r.left + 26, r.top + 17, 21, TEXTE);
        const wchar_t *libelle = T(s->prepare ? T_PREPARATION : s->pret ? T_JOUER : T_PREPARER);
        texte(f_jouer, libelle, r.left + 56, r.top + 9, s->prepare ? DISCRET : TEXTE, 4);
    }
    static const int NOMS[] = {0, T_MENU_REGLAGES, T_MENU_CONTROLES, T_MENU_MODS, T_MENU_QUITTER};
    for (int e = E_REGLAGES; e <= E_QUITTER; e++) {
        const RECT r = zone(e);
        const int ouverte = s->page == e, vise = s->survol == e;
        const uint32_t c = ouverte ? CYAN : vise ? TEXTE : RVB(176, 194, 226);
        int x = r.left + 24;
        if (ouverte) {
            GdiFlush();
            remplir(r.left, r.top + 4, r.left + 3, r.bottom - 4, CYAN, 256);
            remplir(r.left + 3, r.top + 4, r.right, r.bottom - 4, CYAN, 22);
        } else if (vise) {
            GdiFlush();
            fleche(r.left + 6, r.top + 11, 13, ORANGE);
            x += 4;
        }
        texte(f_menu, T(NOMS[e]), x, r.top + 6, c, 3);
    }
}

static void dessiner_infos(const Scene *s) {
    texte(f_petit_gras, T(T_CARTOUCHE_TITRE), 44, 448, DISCRET, 2);
    const int vise = s->survol == E_CARTOUCHE;
    const wchar_t *lien = T(s->cartouche ? T_CHANGER : T_CHOISIR);
    texte(f_petit_gras, lien, 152, 448, vise ? TEXTE : CYAN, 1);
    if (vise) {
        GdiFlush();
        ligne_h(152, 152 + largeur(f_petit_gras, lien, 1), 465, TEXTE, 200);
    }
    if (s->cartouche) {
        texte_borne(f_petit, s->cartouche, rect(44, 468, 400, 488), s->cartouche_bonne ? TEXTE : ORANGE);
    } else {
        texte_borne(f_petit, T(T_AUCUNE), rect(44, 468, 430, 488), ORANGE);
    }
    if (s->cartouche && !s->cartouche_bonne && !s->prepare) {
        texte(f_petit, T(T_NON_RECONNUE), 44, 494, ORANGE, 0);
        texte(f_petit, T(T_IL_FAUT), 44, 514, DISCRET, 0);
    } else if (s->prepare) {
        texte_borne(f_petit, s->etat ? s->etat : T(T_EN_PREPARATION), rect(44, 494, 430, 514), TEXTE);
        /* la jauge, en segments, comme celle du bouclier */
        GdiFlush();
        const int n = 40, x0 = 44, y0 = 518, l = 8, ecart = 2;
        const int pleins = s->avance * n / 1000;
        for (int k = 0; k < n; k++) {
            const int x = x0 + k * (l + ecart);
            if (k < pleins) remplir(x, y0, x + l, y0 + 10, melange(CYAN, RVB(120, 255, 170), k * 256 / n), 240);
            else if (k == pleins) remplir(x, y0, x + l, y0 + 10, CYAN, (int)(90 + 80 * sin(g_t * 6)));
            else remplir(x, y0, x + l, y0 + 10, BORD, 70);
        }
    } else if (s->pret) {
        texte(f_petit, T(T_PRET), 44, 494, RVB(130, 236, 170), 0);
        texte(f_petit, T(T_SAUVEGARDE), 44, 514, DISCRET, 0);
    } else {
        texte(f_petit, T(T_PAS_CONSTRUIT), 44, 494, TEXTE, 0);
        texte(f_petit, T(T_PAS_CONSTRUIT_2), 44, 514, DISCRET, 0);
    }
}

static void dessiner_page(const Scene *s) {
    if (!s->page) return;
    GdiFlush();
    remplir(PANNEAU_X0, PANNEAU_Y0, PANNEAU_X1, PANNEAU_Y1, PANNEAU, 256);
    cadre(PANNEAU_X0, PANNEAU_Y0, PANNEAU_X1, PANNEAU_Y1, BORD, 200);
    equerres(PANNEAU_X0 - 3, PANNEAU_Y0 - 3, PANNEAU_X1 + 3, PANNEAU_Y1 + 3, 16, ORANGE);
    for (int x = PANNEAU_X0 + 16; x < PANNEAU_X1 - 16; x++) {
        const double t = (x - PANNEAU_X0 - 16) / (double)(PANNEAU_X1 - PANNEAU_X0 - 32);
        poser(x, PANNEAU_Y0 + 44, CYAN, (int)(180 * (1 - t)));
    }
    static const int NOMS[] = {0, T_MENU_REGLAGES, T_MENU_CONTROLES, T_MENU_MODS};
    texte(f_page, T(NOMS[s->page]), PANNEAU_X0 + 18, PANNEAU_Y0 + 12, CYAN, 6);
}

static void dessiner_fenetre(const Scene *s) {
    GdiFlush();
    /* un fond sombre sous les commandes du haut, qu'une illustration claire ne les mange pas */
    for (int y = 6; y < 34; y++) {
        const int a = 150 - (y < 10 ? (10 - y) * 30 : y > 30 ? (y - 30) * 30 : 0);
        for (int x = W - 232; x < W - 8; x++) {
            const int bord = x < W - 212 ? (x - (W - 232)) * 150 / 20 : 150;
            poser(x, y, RVB(6, 12, 30), a < bord ? a : bord);
        }
    }
    for (int e = E_REDUIRE; e <= E_FERMER; e++) {
        const RECT r = zone(e);
        const int vise = s->survol == e;
        if (vise) remplir(r.left, r.top, r.right, r.bottom, e == E_FERMER ? RVB(200, 44, 52) : RVB(255, 255, 255), e == E_FERMER ? 230 : 40);
        const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        const uint32_t c = vise ? RVB(255, 255, 255) : RVB(170, 190, 225);
        if (e == E_REDUIRE) {
            remplir(cx - 6, cy, cx + 6, cy + 2, c, 256);
        } else {
            for (int k = 0; k < 2; k++) {
                segment(cx - 6 + k, cy - 6, cx + 5 + k, cy + 5, c, 256);
                segment(cx + 5 + k, cy - 6, cx - 6 + k, cy + 5, c, 256);
            }
        }
    }
    /* la langue du lanceur : EN, FR, 日本語 — celle en cours soulignée */
    static const wchar_t *LANGUES[] = {L"EN", L"FR", L"日本語"};
    for (int e = E_LANGUE_EN; e <= E_LANGUE_JA; e++) {
        const RECT r = zone(e);
        const int la = e - E_LANGUE_EN == g_interface, vise = s->survol == e;
        const wchar_t *t = LANGUES[e - E_LANGUE_EN];
        const int l = largeur(f_langue, t, 1), x = (r.left + r.right - l) / 2;
        texte(f_langue, t, x, r.top + 3, la ? CYAN : vise ? TEXTE : DISCRET, 1);
        if (la) { GdiFlush(); ligne_h(x, x + l, r.bottom - 3, CYAN, 220); }
    }
    GdiFlush();
    cadre(0, 0, W, H, BORD, 150);
}

/* Le texte qui défile en ondulant, en bas : un clin d'œil aux lanceurs d'autrefois. */
/* son texte, dans la langue du lanceur (T_DEFILE) */
static int g_defile_l;
static int g_largeurs[512];

static void dessiner_defile(void) {
    GdiFlush();
    for (int x = 0; x < W; x++) {
        const double t = x / (double)W;
        poser(x, 552, CYAN, (int)(120 * sin(t * 3.14159)));
    }
    const wchar_t *DEFILE = T(T_DEFILE);
    const int n = (int)wcslen(DEFILE);
    if (!g_defile_l) {
        SelectObject(g_dc, f_defile);
        SetTextCharacterExtra(g_dc, 0);
        for (int i = 0; i < n && i < 512; i++) {
            SIZE sz;
            GetTextExtentPoint32W(g_dc, DEFILE + i, 1, &sz);
            g_largeurs[i] = sz.cx + 1;
            g_defile_l += g_largeurs[i];
        }
    }
    const double depart = -fmod(g_t * 70.0, g_defile_l);
    SelectObject(g_dc, f_defile);
    SetTextCharacterExtra(g_dc, 0);
    for (int tour = 0; tour < 3; tour++) {
        double x = depart + tour * g_defile_l;
        for (int i = 0; i < n && i < 512; i++) {
            if (x > W) break;
            if (x + g_largeurs[i] >= 0 && DEFILE[i] != L' ') {
                const double y = 566 + 5 * sin(x / 52.0 + g_t * 2.6);
                const double t = x / W;
                const uint32_t c = t < 0.5 ? melange(CYAN, TEXTE, (int)(t * 2 * 256)) : melange(TEXTE, ORANGE, (int)((t - 0.5) * 2 * 256));
                const int bord = x < 60 ? (int)(x * 256 / 60) : x > W - 60 ? (int)((W - x) * 256 / 60) : 256;
                SetTextColor(g_dc, gdi(melange(RVB(10, 18, 40), c, bord < 0 ? 0 : bord)));
                TextOutW(g_dc, (int)x, (int)y, DEFILE + i, 1);
            }
            x += g_largeurs[i];
        }
    }
}

/* ---- la vie de la fenêtre ---------------------------------------------------------------------------- */

void habillage_creer(HWND fenetre, const wchar_t *version) {
    (void)fenetre;
    wcsncpy(g_version, version, 31);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    g_dc = CreateCompatibleDC(NULL);
    void *bits = NULL;
    g_image = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    g_px = bits;
    g_image_avant = SelectObject(g_dc, g_image);
    SetBkMode(g_dc, TRANSPARENT);
    f_titre = police(L"Bahnschrift", L"Segoe UI Black", 64, FW_BOLD);
    f_sous = police(L"Bahnschrift Light", L"Segoe UI Light", 26, FW_LIGHT);
    f_langue = police(L"Yu Gothic UI", L"Meiryo UI", 13, FW_BOLD);
    habillage_langue();
    preparer_fond();
    preparer_planete();
    preparer_lueur();
}

/* Les polices de l'interface : Bahnschrift, la police des écrans de bord ; en japonais, une police japonaise
   (Bahnschrift n'a pas ses caractères). Le titre garde la sienne. */
void habillage_langue(void) {
    HFONT *f[] = {&f_jouer, &f_menu, &f_page, &f_petit, &f_petit_gras, &f_defile};
    for (size_t i = 0; i < sizeof f / sizeof *f; i++)
        if (*f[i]) { DeleteObject(*f[i]); *f[i] = NULL; }
    if (g_interface == INTERFACE_JA) {
        f_jouer = police(L"Yu Gothic UI", L"Meiryo UI", 28, FW_BOLD);
        f_menu = police(L"Yu Gothic UI", L"Meiryo UI", 19, FW_BOLD);
        f_page = police(L"Yu Gothic UI", L"Meiryo UI", 17, FW_BOLD);
        f_petit = police(L"Yu Gothic UI", L"Meiryo UI", 13, FW_NORMAL);
        f_petit_gras = police(L"Yu Gothic UI", L"Meiryo UI", 12, FW_BOLD);
        f_defile = police(L"Yu Gothic UI", L"Meiryo UI", 16, FW_BOLD);
    } else {
        f_jouer = police(L"Bahnschrift SemiBold", L"Segoe UI Semibold", 30, FW_SEMIBOLD);
        f_menu = police(L"Bahnschrift SemiBold", L"Segoe UI Semibold", 20, FW_SEMIBOLD);
        f_page = police(L"Bahnschrift SemiBold", L"Segoe UI Semibold", 18, FW_SEMIBOLD);
        f_petit = police(L"Segoe UI", L"Segoe UI", 14, FW_NORMAL);
        f_petit_gras = police(L"Bahnschrift SemiBold", L"Segoe UI Semibold", 12, FW_SEMIBOLD);
        f_defile = police(L"Bahnschrift SemiBold", L"Segoe UI Semibold", 17, FW_SEMIBOLD);
    }
    g_defile_l = 0;                          /* les largeurs du texte qui défile se reprennent */
}

void habillage_detruire(void) {
    oublier_images();
    if (g_dc) {
        SelectObject(g_dc, g_image_avant);
        DeleteObject(g_image);
        DeleteDC(g_dc);
        g_dc = NULL;
    }
    HFONT *f[] = {&f_titre, &f_sous, &f_jouer, &f_menu, &f_page, &f_petit, &f_petit_gras, &f_defile, &f_langue};
    for (size_t i = 0; i < sizeof f / sizeof *f; i++) { DeleteObject(*f[i]); *f[i] = NULL; }
    free(g_fond);
    free(g_planete);
    free(g_lueur);
    g_planete = NULL;
    g_fond = NULL;
    g_lueur = NULL;
}

void habillage_avancer(double dt) {
    if (dt > 0.1) dt = 0.1;
    g_t += dt;
    for (int i = 0; i < NB_ETOILES; i++) {
        Etoile *e = &g_etoiles[i];
        e->x -= e->vitesse * (float)dt;
        if (e->x < -3) { e->x += W + 6; e->y = hasard() * H; }
    }
    if (g_filante.active) {
        g_filante.x += g_filante.vx * dt;
        g_filante.y += g_filante.vy * dt;
        g_filante.vie -= dt;
        if (g_filante.vie <= 0) g_filante.active = 0;
    } else if ((g_prochaine_filante -= dt) <= 0) {
        g_filante.active = 1;
        g_filante.x = 300 + hasard() * 600;
        g_filante.y = hasard() * 160;
        g_filante.vx = -(420 + hasard() * 200);
        g_filante.vy = 140 + hasard() * 80;
        g_filante.vie = 0.9;
        g_prochaine_filante = 6 + hasard() * 9;
    }
    if (g_nb_art > 1) {
        g_art_t += dt;
        if (g_art_t > 7.0 + 1.6) {
            g_art_t = 0;
            g_art_vu = (g_art_vu + 1) % g_nb_art;
        }
    }
}

void habillage_peindre(HDC dc, const Scene *s) {
    dessiner_ciel();
    dessiner_grille();
    dessiner_art(s->page);
    dessiner_titre(s);
    dessiner_menu(s);
    dessiner_infos(s);
    dessiner_page(s);
    dessiner_defile();
    dessiner_fenetre(s);
    GdiFlush();
    BitBlt(dc, 0, 0, W, H, g_dc, 0, 0, SRCCOPY);
}
