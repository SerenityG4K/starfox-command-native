/*
 * Dormir une fraction de milliseconde.
 *
 * Sous Windows un sommeil ordinaire dure jusqu'au prochain tic du minuteur du système : quinze
 * millisecondes par défaut, et Windows 11 y ramène toute fenêtre qu'il juge masquée, quoi qu'on ait
 * demandé par timeBeginPeriod. Le cadencement du jeu (16,7 ms) en sortait irrégulier ; l'affichage
 * libre, qui vise quatre millisecondes par image, en sautait trois et les rendait d'un coup. Les
 * minuteurs à haute résolution de Windows 10 (1803) n'ont pas ce défaut.
 *
 * Sous licence MIT.
 */
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

void attente_dormir_ns(int64_t ns) {
    static __thread HANDLE minuteur;      /* un par fil : le jeu et l'affichage dorment chacun de leur côté */
    static __thread int cherche;
    if (ns <= 0) return;
    if (!cherche) {
        cherche = 1;
        minuteur = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
    if (minuteur) {
        LARGE_INTEGER duree;
        duree.QuadPart = -(ns / 100);      /* négatif : relatif, en centaines de nanosecondes */
        if (duree.QuadPart < 0 && SetWaitableTimer(minuteur, &duree, 0, NULL, NULL, FALSE)) {
            WaitForSingleObject(minuteur, INFINITE);
            return;
        }
    }
    Sleep((DWORD)(ns / 1000000));
}

#else
#include <time.h>

void attente_dormir_ns(int64_t ns) {
    if (ns <= 0) return;
    struct timespec t = {(time_t)(ns / 1000000000), (long)(ns % 1000000000)};
    nanosleep(&t, NULL);
}
#endif
