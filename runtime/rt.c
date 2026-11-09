/*
 * Couche d'exécution : dispatch, modes du processeur, coprocesseur 15,
 * points d'arrêt (assertions du jeu), journal.
 *
 * Sous licence MIT. Ne contient rien du jeu.
 */
#include "rt.h"
#include <setjmp.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "hw.h"
#include "arm7_run.h"
#include "video2d.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ journal */
void rt_dump_dispatch(void);

void rt_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

/* SFC_SURVEILLE_ADR : la pile de la machine hôte à l'écriture de ce mot — les fonctions réécrites y portent
   l'adresse d'origine dans leur nom (tools/echantillons.py les traduit). */
/* Ce qui n'a rien à faire dans les amorces : l'adresse nulle, et le point de reprise d'OS_SaveContext, que
   nos crochets tiennent (threads.c). Cartographié, il fait naître une fonction qui en avale d'autres. */
static int amorce_utile(uint32_t addr) {
    uint32_t a = addr & ~1u;
    return a != 0 && a != 0x0202E688u;
}

uint32_t g_adr_surveillee;

void rt_adresse_ecrite(Cpu *c, uint32_t addr, uint32_t v) {
    extern int g_apercu_en_cours;
    static int n;
    if (g_apercu_en_cours || n++ >= 6) return;
    void *hote[40];
    unsigned short nh = RtlCaptureStackBackTrace(0, 40, hote, NULL);
    char l[900] = {0};
    uintptr_t base = (uintptr_t)GetModuleHandleW(NULL);
    for (unsigned i = 0; i < nh; i++)
        snprintf(l + strlen(l), sizeof l - strlen(l), " %llx", (unsigned long long)((uintptr_t)hote[i] - base));
    rt_log("surveillance 0x%08X <- 0x%08X (image %llu, pc 0x%08X) ; pile hôte :%s", addr, v,
           (unsigned long long)hw_frames(), c->r[15], l);
}

/* Quitter depuis le fil du jeu. exit() détruit les objets statiques — le cache de textures, les tampons
   de sommets… — pendant que le fil d'affichage, qui dessine encore, s'en sert : la sortie finissait en
   faute (une fin de rejeu sur deux, les tours d'aperçu en marche). On arrête donc l'affichage d'abord.
   Quand le joueur ferme la fenêtre, c'est le fil d'affichage qui s'arrête le premier, de lui-même. */
void rt_sortir(int code) {
    extern void video_arreter(void);
    video_arreter();
    exit(code);
}

void rt_fatal(Cpu *c, const char *fmt, ...) {
    /* À froid, un essai nourri de valeurs tirées au sort peut mener n'importe où — y compris là
       où le jeu n'irait jamais. Ce n'est pas la fin du banc : l'essai est abandonné. */
    if (g_essai_aretes_max) longjmp(g_essai_reprise, 1);
    { extern int g_apercu_en_cours; extern void apercu_abandon(int);
      if (g_apercu_en_cours) {
          static int n;
          if (n++ < 4) { va_list ap0; va_start(ap0, fmt); char m[300]; vsnprintf(m, sizeof m, fmt, ap0); va_end(ap0);
                         rt_log("aperçu : erreur « %s » (pc 0x%08X, lr 0x%08X)", m, c->r[15], c->r[14]); }
          apercu_abandon(2);
      } }   /* un tour d'aperçu qui s'égare est effacé */
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "FATAL: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n  r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X\n"
                    "  r8=%08X r9=%08X r10=%08X r11=%08X r12=%08X sp=%08X lr=%08X pc=%08X\n"
                    "  mode=%02X irq_off=%u profondeur=%u\n",
            c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7],
            c->r[8], c->r[9], c->r[10], c->r[11], c->r[12], c->r[13], c->r[14], c->r[15],
            c->mode, c->irq_off, c->depth);
    for (uint32_t i = c->depth; i > 0 && i > c->depth - 16; i--)
        fprintf(stderr, "  retour[%u] = %08X\n", i - 1, c->shadow[i - 1]);
    rt_dump_dispatch();
    fflush(stderr);
    rt_sortir(2);
}

/* ------------------------------------------------------------------ traque d'une valeur */
uint64_t hw_frames(void);
uint32_t g_watch_value;

/* SFC_WATCH_VALUE=0x… : chaque écriture de cette valeur en mémoire est journalisée avec l'endroit
   et la pile d'appels — pour remonter à la source d'un pointeur corrompu. */
void rt_value_written(Cpu *c, uint32_t addr, uint32_t v) {
    static int n;
    if (n++ >= 20) return;
    rt_log("VALEUR 0x%08X écrite en 0x%08X (image %llu, pc=0x%08X lr=0x%08X profondeur=%u)",
           v, addr, (unsigned long long)hw_frames(), c->r[15], c->r[14], c->depth);
    rt_log("    r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X sp=%08X",
           c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7], c->r[13]);
    for (int k = 3; k <= 6; k++) {
        uint32_t base = c->r[k] & ~3u;
        if (!mem_resolve(base, 4)) continue;
        char line[320]; int m = 0;
        for (int i = 0; i < 8; i++) m += snprintf(line + m, sizeof line - (size_t)m, " %08X", mem_read32(c, base + (uint32_t)i * 4));
        rt_log("    [r%d=%08X] :%s", k, c->r[k], line);
    }
    for (uint32_t i = c->depth; i > 0 && i > c->depth - 12; i--)
        rt_log("    retour[%u] = %08X", i - 1, c->shadow[i - 1]);
}

/* SFC_POKE=0xadresse:0xvaleur[,0xadresse:0xvaleur…] : réécrit ces mots à chaque image.
 *
 * Sert à éprouver une hypothèse sur un champ d'une structure du jeu : on force la valeur et on
 * regarde ce que devient l'image. Bien plus rapide que de lire le code, et sans risque puisque
 * rien n'est modifié durablement. */
/* SFC_APPEL=image:0xadresse[,…] : appelle cette fonction du jeu à cette image, une fois.
 *
 * Le jeu garde des commandes de mise au point que ses auteurs déclenchaient depuis un menu
 * disparu — « lancer une partie », « choisir un niveau ». Les appeler du dehors permet
 * d'atteindre du code qu'aucune suite de touches ne sait plus atteindre. L'état du processeur
 * est retenu avant l'appel et rendu après : le jeu reprend où il en était, la commande en
 * plus. */
void rt_appel_programme(Cpu *c) {
    static int init;
    static uint64_t quand[8];
    static uint32_t ou[8];
    static int n, faits[8];
    if (!init) {
        init = 1;
        const char *v = getenv("SFC_APPEL");
        while (v && *v && n < 8) {
            char *fin;
            uint64_t image = strtoull(v, &fin, 0);
            if (*fin != ':') break;
            uint32_t adresse = (uint32_t)strtoul(fin + 1, &fin, 0);
            quand[n] = image; ou[n] = adresse; n++;
            rt_log("appel programmé : 0x%08X à l'image %llu", adresse, (unsigned long long)image);
            if (*fin != ',') break;
            v = fin + 1;
        }
    }
    for (int i = 0; i < n; i++) {
        if (faits[i] || hw_frames() < quand[i]) continue;
        faits[i] = 1;
        Cpu garde = *c;
        rt_log("appel programmé : 0x%08X maintenant (image %llu)", ou[i],
               (unsigned long long)hw_frames());
        rt_call(c, ou[i] | 1u);
        uint32_t rendu = c->r[0];
        *c = garde;
        rt_log("appel programmé : rendu 0x%08X", rendu);
    }
}

void rt_poke(void) {
    static int init;
    static uint32_t addr[8], val[8];
    static int n;
    if (!init) {
        init = 1;
        const char *v = getenv("SFC_POKE");
        while (v && *v && n < 8) {
            char *end;
            uint32_t a = (uint32_t)strtoul(v, &end, 0);
            if (*end != ':') break;
            uint32_t x = (uint32_t)strtoul(end + 1, &end, 0);
            addr[n] = a; val[n] = x; n++;
            rt_log("poke : 0x%08X <- 0x%08X à chaque image", a, x);
            if (*end != ',') break;
            v = end + 1;
        }
    }
    for (int i = 0; i < n; i++) {
        uint8_t *p = mem_resolve(addr[i], 4);
        if (p) memcpy(p, &val[i], 4);
    }
}

/* SFC_DUMP_MEM=0xadresse:octets : vide cette zone toutes les SFC_DUMP_MEM_EVERY images (300 par
   défaut). Sert à lire une structure du jeu en fonctionnement quand on en cherche un champ. */
void rt_dump_mem(uint64_t frame) {
    static int init, every, len;
    static uint32_t addr;
    if (!init) {
        init = 1;
        const char *v = getenv("SFC_DUMP_MEM");
        if (v) {
            addr = (uint32_t)strtoul(v, NULL, 0);
            const char *c = strchr(v, ':');
            len = c ? atoi(c + 1) : 64;
            if (len <= 0 || len > 512) len = 64;
        }
        const char *e = getenv("SFC_DUMP_MEM_EVERY");
        every = e ? atoi(e) : 300;
        if (every <= 0) every = 300;
    }
    if (!addr || (frame % (uint64_t)every)) return;
    for (int off = 0; off < len; off += 16) {
        char line[160];
        int m = snprintf(line, sizeof line, "mémoire 0x%08X :", addr + off);
        for (int k = 0; k < 16 && off + k < len; k += 4) {
            const uint8_t *p = mem_resolve(addr + off + k, 4);
            if (p) {
                uint32_t w;
                memcpy(&w, p, 4);
                m += snprintf(line + m, sizeof line - m, " %08X", w);
            } else {
                m += snprintf(line + m, sizeof line - m, " --------");
            }
        }
        rt_log("%s", line);
    }
}

/* SFC_DIAG_OBJETS=image[,image…] : la position de chaque objet de la liste que parcourt le rendu de
   la scène (0x01FFBF8C) — dessiné ou non —, en unités du monde. Deux relevés à quelques images
   d'écart disent quels objets bougent, et à quelle distance de la caméra. La liste a sa tête en
   0x027E14D8 : premier nœud en +8, fin de liste en +4 ; l'objet commence 0x4C octets avant son
   nœud, sa position en +0x10. */
static uint32_t lire_mot(uint32_t adresse) {
    const uint8_t *p = mem_resolve(adresse, 4);
    uint32_t v = 0;
    if (p) memcpy(&v, p, 4);
    return v;
}
/* la position de la caméra courante : le singleton 0x020CF430 (0x0206C790) la tient en +0x10C, et la
   classe Camera range sa position en +0x18 (0x02066700) */
static void camera_oeil(double oeil[3]) {
    uint32_t camera = lire_mot(0x020CF430u + 0x10Cu);
    for (int j = 0; j < 3; j++)
        oeil[j] = camera ? (int32_t)lire_mot(camera + 0x18u + 4u * (uint32_t)j) / 4096.0 : 0.0;
}

void rt_diag_objets(uint64_t frame) {
    {   /* SFC_DIAG_CAMERA=n : la position de la caméra toutes les n images — un témoin de la partie qui ne
           dépend ni des adresses ni de ce qui est dessiné, pour dater l'image où deux parties divergent */
        static long pas = -1;
        if (pas < 0) { const char *e = getenv("SFC_DIAG_CAMERA"); pas = e ? atol(e) : 0; }
        if (pas > 0 && frame % (uint64_t)pas == 0) {
            double oeil[3];
            camera_oeil(oeil);
            rt_log("caméra : image %llu en %.2f %.2f %.2f", (unsigned long long)frame, oeil[0], oeil[1], oeil[2]);
        }
    }
    static const char *liste = (const char *)-1;
    if (liste == (const char *)-1) liste = getenv("SFC_DIAG_OBJETS");
    if (!liste) return;
    int voulu = 0;
    for (const char *p = liste; *p;) {
        char *fin;
        unsigned long long v = strtoull(p, &fin, 0);
        if (fin == p) break;
        if (v == frame) voulu = 1;
        p = *fin == ',' ? fin + 1 : fin;
        if (!*fin) break;
    }
    if (!voulu) return;
    const uint32_t tete = 0x027E14D8u;
    {
        double oeil[3];
        camera_oeil(oeil);
        rt_log("objet : image %llu, caméra en %.1f %.1f %.1f", (unsigned long long)frame, oeil[0], oeil[1], oeil[2]);
    }
    int n = 0;
    for (uint32_t noeud = lire_mot(tete + 8); noeud && noeud != tete + 4 && n < 400; noeud = lire_mot(noeud + 4), n++) {
        uint32_t objet = noeud - 0x4Cu;
        int32_t x = (int32_t)lire_mot(objet + 0x10), y = (int32_t)lire_mot(objet + 0x14), z = (int32_t)lire_mot(objet + 0x18);
        rt_log("objet : image %llu, n°%d 0x%08X (table 0x%08X) en %.1f %.1f %.1f",
               (unsigned long long)frame, n, objet, lire_mot(objet), x / 4096.0, y / 4096.0, z / 4096.0);
    }
}

/* SFC_FIND_VALUE=0x… : balaie la RAM principale toutes les SFC_FIND_EVERY images (32 par défaut)
   et signale les adresses qui contiennent cette valeur — pour savoir quand une valeur aberrante
   apparaît, quand elle n'est écrite ni par une instruction 32 bits ni par une instruction connue. */
void rt_find_value(uint64_t frame) {
    static int init, every;
    static uint32_t want;
    static uint32_t known[64];
    static int known_n;
    if (!init) {
        init = 1;
        const char *v = getenv("SFC_FIND_VALUE");
        want = v ? (uint32_t)strtoul(v, NULL, 0) : 0;
        const char *e = getenv("SFC_FIND_EVERY");
        every = e ? atoi(e) : 32;
        if (every <= 0) every = 32;
    }
    static uint64_t from;
    static int from_read;
    if (!from_read) { from_read = 1; const char *f = getenv("SFC_FIND_FROM"); from = f ? strtoull(f, NULL, 0) : 0; }
    if (!want || frame < from || (frame % (uint64_t)every)) return;
    struct { const uint8_t *p; uint32_t base, size; } zones[] = {
        { g_main_ram, 0x02000000u, 0x400000u },
        { g_dtcm,     0x027E0000u, 0x4000u },
        { g_itcm,     0x01FF8000u, 0x8000u },
        { g_vram,     0x06800000u, 0x80000u },
    };
    for (size_t z = 0; z < sizeof zones / sizeof zones[0]; z++)
    for (uint32_t off = 0; zones[z].p && off + 4 <= zones[z].size; off += 4) {
        uint32_t v;
        memcpy(&v, zones[z].p + off, 4);
        if (v != want) continue;
        uint32_t addr = zones[z].base + off;
        int seen = 0;
        for (int i = 0; i < known_n; i++) if (known[i] == addr) { seen = 1; break; }
        if (seen) continue;
        if (known_n < 64) known[known_n++] = addr;
        rt_log("TROUVÉ 0x%08X en 0x%08X (image %llu)", want, addr, (unsigned long long)frame);
    }
}

/* SFC_RAM_IMAGES=image,image… : la mémoire principale entière (4 Mo) à la fin de ces images, dans
   <SFC_RAM_PREFIXE><image>.bin (build/essai/ram_ par défaut). Deux relevés que seule une commande
   sépare — un stylet décalé, un bouton tenu — disent où le jeu range ce qu'il en fait. */
void rt_ram_images(uint64_t frame) {
    static const char *liste = (const char *)1, *prefixe;
    if (liste == (const char *)1) {
        liste = getenv("SFC_RAM_IMAGES");
        prefixe = getenv("SFC_RAM_PREFIXE");
        if (!prefixe) prefixe = "build/essai/ram_";
    }
    if (!liste) return;
    char image[24];
    snprintf(image, sizeof image, "%llu", (unsigned long long)frame);
    const size_t n = strlen(image);
    for (const char *q = strstr(liste, image); q; q = strstr(q + 1, image)) {
        if ((q != liste && q[-1] != ',') || (q[n] && q[n] != ',')) continue;
        char nom[512];
        snprintf(nom, sizeof nom, "%s%s.bin", prefixe, image);
        FILE *f = fopen(nom, "wb");
        if (f) { fwrite(g_main_ram, 1, 0x400000, f); fclose(f); }
        else rt_log("SFC_RAM_IMAGES : impossible d'écrire %s", nom);
        break;
    }
}

/* SFC_2D_IMAGES=image,image… : ce que composent les deux moteurs 2D à la fin de ces images, sans fenêtre, dans
   <SFC_RAM_PREFIXE>2d_A_<image>.ppm et 2d_B_… — les couches sous la 3D, puis celles du dessus par-dessus, sans la
   3D elle-même. Sépare un défaut de la composition 2D d'un défaut de l'affichage. */
void rt_2d_images(uint64_t frame) {
    static const char *liste = (const char *)1, *prefixe;
    if (liste == (const char *)1) {
        liste = getenv("SFC_2D_IMAGES");
        prefixe = getenv("SFC_RAM_PREFIXE");
        if (!prefixe) prefixe = "build/essai/ram_";
    }
    if (!liste) return;
    char image[24];
    snprintf(image, sizeof image, "%llu", (unsigned long long)frame);
    const size_t n = strlen(image);
    for (const char *q = strstr(liste, image); q; q = strstr(q + 1, image)) {
        if ((q != liste && q[-1] != ',') || (q[n] && q[n] != ',')) continue;
        static Engine2dOutput sortie;
        for (int m = 0; m < 2; m++) {
            video2d_render(m, &sortie);
            char nom[512];
            snprintf(nom, sizeof nom, "%s2d_%c_%s.ppm", prefixe, 'A' + m, image);
            FILE *f = fopen(nom, "wb");
            if (!f) { rt_log("SFC_2D_IMAGES : impossible d'écrire %s", nom); continue; }
            fprintf(f, "P6\n%d %d\n255\n", DS_W, DS_H);
            for (int i = 0; i < DS_W * DS_H; i++) {
                uint32_t c = sortie.below[i], d = sortie.above[i];
                if (d >> 24) c = d;
                fputc((int)(c & 0xFF), f); fputc((int)((c >> 8) & 0xFF), f); fputc((int)((c >> 16) & 0xFF), f);
            }
            fclose(f);
            rt_log("SFC_2D_IMAGES : %s (mode d'affichage %d)", nom, sortie.display_mode);
        }
        break;
    }
}

/* ------------------------------------------------------------------ invariant de pile */
uint64_t hw_frames(void);
int g_check_sp;

/* SFC_CHECK_SP=1 : un appel (bl/blx) doit rendre sp intact ; sinon la fonction appelée (ou un
   morceau pris pour une fonction, ou l'inverse) a déséquilibré la pile : on nomme le retour attendu,
   l'écart et l'état — les dix premières fois. */
void rt_sp_mismatch(Cpu *c, uint32_t ret, uint32_t sp_before) {
    static int n;
    if (n++ >= 10) return;
    rt_log("PILE : appel dont le retour est 0x%08X : sp attendu 0x%08X, obtenu 0x%08X (écart %+d) ; pc=0x%08X lr=0x%08X image %llu",
           ret, sp_before, c->r[13], (int)(c->r[13] - sp_before), c->r[15], c->r[14], (unsigned long long)hw_frames());
    rt_dump_dispatch();
}

/* ------------------------------------------------------------------ overlays et dispatch */
static const OverlayInfo *g_overlays;
static size_t g_overlay_count;

void rt_set_overlays(const OverlayInfo *overlays, size_t count) {
    g_check_sp = getenv("SFC_CHECK_SP") != NULL;
    { const char *w = getenv("SFC_WATCH_VALUE"); g_watch_value = w ? (uint32_t)strtoul(w, NULL, 0) : 0; }
    g_overlays = overlays;
    g_overlay_count = count;
}

/* Quel overlay occupe l'adresse ? Plusieurs se partagent un emplacement (dix-huit pour 5-22).
   On compare avec la RAM 32 échantillons de 16 octets répartis sur toute l'image : ni quelques
   mots à l'adresse demandée (deux fonctions au même prologue se confondent), ni le seul début
   (les overlays 9 et 19 commencent pareil) ni la fin (zéros de part et d'autre). */
/* Le jeu demande ses overlays au lecteur par leur place dans la cartouche : reconnaître
   cette place, c'est savoir lequel il charge, et quand. */
int rt_overlay_at_rom(uint32_t offset) {
    for (size_t i = 0; i < g_overlay_count; i++)
        if (g_overlays[i].rom_start == offset) return g_overlays[i].id;
    return -1;
}

/* La recherche est celle de toujours — le premier overlay de la table dont les trente-deux échantillons sont
   en RAM —, mais elle revient à chaque appel indirect vers un overlay, un millier de fois par image. Deux
   choses la ralentissaient sans rien apporter à sa réponse :
   — chaque échantillon passait par mem_resolve, alors que les overlays vivent en RAM principale ;
   — l'overlay présent, presque toujours celui de la fois précédente, n'était reconnu qu'après l'examen de
     tous ceux qui le précèdent dans la table.
   Les échantillons se lisent donc directement, et l'on essaie d'abord les derniers reconnus. L'un d'eux
   n'est rendu d'emblée que si aucun overlay placé avant lui dans la table ne peut être reconnu en même
   temps que lui : il faut, pour chacun, un octet que tous deux échantillonnent et qu'ils n'ont pas pareil.
   Alors la recherche dans l'ordre l'aurait rendu, lui ; sinon, on la fait. */
typedef struct {
    const uint8_t *ram;          /* l'overlay en RAM principale ; NULL : hors d'elle, on passe par mem_resolve */
    uint32_t off[32], n[32];     /* les échantillons */
    int exclusif;                /* aucun overlay avant lui dans la table ne peut être là en même temps */
} OverlayEchantillons;
static OverlayEchantillons *g_ovl_ech;
static int g_ovl_recents[4] = {-1, -1, -1, -1};     /* les derniers reconnus, le plus récent d'abord */

static inline int octets_egaux(const uint8_t *a, const uint8_t *b, uint32_t n) {
    if (n == 16) {
        uint64_t a0, a1, b0, b1;
        memcpy(&a0, a, 8); memcpy(&a1, a + 8, 8); memcpy(&b0, b, 8); memcpy(&b1, b + 8, 8);
        return ((a0 ^ b0) | (a1 ^ b1)) == 0;
    }
    return memcmp(a, b, n) == 0;
}

static int ovl_present(size_t i) {
    const OverlayInfo *o = &g_overlays[i];
    const OverlayEchantillons *e = &g_ovl_ech[i];
    if (!o->image) return 0;
    for (int k = 0; k < 32; k++) {
        const uint8_t *ram = e->ram ? e->ram + e->off[k] : mem_resolve(o->ram_address + e->off[k], e->n[k]);
        if (!ram || !octets_egaux(ram, o->image + e->off[k], e->n[k])) return 0;
    }
    return 1;
}

/* j ne peut être reconnu quand i l'est : un octet échantillonné par les deux, et différent de l'un à l'autre */
static int ovl_s_excluent(size_t i, size_t j) {
    const OverlayInfo *oi = &g_overlays[i], *oj = &g_overlays[j];
    if (!oi->image || !oj->image) return 1;
    for (int k = 0; k < 32; k++) {
        uint32_t a0 = oi->ram_address + g_ovl_ech[i].off[k], a1 = a0 + g_ovl_ech[i].n[k];
        for (int l = 0; l < 32; l++) {
            uint32_t b0 = oj->ram_address + g_ovl_ech[j].off[l], b1 = b0 + g_ovl_ech[j].n[l];
            for (uint32_t a = a0 > b0 ? a0 : b0; a < (a1 < b1 ? a1 : b1); a++)
                if (oi->image[a - oi->ram_address] != oj->image[a - oj->ram_address]) return 1;
        }
    }
    return 0;
}

static void ovl_echantillons_init(void) {
    g_ovl_ech = calloc(g_overlay_count ? g_overlay_count : 1, sizeof *g_ovl_ech);
    for (size_t i = 0; i < g_overlay_count; i++) {
        const OverlayInfo *o = &g_overlays[i];
        OverlayEchantillons *e = &g_ovl_ech[i];
        for (uint32_t k = 0; k < 32; k++) {
            e->off[k] = (uint32_t)(((uint64_t)o->ram_size * k / 32) & ~3u);
            e->n[k] = o->ram_size - e->off[k];
            if (e->n[k] > 16) e->n[k] = 16;
        }
        /* la RAM principale hors de la fenêtre DTCM : ce que mem_resolve y rendrait */
        if (o->ram_address >= 0x02000000u && (uint64_t)o->ram_address + o->ram_size <= 0x02400000u)
            e->ram = g_main_ram + (o->ram_address - 0x02000000u);
    }
    int exclusifs = 0;
    for (size_t i = 0; i < g_overlay_count; i++) {
        const OverlayInfo *oi = &g_overlays[i];
        int ex = 1;
        for (size_t j = 0; j < i && ex; j++) {
            const OverlayInfo *oj = &g_overlays[j];
            int chevauchent = oj->ram_address < oi->ram_address + oi->ram_size && oi->ram_address < oj->ram_address + oj->ram_size;
            if (chevauchent && !ovl_s_excluent(i, j)) ex = 0;
        }
        g_ovl_ech[i].exclusif = ex;
        exclusifs += ex;
    }
    rt_log("overlays : %d sur %zu reconnaissables sans examiner ceux qui les précèdent", exclusifs, g_overlay_count);
}

int rt_overlay_loaded(uint32_t addr) {
    if (!g_ovl_ech) ovl_echantillons_init();
    for (int r = 0; r < 4; r++) {
        int i = g_ovl_recents[r];
        if (i < 0) break;
        const OverlayInfo *o = &g_overlays[i];
        if (addr < o->ram_address || addr >= o->ram_address + o->ram_size || !g_ovl_ech[i].exclusif) continue;
        if (ovl_present((size_t)i)) return o->id;
    }
    for (size_t i = 0; i < g_overlay_count; i++) {
        const OverlayInfo *o = &g_overlays[i];
        if (addr < o->ram_address || addr >= o->ram_address + o->ram_size) continue;
        if (ovl_present(i)) {
            /* en tête des récents */
            int r = 0;
            while (r < 3 && g_ovl_recents[r] != (int)i) r++;
            for (; r > 0; r--) g_ovl_recents[r] = g_ovl_recents[r - 1];
            g_ovl_recents[0] = (int)i;
            return o->id;
        }
    }
    return -1;
}

/* Pose en mémoire l'image d'un overlay, comme le jeu le ferait en le chargeant. Le banc à
   froid s'en sert pour essayer les fonctions d'un overlay que la partie n'a pas chargé : leur
   traduction lit ses constantes dans le code, il faut donc qu'il soit là. Rend 0 si la région
   n'est pas un overlay connu. */
int rt_overlay_installer(const char *region) {
    if (strncmp(region, "ovl", 3) != 0) return 1;
    int id = atoi(region + 3);
    for (size_t i = 0; i < g_overlay_count; i++) {
        const OverlayInfo *o = &g_overlays[i];
        if (o->id != id) continue;
        uint8_t *ram = mem_resolve(o->ram_address, o->ram_size);
        if (!ram) return 0;
        memcpy(ram, o->image, o->ram_size);
        memset(ram + o->ram_size, 0, o->bss_size);
        return 1;
    }
    return 0;
}

/* La fonction d'une région est-elle en mémoire à cette adresse ? Celles de l'ARM9 et de l'ITCM y sont
   toujours ; celle d'un overlay, si c'est lui qui est chargé. L'overlay chargé ne dépend que de la mémoire,
   qui ne change pas pendant qu'on cherche : on le calcule une fois, au premier candidat qui le demande, au
   lieu d'une fois par candidat — ils sont jusqu'à dix-huit quand dix-huit overlays se partagent la place.
   `*charge` vaut -2 tant qu'on ne l'a pas calculé. */
static int region_matches_une_fois(const char *region, uint32_t addr, int *charge) {
    if (strncmp(region, "ovl", 3) != 0) return 1;      /* arm9, itcm : toujours présents */
    if (*charge == -2) *charge = rt_overlay_loaded(addr);
    return *charge == atoi(region + 3);
}

/* Chaque processeur a sa table de fonctions traduites et son index trié par adresse de début
 * (la table elle-même suit l'ordre des indices, qui est celui des crochets natifs).
 *
 * Et un index des intervalles : l'espace d'adresses découpé à chaque début et chaque fin de fonction, et
 * pour chaque tranche les fonctions qui la couvrent, dans l'ordre où la recherche les examinait — du
 * début le plus proche au plus lointain, comme la remontée de `sorted`. La recherche remontait jusqu'à un
 * mégaoctet de fonctions pour chaque appel indirect ; elle ne voit plus que celles qui couvrent l'adresse.
 * Même candidats, même ordre, même choix. */
typedef struct {
    const RecompEntry *table; size_t size; NativeHook *hooks; uint32_t *sorted;
    uint32_t *tranche_debut;      /* nb_tranches + 1 bornes croissantes */
    uint32_t *tranche_premier;    /* nb_tranches + 1 : où commence la liste de chaque tranche dans `couvrent` */
    uint32_t *couvrent;           /* indices dans `table` */
    size_t nb_tranches;
    int sans_index;               /* une fonction de plus d'un mégaoctet : on garde la remontée d'origine */
} Dispatch;
static Dispatch g_dispatch[2];
static void dispatch_init(void) {
    g_dispatch[0] = (Dispatch){g_recomp_table, g_recomp_table_size, g_fn_hooks, NULL, NULL, NULL, NULL, 0, 0};
#ifdef SFC_HAVE_ARM7
    g_dispatch[1] = (Dispatch){g_recomp7_table, g_recomp7_table_size, g_fn7_hooks, NULL, NULL, NULL, NULL, 0, 0};
#endif
}
static const Dispatch *g_cmp_d;
static int cmp_entries(const void *a, const void *b) {
    uint32_t sa = g_cmp_d->table[*(const uint32_t *)a].start, sb = g_cmp_d->table[*(const uint32_t *)b].start;
    return sa < sb ? -1 : sa > sb;
}
static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static void index_intervalles(Dispatch *d) {
    /* les bornes : chaque début et chaque fin, triés, sans doublon */
    uint32_t *bornes = malloc(2 * d->size * sizeof *bornes);
    size_t nb = 0;
    for (size_t i = 0; i < d->size; i++) {
        if (d->table[i].end - d->table[i].start > 0x100000u) d->sans_index = 1;
        bornes[nb++] = d->table[i].start;
        bornes[nb++] = d->table[i].end;
    }
    if (d->sans_index) {
        free(bornes);
        rt_log("dispatch : une fonction de plus d'un mégaoctet, pas d'index des intervalles");
        return;
    }
    qsort(bornes, nb, sizeof *bornes, cmp_u32);
    size_t u = 0;
    for (size_t i = 0; i < nb; i++) if (!u || bornes[u - 1] != bornes[i]) bornes[u++] = bornes[i];
    size_t nt = u ? u - 1 : 0;                           /* tranches [bornes[k], bornes[k+1]) */
    /* compter, puis ranger : pour chaque fonction, dans l'ordre inverse de `sorted` (la remontée), les
       tranches qu'elle couvre */
    uint32_t *compte = calloc(nt + 1, sizeof *compte);
    for (size_t r = d->size; r > 0; r--) {
        const RecompEntry *e = &d->table[d->sorted[r - 1]];
        size_t k = (size_t)((uint32_t *)bsearch(&e->start, bornes, u, sizeof *bornes, cmp_u32) - bornes);
        for (; k < nt && bornes[k] < e->end; k++) compte[k]++;
    }
    uint32_t *premier = malloc((nt + 1) * sizeof *premier);
    uint32_t total = 0;
    for (size_t k = 0; k < nt; k++) { premier[k] = total; total += compte[k]; }
    premier[nt] = total;
    uint32_t *couvrent = malloc((total ? total : 1) * sizeof *couvrent);
    memset(compte, 0, (nt + 1) * sizeof *compte);
    for (size_t r = d->size; r > 0; r--) {
        uint32_t idx = d->sorted[r - 1];
        const RecompEntry *e = &d->table[idx];
        size_t k = (size_t)((uint32_t *)bsearch(&e->start, bornes, u, sizeof *bornes, cmp_u32) - bornes);
        for (; k < nt && bornes[k] < e->end; k++) couvrent[premier[k] + compte[k]++] = idx;
    }
    free(compte);
    {   /* ce que coûte l'index, et combien de candidats une recherche voit en moyenne et au plus */
        uint32_t pire = 0;
        for (size_t k = 0; k < nt; k++) if (premier[k + 1] - premier[k] > pire) pire = premier[k + 1] - premier[k];
        rt_log("dispatch : index de %zu tranches, %u entrées (%.1f par tranche, %u au plus) pour %zu fonctions",
               nt, total, nt ? (double)total / (double)nt : 0.0, pire, d->size);
    }
    d->tranche_debut = bornes;
    d->tranche_premier = premier;
    d->couvrent = couvrent;
    d->nb_tranches = nt;
}

static Dispatch *dispatch_for(const Cpu *c) {
    if (!g_dispatch[0].table) dispatch_init();
    Dispatch *d = &g_dispatch[c->is_arm7 ? 1 : 0];
    if (!d->sorted && d->table) {
        d->sorted = malloc(d->size * sizeof *d->sorted);
        for (size_t i = 0; i < d->size; i++) d->sorted[i] = (uint32_t)i;
        g_cmp_d = d;
        qsort(d->sorted, d->size, sizeof *d->sorted, cmp_entries);
        index_intervalles(d);
    }
    return d;
}

/* La recherche elle-même. `*charge` rend l'overlay présent à l'adresse s'il a fallu le connaître, -2 sinon :
   c'est de lui seul, avec l'adresse, que dépend la réponse. */
static const RecompEntry *rt_find_calcul(Cpu *c, Dispatch *d, uint32_t addr, uint32_t thumb, int *charge_) {
    int charge = -2;
    if (!d->sans_index) {
        /* la tranche qui contient l'adresse, puis ses fonctions dans l'ordre de la remontée */
        if (!d->nb_tranches || addr < d->tranche_debut[0] || addr >= d->tranche_debut[d->nb_tranches]) { *charge_ = -2; return NULL; }
        size_t bas = 0, haut = d->nb_tranches;
        while (bas + 1 < haut) {
            size_t milieu = (bas + haut) / 2;
            if (d->tranche_debut[milieu] <= addr) bas = milieu; else haut = milieu;
        }
        const RecompEntry *best = NULL;
        for (uint32_t k = d->tranche_premier[bas]; k < d->tranche_premier[bas + 1]; k++) {
            const RecompEntry *e = &d->table[d->couvrent[k]];
            if (!c->is_arm7 && !region_matches_une_fois(e->region, addr, &charge)) continue;
            if (e->entry == addr && e->thumb == thumb) { best = e; break; }
            if (best == NULL || (e->thumb == thumb && best->thumb != thumb)) best = e;
        }
        *charge_ = charge;
        return best;
    }
    /* plusieurs overlays se chevauchent : recherche linéaire bornée par une dichotomie sur le début */
    size_t lo = 0, hi = d->size;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (d->table[d->sorted[mid]].start <= addr) lo = mid + 1; else hi = mid;
    }
    const RecompEntry *best = NULL;
    for (size_t i = lo; i > 0; i--) {
        const RecompEntry *e = &d->table[d->sorted[i - 1]];
        if (e->start > addr) continue;
        if (addr >= e->end) {
            if (e->start + 0x100000 < addr) break;   /* trop loin en arrière : fini */
            continue;
        }
        if (!c->is_arm7 && !region_matches_une_fois(e->region, addr, &charge)) continue;
        /* plusieurs candidats (mode ARM et Thumb décodés au même endroit) : celui du bon mode */
        if (e->entry == addr && e->thumb == thumb) { best = e; break; }
        if (best == NULL || (e->thumb == thumb && best->thumb != thumb)) best = e;
    }
    *charge_ = charge;
    return best;
}

/* Les réponses gardées. La table des fonctions ne change jamais : la réponse pour une adresse ne dépend que
   d'elle — et, dans un emplacement d'overlays, de l'overlay présent. On la garde donc, par adresse, dans une
   table à correspondance directe, avec l'overlay qu'il a fallu connaître ; elle ne resert que s'il est
   toujours le même. Un mot de 64 bits par case — l'adresse avec son bit Thumb, la fonction, l'overlay —,
   écrit et lu d'un seul coup. Neuf appels sur dix viennent de l'attente active du jeu, qui relit son
   horloge des milliers de fois par image. */
#define FIND_BITS 13
static uint64_t g_find_cache[2][1u << FIND_BITS];

const RecompEntry *rt_find(Cpu *c, uint32_t addr) {
    uint32_t cle = addr, thumb = addr & 1u;
    addr &= ~1u;
    Dispatch *d = dispatch_for(c);
    if (!d->table) return NULL;
    uint64_t *case_ = &g_find_cache[c->is_arm7 ? 1 : 0][(cle * 2654435761u) >> (32 - FIND_BITS)];
    uint64_t v = __atomic_load_n(case_, __ATOMIC_RELAXED);
    uint32_t code = (uint32_t)(v >> 32) & 0xFFFFFFu, dep = (uint32_t)(v >> 56);
    if ((uint32_t)v == cle && code && (!dep || rt_overlay_loaded(addr) + 2 == (int)dep))
        return code == 1 ? NULL : &d->table[code - 2];
    int charge = -2;
    const RecompEntry *e = rt_find_calcul(c, d, addr, thumb, &charge);
    code = e ? (uint32_t)(e - d->table) + 2u : 1u;
    __atomic_store_n(case_, (uint64_t)cle | (uint64_t)code << 32 | (uint64_t)(uint32_t)(charge + 2) << 56, __ATOMIC_RELAXED);
    return e;
}

/* ------------------------------------------------------------------ crochets natifs */
/* Un crochet posé sur une fonction traduite est appelé depuis le corps de celle-ci, qui ne
   lui dit pas quelle adresse il remplace. Pour qu'un crochet puisse exécuter la fonction
   d'origine — c'est ce que fait le banc de vérification — il faut pouvoir l'ôter le temps de
   cet appel : on rend donc les emplacements eux-mêmes, à écrire directement. */
/* Un crochet posé sur une seule région. Plusieurs overlays se partagent les mêmes adresses et
   y logent des fonctions qui n'ont rien à voir : un crochet posé par adresse seule remplacerait
   aussi celle du voisin. Celui-ci ne touche que la fonction traduite de la région nommée, et
   n'entre pas dans la table des crochets par adresse — le dispatch passe par la fonction
   traduite, qui consulte son propre emplacement. Rend cet emplacement, ou NULL. */
/* La région « arm7 » est celle du coprocesseur : sa table, ses crochets. */
NativeHook *rt_hook_region(const char *region, uint32_t addr, NativeHook fn) {
    Cpu sonde = {0};
    sonde.is_arm7 = !strcmp(region, "arm7");
    Dispatch *d = dispatch_for(&sonde);
    addr &= ~1u;
    for (size_t i = 0; d->table && i < d->size; i++)
        if (d->table[i].entry == addr && !strcmp(d->table[i].region, region)) {
            d->hooks[i] = fn;
            return &d->hooks[i];
        }
    return NULL;
}

/* Le crochet déjà posé sur la fonction d'une région, s'il y en a un : le runtime remplace
   quelques fonctions du SDK par les siennes — la commutation de contexte des threads — et
   celles-là ne sont ni à réécrire ni à juger contre leur traduction, qui ne tourne jamais. */
NativeHook rt_hook_present(const char *region, uint32_t addr) {
    Cpu sonde = {0};
    sonde.is_arm7 = !strcmp(region, "arm7");
    Dispatch *d = dispatch_for(&sonde);
    addr &= ~1u;
    for (size_t i = 0; d->table && i < d->size; i++)
        if (d->table[i].entry == addr && !strcmp(d->table[i].region, region)) return d->hooks[i];
    return NULL;
}

/* La fonction traduite d'une région à une adresse, telle quelle : le banc l'appelle par là,
   sans passer par rt_find, qui choisirait d'après le mode courant du processeur — et une
   fonction ARM appelée depuis du Thumb tourne avec le bit T de son appelant. */
const RecompEntry *rt_entree_region(const char *region, uint32_t addr) {
    Cpu sonde = {0};
    sonde.is_arm7 = !strcmp(region, "arm7");
    Dispatch *d = dispatch_for(&sonde);
    addr &= ~1u;
    for (size_t i = 0; d->table && i < d->size; i++)
        if (d->table[i].entry == addr && !strcmp(d->table[i].region, region)) return &d->table[i];
    return NULL;
}

size_t rt_hook_emplacements(uint32_t addr, NativeHook **out, size_t max) {
    Cpu sonde = {0};
    Dispatch *d = dispatch_for(&sonde);
    size_t n = 0;
    for (size_t i = 0; d->table && i < d->size && n < max; i++)
        if (d->table[i].entry == (addr & ~1u)) out[n++] = &d->hooks[i];
    return n;
}
typedef struct { uint32_t addr; NativeHook fn; const char *name; } Hook;
static Hook g_hooks[2][1024];
static size_t g_hook_count[2];

/* `arm7` : le crochet vise la traduction du coprocesseur, dont les adresses sont les siennes. */
static void hook_on(int arm7, uint32_t addr, NativeHook fn, const char *name) {
    addr &= ~1u;
    Cpu probe = {0};
    probe.is_arm7 = (uint32_t)arm7;
    Dispatch *d = dispatch_for(&probe);
    /* toutes les fonctions générées à cette entrée (plusieurs overlays possibles) */
    int found = 0;
    for (size_t i = 0; d->table && i < d->size; i++)
        if (d->table[i].entry == addr) { d->hooks[i] = fn; found = 1; }
    if (!found) rt_log("hook %s : aucune fonction générée à 0x%08X (dispatch seulement)", name, addr);
    for (size_t i = 0; i < g_hook_count[arm7]; i++)
        if (g_hooks[arm7][i].addr == addr) { g_hooks[arm7][i].fn = fn; g_hooks[arm7][i].name = name; return; }
    if (g_hook_count[arm7] < 1024)
        g_hooks[arm7][g_hook_count[arm7]++] = (Hook){addr, fn, name};
}

/* SFC_DIAG_ANNEAU=1 : remplace le test de passage d'un anneau par une version native qui
 * journalise la valeur comparée. Le test d'origine tient en cinq instructions — il compare un
 * champ de l'objet au seuil 0x1000 — et se reproduit donc à l'identique. C'est le seul moyen
 * d'observer une fonction appelée directement : la trace par adresse ne voit que les appels
 * indirects, et un hook remplace la fonction au lieu de l'accompagner. */
static void diag_anneau_test(Cpu *c) {
    uint32_t base = c->r[0];
    uint32_t champ = mem_read32(c, base + 0x14);
    static uint64_t n;
    static uint32_t vu_max;
    if (champ > vu_max) vu_max = champ;
    if (n++ % 300 == 0 || champ > 0) {
        /* l'état du calcul est en +4 ; les positions suivent. Tout à zéro veut dire que le
           calcul n'a jamais rien produit ; un état figé dit où il s'arrête. */
        /* L'objet commence douze octets avant ce que reçoit le test ; on remonte donc, et on
           lit assez loin pour attraper sa position — c'est elle qu'il faut comparer à celle du
           joueur pour comprendre pourquoi la proximité n'est jamais constatée. */
        /* La position de l'objet vit à l'offset 0x168 : son accesseur ne fait que rendre
           cette adresse. Les coordonnées sont en 4.12, comme tout le reste du monde. */
        /* Les coordonnées du monde sont en 4.12 et se comptent en milliers d'unités : on vide
           la zone en clair pour reconnaître le triplet, plutôt que de parier sur un offset. */
        uint32_t obj = base - 0xC;
        for (int bloc = 0; bloc < 2; bloc++) {
            char l[240] = {0};
            for (int i = 0; i < 8; i++) {
                int32_t v = (int32_t)mem_read32(c, obj + 0x150 + (bloc * 8 + i) * 4);
                snprintf(l + strlen(l), sizeof l - strlen(l), " %+.1f", v / 4096.0);
            }
            rt_log("anneau 0x%08X +%03X :%s", obj, 0x150 + bloc * 32, l);
        }
    }
    c->r[0] = (int32_t)champ > 0x1000 ? 1u : 0u;
}

/* L'accesseur de position : il appelle une fonction interne puis rend l'adresse du triplet,
 * à l'offset 0x168 de l'objet. On le reproduit à l'identique — l'appel interne compris, sans
 * quoi la position ne serait pas à jour — et l'on journalise ce qu'il rend quand il est appelé
 * depuis le calcul de proximité de l'anneau. C'est ainsi qu'on voit les deux positions
 * comparées, que la mémoire seule ne livrait pas. */
static void diag_position(Cpu *c) {
    uint32_t obj = c->r[0];
    uint32_t lr = c->r[14];
    rt_call(c, 0x0216CBA1);            /* la fonction interne, avec r0 = l'objet */
    c->r[0] = obj + 0x168;
    /* 0x02265324 et 0x0226532C sont les deux retours dans le calcul de proximité */
    if ((lr & ~1u) == 0x02265324u || (lr & ~1u) == 0x0226532Cu) {
        static uint64_t n;
        if (n++ < 120) {
            int32_t x = (int32_t)mem_read32(c, obj + 0x168);
            int32_t y = (int32_t)mem_read32(c, obj + 0x16C);
            int32_t z = (int32_t)mem_read32(c, obj + 0x170);
            rt_log("position : objet 0x%08X (appelé de %08X) -> (%.1f, %.1f, %.1f)",
                   obj, lr, x / 4096.0, y / 4096.0, z / 4096.0);
        }
    }
}

void rt_diag_hooks(void) {
    if (!getenv("SFC_DIAG_ANNEAU")) return;
    rt_hook(0x0226529C, diag_anneau_test, "test de passage d'anneau");
    rt_hook(0x0216CB64, diag_position, "accesseur de position");
}

void rt_hook(uint32_t addr, NativeHook fn, const char *name) { hook_on(0, addr, fn, name); }
void rt_hook7(uint32_t addr, NativeHook fn, const char *name) { hook_on(1, addr, fn, name); }

static NativeHook hook_for_on(int arm7, uint32_t addr) {
    addr &= ~1u;
    for (size_t i = 0; i < g_hook_count[arm7]; i++)
        if (g_hooks[arm7][i].addr == addr) return g_hooks[arm7][i].fn;
    return NULL;
}

NativeHook rt_hook_for(uint32_t addr) { return hook_for_on(0, addr); }

/* ------------------------------------------------------------------ appels et sauts */
/* les derniers dispatchs indirects (cible, lr, sp), pour comprendre une faute */
static struct { uint32_t target, lr, sp; } g_trace_ring[64];
static unsigned g_trace_pos;

void rt_dump_dispatch(void) {
    fprintf(stderr, "  derniers dispatchs indirects (cible <- lr, sp) :\n");
    for (unsigned i = 0; i < 64; i++) {
        unsigned k = (g_trace_pos + i) % 64;
        if (g_trace_ring[k].target)
            fprintf(stderr, "    %08X <- %08X (sp %08X)\n", g_trace_ring[k].target, g_trace_ring[k].lr, g_trace_ring[k].sp);
    }
}

/* SFC_TRACE_CALL=0xadresse[,0xadresse…] : journalise chaque appel de ces fonctions, avec
 * l'image et les premiers arguments. Sert à savoir si le jeu passe seulement par un endroit —
 * par exemple le rappel d'un objet dont l'effet ne se produit pas. */
static int trace_call_wanted(uint32_t target) {
    static int init;
    static uint32_t want[8];
    static int n;
    if (!init) {
        init = 1;
        const char *v = getenv("SFC_TRACE_CALL");
        while (v && *v && n < 8) {
            char *end;
            want[n++] = (uint32_t)strtoul(v, &end, 0) & ~1u;
            if (*end != ',') break;
            v = end + 1;
        }
    }
    uint32_t t = target & ~1u;
    for (int i = 0; i < n; i++)
        if (want[i] == t) return 1;
    return 0;
}

/* Le journal porte les arguments à l'entrée et la valeur rendue à la sortie : savoir qu'une
   fonction est appelée ne suffit pas quand c'est son résultat qui décide de la suite. */
static void trace_call(Cpu *c, uint32_t target, uint32_t ret, int sortie) {
    static uint64_t vus, max;
    if (!max) {
        const char *e = getenv("SFC_TRACE_CALL_MAX");
        max = e ? strtoull(e, NULL, 0) : 400;
    }
    if (vus >= max) return;
    vus++;
    if (sortie)
        rt_log("  rend : 0x%08X -> %08X", target, ret);
    else
        rt_log("appel : 0x%08X (image %llu) r0=%08X r1=%08X r2=%08X r3=%08X",
               target, (unsigned long long)hw_frames(), c->r[0], c->r[1], c->r[2], c->r[3]);
}

/* SFC_STAT_CALL=n : à la fin, les n fonctions les plus souvent atteintes par un appel
 * indirect. Le profilage par échantillonnage dit où le temps passe ; ceci dit ce que le jeu
 * appelle, ce qui n'est pas la même question — une fonction peut être appelée sans cesse et
 * ne rien coûter, et c'est pourtant elle qu'il faut comprendre en premier. */
#define STAT_CALL_TAILLE 4096
static struct { uint32_t cible; uint64_t nombre; } g_stat_call[STAT_CALL_TAILLE];
static int g_stat_call_actif = -1;

static void stat_call_rapport(void) {
    const char *v = getenv("SFC_STAT_CALL");
    int combien = v ? atoi(v) : 0;
    if (combien <= 0) return;
    rt_log("appels indirects : les %d cibles les plus fréquentes", combien);
    for (int rang = 0; rang < combien; rang++) {
        size_t meilleur = 0;
        for (size_t i = 1; i < STAT_CALL_TAILLE; i++)
            if (g_stat_call[i].nombre > g_stat_call[meilleur].nombre) meilleur = i;
        if (!g_stat_call[meilleur].nombre) break;
        rt_log("   0x%08X : %llu", g_stat_call[meilleur].cible,
               (unsigned long long)g_stat_call[meilleur].nombre);
        g_stat_call[meilleur].nombre = 0;
    }
}

static void stat_call(uint32_t cible) {
    if (g_stat_call_actif < 0) {
        const char *v = getenv("SFC_STAT_CALL");
        g_stat_call_actif = v && atoi(v) > 0;
        if (g_stat_call_actif) atexit(stat_call_rapport);
    }
    if (!g_stat_call_actif) return;
    cible &= ~1u;
    size_t i = (cible * 2654435761u) % STAT_CALL_TAILLE;
    for (size_t essai = 0; essai < 16; essai++, i = (i + 1) % STAT_CALL_TAILLE) {
        if (!g_stat_call[i].nombre) { g_stat_call[i].cible = cible; g_stat_call[i].nombre = 1; return; }
        if (g_stat_call[i].cible == cible) { g_stat_call[i].nombre++; return; }
    }
}

/* Le banc à froid tient lieu de toutes les appelées : un même crochet — le bouchon — posé sur
   chaque fonction traduite, et pris aussi par le dispatch pour les appels par registre. */
NativeHook g_bouchon;

void rt_bouchon_partout(NativeHook fn) {
    for (uint32_t cpu = 0; cpu < 2; cpu++) {         /* les appelées des deux processeurs */
        Cpu sonde = {0};
        sonde.is_arm7 = cpu;
        Dispatch *d = dispatch_for(&sonde);
        for (size_t i = 0; d->table && i < d->size; i++) d->hooks[i] = fn;
    }
    g_bouchon = fn;
}

int (*g_dispatch_milieu)(Cpu *c, const RecompEntry *e, uint32_t adresse);

/* ------------------------------------------------------------------ l'étude des objets
 *
 * SFC_ETUDE_OBJETS=fichier : à chaque appel, on note la fonction et l'adresse qu'elle reçoit en
 * premier argument. Le code réécrit dit *quels décalages* une fonction touche à partir de ce
 * premier argument (tools/structures.py) ; ceci dit *sur quoi* elle les touche. Les deux mis
 * ensemble donnent des objets réels : telle plage de mémoire, tels champs, telles fonctions qui
 * s'en servent. C'est le dictionnaire dont toute modification du jeu a besoin.
 *
 * On garde peu : pour chaque fonction, huit adresses distinctes et le compte des appels. Les
 * adresses sont arrondies au bloc de 4 Ko pour reconnaître les objets d'un même tas.
 */
#define ETUDE_OBJETS_CASES (1u << 16)
typedef struct { uint32_t fn; uint64_t appels; uint32_t bases[8]; uint32_t n; uint32_t plage_min, plage_max; } EtudeObjet;
static EtudeObjet *g_etude_objets;
static const char *g_etude_objets_fichier;

/* L'autre sens : pour chaque objet, les fonctions qui l'ont reçu. Deux objets que les mêmes
   fonctions manipulent sont du même type — c'est ainsi qu'on les regroupe sans rien savoir d'eux.
   On garde seize fonctions par objet : au-delà, on a déjà de quoi reconnaître une famille. */
#define ETUDE_ADR_CASES (1u << 17)
#define ETUDE_CHAMPS 48          /* les quarante-huit premiers mots d'un objet : de quoi voir sa forme */
/* Ce qu'un champ contient, vu de loin : de quoi reconnaître une position d'un pointeur sans rien savoir
   du jeu. Un même champ peut porter plusieurs de ces marques au fil du temps. */
/* Pour chaque champ, combien de fois on l'a vu de chaque sorte : ce qui compte est la sorte qui
   domine, non l'ensemble de celles qu'il a un jour portées — un champ de position vaut parfois zéro,
   parfois un petit nombre, et reste une position. */
enum { CAT_ZERO, CAT_POINTEUR, CAT_PETIT, CAT_COORD, CAT_TEXTE, CAT_AUTRE, CAT_N };
typedef struct { uint32_t adr; uint64_t appels; uint32_t fns[16]; uint32_t n; uint8_t cat[ETUDE_CHAMPS][CAT_N]; } EtudeAdresse;

/* quatre octets lisibles d'affilée : c'est du texte, et non la grandeur qu'on y aurait vue */
static int etude_texte(uint32_t v) {
    for (int i = 0; i < 4; i++) {
        uint32_t o = (v >> (i * 8)) & 0xFFu;
        if (o < 0x20u || o > 0x7Eu) return 0;
    }
    return 1;
}

static int etude_categorie(uint32_t v) {
    if (v == 0) return CAT_ZERO;
    if (etude_texte(v)) return CAT_TEXTE;
    if (v >= 0x02000000u && v < 0x02400000u) return CAT_POINTEUR;
    if (v >= 0x027E0000u && v < 0x02800000u) return CAT_POINTEUR;
    int32_t sv = (int32_t)v;
    uint32_t abs = sv < 0 ? (uint32_t)(-sv) : (uint32_t)sv;
    if (abs < 0x10000u) return CAT_PETIT;
    /* 20.12 : une longueur du monde, avec une partie fractionnaire — une position, un angle, une vitesse */
    if (abs < (1u << 28) && (v & 0xFFFu) != 0) return CAT_COORD;
    return CAT_AUTRE;
}

/* la sorte qui domine ce champ, en une lettre */
static char etude_lettre(const uint8_t *cat) {
    static const char lettres[CAT_N] = {'.', 'p', 'n', 'x', 't', '?'};
    int mieux = 0, total = 0;
    for (int i = 0; i < CAT_N; i++) { total += cat[i]; if (cat[i] > cat[mieux]) mieux = i; }
    if (!total) return ' ';
    /* une sorte qui ne domine pas franchement ne dit rien */
    return cat[mieux] * 2 > total ? lettres[mieux] : '?';
}
static EtudeAdresse *g_etude_adr;

static void etude_adr_noter(Cpu *c, uint32_t fn, uint32_t adr) {
    uint32_t h = (adr * 2246822519u) >> 8 & (ETUDE_ADR_CASES - 1);
    for (uint32_t k = 0; k < 24; k++) {
        EtudeAdresse *e = &g_etude_adr[(h + k) & (ETUDE_ADR_CASES - 1)];
        if (e->adr == 0) e->adr = adr;
        if (e->adr != adr) continue;
        e->appels++;
        /* de loin en loin, on regarde ce que l'objet contient : c'est ce qui nomme ses champs */
        if ((e->appels & 0x3Fu) == 1u)
            for (uint32_t i = 0; i < ETUDE_CHAMPS; i++) {
                int cat = etude_categorie(mem_read32(c, adr + i * 4u));
                if (e->cat[i][cat] < 255) e->cat[i][cat]++;
            }
        for (uint32_t i = 0; i < e->n; i++) if (e->fns[i] == fn) return;
        if (e->n < 16) e->fns[e->n++] = fn;
        return;
    }
}

static void etude_objets_noter(Cpu *c, uint32_t fn, uint32_t r0) {
    /* La mémoire vive du jeu, et non la pile : un cadre de pile reçoit lui aussi des appels, mais ce
       n'est pas un objet — il change de forme à chaque fois qu'on y passe. */
    if (r0 < 0x02000000u || r0 >= 0x02400000u) return;
    if (g_etude_adr) etude_adr_noter(c, fn, r0);
    uint32_t h = (fn * 2654435761u) >> 8 & (ETUDE_OBJETS_CASES - 1);
    for (uint32_t k = 0; k < 32; k++) {
        EtudeObjet *e = &g_etude_objets[(h + k) & (ETUDE_OBJETS_CASES - 1)];
        if (e->fn == 0) { e->fn = fn; e->plage_min = e->plage_max = r0; }
        if (e->fn != fn) continue;
        e->appels++;
        if (r0 < e->plage_min) e->plage_min = r0;
        if (r0 > e->plage_max) e->plage_max = r0;
        for (uint32_t i = 0; i < e->n; i++) if (e->bases[i] == r0) return;
        if (e->n < 8) e->bases[e->n++] = r0;
        return;
    }
}

static void etude_objets_ecrire(void) {
    FILE *f = fopen(g_etude_objets_fichier, "w");
    if (!f) return;
    fprintf(f, "# fonction ; appels ; plage des premiers arguments ; quelques adresses vues\n");
    for (uint32_t i = 0; i < ETUDE_OBJETS_CASES; i++) {
        EtudeObjet *e = &g_etude_objets[i];
        if (!e->fn || !e->appels) continue;
        fprintf(f, "0x%08X %llu 0x%08X 0x%08X", e->fn, (unsigned long long)e->appels, e->plage_min, e->plage_max);
        for (uint32_t k = 0; k < e->n; k++) fprintf(f, " 0x%08X", e->bases[k]);
        fputc(10, f);
    }
    fclose(f);
    /* et le relevé par objet, à côté */
    {
        char chemin[512];
        snprintf(chemin, sizeof chemin, "%s", g_etude_objets_fichier);
        char *point = strrchr(chemin, '.');
        snprintf(point ? point : chemin + strlen(chemin), 32, "_par_objet.txt");
        FILE *g = fopen(chemin, "w");
        if (!g) return;
        fprintf(g, "# objet ; appels ; les fonctions qui l'ont reçu en premier argument\n");
        for (uint32_t i = 0; i < ETUDE_ADR_CASES; i++) {
            EtudeAdresse *e = &g_etude_adr[i];
            if (!e->adr || e->n < 2) continue;      /* un objet qu'une seule fonction touche ne dit rien */
            fprintf(g, "0x%08X %llu", e->adr, (unsigned long long)e->appels);
            for (uint32_t k = 0; k < e->n; k++) fprintf(g, " 0x%08X", e->fns[k]);
            fputc('|', g);       /* puis ce que contiennent ses quarante-huit premiers mots */
            for (uint32_t i = 0; i < ETUDE_CHAMPS; i++) fputc(etude_lettre(e->cat[i]), g);
            fputc(10, g);
        }
        fclose(g);
    }
}

/* SFC_SUIVRE_OBJET=0xADRESSE[,fichier] : les quarante-huit premiers mots de cet objet, à chaque image.
 * Un champ qui varie un peu à chaque image et beaucoup sur la durée est une position ; un qui monte de
 * un est un compteur ; un qui ne bouge jamais est une identité. C'est ainsi qu'un champ prend son nom. */
static uint32_t g_suivi_adr;
static FILE *g_suivi_f;
static uint64_t g_suivi_image;

void rt_suivre_objet(Cpu *c) {
    if (!g_suivi_adr || !g_suivi_f) return;
    if (hw_frames() == g_suivi_image) return;
    g_suivi_image = hw_frames();
    fprintf(g_suivi_f, "%llu", (unsigned long long)g_suivi_image);
    for (uint32_t i = 0; i < ETUDE_CHAMPS; i++)
        fprintf(g_suivi_f, " %08X", mem_read32(c, g_suivi_adr + i * 4u));
    fputc(10, g_suivi_f);
}

static void suivi_fermer(void) { if (g_suivi_f) fclose(g_suivi_f); }

void rt_suivre_objet_init(void) {
    const char *v = getenv("SFC_SUIVRE_OBJET");
    if (!v) return;
    char *fin;
    g_suivi_adr = (uint32_t)strtoul(v, &fin, 0);
    const char *chemin = (*fin == ',') ? fin + 1 : "analysis/suivi.txt";
    g_suivi_f = fopen(chemin, "w");
    if (!g_suivi_f) { g_suivi_adr = 0; return; }
    fprintf(g_suivi_f, "# image ; les quarante-huit premiers mots de 0x%08X\n", g_suivi_adr);
    atexit(suivi_fermer);
}

void rt_etude_objets_init(void) {
    rt_suivre_objet_init();
    g_etude_objets_fichier = getenv("SFC_ETUDE_OBJETS");
    if (!g_etude_objets_fichier) return;
    g_etude_objets = calloc(ETUDE_OBJETS_CASES, sizeof *g_etude_objets);
    g_etude_adr = calloc(ETUDE_ADR_CASES, sizeof *g_etude_adr);
    if (g_etude_objets && g_etude_adr) atexit(etude_objets_ecrire);
}

static void dispatch(Cpu *c, uint32_t target) {
    if (g_etude_objets) etude_objets_noter(c, target & ~1u, c->r[0]);
    if (g_bouchon) {
        g_hook_entree = target & ~1u;
        c->thumb = target & 1u;
        g_bouchon(c);
        return;
    }
    stat_call(target);
    int trace = trace_call_wanted(target);
    if (trace) trace_call(c, target, 0, 0);
    g_trace_ring[g_trace_pos].target = target;
    g_trace_ring[g_trace_pos].lr = c->r[14];
    g_trace_ring[g_trace_pos].sp = c->r[13];
    g_trace_pos = (g_trace_pos + 1) % 64;
    NativeHook hook = hook_for_on(c->is_arm7 ? 1 : 0, target);
    c->thumb = target & 1u;
    if (hook) { hook(c); if (trace) trace_call(c, target, c->r[0], 1); return; }
    const RecompEntry *e = rt_find(c, target);   /* bit 0 : mode demandé */
    if (e == NULL) {
        /* retour d'expérience : la cartographie relit ce fichier comme amorces manuelles — pas ce qu'un tour
           d'aperçu égaré a visé (natif.c) */
        int ovl = c->is_arm7 ? -1 : rt_overlay_loaded(target & ~1u);
        extern int g_apercu_en_cours;
        FILE *f = (g_apercu_en_cours || !amorce_utile(target)) ? NULL : fopen("analysis/amorces.txt", "a");
        if (f) {
            if (c->is_arm7) fprintf(f, "arm7 0x%08X\n", target);
            else if (ovl >= 0) fprintf(f, "ovl%d 0x%08X\n", ovl, target);
            else fprintf(f, "arm9 0x%08X\n", target);
            fclose(f);
        }
        rt_fatal(c, "saut vers du code non cartographié : 0x%08X (%s ; overlay présent : %d) ; "
                    "adresse consignée dans analysis/amorces.txt pour la prochaine cartographie",
                 target, c->is_arm7 ? "coprocesseur" : "ARM9", ovl);
    }
    /* Le crochet d'une fonction traduite ne joue qu'à son entrée. Un saut qui tombe en son milieu —
       un retour déroulé par ici, un pointeur vers une entrée secondaire — a peut-être sa
       réécriture : natif.c le sait. */
    if (g_dispatch_milieu && (target & ~1u) != e->entry && g_dispatch_milieu(c, e, target & ~1u)) {
        if (trace) trace_call(c, target, c->r[0], 1);
        return;
    }
    e->fn(c, target & ~1u);
    if (trace) trace_call(c, target, c->r[0], 1);
}

/* Quatre cents appels imbriqués : le jeu n'en fait jamais tant. À froid, c'est une récursion
   nourrie de valeurs tirées au sort, et l'essai est abandonné ; en jeu, on laisse faire — la
   pile fantôme se tronque déjà à 512. */
void rt_trop_profond(Cpu *c) {
    (void)c;
    if (g_essai_aretes_max) longjmp(g_essai_reprise, 1);
}

void rt_call(Cpu *c, uint32_t target) {
    dispatch(c, target);
}

/* Le banc de vérification juge une fonction qui finit par un saut à l'instant où elle saute :
   il arme ce drapeau, le prochain saut est noté au lieu d'être suivi, et c'est le banc qui le
   suivra, une seule fois, quand il aura comparé. */
int g_jump_intercepte;
uint32_t g_jump_cible, g_jump_profondeur;
volatile uint64_t g_perturbations;
uint32_t g_hook_entree;     /* posée par le code traduit juste avant d'appeler un crochet */

void rt_jump(Cpu *c, uint32_t target) {
    /* seul le saut de la fonction jugée compte : ses appelées reviennent par un saut aussi,
       à une profondeur de pile fantôme plus grande */
    if (g_jump_intercepte && !c->is_arm7 && c->depth == g_jump_profondeur) {
        g_jump_intercepte = 0;
        g_jump_cible = target;
        return;
    }
    /* retour de fonction déguisé en saut par registre ? */
    if (rt_is_return(c, target)) {
        c->thumb = target & 1u;
        return;
    }
    dispatch(c, target);
}

/* ------------------------------------------------------------------ modes et banques */
static uint32_t *bank_for(Cpu *c, uint32_t mode) {
    switch (mode) {
    case MODE_IRQ: return c->bank_irq;
    case MODE_SVC: return c->bank_svc;
    case MODE_ABT: return c->bank_abt;
    case MODE_UND: return c->bank_und;
    case MODE_FIQ: return c->bank_fiq + 5;   /* r13, r14, spsr après r8..r12 */
    default:       return c->bank_usr;       /* usr et sys partagent */
    }
}

void cpu_switch_mode(Cpu *c, uint32_t mode) {
    if (mode == c->mode) return;
    uint32_t *old = bank_for(c, c->mode);
    old[0] = c->r[13];
    old[1] = c->r[14];
    if (c->mode != MODE_USR && c->mode != MODE_SYS) old[2] = c->spsr;
    if (c->mode == MODE_FIQ) memcpy(c->bank_fiq, c->r + 8, 5 * sizeof(uint32_t));
    else memcpy(c->usr_r8_r12, c->r + 8, 5 * sizeof(uint32_t));
    uint32_t *nw = bank_for(c, mode);
    c->r[13] = nw[0];
    c->r[14] = nw[1];
    c->spsr = (mode != MODE_USR && mode != MODE_SYS) ? nw[2] : 0;
    if (mode == MODE_FIQ) memcpy(c->r + 8, c->bank_fiq, 5 * sizeof(uint32_t));
    else memcpy(c->r + 8, c->usr_r8_r12, 5 * sizeof(uint32_t));
    c->mode = mode;
}

uint32_t user_reg_read(Cpu *c, int n) {
    if (c->mode == MODE_USR || c->mode == MODE_SYS) return c->r[n];
    return c->bank_usr[n - 13];
}

void user_reg_write(Cpu *c, int n, uint32_t v) {
    if (c->mode == MODE_USR || c->mode == MODE_SYS) c->r[n] = v;
    else c->bank_usr[n - 13] = v;
}

uint32_t cpsr_read(Cpu *c) {
    return (c->nf << 31) | (c->zf << 30) | (c->cf << 29) | (c->vf << 28) | (c->qf << 27)
         | (c->irq_off << 7) | (c->fiq_off << 6) | (c->thumb << 5) | c->mode;
}

void cpsr_write(Cpu *c, uint32_t v, uint32_t fields) {
    if (fields & 8u) {
        c->nf = v >> 31; c->zf = (v >> 30) & 1u; c->cf = (v >> 29) & 1u; c->vf = (v >> 28) & 1u;
        c->qf = (v >> 27) & 1u;
    }
    if (fields & 1u) {
        c->irq_off = (v >> 7) & 1u;
        c->fiq_off = (v >> 6) & 1u;
        c->thumb = (v >> 5) & 1u;
        cpu_switch_mode(c, v & 0x1Fu);
    }
}

uint32_t spsr_read(Cpu *c) { return c->spsr; }

void spsr_write(Cpu *c, uint32_t v, uint32_t fields) {
    uint32_t mask = 0;
    if (fields & 1u) mask |= 0x000000FFu;
    if (fields & 2u) mask |= 0x0000FF00u;
    if (fields & 4u) mask |= 0x00FF0000u;
    if (fields & 8u) mask |= 0xFF000000u;
    c->spsr = (c->spsr & ~mask) | (v & mask);
}

/* subs pc, lr, #4 / ldm ^ : cpsr = spsr, retour au mode interrompu */
void rt_return_from_exception(Cpu *c) {
    cpsr_write(c, c->spsr, 0xFu);
}

/* ------------------------------------------------------------------ coprocesseur 15 */
static uint32_t g_cp15_control = 0x00002078u;
static uint32_t g_cp15_dtcm = 0x027E000Au;   /* base 0x027E0000, taille 16 Ko */
static uint32_t g_cp15_itcm = 0x00000020u;   /* base 0, taille 32 Mo (miroirs) */
static uint32_t g_cp15_regions[8];
static uint32_t g_cp15_perm_d, g_cp15_perm_i, g_cp15_cache_d, g_cp15_cache_i, g_cp15_wb;

void rt_cpu_wait_interrupt(Cpu *c);   /* fourni par irq.c */

void rt_cp15_write(Cpu *c, uint32_t opc1, uint32_t crn, uint32_t crm, uint32_t opc2, uint32_t v) {
    (void)opc1;
    switch (crn) {
    case 1: g_cp15_control = v; break;
    case 2: if (opc2 == 0) g_cp15_cache_d = v; else g_cp15_cache_i = v; break;
    case 3: g_cp15_wb = v; break;
    case 5: if (opc2 == 0 || opc2 == 2) g_cp15_perm_d = v; else g_cp15_perm_i = v; break;
    case 6: if (crm < 8) g_cp15_regions[crm] = v; break;
    case 7:
        /* opérations de cache : sans effet ; c7,c0,4 = attente d'interruption */
        if (crm == 0 && opc2 == 4) rt_cpu_wait_interrupt(c);
        break;
    case 9:
        if (crm == 1) { if (opc2 == 0) g_cp15_dtcm = v; else g_cp15_itcm = v; }
        break;
    default: break;
    }
}

uint32_t rt_cp15_read(Cpu *c, uint32_t opc1, uint32_t crn, uint32_t crm, uint32_t opc2) {
    (void)c; (void)opc1;
    switch (crn) {
    case 0:
        if (opc2 == 0) return 0x41059461u;          /* ARM946E-S */
        if (opc2 == 1) return 0x0F0D2112u;          /* caches */
        return 0x00140180u;                          /* TCM */
    case 1: return g_cp15_control;
    case 2: return opc2 == 0 ? g_cp15_cache_d : g_cp15_cache_i;
    case 3: return g_cp15_wb;
    case 5: return (opc2 == 0 || opc2 == 2) ? g_cp15_perm_d : g_cp15_perm_i;
    case 6: return crm < 8 ? g_cp15_regions[crm] : 0;
    case 9: if (crm == 1) return opc2 == 0 ? g_cp15_dtcm : g_cp15_itcm; return 0;
    default: return 0;
    }
}

/* ------------------------------------------------------------------ bkpt, undefined (svc : bios.c) */

/* Le jeu écrit (expression, fichier, ligne, fonction) dans une structure puis bkpt. */
static uint32_t g_assert_info;   /* adresse de la structure, si connue */
void rt_set_assert_info(uint32_t addr) { g_assert_info = addr; }

static void read_cstring(Cpu *c, uint32_t addr, char *out, size_t n) {
    size_t i = 0;
    if (addr == 0) { out[0] = 0; return; }
    for (; i + 1 < n; i++) {
        uint8_t b = mem_read8(c, addr + (uint32_t)i);
        if (!b) break;
        out[i] = (char)b;
    }
    out[i] = 0;
}

void rt_bkpt(Cpu *c, uint32_t imm, uint32_t addr) {
    if (g_assert_info) {
        char expr[128], file[64], func[64];
        read_cstring(c, mem_read32(c, g_assert_info + 0x28), expr, sizeof expr);
        read_cstring(c, mem_read32(c, g_assert_info + 0x2C), file, sizeof file);
        read_cstring(c, mem_read32(c, g_assert_info + 0x34), func, sizeof func);
        uint32_t line = mem_read32(c, g_assert_info + 0x30);
        rt_fatal(c, "assertion du jeu : (%s) dans %s, fonction %s, ligne %u [bkpt %u à 0x%08X]",
                 expr, file, func, line, imm, addr);
    }
    rt_fatal(c, "bkpt %u à 0x%08X", imm, addr);
}

/* Le flot entre à une adresse que la fonction qui la contient n'a pas étiquetée : la carte a
   donné cette adresse à une autre fonction que la vraie. Comme pour une adresse non
   cartographiée, on la consigne : reprise en amorce manuelle, elle évincera l'usurpatrice. */
static void note_seed(Cpu *c, uint32_t addr) {
    int ovl = c->is_arm7 ? -1 : rt_overlay_loaded(addr & ~1u);
    extern int g_apercu_en_cours;
    if (g_apercu_en_cours || !amorce_utile(addr)) return;
    FILE *f = fopen("analysis/amorces.txt", "a");
    if (!f) return;
    if (c->is_arm7) fprintf(f, "arm7 0x%08X\n", addr);
    else if (ovl >= 0) fprintf(f, "ovl%d 0x%08X\n", ovl, addr);
    else fprintf(f, "arm9 0x%08X\n", addr);
    fclose(f);
}

void rt_unlabeled(Cpu *c, uint32_t entry) {
    const RecompEntry *e = rt_find(c, entry | (c->thumb ? 1u : 0u));
    note_seed(c, entry | (c->thumb ? 1u : 0u));
    if (e)
        rt_fatal(c, "entrée à 0x%08X : aucune étiquette à cette adresse dans la fonction générée "
                    "%s 0x%08X [0x%08X-0x%08X] (overlay présent : %d) ; "
                    "adresse consignée dans analysis/amorces.txt pour la prochaine cartographie",
                 entry, e->region, e->entry, e->start, e->end, rt_overlay_loaded(entry));
    rt_log("  overlays chargés : emplacement 2-4 -> %d, 5-22 -> %d, 23-25 -> %d",
           rt_overlay_loaded(0x021F0000u), rt_overlay_loaded(0x02228000u), rt_overlay_loaded(0x0225A000u));
    rt_fatal(c, "entrée à 0x%08X : aucune étiquette à cette adresse dans la fonction générée (overlay présent : %d)",
             entry, rt_overlay_loaded(entry));
}

void rt_undefined(Cpu *c, uint32_t addr, uint32_t raw) {
    rt_fatal(c, "instruction non traduite ou chute hors fonction à 0x%08X (mot 0x%08X)", addr, raw);
}

void rt_trace(Cpu *c, uint32_t addr) {
    c->r[15] = addr;
}

#ifdef SFC_COMPTE_TRADUIT
/* Ce qui passe encore par la traduction. SFC_TRADUIT=fichier : à la sortie, une ligne par fonction
   traduite qui a tourné — région, entrée, exécutions depuis son entrée, exécutions depuis son
   milieu (une suite venue d'une autre fonction, une reprise après un changement de thread). */
uint32_t g_traduit[40000][2];
static struct { uint32_t adresse, fois; } g_milieux[4096];   /* adressage ouvert, par l'adresse */

void rt_traduit_milieu(uint32_t adresse) {
    for (uint32_t k = (adresse >> 1) * 2654435761u >> 20, n = 0; n < 4096; k = (k + 1) & 4095u, n++) {
        if (g_milieux[k].adresse == adresse) { g_milieux[k].fois++; return; }
        if (!g_milieux[k].fois) { g_milieux[k].adresse = adresse; g_milieux[k].fois = 1; return; }
    }
}

uint32_t g_traduit7[4096][2];
static struct { uint32_t adresse, fois; } g_milieux7[1024];

void rt_traduit_milieu7(uint32_t adresse) {
    for (uint32_t k = (adresse >> 1) * 2654435761u >> 22, n = 0; n < 1024; k = (k + 1) & 1023u, n++) {
        if (g_milieux7[k].adresse == adresse) { g_milieux7[k].fois++; return; }
        if (!g_milieux7[k].fois) { g_milieux7[k].adresse = adresse; g_milieux7[k].fois = 1; return; }
    }
}

static void traduit_rapport(void) {
    const char *chemin = getenv("SFC_TRADUIT");
    FILE *f = chemin ? fopen(chemin, "w") : NULL;
    size_t fonctions = 0;
    uint64_t entrees = 0, milieux = 0;
    for (size_t i = 0; i < g_recomp_table_size && i < 40000; i++) {
        if (!g_traduit[i][0] && !g_traduit[i][1]) continue;
        fonctions++;
        entrees += g_traduit[i][1]; milieux += g_traduit[i][0];
        if (f) fprintf(f, "%s 0x%08X %u %u\n", g_recomp_table[i].region, g_recomp_table[i].entry,
                       g_traduit[i][1], g_traduit[i][0]);
    }
    if (f) {
        for (size_t k = 0; k < 4096; k++)
            if (g_milieux[k].fois) fprintf(f, "milieu 0x%08X %u\n", g_milieux[k].adresse, g_milieux[k].fois);
        fclose(f);
    }
    rt_log("traduction : %zu fonctions traduites ont tourné — %llu fois depuis leur entrée, %llu depuis leur milieu",
           fonctions, (unsigned long long)entrees, (unsigned long long)milieux);
#ifdef SFC_HAVE_ARM7
    size_t fonctions7 = 0;
    uint64_t entrees7 = 0, milieux7 = 0;
    f = chemin ? fopen(chemin, "a") : NULL;
    for (size_t i = 0; i < g_recomp7_table_size && i < 4096; i++) {
        if (!g_traduit7[i][0] && !g_traduit7[i][1]) continue;
        fonctions7++;
        entrees7 += g_traduit7[i][1]; milieux7 += g_traduit7[i][0];
        if (f) fprintf(f, "%s 0x%08X %u %u\n", g_recomp7_table[i].region, g_recomp7_table[i].entry,
                       g_traduit7[i][1], g_traduit7[i][0]);
    }
    if (f) {
        for (size_t k = 0; k < 1024; k++)
            if (g_milieux7[k].fois) fprintf(f, "milieu7 0x%08X %u\n", g_milieux7[k].adresse, g_milieux7[k].fois);
        fclose(f);
    }
    rt_log("traduction du coprocesseur : %zu fonctions ont tourné — %llu fois depuis leur entrée, %llu depuis leur milieu",
           fonctions7, (unsigned long long)entrees7, (unsigned long long)milieux7);
#endif
}
#endif

void rt_init(Cpu *c) {
#ifdef SFC_COMPTE_TRADUIT
    { static int inscrit; if (!inscrit++) atexit(traduit_rapport); }   /* rt_init sert à chaque contexte */
#endif
    memset(c, 0, sizeof *c);
    c->mode = MODE_SVC;
    c->irq_off = 1;
    c->fiq_off = 1;
}

/* ------------------------------------------------------------------ chien de garde */
static Cpu *volatile g_watch_cpu;
static volatile LONG g_watch_alive;
static DWORD g_watch_seconds;

void rt_dump_stack(Cpu *c) {
    fprintf(stderr, "  mode=%02X thumb=%u profondeur=%u\n", c->mode, c->thumb, c->depth);
    for (uint32_t i = c->depth; i > 0 && i + 24 > c->depth; i--)
        fprintf(stderr, "  retour[%u] = %08X\n", i - 1, c->shadow[i - 1]);
    fflush(stderr);
}

volatile int g_stuck_dump;
uint32_t mmio_peek(uint32_t off);   /* mmio.c */

static void print_cpu(Cpu *c) {
    fprintf(stderr, "  r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X\n"
                    "  r8=%08X r9=%08X r10=%08X r11=%08X r12=%08X sp=%08X lr=%08X pc=%08X\n"
                    "  irq_off=%u in_irq=%u IME=%u IE=%08X IF=%08X\n",
            c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7],
            c->r[8], c->r[9], c->r[10], c->r[11], c->r[12], c->r[13], c->r[14], c->r[15],
            c->irq_off, c->in_irq, mmio_peek(0x208) & 1u, mmio_peek(0x210), mmio_peek(0x214));
    rt_dump_stack(c);
    rt_dump_dispatch();
    fflush(stderr);
}

/* Quand le coprocesseur tourne, un blocage vient souvent d'une attente croisée : son état
   compte donc autant que celui de l'ARM9. */
static void print_arm7(void) {
    if (!arm7_enabled()) return;
    Cpu *c = arm7_current_cpu();
    if (!c) c = arm7_cpu();
    fprintf(stderr, "  coprocesseur : pc=0x%08X lr=0x%08X sp=0x%08X mode=%02X irq_off=%u in_irq=%u "
                    "profondeur=%u ; IME=%u IE=%08X IF=%08X ; %llu arêtes\n",
            c->r[15], c->r[14], c->r[13], c->mode, c->irq_off, c->in_irq, c->depth,
            mmio7_peek(0x208) & 1u, mmio7_peek(0x210), mmio7_peek(0x214),
            (unsigned long long)arm7_edges());
    rt_dump_stack(c);
}

void rt_rapport_arm7(void) { print_arm7(); }

/* Imprimé par le thread bloqué lui-même, à une arête arrière : l'état est cohérent. */
void rt_stuck_report(Cpu *c) {
    fprintf(stderr, "CHIEN DE GARDE : le thread bloqué rapporte son état (boucle d'attente active) :\n");
    print_cpu(c);
    print_arm7();
    ExitProcess(3);
}

static DWORD WINAPI watchdog_main(LPVOID param) {
    (void)param;
    DWORD waited = 0;
    for (;;) {
        Sleep(1000);
        waited++;
        if (InterlockedExchange(&g_watch_alive, 0) || g_temps_fige) { waited = 0; continue; }
        if (waited >= g_watch_seconds) {
            fprintf(stderr, "CHIEN DE GARDE : aucune progression depuis %lu s ; demande d'état au thread bloqué…\n",
                    (unsigned long)waited);
            fflush(stderr);
            /* on force le test d'interruption des arêtes arrière : si le thread boucle, il répond */
            g_stuck_dump = 1;
            for (int i = 0; i < 20; i++) { g_irq_line = 1; g_edge_budget = 0; Sleep(100); }
            fprintf(stderr, "CHIEN DE GARDE : pas de réponse (pas de boucle active) ; état du dernier contexte connu (lecture concurrente) :\n");
            if (g_watch_cpu) print_cpu(g_watch_cpu);
            print_arm7();
            fflush(stderr);
            ExitProcess(3);
        }
    }
}

/* ------------------------------------------------------------------ point d'observation d'écriture */
/* SFC_WATCH=0xADDR : chaque écriture qui change le mot invité à cette adresse est journalisée
   (image, fonction générée retrouvée par la table, pile fantôme). La page hôte qui le contient
   est protégée en lecture seule ; la faute la rend accessible le temps d'une instruction (pas à
   pas par le drapeau TF), puis on compare et on reprotège. */
uint64_t hw_frames(void);
uint8_t *mem_resolve(uint32_t addr, uint32_t size);
static uint32_t g_wp_addr;
static volatile uint32_t *g_wp_host;
static void *g_wp_page;
static uint32_t g_wp_old;
static int g_wp_stepping;
/* SFC_WATCH_DEPUIS=image : ne commencer qu'à cette image ; SFC_WATCH_MAX=n : s'arrêter après n écritures.
   Sans eux, la page reste protégée depuis le démarrage, et une page de variables écrites sans cesse rend
   le jeu très lent avant même d'arriver au moment voulu. */
static uint64_t g_wp_depuis;
static long g_wp_max = -1, g_wp_n;
static int g_wp_arme, g_wp_fini;
/* SFC_WATCH_LECTURES=1 : la page est aussi interdite en lecture, et chaque endroit qui LIT le mot est dit
   une fois, avec le nombre de lectures qu'il fait ensuite — pour trouver qui se sert d'un réglage. À faire
   sous la traduction (SFC_NATIF=non) : son code lit la mémoire en ligne, et la fonction qui lit se déduit de
   l'adresse hôte ; le code natif, lui, passe par des aides du runtime. */
static int g_wp_lectures, g_wp_lecture;
static const void *g_wp_pc;

static void wp_protect(int on) {
    DWORD old;
    VirtualProtect(g_wp_page, 4096, on ? (g_wp_lectures ? PAGE_NOACCESS : PAGE_READONLY) : PAGE_READWRITE, &old);
}

void rt_watch_start(void) {
    const char *w = getenv("SFC_WATCH");
    if (!w) return;
    g_wp_addr = (uint32_t)strtoul(w, NULL, 0);
    uint8_t *host = mem_resolve(g_wp_addr, 4);
    if (!host) { rt_log("observation : 0x%08X n'est pas en RAM", g_wp_addr); return; }
    g_wp_host = (volatile uint32_t *)host;
    g_wp_page = (void *)((uintptr_t)host & ~(uintptr_t)4095);
    { const char *e = getenv("SFC_WATCH_DEPUIS"); g_wp_depuis = e ? strtoull(e, NULL, 0) : 0; }
    g_wp_lectures = getenv("SFC_WATCH_LECTURES") != NULL;
    { const char *e = getenv("SFC_WATCH_MAX"); g_wp_max = e ? strtol(e, NULL, 0) : -1; }
    if (!g_wp_depuis) { wp_protect(1); g_wp_arme = 1; }
    rt_log("observation : écritures de 0x%08X (page hôte %p)%s", g_wp_addr, g_wp_page,
           g_wp_depuis ? ", à partir de l'image demandée" : "");
}

/* à chaque balayage : arme la surveillance à l'image demandée */
void rt_watch_tick(uint64_t frame) {
    if (g_wp_page && !g_wp_arme && !g_wp_fini && frame >= g_wp_depuis) {
        g_wp_arme = 1;
        wp_protect(1);
        rt_log("observation : armée à l'image %llu", (unsigned long long)frame);
    }
}

/* la fonction générée qui contient une adresse hôte : celle dont le code commence le plus près avant */
static const RecompEntry *host_owner(const void *pc) {
    const RecompEntry *best = NULL;
    for (size_t i = 0; i < g_recomp_table_size; i++) {
        const RecompEntry *e = &g_recomp_table[i];
        if ((const void *)e->fn <= pc && (best == NULL || (const void *)e->fn > (const void *)best->fn)) best = e;
    }
    return best;
}

static LONG watch_handler(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        uintptr_t at = (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1];
        if (at >= (uintptr_t)g_wp_page && at < (uintptr_t)g_wp_page + 4096) {
            wp_protect(0);
            g_wp_old = *g_wp_host;
            g_wp_lecture = g_wp_lectures && ep->ExceptionRecord->ExceptionInformation[0] == 0
                           && at >= (uintptr_t)g_wp_host && at < (uintptr_t)g_wp_host + 4;
            g_wp_pc = ep->ExceptionRecord->ExceptionAddress;
            ep->ContextRecord->EFlags |= 0x100u;   /* pas à pas : reprotéger après cette instruction */
            g_wp_stepping = 1;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    } else if (code == EXCEPTION_SINGLE_STEP && g_wp_stepping) {
        g_wp_stepping = 0;
        if (g_wp_lecture) {
            static const void *vus[64];
            static uint64_t combien[64];
            static int n;
            int k = 0;
            while (k < n && vus[k] != g_wp_pc) k++;
            if (k == n && n < 64) {
                vus[n] = g_wp_pc; combien[n] = 0; n++;
                const RecompEntry *e = host_owner(g_wp_pc);
                rt_log("observation : image %llu, lecture de 0x%08X (= %08X) par %s 0x%08X (hôte %p)",
                       (unsigned long long)hw_frames(), g_wp_addr, *g_wp_host,
                       e ? e->region : "runtime", e ? e->entry : 0u, g_wp_pc);
                { extern Cpu *rt_current_cpu(void); Cpu *cur = rt_current_cpu(); if (cur) rt_dump_stack(cur); }
            }
            if (k < 64 && ++combien[k] % 1000 == 0)
                rt_log("observation : l'endroit %p a lu 0x%08X %llu fois", vus[k], g_wp_addr, (unsigned long long)combien[k]);
        }
        uint32_t now = *g_wp_host;
        if (now != g_wp_old) {
            const void *pc = ep->ExceptionRecord->ExceptionAddress;
            const RecompEntry *e = host_owner(pc);
            rt_log("observation : image %llu, 0x%08X : %08X -> %08X, par %s 0x%08X (hôte %p)",
                   (unsigned long long)hw_frames(), g_wp_addr, g_wp_old, now,
                   e ? e->region : "runtime", e ? e->entry : 0u, pc);
            { extern int g_in_dma; extern uint32_t g_dma_src, g_dma_dst, g_dma_cnt;
              if (g_in_dma >= 0) rt_log("  pendant le DMA%d : src=%08X dst=%08X cnt=%08X", g_in_dma, g_dma_src, g_dma_dst, g_dma_cnt); }
            { extern Cpu *rt_current_cpu(void); Cpu *cur = rt_current_cpu();
              if (cur) { rt_log("  thread courant : pc=0x%08X (exact avec SFC_PC_TRACE) lr=0x%08X sp=0x%08X mode=%02X", cur->r[15], cur->r[14], cur->r[13], cur->mode); rt_dump_stack(cur); } }
            if (g_wp_max >= 0 && ++g_wp_n >= g_wp_max) {
                g_wp_fini = 1;
                rt_log("observation : %ld écritures relevées, on s'arrête là", g_wp_n);
            }
        }
        if (!g_wp_fini) wp_protect(1);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Une faute côté hôte (accès invalide, débordement de pile de fibre…) imprime l'état invité. */
static LONG WINAPI crash_handler(EXCEPTION_POINTERS *ep) {
    if (g_wp_page) {
        LONG r = watch_handler(ep);
        if (r == EXCEPTION_CONTINUE_EXECUTION) return r;
    }
    /* seules les erreurs (0xC…) nous concernent : les codes informatifs (nommage de thread
       0x406D1388 par le pilote graphique, exceptions C++ relayées…) suivent leur cours */
    if (ep->ExceptionRecord->ExceptionCode < 0xC0000000u)
        return EXCEPTION_CONTINUE_SEARCH;
    fprintf(stderr, "FAUTE HÔTE : code 0x%08lX à l'adresse %p", (unsigned long)ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress);
    if (ep->ExceptionRecord->NumberParameters >= 2)
        fprintf(stderr, " (accès %s en %p)", ep->ExceptionRecord->ExceptionInformation[0] ? "écriture" : "lecture",
                (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    fprintf(stderr, "\n");
    {   /* Sur quel fil, et par où : la pile de la machine hôte, module et décalage à chaque étage
           (tools/echantillons.py retrouve les fonctions de l'exécutable). Le portage a plusieurs fils
           — affichage, composition 2D — et l'état invité qui suit n'est que celui du fil du jeu. */
        extern const char *rt_fil_nom(void);
        CONTEXT ctx = *ep->ContextRecord;
        char l[1800];
        size_t n = 0;
        l[0] = 0;
        for (int k = 0; k < 24 && ctx.Rip && n < sizeof l - 100; k++) {
            HMODULE m = NULL;
            char nom[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)(uintptr_t)ctx.Rip, &m))
                GetModuleFileNameA(m, nom, sizeof nom);
            const char *court = strrchr(nom, '\\');
            n += (size_t)snprintf(l + n, sizeof l - n, " %s+%llx", court ? court + 1 : nom,
                                  (unsigned long long)(ctx.Rip - (uintptr_t)m));
            DWORD64 ib = 0;
            PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &ib, NULL);
            if (fe) {
                PVOID hd; DWORD64 ef;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, ib, ctx.Rip, fe, &ctx, &hd, &ef, NULL);
            } else {
                ctx.Rip = *(DWORD64 *)ctx.Rsp;
                ctx.Rsp += 8;
            }
        }
        fprintf(stderr, "  fil « %s » (%lu), pile hôte :%s\n", rt_fil_nom(), (unsigned long)GetCurrentThreadId(), l);
    }
    if (g_watch_cpu) {
        Cpu *c = g_watch_cpu;
        fprintf(stderr, "  r0=%08X r1=%08X r2=%08X r3=%08X sp=%08X lr=%08X pc=%08X\n",
                c->r[0], c->r[1], c->r[2], c->r[3], c->r[13], c->r[14], c->r[15]);
        rt_dump_stack(c);
    }
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

void rt_watchdog_start(unsigned seconds) {
    g_watch_seconds = seconds;
    AddVectoredExceptionHandler(1, crash_handler);
    CreateThread(NULL, 0, watchdog_main, NULL, 0, NULL);
    rt_watch_start();
}

/* appelé aux points sûrs (attente d'interruption) : signale une progression */
void rt_watchdog_kick(Cpu *c) {
    g_watch_cpu = c;
    InterlockedExchange(&g_watch_alive, 1);
}

void rt_watchdog_set_cpu(Cpu *c) { g_watch_cpu = c; }
