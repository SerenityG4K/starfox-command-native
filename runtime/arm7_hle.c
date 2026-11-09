/*
 * ARM7 simulé.
 *
 * Le composant ARM7 du NitroSDK dialogue avec l'ARM9 par IPCSYNC (poignée de
 * main au démarrage) puis par la FIFO PXI : chaque mot porte une étiquette
 * (bits 0-4), un drapeau (bit 5) et une donnée (bits 6-31). Les sous-systèmes :
 * OS (mise en veille, alimentation), SND (pilote son), MIC, WM (sans-fil),
 * RTC, TP (tactile), PM (gestion d'énergie), MI, CTRDG (cartouche GBA).
 *
 * Tactile (étiquette 6) : la requête porte la commande en bits 8-14 ; la
 * réponse reprend la commande, lève le bit 24 et met le résultat dans l'octet
 * bas (0 = succès, 1 = erreur). En échantillonnage automatique, l'ARM7 dépose chaque
 * mesure en RAM partagée (0x027FFFAA : x | y << 12 | contact << 24 |
 * validité << 25) et envoie la commande 0x10.
 *
 * Carte (étiquette 11, drapeau levé) : le composant CARD de l'ARM7 exécute
 * pour l'ARM9 les accès à la puce de sauvegarde et les lectures de ROM qu'il
 * lui délègue (NitroSDK card_common). L'ARM9 envoie CARD_REQ_INIT puis
 * l'adresse de la structure partagée CARDiCommandArg, ensuite une commande
 * par requête ; l'ARM7 fait le travail, écrit le résultat dans la structure
 * et répond un mot (drapeau levé ; le rappel ARM9 ignore la donnée et ne fait
 * que réveiller le thread demandeur).
 *
 * Son (étiquette 7) : l'ARM9 envoie l'adresse d'une liste de commandes
 * SNDCommand {suivant, identifiant, arg[4]} (0x18 octets) ; le pilote son de
 * l'ARM7 l'exécute puis incrémente finishCommandTag, premier mot de la zone
 * partagée SNDSharedWork dont la commande SHARED_WORK (0x1D) lui a donné
 * l'adresse ; l'ARM9 compte ainsi les listes terminées et récupère ses
 * commandes. Une donnée nulle demande un traitement immédiat. Du pilote
 * lui-même, on simule ce que l'ARM9 observe : les alarmes du minuteur son
 * (SETUP_ALARM puis START_TIMER ; l'ARM7 renvoie « numéro | identifiant << 8 »
 * sur l'étiquette 7 à chaque échéance, en unités de 64 cycles ARM7) et
 * l'état des lecteurs de séquence dans la zone partagée (bit d'activité,
 * compteur de ticks). Les canaux et l'audio ne sont pas produits.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include "arm7.h"
#include <stdlib.h>
#include <string.h>

/* étiquettes PXI (NitroSDK pxi_fifo.h ; 6, 7 et 11 vérifiées sur le jeu : tactile, son, carte) */
enum {
    PXI_TAG_EX = 0, PXI_TAG_OS = 1, PXI_TAG_PM = 2, PXI_TAG_MIC = 3, PXI_TAG_WM = 4, PXI_TAG_RTC = 5,
    PXI_TAG_TOUCH = 6, PXI_TAG_SOUND = 7, PXI_TAG_MI = 8, PXI_TAG_CTRDG = 9, PXI_TAG_WVR = 10, PXI_TAG_CARD = 11,
    PXI_TAG_USER = 20,
};

/* commandes du tactile */
enum { TP_CMD_SAMPLING = 0, TP_CMD_AUTO_ON = 3, TP_CMD_AUTO_OFF = 4, TP_CMD_AUTO_SAMPLE = 0x10 };
#define TP_SHARED_SAMPLE 0x027FFFAAu

static uint32_t g_sync9_seen;
static uint32_t g_sync7_value;
static int g_touch_down, g_touch_x, g_touch_y;
static int g_tp_auto;

/* Le composant ARM7 déclare ses rappels PXI prêts par des bits dans un mot de la
 * RAM partagée : 0x027FFC00 + 0x388 + 4 * processeur (1 = ARM7), un bit par
 * étiquette. PXI_IsCallbackReady(tag, ARM7) le consulte. */
#define PXI_READY_FLAGS_ARM7 0x027FFF8Cu

void arm7_init(Cpu *c) {
    g_sync7_value = 0;
    ipc_arm7_set_sync(c, 0);
    mem_write32(c, PXI_READY_FLAGS_ARM7, 0xFFFFFFFFu);
}

/* Poignée de main NitroSDK (PXI_Init) : l'ARM9 renvoie en écho la valeur qu'il
 * lit de l'ARM7 et attend qu'elle change ; il s'arrête quand elle vaut 0 après
 * plus de quatre changements. L'ARM7 fait de même de son côté : à chaque écho
 * correct, il passe à la valeur suivante de la séquence 1, 2, …, 6, 0. */
void arm7_on_sync(Cpu *c, uint32_t value) {
    g_sync9_seen = value;
    if (value == g_sync7_value) {
        g_sync7_value = (g_sync7_value + 1) % 7;
        ipc_arm7_set_sync(c, g_sync7_value);
    }
}

void arm7_on_sync_irq(Cpu *c) {
    (void)c;
}

static void reply(Cpu *c, uint32_t tag, uint32_t data, int flag) {
    ipc_arm7_send(c, (tag & 0x1F) | (flag ? 0x20 : 0) | (data << 6));
}

/* ------------------------------------------------------------------ tactile */
void hw_set_touch(int down, int x, int y) {
    g_touch_down = down;
    g_touch_x = x;
    g_touch_y = y;
}

/* Conversion pixel -> ADC, inverse de la calibration écrite dans les réglages
   utilisateur (ADC (0x100, 0x100) -> pixel (16, 16), (0xF00, 0xB80) -> (240, 176)). */
static uint32_t touch_sample(void) {
    if (!g_touch_down) return 0;
    int adc_x = 0x100 + (g_touch_x - 16) * (0xF00 - 0x100) / (240 - 16);
    int adc_y = 0x100 + (g_touch_y - 16) * (0xB80 - 0x100) / (176 - 16);
    if (adc_x < 0) adc_x = 0; if (adc_x > 0xFFF) adc_x = 0xFFF;
    if (adc_y < 0) adc_y = 0; if (adc_y > 0xFFF) adc_y = 0xFFF;
    return (uint32_t)adc_x | ((uint32_t)adc_y << 12) | (1u << 24);   /* contact, validité 0 = valide */
}

/* La même mesure, pour le coprocesseur traduit : c'est lui qui interroge alors le
   convertisseur par la liaison série, au lieu qu'on lui souffle le résultat. */
void arm7_touch_adc(int *down, uint32_t *adc_x, uint32_t *adc_y) {
    uint32_t w = touch_sample();
    *down = g_touch_down;
    *adc_x = w & 0xFFFu;
    *adc_y = (w >> 12) & 0xFFFu;
}

static void tp_store_sample(Cpu *c) {
    uint32_t w = touch_sample();
    mem_write16(c, TP_SHARED_SAMPLE, (uint16_t)w);
    mem_write16(c, TP_SHARED_SAMPLE + 2, (uint16_t)(w >> 16));
}

static void tp_request(Cpu *c, uint32_t data) {
    uint32_t cmd = (data >> 8) & 0x7F;
    switch (cmd) {
    case TP_CMD_SAMPLING:
        tp_store_sample(c);
        break;
    case TP_CMD_AUTO_ON:
        g_tp_auto = 1;
        break;
    case TP_CMD_AUTO_OFF:
        g_tp_auto = 0;
        break;
    default:
        break;
    }
    reply(c, PXI_TAG_TOUCH, 0x01000000u | (cmd << 8), 0);   /* octet bas 0 = succès */
}

/* ------------------------------------------------------------------ carte */
enum {
    CARD_REQ_INIT = 0, CARD_REQ_ACK, CARD_REQ_IDENTIFY, CARD_REQ_READ_ID, CARD_REQ_READ_ROM, CARD_REQ_WRITE_ROM,
    CARD_REQ_READ_BACKUP, CARD_REQ_WRITE_BACKUP, CARD_REQ_PROGRAM_BACKUP, CARD_REQ_VERIFY_BACKUP,
    CARD_REQ_ERASE_PAGE_BACKUP, CARD_REQ_ERASE_SECTOR_BACKUP, CARD_REQ_ERASE_CHIP_BACKUP,
};
enum { CARD_RESULT_SUCCESS = 0, CARD_RESULT_FAILURE = 1, CARD_RESULT_UNSUPPORTED = 3 };
/* CARDiCommandArg : résultat, type de sauvegarde, identifiant de la ROM, source, destination, longueur.
   Lecture : source dans la puce, destination en RAM ; écriture, vérification, effacement : l'inverse. */
enum { ARG_RESULT = 0x00, ARG_TYPE = 0x04, ARG_ID = 0x08, ARG_SRC = 0x0C, ARG_DST = 0x10, ARG_LEN = 0x14 };

static uint32_t g_card_arg;        /* adresse de la structure partagée */
static int g_card_await_arg;       /* CARD_REQ_INIT reçu : le mot suivant est cette adresse */

static void card_request(Cpu *c, uint32_t req) {
    if (g_card_await_arg) {
        g_card_arg = req;
        g_card_await_arg = 0;
        rt_log("ARM7 simulé : carte initialisée (structure partagée 0x%08X), sauvegarde type 0x%04X, %u octets",
               g_card_arg, backup_type(), backup_size());
        reply(c, PXI_TAG_CARD, CARD_REQ_ACK, 1);
        return;
    }
    if (req == CARD_REQ_INIT) { g_card_await_arg = 1; return; }
    if (!g_card_arg) { rt_log("ARM7 simulé : requête carte %u avant l'initialisation", req); return; }
    uint32_t a = g_card_arg;
    uint32_t src = mem_read32(c, a + ARG_SRC), dst = mem_read32(c, a + ARG_DST), len = mem_read32(c, a + ARG_LEN);
    uint32_t size = backup_size();
    uint8_t *bk = backup_bytes();
    uint32_t result = CARD_RESULT_SUCCESS;
    static int logged;
    if (logged < 16 || card_tracing()) { logged++; rt_log("ARM7 simulé : image %llu, carte requête %u (src 0x%08X, dst 0x%08X, %u octets)", (unsigned long long)hw_frames(), req, src, dst, len); }
    switch (req) {
    case CARD_REQ_IDENTIFY: {
        uint32_t type = mem_read32(c, a + ARG_TYPE);
        result = type == backup_type() ? CARD_RESULT_SUCCESS : CARD_RESULT_FAILURE;
        break;
    }
    case CARD_REQ_READ_ID:
        mem_write32(c, a + ARG_ID, card_chip_id());
        break;
    case CARD_REQ_READ_ROM: {
        uint8_t *tampon = malloc(len ? len : 1);
        card_rom_read(src, tampon, len);
        for (uint32_t i = 0; i < len; i++) mem_write8(c, dst + i, tampon[i]);
        free(tampon);
        break;
    }
    case CARD_REQ_READ_BACKUP:
        for (uint32_t i = 0; i < len; i++) mem_write8(c, dst + i, bk[(src + i) % size]);
        break;
    case CARD_REQ_WRITE_BACKUP:
    case CARD_REQ_PROGRAM_BACKUP:
        for (uint32_t i = 0; i < len; i++) bk[(dst + i) % size] = mem_read8(c, src + i);
        backup_commit();
        break;
    case CARD_REQ_VERIFY_BACKUP:
        for (uint32_t i = 0; i < len; i++)
            if (bk[(dst + i) % size] != mem_read8(c, src + i)) { result = CARD_RESULT_FAILURE; break; }
        break;
    case CARD_REQ_ERASE_PAGE_BACKUP:
    case CARD_REQ_ERASE_SECTOR_BACKUP:
        for (uint32_t i = 0; i < len; i++) bk[(dst + i) % size] = 0xFF;
        backup_commit();
        break;
    case CARD_REQ_ERASE_CHIP_BACKUP:
        memset(bk, 0xFF, size);
        backup_commit();
        break;
    default:
        rt_log("ARM7 simulé : requête carte inconnue %u", req);
        result = CARD_RESULT_UNSUPPORTED;
        break;
    }
    mem_write32(c, a + ARG_RESULT, result);
    reply(c, PXI_TAG_CARD, CARD_REQ_ACK, 1);
}

/* ------------------------------------------------------------------ son */
enum {
    SND_CMD_START_SEQ = 0x00, SND_CMD_STOP_SEQ = 0x01, SND_CMD_PREPARE_SEQ = 0x02, SND_CMD_START_PREPARED_SEQ = 0x03,
    SND_CMD_START_TIMER = 0x0C, SND_CMD_STOP_TIMER = 0x0D, SND_CMD_SETUP_ALARM = 0x12, SND_CMD_SHARED_WORK = 0x1D,
};
/* SNDSharedWork : finishCommandTag @0, playerStatus @4, channelStatus @8, captureStatus @A,
   puis 16 lecteurs de 36 octets (tickCounter @0, 16 variables locales) à partir de 0xC */
enum { SND_WORK_PLAYER_STATUS = 0x04, SND_WORK_PLAYERS = 0x0C, SND_WORK_PLAYER_SIZE = 36 };
#define SND_UNITS_PER_FRAME 8753u   /* 560 190 cycles ARM7 par image, en unités de 64 cycles */
#define SND_TICKS_PER_FRAME 2u      /* progression approximative d'un lecteur (tempo inconnu) */

static uint32_t g_snd_shared;      /* SNDSharedWork, dans la RAM principale */
static uint32_t g_snd_finished;    /* listes de commandes terminées */
static uint32_t g_snd_now;         /* horloge du minuteur son */
static struct { uint32_t tick, period, id, next; int enabled; } g_snd_alarm[8];
static struct { int active; uint32_t ticks; } g_snd_player[16];

static void snd_command_list(Cpu *c, uint32_t list) {
    static int logged;
    for (uint32_t cmd = list; cmd; cmd = mem_read32(c, cmd)) {
        uint32_t id = mem_read32(c, cmd + 4);
        uint32_t a0 = mem_read32(c, cmd + 8), a1 = mem_read32(c, cmd + 12);
        uint32_t a2 = mem_read32(c, cmd + 16), a3 = mem_read32(c, cmd + 20);
        if (logged < 40) {
            logged++;
            rt_log("ARM7 simulé : son commande 0x%02X (%08X %08X %08X %08X)", id, a0, a1, a2, a3);
        }
        switch (id) {
        case SND_CMD_SHARED_WORK: g_snd_shared = a0; break;
        case SND_CMD_START_SEQ: case SND_CMD_START_PREPARED_SEQ:
            if (a0 < 16) { g_snd_player[a0].active = 1; g_snd_player[a0].ticks = 0; }
            break;
        case SND_CMD_PREPARE_SEQ:
            if (a0 < 16) g_snd_player[a0].ticks = 0;
            break;
        case SND_CMD_STOP_SEQ:
            if (a0 < 16) g_snd_player[a0].active = 0;
            break;
        case SND_CMD_SETUP_ALARM:
            if (a0 < 8) { g_snd_alarm[a0].tick = a1; g_snd_alarm[a0].period = a2; g_snd_alarm[a0].id = a3; }
            break;
        case SND_CMD_START_TIMER:
            for (int i = 0; i < 8; i++)
                if ((a2 >> i) & 1) { g_snd_alarm[i].enabled = 1; g_snd_alarm[i].next = g_snd_now + g_snd_alarm[i].tick; }
            break;
        case SND_CMD_STOP_TIMER:
            for (int i = 0; i < 8; i++)
                if ((a2 >> i) & 1) g_snd_alarm[i].enabled = 0;
            break;
        default: break;
        }
    }
    g_snd_finished++;
    if (g_snd_shared) mem_write32(c, g_snd_shared, g_snd_finished);
    else rt_log("ARM7 simulé : liste son terminée sans zone partagée connue");
}

/* Une image de temps pour le minuteur son : alarmes échues, lecteurs qui avancent. */
static void snd_on_vblank(Cpu *c) {
    g_snd_now += SND_UNITS_PER_FRAME;
    for (int i = 0; i < 8; i++) {
        if (!g_snd_alarm[i].enabled) continue;
        int fired = 0;
        while ((int32_t)(g_snd_now - g_snd_alarm[i].next) >= 0 && fired < 8) {
            reply(c, PXI_TAG_SOUND, (uint32_t)i | ((g_snd_alarm[i].id & 0xFF) << 8), 0);
            fired++;
            if (g_snd_alarm[i].period == 0) { g_snd_alarm[i].enabled = 0; break; }
            g_snd_alarm[i].next += g_snd_alarm[i].period;
        }
    }
    if (!g_snd_shared) return;
    uint32_t status = 0;
    for (int p = 0; p < 16; p++) {
        if (!g_snd_player[p].active) continue;
        status |= 1u << p;
        g_snd_player[p].ticks += SND_TICKS_PER_FRAME;
        mem_write32(c, g_snd_shared + SND_WORK_PLAYERS + (uint32_t)p * SND_WORK_PLAYER_SIZE, g_snd_player[p].ticks);
    }
    mem_write32(c, g_snd_shared + SND_WORK_PLAYER_STATUS, status);
}

/* ------------------------------------------------------------------ réception */
void arm7_on_fifo(Cpu *c, uint32_t word) {
    uint32_t tag = word & 0x1F;
    int flag = (word >> 5) & 1;
    uint32_t data = word >> 6;
    static int seen[32], trace_pxi = -1;
    if (trace_pxi < 0) trace_pxi = getenv("SFC_TRACE_PXI") != NULL;   /* SFC_TRACE_PXI=1 : tous les messages, avec l image */
    if (seen[tag] < 3 || trace_pxi) {
        seen[tag]++;
        rt_log("PXI -> ARM7 : image %llu, étiquette %u donnée 0x%07X%s", (unsigned long long)hw_frames(), tag, data, flag ? " (drapeau)" : "");
    }
    switch (tag) {
    case PXI_TAG_TOUCH:
        tp_request(c, data);
        break;
    case PXI_TAG_SOUND:
        if (data) snd_command_list(c, data);   /* une donnée nulle : traiter les listes en attente */
        break;
    case PXI_TAG_PM:
        reply(c, PXI_TAG_PM, data, flag);
        break;
    case PXI_TAG_RTC:
        /* commande en bits 8-14, octet bas = resultat (0 = succes), comme le tactile */
        reply(c, PXI_TAG_RTC, data & ~0xFFu, flag);
        break;
    case PXI_TAG_CARD:
        card_request(c, data);
        break;
    case PXI_TAG_MIC:
    case PXI_TAG_WM:
    case PXI_TAG_CTRDG:
    case PXI_TAG_MI:
    case PXI_TAG_OS:
    case PXI_TAG_EX:
    default:
        rt_log("ARM7 simulé : message PXI étiquette %u donnée 0x%X%s", tag, data, flag ? " (drapeau)" : "");
        break;
    }
}

/* Une fois par image : le minuteur son, puis l'échantillon automatique du tactile. */
void arm7_on_vblank(Cpu *c) {
    snd_on_vblank(c);
    if (!g_tp_auto) return;
    tp_store_sample(c);
    reply(c, PXI_TAG_TOUCH, (uint32_t)TP_CMD_AUTO_SAMPLE << 8, 0);
}
