/*
 * Entre deux images du jeu (voir entre_images.h).
 *
 * L'appariement se fait dans le repère de la caméra, où il est simple : d'une image à la suivante, tout
 * ce qui ne bouge pas dans le monde subit le même déplacement rigide — celui de la caméra, à l'envers.
 * On l'estime sur les polygones dont l'identité est sans équivoque, puis on s'en sert deux fois : pour
 * départager ceux qui se ressemblent (cinq anneaux, cent tuiles de sol : le bon pareil est celui que
 * le mouvement de la caméra prédit), et pour placer ceux qui n'ont pas de pareil du tout (une particule
 * née à l'instant, un tir) — ils suivent la caméra au lieu de sauter avec elle.
 *
 * Sous licence MIT.
 */
#include "entre_images.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

extern "C" void rt_log(const char *fmt, ...);

namespace sfc {

uint64_t g_blocs_ambigus;   /* blocs refusés : deux prétendants se valaient pour la même place */
uint64_t g_retournes;       /* polygones que l'interpolation retourne : le moteur les élimine, ils disparaissent */

/* SFC_DIAG_CLIGNOTE=1 : des polygones que le jeu dessine une image sur deux. C'est ainsi que la console
   simule un effet translucide — à soixante images par seconde l'œil les fond. Affichés quatre fois plus
   longtemps, ils se mettent à clignoter pour de bon. On suit la présence de chaque forme sur les huit
   derniers tours : une suite 1,0,1,0 trahit l'alternance. */
static std::unordered_map<uint32_t, uint8_t> g_presence;      /* forme -> huit derniers tours, un bit chacun */
uint64_t g_formes_alternees, g_formes_suivies;

void noter_presence(const std::vector<GxPolygon> &polys) {
    static int actif = -1;
    if (actif < 0) actif = getenv("SFC_DIAG_CLIGNOTE") != nullptr;
    if (!actif) return;
    for (auto &kv : g_presence) kv.second = (uint8_t)(kv.second << 1);
    for (const GxPolygon &q : polys) g_presence[q.forme] |= 1u;
    uint64_t alternees = 0, suivies = 0;
    for (auto &kv : g_presence) {
        uint8_t h = kv.second;
        if (h == 0 || h == 0xFF) continue;                    /* toujours absent ou toujours là */
        suivies++;
        if (h == 0xAA || h == 0x55) alternees++;              /* une image sur deux, sans faute */
    }
    g_formes_alternees = alternees; g_formes_suivies = suivies;
    static uint64_t n;
    if (++n % 120 == 0) {
        rt_log("formes : %llu alternent une image sur deux, sur %llu qui changent (%zu formes suivies)",
               (unsigned long long)alternees, (unsigned long long)suivies, g_presence.size());
        /* les suites de présence les plus fréquentes parmi celles qui changent : 11011011 dit « un tour
           sur trois sauté », 10000000 dit « elle vient d'apparaître », 01111111 « elle vient de partir » */
        unsigned compte[256] = {0};
        for (auto &kv : g_presence) if (kv.second && kv.second != 0xFF) compte[kv.second]++;
        for (int tour = 0; tour < 5; tour++) {
            int mieux = 0;
            for (int k = 1; k < 256; k++) if (compte[k] > compte[mieux]) mieux = k;
            if (!compte[mieux]) break;
            char bits[9];
            for (int b2 = 0; b2 < 8; b2++) bits[b2] = (mieux >> (7 - b2)) & 1 ? '1' : '0';
            bits[8] = 0;
            rt_log("   %s : %u formes (le tour le plus ancien à gauche)", bits, compte[mieux]);
            compte[mieux] = 0;
        }
    }
}

namespace {

/* matrices 4×4 en double, ligne-majeure, vecteur ligne × matrice — la convention du moteur géométrique */
void identite(double *m) { for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.0 : 0.0; }

void produit(const double *a, const double *b, double *r) {          /* a puis b */
    double t[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0;
            for (int k = 0; k < 4; k++) s += a[i * 4 + k] * b[k * 4 + j];
            t[i * 4 + j] = s;
        }
    memcpy(r, t, sizeof t);
}

bool inverser(const double *m, double *r) {
    double a[4][8];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) { a[i][j] = m[i * 4 + j]; a[i][4 + j] = i == j ? 1.0 : 0.0; }
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int i = c + 1; i < 4; i++) if (std::fabs(a[i][c]) > std::fabs(a[p][c])) p = i;
        if (std::fabs(a[p][c]) < 1e-12) return false;
        if (p != c) for (int j = 0; j < 8; j++) std::swap(a[p][j], a[c][j]);
        double d = 1.0 / a[c][c];
        for (int j = 0; j < 8; j++) a[c][j] *= d;
        for (int i = 0; i < 4; i++) {
            if (i == c || a[i][c] == 0.0) continue;
            double f = a[i][c];
            for (int j = 0; j < 8; j++) a[i][j] -= f * a[c][j];
        }
    }
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r[i * 4 + j] = a[i][4 + j];
    return true;
}

inline void appliquer(const double *v, const double *m, double *o) {
    for (int j = 0; j < 4; j++) o[j] = v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j] + v[3] * m[12 + j];
}

inline double ecart2(const double *p, const double *q) {
    double x = p[0] - q[0], y = p[1] - q[1], z = p[2] - q[2];
    return x * x + y * y + z * z;
}

/* Les projections d'une image et leurs inverses ; une projection qu'on ne sait pas inverser vaut l'identité. */
struct Projections {
    double p[GX_PROJS_MAX][16], inv[GX_PROJS_MAX][16];
    int n;
    explicit Projections(const GxFrame &f) : n(f.n_projs) {
        for (int k = 0; k < n; k++) {
            for (int i = 0; i < 16; i++) p[k][i] = f.projs[k][i] / 4096.0;
            if (!inverser(p[k], inv[k])) { identite(p[k]); identite(inv[k]); }
        }
    }
};

/* Les sommets dans le repère de la caméra : 4 sommets × (x, y, z, w) par polygone. */
void vers_camera(const std::vector<GxPolygon> &polys, const Projections &pr, std::vector<double> &out) {
    out.resize(polys.size() * 16);
    for (size_t i = 0; i < polys.size(); i++) {
        const GxPolygon &q = polys[i];
        for (int k = 0; k < q.count; k++) {
            double c[4] = {q.v[k].x / 4096.0, q.v[k].y / 4096.0, q.v[k].z / 4096.0, q.v[k].w / 4096.0};
            if (q.proj < pr.n) appliquer(c, pr.inv[q.proj], &out[i * 16 + k * 4]);
            else memcpy(&out[i * 16 + k * 4], c, sizeof c);
        }
    }
}

/* Le déplacement D tel que b ≈ a × D, par moindres carrés pondérés — chaque point pèse l'inverse du
   carré de sa distance, l'erreur qu'on minimise est donc un angle, celui que l'œil voit. Ce qui bouge
   pour son propre compte (un ennemi, le vaisseau) est écarté tour après tour. Une légère attache à
   l'identité tient les directions que les points ne contraignent pas (une scène toute plate). */
struct Paire { const double *a, *b; };

bool estimer(const std::vector<Paire> &paires, double *D, size_t *retenues, double *residu) {
    if (paires.size() < 12) return false;
    std::vector<char> garde(paires.size(), 1);
    std::vector<double> r(paires.size());
    size_t n_garde = paires.size();
    for (int tour = 0; tour < 4; tour++) {
        double N[16] = {0}, R[16] = {0};
        for (size_t k = 0; k < paires.size(); k++) {
            if (!garde[k]) continue;
            const double *a = paires[k].a, *b = paires[k].b;
            double w = 1.0 / (a[0] * a[0] + a[1] * a[1] + a[2] * a[2] + 1.0);
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++) { N[i * 4 + j] += w * a[i] * a[j]; R[i * 4 + j] += w * a[i] * b[j]; }
        }
        double lambda = 1e-6 * (N[0] + N[5] + N[10] + N[15]) / 4.0 + 1e-12;
        for (int i = 0; i < 4; i++) { N[i * 4 + i] += lambda; R[i * 4 + i] += lambda; }
        double Ninv[16];
        if (!inverser(N, Ninv)) return false;
        produit(Ninv, R, D);
        for (size_t k = 0; k < paires.size(); k++) {
            double p[4];
            appliquer(paires[k].a, D, p);
            const double *b = paires[k].b;
            r[k] = std::sqrt(ecart2(p, b) / (b[0] * b[0] + b[1] * b[1] + b[2] * b[2] + 1.0));
        }
        std::vector<double> tri;
        tri.reserve(n_garde);
        for (size_t k = 0; k < paires.size(); k++) if (garde[k]) tri.push_back(r[k]);
        std::nth_element(tri.begin(), tri.begin() + tri.size() / 2, tri.end());
        double mediane = tri[tri.size() / 2];
        *residu = mediane;
        double seuil = std::max(3.0 * mediane, 5e-4);
        n_garde = 0;
        for (size_t k = 0; k < paires.size(); k++) { garde[k] = r[k] <= seuil; n_garde += garde[k]; }
        if (n_garde < 12 || n_garde * 10 < paires.size() * 3) return false;
    }
    *retenues = n_garde;
    /* un déplacement de caméra : ni grossissement, ni perspective */
    double det = D[0] * (D[5] * D[10] - D[6] * D[9]) - D[1] * (D[4] * D[10] - D[6] * D[8]) + D[2] * (D[4] * D[9] - D[5] * D[8]);
    if (det < 0.6 || det > 1.6) return false;
    if (std::fabs(D[3]) > 0.05 || std::fabs(D[7]) > 0.05 || std::fabs(D[11]) > 0.05 || std::fabs(D[15] - 1.0) > 0.05) return false;
    return true;
}

struct Candidat { double cout; int i, j; };

inline uint32_t famille(const GxPolygon &q) {
    /* tout de la texture sauf son adresse (une animation change de vignette), le mode et l'identifiant du polygone */
    return (q.texparam & 0xFFFF0000u) * 0x9E3779B1u ^ (q.attr & 0x3F0000F0u) * 0x85EBCA77u ^ (uint32_t)q.count;
}

inline int32_t borne(double v) {
    return v >= 2147483647.0 ? INT32_MAX : v <= -2147483648.0 ? INT32_MIN : (int32_t)std::lround(v);
}

/* De combien la forme d'un polygone change-t-elle entre deux images, à l'écran ? On compare chaque
   arête à son homologue et on rend le rapport entre la plus étirée et la plus rétrécie : 1 pour un
   mouvement qui garde la forme (un rapprochement, une rotation), beaucoup plus pour un pareil qui
   n'est pas le bon. Rend 0 quand la mesure n'a pas de sens : un sommet derrière la caméra, ou des
   arêtes trop courtes pour qu'un rapport veuille dire quelque chose. `centres` reçoit, si on le
   demande, le centre à l'écran du polygone récent et les deux arêtes les plus longues. */
double distorsion(const GxPolygon &o, const GxPolygon &p, double *mesures) {
    if (o.count != p.count || p.count < 3) return 0;
    double rmin = 1e30, rmax = 0, cx = 0, cy = 0, lomax = 0, lpmax = 0;
    for (int k = 0; k < p.count; k++) {
        int k2 = (k + 1) % p.count;
        if (o.v[k].w <= 0 || o.v[k2].w <= 0 || p.v[k].w <= 0 || p.v[k2].w <= 0) return 0;
        double ox = (double)o.v[k].x / o.v[k].w * 128.0, oy = (double)o.v[k].y / o.v[k].w * 96.0;
        double ox2 = (double)o.v[k2].x / o.v[k2].w * 128.0, oy2 = (double)o.v[k2].y / o.v[k2].w * 96.0;
        double px = (double)p.v[k].x / p.v[k].w * 128.0, py = (double)p.v[k].y / p.v[k].w * 96.0;
        double px2 = (double)p.v[k2].x / p.v[k2].w * 128.0, py2 = (double)p.v[k2].y / p.v[k2].w * 96.0;
        cx += px / p.count; cy += py / p.count;
        double lo = std::sqrt((ox2 - ox) * (ox2 - ox) + (oy2 - oy) * (oy2 - oy));
        double lp = std::sqrt((px2 - px) * (px2 - px) + (py2 - py) * (py2 - py));
        if (lo > lomax) lomax = lo;
        if (lp > lpmax) lpmax = lp;
        if (lo < 1.5 && lp < 1.5) continue;          /* deux arêtes courtes : le rapport n'y veut rien dire */
        double r = (lp + 0.5) / (lo + 0.5);
        if (r < rmin) rmin = r;
        if (r > rmax) rmax = r;
    }
    if (rmax <= 0 || rmin > 1e29) return 0;
    if (mesures) { mesures[0] = cx; mesures[1] = cy; mesures[2] = lomax; mesures[3] = lpmax; }
    return rmax / rmin;
}

/* Au-delà de ce rapport, la forme ne s'interpole plus : une arête qui change trois fois plus qu'une
   autre du même polygone n'est pas un mouvement, c'est un pareil qui n'est pas le bon. */
const double SEUIL_RAIDE = 3.0;

/* SFC_REALIGNER=0 : sans le second tour de l'étage des objets entiers, pour comparer */
const bool SANS_REALIGNEMENT = [] { const char *e = getenv("SFC_REALIGNER"); return e && *e == '0'; }();

inline int32_t entre(int32_t a, int32_t b, int32_t f16) { return a + (int32_t)(((int64_t)b - a) * f16 >> 16); }

}  // namespace

Lien apparier(const GxFrame &fa, const std::vector<GxPolygon> &a, const GxFrame &fb, const std::vector<GxPolygon> &b) {
    Lien l;
    l.pareil.assign(b.size(), -1);
    for (int p = 0; p < GX_PROJS_MAX; p++) { identite(l.avant[p]); identite(l.arriere[p]); }
    Projections pa(fa), pb(fb);
    std::vector<double> va, vb;
    vers_camera(a, pa, va);
    vers_camera(b, pb, vb);

    struct Groupe { std::vector<int> a, b; };
    std::unordered_map<uint32_t, Groupe> groupes;
    groupes.reserve(a.size() + b.size());
    for (size_t i = 0; i < a.size(); i++) groupes[a[i].id].a.push_back((int)i);
    for (size_t j = 0; j < b.size(); j++) groupes[b[j].id].b.push_back((int)j);

    /* ---- le mouvement de la caméra, par projection, sur les paires sans équivoque */
    std::vector<Paire> sures[GX_PROJS_MAX];
    int votes[GX_PROJS_MAX][GX_PROJS_MAX] = {};
    for (auto &kv : groupes) {
        const Groupe &g = kv.second;
        if (g.a.size() != 1 || g.b.size() != 1) continue;
        int i = g.a[0], j = g.b[0];
        if (a[i].count != b[j].count || a[i].proj >= pa.n || b[j].proj >= pb.n) continue;
        votes[b[j].proj][a[i].proj]++;
        for (int k = 0; k < b[j].count; k++) sures[b[j].proj].push_back(Paire{&va[i * 16 + k * 4], &vb[j * 16 + k * 4]});
    }
    double D[GX_PROJS_MAX][16];                 /* par projection de B */
    int de_b[GX_PROJS_MAX], de_a[GX_PROJS_MAX];  /* la projection de A qui répond à celle de B, et l'inverse */
    for (int p = 0; p < GX_PROJS_MAX; p++) { identite(D[p]); de_b[p] = de_a[p] = -1; }
    for (int q = 0; q < pb.n; q++) {
        int meilleur = -1;
        for (int p = 0; p < pa.n; p++) if (votes[q][p] > (meilleur < 0 ? 0 : votes[q][meilleur])) meilleur = p;
        if (meilleur < 0)
            for (int p = 0; p < pa.n && meilleur < 0; p++) if (!memcmp(fa.projs[p], fb.projs[q], sizeof fa.projs[p])) meilleur = p;
        if (meilleur < 0) continue;
        de_b[q] = meilleur;
        if (de_a[meilleur] < 0) de_a[meilleur] = q;
        /* seules comptent les paires dont l'ancienne vient de cette projection-là */
        std::vector<Paire> &s = sures[q];
        l.sures += s.size();
        size_t retenues = 0;
        double residu = 0;
        double d[16];
        if (estimer(s, d, &retenues, &residu)) {
            memcpy(D[q], d, sizeof d);
            l.retenues += retenues;
            if (residu > l.residu) l.residu = residu;
        }
    }
    for (int q = 0; q < pb.n; q++) {
        if (de_b[q] < 0) continue;
        double dinv[16], t[16];
        if (!inverser(D[q], dinv)) { identite(D[q]); identite(dinv); }
        produit(pb.inv[q], dinv, t); produit(t, pa.p[de_b[q]], l.arriere[q]);
        if (de_a[de_b[q]] == q) { produit(pa.inv[de_b[q]], D[q], t); produit(t, pb.p[q], l.avant[de_b[q]]); }
    }
    /* où serait chaque polygone de A si rien ne bougeait que la caméra */
    std::vector<double> pred(va.size());
    for (size_t i = 0; i < a.size(); i++) {
        int q = a[i].proj < pa.n ? de_a[a[i].proj] : -1;
        for (int k = 0; k < a[i].count; k++) {
            if (q >= 0) appliquer(&va[i * 16 + k * 4], D[q], &pred[i * 16 + k * 4]);
            else memcpy(&pred[i * 16 + k * 4], &va[i * 16 + k * 4], 4 * sizeof(double));
        }
    }

    std::vector<char> pris(a.size(), 0);
    std::vector<Candidat> cands;
    l.par_voisinage.assign(b.size(), 0);
    /* l'écart entre un polygone de A et un de B, relatif à la distance : celui que prédit le mouvement de la
       caméra — ou, pour ce qui est attaché à elle (le vaisseau du joueur, l'interface), celui qui n'a pas bougé */
    auto cout = [&](int i, int j) {
        double cd = 0, ci = 0, n2 = 0;
        for (int k = 0; k < b[j].count; k++) {
            const double *ub = &vb[j * 16 + k * 4];
            cd += ecart2(&pred[i * 16 + k * 4], ub);
            ci += ecart2(&va[i * 16 + k * 4], ub);
            n2 += ub[0] * ub[0] + ub[1] * ub[1] + ub[2] * ub[2] + 1.0;
        }
        return std::min(cd, ci) / n2;
    };
    const double seuil = 0.3 * 0.3;             /* son mouvement propre : moins du tiers de sa distance, en un tour */
    /* SFC_DIAG_DEFORME=s : journalise à partir de l'image 3D numéro s (1 pour tout) */
    static int diag_deforme = -1;
    if (diag_deforme < 0) { const char *e = getenv("SFC_DIAG_DEFORME"); diag_deforme = e ? atoi(e) : 0; }
    const bool deforme_ici = diag_deforme && (uint32_t)diag_deforme <= fb.serial;
    std::vector<uint8_t> niveau;                /* pour le diagnostic : par quel étage la paire a été faite */
    niveau.assign(b.size(), 0);
    int etage = 1;
    auto lier = [&](int i, int j, bool voisinage) {
        pris[i] = 1; l.pareil[j] = i; l.apparies++;
        if (voisinage) { l.par_voisinage[j] = 1; l.seconds++; }
        if (!niveau.empty()) niveau[j] = (uint8_t)etage;
    };

    /* ---- premier niveau : les objets entiers. Un objet, c'est une suite de polygones placés par la même
       matrice — un ennemi, une aile, une particule faite de deux moitiés. S'il soumet les mêmes polygones dans
       le même ordre qu'au tour d'avant, on l'apparie d'un bloc : ses polygones ne peuvent plus partir chacun
       de son côté vers l'objet voisin qui lui ressemble, et un ennemi ne se déchire plus. */
    etage = 1;
    l.change.assign(b.size(), 0);
    l.objet_a.assign(a.size(), -1);
    l.objet_b.assign(b.size(), -1);
    struct Bloc { int debut, n; uint32_t forme, identite; };
    auto blocs_de = [](const std::vector<GxPolygon> &polys) {
        std::vector<Bloc> blocs;
        for (size_t i = 0; i < polys.size(); i++) {
            const GxPolygon &q = polys[i];
            bool suite = !blocs.empty() && polys[i - 1].ox == q.ox && polys[i - 1].oy == q.oy && polys[i - 1].oz == q.oz && polys[i - 1].proj == q.proj;
            if (!suite) blocs.push_back(Bloc{(int)i, 0, 0x811C9DC5u, 0x811C9DC5u});
            Bloc &k = blocs.back();
            k.n++;
            k.forme = (k.forme ^ q.forme) * 0x01000193u;
            k.identite = (k.identite ^ q.id) * 0x01000193u;
        }
        return blocs;
    };
    std::vector<Bloc> ba = blocs_de(a), bb = blocs_de(b);
    {
        struct Paquet { std::vector<int> a, b; };
        std::unordered_map<uint64_t, Paquet> paquets;
        for (size_t k = 0; k < ba.size(); k++) if (ba[k].n >= 2) paquets[((uint64_t)ba[k].forme << 20) ^ (uint64_t)ba[k].n].a.push_back((int)k);
        for (size_t k = 0; k < bb.size(); k++) if (bb[k].n >= 2) paquets[((uint64_t)bb[k].forme << 20) ^ (uint64_t)bb[k].n].b.push_back((int)k);
        std::vector<char> pris_a(ba.size(), 0), pris_b(bb.size(), 0);
        for (auto &kv : paquets) {
            const Paquet &g = kv.second;
            if (g.a.empty() || g.b.empty() || g.a.size() * g.b.size() > 250000) continue;
            cands.clear();
            for (int x : g.a)
                for (int y : g.b) {
                    /* trois polygones témoins : le premier, celui du milieu, le dernier */
                    const Bloc &p = ba[x], &q = bb[y];
                    double c = (cout(p.debut, q.debut) + cout(p.debut + p.n / 2, q.debut + q.n / 2) + cout(p.debut + p.n - 1, q.debut + q.n - 1)) / 3.0;
                    if (p.identite != q.identite) c += 1e-6;           /* à écart égal, celui qui a gardé ses textures */
                    if (c < seuil) cands.push_back(Candidat{c, x, y});
                }
            std::sort(cands.begin(), cands.end(), [](const Candidat &x, const Candidat &y) { return x.cout < y.cout; });
            for (const Candidat &c : cands) {
                if (pris_a[c.i] || pris_b[c.j]) continue;
                pris_a[c.i] = pris_b[c.j] = 1;
                for (int k = 0; k < ba[c.i].n; k++) lier(ba[c.i].debut + k, bb[c.j].debut + k, false);
            }
        }

        /* Second tour : le même objet, au même endroit, avec autant de polygones, dont certains ont changé de
           forme sur place. Le vaisseau mère allume et éteint les panneaux de ses cinq bras en réécrivant leurs
           sommets : un tour sur trois, 80 de leurs 156 polygones changent, le bloc n'était plus reconnu, et
           chaque polygone cherchait son pareil par sa seule forme — dans le bras d'à côté, qui a les mêmes.
           On les apparie ici rang par rang ; ceux dont la forme n'a pas changé disent où l'objet est allé. */
        if (!SANS_REALIGNEMENT) {
            for (size_t y = 0; y < bb.size(); y++) {
                const Bloc &q = bb[y];
                if (pris_b[y] || q.n < 8) continue;
                /* un écart dix fois plus serré qu'au premier tour : la preuve est plus mince (une partie des
                   formes seulement), la place doit être plus sûre — les bras mesurent 0,00001 à 0,0004 */
                int meilleur = -1; double cm = seuil / 10.0;
                for (size_t x = 0; x < ba.size(); x++) {
                    const Bloc &p = ba[x];
                    if (pris_a[x] || p.n != q.n || a[p.debut].proj != b[q.debut].proj) continue;
                    /* un quart au moins de ses polygones gardent leur forme à leur rang — les bras du vaisseau
                       mère en gardent 71 à 78 sur 156 —, et ceux-là sont là où le mouvement de la caméra les
                       attend (l'écart médian) */
                    std::vector<double> ecarts;
                    for (int k = 0; k < q.n; k++)
                        if (a[p.debut + k].forme == b[q.debut + k].forme && a[p.debut + k].count == b[q.debut + k].count)
                            ecarts.push_back(cout(p.debut + k, q.debut + k));
                    if (ecarts.size() * 4 < (size_t)q.n) continue;
                    std::nth_element(ecarts.begin(), ecarts.begin() + ecarts.size() / 2, ecarts.end());
                    double c = ecarts[ecarts.size() / 2];
                    if (c < cm) { cm = c; meilleur = (int)x; }
                }
                if (meilleur < 0) continue;
                pris_a[meilleur] = pris_b[y] = 1;
                const Bloc &p = ba[meilleur];
                int objet = (int)l.objet_avant.size();
                std::vector<Paire> sures_objet;
                for (int k = 0; k < q.n; k++) {
                    int i = p.debut + k, j = q.debut + k;
                    l.objet_a[i] = objet; l.objet_b[j] = objet;
                    if (a[i].count != b[j].count) continue;         /* ne s'apparient pas : chacun suivra l'objet */
                    lier(i, j, false);
                    if (a[i].forme != b[j].forme) l.change[j] = 1;
                    else for (int s2 = 0; s2 < b[j].count; s2++) sures_objet.push_back(Paire{&va[i * 16 + s2 * 4], &vb[j * 16 + s2 * 4]});
                }
                /* le mouvement propre de l'objet, estimé sur ses polygones inchangés ; à défaut, celui de la caméra */
                std::vector<double> av(16), ar(16);
                double d[16]; size_t ret = 0; double res = 0;
                int pa_ = a[p.debut].proj, pb_ = b[q.debut].proj;
                if (pa_ < pa.n && pb_ < pb.n && estimer(sures_objet, d, &ret, &res)) {
                    double dinv[16], t[16];
                    if (!inverser(d, dinv)) { identite(d); identite(dinv); }
                    produit(pa.inv[pa_], d, t); produit(t, pb.p[pb_], av.data());
                    produit(pb.inv[pb_], dinv, t); produit(t, pa.p[pa_], ar.data());
                } else {
                    memcpy(av.data(), l.avant[pa_ < GX_PROJS_MAX ? pa_ : 0], sizeof(double) * 16);
                    memcpy(ar.data(), l.arriere[pb_ < GX_PROJS_MAX ? pb_ : 0], sizeof(double) * 16);
                }
                l.objet_avant.push_back(av);
                l.objet_arriere.push_back(ar);
                l.realignes++;
            }
        }

        /* SFC_DIAG_BLOCS=s[:n] : pour les images 3D s à s+n (12 par défaut), les objets de huit polygones ou plus — reconnus
           ou non — et, pour ceux qui ne l'ont pas été, ce qui a changé par rapport à l'objet de même taille
           le plus proche dans l'image d'avant : combien de polygones diffèrent à rang égal, et lesquels. */
        static long diag_blocs = -2, diag_blocs_n = 12;
        if (diag_blocs == -2) {
            const char *e = getenv("SFC_DIAG_BLOCS");                  /* s, ou s:n pour n images */
            diag_blocs = e ? atol(e) : -1;
            if (e && strchr(e, ':')) diag_blocs_n = atol(strchr(e, ':') + 1);
        }
        if (diag_blocs >= 0 && fb.serial >= (uint32_t)diag_blocs && fb.serial <= (uint32_t)(diag_blocs + diag_blocs_n)) {
            int grands = 0, reconnus = 0;
            for (size_t y = 0; y < bb.size(); y++) {
                const Bloc &q = bb[y];
                if (q.n < 8) continue;
                grands++;
                if (pris_b[y]) { reconnus++; continue; }
                int meilleur = -1; double cm = 1e30;
                for (size_t x = 0; x < ba.size(); x++) {
                    const Bloc &p = ba[x];
                    if (p.n != q.n || a[p.debut].proj != b[q.debut].proj) continue;
                    double c = (cout(p.debut, q.debut) + cout(p.debut + p.n / 2, q.debut + q.n / 2) + cout(p.debut + p.n - 1, q.debut + q.n - 1)) / 3.0;
                    if (c < cm) { cm = c; meilleur = (int)x; }
                }
                char rangs[160]; int nr = 0, differents = 0;
                rangs[0] = 0;
                if (meilleur >= 0) {
                    const Bloc &p = ba[meilleur];
                    for (int k = 0; k < q.n; k++)
                        if (a[p.debut + k].forme != b[q.debut + k].forme || a[p.debut + k].count != b[q.debut + k].count) {
                            differents++;
                            if (nr < 140) nr += snprintf(rangs + nr, sizeof rangs - (size_t)nr, " %d", k);
                        }
                }
                int proche = -1; double cp = 1e30;
                for (size_t x = 0; x < ba.size(); x++) {
                    if (ba[x].n < 4 || a[ba[x].debut].proj != b[q.debut].proj) continue;
                    double c = cout(ba[x].debut, q.debut);
                    if (c < cp) { cp = c; proche = (int)x; }
                }
                rt_log("blocs : image 3D %u, objet de %d polygones (rang %d) NON reconnu ; même taille le plus proche : %s"
                       " écart %.5f, %d différents à rang égal :%s ; le plus proche en tout : %d polygones, écart %.5f",
                       fb.serial, q.n, q.debut, meilleur >= 0 ? "oui," : "aucun,", meilleur >= 0 ? cm : 0.0, differents, rangs,
                       proche >= 0 ? ba[proche].n : 0, proche >= 0 ? cp : 0.0);
            }
            size_t pol1 = 0;
            for (size_t j = 0; j < b.size(); j++) pol1 += l.pareil[j] >= 0;
            rt_log("blocs : image 3D %u -> %u — %d objets de huit polygones ou plus, %d reconnus d'un bloc ; %zu polygones en tout, %zu appariés à l'étage 1",
                   fa.serial, fb.serial, grands, reconnus, b.size(), pol1);
        }
    }

    /* ---- deuxième niveau : les polygones un à un, par leur forme. Ce qui reste : les objets d'un seul
       polygone, et ceux dont la liste a changé (une tuile de sol dont le jeu ne soumet que la part visible). */
    etage = 2;
    {
        struct Paquet { std::vector<int> a, b; };
        std::unordered_map<uint32_t, Paquet> paquets;
        for (size_t i = 0; i < a.size(); i++) if (!pris[i] && l.objet_a[i] < 0) paquets[a[i].forme].a.push_back((int)i);
        for (size_t j = 0; j < b.size(); j++) if (l.pareil[j] < 0 && l.objet_b[j] < 0) paquets[b[j].forme].b.push_back((int)j);
        for (auto &kv : paquets) {
            const Paquet &g = kv.second;
            if (g.a.empty() || g.b.empty()) continue;
            if (g.a.size() * g.b.size() > 250000) {  /* démesuré : on s'en remet à l'ordre */
                for (size_t k = 0; k < g.a.size() && k < g.b.size(); k++)
                    if (a[g.a[k]].count == b[g.b[k]].count && cout(g.a[k], g.b[k]) < seuil) lier(g.a[k], g.b[k], false);
                continue;
            }
            cands.clear();
            for (int i : g.a)
                for (int j : g.b) {
                    if (a[i].count != b[j].count) continue;
                    double c = cout(i, j) + (a[i].id != b[j].id ? 1e-6 : 0.0);
                    if (c < seuil) cands.push_back(Candidat{c, i, j});
                }
            std::sort(cands.begin(), cands.end(), [](const Candidat &x, const Candidat &y) { return x.cout < y.cout; });
            for (const Candidat &c : cands) if (!pris[c.i] && l.pareil[c.j] < 0) lier(c.i, c.j, false);
        }
    }

    /* ---- troisième niveau : ce qui n'a pas de forme stable, par le voisinage. Une particule dont le jeu
       recalcule les sommets à chaque tour est là où elle était, à peu près de la même taille. On l'apparie par
       pièces : des polygones qui se suivent et se touchent — les deux moitiés d'une flamme — vont ensemble ou
       ne vont pas. Appariées séparément, l'une partait vers la flamme voisine et l'explosion se voyait en
       morceaux. */
    etage = 3;
    struct Piece { int debut, n; uint32_t famille; double c[3], p[3], rayon; };
    auto touche = [](const GxPolygon &x, const GxPolygon &y) {
        for (int i = 0; i < x.count; i++)
            for (int j = 0; j < y.count; j++)
                if (x.v[i].x == y.v[j].x && x.v[i].y == y.v[j].y && x.v[i].z == y.v[j].z && x.v[i].w == y.v[j].w) return true;
        return false;
    };
    auto pieces_de = [&](const std::vector<GxPolygon> &polys, const std::vector<double> &v, const std::vector<double> *pr, auto libre) {
        std::vector<Piece> pieces;
        for (size_t i = 0; i < polys.size(); i++) {
            if (!libre(i)) continue;
            bool suite = !pieces.empty() && pieces.back().debut + pieces.back().n == (int)i && pieces.back().n < 64
                         && pieces.back().famille == famille(polys[i]) && polys[i - 1].proj == polys[i].proj && touche(polys[i - 1], polys[i]);
            if (!suite) pieces.push_back(Piece{(int)i, 0, famille(polys[i]), {0, 0, 0}, {0, 0, 0}, 0});
            pieces.back().n++;
        }
        for (Piece &k : pieces) {
            int sommets = 0;
            for (int i = k.debut; i < k.debut + k.n; i++) sommets += polys[i].count;
            for (int i = k.debut; i < k.debut + k.n; i++)
                for (int s = 0; s < polys[i].count; s++)
                    for (int d = 0; d < 3; d++) {
                        k.c[d] += v[i * 16 + s * 4 + d] / sommets;
                        if (pr) k.p[d] += (*pr)[i * 16 + s * 4 + d] / sommets;
                    }
            for (int i = k.debut; i < k.debut + k.n; i++)
                for (int s = 0; s < polys[i].count; s++) k.rayon = std::max(k.rayon, std::sqrt(ecart2(&v[i * 16 + s * 4], k.c)));
        }
        return pieces;
    };
    {
        std::vector<Piece> ra = pieces_de(a, va, &pred, [&](size_t i) { return !pris[i] && l.objet_a[i] < 0; });
        std::vector<Piece> rb = pieces_de(b, vb, nullptr, [&](size_t j) { return l.pareil[j] < 0 && l.objet_b[j] < 0; });
        struct Paquet { std::vector<int> a, b; };
        std::unordered_map<uint64_t, Paquet> paquets;
        auto cle = [&](const std::vector<GxPolygon> &polys, const Piece &k) {
            uint64_t h = ((uint64_t)k.famille << 8) ^ (uint64_t)k.n;
            for (int i = k.debut; i < k.debut + k.n; i++) h = h * 31 + polys[i].count;
            return h;
        };
        for (size_t k = 0; k < ra.size(); k++) paquets[cle(a, ra[k])].a.push_back((int)k);
        for (size_t k = 0; k < rb.size(); k++) paquets[cle(b, rb[k])].b.push_back((int)k);
        std::vector<char> pris_a(ra.size(), 0), pris_b(rb.size(), 0);
        for (auto &kv : paquets) {
            const Paquet &g = kv.second;
            if (g.a.empty() || g.b.empty() || g.a.size() * g.b.size() > 100000) continue;
            cands.clear();
            for (int x : g.a)
                for (int y : g.b) {
                    const Piece &p = ra[x], &q = rb[y];
                    double portee = 1.5 * std::max(p.rayon, q.rayon) + 0.01 * std::sqrt(q.c[0] * q.c[0] + q.c[1] * q.c[1] + q.c[2] * q.c[2]);
                    double c = std::min(ecart2(p.p, q.c), ecart2(p.c, q.c)) / (portee * portee + 1e-12);
                    if (c < 1.0 && p.rayon < 3.0 * q.rayon + 1e-9 && q.rayon < 3.0 * p.rayon + 1e-9) cands.push_back(Candidat{c, x, y});
                }
            std::sort(cands.begin(), cands.end(), [](const Candidat &x, const Candidat &y) { return x.cout < y.cout; });
            for (const Candidat &c : cands) {
                if (pris_a[c.i] || pris_b[c.j]) continue;
                pris_a[c.i] = pris_b[c.j] = 1;
                for (int k = 0; k < ra[c.i].n; k++) lier(ra[c.i].debut + k, rb[c.j].debut + k, true);
            }
        }
    }

    for (size_t i = 0; i < a.size(); i++) if (!pris[i]) l.disparus.push_back((int)i);
    /* à plat : la colonne w de la projection ne dépend pas de la profondeur */
    for (int q = 0; q < pb.n; q++)
        l.a_plat[q] = fb.projs[q][3] == 0 && fb.projs[q][7] == 0 && fb.projs[q][11] == 0;
    size_t grand = std::max(a.size(), b.size()), petit = std::min(a.size(), b.size());
    l.coupure = grand == 0 || l.apparies * 2 < b.size() || petit * 10 < grand * 4;

    l.etage = niveau;
    l.serie_a = fa.serial; l.serie_b = fb.serial;
    /* SFC_DIAG_RANGS=s : à l'image 3D s, pour le premier objet de plus de cent polygones non reconnu d'un
       bloc, chaque rang : même forme qu'au rang égal de l'objet de même taille le plus proche, apparié ou
       non, par quel étage, et à quel rang de quel objet */
    {
        static long diag_rangs = -2;
        if (diag_rangs == -2) { const char *e = getenv("SFC_DIAG_RANGS"); diag_rangs = e ? atol(e) : -1; }
        if (diag_rangs >= 0 && fb.serial == (uint32_t)diag_rangs) {
            for (size_t y = 0; y < bb.size(); y++) {
                const Bloc &q = bb[y];
                if (q.n < 100) continue;
                bool entier = true;
                for (int k = 0; k < q.n && entier; k++) if (niveau[q.debut + k] != 1) entier = false;
                if (entier) continue;
                int meilleur = -1; double cm = 1e30;
                for (size_t x = 0; x < ba.size(); x++) {
                    const Bloc &p = ba[x];
                    if (p.n != q.n) continue;
                    double c = cout(p.debut, q.debut);
                    if (c < cm) { cm = c; meilleur = (int)x; }
                }
                rt_log("rangs : image 3D %u, objet de %d polygones au rang %d, pareil de même taille : objet au rang %d (écart du premier %.5f)",
                       fb.serial, q.n, q.debut, meilleur >= 0 ? ba[meilleur].debut : -1, cm);
                auto bloc_de = [&](const std::vector<Bloc> &bl, int i) {
                    for (size_t k = 0; k < bl.size(); k++) if (i >= bl[k].debut && i < bl[k].debut + bl[k].n) return (int)k;
                    return -1;
                };
                for (int k = 0; k < q.n; k++) {
                    int j = q.debut + k, i = l.pareil[j];
                    bool meme = meilleur >= 0 && a[ba[meilleur].debut + k].forme == b[j].forme;
                    int x = i >= 0 ? bloc_de(ba, i) : -1;
                    /* combien de polygones de A ont la même forme, et le plus proche d'entre eux */
                    int n_meme = 0; double c_min = 1e30;
                    for (size_t i2 = 0; i2 < a.size(); i2++)
                        if (a[i2].forme == b[j].forme && a[i2].count == b[j].count) { n_meme++; c_min = std::min(c_min, cout((int)i2, j)); }
                    rt_log("   rang %3d : forme %08x %s ; %s étage %d%s ; %d de même forme dans A, le plus proche à %.5f",
                           k, b[j].forme, meme ? "inchangée" : "CHANGÉE  ",
                           i >= 0 ? "apparié," : "SANS PAREIL,", niveau[j],
                           i >= 0 ? (x >= 0 ? (std::string(" à l'objet de ") + std::to_string(ba[x].n) + " rang " + std::to_string(i - ba[x].debut)).c_str() : "") : "",
                           n_meme, n_meme ? c_min : 0.0);
                }
                break;
            }
        }
    }
    {   /* SFC_DIAG_BLOCS : les liens où l'étage des objets entiers n'a presque rien reconnu */
        static int diag = -1;
        if (diag < 0) diag = getenv("SFC_DIAG_BLOCS") != nullptr;
        if (diag && b.size() > 500) {
            size_t e1 = 0;
            for (size_t j = 0; j < b.size(); j++) e1 += niveau[j] == 1;
            if (e1 * 2 < b.size())
                rt_log("blocs : lien de l'image 3D %u à %u — seulement %zu polygones sur %zu reconnus par objets entiers (%zu dans l'ancienne)",
                       fa.serial, fb.serial, e1, b.size(), a.size());
        }
    }
    /* Quelles paires ne doivent pas voir leur forme interpolée (voir `raide` dans entre_images.h) */
    l.raide.assign(b.size(), 0);
    struct Pire { double d; double sx, sy; uint32_t id, forme; int etage; int count; double lo, lp; };
    std::vector<Pire> pires;
    uint64_t mesures = 0, deformes = 0;
    for (size_t j = 0; j < b.size(); j++) {
        int i = l.pareil[j];
        if (i < 0) continue;
        double m[4] = {0, 0, 0, 0};
        double d = distorsion(a[i], b[j], m);
        if (d <= 0) continue;
        mesures++;
        if (d < SEUIL_RAIDE) continue;
        static int actif = -1;                       /* SFC_RAIDE=0 : sans le garde-fou, pour comparer */
        if (actif < 0) { const char *e = getenv("SFC_RAIDE"); actif = !e || *e != '0'; }
        if (actif) l.raide[j] = 1;
        deformes++;
        if (deforme_ici) pires.push_back(Pire{d, m[0], m[1], b[j].id, b[j].forme, niveau[j], b[j].count, m[2], m[3]});
    }
    l.raides = deformes;

    /* SFC_DIAG_DEFORME=s : le détail, à partir de l'image 3D numéro s */
    if (deforme_ici && deformes) {
        std::sort(pires.begin(), pires.end(), [](const Pire &x, const Pire &y) { return x.d > y.d; });
        uint64_t par_etage[4] = {0, 0, 0, 0}, def_etage[4] = {0, 0, 0, 0};
        for (size_t j = 0; j < b.size(); j++) if (l.pareil[j] >= 0) par_etage[niveau[j] & 3]++;
        for (const Pire &q : pires) def_etage[q.etage & 3]++;
        rt_log("déforme : image 3D %u — %llu paires déformées sur %llu mesurées (%zu polygones) ; "
               "appariées par étage 1/2/3 : %llu/%llu/%llu, déformées %llu/%llu/%llu",
               fb.serial, (unsigned long long)deformes, (unsigned long long)mesures, b.size(),
               (unsigned long long)par_etage[1], (unsigned long long)par_etage[2], (unsigned long long)par_etage[3],
               (unsigned long long)def_etage[1], (unsigned long long)def_etage[2], (unsigned long long)def_etage[3]);
        for (size_t k = 0; k < pires.size() && k < 8; k++) {
            const Pire &q = pires[k];
            rt_log("   ×%.1f  écran %+.0f,%+.0f  %d sommets  arête %.0f→%.0f px  étage %d  id %08x forme %08x",
                   q.d, q.sx, q.sy, q.count, q.lo, q.lp, q.etage, q.id, q.forme);
        }
    }
    return l;
}

void interpoler(const std::vector<GxPolygon> &a, const std::vector<GxPolygon> &b, const Lien &lien, float t,
                std::vector<GxPolygon> &out) {
    int32_t f16 = (int32_t)(t * 65536.0f);
    out.clear();
    out.reserve(b.size() + lien.disparus.size());
    /* SFC_DIAG_COULEURS=1 : chaque polygone prend la couleur du chemin que l'interpolation lui fait
       suivre — rouge sans pareil, bleu disparu, jaune raide, cyan retourné, magenta apparié par le
       voisinage, vert « grossit vite » (la prédiction ne prolonge que son déplacement), orange apparié
       par sa seule forme, blanc apparié par son rang mais changé de forme. Ce qui garde ses couleurs
       a été reconnu avec son objet entier. Une image dit alors d'où vient un défaut. */
    static int couleurs = -1;
    if (couleurs < 0) couleurs = getenv("SFC_DIAG_COULEURS") != nullptr;
    enum { C_SANS = 1, C_DISPARU, C_RAIDE, C_RETOURNE, C_VOISIN, C_TAILLE, C_FORME, C_CHANGE };
    auto colorier = [&](GxPolygon &q, int c) {
        if (!couleurs) return;
        static const uint8_t rgb[9][3] = {{0, 0, 0}, {31, 0, 0}, {0, 0, 31}, {31, 31, 0}, {0, 31, 31}, {31, 0, 31}, {0, 31, 0}, {31, 14, 0}, {31, 31, 31}};
        for (int k = 0; k < q.count; k++) { q.v[k].r = rgb[c][0]; q.v[k].g = rgb[c][1]; q.v[k].b = rgb[c][2]; }
        q.attr &= ~0x30u;                              /* en modulation, pour que la couleur se voie */
    };
    /* sans pareil : il suit la caméra. `m` mène du repère de son image à celui de l'autre. */
    auto suivre = [&](GxPolygon q, const double *m, bool depuis_a) {
        if (q.proj < GX_PROJS_MAX)
            for (int k = 0; k < q.count; k++) {
                double c[4] = {(double)q.v[k].x, (double)q.v[k].y, (double)q.v[k].z, (double)q.v[k].w}, o[4];
                appliquer(c, m, o);
                double f = depuis_a ? (double)t : 1.0 - (double)t;       /* la part de l'autre image */
                q.v[k].x = borne(c[0] + (o[0] - c[0]) * f); q.v[k].y = borne(c[1] + (o[1] - c[1]) * f);
                q.v[k].z = borne(c[2] + (o[2] - c[2]) * f); q.v[k].w = borne(c[3] + (o[3] - c[3]) * f);
            }
        q.ancienne = depuis_a ? 1 : 0;
        colorier(q, depuis_a ? C_DISPARU : C_SANS);
        out.push_back(q);
    };
    size_t c = 0;                                 /* les disparus reprennent leur place dans l'ordre du dessin */
    for (size_t j = 0; j < b.size(); j++) {
        int i = lien.pareil[j];
        if (i < 0) {
            /* Sans pareil dans l'image d'avant. On le montrait à partir du milieu du chemin, pour que sa
               naissance dure un tour entier — mais sur un objet détaillé, où quelques polygones changent de
               forme à chaque tour, cela les fait disparaître une image sur deux : le joueur voit le vaisseau
               cligner par morceaux. Mieux vaut le montrer tout de suite, un peu tôt, que par intermittence. */
            if (j < lien.objet_b.size() && lien.objet_b[j] >= 0) suivre(b[j], lien.objet_arriere[lien.objet_b[j]].data(), false);
            else suivre(b[j], lien.arriere[b[j].proj < GX_PROJS_MAX ? b[j].proj : 0], false);
            continue;
        }
        /* de même pour celui qui a disparu : il reste jusqu'au bout du chemin plutôt que de s'effacer à
           mi-course, faute de quoi le trou qu'il laisse clignote lui aussi */
        while (c < lien.disparus.size() && lien.disparus[c] < i) {
            int id = lien.disparus[c++];
            const GxPolygon &d = a[id];
            if ((size_t)id < lien.objet_a.size() && lien.objet_a[id] >= 0) suivre(d, lien.objet_avant[lien.objet_a[id]].data(), true);
            else suivre(d, lien.avant[d.proj < GX_PROJS_MAX ? d.proj : 0], true);
        }
        GxPolygon q = b[j];
        const GxPolygon &o = a[i];
        if (j < lien.change.size() && lien.change[j] && lien.objet_b[j] >= 0) {
            const int ob = lien.objet_b[j];
            /* la forme d'avant emportée vers B, ou la nouvelle ramenée vers A ; vers un aperçu, toujours celle
               d'avant : la nouvelle attend la vraie image du jeu */
            const bool avant = f16 < 32768 || lien.b_apercu;
            GxPolygon r = avant ? o : q;
            const double *m = avant ? lien.objet_avant[ob].data() : lien.objet_arriere[ob].data();
            double f = avant ? (double)t : 1.0 - (double)t;
            for (int k = 0; k < r.count; k++) {
                double v4[4] = {(double)r.v[k].x, (double)r.v[k].y, (double)r.v[k].z, (double)r.v[k].w}, w4[4];
                appliquer(v4, m, w4);
                r.v[k].x = borne(v4[0] + (w4[0] - v4[0]) * f); r.v[k].y = borne(v4[1] + (w4[1] - v4[1]) * f);
                r.v[k].z = borne(v4[2] + (w4[2] - v4[2]) * f); r.v[k].w = borne(v4[3] + (w4[3] - v4[3]) * f);
            }
            r.ancienne = avant ? 1 : 0;
            colorier(r, C_CHANGE);
            out.push_back(r);
            continue;
        }
        /* on ne prolonge que ce dont on est sûr : un appariement par le seul voisinage (une flamme, une
           étincelle) sait placer le polygone entre deux images, pas deviner où il ira */
        if (f16 > 65536 && lien.par_voisinage[j]) { q.ancienne = 0; colorier(q, C_VOISIN); out.push_back(q); continue; }
        /* l'interface à plat avance par sauts de pixel (la flèche « suite » d'un dialogue) : prolonger un saut
           en invente un second ; elle reste où le jeu l'a mise jusqu'à l'image suivante */
        if (f16 > 65536 && q.proj < GX_PROJS_MAX && lien.a_plat[q.proj]) { q.ancienne = 0; out.push_back(q); continue; }
        /* Ce qui change vite de taille — une explosion qui gonfle, un éclair — ralentit aussi vite : prolonger
           sa croissance l'enflait bien au-delà de ce que le jeu montrera. Pour lui, la prédiction ne prolonge
           que le déplacement : le polygone de la plus récente, décalé comme son centre. */
        if (f16 > 65536) {
            double co[4] = {0, 0, 0, 0}, cq[4] = {0, 0, 0, 0};
            for (int k = 0; k < q.count; k++) {
                co[0] += o.v[k].x; co[1] += o.v[k].y; co[2] += o.v[k].z; co[3] += o.v[k].w;
                cq[0] += q.v[k].x; cq[1] += q.v[k].y; cq[2] += q.v[k].z; cq[3] += q.v[k].w;
            }
            for (int d = 0; d < 4; d++) { co[d] /= q.count; cq[d] /= q.count; }
            auto taille = [&](const GxPolygon &p, const double *c) {
                double r = 0;
                for (int k = 0; k < p.count; k++) {
                    double w = std::fabs((double)p.v[k].w) + 1.0, dx = (double)p.v[k].x / w - c[0] / (std::fabs(c[3]) + 1.0),
                           dy = (double)p.v[k].y / w - c[1] / (std::fabs(c[3]) + 1.0);
                    r = std::max(r, dx * dx + dy * dy);
                }
                return std::sqrt(r);
            };
            double ro = taille(o, co), rq = taille(q, cq);
            if (ro > 1e-6 && rq > 1e-6 && (rq > 1.15 * ro || ro > 1.15 * rq)) {
                double f = (double)(f16 - 65536) / 65536.0;
                for (int k = 0; k < q.count; k++) {
                    q.v[k].x = borne(q.v[k].x + (cq[0] - co[0]) * f); q.v[k].y = borne(q.v[k].y + (cq[1] - co[1]) * f);
                    q.v[k].z = borne(q.v[k].z + (cq[2] - co[2]) * f); q.v[k].w = borne(q.v[k].w + (cq[3] - co[3]) * f);
                }
                q.ancienne = 0;
                colorier(q, C_TAILLE);
                out.push_back(q);
                continue;
            }
        }
        /* Une paire dont la forme ne s'interpole pas (voir `raide`) : on garde celle de l'image du jeu
           la plus proche et on ne fait glisser que son centre. Le polygone ne passe plus par des
           figures que le jeu ne montre jamais — une pointe, un trait — et il change de forme d'un coup,
           une fois par tour, comme sur la console. */
        if (lien.raide[j]) {
            const GxPolygon &base = f16 < 32768 ? o : b[j];
            double co[4] = {0, 0, 0, 0}, cq[4] = {0, 0, 0, 0};
            for (int k = 0; k < q.count; k++) {
                co[0] += o.v[k].x; co[1] += o.v[k].y; co[2] += o.v[k].z; co[3] += o.v[k].w;
                cq[0] += q.v[k].x; cq[1] += q.v[k].y; cq[2] += q.v[k].z; cq[3] += q.v[k].w;
            }
            for (int d = 0; d < 4; d++) { co[d] /= q.count; cq[d] /= q.count; }
            /* la part du chemin qui reste à faire depuis l'image dont on garde la forme */
            double f = f16 < 32768 ? (double)f16 / 65536.0 : (double)f16 / 65536.0 - 1.0;
            q = base;
            for (int k = 0; k < q.count; k++) {
                q.v[k].x = borne(q.v[k].x + (cq[0] - co[0]) * f); q.v[k].y = borne(q.v[k].y + (cq[1] - co[1]) * f);
                q.v[k].z = borne(q.v[k].z + (cq[2] - co[2]) * f); q.v[k].w = borne(q.v[k].w + (cq[3] - co[3]) * f);
            }
            q.ancienne = 0;
            colorier(q, C_RAIDE);
            out.push_back(q);
            continue;
        }
        /* les coordonnées de texture : une texture qui défile reboucle, on ne la rembobine pas */
        int32_t ds = 0, dt = 0;
        bool uv = !lien.par_voisinage[j];
        if (uv) {
            int32_t ls = (8 << ((q.texparam >> 20) & 7)) * 16, lt = (8 << ((q.texparam >> 23) & 7)) * 16;
            int32_t ns = (int32_t)std::lround((double)(q.v[0].s - o.v[0].s) / ls), nt = (int32_t)std::lround((double)(q.v[0].t - o.v[0].t) / lt);
            if ((ns && !(q.texparam & 0x10000u)) || (nt && !(q.texparam & 0x20000u))) uv = false;
            ds = ns * ls; dt = nt * lt;
            /* Un clignement d'yeux, une bouche qui parle : le jeu passe d'une vignette à la suivante en décalant
               la texture d'un coup, par sa matrice. Interpoler ce saut-là faisait défiler la planche sous le
               carré — deux moitiés de vignettes l'une sur l'autre. Ne s'interpole que ce qui glisse vraiment :
               trois texels au plus par tour (une eau, des nuages) ; le reste change d'un coup, comme la texture. */
            for (int k = 0; k < q.count && uv; k++)
                if (std::abs(q.v[k].s - (o.v[k].s + ds)) > 48 || std::abs(q.v[k].t - (o.v[k].t + dt)) > 48) uv = false;
        }
        /* Le dessin au pixel avance au pixel. La flèche « suite » d'une boîte de dialogue saute d'un pixel,
           adossée à son ombre ; montrée à mi-chemin, entre deux pixels, l'ombre dépassait en un liseré noir.
           Le chemin parcouru depuis l'image du jeu la plus proche est donc arrondi à des pixels entiers de la
           console (256 × 192 : le rendu couvre l'écran entier) : un saut d'un pixel reste un saut, comme sur
           la console, ce qui glisse sur plusieurs pixels glisse de pixel en pixel — et ce qui ne bouge pas
           reste exactement où le jeu l'a mis, même entre deux pixels. */
        const bool au_pixel = q.proj < GX_PROJS_MAX && lien.a_plat[q.proj] && f16 > 0 && f16 != 65536;
        /* Au-delà de la plus récente (t > 1), on prolonge le mouvement : c'est la prédiction, qui évite
           d'attendre l'image suivante. Les couleurs restent dans leur plage, et un sommet qui passerait
           derrière la caméra reste où le jeu l'a mis. */
        auto borne31 = [](int32_t v) { return (uint8_t)(v < 0 ? 0 : v > 31 ? 31 : v); };
        for (int k = 0; k < q.count; k++) {
            const GxVertex vo = o.v[k], vq = q.v[k];
            q.v[k].x = entre(o.v[k].x, q.v[k].x, f16); q.v[k].y = entre(o.v[k].y, q.v[k].y, f16);
            q.v[k].z = entre(o.v[k].z, q.v[k].z, f16); q.v[k].w = entre(o.v[k].w, q.v[k].w, f16);
            if (f16 > 65536 && (q.v[k].w <= 0) != (vq.w <= 0)) { q.v[k].x = vq.x; q.v[k].y = vq.y; q.v[k].z = vq.z; q.v[k].w = vq.w; }
            if (au_pixel && vo.w > 0 && vq.w > 0) {
                const GxVertex &base = f16 < 32768 ? vo : vq;               /* l'image du jeu la plus proche */
                double w = q.v[k].w, bx = (double)base.x / base.w, by = (double)base.y / base.w;
                double dx = std::nearbyint(((double)q.v[k].x / w - bx) * 128.0) / 128.0;
                double dy = std::nearbyint(((double)q.v[k].y / w - by) * 96.0) / 96.0;
                q.v[k].x = borne((bx + dx) * w);
                q.v[k].y = borne((by + dy) * w);
            }
            q.v[k].r = borne31(entre(o.v[k].r, q.v[k].r, f16)); q.v[k].g = borne31(entre(o.v[k].g, q.v[k].g, f16));
            q.v[k].b = borne31(entre(o.v[k].b, q.v[k].b, f16));
            if (uv) {
                q.v[k].s = (int16_t)entre(o.v[k].s + ds, q.v[k].s, f16);
                q.v[k].t = (int16_t)entre(o.v[k].t + dt, q.v[k].t, f16);
            }
        }
        /* Un polygone que l'interpolation retourne disparaît : le moteur élimine les faces vues de dos.
           On compare donc son orientation à l'écran à celle qu'il avait dans les deux images du jeu. */
        {
            auto sens = [](const GxPolygon &p) {
                if (p.count < 3) return 0.0;
                double x[4], y[4];
                int n = p.count > 4 ? 4 : p.count;
                for (int k = 0; k < n; k++) {
                    double w = std::fabs((double)p.v[k].w) + 1.0;
                    x[k] = (double)p.v[k].x / w; y[k] = (double)p.v[k].y / w;
                }
                double aire = 0;
                for (int k = 0; k < n; k++) { int s2 = (k + 1) % n; aire += x[k] * y[s2] - x[s2] * y[k]; }
                return aire;
            };
            double so = sens(o), sb = sens(b[j]), sq = sens(q);
            /* Le polygone regardait l'écran dans les deux images du jeu, et le chemin l'a retourné : le
               moteur l'éliminerait comme une face vue de dos, et il disparaîtrait le temps de cette image.
               On lui rend celui de l'image du jeu la plus proche, qui est du bon côté. */
            if (so * sb > 0 && sq * so < 0) {
                g_retournes++;
                q = f16 < 32768 ? o : b[j];
                q.ancienne = f16 < 32768 ? 1 : 0;
                colorier(q, C_RETOURNE);
            } else if (lien.par_voisinage[j]) colorier(q, C_VOISIN);
            else if (j < lien.etage.size() && lien.etage[j] == 2) colorier(q, C_FORME);   /* orange : par sa seule forme */
        }
        q.ancienne = 0;
        out.push_back(q);
    }
    /* et ceux qui restaient à placer : eux aussi vont jusqu'au bout du chemin */
    while (c < lien.disparus.size()) {
        int id = lien.disparus[c++];
        const GxPolygon &d = a[id];
        if ((size_t)id < lien.objet_a.size() && lien.objet_a[id] >= 0) suivre(d, lien.objet_avant[lien.objet_a[id]].data(), true);
        else suivre(d, lien.avant[d.proj < GX_PROJS_MAX ? d.proj : 0], true);
    }
}

void planifier(const std::vector<GxPolygon> &a, const std::vector<GxPolygon> &b, const Lien &lien,
               std::vector<PolyPlan> &out) {
    out.clear();
    out.reserve(b.size() + lien.disparus.size() + 64);
    auto couleurs_et_texture = [](PolyPlan &p, const GxPolygon &q, bool bout0, bool bout1) {
        for (int k = 0; k < q.count; k++) {
            if (bout0) { p.c0[k][0] = q.v[k].r; p.c0[k][1] = q.v[k].g; p.c0[k][2] = q.v[k].b; p.s0[k] = q.v[k].s; p.t0[k] = q.v[k].t; }
            if (bout1) { p.c1[k][0] = q.v[k].r; p.c1[k][1] = q.v[k].g; p.c1[k][2] = q.v[k].b; p.s1[k] = q.v[k].s; p.t1[k] = q.v[k].t; }
        }
    };
    /* Un trajet tout tracé : le polygone q emmené par m, de sa place (depuis_a) ou jusqu'à elle — ce que fait
       `suivre` et la branche des changés de forme. Ses couleurs et sa texture ne bougent pas. */
    auto trajet = [&](const GxPolygon &q, const double *m, bool depuis_a, uint8_t ancienne, uint32_t drapeaux) {
        PolyPlan p{};
        p.etat = &q;
        p.ancienne = ancienne;
        p.drapeaux = PLAN_CHEMIN | drapeaux | (q.count == 4 ? PLAN_QUATRE : 0u);
        for (int k = 0; k < q.count; k++) {
            double c[4] = {(double)q.v[k].x, (double)q.v[k].y, (double)q.v[k].z, (double)q.v[k].w}, o[4];
            if (m) appliquer(c, m, o);
            else memcpy(o, c, sizeof o);
            int32_t *de = depuis_a ? p.p0[k] : p.p1[k], *vers = depuis_a ? p.p1[k] : p.p0[k];
            for (int d = 0; d < 4; d++) { de[d] = borne(c[d]); vers[d] = borne(o[d]); }
        }
        couleurs_et_texture(p, q, true, true);
        out.push_back(p);
    };
    auto suivre = [&](const GxPolygon &q, const double *m, bool depuis_a) {
        trajet(q, q.proj < GX_PROJS_MAX ? m : nullptr, depuis_a, depuis_a ? 1 : 0, 0);
    };
    /* le sens d'un polygone à l'écran, comme `interpoler` le mesure */
    auto sens = [](const GxPolygon &p) {
        if (p.count < 3) return 0.0;
        double x[4], y[4];
        int n = p.count > 4 ? 4 : p.count;
        for (int k = 0; k < n; k++) {
            double w = std::fabs((double)p.v[k].w) + 1.0;
            x[k] = (double)p.v[k].x / w; y[k] = (double)p.v[k].y / w;
        }
        double aire = 0;
        for (int k = 0; k < n; k++) { int s2 = (k + 1) % n; aire += x[k] * y[s2] - x[s2] * y[k]; }
        return aire;
    };
    /* deux polygones que le moteur dessine pareil : même texture, même mode, même élimination des faces,
       même transparence (les bits de POLYGON_ATTR que lit le dessin) */
    auto meme_etat = [](const GxPolygon &x, const GxPolygon &y) {
        return (x.texparam & 0x3FFFFFFFu) == (y.texparam & 0x3FFFFFFFu) && x.pltt_base == y.pltt_base
               && (x.attr & 0x001F00F0u) == (y.attr & 0x001F00F0u) && x.translucent == y.translucent;
    };
    size_t c = 0;
    for (size_t j = 0; j < b.size(); j++) {
        int i = lien.pareil[j];
        if (i < 0) {
            if (j < lien.objet_b.size() && lien.objet_b[j] >= 0) suivre(b[j], lien.objet_arriere[lien.objet_b[j]].data(), false);
            else suivre(b[j], lien.arriere[b[j].proj < GX_PROJS_MAX ? b[j].proj : 0], false);
            continue;
        }
        while (c < lien.disparus.size() && lien.disparus[c] < i) {
            int id = lien.disparus[c++];
            const GxPolygon &d = a[id];
            if ((size_t)id < lien.objet_a.size() && lien.objet_a[id] >= 0) suivre(d, lien.objet_avant[lien.objet_a[id]].data(), true);
            else suivre(d, lien.avant[d.proj < GX_PROJS_MAX ? d.proj : 0], true);
        }
        const GxPolygon &q = b[j], &o = a[i];
        if (j < lien.change.size() && lien.change[j] && lien.objet_b[j] >= 0) {
            /* la forme d'avant emportée vers B jusqu'à mi-chemin, puis la nouvelle ramenée vers A ; vers un
               aperçu, toujours celle d'avant */
            const int ob = lien.objet_b[j];
            const uint32_t apercu = lien.b_apercu ? PLAN_B_APERCU : 0u;
            trajet(o, lien.objet_avant[ob].data(), true, 1, PLAN_AVANT_SEUL | apercu);
            trajet(q, lien.objet_arriere[ob].data(), false, 0, PLAN_APRES_SEUL | apercu);
            continue;
        }
        PolyPlan p{};
        p.etat = &q;
        p.ancienne = 0;
        p.drapeaux = q.count == 4 ? PLAN_QUATRE : 0u;
        for (int k = 0; k < q.count; k++) {
            p.p0[k][0] = o.v[k].x; p.p0[k][1] = o.v[k].y; p.p0[k][2] = o.v[k].z; p.p0[k][3] = o.v[k].w;
            p.p1[k][0] = q.v[k].x; p.p1[k][1] = q.v[k].y; p.p1[k][2] = q.v[k].z; p.p1[k][3] = q.v[k].w;
        }
        couleurs_et_texture(p, o, true, false);
        couleurs_et_texture(p, q, false, true);
        if (lien.par_voisinage[j]) p.drapeaux |= PLAN_VOISIN;
        if (q.proj < GX_PROJS_MAX && lien.a_plat[q.proj]) p.drapeaux |= PLAN_A_PLAT;
        /* le déplacement du centre, et ce qui change vite de taille (les mesures de `interpoler`) */
        double co[4] = {0, 0, 0, 0}, cq[4] = {0, 0, 0, 0};
        for (int k = 0; k < q.count; k++) {
            co[0] += o.v[k].x; co[1] += o.v[k].y; co[2] += o.v[k].z; co[3] += o.v[k].w;
            cq[0] += q.v[k].x; cq[1] += q.v[k].y; cq[2] += q.v[k].z; cq[3] += q.v[k].w;
        }
        for (int d = 0; d < 4; d++) { co[d] /= q.count; cq[d] /= q.count; p.delta[d] = (float)(cq[d] - co[d]); }
        auto taille = [&](const GxPolygon &pp, const double *cc) {
            double r = 0;
            for (int k = 0; k < pp.count; k++) {
                double w = std::fabs((double)pp.v[k].w) + 1.0, dx = (double)pp.v[k].x / w - cc[0] / (std::fabs(cc[3]) + 1.0),
                       dy = (double)pp.v[k].y / w - cc[1] / (std::fabs(cc[3]) + 1.0);
                r = std::max(r, dx * dx + dy * dy);
            }
            return std::sqrt(r);
        };
        double ro = taille(o, co), rq = taille(q, cq);
        if (ro > 1e-6 && rq > 1e-6 && (rq > 1.15 * ro || ro > 1.15 * rq)) p.drapeaux |= PLAN_GROSSIT;
        if (lien.raide[j]) p.drapeaux |= PLAN_RAIDE;
        /* les coordonnées de texture : les mêmes règles que `interpoler` */
        bool uv = !lien.par_voisinage[j];
        if (uv) {
            int32_t ls = (8 << ((q.texparam >> 20) & 7)) * 16, lt = (8 << ((q.texparam >> 23) & 7)) * 16;
            int32_t ns = (int32_t)std::lround((double)(q.v[0].s - o.v[0].s) / ls), nt = (int32_t)std::lround((double)(q.v[0].t - o.v[0].t) / lt);
            if ((ns && !(q.texparam & 0x10000u)) || (nt && !(q.texparam & 0x20000u))) uv = false;
            int32_t ds = ns * ls, dt = nt * lt;
            p.ds = (float)ds; p.dt = (float)dt;
            for (int k = 0; k < q.count && uv; k++)
                if (std::abs(q.v[k].s - (o.v[k].s + ds)) > 48 || std::abs(q.v[k].t - (o.v[k].t + dt)) > 48) uv = false;
        }
        if (uv) p.drapeaux |= PLAN_UV;
        const double so = sens(o), sb = sens(b[j]);
        if (so * sb > 0) p.drapeaux |= PLAN_RETOURNABLE | (so > 0 ? PLAN_SENS_O : 0u);
        /* la forme de A ne se montre que raide, ou retournée, avant mi-chemin */
        const bool forme_a = (p.drapeaux & (PLAN_RAIDE | PLAN_RETOURNABLE)) != 0;
        if (!forme_a) { p.drapeaux |= PLAN_ETAT_B; out.push_back(p); }
        else if (meme_etat(o, q)) { p.drapeaux |= PLAN_ETAT_A | PLAN_ETAT_B; out.push_back(p); }
        else {
            PolyPlan pa = p;
            p.drapeaux |= PLAN_ETAT_B;
            out.push_back(p);
            pa.etat = &o;
            pa.drapeaux |= PLAN_ETAT_A;
            out.push_back(pa);
        }
    }
    while (c < lien.disparus.size()) {
        int id = lien.disparus[c++];
        const GxPolygon &d = a[id];
        if ((size_t)id < lien.objet_a.size() && lien.objet_a[id] >= 0) suivre(d, lien.objet_avant[lien.objet_a[id]].data(), true);
        else suivre(d, lien.avant[d.proj < GX_PROJS_MAX ? d.proj : 0], true);
    }
}

}  // namespace sfc
