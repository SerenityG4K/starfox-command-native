/*
 * Sortie audio de l'hôte : une file de petits tampons confiés à waveOut.
 *
 * Le mixage produit une image de son à chaque image d'affichage ; on la remet
 * telle quelle au pilote. Quelques tampons d'avance suffisent à absorber
 * l'irrégularité du rendu sans allonger la latence au point de s'entendre.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "arm7_run.h"
#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>

/* La file est faite de petits tampons plutôt que d'un par image : la marge se compte en
 * tampons, la latence aussi, et des tampons courts donnent l'une sans l'autre. Un tampon de
 * 128 échantillons dure 3,9 ms ; seize d'avance font 62 ms de marge pour autant de latence,
 * là où des tampons d'une image entière en auraient coûté 267. */
#define CHUNK     128
#define BUFFERS   96
#define PREFILL   20          /* marge de départ ; elle s'ajuste à la machine (voir plus bas) */
#define PREFILL_MAX 48
#define MAX_FRAME 1024

static HWAVEOUT g_dev;
static WAVEHDR g_hdr[BUFFERS];
static int16_t g_buf[BUFFERS][CHUNK * 2];
static int g_next;
static int g_ready;
static int g_failed;
static int g_playing;         /* la lecture est lancée (la file a été amorcée) */
static uint64_t g_underruns;  /* combien de fois la file s'est vidée sous nos pieds */
/* Marge visée, en tampons. Une image de jeu qui demande plus de temps que les 16,7 ms qu'elle
   représente — un chargement, une décompression — laisse la carte sans rien à jouer. Plutôt
   que de choisir une marge au jugé, on part petit et on l'agrandit à chaque trou. */
static int g_prefill = PREFILL;

/* Chaque tampon est préparé une fois, à l'ouverture, pour sa longueur fixe d'un morceau : le défaire et le
   refaire à chaque envoi coûtait deux appels au système de plus par morceau — deux cent cinquante morceaux
   par seconde, et le fil du jeu y passait quatre pour cent de son temps. Un tampon joué revient marqué
   WHDR_DONE, toujours préparé : on le remplit et on le renvoie. */
static int push_chunk(const int16_t *samples, int n) {
    WAVEHDR *h = &g_hdr[g_next];
    if (!(h->dwFlags & WHDR_DONE) || n != CHUNK) return 0;
    memcpy(g_buf[g_next], samples, (size_t)n * 4);
    h->dwFlags &= ~WHDR_DONE;
    if (waveOutWrite(g_dev, h, sizeof *h) != MMSYSERR_NOERROR) { h->dwFlags |= WHDR_DONE; return 0; }
    g_next = (g_next + 1) % BUFFERS;
    return 1;
}

static void audio_open(void) {
    if (g_ready || g_failed) return;
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = 32768;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    if (waveOutOpen(&g_dev, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        rt_log("son : aucune sortie audio disponible");
        g_failed = 1;
        return;
    }
    for (int i = 0; i < BUFFERS; i++) {
        g_hdr[i].lpData = (LPSTR)g_buf[i];
        g_hdr[i].dwBufferLength = CHUNK * 4;
        g_hdr[i].dwFlags = 0;
        g_hdr[i].dwLoops = 0;
        if (waveOutPrepareHeader(g_dev, &g_hdr[i], sizeof g_hdr[i]) != MMSYSERR_NOERROR) {
            rt_log("son : tampon %d impossible à préparer", i);
            waveOutClose(g_dev);
            g_failed = 1;
            return;
        }
        g_hdr[i].dwFlags |= WHDR_DONE;               /* libre : rien à jouer encore */
    }
    g_ready = 1;
    g_playing = 1;
    /* On amorce la file avec du silence : démarrer la lecture sur un seul tampon ne laisse
       aucune marge, et la moindre image en retard creuse un trou. Attendre que le jeu remplisse
       la file lui-même prendrait des secondes, la cadence étant par construction la bonne. */
    for (int i = 0; i < g_prefill; i++) {
        static const int16_t silence[CHUNK * 2];
        push_chunk(silence, CHUNK);
    }
    rt_log("son : sortie audio ouverte (32 768 Hz, stéréo), %d ms d'avance",
           g_prefill * CHUNK * 1000 / 32768);
}

/* Combien de tampons attendent encore d'être joués. Le mixage s'en sert pour se caler : trop
   pleine, la file retarde le son ; vide, elle laisse un silence. */
int audio_queued(void) {
    if (!g_ready) return -1;          /* pas de sortie : rien à réguler */
    int n = 0;
    for (int i = 0; i < BUFFERS; i++)
        if (!(g_hdr[i].dwFlags & WHDR_DONE)) n++;
    return n;
}

/* Remet un tampon à la carte. Rend 0 si la file est pleine : le rendu ne doit jamais
   s'arrêter pour attendre la carte son. */
void audio_push(const int16_t *samples, int frames) {
    if (getenv("SFC_NO_AUDIO")) return;
    if (!g_ready) audio_open();          /* l'amorçage appelle push_chunk, pas audio_push */
    if (!g_ready || frames <= 0 || frames > MAX_FRAME) return;
    /* La file s'est vidée : la carte a manqué de données et a laissé un trou. On la ré-amorce
       avec du silence pour retrouver de la marge plutôt que de repartir au compte-gouttes. */
    if (audio_queued() == 0) {
        g_underruns++;
        if (g_prefill < PREFILL_MAX) g_prefill += 4;    /* cette machine demande plus de marge */
        static const int16_t silence[CHUNK * 2];
        for (int i = 0; i < g_prefill; i++) push_chunk(silence, CHUNK);
    }
    /* Le reste d'une image à l'autre : les images ne font pas un compte rond de tampons. */
    static int16_t pending[CHUNK * 2];
    static int held;
    int done = 0;
    while (done < frames) {
        int take = CHUNK - held;
        if (take > frames - done) take = frames - done;
        memcpy(pending + (size_t)held * 2, samples + (size_t)done * 2, (size_t)take * 4);
        held += take;
        done += take;
        if (held == CHUNK) {
            if (!push_chunk(pending, CHUNK)) { held = 0; return; }   /* file pleine : on saute */
            held = 0;
        }
    }
}

uint64_t audio_underruns(void) { return g_underruns; }
int audio_target(void) { return g_prefill; }
