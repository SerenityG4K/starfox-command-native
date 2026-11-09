/*
 * La manette.
 *
 * Star Fox Command se pilote au stylet ; le jeu en tire, à chaque tour de vol, les commandes du cockpit —
 * tangage, lacet, turbo, frein, tonneau —, et ses boutons de l'écran tactile lancent le looping et le
 * demi-tour. La manette ne fabrique pas de gestes : chaque commande a son bouton, qui la donne directement
 * au cockpit, au même endroit et sous la même forme que le stylet (plus bas, « la manette dans le cockpit »).
 *
 * Les commandes du vol sont celles de Star Fox 64 3D, que le joueur a demandées (Xbox / PlayStation) :
 *  - stick gauche : piloter — le lacet et le tangage que donnerait le stylet décalé d'autant, jusqu'au bout
 *    de sa course utile ; poussé, l'Arwing monte, comme le stylet glissé vers le haut ; lâché, il va droit ;
 *  - laser : A / ✕ — la touche A de la console ;
 *  - turbo : X / □, frein : B / ○ — tenus autant que le bouton ;
 *  - tonneau : RB ou RT / R1 ou R2 à droite, LB ou LT / L1 ou L2 à gauche ;
 *  - bombe : Y / △ — lancée sur la position de l'Arwing, s'il en reste au pilote ;
 *  - looping : stick vers la montée et turbo ; demi-tour : stick vers la montée et frein ; le stick droit
 *    les donne aussi (haut : looping, bas : demi-tour, côtés : tonneau) ;
 *  - Start, Select (Back / Share), la croix : ceux de la console.
 * Hors du vol, les boutons sont ceux de la console par leur place (A / ✕ valide, B / ○ annule), et le stick
 * gauche vaut la croix : sur la carte, dans les menus, à la pause.
 *
 * Les manettes : XInput (Xbox et compatibles, et tout ce que Steam présente comme telles) ; DirectInput pour
 * les autres — DualShock 4 et DualSense, manette Switch Pro, manettes génériques, disposées selon leur
 * fabricant. Un fil à part les interroge et en cherche de nouvelles, pour que l'affichage n'attende jamais
 * une recherche de périphérique. SFC_MANETTE=0 les ignore ; SFC_MANETTE_INVERSE=1 inverse la hauteur,
 * comme dans Star Fox 64 : pousser le stick fait piquer, le tirer fait monter (le lanceur le propose, menu
 * Manette). SFC_MANETTE_ESSAI=fichier les remplace par un script, une ligne par
 * changement — « image gx gy dx dy boutons » (boutons : MANETTE_…, en hexadécimal) —, pour éprouver les
 * commandes dans un rejeu, avec ou sans fenêtre. Un enregistrement (SFC_INPUT_RECORD) note la manette avec
 * les touches : la partie se rejoue à l'identique.
 *
 * Sous licence MIT.
 */
#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800
#include "manette.h"
#include "cpu.h"
#include "mods.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>

void rt_log(const char *fmt, ...);
uint64_t hw_frames(void);
void rt_fil_nommer(const char *nom);

/* ------------------------------------------------------------------ l'état publié par le fil des manettes */
static SRWLOCK g_verrou = SRWLOCK_INIT;
static Manette g_publiee;
static volatile LONG g_fil_lance;
static int g_desactivee = -1;

static int desactivee(void) {
    if (g_desactivee < 0) { const char *e = getenv("SFC_MANETTE"); g_desactivee = e && (*e == '0' || !strcmp(e, "non")); }
    return g_desactivee;
}

/* Une zone morte ronde au centre, puis une droite jusqu'à 95 % de la course, où la commande est pleine — un
   stick usé n'atteint pas toujours le bout. La zone morte que conseille Microsoft pour XInput (24 %) laissait
   le premier quart de la course sans effet, puis la réponse rattrapait d'un coup : le jeu, lui, répond au
   stick en ligne droite (0x021E9D64 : virage et montée proportionnels au décalage du stylet, mesurés). Le
   stick qui pilote n'en garde que 5 %, à la demande du joueur ; celui des manœuvres, qui ne fait que les
   déclencher, garde la zone de Microsoft. */
#define ZONE_MORTE 0.05f
#define BOUT_DE_COURSE 0.95f
static void stick(float fx, float fy, float zone, float *ox, float *oy) {
    float n = sqrtf(fx * fx + fy * fy);
    if (n <= zone) { *ox = *oy = 0; return; }
    float k = ((n > BOUT_DE_COURSE ? BOUT_DE_COURSE : n) - zone) / (BOUT_DE_COURSE - zone);
    *ox = fx / n * k;
    *oy = fy / n * k;
}

/* ------------------------------------------------------------------ XInput */
typedef struct { WORD boutons; BYTE gachette_g, gachette_d; SHORT gx, gy, dx, dy; } XPad;
typedef struct { DWORD paquet; XPad pad; } XEtat;
typedef DWORD(WINAPI *XLireEtat)(DWORD, XEtat *);
static XLireEtat g_xlire;

static const char *charger_xinput(void) {
    static const char *const dll[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (int i = 0; i < 3; i++) {
        HMODULE m = LoadLibraryA(dll[i]);
        if (m && (g_xlire = (XLireEtat)(void *)GetProcAddress(m, "XInputGetState"))) return dll[i];
    }
    return "absent";
}

static int xinput_lire(Manette *m, int chercher) {
    static int courante = -1;
    XEtat e;
    if (!g_xlire) return 0;
    if (courante >= 0 && g_xlire((DWORD)courante, &e) != ERROR_SUCCESS) {
        rt_log("manette : XInput %d débranchée", courante);
        courante = -1;
    }
    if (courante < 0) {                 /* interroger une place vide est lent : seulement quand il le faut */
        if (!chercher) return 0;
        for (int i = 0; i < 4 && courante < 0; i++)
            if (g_xlire((DWORD)i, &e) == ERROR_SUCCESS) { courante = i; rt_log("manette : XInput %d branchée", i); }
        if (courante < 0) return 0;
    }
    const WORD b = e.pad.boutons;
    uint32_t t = 0;
    if (b & 0x1000) t |= MANETTE_SUD;
    if (b & 0x2000) t |= MANETTE_EST;
    if (b & 0x4000) t |= MANETTE_OUEST;
    if (b & 0x8000) t |= MANETTE_NORD;
    if (b & 0x0100) t |= MANETTE_L1;
    if (b & 0x0200) t |= MANETTE_R1;
    if (b & 0x0020) t |= MANETTE_SELECT;
    if (b & 0x0010) t |= MANETTE_START;
    if (b & 0x0040) t |= MANETTE_L3;
    if (b & 0x0080) t |= MANETTE_R3;
    if (b & 0x0001) t |= MANETTE_HAUT;
    if (b & 0x0002) t |= MANETTE_BAS;
    if (b & 0x0004) t |= MANETTE_GAUCHE;
    if (b & 0x0008) t |= MANETTE_DROITE;
    if (e.pad.gachette_g > 127) t |= MANETTE_L2;
    if (e.pad.gachette_d > 127) t |= MANETTE_R2;
    m->boutons = t;
    stick(e.pad.gx / 32767.0f, e.pad.gy / 32767.0f, ZONE_MORTE, &m->gx, &m->gy);
    stick(e.pad.dx / 32767.0f, e.pad.dy / 32767.0f, 8689 / 32767.0f, &m->dx, &m->dy);
    m->branchee = 1;
    return 1;
}

/* ------------------------------------------------------------------ DirectInput : les autres manettes */
/* Comment chaque fabricant range ses boutons et ses axes dans DIJOYSTATE2 :
   - Sony (DualShock 4, DualSense) : □ ✕ ○ △ L1 R1 L2 R2 Share Options L3 R3 ; stick droit en Z et Rz ;
     gâchettes analogiques en Rx et Ry ;
   - Nintendo (Switch Pro) : B A Y X L R ZL ZR − + L3 R3 ; stick droit en Rx et Ry ;
   - les autres : la disposition la plus répandue sur PC, celle de Sony (gauche, bas, droite, haut, …), stick
     droit en Z et Rz, gâchettes en boutons. */
enum { FAB_AUTRE, FAB_SONY, FAB_NINTENDO };
static const uint32_t ORDRE_SONY[12] = {MANETTE_OUEST, MANETTE_SUD, MANETTE_EST, MANETTE_NORD, MANETTE_L1, MANETTE_R1,
                                        MANETTE_L2, MANETTE_R2, MANETTE_SELECT, MANETTE_START, MANETTE_L3, MANETTE_R3};
static const uint32_t ORDRE_NINTENDO[12] = {MANETTE_SUD, MANETTE_EST, MANETTE_OUEST, MANETTE_NORD, MANETTE_L1, MANETTE_R1,
                                            MANETTE_L2, MANETTE_R2, MANETTE_SELECT, MANETTE_START, MANETTE_L3, MANETTE_R3};

static IDirectInput8A *g_di;
static IDirectInputDevice8A *g_pad_di;
static int g_fabricant;
static HWND g_fenetre;                  /* une fenêtre cachée, à ce fil : DirectInput en veut une, et Windows
                                           y annonce l'arrivée et le départ des périphériques */
static struct { WORD vid, pid; } g_xinput_ids[16];
static int g_xinput_n;

/* Les manettes XInput apparaissent aussi dans DirectInput : on les reconnaît à « IG_ » dans leur nom de
   périphérique (Raw Input), pour ne pas les lire deux fois. */
static void relever_xinput(void) {
    g_xinput_n = 0;
    UINT n = 0;
    if (GetRawInputDeviceList(NULL, &n, sizeof(RAWINPUTDEVICELIST)) != 0 || !n) return;
    RAWINPUTDEVICELIST *liste = malloc(n * sizeof *liste);
    if (!liste) return;
    UINT lus = GetRawInputDeviceList(liste, &n, sizeof(RAWINPUTDEVICELIST));
    for (UINT i = 0; i < lus && lus != (UINT)-1; i++) {
        if (liste[i].dwType != RIM_TYPEHID) continue;
        char nom[512];
        UINT taille = sizeof nom;
        if (GetRawInputDeviceInfoA(liste[i].hDevice, RIDI_DEVICENAME, nom, &taille) == (UINT)-1) continue;
        nom[sizeof nom - 1] = 0;
        if (!strstr(nom, "IG_") && !strstr(nom, "ig_")) continue;
        const char *v = strstr(nom, "VID_"), *p = strstr(nom, "PID_");
        if (!v) v = strstr(nom, "vid_");
        if (!p) p = strstr(nom, "pid_");
        if (v && p && g_xinput_n < 16) {
            g_xinput_ids[g_xinput_n].vid = (WORD)strtoul(v + 4, NULL, 16);
            g_xinput_ids[g_xinput_n].pid = (WORD)strtoul(p + 4, NULL, 16);
            g_xinput_n++;
        }
    }
    free(liste);
}

static BOOL CALLBACK une_manette(LPCDIDEVICEINSTANCEA inst, LPVOID trouve) {
    const WORD vid = LOWORD(inst->guidProduct.Data1), pid = HIWORD(inst->guidProduct.Data1);
    for (int i = 0; i < g_xinput_n; i++)
        if (g_xinput_ids[i].vid == vid && g_xinput_ids[i].pid == pid) return DIENUM_CONTINUE;
    IDirectInputDevice8A *dev = NULL;
    if (FAILED(IDirectInput8_CreateDevice(g_di, &inst->guidInstance, &dev, NULL))) return DIENUM_CONTINUE;
    if (FAILED(IDirectInputDevice8_SetDataFormat(dev, &c_dfDIJoystick2))
        || FAILED(IDirectInputDevice8_SetCooperativeLevel(dev, g_fenetre, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
        IDirectInputDevice8_Release(dev);
        return DIENUM_CONTINUE;
    }
    /* tous les axes de -32768 à 32767 */
    DIPROPRANGE r;
    r.diph.dwSize = sizeof r;
    r.diph.dwHeaderSize = sizeof r.diph;
    r.diph.dwHow = DIPH_DEVICE;
    r.diph.dwObj = 0;
    r.lMin = -32768;
    r.lMax = 32767;
    IDirectInputDevice8_SetProperty(dev, DIPROP_RANGE, &r.diph);
    IDirectInputDevice8_Acquire(dev);
    g_pad_di = dev;
    g_fabricant = vid == 0x054C ? FAB_SONY : vid == 0x057E ? FAB_NINTENDO : FAB_AUTRE;
    rt_log("manette : %s branchée (DirectInput, %04X:%04X, disposition %s)", inst->tszProductName, vid, pid,
           g_fabricant == FAB_SONY ? "Sony" : g_fabricant == FAB_NINTENDO ? "Nintendo" : "générique");
    *(int *)trouve = 1;
    return DIENUM_STOP;
}

static void directinput_chercher(void) {
    if (!g_fenetre) return;
    if (!g_di) {
        if (FAILED(DirectInput8Create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, &IID_IDirectInput8A, (void **)&g_di, NULL))) {
            static int dit;
            if (!dit++) rt_log("manette : DirectInput indisponible");
            g_di = NULL;
            return;
        }
    }
    relever_xinput();
    int trouve = 0;
    IDirectInput8_EnumDevices(g_di, DI8DEVCLASS_GAMECTRL, une_manette, &trouve, DIEDFL_ATTACHEDONLY);
}

static float axe(LONG v) { return (float)v / 32767.0f < -1.0f ? -1.0f : (float)v / 32767.0f; }

static int directinput_lire(Manette *m, int chercher) {
    if (!g_pad_di) {
        if (chercher) directinput_chercher();
        if (!g_pad_di) return 0;
    }
    DIJOYSTATE2 j;
    HRESULT h = IDirectInputDevice8_Poll(g_pad_di);
    if (FAILED(h)) { IDirectInputDevice8_Acquire(g_pad_di); IDirectInputDevice8_Poll(g_pad_di); }
    if (FAILED(IDirectInputDevice8_GetDeviceState(g_pad_di, sizeof j, &j))) {
        if (FAILED(IDirectInputDevice8_Acquire(g_pad_di)) || FAILED(IDirectInputDevice8_GetDeviceState(g_pad_di, sizeof j, &j))) {
            rt_log("manette : DirectInput débranchée");
            IDirectInputDevice8_Unacquire(g_pad_di);
            IDirectInputDevice8_Release(g_pad_di);
            g_pad_di = NULL;
            return 0;
        }
    }
    const uint32_t *ordre = g_fabricant == FAB_NINTENDO ? ORDRE_NINTENDO : ORDRE_SONY;
    uint32_t t = 0;
    for (int i = 0; i < 12; i++) if (j.rgbButtons[i] & 0x80) t |= ordre[i];
    /* la croix : le premier chapeau, en centièmes de degré depuis le haut */
    DWORD pov = j.rgdwPOV[0];
    if (LOWORD(pov) != 0xFFFF) {
        if (pov > 31500 || pov < 4500) t |= MANETTE_HAUT;
        if (pov > 4500 && pov < 13500) t |= MANETTE_DROITE;
        if (pov > 13500 && pov < 22500) t |= MANETTE_BAS;
        if (pov > 22500 && pov < 31500) t |= MANETTE_GAUCHE;
        if (pov == 4500) t |= MANETTE_HAUT | MANETTE_DROITE;
        if (pov == 13500) t |= MANETTE_BAS | MANETTE_DROITE;
        if (pov == 22500) t |= MANETTE_BAS | MANETTE_GAUCHE;
        if (pov == 31500) t |= MANETTE_HAUT | MANETTE_GAUCHE;
    }
    float rx, ry;
    if (g_fabricant == FAB_NINTENDO) { rx = axe(j.lRx); ry = axe(j.lRy); }
    else { rx = axe(j.lZ); ry = axe(j.lRz); }
    if (g_fabricant == FAB_SONY) {          /* gâchettes analogiques, de -32768 (relâchée) à 32767 */
        if (j.lRx > 0) t |= MANETTE_L2;
        if (j.lRy > 0) t |= MANETTE_R2;
    }
    m->boutons = t;
    /* en DirectInput, y croît vers le bas */
    stick(axe(j.lX), -axe(j.lY), ZONE_MORTE, &m->gx, &m->gy);
    stick(rx, -ry, 0.26f, &m->dx, &m->dy);
    m->branchee = 1;
    return 1;
}

/* ------------------------------------------------------------------ le fil des manettes */
/* Chercher une manette coûte : une place XInput vide répond lentement, et l'inventaire de DirectInput prend
   des dizaines de millisecondes. On cherche donc au démarrage, puis quand Windows annonce qu'un périphérique
   arrive ou part — une demi-seconde après, le temps qu'il soit prêt, et encore deux fois ensuite —, et au
   plus une fois toutes les trente secondes sinon. */
static volatile LONG g_changements;

static LRESULT CALLBACK fenetre_manettes(HWND f, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DEVICECHANGE) InterlockedIncrement(&g_changements);
    return DefWindowProcA(f, msg, wp, lp);
}

static DWORD WINAPI fil_manettes(void *p) {
    (void)p;
    rt_fil_nommer("manettes");
    rt_log("manette : XInput (%s) et DirectInput, en attente", charger_xinput());
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = fenetre_manettes;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "sfc_manettes";
    RegisterClassA(&wc);
    g_fenetre = CreateWindowExA(0, "sfc_manettes", "", WS_POPUP, 0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
    int source = 0;                     /* 1 XInput, 2 DirectInput : pour le journal */
    LONG vus = 0;
    int restes = 1;                     /* les recherches prévues, à partir de `quand` */
    DWORD quand = GetTickCount(), derniere = 0;
    for (;;) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        const DWORD t = GetTickCount();
        const LONG c = g_changements;
        if (c != vus) { vus = c; quand = t + 500; restes = 3; }
        int chercher = 0;
        if ((restes && (LONG)(t - quand) >= 0) || t - derniere >= 30000) {
            chercher = 1;
            derniere = t;
            if (restes) { restes--; quand = t + 1000; }
        }
        Manette m;
        memset(&m, 0, sizeof m);
        const int s = xinput_lire(&m, chercher) ? 1 : directinput_lire(&m, chercher) ? 2 : 0;
        if (s != source && !s) rt_log("manette : aucune");
        source = s;
        AcquireSRWLockExclusive(&g_verrou);
        g_publiee = m;
        ReleaseSRWLockExclusive(&g_verrou);
        Sleep(4);
    }
    return 0;
}

static void lancer_fil(void) {
    if (InterlockedCompareExchange(&g_fil_lance, 1, 0) != 0) return;
    HANDLE h = CreateThread(NULL, 0, fil_manettes, NULL, 0, NULL);
    if (h) CloseHandle(h);
}

/* ------------------------------------------------------------------ le script d'essai */
typedef struct { uint64_t image; Manette m; } LigneEssai;
static LigneEssai *g_essai;
static size_t g_essai_n;
static int g_essai_lu;

static void lire_essai(void) {
    g_essai_lu = 1;
    const char *chemin = getenv("SFC_MANETTE_ESSAI");
    if (!chemin) return;
    FILE *f = fopen(chemin, "r");
    if (!f) { rt_log("manette : script %s introuvable", chemin); return; }
    size_t cap = 64;
    g_essai = malloc(cap * sizeof *g_essai);
    unsigned long long img;
    float gx, gy, dx, dy;
    unsigned b;
    while (g_essai && fscanf(f, "%llu %f %f %f %f %x", &img, &gx, &gy, &dx, &dy, &b) == 6) {
        if (g_essai_n == cap) g_essai = realloc(g_essai, (cap *= 2) * sizeof *g_essai);
        LigneEssai *l = &g_essai[g_essai_n++];
        l->image = img;
        l->m = (Manette){1, b, gx, gy, dx, dy};
    }
    fclose(f);
    rt_log("manette : script d'essai %s, %zu lignes", chemin, g_essai_n);
}

/* l'état du script à cette image : la dernière ligne qui n'est pas après elle */
static int etat_essai(uint64_t image, Manette *m) {
    if (!g_essai_lu) lire_essai();
    if (!g_essai_n) return 0;
    size_t k = 0;
    while (k + 1 < g_essai_n && g_essai[k + 1].image <= image) k++;
    if (g_essai[k].image > image) { memset(m, 0, sizeof *m); m->branchee = 1; return 1; }
    *m = g_essai[k].m;
    return 1;
}

int manette_script(Manette *m) { return etat_essai(hw_frames(), m); }

void manette_lire(Manette *m) {
    memset(m, 0, sizeof *m);
    if (!g_essai_lu) lire_essai();
    if (g_essai_n) { etat_essai(hw_frames(), m); return; }
    if (desactivee()) return;
    lancer_fil();
    AcquireSRWLockShared(&g_verrou);
    *m = g_publiee;
    ReleaseSRWLockShared(&g_verrou);
}

/* ------------------------------------------------------------------ les touches de la console */
/* Le vol : la pile des scènes du jeu (objet global 0x0216BED4, hauteur en +0x188) a deux étages — un sur la
   carte et dans les menus —, et aucune fenêtre n'est ouverte par-dessus. Le gestionnaire des fenêtres
   (0x0216C2E8, la liste des fenêtres puis leur nombre en +0x80) écrit en +0x84 le nom de celle qui s'ouvre
   ainsi : « PauseWindow » à la pause, dont les boutons Continuer, Rejouer et Quitter sont sur l'écran
   tactile, là où la manette poserait son stylet ; rien pendant les trois vols de la partie obs52.
   Relevé par le fil du jeu à chaque image ; lu aussi par celui de l'affichage, pour les tours d'aperçu. */
static volatile int g_en_vol;

static int lire_en_vol(void) {
    uint32_t h;
    memcpy(&h, g_main_ram + (0x0216BED4u + 0x188u - 0x02000000u), 4);
    const char *fenetre = (const char *)g_main_ram + (0x0216C36Cu - 0x02000000u);
    const int vol = h == 2 && !fenetre[0];
    static int trace = -1, vu = -1;
    if (trace < 0) trace = getenv("SFC_TRACE_INPUT") != NULL;
    if (trace && vol != vu) {
        char nom[17];
        memcpy(nom, fenetre, 16);
        nom[16] = 0;
        if (vol) rt_log("manette : en vol (image %llu)", (unsigned long long)hw_frames());
        else rt_log("manette : hors du vol — pile des scènes à %u étages, fenêtre « %s » (image %llu)", h, nom,
                    (unsigned long long)hw_frames());
        vu = vol;
    }
    return vol;
}

/* les sticks comme des boutons : le gauche au-delà de la moitié de sa course (la croix, hors du vol), le droit
   au-delà de 60 % (les raccourcis du vol) */
#define GAUCHE_HAUT (1u << 16)
#define GAUCHE_BAS (1u << 17)
#define GAUCHE_G (1u << 18)
#define GAUCHE_D (1u << 19)
#define DROIT_HAUT (1u << 20)
#define DROIT_BAS (1u << 21)
#define DROIT_COTE (1u << 22)

static uint32_t boutons(const Manette *m) {
    uint32_t b = m->boutons;
    if (m->gx > 0.5f) b |= GAUCHE_D;
    if (m->gx < -0.5f) b |= GAUCHE_G;
    if (m->gy > 0.5f) b |= GAUCHE_HAUT;
    if (m->gy < -0.5f) b |= GAUCHE_BAS;
    if (m->dy > 0.6f) b |= DROIT_HAUT;
    if (m->dy < -0.6f) b |= DROIT_BAS;
    if (fabsf(m->dx) > 0.6f) b |= DROIT_COTE;
    return b;
}

/* Ce qui change de sens en entrant en vol ou en en sortant. Tenu à ce moment-là, cela ne compte qu'une fois
   relâché : le frein tenu à la fin d'une mission n'annule pas l'écran qui suit ; A, tenu en validant le
   départ, ne tire pas en arrivant en vol. */
#define SELON_LE_VOL (MANETTE_SUD | MANETTE_EST | MANETTE_OUEST | MANETTE_NORD | MANETTE_L1 | MANETTE_R1 \
                      | MANETTE_L2 | MANETTE_R2 | GAUCHE_HAUT | GAUCHE_BAS | GAUCHE_G | GAUCHE_D \
                      | DROIT_HAUT | DROIT_BAS | DROIT_COTE)
static volatile uint32_t g_masque;

/* KEYINPUT et EXTKEYIN tels que les range `held` : A B Select Start → ← ↑ ↓ R L X Y */
enum { DS_A = 1u << 0, DS_B = 1u << 1, DS_SELECT = 1u << 2, DS_START = 1u << 3, DS_DROITE = 1u << 4,
       DS_GAUCHE = 1u << 5, DS_HAUT = 1u << 6, DS_BAS = 1u << 7, DS_R = 1u << 8, DS_L = 1u << 9,
       DS_X = 1u << 10, DS_Y = 1u << 11 };

uint32_t manette_touches(const Manette *m) {
    if (!m->branchee) return 0;
    const uint32_t b = boutons(m) & ~g_masque;
    uint32_t t = 0;
    if (b & MANETTE_SELECT) t |= DS_SELECT;
    if (b & MANETTE_START) t |= DS_START;
    if (b & MANETTE_HAUT) t |= DS_HAUT;
    if (b & MANETTE_BAS) t |= DS_BAS;
    if (b & MANETTE_GAUCHE) t |= DS_GAUCHE;
    if (b & MANETTE_DROITE) t |= DS_DROITE;
    if (b & MANETTE_SUD) t |= DS_A;
    if (!g_en_vol) {
        /* en vol, ceux-là sont des commandes du cockpit (plus bas) */
        if (b & MANETTE_NORD) t |= DS_Y;
        if (b & MANETTE_EST) t |= DS_B;
        if (b & MANETTE_OUEST) t |= DS_X;
        if (b & MANETTE_L1) t |= DS_L;
        if (b & MANETTE_R1) t |= DS_R;
        if (b & GAUCHE_D) t |= DS_DROITE;
        if (b & GAUCHE_G) t |= DS_GAUCHE;
        if (b & GAUCHE_HAUT) t |= DS_HAUT;
        if (b & GAUCHE_BAS) t |= DS_BAS;
    }
    return t;
}

/* ------------------------------------------------------------------ la manette dans le cockpit
 *
 * En vol, le jeu tire du stylet, à chaque tour, les commandes du cockpit. La mise à jour du contrôle au
 * stylet (ArwingCockpitRelativeCtrl, 0x021E9D64) les range dans un bloc de l'objet du cockpit (son + 0x68,
 * que rend 0x021E9428), et le tour du cockpit (0x0220EFC4) les lit aussitôt après :
 *   +0x30, +0x32, +0x34  tangage, lacet, inclinaison (demi-mots, 0x10000 le tour). Le stylet décalé de
 *                        (dx, dy) points depuis son point d'appui y met −0,85·dx degrés de lacet (±45 au
 *                        plus), 0,85·dy·4/3 de tangage (0,85·dy borné à ±25), et −1,2 fois le lacet
 *                        d'inclinaison ; le virage serré du bord de l'écran lève +0x5A ;
 *   +0x41, +0x42, +0x48  tonneau à gauche, à droite, et 10 : ce que pose la reconnaissance d'un cercle
 *                        (0x021EC1C4) ;
 *   +0x43, +0x44, +0x46  turbo, frein, « en cours », et +0x50 = 5 : ce que pose la double tape. Tant que la
 *                        minuterie +0x50 court, 0x021EBACC la décompte et tient « en cours » ; ensuite,
 *                        0x021EBBB8 compare « en cours » au stylet posé, et s'ils diffèrent — le stylet
 *                        levé —, efface tout et lève +0x58, que le cockpit lit pour arrêter l'Arwing
 *                        (0x021EE294) ; le vol, lui, peut tout interrompre en levant +0x59 (jauge vide,
 *                        manœuvre).
 * Le looping et le demi-tour ne passent pas par là : leurs boutons de l'écran tactile (0x02205440) appellent
 * 0x021EE528 et 0x021EE550 sur la dynamique de l'Arwing (0x02188AD0), si la mission les permet (0x021A209C,
 * 0x021A2128).
 *
 * La manette passe par des points de modification (runtime/modifications.c).
 *  - Le pilotage, dans la mise à jour du contrôle (0x021E9D64). Elle y demande deux fois, quand l'Arwing
 *    obéit aux commandes (ni pendant une manœuvre, ni pendant une scène), si le stylet tient le manche —
 *    l'octet 0x02222C6C, que l'écran tactile lève quand l'appui commence hors des boutons — et s'il est
 *    posé : pour piloter (0x021E9E84, 0x021E9E96), et pour l'aide au verrouillage (0x021EA30A,
 *    0x021EA31A). La manette y répond oui, et place elle-même le point du stylet (+0x10, +0x14 du
 *    contrôle) : au centre au premier tour, que le jeu prend pour point d'appui, puis décalé comme le stick.
 *    Tout le reste est le code du jeu : l'échelle, les bornes, l'aide au verrouillage, l'inclinaison. Le
 *    jeu lit toujours le vrai stylet pour les doubles tapes : la manette ne peut pas en faire par mégarde.
 *    Il reconnaît en revanche le cercle sur ce stylet-là (0x021EC1C4) : tourner le stick en rond ferait un
 *    tonneau. Le joueur ne le veut pas — le tonneau est aux gâchettes —, et la fenêtre de reconnaissance
 *    expire donc à chaque tour quand la manette pilote (0x021E9F76).
 *    Pendant la poursuite d'un missile, le jeu prend un autre contrôle, sur rails (ArwingCockpitRelRailsCtrl,
 *    0x021EAF90), qui pilote lui aussi depuis un point d'appui. Il demande seulement si le stylet est posé
 *    (0x021EB016) et, au premier tour d'un appui, s'il tient le manche (0x021EB02A) : la manette y répond de
 *    même, à l'échelle de ce contrôle, et la fenêtre du cercle y expire aussi (0x021EB080).
 *    Un combat, et une poursuite, s'ouvrent sur un plan pendant lequel rien ne pilote ; passé deux secondes, le
 *    stylet posé le referme (0x0204863C) : le stick poussé aussi.
 *  - Le turbo, le frein et les manœuvres, juste après cette mise à jour (0x0220F01E), avant que le cockpit
 *    ne lise le bloc : la manette y écrit ce que poseraient la double tape et le cercle, et appelle le
 *    looping et le demi-tour comme leurs boutons.
 * Pendant un tour d'aperçu, les crochets donnent les commandes tenues mais ne demandent aucune manœuvre et ne
 * changent rien de leur propre état : le vrai tour s'en charge. Le stylet de la souris garde la main : dès
 * qu'il se pose, la manette se tait, jusqu'à ce qu'on s'en resserve.
 */
extern int g_apercu_en_cours;               /* natif.c : un tour d'aperçu, que l'affichage efface ensuite */

static Manette g_jeu;                       /* la manette de l'image, sur le fil du jeu */
static int g_stylet;                        /* le stylet est posé à cette image */
static int g_pilote;                        /* la manette pilote : servie depuis le dernier appui du stylet */
static uint32_t g_avant;                    /* les boutons et les sticks à l'image précédente, pour les fronts */
static uint32_t g_consomme;                 /* X ou B tenus depuis un looping ou un demi-tour : ni turbo ni frein */
enum { BOUCLE, DEMITOUR, TONNEAU_G, TONNEAU_D, BOMBE, N_DEMANDES };
static uint64_t g_demande[N_DEMANDES];      /* l'image où la manette a demandé chaque manœuvre, 0 sinon */
#define DEMANDE_VALABLE 8                   /* images : un tour de vol en dure deux, le cockpit en saute parfois un */
static int g_obeit;                         /* l'Arwing obéit aux commandes à ce tour : posé par les crochets du
                                               contrôle, lu par celui des commandes, qui passe après eux */
static int g_accel;                         /* la manette tient le turbo ou le frein */
/* Le sens du stick en hauteur. Par défaut, pousser fait monter, comme le stylet glissé vers le haut — le
   joueur a trouvé l'autre sens inversé ; SFC_MANETTE_INVERSE=1 donne celui de Star Fox 64, où pousser fait
   piquer. Le looping et le demi-tour commencent en montant : on les demande le stick vers la montée, poussé
   ou tiré selon le sens choisi — tiré vers soi, comme dans Star Fox 64, quand on y joue à sa façon. */
static int sens_aviation(void) {
    static int v = -1;
    if (v < 0) { const char *e = getenv("SFC_MANETTE_INVERSE"); v = e && *e && *e != '0'; }
    return v;
}
#define VERS_LA_MONTEE 0.6f                 /* au-delà, le stick est vers la montée : looping, demi-tour */
/* les boutons qui, en vol, sont des commandes du cockpit plutôt que des touches de la console */
#define BOUTONS_DU_VOL (MANETTE_OUEST | MANETTE_EST | MANETTE_L1 | MANETTE_R1 | MANETTE_L2 | MANETTE_R2 \
                        | MANETTE_NORD | DROIT_HAUT | DROIT_BAS | DROIT_COTE)

void manette_appliquer(const Manette *m, uint32_t *touches, int stylet) {
    g_obeit = 0;                    /* rien ne reste d'un tour d'aperçu interrompu entre les crochets */
    const int vol = lire_en_vol();
    const uint32_t b = m->branchee ? boutons(m) : 0;
    g_masque = vol != g_en_vol ? b & SELON_LE_VOL : g_masque & b;
    g_en_vol = vol;
    const uint32_t appuye = b & ~g_avant;
    g_avant = b;
    g_stylet = stylet;
    if (!m->branchee) { memset(&g_jeu, 0, sizeof g_jeu); g_pilote = 0; return; }
    g_jeu = *m;
    *touches |= manette_touches(m);
    if (stylet) g_pilote = 0;
    if (!vol) { memset(g_demande, 0, sizeof g_demande); g_consomme = 0; return; }
    if (!stylet && (m->gx != 0 || m->gy != 0 || (b & BOUTONS_DU_VOL))) g_pilote = 1;
    const uint64_t t = hw_frames();
    const int monte = sens_aviation() ? m->gy < -VERS_LA_MONTEE : m->gy > VERS_LA_MONTEE;
    g_consomme &= b;
    if ((appuye & MANETTE_OUEST) && monte) { g_demande[BOUCLE] = t; g_consomme |= MANETTE_OUEST; }
    if ((appuye & MANETTE_EST) && monte) { g_demande[DEMITOUR] = t; g_consomme |= MANETTE_EST; }
    if (appuye & DROIT_HAUT) g_demande[BOUCLE] = t;
    if (appuye & DROIT_BAS) g_demande[DEMITOUR] = t;
    if (appuye & (MANETTE_L1 | MANETTE_L2)) g_demande[TONNEAU_G] = t;
    if (appuye & (MANETTE_R1 | MANETTE_R2)) g_demande[TONNEAU_D] = t;
    if (appuye & DROIT_COTE) g_demande[m->dx < 0 ? TONNEAU_G : TONNEAU_D] = t;
    if (appuye & MANETTE_NORD) g_demande[BOMBE] = t;
}

/* Appeler une fonction du jeu depuis le crochet du cockpit : comme si le cockpit l'appelait juste après la
   mise à jour du stylet — lr et la pile fantôme disent où revenir, et son retour s'y reconnaît. */
static void appeler(Cpu *c, uint32_t adresse) {
    const uint32_t retour = 0x0220F01Fu;
    c->r[14] = retour;
    RT_CALL(c, retour, rt_call(c, adresse));
}

/* Le looping ou le demi-tour, comme le bouton de l'écran tactile (0x02205440). L'état du processeur est
   rendu tel quel ensuite : le cockpit reprend où il en était, la manœuvre en plus. */
static void manoeuvre(Cpu *c, int demi_tour) {
    Cpu garde = *c;
    c->r[0] = 0x021D68B8u;
    appeler(c, (demi_tour ? 0x021A2128u : 0x021A209Cu) | 1u);
    if (c->r[0]) {
        appeler(c, 0x0218A340u | 1u);
        appeler(c, 0x02188AD0u | 1u);
        const uint32_t dynamique = c->r[0];
        if (dynamique) {
            c->r[0] = dynamique;
            c->r[1] = 0;
            appeler(c, (demi_tour ? 0x021EE550u : 0x021EE528u) | 1u);
        }
    }
    *c = garde;
}

/* La bombe, comme l'icône B glissée sur le radar (0x02205030) : la dynamique de l'Arwing lance la bombe
   intelligente vers un point de la carte (0x021ECD8C, qui n'en prend que x et z — la hauteur, il la fixe à
   3 000). La manette vise la position de l'Arwing (l'acteur du joueur, 0x02188B98, en +0x3C). Le lancement
   ne regarde pas le stock : l'écran tactile n'offre l'icône que s'il reste une bombe au pilote. La manette
   demande donc à la mission si la bombe est permise (0x021A23B0, l'octet +0x75), puis le stock du pilote —
   sa fiche dans l'état de mission (0x021D68B8, fiches en +4, le rang du pilote par 0x021889A4), +0x70.
   L'écran tactile ne le décompte que pour ses propres bombes, à la fin de la livraison (0x022010AE) : la
   manette le décompte donc elle-même, dès que la bombe part. Pendant une livraison, le lancement refuse
   d'en faire une seconde (+0xF4 de la dynamique, la bombe en cours) ; la manette ne le lui demande pas. */
/* la bombe que la dynamique a en cours : sa poignée (+0xF4) désigne l'acteur tant qu'il vit — le lancement
   fait le même test pour refuser une seconde bombe */
static uint32_t bombe_en_cours(Cpu *c, uint32_t dynamique) {
    const uint32_t poignee = ldr_word(c, dynamique + 0xF4u);
    const uint32_t objet = poignee ? ldr_word(c, poignee) : 0;
    return objet ? objet - 0x1Cu : 0;
}

static void bombe(Cpu *c) {
    Cpu garde = *c;
    c->r[0] = 0x021D68B8u;
    appeler(c, 0x021A23B0u | 1u);
    if (c->r[0]) {
        appeler(c, 0x0218A340u | 1u);
        const uint32_t scene = c->r[0];
        appeler(c, 0x02188B98u | 1u);
        const uint32_t acteur = c->r[0];
        if (acteur) {
            c->r[0] = scene;
            c->r[1] = acteur;
            appeler(c, 0x021889A4u | 1u);
            const int32_t pilote = (int32_t)c->r[0];
            uint32_t fiche = 0;
            if (pilote >= 0 && pilote < 17) fiche = ldr_word(c, 0x021D68BCu + 4u * (uint32_t)pilote);
            if (fiche && (int32_t)ldr_word(c, fiche + 0x70u) > 0) {
                c->r[0] = scene;
                appeler(c, 0x02188AD0u | 1u);
                const uint32_t dynamique = c->r[0];
                if (dynamique && !bombe_en_cours(c, dynamique)) {
                    c->r[0] = dynamique;
                    c->r[1] = acteur + 0x3Cu;
                    appeler(c, 0x021ECD8Cu | 1u);
                    /* partie : le stock baisse d'une bombe, comme l'écran tactile le fait pour les siennes
                       (0x022010AE) — hors d'une partie en réseau, où il ne le fait pas */
                    if (bombe_en_cours(c, dynamique)) {
                        c->r[0] = 0x020B1AFCu;
                        appeler(c, 0x02059700u | 1u);
                        const int32_t reste = (int32_t)ldr_word(c, fiche + 0x70u);
                        if (!c->r[0] && reste > 0 && !mods_bombes_infinies()) mem_write32(c, fiche + 0x70u, (uint32_t)(reste - 1));
                    }
                }
            }
        }
    }
    *c = garde;
}

static int manette_pilote(void) { return g_jeu.branchee && g_pilote && !g_stylet && g_en_vol; }

/* 0x021E9D64, en 0x021E9E84 et 0x021EA30A — r0 : le stylet tient-il le manche. On n'y passe que si
   l'Arwing obéit aux commandes : le crochet des commandes le saura. */
void manette_crochet_manche(Cpu *c) {
    g_obeit = 1;
    if (manette_pilote()) c->r[0] = 1;
}

/* 0x021E9D64, en 0x021EA31A — r0 : le stylet est-il posé, pour l'aide au verrouillage */
void manette_crochet_appui(Cpu *c) {
    if (manette_pilote()) c->r[0] = 1;
}

/* 0x021E9D64, en 0x021E9F76 — juste avant la reconnaissance du cercle (0x021EC1C4) ; r4 est le contrôle.
   Elle compte les demi-tours du stylet dans une fenêtre de huit tours (+0x3C), qu'elle remet à zéro quand
   la fenêtre expire — comme quand le stylet s'arrête. Quand la manette pilote, la fenêtre expire à chaque
   tour : tourner le stick en rond ne fait pas de tonneau, le joueur ayant les gâchettes pour cela. */
void manette_crochet_cercle(Cpu *c) {
    if (manette_pilote()) mem_write32(c, c->r[4] + 0x3Cu, 1);
}

/* Le point du stylet que la manette donne au contrôle (+0x10, +0x14, en 20.12) : au centre au premier tour
   d'un appui (+0x71 levé : le jeu en fait son point d'appui), puis décalé comme le stick, de « côté » et
   « hauteur » points au bout de sa course. */
static void placer_le_stylet(Cpu *c, uint32_t ctrl, float cote, float hauteur) {
    float x = 128.0f, y = 96.0f;
    if (!mem_read8(c, ctrl + 0x71u)) {
        /* en hauteur : pousser le stick le glisse vers le haut, et l'Arwing monte — l'inverse dans le sens de
           Star Fox 64 */
        x += g_jeu.gx * cote;
        y += (sens_aviation() ? g_jeu.gy : -g_jeu.gy) * hauteur;
    }
    mem_write32(c, ctrl + 0x10u, (uint32_t)lroundf(x * 4096.0f));
    mem_write32(c, ctrl + 0x14u, (uint32_t)lroundf(y * 4096.0f));
}

/* 0x021E9D64, en 0x021E9E96 — r0 : le stylet est-il posé, pour piloter ; r4 est le contrôle. Quand la
   manette pilote, il l'est, au point que donne le stick : 53 points de côté et 29,5 en hauteur au bout de sa
   course, ce qu'il faut pour que le jeu, qui en prend 0,85, touche ses bornes de 45 et 25. */
void manette_crochet_stylet(Cpu *c) {
    if (!manette_pilote()) return;
    placer_le_stylet(c, c->r[4], 53.0f, 29.5f);
    c->r[0] = 1;
}

/* 0x021EAF90, en 0x021EB016 — le contrôle sur rails (ArwingCockpitRelRailsCtrl), celui des poursuites de
   missile : r1 dit si le stylet est posé ; on n'y passe que si l'Arwing obéit aux commandes (+0x94 du contrôle
   baissé) ; r4 est le contrôle. Il pilote comme l'autre, depuis le point d'appui, mais ne prend que 0,3 du
   décalage (0x4CC), borné à 35 de côté et 45 en hauteur. 117 et 151 points au bout de la course du stick
   toucheraient ces bornes : le joueur a trouvé la poursuite trop sensible ainsi, et le stick n'en donne que la
   moitié — 58,5 et 75,5 points, soit 17,5° de lacet et 30° de tangage au bout de sa course. */
void manette_crochet_rails(Cpu *c) {
    g_obeit = 1;
    if (!manette_pilote()) return;
    placer_le_stylet(c, c->r[4], 58.5f, 75.5f);
    c->r[1] = 1;
}

/* 0x02046E58, en 0x0204863C — le plan qui ouvre un combat (DemoTarget… : la cible de la mission), et la
   poursuite d'un missile. Pendant ce plan, aucun contrôle ne pilote. Passé deux secondes (+0xD8, 0x2000), il se
   referme dès qu'une touche vient d'être pressée ou que le stylet est posé — r0 : le stylet est-il posé —, et
   de lui-même à quatre. Quand la manette pilote, le stick poussé ou un bouton du vol tenu compte comme le
   stylet posé : ce qui pilote l'Arwing referme le plan, comme le stylet sur la console. */
void manette_crochet_ouverture(Cpu *c) {
    if (manette_pilote() && (g_jeu.gx != 0 || g_jeu.gy != 0 || (boutons(&g_jeu) & ~g_masque & BOUTONS_DU_VOL)))
        c->r[0] = 1;
}

/* 0x0220EFC4, en 0x0220F01E — la mise à jour du stylet vient de ranger les commandes du tour (r4 : le
   cockpit) ; le cockpit va les lire. */
void manette_crochet_commandes(Cpu *c) {
    const int obeit = g_obeit;
    g_obeit = 0;
    const uint32_t cockpit = ldr_word(c, c->r[4] + 0x34u);
    if (!cockpit) return;
    const uint32_t cmd = cockpit + 0x68u;
    if (!g_jeu.branchee || !g_pilote || g_stylet || !obeit || !g_en_vol) {
        /* la manette lâche le turbo ou le frein qu'elle tenait : le jeu les arrête au tour suivant */
        if (g_accel) {
            mem_write32(c, cmd + 0x50u, 0);
            if (!g_apercu_en_cours) g_accel = 0;
        }
        return;
    }
    /* Le turbo, le frein : ce que pose la double tape, et sa minuterie entretenue tant que le bouton est tenu —
       le jeu ne compare pas alors « en cours » au stylet. Relâché, la minuterie tombe à zéro et le jeu arrête
       l'Arwing comme au stylet levé. S'il l'a interrompu lui-même, il faut relâcher le bouton pour reprendre. */
    if (g_accel && !mem_read8(c, cmd + 0x43u) && !mem_read8(c, cmd + 0x44u) && !g_apercu_en_cours)
        g_consomme |= boutons(&g_jeu) & (MANETTE_OUEST | MANETTE_EST);
    const uint32_t b = boutons(&g_jeu) & ~g_masque & ~g_consomme;
    const int turbo = (b & MANETTE_OUEST) != 0, frein = !turbo && (b & MANETTE_EST) != 0;
    if (turbo || frein) {
        mem_write8(c, cmd + 0x43u, (uint8_t)turbo); mem_write8(c, cmd + 0x44u, (uint8_t)frein);
        mem_write8(c, cmd + 0x46u, 1);
        mem_write32(c, cmd + 0x50u, 5);
        if (!g_apercu_en_cours) g_accel = 1;
    } else if (g_accel) {
        mem_write32(c, cmd + 0x50u, 0);
        if (!g_apercu_en_cours) g_accel = 0;
    }
    /* les manœuvres : au vrai tour seulement */
    if (g_apercu_en_cours) return;
    const uint64_t t = hw_frames();
    for (int k = 0; k < N_DEMANDES; k++)
        if (g_demande[k] && t - g_demande[k] > DEMANDE_VALABLE) g_demande[k] = 0;
    if (g_demande[TONNEAU_G] || g_demande[TONNEAU_D]) {
        const int gauche = g_demande[TONNEAU_G] > g_demande[TONNEAU_D];
        mem_write8(c, cmd + 0x41u, (uint8_t)gauche);
        mem_write8(c, cmd + 0x42u, (uint8_t)!gauche);
        mem_write32(c, cmd + 0x48u, 10);
        g_demande[TONNEAU_G] = g_demande[TONNEAU_D] = 0;
    }
    if (g_demande[BOUCLE]) { manoeuvre(c, 0); g_demande[BOUCLE] = 0; g_demande[DEMITOUR] = 0; }
    else if (g_demande[DEMITOUR]) { manoeuvre(c, 1); g_demande[DEMITOUR] = 0; }
    if (g_demande[BOMBE]) { bombe(c); g_demande[BOMBE] = 0; }
}
