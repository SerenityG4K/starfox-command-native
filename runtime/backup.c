/*
 * Mémoire de sauvegarde de la cartouche : puce SPI derrière AUXSPICNT
 * (0x040001A0) et AUXSPIDATA (0x040001A2).
 *
 * Le SDK identifie la puce par la longueur d'adresse et le comportement des
 * commandes standard : 0x03 lecture, 0x02/0x0A écriture, 0x06 activation
 * d'écriture, 0x04 désactivation, 0x05 statut, 0x01 écriture du statut,
 * 0x9F identifiant JEDEC, 0xD8/0xDB effacements. Le composant CARD de l'ARM7
 * simulé (arm7_hle.c) accède aussi directement à la puce, comme le ferait
 * l'ARM7 réel par SPI pour le compte de l'ARM9.
 *
 * Type simulé : FLASH de 256 Ko (CARDBackupType 0x1202 : famille 2, 2^18
 * octets), le premier que le jeu essaie ; SFC_BACKUP_TYPE le change
 * (famille 1 EEPROM, 2 FLASH, 3 FRAM | log2(taille) << 8). Persistée dans
 * build/save.bin.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir((p), 0755)
#endif

#define BACKUP_MAX 0x80000u
#define ADDR_BYTES 3
/* Où vit la sauvegarde.
 *
 * Elle ne peut pas vivre dans build/ : ce répertoire est celui de la compilation, on l'efface
 * pour repartir propre, et la progression d'une partie part avec lui. Elle vit donc à côté du
 * dépôt, dans sauvegarde/, sous un nom fixe. La première fois, une sauvegarde restée dans
 * build/ est reprise plutôt que perdue.
 *
 * SFC_SAVE=chemin la déplace : c'est ce qu'il faut utiliser pour essayer quelque chose sans
 * toucher à une partie en cours — et surtout ce qu'il ne faut PAS utiliser pour jouer, sans
 * quoi chaque essai repart d'une sauvegarde vierge. */
#define SAVE_DEFAULT "sauvegarde/starfox_command.sav"

static void copy_file(const char *from, const char *to) {
    FILE *a = fopen(from, "rb");
    if (!a) return;
    FILE *b = fopen(to, "wb");
    if (!b) { fclose(a); return; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
    fclose(a); fclose(b);
    rt_log("sauvegarde : %s repris depuis %s", to, from);
}

static const char *save_path(void) {
    const char *p = getenv("SFC_SAVE");
    if (p && *p) return p;
    static int prepared;
    if (!prepared) {
        prepared = 1;
        make_dir("sauvegarde");
        FILE *f = fopen(SAVE_DEFAULT, "rb");
        if (f) fclose(f);
        else copy_file("build/save.bin", SAVE_DEFAULT);
    }
    return SAVE_DEFAULT;
}

static uint32_t g_type = 0x1202u;
static uint32_t g_size;
static uint8_t *g_data;

uint32_t backup_type(void) {
    if (!g_size) {
        const char *t = getenv("SFC_BACKUP_TYPE");
        if (t) g_type = (uint32_t)strtoul(t, NULL, 0);
        g_size = 1u << ((g_type >> 8) & 0xFF);
        if (g_size > BACKUP_MAX) g_size = BACKUP_MAX;
    }
    return g_type;
}

uint32_t backup_size(void) {
    backup_type();
    return g_size;
}
static uint32_t g_cnt;
static int g_selected;          /* transaction en cours (chip select maintenu) */
static uint8_t g_cmd;
static int g_pos;               /* octets reçus depuis le début de la transaction */
static uint32_t g_addr;
static uint32_t g_start_addr;    /* adresse de la transaction en cours, pour la trace */
static uint8_t g_status;        /* bit 1 = écriture activée */
static uint8_t g_last_out;
static int g_dirty;

static void backup_load(void) {
    g_data = malloc(BACKUP_MAX);
    memset(g_data, 0xFF, BACKUP_MAX);
    FILE *f = fopen(save_path(), "rb");
    if (f) { size_t n = fread(g_data, 1, backup_size(), f); (void)n; fclose(f); rt_log("sauvegarde : %s chargée", save_path()); }
}

static void backup_flush(void) {
    if (!g_dirty) return;
    const char *path = save_path();
    FILE *f = fopen(path, "wb");
    if (!f) { rt_log("sauvegarde : impossible d'écrire %s", path); return; }
    fwrite(g_data, 1, backup_size(), f);
    fclose(f);
    g_dirty = 0;
}

uint8_t *backup_bytes(void) {
    if (!g_data) backup_load();
    return g_data;
}

void backup_commit(void) {
    g_dirty = 1;
    backup_flush();
}

static int backup_tracing(void) {
    static int t = -1;
    if (t < 0) t = getenv("SFC_TRACE_BACKUP") != NULL;
    return t;
}

static void end_transaction(void) {
    if (g_cmd == 0x02 || g_cmd == 0x0A || g_cmd == 0xD8 || g_cmd == 0xDB || g_cmd == 0xC7) backup_flush();
    if (g_cmd == 0x06) g_status |= 2;                                  /* WREN */
    if (g_cmd == 0x04) g_status &= ~2;                                 /* WRDI */
    /* Le matériel efface lui-même l'autorisation d'écriture dès qu'une écriture ou un
       effacement s'achève : le pilote s'en sert pour savoir que l'opération est finie. */
    if (g_cmd == 0x02 || g_cmd == 0x0A || g_cmd == 0xD8 || g_cmd == 0xDB || g_cmd == 0xC7)
        g_status &= ~2u;
    if (backup_tracing() && g_cmd)
        rt_log("sauvegarde : commande 0x%02X, adresse 0x%05X, %d octets, statut %02X",
               g_cmd, g_start_addr, g_pos - 1 - (g_cmd == 0x02 || g_cmd == 0x0A || g_cmd == 0x03 ? ADDR_BYTES : 0),
               g_status);
    g_selected = 0;
    g_pos = 0;
    g_cmd = 0;
}

/* SFC_TRACE_BACKUP=1 : chaque transaction avec la puce, pour voir si le jeu écrit vraiment. */
static uint8_t transfer(uint8_t in) {
    if (!g_data) backup_load();
    uint8_t out = 0;
    if (g_pos == 0) { g_cmd = in; g_addr = 0; g_pos = 1; return 0; }
    if (g_pos == ADDR_BYTES) g_start_addr = ((g_addr << 8) | in) & 0xFFFFFFu;
    switch (g_cmd) {
    case 0x05: out = g_status; break;                                  /* RDSR */
    case 0x01: g_status = (g_status & 2) | (in & 0x8C); break;         /* WRSR */
    case 0x9F: {                                                       /* identification JEDEC */
        /* Le pilote du SDK déduit la taille de la puce du troisième octet, qui vaut le
           logarithme binaire de sa capacité. L'annoncer plus grande qu'elle n'est fait écrire
           le jeu au-delà, et les adresses se replient sur elles-mêmes. */
        uint32_t capacity = 0;
        for (uint32_t n = backup_size(); n > 1; n >>= 1) capacity++;
        const uint8_t id[3] = {0x20, 0x40, (uint8_t)capacity};
        out = g_pos - 1 < 3 ? id[g_pos - 1] : 0xFF;
        break;
    }
    case 0x03: case 0x0B:                                              /* lecture (0x0B : octet factice) */
        if (g_pos <= ADDR_BYTES) g_addr = (g_addr << 8) | in;
        else if (g_cmd == 0x0B && g_pos == ADDR_BYTES + 1) out = 0;
        else { out = g_data[g_addr % backup_size()]; g_addr++; }
        break;
    case 0x02: case 0x0A:                                              /* écriture page */
        if (g_pos <= ADDR_BYTES) g_addr = (g_addr << 8) | in;
        else if (g_status & 2) { g_data[g_addr % backup_size()] = in; g_addr++; g_dirty = 1; }
        break;
    case 0xD8:                                                         /* effacement de secteur 64 Ko */
        if (g_pos <= ADDR_BYTES) g_addr = (g_addr << 8) | in;
        if (g_pos == ADDR_BYTES && (g_status & 2)) { memset(g_data + (g_addr % backup_size() & ~0xFFFFu), 0xFF, 0x10000); g_dirty = 1; }
        break;
    case 0xDB:                                                         /* effacement de page 256 o */
        if (g_pos <= ADDR_BYTES) g_addr = (g_addr << 8) | in;
        if (g_pos == ADDR_BYTES && (g_status & 2)) { memset(g_data + (g_addr % backup_size() & ~0xFFu), 0xFF, 0x100); g_dirty = 1; }
        break;
    case 0xC7:                                                         /* effacement complet */
        if (g_status & 2) { memset(g_data, 0xFF, backup_size()); g_dirty = 1; }
        break;
    default: break;
    }
    g_pos++;
    return out;
}

void card_write_auxspi(Cpu *c, uint32_t v) {
    (void)c;
    /* AUXSPICNT (16 bits bas) : bit 6 = chip select maintenu, bit 13 = mode SPI, bit 15 = activé.
     *
     * Le bit 6 dit ce qu'il adviendra à la fin du transfert SUIVANT, pas du précédent : pour
     * envoyer le dernier octet d'une transaction, le pilote l'abaisse d'abord, puis écrit
     * l'octet. Terminer la transaction ici même revenait à jeter cet octet — le suivant
     * repartait en début de transaction et passait pour une commande. Un octet manquait donc à
     * la fin de chaque lecture comme de chaque écriture, et la sauvegarde du jeu se retrouvait
     * trouée d'un octet par page. La transaction se termine à l'octet, dans card_write_auxspidata.
     *
     * Couper l'alimentation du module (bit 15) l'interrompt en revanche pour de bon. */
    uint32_t cnt = v & 0xFFFF;
    uint32_t old = g_cnt;
    g_cnt = cnt;
    mmio_poke(0x1A0, (mmio_peek(0x1A0) & 0xFFFF0000u) | cnt);
    if ((old & 0x8000) && !(cnt & 0x8000)) end_transaction();
}

void card_write_auxspidata(Cpu *c, uint32_t v) {
    (void)c;
    if (!(g_cnt & 0x8000) || !(g_cnt & 0x2000)) return;
    g_selected = 1;
    g_last_out = transfer((uint8_t)v);
    if (!(g_cnt & 0x40)) end_transaction();
}

uint32_t card_read_auxspidata(Cpu *c) {
    (void)c;
    return g_last_out;
}
