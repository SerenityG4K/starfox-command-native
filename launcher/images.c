/*
 * Les images du lanceur, tirées de la cartouche du joueur.
 *
 * Le portage ne livre rien du jeu, pas même une image : le lanceur dessine son décor lui-même, et va
 * chercher les illustrations de Star Fox Command dans la cartouche que le joueur lui a désignée — à chaque
 * lancement, en mémoire, sans rien écrire sur le disque. Sans cartouche, il n'en montre aucune.
 *
 * Ce qu'il en tire :
 *   - les illustrations de l'introduction (3D/Textures/Adv/Opening/…) : 256×192 en 256 couleurs, rangées
 *     dans une texture de 256×256 — 65 536 indices, puis la palette en RGB555 ; compressées en LZ10 (« -cmp ») ;
 *   - l'icône de la cartouche (la bannière : 32×32, 4 bits par point, en tuiles de 8×8, et sa palette).
 *
 * La cartouche se lit par son système de fichiers (NitroFS) : la table des noms (FNT, en-tête 0x40) mène
 * d'un chemin à un numéro de fichier, la table d'allocation (FAT, en-tête 0x48) à son début et sa fin. Un
 * dossier de données (tools/build_data.py) se lit plus simplement : files/<chemin>, system/banner.bin.
 *
 * Sous licence MIT.
 */
#include "images.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE g_rom = INVALID_HANDLE_VALUE;     /* la cartouche, ou rien si c'est un dossier de données */
static wchar_t g_dossier[MAX_PATH];             /* le dossier de données, sinon vide */
static uint8_t *g_fnt;
static uint32_t g_fnt_taille;
static uint8_t *g_fat;
static uint32_t g_fat_taille;
static uint8_t g_entete[0x200];

static int lire_a(uint64_t ou, void *dest, uint32_t n) {
    LARGE_INTEGER p;
    p.QuadPart = (LONGLONG)ou;
    DWORD lu = 0;
    return SetFilePointerEx(g_rom, p, NULL, FILE_BEGIN) && ReadFile(g_rom, dest, n, &lu, NULL) && lu == n;
}

static uint32_t mot(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t demi(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

void images_fermer(void) {
    if (g_rom != INVALID_HANDLE_VALUE) CloseHandle(g_rom);
    g_rom = INVALID_HANDLE_VALUE;
    free(g_fnt);
    free(g_fat);
    g_fnt = g_fat = NULL;
    g_dossier[0] = 0;
}

int images_ouvrir(const wchar_t *source) {
    images_fermer();
    const DWORD a = GetFileAttributesW(source);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;
    if (a & FILE_ATTRIBUTE_DIRECTORY) {
        wcsncpy(g_dossier, source, MAX_PATH - 1);
        g_dossier[MAX_PATH - 1] = 0;
        return 1;
    }
    g_rom = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (g_rom == INVALID_HANDLE_VALUE) return 0;
    if (!lire_a(0, g_entete, sizeof g_entete) || memcmp(g_entete + 0x0C, "ASFP", 4)) { images_fermer(); return 0; }
    g_fnt_taille = mot(g_entete + 0x44);
    g_fat_taille = mot(g_entete + 0x4C);
    if (g_fnt_taille > (4u << 20) || g_fat_taille > (1u << 20)) { images_fermer(); return 0; }
    g_fnt = malloc(g_fnt_taille);
    g_fat = malloc(g_fat_taille);
    if (!g_fnt || !g_fat || !lire_a(mot(g_entete + 0x40), g_fnt, g_fnt_taille) ||
        !lire_a(mot(g_entete + 0x48), g_fat, g_fat_taille)) {
        images_fermer();
        return 0;
    }
    return 1;
}

/* Le numéro d'un fichier d'après son chemin (« 3D/Textures/Adv/Opening/02_team.ntfq-cmp »), ou -1. */
static int numero(const char *chemin) {
    uint32_t dossier = 0;                       /* la racine */
    while (*chemin) {
        const char *fin = strchr(chemin, '/');
        const size_t n = fin ? (size_t)(fin - chemin) : strlen(chemin);
        if (8 * dossier + 8 > g_fnt_taille) return -1;
        uint32_t p = mot(g_fnt + 8 * dossier);
        int id = demi(g_fnt + 8 * dossier + 4);
        for (;;) {
            if (p >= g_fnt_taille) return -1;
            const uint8_t t = g_fnt[p++];
            if (!t) return -1;                   /* fin de la sous-table : introuvable */
            const size_t l = t & 0x7F;
            if (p + l > g_fnt_taille) return -1;
            const int egal = l == n && !memcmp(g_fnt + p, chemin, n);
            p += (uint32_t)l;
            if (t & 0x80) {                      /* un sous-dossier, suivi de son numéro */
                if (p + 2 > g_fnt_taille) return -1;
                const uint16_t sous = demi(g_fnt + p);
                p += 2;
                if (egal && fin) { dossier = sous & 0x0FFFu; break; }
            } else {
                if (egal && !fin) return id;
                id++;
            }
        }
        chemin = fin + 1;
    }
    return -1;
}

/* LZ10 de la console : un en-tête (0x10, taille sur 24 bits), puis des groupes de huit, chacun précédé d'un
   octet de drapeaux — 0 un octet tel quel, 1 une copie (longueur 3 à 18, distance 1 à 4096). */
static uint8_t *lz10(const uint8_t *s, uint32_t n, uint32_t *taille) {
    if (n < 4 || s[0] != 0x10) return NULL;
    const uint32_t t = s[1] | s[2] << 8 | (uint32_t)s[3] << 16;
    uint8_t *d = malloc(t ? t : 1);
    if (!d) return NULL;
    uint32_t i = 4, o = 0;
    while (o < t && i < n) {
        const uint8_t drapeaux = s[i++];
        for (int b = 7; b >= 0 && o < t; b--) {
            if (!(drapeaux >> b & 1)) {
                if (i >= n) break;
                d[o++] = s[i++];
            } else {
                if (i + 1 >= n) break;
                const uint32_t longueur = (uint32_t)(s[i] >> 4) + 3, distance = (uint32_t)((s[i] & 15) << 8 | s[i + 1]) + 1;
                i += 2;
                if (distance > o) { free(d); return NULL; }
                for (uint32_t k = 0; k < longueur && o < t; k++, o++) d[o] = d[o - distance];
            }
        }
    }
    if (o < t) { free(d); return NULL; }
    *taille = t;
    return d;
}

/* Le contenu d'un fichier du jeu (décompressé si son nom finit par « -cmp »), à libérer ; NULL s'il manque. */
static uint8_t *fichier(const char *chemin, uint32_t *taille) {
    uint8_t *brut = NULL;
    uint32_t n = 0;
    if (g_dossier[0]) {
        wchar_t complet[MAX_PATH];
        int k = _snwprintf(complet, MAX_PATH, L"%s\\files\\", g_dossier);
        for (const char *c = chemin; *c && k < MAX_PATH - 1; c++) complet[k++] = *c == '/' ? L'\\' : (wchar_t)(unsigned char)*c;
        complet[k] = 0;
        FILE *f = _wfopen(complet, L"rb");
        if (!f) return NULL;
        fseek(f, 0, SEEK_END);
        const long l = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (l > 0 && l < (16 << 20) && (brut = malloc((size_t)l)) && fread(brut, 1, (size_t)l, f) == (size_t)l) n = (uint32_t)l;
        fclose(f);
    } else if (g_rom != INVALID_HANDLE_VALUE) {
        const int id = numero(chemin);
        if (id < 0 || 8u * (uint32_t)id + 8 > g_fat_taille) return NULL;
        const uint32_t debut = mot(g_fat + 8 * id), fin = mot(g_fat + 8 * id + 4);
        if (fin <= debut || fin - debut > (16u << 20)) return NULL;
        n = fin - debut;
        brut = malloc(n);
        if (brut && !lire_a(debut, brut, n)) n = 0;
    }
    if (!brut || !n) { free(brut); return NULL; }
    const size_t l = strlen(chemin);
    if (l > 4 && !strcmp(chemin + l - 4, "-cmp")) {
        uint8_t *d = lz10(brut, n, taille);
        free(brut);
        return d;
    }
    *taille = n;
    return brut;
}

unsigned images_archives(const wchar_t *source, const char *const *noms, int n) {
    unsigned vues = 0;
    if (!images_ouvrir(source)) return 0;
    for (int i = 0; i < n && i < 32; i++) {
        int la = 0;
        if (g_dossier[0]) {
            wchar_t complet[MAX_PATH];
            _snwprintf(complet, MAX_PATH, L"%s\\files\\%hs", g_dossier, noms[i]);
            complet[MAX_PATH - 1] = 0;
            la = GetFileAttributesW(complet) != INVALID_FILE_ATTRIBUTES;
        } else {
            la = numero(noms[i]) >= 0;
        }
        if (la) vues |= 1u << i;
    }
    images_fermer();
    return vues;
}

static uint32_t couleur(uint16_t c) {           /* RGB555 -> BGRA opaque */
    const uint32_t r = (c & 31) * 255 / 31, v = (c >> 5 & 31) * 255 / 31, b = (c >> 10 & 31) * 255 / 31;
    return 0xFF000000u | r << 16 | v << 8 | b;
}

int image_illustration(const char *chemin, Image *img) {
    uint32_t n = 0;
    uint8_t *d = fichier(chemin, &n);
    if (!d) return 0;
    const uint32_t couleurs = n > 65536 ? (n - 65536) / 2 : 0;
    if (n < 65536 || !couleurs || couleurs > 256) { free(d); return 0; }
    img->largeur = 256;
    img->hauteur = 192;                         /* l'écran ; le reste de la texture est vide */
    img->pixels = malloc(256 * 192 * sizeof *img->pixels);
    if (!img->pixels) { free(d); return 0; }
    uint32_t palette[256];
    for (uint32_t i = 0; i < 256; i++) palette[i] = i < couleurs ? couleur(demi(d + 65536 + 2 * i)) : 0xFF000000u;
    for (int i = 0; i < 256 * 192; i++) img->pixels[i] = palette[d[i]];
    free(d);
    return 1;
}

int image_icone(Image *img) {
    uint8_t banniere[0x240];
    if (g_dossier[0]) {
        wchar_t chemin[MAX_PATH];
        _snwprintf(chemin, MAX_PATH, L"%s\\system\\banner.bin", g_dossier);
        FILE *f = _wfopen(chemin, L"rb");
        if (!f) return 0;
        const size_t lu = fread(banniere, 1, sizeof banniere, f);
        fclose(f);
        if (lu != sizeof banniere) return 0;
    } else if (g_rom == INVALID_HANDLE_VALUE || !mot(g_entete + 0x68) || !lire_a(mot(g_entete + 0x68), banniere, sizeof banniere)) {
        return 0;
    }
    img->largeur = img->hauteur = 32;
    img->pixels = malloc(32 * 32 * sizeof *img->pixels);
    if (!img->pixels) return 0;
    uint32_t palette[16];
    for (int i = 0; i < 16; i++) palette[i] = i ? couleur(demi(banniere + 0x220 + 2 * i)) : 0;   /* 0 : transparent */
    for (int tuile = 0; tuile < 16; tuile++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                const uint8_t o = banniere[0x20 + tuile * 32 + y * 4 + x / 2];
                const int px = (tuile % 4) * 8 + x, py = (tuile / 4) * 8 + y;
                img->pixels[py * 32 + px] = palette[x & 1 ? o >> 4 : o & 15];
            }
    return 1;
}

void image_liberer(Image *img) {
    free(img->pixels);
    img->pixels = NULL;
}
