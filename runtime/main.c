/*
 * Amorçage : charge la ROM, place l'ARM9 tel que le firmware le ferait, et
 * lance le crt0 recompilé. Tout le reste (décompression, ITCM/DTCM, BSS,
 * initialisation du SDK, NitroMain) est le code du jeu.
 *
 * Usage : sfc.exe rom.nds — la cartouche du joueur, que le portage ne fournit pas. Sans argument, le jeu
 * se sert dans un dossier de données (data/, ou SFC_DATA), découpé dans cette même cartouche.
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include "arm7.h"
#include "arm7_run.h"
#include "threads.h"
#include "gx_geom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static uint32_t rd32(const uint8_t *p, size_t off) { uint32_t v; memcpy(&v, p + off, 4); return v; }

/* CRC-16 du BIOS (polynôme 0xA001, valeur initiale 0xFFFF), utilisé par le firmware
   pour valider le bloc des réglages utilisateur. */
uint16_t bios_crc16(uint16_t crc, const uint8_t *p, size_t n);
static uint16_t crc16(const uint8_t *p, size_t n) { return bios_crc16(0xFFFFu, p, n); }

/* Le firmware copie ses réglages utilisateur en 0x027FFC80 : langue, pseudo,
   calibration de l'écran tactile… Le jeu y lit sa langue. */
static void write_user_settings(Cpu *c) {
    uint8_t s[0x74];
    memset(s, 0, sizeof s);
    s[0x00] = 5;                                   /* version */
    s[0x02] = 3;                                   /* couleur favorite */
    s[0x03] = 1; s[0x04] = 1;                      /* anniversaire : 1er janvier */
    const char *nick = "Fox";
    for (int i = 0; nick[i]; i++) { s[0x06 + 2 * i] = (uint8_t)nick[i]; s[0x07 + 2 * i] = 0; }
    s[0x1A] = 3;                                   /* longueur du pseudo */
    s[0x50] = 0;                                   /* longueur du message */
    /* calibration tactile : deux points (ADC -> pixel) */
    uint16_t adc_x1 = 0x0100, adc_y1 = 0x0100, adc_x2 = 0x0F00, adc_y2 = 0x0B80;
    memcpy(s + 0x58, &adc_x1, 2); memcpy(s + 0x5A, &adc_y1, 2); s[0x5C] = 16; s[0x5D] = 16;
    memcpy(s + 0x5E, &adc_x2, 2); memcpy(s + 0x60, &adc_y2, 2); s[0x62] = 240; s[0x63] = 176;
    uint16_t flags = 2 /* français */ | (3 << 4) /* rétroéclairage */ | (0 << 6);
    memcpy(s + 0x64, &flags, 2);
    uint16_t year = 6;                             /* 2006 */
    memcpy(s + 0x66, &year, 2);
    uint16_t crc = crc16(s, 0x70);
    memcpy(s + 0x72, &crc, 2);
    for (size_t i = 0; i < sizeof s; i++) mem_write8(c, 0x027FFC80u + (uint32_t)i, s[i]);
}

extern void rt_set_assert_info(uint32_t addr);
extern bool video_init(void);
extern OverlayInfo *rt_load_overlay_table(const uint8_t *rom, size_t size, size_t *count);

/* Le dossier de données, cherché là où il a des chances d'être : là où on le désigne, dans
   le dossier courant, puis à côté de l'exécutable — c'est ainsi qu'on le trouve quand le
   jeu est lancé depuis un raccourci. */
static int ouvrir_donnees(const char *exe) {
    const char *env = getenv("SFC_DATA");
    if (env && *env) return data_init(env);
    if (data_init("data")) return 1;
    char base[512];
    snprintf(base, sizeof base, "%s", exe);
    char *fin = strrchr(base, '\\');
    char *autre = strrchr(base, '/');
    if (!fin || (autre && autre > fin)) fin = autre;
    if (!fin) return 0;
    snprintf(fin, sizeof base - (size_t)(fin - base), "/data");
    return data_init(base);
}

static int finit_par_nds(const char *s) {
    size_t n = strlen(s);
    if (n < 4) return 0;
    const char *e = s + n - 4;
    return e[0] == '.' && (e[1] | 32) == 'n' && (e[2] | 32) == 'd' && (e[3] | 32) == 's';
}

/* Les portables à deux cartes graphiques — une Intel intégrée, une NVIDIA ou une AMD dédiée — lancent un
   programme OpenGL sur la carte intégrée, à moins qu'il ne demande l'autre. Ces deux symboles exportés sont la
   demande que lisent les pilotes NVIDIA (Optimus) et AMD (PowerXpress) ; sur une machine à une seule carte, ils
   ne font rien. */
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

int main(int argc, char **argv) {
    /* SFC_JOURNAL=fichier : tout ce que le jeu dit — et surtout pourquoi il s'arrête, s'il s'arrête — va
       dans ce fichier plutôt que dans la console. Le lanceur le demande : sans lui, le jeu lancé d'un
       double clic s'ouvrait une console à part, et le message d'une erreur fatale s'y effaçait avec elle. */
    {
        const char *j = getenv("SFC_JOURNAL");
        if (j && *j && freopen(j, "w", stderr)) setvbuf(stderr, NULL, _IOLBF, 4096);
    }
    /* La cartouche du joueur, ou à défaut le dossier de données qu'il en a tiré (tools/build_data.py) pour
       en modifier les fichiers : le jeu y lit la même chose, à l'octet près. */
    const char *chemin_rom = (argc > 1 && finit_par_nds(argv[1])) ? argv[1] : NULL;
    int arg_veille = chemin_rom ? 2 : 1;
    size_t rom_size = 0;
    uint8_t *rom = NULL;
    if (chemin_rom) {
        rom = read_file(chemin_rom, &rom_size);
        if (!rom) { fprintf(stderr, "impossible de lire %s\n", chemin_rom); return 1; }
        /* Une copie réduite à ses données utiles (« trimmed ») s'arrête avant la fin du support : la
           cartouche d'origine, elle, porte des zéros jusqu'à sa capacité (octet 0x14 de l'en-tête). On les
           rend, pour que le jeu lise la même chose de l'une ou de l'autre. */
        if (rom_size >= 0x200 && rom[0x14] <= 12) {
            const size_t capacite = (size_t)0x20000 << rom[0x14];
            if (rom_size < capacite) {
                uint8_t *pleine = realloc(rom, capacite);
                if (pleine) {
                    memset(pleine + rom_size, 0, capacite - rom_size);
                    rom = pleine;
                    rom_size = capacite;
                }
            }
        }
    } else if (ouvrir_donnees(argv[0])) {
        rom = data_preambule(&rom_size);
        if (!rom) { fprintf(stderr, "dossier de données illisible\n"); return 1; }
    } else {
        fprintf(stderr, "usage : %s rom.nds [minutes]\n"
                        "la cartouche de Star Fox Command (Europe), la vôtre : le portage ne la fournit pas\n"
                        "(ou un dossier de données tiré d'elle : data/, ou SFC_DATA)\n", argv[0]);
        return 1;
    }
    /* Le code a été recompilé depuis la version européenne : une autre version ne place pas le sien aux
       mêmes adresses, et planterait sans dire pourquoi. */
    if (rom_size < 0x200 || memcmp(rom + 0x0C, "ASFP", 4) != 0) {
        fprintf(stderr, "cette cartouche n'est pas Star Fox Command (Europe) : code de jeu %.4s, attendu ASFP\n",
                rom_size >= 0x10 ? (const char *)rom + 0x0C : "????");
        return 1;
    }

    static Cpu cpu;
    Cpu *c = &cpu;
    mem_init();
    rt_init(c);
    hw_init(c);
    if (!data_disponible()) card_init(rom, rom_size);
    /* SFC_ARM7=1 : le binaire du coprocesseur, traduit, prend la place de la simulation de son
       rôle — laquelle, sinon, déclarerait à l'ARM9 des services que personne ne rendrait. */
    arm7_boot(rom, rom_size, c);
    if (!arm7_enabled()) arm7_init(c);
    threads_init(c);
    gx_geom_init();
    { extern void fenetre_icone(const uint8_t *, size_t); fenetre_icone(rom, rom_size); }   /* l'emblème, pour la fenêtre */
    if (!getenv("SFC_NO_VIDEO") && !video_init()) rt_log("vidéo : désactivée");

    /* L'ARM9 tel que le firmware le charge : image brute (compressée) à son adresse. */
    uint32_t arm9_off = rd32(rom, 0x20), arm9_entry = rd32(rom, 0x24), arm9_addr = rd32(rom, 0x28), arm9_size = rd32(rom, 0x2C);
    memcpy(mem_resolve(arm9_addr, arm9_size), rom + arm9_off, arm9_size);
    /* Le firmware laisse aussi l'en-tête de la ROM en 0x027FFE00 et quelques mots de réglage. */
    memcpy(mem_resolve(0x027FFE00, 0x170), rom, 0x170);
    mem_write32(c, 0x027FF800, 0x00000FC2u);      /* identifiant de puce */
    mem_write32(c, 0x027FF804, 0x00000FC2u);
    mem_write32(c, 0x027FFC00, 0x00000FC2u);
    mem_write32(c, 0x027FFC04, 0x00000FC2u);
    mem_write16(c, 0x027FFC10, 0x5835u);           /* type de démarrage / drapeaux */
    mem_write16(c, 0x027FFC40, 0x0001u);           /* démarrage normal */
    write_user_settings(c);

    size_t overlay_count;
    OverlayInfo *overlays = rt_load_overlay_table(rom, rom_size, &overlay_count);
    rt_set_overlays(overlays, overlay_count);
    rt_set_assert_info(0x020E237Cu);
    natif_init();
    { extern void echantillonneur_demarrer(void); echantillonneur_demarrer(); }   /* SFC_ECHANTILLONS */
    { extern void rt_fil_nommer(const char *); rt_fil_nommer("jeu"); }
    { extern void apercu_init(void); apercu_init(); }                             /* tours d'aperçu, étude */
    { extern uint32_t g_adr_surveillee; const char *a = getenv("SFC_SURVEILLE_ADR");
      if (a) g_adr_surveillee = (uint32_t)strtoul(a, NULL, 0); }

    /* Registres au démarrage : mode SVC, IRQ masquées, ARM ; sp posé par le crt0. */
    c->r[13] = 0x027E3FC0u;
    c->r[15] = arm9_entry;
    rt_watchdog_set_cpu(c);
    rt_watchdog_start(argc > arg_veille ? (unsigned)atoi(argv[arg_veille]) : 8u);
    rt_log("démarrage ARM9 à 0x%08X (%u octets chargés en 0x%08X)", arm9_entry, arm9_size, arm9_addr);
    rt_call(c, arm9_entry);
    rt_log("le crt0 a rendu la main : pc=0x%08X", c->r[15]);
    return 0;
}
