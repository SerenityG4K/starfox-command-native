/*
 * SFC_ECHANTILLONS=fichier : où passe le temps de la machine hôte, par échantillonnage.
 *
 * Un fil suspend celui du jeu toutes les millisecondes, relève son adresse d'exécution, et le relâche.
 * À la sortie, l'histogramme des adresses va dans le fichier ; tools/echantillons.py les rattache aux
 * fonctions de l'exécutable (nm). Le profil par arêtes (SFC_PROFILE_ARM9) compte le travail simulé ;
 * celui-ci compte le temps réel, simulation du matériel comprise.
 *
 * Sous licence MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#define CASES (1u << 20)
static uint64_t g_adresses[CASES];
static uint32_t g_comptes[CASES];
static HANDLE volatile g_fil_jeu;

static int suivre_affichage(void) {
    const char *f = getenv("SFC_ECHANTILLONS_FIL");
    return f && !strcmp(f, "affichage");
}
static volatile LONG g_arret;
static const char *g_sortie;
static uint64_t g_debut_image;
static HMODULE g_base;
static size_t g_taille;

uint64_t hw_frames(void);

static void noter(uint64_t rip) {
    uint32_t h = (uint32_t)((rip * 0x9E3779B97F4A7C15ull) >> 44) & (CASES - 1);
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t k = (h + i) & (CASES - 1);
        if (g_adresses[k] == rip) { g_comptes[k]++; return; }
        if (!g_adresses[k]) { g_adresses[k] = rip; g_comptes[k] = 1; return; }
    }
}

static DWORD WINAPI echantillonner(void *p) {
    (void)p;
    timeBeginPeriod(1);
    while (!g_arret) {
        Sleep(1);
        if (!g_fil_jeu || hw_frames() < g_debut_image) continue;
        if (SuspendThread(g_fil_jeu) == (DWORD)-1) continue;
        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(g_fil_jeu, &ctx)) {
            noter(ctx.Rip);
            /* hors de l'exécutable (bibliothèque C, système, pilote) : on remonte la pile jusqu'au premier
               appelant qui en fait partie, et on le note à part — c'est lui qui paie */
            uintptr_t lo = (uintptr_t)g_base, hi = lo + g_taille;
            for (int n = 0; n < 64 && !(ctx.Rip >= lo && ctx.Rip < hi); n++) {
                DWORD64 ib = 0;
                PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &ib, NULL);
                if (fe) {
                    PVOID hd; DWORD64 ef;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, ib, ctx.Rip, fe, &ctx, &hd, &ef, NULL);
                } else {
                    ctx.Rip = *(DWORD64 *)ctx.Rsp;
                    ctx.Rsp += 8;
                }
                if (!ctx.Rip) break;
            }
            if (ctx.Rip >= lo && ctx.Rip < hi) noter(ctx.Rip | (1ull << 63));   /* bit haut : « appelant de » */
        }
        ResumeThread(g_fil_jeu);
    }
    return 0;
}

static void ecrire(void) {
    g_arret = 1;
    FILE *f = fopen(g_sortie, "w");
    if (!f) return;
    HMODULE base = GetModuleHandleW(NULL);
    fprintf(f, "base %llx\n", (unsigned long long)(uintptr_t)base);
    for (uint32_t k = 0; k < CASES; k++) {
        if (!g_adresses[k]) continue;
        if (g_adresses[k] >> 63) {
            fprintf(f, "%llx %u @appelant\n", (unsigned long long)(g_adresses[k] & ~(1ull << 63)), g_comptes[k]);
            continue;
        }
        HMODULE m = NULL;
        char nom[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)(uintptr_t)g_adresses[k], &m) && m != base) {
            GetModuleFileNameA(m, nom, sizeof nom);
            char *b = strrchr(nom, '\\');
            fprintf(f, "%llx %u %s\n", (unsigned long long)g_adresses[k], g_comptes[k], b ? b + 1 : nom);
        } else fprintf(f, "%llx %u\n", (unsigned long long)g_adresses[k], g_comptes[k]);
    }
    fclose(f);
}

/* appelé une fois, depuis le fil du jeu */
void echantillonneur_demarrer(void) {
    g_sortie = getenv("SFC_ECHANTILLONS");
    if (!g_sortie) return;
    const char *d = getenv("SFC_ECHANTILLONS_DEPUIS");
    g_debut_image = d ? strtoull(d, NULL, 10) : 0;
    g_base = GetModuleHandleW(NULL);
    {   /* la taille de l'image : son en-tête PE */
        const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)g_base;
        const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)g_base + dos->e_lfanew);
        g_taille = nt->OptionalHeader.SizeOfImage;
    }
    HANDLE h;
    if (!suivre_affichage()
        && DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h,
                           THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0))
        g_fil_jeu = h;
    atexit(ecrire);
    CreateThread(NULL, 0, echantillonner, NULL, 0, NULL);
}

/* ------------------------------------------------------------------ les fils nommés
 *
 * Chaque fil du portage se nomme en démarrant (rt_fil_nommer) : le nom se voit dans un débogueur, et
 * rt_fils_rapport dit, sous SFC_PROFILE, la part d'un cœur que chacun a prise depuis le relevé précédent —
 * plus celle des fils que le portage ne crée pas lui-même (pilote graphique, son du système). */
#define FILS_MAX 16
static struct { HANDLE h; const char *nom; uint64_t avant; DWORD id; } g_fils[FILS_MAX];
static volatile LONG g_nb_fils;

static uint64_t temps_fil(HANDLE h) {
    FILETIME c, e, k, u;
    if (!GetThreadTimes(h, &c, &e, &k, &u)) return 0;
    return (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime);
}

void rt_fil_nommer(const char *nom) {
    LONG i = InterlockedIncrement(&g_nb_fils) - 1;
    if (i >= FILS_MAX) return;
    HANDLE h;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h,
                         THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0)) return;
    g_fils[i].nom = nom;
    g_fils[i].avant = temps_fil(h);
    g_fils[i].id = GetCurrentThreadId();
    g_fils[i].h = h;
    /* le nom pour les débogueurs : SetThreadDescription n'existe que depuis Windows 10 (1607) */
    typedef HRESULT (WINAPI *Decrire)(HANDLE, PCWSTR);
    static Decrire decrire;
    static int cherche;
    if (!cherche) { cherche = 1; decrire = (Decrire)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"); }
    if (decrire) {
        wchar_t w[32];
        int n = MultiByteToWideChar(CP_UTF8, 0, nom, -1, w, 32);
        if (n > 0) decrire(GetCurrentThread(), w);
    }
}

/* le nom du fil qui appelle, s'il s'est nommé */
const char *rt_fil_nom(void) {
    DWORD id = GetCurrentThreadId();
    for (int i = 0; i < FILS_MAX && i < g_nb_fils; i++)
        if (g_fils[i].h && g_fils[i].id == id) return g_fils[i].nom;
    return "sans nom";
}

void rt_log(const char *fmt, ...);
void rt_fils_rapport(void) {
    static uint64_t proc_avant;
    static LARGE_INTEGER t_avant;
    LARGE_INTEGER t, freq;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&freq);
    FILETIME c, e, k, u;
    uint64_t proc = 0;
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
        proc = (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime);
    if (!t_avant.QuadPart) {
        t_avant = t; proc_avant = proc;
        for (int i = 0; i < FILS_MAX && i < g_nb_fils; i++) if (g_fils[i].h) g_fils[i].avant = temps_fil(g_fils[i].h);
        return;
    }
    double mur = (double)(t.QuadPart - t_avant.QuadPart) / (double)freq.QuadPart * 1e7;   /* en centaines de ns */
    if (mur <= 0) return;
    char ligne[800];
    int n = 0;
    uint64_t somme = 0;
    for (int i = 0; i < FILS_MAX && i < g_nb_fils; i++) {
        if (!g_fils[i].h) continue;
        uint64_t v = temps_fil(g_fils[i].h), d = v - g_fils[i].avant;
        g_fils[i].avant = v;
        somme += d;
        n += snprintf(ligne + n, sizeof ligne - (size_t)n, "%s %.1f %%, ", g_fils[i].nom, 100.0 * (double)d / mur);
    }
    uint64_t dp = proc - proc_avant;
    rt_log("fils : %sautres %.1f %% — en tout %.1f %% d'un cœur", ligne,
           100.0 * (double)(dp > somme ? dp - somme : 0) / mur, 100.0 * (double)dp / mur);
    t_avant = t; proc_avant = proc;
}

/* SFC_ECHANTILLONS_FIL=affichage : suivre le fil de la fenêtre et d'OpenGL (affichage libre) au lieu de
   celui du jeu. Appelé par ce fil à son démarrage — qui peut précéder celui de l'échantillonneur. */
void echantillonneur_fil_affichage(void) {
    if (!getenv("SFC_ECHANTILLONS") || !suivre_affichage()) return;
    HANDLE h;
    if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h,
                        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0))
        g_fil_jeu = h;
}
