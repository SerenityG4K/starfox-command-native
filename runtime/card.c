/*
 * Carte de jeu : registres ROMCTRL, commande, données.
 *
 * Après le démarrage par le firmware, la cartouche est en mode « normal » :
 * le jeu envoie des commandes de 8 octets (0xB7 : lire 512 octets à une
 * adresse ; 0xB8 : identifiant de la puce) et lit les mots en 0x04100010,
 * directement ou par DMA. Le chiffrement KEY2 est invisible pour le logiciel.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include <stdlib.h>
#include <string.h>

static const uint8_t *g_rom;
static size_t g_rom_size;
static uint32_t g_romctrl;
static uint32_t g_data_pos, g_data_len;
static uint8_t g_buffer[0x4000];
static uint32_t g_chip_id = 0x00000FC2u;
static int g_trace = -1;

/* SFC_TRACE_CARD=1 : chaque commande, chaque transfert DMA, chaque interruption de la carte */
int card_tracing(void) {
    if (g_trace < 0) g_trace = getenv("SFC_TRACE_CARD") != NULL;
    return g_trace;
}

/* SFC_TRACE_FICHIER=1 : le nom de chaque fichier que le jeu demande au lecteur */
static int fichiers_traces(void) {
    static int t = -1;
    if (t < 0) t = getenv("SFC_TRACE_FICHIER") != NULL;
    return t;
}

/* des données attendent d'être lues en 0x04100010 (ROMCTRL bit 23) */
int card_data_ready(void) {
    return (g_romctrl & 0x00800000u) != 0;
}

void card_init(const uint8_t *rom, size_t size) {
    g_rom = rom;
    g_rom_size = size;
}

uint32_t card_chip_id(void) { return g_chip_id; }

const uint8_t *card_rom_bytes(size_t *size) {
    *size = g_rom_size;
    return g_rom;
}

/* Les octets de la cartouche, d'où qu'ils viennent : du dossier de données quand il est là,
   de l'image sinon. Au-delà des données, la cartouche ne porte que des zéros — sauf
   au-delà du support lui-même, où l'image ne dit plus rien et où le bus reste à un. */
void card_rom_read(uint32_t addr, uint8_t *dst, uint32_t len) {
    if (data_disponible()) { data_read(addr, dst, len); return; }
    for (uint32_t i = 0; i < len; i++)
        dst[i] = (size_t)addr + i < g_rom_size ? g_rom[addr + i] : 0xFF;
}

static uint32_t block_size(uint32_t romctrl) {
    uint32_t n = (romctrl >> 24) & 7;
    if (n == 0) return 0;
    if (n == 7) return 4;
    return 0x100u << n;
}

static void start_command(Cpu *c) {
    uint8_t cmd[8];
    for (int i = 0; i < 8; i++) cmd[i] = (uint8_t)(mmio_peek(0x1A8 + (i & 4)) >> ((i & 3) * 8));
    uint32_t len = block_size(g_romctrl);
    g_data_pos = 0;
    g_data_len = len;
    memset(g_buffer, 0xFF, sizeof g_buffer);
    switch (cmd[0]) {
    case 0xB7: {
        uint32_t addr = ((uint32_t)cmd[1] << 24) | ((uint32_t)cmd[2] << 16) | ((uint32_t)cmd[3] << 8) | cmd[4];
        static int logged;
        if (logged < 12 || card_tracing()) {
            logged++;
            rt_log("carte : image %llu, lecture 0x%08X (%u octets), MCCNT0=%04X", (unsigned long long)hw_frames(), addr, len, mmio_peek(0x1A0) & 0xFFFF);
        }
        if (fichiers_traces()) {
            int ov = rt_overlay_at_rom(addr);
            const char *nom = ov < 0 ? data_nom_a(addr) : NULL;
            if (ov >= 0) rt_log("carte : image %llu, overlay %d", (unsigned long long)hw_frames(), ov);
            else if (nom) rt_log("carte : image %llu, %s", (unsigned long long)hw_frames(), nom);
        }
        if (addr < 0x8000) addr = 0x8000 + (addr & 0x1FF);   /* la zone sécurisée n'est pas relisible */
        card_rom_read(addr, g_buffer, len < sizeof g_buffer ? len : (uint32_t)sizeof g_buffer);
        break;
    }
    case 0xB8:
        for (uint32_t i = 0; i < len && i < sizeof g_buffer; i += 4) memcpy(g_buffer + i, &g_chip_id, 4);
        break;
    case 0x9F: case 0x00: case 0x90:
        /* mode brut : en-tête ou identifiant ; on renvoie l'en-tête de la ROM ou l'identifiant */
        if (cmd[0] == 0x90) { for (uint32_t i = 0; i < len && i < sizeof g_buffer; i += 4) memcpy(g_buffer + i, &g_chip_id, 4); }
        else if (cmd[0] == 0x00) {
            /* l'en-tête, répétée tous les 4 Ko comme le fait la cartouche en mode brut */
            static uint8_t entete[0x1000];
            static int lue;
            if (!lue) { card_rom_read(0, entete, sizeof entete); lue = 1; }
            for (uint32_t i = 0; i < len && i < sizeof g_buffer; i++) g_buffer[i] = entete[i & 0xFFF];
        }
        break;
    default:
        rt_log("carte : commande inconnue %02X %02X %02X %02X %02X %02X %02X %02X",
               cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7]);
        break;
    }
    if (len == 0) {
        g_romctrl &= ~0x80000000u;
        if (mmio_peek(0x1A0) & 0x4000) hw_raise_irq(c, IRQ_CARD);
        return;
    }
    g_romctrl |= 0x00800000u;   /* données prêtes */
    card_feed_dma(c);
}

/* Un DMA en mode « carte » se déclenche à chaque mot prêt, tant que le canal reste armé
   (le SDK charge les overlays avec un canal d'un mot en répétition) : on le relance jusqu'à
   épuisement des données, ou jusqu'à ce qu'aucun canal ne lise plus la carte. */
void card_feed_dma(Cpu *c) {
    while (g_romctrl & 0x00800000u) {
        uint32_t before = g_data_pos;
        dma_trigger(c, 3);
        if (g_data_pos == before) break;
    }
}

uint32_t card_read_romctrl(Cpu *c) {
    (void)c;
    return g_romctrl;
}

void card_write_romctrl(Cpu *c, uint32_t v) {
    uint32_t start = (v & 0x80000000u) && !(g_romctrl & 0x80000000u);
    g_romctrl = (v & 0x7F7FFFFFu) | (g_romctrl & 0x00800000u);
    if (start) {
        g_romctrl |= 0x80000000u;
        start_command(c);
    }
}

uint32_t card_read_data(Cpu *c) {
    if (!(g_romctrl & 0x00800000u)) return 0xFFFFFFFFu;
    uint32_t v;
    memcpy(&v, g_buffer + g_data_pos, 4);
    g_data_pos += 4;
    if (g_data_pos >= g_data_len) {
        g_romctrl &= ~(0x80000000u | 0x00800000u);
        if (mmio_peek(0x1A0) & 0x4000) hw_raise_irq(c, IRQ_CARD);
        if (card_tracing())
            rt_log("carte : transfert terminé (%u octets), IRQ carte %s", g_data_len,
                   (mmio_peek(0x1A0) & 0x4000) ? "levée" : "désactivée");
    }
    return v;
}
