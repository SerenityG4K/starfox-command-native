/*
 * Les seize canaux sonores de la console, et leur mixage.
 *
 * Chaque canal est décrit par quatre mots à partir de 0x04000400 :
 *
 *   +0  contrôle : volume (0-6), décalage de volume (8-9), panoramique (16-22),
 *                  répétition (27-28), format (29-30), marche (31)
 *   +4  adresse de la forme d'onde en mémoire
 *   +8  période (bits 0-15) : le pas d'horloge entre deux échantillons, et
 *       point de reprise en unités de mots (bits 16-31)
 *   +12 longueur, en mots, après le point de reprise
 *
 * Quatre formats : PCM 8 bits signé, PCM 16 bits signé, ADPCM (le codec IMA,
 * avec son en-tête de quatre octets), et pour les canaux 8-13 une onde carrée,
 * 14-15 un bruit. Le jeu n'emploie que les trois premiers.
 *
 * Le mixage se fait une fois par image, sur la durée exacte d'une image. Le
 * matériel produit un échantillon toutes les 1 024 horloges ; les périodes des
 * canaux sont exprimées dans cette même horloge, ce qui donne le pas de
 * lecture.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include "hw.h"
#include "arm7_run.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHANNELS        16
#define ARM7_CLOCK      33513982        /* Hz : la même horloge que l'ARM9, divisée par deux */
#define SND_RATE        32768           /* échantillons par seconde en sortie du matériel */

/* Une image de son dure 32768 / 59,8261 = 547,7 échantillons : ni 547 ni 548 ne tombent juste,
   on garde donc le reste d'une image sur l'autre. Sans cela le son dériverait de deux pour cent
   par rapport à l'image, et la file de sortie finirait par sauter des morceaux. */
/* Large : une image de jeu peut couvrir plus de temps virtuel qu'une image de console (le
   temps avance par bonds lors des attentes), et tronquer ici perdrait du son. */
#define FRAME_SAMPLES_MAX  4096
#define CYCLES_PER_FRAME   560190ull    /* la durée d'une image, dans l'horloge du matériel */

typedef struct {
    uint32_t ctrl, src, timer_loop, length;
    /* état de lecture */
    uint64_t pos;                /* position dans la forme d'onde, en 1/65536 d'échantillon */
    int32_t adpcm_value, adpcm_index;          /* état du décodeur ADPCM */
    uint32_t adpcm_word, adpcm_nibble;
    int32_t adpcm_loop_value, adpcm_loop_index;
    int playing;
} Channel;

static Channel g_ch[CHANNELS];
static int16_t g_mix[FRAME_SAMPLES_MAX * 2];

static uint64_t g_owed;        /* dette d'échantillons, en cycles de l'horloge du matériel */
static uint64_t g_late_samples;/* échantillons produits en plus pour tenir le temps réel */

uint64_t snd_late_samples(void) { return g_late_samples; }
static int g_filled;           /* échantillons déjà mixés pour l'image en cours */
static int g_rate_adjust;      /* correction de cadence, pour se caler sur la file de sortie */
static uint64_t g_dropped;     /* échantillons perdus faute de place : doit rester à zéro */
uint64_t snd_dropped(void) { return g_dropped; }
static FILE *g_wav;
static uint32_t g_wav_bytes;
static uint64_t g_wav_from;     /* SFC_WAV_FROM=n : n'écrire qu'à partir de cette image */
static int g_trace;

static inline uint32_t reg(uint32_t off) { uint32_t v; memcpy(&v, g_io7 + off, 4); return v; }

/* ------------------------------------------------------------------ ADPCM (codec IMA) */
static const int16_t ADPCM_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
    449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};
static const int8_t ADPCM_INDEX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static void adpcm_step(Channel *ch, uint32_t nibble) {
    int step = ADPCM_STEP[ch->adpcm_index];
    int diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    ch->adpcm_value += (nibble & 8) ? -diff : diff;
    if (ch->adpcm_value < -0x7FFF) ch->adpcm_value = -0x7FFF;
    if (ch->adpcm_value > 0x7FFF) ch->adpcm_value = 0x7FFF;
    ch->adpcm_index += ADPCM_INDEX[nibble & 7];
    if (ch->adpcm_index < 0) ch->adpcm_index = 0;
    if (ch->adpcm_index > 88) ch->adpcm_index = 88;
}

/* Deux leviers pour isoler ce qu'on entend quand le son ne va pas :
 *   SFC_SND_ONLY=n   ne mixe que le canal n (0-15) ;
 *   SFC_SND_FLAT=1   ignore volumes et panoramique, tout à plein au centre.
 * Si le son devient propre à plat, le défaut est dans les volumes — la table de décibels du
 * BIOS est reconstituée, faute de l'avoir. Sinon il est dans la lecture des formes d'onde. */
static int snd_only(void) {
    static int v = -2;
    if (v == -2) { const char *e = getenv("SFC_SND_ONLY"); v = e ? atoi(e) : -1; }
    return v;
}
static int snd_flat(void) {
    static int v = -1;
    if (v < 0) v = getenv("SFC_SND_FLAT") != NULL;
    return v;
}

/* ------------------------------------------------------------------ lecture d'un canal */
static void channel_start(Cpu *c, int i) {
    Channel *ch = &g_ch[i];
    ch->pos = 0;
    ch->playing = 1;
    if (((ch->ctrl >> 29) & 3u) == 2) {          /* ADPCM : quatre octets d'en-tête */
        uint32_t head = mem_read32(c, ch->src);
        ch->adpcm_value = (int16_t)(head & 0xFFFF);
        ch->adpcm_index = (int32_t)((head >> 16) & 0x7F);
        if (ch->adpcm_index > 88) ch->adpcm_index = 88;
        ch->adpcm_word = 0;
        ch->adpcm_nibble = 0;                    /* le premier échantillon est celui de l'en-tête */
        ch->adpcm_loop_value = ch->adpcm_value;
        ch->adpcm_loop_index = ch->adpcm_index;
    }
}

/* L'échantillon brut du canal à sa position courante, sans volume. */
static int32_t channel_raw(Cpu *c, int i) {
    Channel *ch = &g_ch[i];
    if (!ch->playing) return 0;
    uint32_t fmt = (ch->ctrl >> 29) & 3u;
    uint32_t loop = (ch->ctrl >> 27) & 3u;
    uint32_t loop_words = (ch->timer_loop >> 16) & 0xFFFFu;
    uint32_t total_words = loop_words + (ch->length & 0x3FFFFFu);
    uint64_t index = ch->pos >> 16;              /* en échantillons depuis le début */
    int32_t out = 0;
    if (fmt == 0) {                              /* PCM 8 bits */
        if (index >= (uint64_t)total_words * 4) {
            if (loop == 1) { ch->pos = (uint64_t)loop_words * 4 << 16; index = ch->pos >> 16; }
            else { ch->playing = 0; return 0; }
        }
        out = (int8_t)mem_read8(c, ch->src + (uint32_t)index) << 8;
    } else if (fmt == 1) {                       /* PCM 16 bits */
        if (index >= (uint64_t)total_words * 2) {
            if (loop == 1) { ch->pos = (uint64_t)loop_words * 2 << 16; index = ch->pos >> 16; }
            else { ch->playing = 0; return 0; }
        }
        out = (int16_t)mem_read16(c, ch->src + (uint32_t)index * 2);
    } else if (fmt == 2) {                       /* ADPCM : décodage séquentiel */
        uint64_t nib_total = (uint64_t)total_words * 8 - 8;   /* moins l'en-tête */
        if (index >= nib_total) {
            if (loop == 1) {
                /* Le point de reprise est donné en mots depuis le début de la donnée, en-tête
                   compris : en nibbles utiles, c'est huit de moins. */
                uint64_t back = loop_words ? (uint64_t)(loop_words - 1) * 8 : 0;
                ch->pos = back << 16;
                ch->adpcm_value = ch->adpcm_loop_value;
                ch->adpcm_index = ch->adpcm_loop_index;
                ch->adpcm_nibble = (uint32_t)back;
                index = back;
            } else { ch->playing = 0; return 0; }
        }
        /* On décode jusqu'à la position voulue : le codec est à état, on ne peut pas sauter.
           L'en-tête ne donne que l'état de départ ; c'est le nibble n qui produit
           l'échantillon n. Commencer au nibble huit, comme on le faisait, sautait le premier
           mot de données et faisait dériver le décodeur pour tout le reste du son. */
        while (ch->adpcm_nibble <= index) {
            if ((ch->adpcm_nibble & 7u) == 0)
                ch->adpcm_word = mem_read32(c, ch->src + 4 + (uint32_t)(ch->adpcm_nibble / 8) * 4);
            adpcm_step(ch, (ch->adpcm_word >> ((ch->adpcm_nibble & 7u) * 4)) & 0xFu);
            ch->adpcm_nibble++;
            if (loop_words && ch->adpcm_nibble == (loop_words - 1) * 8) {
                ch->adpcm_loop_value = ch->adpcm_value;
                ch->adpcm_loop_index = ch->adpcm_index;
            }
        }
        out = ch->adpcm_value;
    }
    return out;
}

/* L'échantillon du canal, volume compris. Le matériel ne lisse rien entre deux points de la
 * forme d'onde : on ne lisse pas non plus. */
static int32_t channel_sample(Cpu *c, int i) {
    Channel *ch = &g_ch[i];
    int32_t out = channel_raw(c, i);
    if (!ch->playing) return 0;
    /* Volume : sept bits, donc une fraction de 128, puis un diviseur grossier de 1, 2, 4 ou 16. */
    if (snd_flat()) return out;
    uint32_t vol = ch->ctrl & 0x7Fu;
    static const int divider[4] = {0, 1, 2, 4};
    return (int32_t)(((int64_t)out * (int32_t)vol) >> (7 + divider[(ch->ctrl >> 8) & 3u]));
}

static inline int16_t clip16(int32_t v) {
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

/* Pas de lecture : la période est un compteur qui déborde à 0x10000 au rythme de l'horloge. */
static uint64_t channel_step(const Channel *ch) {
    uint32_t period = ch->timer_loop & 0xFFFFu;
    if (period == 0) return 0;
    /* Fréquence du canal = horloge / 2 / (0x10000 - période). Le pas est en 1/65536
       d'échantillon : arrondi au 1/256 près, l'erreur de hauteur atteignait un pour cent. */
    uint64_t num = (uint64_t)ARM7_CLOCK << 16;
    return num / (2ull * (0x10000ull - period) * (uint64_t)SND_RATE);
}

/* ------------------------------------------------------------------ registres */
uint32_t snd_read(Cpu *c, uint32_t off) {
    (void)c;
    if (off < 0x500) {
        int i = (int)((off - 0x400) / 16);
        uint32_t which = (off - 0x400) % 16;
        if (which == 0) {
            uint32_t v = g_ch[i].ctrl;
            if (!g_ch[i].playing) v &= ~0x80000000u;
            return v;
        }
        return which == 4 ? g_ch[i].src : which == 8 ? g_ch[i].timer_loop : g_ch[i].length;
    }
    return reg(off & ~3u);
}

void snd_write(Cpu *c, uint32_t off, uint32_t v, uint32_t size) {
    (void)size;
    uint32_t aligned = off & ~3u;
    memcpy(g_io7 + aligned, &v, 4);
    if (aligned >= 0x500) return;                /* SOUNDCNT, SOUNDBIAS, capture : mémorisés */
    int i = (int)((aligned - 0x400) / 16);
    uint32_t which = (aligned - 0x400) % 16;
    Channel *ch = &g_ch[i];
    switch (which) {
    case 0: {
        uint32_t was = ch->ctrl & 0x80000000u;
        ch->ctrl = v;
        if ((v & 0x80000000u) && !was) channel_start(c, i);
        else if (!(v & 0x80000000u)) ch->playing = 0;
        if (g_trace && (v & 0x80000000u) && !was)
            rt_log("son : canal %2d démarre, source 0x%08X, période %u, format %u, volume %u",
                   i, ch->src, ch->timer_loop & 0xFFFFu, (v >> 29) & 3u, v & 0x7Fu);
        return;
    }
    case 4:  ch->src = v; return;
    case 8:  ch->timer_loop = v; return;
    default: ch->length = v; return;
    }
}

/* ------------------------------------------------------------------ mixage */
void snd_init(void) {
    g_trace = getenv("SFC_TRACE_SOUND") != NULL;
    memset(g_ch, 0, sizeof g_ch);
    const char *path = getenv("SFC_WAV");
    if (!path) return;
    const char *from = getenv("SFC_WAV_FROM");
    g_wav_from = from ? strtoull(from, NULL, 10) : 0;
    g_wav = fopen(path, "wb");
    if (!g_wav) { rt_log("son : impossible d'écrire %s", path); return; }
    /* En-tête WAVE : les tailles seront reprises à la fermeture. */
    uint8_t head[44] = {0};
    memcpy(head, "RIFF", 4); memcpy(head + 8, "WAVEfmt ", 8);
    uint32_t sz = 16; memcpy(head + 16, &sz, 4);
    uint16_t fmt = 1, chn = 2; memcpy(head + 20, &fmt, 2); memcpy(head + 22, &chn, 2);
    uint32_t rate = SND_RATE; memcpy(head + 24, &rate, 4);
    uint32_t bps = SND_RATE * 4; memcpy(head + 28, &bps, 4);
    uint16_t align = 4, bits = 16; memcpy(head + 32, &align, 2); memcpy(head + 34, &bits, 2);
    memcpy(head + 36, "data", 4);
    fwrite(head, 1, 44, g_wav);
    rt_log("son : enregistrement dans %s", path);
}

static void wav_close(void) {
    if (!g_wav) return;
    uint32_t riff = 36 + g_wav_bytes;
    fseek(g_wav, 4, SEEK_SET); fwrite(&riff, 4, 1, g_wav);
    fseek(g_wav, 40, SEEK_SET); fwrite(&g_wav_bytes, 4, 1, g_wav);
    fclose(g_wav);
    g_wav = NULL;
}

/* Mixe `n` échantillons à partir de l'état courant des canaux. Appelé souvent, par petits
 * paquets : le pilote son modifie ses canaux plusieurs fois par image, et produire seize
 * millisecondes d'un coup avec un état figé s'entend — notes qui sautent, craquements. */
static void mix_into(Cpu *c, int16_t *out, int n) {
    uint32_t cnt = reg(0x500);
    int master = snd_flat() ? 127 : (int)(cnt & 0x7Fu);
    int only = snd_only();
    memset(out, 0, (size_t)n * 4);
    if (!(cnt & 0x8000u)) return;
    for (int i = 0; i < CHANNELS; i++) {
        Channel *ch = &g_ch[i];
        if (only >= 0 && i != only) continue;
        if (!ch->playing || !(ch->ctrl & 0x80000000u)) continue;
        uint64_t step = channel_step(ch);
        if (!step) continue;
        uint32_t pan = snd_flat() ? 64u : (ch->ctrl >> 16) & 0x7Fu;
        for (int s = 0; s < n; s++) {
            int32_t v = channel_sample(c, i);
            if (!ch->playing) break;
            ch->pos += step;
            int32_t l = (int32_t)(((int64_t)v * (int32_t)(127 - pan) * master) >> 14);
            int32_t r = (int32_t)(((int64_t)v * (int32_t)pan * master) >> 14);
            out[s * 2] = clip16(out[s * 2] + l);
            out[s * 2 + 1] = clip16(out[s * 2 + 1] + r);
        }
    }
}

/* Le temps passe : on produit ce qu'il doit d'échantillons, en suivant l'état des canaux tel
 * qu'il est maintenant. Le reste fractionnaire est reporté. */
/* SFC_PROFILE=1 : le temps passé à mixer, qui tombe dans le compte du coprocesseur puisqu'il
   est appelé au fil de son balayage. */
static uint64_t g_mix_ns;
uint64_t snd_mix_ns(void) { uint64_t v = g_mix_ns; g_mix_ns = 0; return v; }

void snd_mix_cycles(Cpu *c, uint64_t cycles) {
    g_owed += cycles * (uint64_t)(SND_RATE + g_rate_adjust);
    int n = (int)(g_owed / (uint64_t)ARM7_CLOCK);
    if (n <= 0) return;
    g_owed -= (uint64_t)n * (uint64_t)ARM7_CLOCK;
    if (g_filled + n > FRAME_SAMPLES_MAX) {
        g_dropped += g_filled + n - FRAME_SAMPLES_MAX;
        n = FRAME_SAMPLES_MAX - g_filled;
    }
    if (n <= 0) return;
#ifdef _WIN32
    static int prof = -1;
    if (prof < 0) prof = getenv("SFC_PROFILE") != NULL;
    LARGE_INTEGER t0;
    if (prof) QueryPerformanceCounter(&t0);
#endif
    mix_into(c, g_mix + (size_t)g_filled * 2, n);
    g_filled += n;
#ifdef _WIN32
    if (prof) {
        static LARGE_INTEGER freq;
        LARGE_INTEGER t1;
        if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t1);
        g_mix_ns += (uint64_t)((t1.QuadPart - t0.QuadPart) * 1000000000 / freq.QuadPart);
    }
#endif
}

/* Le temps réel écoulé depuis l'appel précédent, en microsecondes. */
static uint64_t elapsed_us(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq, last;
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    if (!last.QuadPart) { last = now; return 0; }
    uint64_t dt = (uint64_t)((now.QuadPart - last.QuadPart) * 1000000 / freq.QuadPart);
    last = now;
    return dt;
#else
    return 0;
#endif
}

/* Sur la console, le son ne dépend pas de la cadence du jeu : seize canaux lisent les formes
 * d'onde en mémoire par accès direct, à une cadence fixée par des minuteurs matériels, et le
 * pilote sonore tourne sur un second processeur. Quand l'image peine, la musique continue à
 * l'heure.
 *
 * Ici les deux processeurs partagent un fil d'exécution et une horloge simulée : si la machine
 * prend du retard, le son le prend aussi, la file de sortie se vide et l'on entend des trous.
 * On complète donc chaque image jusqu'à ce que le temps réel demande, en laissant les canaux
 * avancer là où ils en sont — ce que fait le matériel quand le pilote n'a rien changé.
 *
 * Rien de tout cela pendant un rejeu accéléré ou un banc d'essai : ils tournent plus vite que
 * le temps réel et doivent rester reproductibles. SFC_SND_SIMULE=1 revient à l'ancien
 * comportement, pour comparer. */
static int snd_realtime(void) {
    static int v = -1;
    if (v < 0)
        v = !getenv("SFC_SND_SIMULE") && !getenv("SFC_FAST")
            && (!getenv("SFC_MAX_FRAMES") || getenv("SFC_CADENCE"))
            && !getenv("SFC_NO_AUDIO") && !getenv("SFC_HASH") && !getenv("SFC_NO_VIDEO");
    return v;
}

static void fill_to_realtime(Cpu *c) {
    uint64_t us = elapsed_us();
    if (!snd_realtime() || !us) return;
    if (us > 200000) return;                 /* une pause de la fenêtre : on ne rattrape pas */
    /* On ne complète que lorsque la file de sortie se vide vraiment, c'est-à-dire quand la
       simulation est en retard sur le temps réel. Le faire au seul vu de l'horloge produisait
       un surplus permanent — un pour cent suffit à saturer la file en quelques minutes — et la
       carte son jetait alors les tampons en trop : ce sont eux qu'on entendait craquer. */
    int queued = audio_queued();
    if (queued < 0 || queued >= audio_target()) return;
    int want = (int)((us * (uint64_t)SND_RATE) / 1000000ull);
    if (want <= g_filled) return;
    int n = want - g_filled;
    if (n > FRAME_SAMPLES_MAX - g_filled) n = FRAME_SAMPLES_MAX - g_filled;
    if (n <= 0) return;
    mix_into(c, g_mix + (size_t)g_filled * 2, n);
    g_filled += n;
    g_late_samples += n;
}

/* Une fois par image : ce qui a été mixé part à la sortie, et on se recale sur le remplissage
 * de sa file — les deux horloges ne sont pas la même, et sans cela elle déborde ou se vide. */
void snd_mix_frame(Cpu *c) {
    fill_to_realtime(c);
    if (g_filled <= 0) return;
    if (g_wav && hw_frames() >= g_wav_from) {
        fwrite(g_mix, 1, (size_t)g_filled * 4, g_wav);
        g_wav_bytes += (uint32_t)g_filled * 4;
        static int registered;
        if (!registered) { registered = 1; atexit(wav_close); }
    }
    audio_push(g_mix, g_filled);
    g_filled = 0;
    /* Le recalage change la cadence du mixage d'un pour cent, donc l'instant où un canal se
       tait — et le jeu lit l'état des canaux. Un banc d'essai ne peut pas dépendre de la file
       d'une carte son : sous charge, la même partie rejouée changeait d'empreinte une image
       sur mille, et la dichotomie accusait des fonctions jamais appelées. */
    int queued = snd_realtime() ? audio_queued() : -1;
    if (queued < 0) g_rate_adjust = 0;                       /* sortie fermée, ou banc : cadence exacte */
    else if (queued >= audio_target() + 8) g_rate_adjust = -(SND_RATE / 100);
    else if (queued <= audio_target() - 8) g_rate_adjust = SND_RATE / 100;
    else g_rate_adjust = 0;
}
