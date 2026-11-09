// Rendu vidéo : fenêtre, OpenGL 3.3, affichage de la sortie 3D de la DS.
//
// Les polygones produits par le moteur géométrique (gx_geom.c) sont déjà en
// espace de clipping 20.12 : on les envoie tels quels au GPU, avec la couleur
// par sommet et la texture décodée depuis la VRAM selon TEXIMAGE_PARAM et
// PLTT_BASE. Les entrées de la fenêtre deviennent boutons et stylet.
//
// Sous licence MIT.
#include "gx_geom.h"
#include "hw.h"
#include "memory.h"
extern "C" uint64_t snd_mix_ns(void);
#include "video2d.h"
#include "manette.h"
#include "entre_images.h"
#include "verify.h"
#include "window.h"
#include "gl_api.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

void apercus_pendant(std::chrono::steady_clock::time_point echeance);   /* les tours d'aperçu, plus bas */
extern "C" int natif_apercu_possible(void);
extern "C" int natif_apercu_jouer(int32_t k_q16, double tour, uint32_t touches, double budget_s);

extern "C" {
uint32_t mmio_peek(uint32_t off);
void rt_log(const char *fmt, ...);
void attente_dormir_ns(int64_t ns);       /* attente.c : un sommeil qui dure ce qu'on lui demande */
void echantillonneur_fil_affichage(void); /* echantillonneur.c : SFC_ECHANTILLONS_FIL=affichage */
void rt_fil_nommer(const char *nom);       /* echantillonneur.c : le nom du fil, et sa part sous SFC_PROFILE */
void rt_fils_rapport(void);
#ifdef _WIN32
__declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);   /* winmm */
#endif
}

namespace {

using sfc::platform::Key;
using sfc::platform::Window;

Window *g_window;
GLuint g_program, g_vao, g_vbo;
/* Des tampons à part pour ce qui ne change pas — les rectangles des couches 2D et de la réduction, posés
   une fois pour toutes — et pour le fond de la 3D : le tampon principal ne reçoit plus qu'un envoi par
   écran et par image. Chaque envoi dans un tampon que le GPU lit encore peut faire attendre le pilote. */
GLuint g_vao_rects, g_vbo_rects, g_vao_fond, g_vbo_fond;
GLint g_uni_tex, g_uni_use_tex, g_uni_alpha_ref, g_uni_mode;
GLint g_uni_fog_on, g_uni_fog_alpha_only, g_uni_fog_color, g_uni_fog_offset, g_uni_fog_step, g_uni_fog_table;
/* le programme de l'interpolation sur le GPU ; 0 si le pilote n'en veut pas — on reste alors au processeur */
GLuint g_prog_plan;
/* Les fonctions d'OpenGL 3.0 dont seule l'interpolation sur le GPU a besoin. Toutes appartiennent au cœur de la
   3.3 que demande la fenêtre, chez NVIDIA, AMD et Intel ; qu'un pilote en manque une, et c'est seulement
   l'interpolation qui reste au processeur — le jeu, lui, n'en dépend pas. */
#ifndef GL_TRANSFORM_FEEDBACK_BUFFER
#define GL_TRANSFORM_FEEDBACK_BUFFER 0x8C8E
#endif
#ifndef GL_INTERLEAVED_ATTRIBS
#define GL_INTERLEAVED_ATTRIBS 0x8C8C
#endif
#ifndef GL_RASTERIZER_DISCARD
#define GL_RASTERIZER_DISCARD 0x8C89
#endif
#ifndef GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN
#define GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN 0x8C88
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif
#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif
struct GlPlan {
    void(APIENTRY *VertexAttribIPointer)(GLuint, GLint, GLenum, GLsizei, const void *);
    void(APIENTRY *TransformFeedbackVaryings)(GLuint, GLsizei, const GLchar *const *, GLenum);
    void(APIENTRY *BeginTransformFeedback)(GLenum);
    void(APIENTRY *EndTransformFeedback)();
    void(APIENTRY *BindBufferBase)(GLenum, GLuint, GLuint);
    void(APIENTRY *GetBufferSubData)(GLenum, GLintptr, GLsizeiptr, void *);
    void(APIENTRY *GenQueries)(GLsizei, GLuint *);
    void(APIENTRY *DeleteQueries)(GLsizei, const GLuint *);
    void(APIENTRY *BeginQuery)(GLenum, GLuint);
    void(APIENTRY *EndQuery)(GLenum);
    void(APIENTRY *GetQueryObjectuiv)(GLuint, GLenum, GLuint *);
} g_glp;
bool charger_gl_plan() {
    auto f = [](const char *nom) -> PROC {
        PROC p = wglGetProcAddress(nom);
        intptr_t v = (intptr_t)p;
        return (v >= -1 && v <= 3) ? nullptr : p;         /* certains pilotes rendent 1, 2, 3 ou -1 pour « absente » */
    };
    bool ok = true;
    auto prendre = [&](auto &champ, const char *nom) {
        PROC p = f(nom);
        champ = reinterpret_cast<std::remove_reference_t<decltype(champ)>>(reinterpret_cast<void *>(p));
        if (!p) { rt_log("interpolation : fonction %s absente", nom); ok = false; }
    };
    prendre(g_glp.VertexAttribIPointer, "glVertexAttribIPointer");
    prendre(g_glp.TransformFeedbackVaryings, "glTransformFeedbackVaryings");
    prendre(g_glp.BeginTransformFeedback, "glBeginTransformFeedback");
    prendre(g_glp.EndTransformFeedback, "glEndTransformFeedback");
    prendre(g_glp.BindBufferBase, "glBindBufferBase");
    prendre(g_glp.GetBufferSubData, "glGetBufferSubData");
    prendre(g_glp.GenQueries, "glGenQueries");
    prendre(g_glp.DeleteQueries, "glDeleteQueries");
    prendre(g_glp.BeginQuery, "glBeginQuery");
    prendre(g_glp.EndQuery, "glEndQuery");
    prendre(g_glp.GetQueryObjectuiv, "glGetQueryObjectuiv");
    return ok;
}
struct UnisPlan { GLint tex, use_tex, alpha_ref, mode, fog[6], f16, tq, tf; } g_up;
bool g_ready;
GLuint g_screen_tex[4];          // haut dessous, haut dessus, bas dessous, bas dessus
Engine2dOutput g_out2d[2];
const GxFrame *g_last_3d;

/* Ce qu'il y a à montrer à un balayage vertical, détaché de la machine simulée : le fil
   d'affichage (SFC_AFFICHAGE=libre) ne lit que cela, jamais la mémoire du jeu, qui continue de
   tourner pendant qu'il dessine. */
/* L'espace texture et l'espace palette tels que le moteur 3D les voit, recopiés. `version` dit quel
   contenu c'est : deux copies de même version sont identiques, le jeu n'a rien écrit entre elles. */
struct Textures {
    std::vector<uint8_t> tex;          /* 4 × 128 Ko */
    std::vector<uint8_t> pal;          /* 128 Ko */
    uint64_t version = 0;
    uint64_t ecritures = 0;            /* le compteur d'écritures graphiques de la machine à la copie */
    uint32_t banques[2] = {0, 0};      /* VRAMCNT : un changement de banque change ce qu'on voit sans rien écrire */
};

/* Tout un instantané sauf ses couches 2D : ce qu'on remet à zéro quand il se recycle (plus bas). */
struct InstantaneEnTete {
    int a_top = 0;
    bool a_3d = false;
    GxFrame frame{};                   /* l'en-tête ; ses polygones sont dans `polys` */
    std::vector<GxPolygon> polys;
    std::shared_ptr<const Textures> textures;   /* partagées avec les instantanés voisins tant que rien n'a changé */
    uint32_t alpha_ref = 0;            /* ALPHA_TEST_REF */
    uint32_t capture = 0;              /* DISPCAPCNT : le jeu demande-t-il la capture de cette image ? */
    uint32_t vram_ecrit[VRAM_PAGES_PHYS] = {};   /* les écritures du jeu en mémoire vidéo à cette image (memory.c) */
    uint64_t image = 0;
    std::chrono::steady_clock::time_point quand;
    bool precoce = false;              /* publié dès la fin du tour, avant sa présentation : ce n'est pas un balayage */
    bool apercu = false;               /* l'image d'un tour d'aperçu (natif.c) : le jeu à une part de tour */
    double tour = 0;                   /* le tour du jeu qu'elle montre : le numéro de l'image 3D, plus la part
                                          de tour d'un aperçu */
    uint64_t numero = 0;               /* unique, croissant : dit, sans les relire, que deux couches sont les mêmes */
};
struct Instantane : InstantaneEnTete {
    Engine2dOutput out2d[2];           /* les deux moteurs 2D, déjà composés par le fil du jeu */
};
std::atomic<uint64_t> g_numero_instantane{0};

/* Les instantanés se recyclent. Chacun pèse près d'un mégaoctet — les couches 2D des deux écrans —, et en
   créer un neuf à chaque image, deux cents fois par seconde avec les tours d'aperçu, faisait mettre à zéro
   puis toucher pour la première fois autant de mémoire neuve, sur le fil du jeu, pour des couches que
   chaque créateur réécrit de toute façon en entier. Celui que l'affichage lâche retourne à la réserve,
   son en-tête remis à ses valeurs de départ. */
std::shared_ptr<Instantane> nouvel_instantane() {
    /* La réserve ne meurt jamais : à la sortie du programme, le fil d'affichage lâche encore des instantanés
       quand les objets statiques sont déjà détruits. */
    static std::mutex *m = new std::mutex;
    static std::vector<Instantane *> *reserve = new std::vector<Instantane *>;
    Instantane *i = nullptr;
    {   std::lock_guard<std::mutex> l(*m);
        if (!reserve->empty()) { i = reserve->back(); reserve->pop_back(); }
    }
    if (!i) i = new Instantane();
    return std::shared_ptr<Instantane>(i, [](Instantane *p) {
        static_cast<InstantaneEnTete &>(*p) = InstantaneEnTete{};
        std::lock_guard<std::mutex> l(*m);
        reserve->push_back(p);
    });
}
const Instantane *g_source;            /* l'instantané qu'on dessine ; nul : on lit la machine, comme avant */
bool g_passe_exacte;                   /* on dessine l'instantané tel quel, pour la capture que le jeu attend */
const Instantane *g_src_recente, *g_src_ancienne;   /* image interpolée : d'où viennent les textures de chacun */
uint64_t g_image_montree;              /* le numéro de l'image du jeu qu'on est en train de montrer */

struct Vtx {
    float x, y, z, w;
    float r, g, b, a;
    float s, t;
};

const char *VS = R"(#version 330 core
layout(location = 0) in vec4 pos;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 uv;
out vec4 vColor;
out vec2 vUv;
void main() {
    gl_Position = pos;
    vColor = color;
    vUv = uv;
}
)";

const char *FS = R"(#version 330 core
in vec4 vColor;
in vec2 vUv;
out vec4 frag;
uniform sampler2D tex;
uniform int useTex;
uniform int mode;
uniform float alphaRef;
uniform int fogEnable;        // brouillard demandé par DISP3DCNT
uniform int fogAlphaOnly;     // il ne teinte alors que la transparence
uniform vec4 fogColor;
uniform float fogOffset;      // début du brouillard, en profondeur normalisée
uniform float fogStep;        // largeur d'un pas de la table, idem
uniform float fogTable[32];   // densité par pas, de 0 à 1
void main() {
    vec4 c = vColor;
    if (useTex == 1) {
        vec4 t = texture(tex, vUv);
        if (mode == 1) c = vec4(mix(c.rgb, t.rgb, t.a), c.a);       // décalcomanie
        else c = vec4(c.rgb * t.rgb, c.a * t.a);                     // modulation
    }
    if (c.a < alphaRef) discard;
    if (fogEnable == 1) {
        // La console range la densité dans une table de trente-deux pas, lus à la profondeur
        // du fragment et interpolés entre deux entrées voisines.
        float d = (gl_FragCoord.z - fogOffset) / max(fogStep, 1e-9);
        float density;
        if (d <= 0.0) density = fogTable[0];
        else if (d >= 31.0) density = fogTable[31];
        else {
            int i = int(floor(d));
            density = mix(fogTable[i], fogTable[i + 1], fract(d));
        }
        if (fogAlphaOnly == 1) c.a = mix(c.a, fogColor.a, density);
        else c = vec4(mix(c.rgb, fogColor.rgb, density), mix(c.a, fogColor.a, density));
    }
    frag = c;
}
)";

/* SFC_SCALE : le rendu se fait dans un tampon N fois plus grand que la fenêtre, puis on le
 * réduit. Chaque pixel affiché est alors la moyenne de N×N points calculés : c'est du
 * suréchantillonnage, l'anti-crénelage le plus simple et le plus sûr — il lisse les bords des
 * polygones comme les contours des textures, sans rien supposer de la scène. Son prix est
 * exactement N² fois plus de pixels à calculer. */
int render_scale() {
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("SFC_SCALE");
        v = e ? atoi(e) : 1;
        if (v < 1) v = 1;
        if (v > 4) v = 4;
    }
    return v;
}

struct Offscreen {
    GLuint fbo = 0, color = 0, depth = 0;
    int w = 0, h = 0;
};
Offscreen g_off;

/* Prépare le tampon hors écran à la taille voulue. Rend false si la carte n'en veut pas : on
   retombe alors sur le rendu direct, sans suréchantillonnage. */
bool offscreen_bind(int w, int h) {
    using namespace sfc::gfx;
    if (g_off.w != w || g_off.h != h) {
        if (!g_off.fbo) glGenFramebuffers(1, &g_off.fbo);
        if (!g_off.color) glGenTextures(1, &g_off.color);
        if (!g_off.depth) glGenRenderbuffers(1, &g_off.depth);
        glBindTexture(GL_TEXTURE_2D, g_off.color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindRenderbuffer(GL_RENDERBUFFER, g_off.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, g_off.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_off.color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_off.depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            rt_log("vidéo : tampon hors écran %dx%d refusé, suréchantillonnage abandonné", w, h);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            g_off.w = g_off.h = 0;
            return false;
        }
        g_off.w = w; g_off.h = h;
        rt_log("vidéo : rendu en %dx%d réduit à l'affichage", w, h);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_off.fbo);
    return true;
}

/* SFC_FILTER : 0 net comme la console (défaut), 1 bilinéaire, 2 bilinéaire et mipmaps. */
/* SFC_PROFILE=1 : où passe le temps d'une image. Le portage est lié au processeur — il simule
 * deux ARM instruction par instruction, décode les textures et compose les deux écrans en
 * logiciel — et rien de tout cela ne profite d'une machine à plusieurs cœurs. Reste à savoir
 * laquelle de ces parts coûte, ce qui ne se devine pas. */
struct Chrono {
    double total = 0;
    std::chrono::steady_clock::time_point t0;
    void start() { t0 = std::chrono::steady_clock::now(); }
    void stop() {
        total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
};
Chrono g_t_2d, g_t_3d, g_t_tex, g_t_image, g_t_swap, g_t_sim;
std::chrono::steady_clock::time_point g_hors_rendu;   /* fin de l'image précédente */
uint64_t g_prof_frames, g_prof_polys, g_prof_tex;
uint64_t g_prof_tex_miss, g_prof_tex_hash, g_prof_tex_decode, g_prof_tex_bytes;

int profiling() {
    static int v = -1;
    if (v < 0) v = getenv("SFC_PROFILE") != nullptr;
    return v;
}

/* Une moyenne sur trois cents images lisse précisément les à-coups qu'on cherche : on suit
   donc aussi l'image la plus longue et le nombre de celles qui dépassent le budget. */
std::chrono::steady_clock::time_point g_pace_fin;   /* instant où le cadencement rend la main */
double g_pire_image, g_pire_rendu;
uint64_t g_depassements;
struct { uint64_t frame; double ms; } g_pics[16];
int g_pics_n;

void profile_frame(double travail, double rendu) {
    if (travail > g_pire_image) { g_pire_image = travail; g_pire_rendu = rendu; }
    if (travail > 16.715) g_depassements++;
    /* Les à-coups viennent de quelques images très longues, pas de la moyenne. On note à quelle
       image de jeu elles tombent, pour aller voir ce que le jeu y faisait. */
    if (travail > 16.715 && g_pics_n < 16) {
        g_pics[g_pics_n].frame = hw_frames();
        g_pics[g_pics_n].ms = travail;
        g_pics_n++;
    }
}

void profile_report() {
    if (!profiling() || ++g_prof_frames % 300) return;
    static std::chrono::steady_clock::time_point last;
    auto now = std::chrono::steady_clock::now();
    double mur = last.time_since_epoch().count()
                 ? std::chrono::duration<double, std::milli>(now - last).count() : 0;
    last = now;
    if (mur > 0)
        rt_log("pics : %llu images sur 300 dépassent le budget, la pire demande %.1f ms"
               " (dont rendu %.1f)", (unsigned long long)g_depassements, g_pire_image, g_pire_rendu);
    if (mur > 0 && g_pics_n) {
        char l[220] = {0};
        for (int i = 0; i < g_pics_n && strlen(l) < 180; i++)
            snprintf(l + strlen(l), sizeof l - strlen(l), " %llu(%.0f)",
                     (unsigned long long)g_pics[i].frame, g_pics[i].ms);
        rt_log("   images longues :%s", l);
    }
    g_pics_n = 0;
    g_pire_image = g_pire_rendu = 0;
    g_depassements = 0;
    double image = g_t_image.total, d2 = g_t_2d.total, d3 = g_t_3d.total, tx = g_t_tex.total;
    if (mur > 0)
        rt_log("temps : %.1f ms par image dont rendu %.2f (2D %.2f, 3D %.2f dont textures %.2f),"
               " échange %.2f, simulation %.2f (dont mixage %.2f) ; %llu polygones, %llu textures",
               mur / 300.0, image / 300.0, d2 / 300.0, d3 / 300.0, tx / 300.0,
               g_t_swap.total / 300.0, g_t_sim.total / 300.0, snd_mix_ns() / 1e6 / 300.0,
               (unsigned long long)(g_prof_polys / 300),
               (unsigned long long)(g_prof_tex / 300));
    if (mur > 0)
        rt_log("   textures : %llu demandes par image, %llu distinctes, %llu empreintes, %llu décodages, %llu Ko lus",
               (unsigned long long)(g_prof_tex / 300), (unsigned long long)(g_prof_tex_miss / 300),
               (unsigned long long)(g_prof_tex_hash / 300), (unsigned long long)(g_prof_tex_decode / 300),
               (unsigned long long)(g_prof_tex_bytes / 300 / 1024));
    g_t_2d.total = g_t_3d.total = g_t_tex.total = g_t_image.total = g_t_swap.total = 0;
    g_t_sim.total = 0;
    g_prof_polys = g_prof_tex = 0;
    g_prof_tex_miss = g_prof_tex_hash = g_prof_tex_decode = g_prof_tex_bytes = 0;
}

/* SFC_NO_PARALLEL=1 : compose les deux écrans l'un après l'autre, pour comparer. */
int parallel_2d() {
    static int v = -1;
    if (v < 0) v = getenv("SFC_NO_PARALLEL") == nullptr;
    return v;
}

int tex_filter() {
    static int v = -1;
    if (v < 0) { const char *e = getenv("SFC_FILTER"); v = e ? atoi(e) : 0; }
    return v;
}

/* Un texel transparent porte tout de même une couleur, souvent noire ou arbitraire. Le
 * filtrage la mélange à celle des texels opaques voisins et cerne les objets d'un liseré
 * sombre. On donne donc à chaque texel transparent la moyenne de ses voisins opaques avant
 * d'envoyer la texture ; sa transparence, elle, ne change pas. */
void bleed_alpha(std::vector<uint32_t> &px, int w, int h) {
    std::vector<uint32_t> src = px;
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint32_t v = src[(size_t)y * w + x];
                if ((v >> 24) != 0) continue;
                int r = 0, g = 0, b = 0, n = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        uint32_t u = src[(size_t)ny * w + nx];
                        if ((u >> 24) == 0) continue;
                        r += u & 0xFF; g += (u >> 8) & 0xFF; b += (u >> 16) & 0xFF; n++;
                    }
                }
                if (!n) continue;
                px[(size_t)y * w + x] = (uint32_t)(r / n) | ((uint32_t)(g / n) << 8) |
                                        ((uint32_t)(b / n) << 16);   /* alpha reste nul */
            }
        }
        src = px;
    }
}

/* L'interpolation sur le GPU (voir « l'interpolation sur le GPU », plus bas). Le nuanceur de sommets passe
   tout au nuanceur de géométrie, qui reçoit un polygone entier — quatre sommets, en « lignes adjacentes » —
   et refait pour lui ce que fait `interpoler` (entre_images.cpp) à l'instant demandé. Les drapeaux sont ceux
   de entre_images.h (PLAN_…). */
const char *VS_PLAN = R"(#version 330 core
layout(location = 0) in ivec4 p0;
layout(location = 1) in ivec4 p1;
layout(location = 2) in vec4 c0;
layout(location = 3) in vec4 c1;
layout(location = 4) in vec4 st;
layout(location = 5) in vec4 extra;
flat out ivec4 gP0; flat out ivec4 gP1;
out vec4 gC0; out vec4 gC1; out vec4 gSt; out vec4 gExtra;
void main() { gP0 = p0; gP1 = p1; gC0 = c0; gC1 = c1; gSt = st; gExtra = extra; }
)";

const char *GS_PLAN = R"(#version 330 core
layout(lines_adjacency) in;
layout(triangle_strip, max_vertices = 6) out;
flat in ivec4 gP0[]; flat in ivec4 gP1[];
in vec4 gC0[]; in vec4 gC1[]; in vec4 gSt[]; in vec4 gExtra[];
out vec4 vColor;
out vec2 vUv;
uniform int f16;       // l'instant, compté comme interpoler le compte : (int)(t * 65536)
uniform float tq;      // f16 / 65536
uniform float tf;      // t lui-même, que prennent les trajets tout tracés
const int CHEMIN = 1, VOISIN = 2, A_PLAT = 4, GROSSIT = 8, RAIDE = 16, UV = 32, RETOURNABLE = 64, SENS_O = 128,
          ETAT_A = 256, ETAT_B = 512, QUATRE = 1024, AVANT_SEUL = 2048, APRES_SEUL = 4096, B_APERCU = 8192;
ivec4 P[4];            // les sommets montrés, entiers 20.12 comme ceux du jeu
ivec3 C[4];
ivec2 S[4];
// entre() d'interpoler au bit près : a + ((b - a) * f16 >> 16), le produit de 64 bits pris par morceaux
int entre(int a, int b) {
    int d = b - a;
    int fh = f16 >> 16, fl = f16 & 0xFFFF;
    return a + d * fh + (d >> 16) * fl + int((uint(d & 0xFFFF) * uint(fl)) >> 16u);
}
// lround, et borne : le plus proche entier, les moitiés loin de zéro
int arrondi(float v) { return int(clamp(sign(v) * floor(abs(v) + 0.5), -2147483648.0, 2147483520.0)); }
ivec4 arrondi4(vec4 v) { return ivec4(arrondi(v.x), arrondi(v.y), arrondi(v.z), arrondi(v.w)); }
// la forme de l'une des deux images, telle quelle
void forme(bool de_a) {
    for (int k = 0; k < 4; k++) {
        P[k] = de_a ? gP0[k] : gP1[k];
        C[k] = ivec3(de_a ? gC0[k].rgb : gC1[k].rgb);
        S[k] = ivec2(de_a ? gSt[k].xy : gSt[k].zw);
    }
}
float sens(int n) {
    vec2 e[4];
    for (int k = 0; k < 4; k++) e[k] = vec2(P[k].xy) / (abs(float(P[k].w)) + 1.0);
    float aire = 0.0;
    for (int k = 0; k < n; k++) { int k2 = (k + 1) % n; aire += e[k].x * e[k2].y - e[k2].x * e[k].y; }
    return aire;
}
void sommet(int k) {
    gl_Position = vec4(P[k]) / 4096.0;
    vColor = vec4(vec3(C[k]) / 31.0, gC0[0].a / 31.0);
    vUv = vec2(S[k]) * gExtra[2].xy;
    EmitVertex();
}
void main() {
    int fl = int(gExtra[1].x);
    int n = (fl & QUATRE) != 0 ? 4 : 3;
    bool avant = f16 < 32768, pred = f16 > 65536;
    bool montre_a = false;
    if ((fl & CHEMIN) != 0) {
        bool av = avant || (fl & B_APERCU) != 0;
        if (((fl & AVANT_SEUL) != 0 && !av) || ((fl & APRES_SEUL) != 0 && av)) return;
        for (int k = 0; k < 4; k++) {
            P[k] = arrondi4(vec4(gP0[k]) + (vec4(gP1[k]) - vec4(gP0[k])) * tf);
            C[k] = ivec3(gC0[k].rgb);
            S[k] = ivec2(gSt[k].xy);
        }
    } else {
        vec4 delta = gExtra[0];
        if (pred && (fl & (VOISIN | A_PLAT)) != 0) forme(false);
        else if (pred && (fl & GROSSIT) != 0) {
            forme(false);
            for (int k = 0; k < 4; k++) P[k] = arrondi4(vec4(P[k]) + delta * (tq - 1.0));
        } else if ((fl & RAIDE) != 0) {
            forme(avant);
            montre_a = avant;
            float f = avant ? tq : tq - 1.0;
            for (int k = 0; k < 4; k++) P[k] = arrondi4(vec4(P[k]) + delta * f);
        } else {
            bool au_pixel = (fl & A_PLAT) != 0 && f16 > 0 && f16 != 65536;
            ivec2 dst = ivec2(gExtra[1].yz);
            for (int k = 0; k < 4; k++) {
                ivec4 o = gP0[k], q = gP1[k];
                ivec4 v = ivec4(entre(o.x, q.x), entre(o.y, q.y), entre(o.z, q.z), entre(o.w, q.w));
                if (pred && (v.w <= 0) != (q.w <= 0)) v = q;
                if (au_pixel && o.w > 0 && q.w > 0) {
                    ivec4 base = avant ? o : q;
                    float w = float(v.w), bx = float(base.x) / float(base.w), by = float(base.y) / float(base.w);
                    float dx = roundEven((float(v.x) / w - bx) * 128.0) / 128.0;
                    float dy = roundEven((float(v.y) / w - by) * 96.0) / 96.0;
                    v.x = arrondi((bx + dx) * w);
                    v.y = arrondi((by + dy) * w);
                }
                P[k] = v;
                ivec3 a = ivec3(gC0[k].rgb), b = ivec3(gC1[k].rgb);
                C[k] = clamp(ivec3(entre(a.r, b.r), entre(a.g, b.g), entre(a.b, b.b)), 0, 31);
                ivec4 t = ivec4(gSt[k]);
                S[k] = (fl & UV) != 0 ? ivec2(entre(t.x + dst.x, t.z), entre(t.y + dst.y, t.w)) : t.zw;
            }
            if ((fl & RETOURNABLE) != 0) {
                float sq = sens(n);
                if (sq != 0.0 && (sq > 0.0) != ((fl & SENS_O) != 0)) { forme(avant); montre_a = avant; }
            }
        }
        if ((fl & (montre_a ? ETAT_A : ETAT_B)) == 0) return;
    }
    sommet(0); sommet(2); sommet(1); EndPrimitive();
    if (n == 4) { sommet(0); sommet(3); sommet(2); EndPrimitive(); }
}
)";

GLuint compile(GLenum type, const char *src) {
    GLuint sh = sfc::gfx::glCreateShader(type);
    sfc::gfx::glShaderSource(sh, 1, &src, nullptr);
    sfc::gfx::glCompileShader(sh);
    GLint ok = 0;
    sfc::gfx::glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        sfc::gfx::glGetShaderInfoLog(sh, sizeof log, nullptr, log);
        rt_log("shader : %s", log);
    }
    return sh;
}

// ------------------------------------------------------------------ VRAM : textures et palettes
// Banques A-D en mode texture (MST 3) : emplacement OFS de 128 Ko dans l'espace texture (512 Ko).
// E/F/G en mode palette (MST 3) : E -> 0, F/G -> (OFS&1)*16K + (OFS>>1)*64K.
const uint32_t BANK_OFFSET[9] = {0, 0x20000, 0x40000, 0x60000, 0x80000, 0x90000, 0x94000, 0x98000, 0xA0000};
/* La capture d'affichage écrit en VRAM depuis ce fil-ci, pendant qu'un tour d'aperçu, sur le fil du jeu,
   prend et rend la mémoire. Un verrou tient les deux à l'écart ; et pendant un aperçu, la capture écrit de
   côté au lieu d'écrire en mémoire vidéo — l'aperçu peut ainsi tout rendre, au dernier octet, et la capture
   est posée juste après. Laisser la capture écrire en VRAM pendant un aperçu obligeait à épargner sa banque
   à la restitution : ce que le jeu y écrivait pendant l'aperçu y restait, et l'écran du bas montrait alors
   un instant celui du haut. */
static std::mutex g_mx_capture;
extern "C" void capture_verrou_prendre(void) { g_mx_capture.lock(); }
extern "C" void capture_verrou_rendre(void) { g_mx_capture.unlock(); }
extern "C" int g_apercu_en_cours;
static std::vector<uint8_t> g_capture_de_cote;   /* ce que la capture a écrit pendant un aperçu */
static size_t g_capture_debut, g_capture_n;        /* où elle va dans g_vram, et combien */
static uint32_t g_capture_ecrit[VRAM_PAGES_PHYS];  /* les écritures du jeu à l'image capturée */
static bool g_capture_suivie;                      /* faite après coup (affichage libre) : g_capture_ecrit vaut */
static uint64_t g_capture_image;

/* Pose une capture dans la mémoire vidéo. En affichage libre, elle se fait après coup, quand le fil d'affichage
   voit l'image ; la console, elle, l'a faite pendant l'image même, avant ce que le jeu a écrit ensuite. Une page
   de 16 Ko que le jeu a écrite depuis garde donc ce qu'il y a mis : au passage de la carte de stratégie, il
   reprend la banque C de ses captures pour les tuiles de l'écran du bas et en efface la première, transparente ;
   posée par-dessus, la capture d'un écran noir y laissait un point noir sur quatre, des rayures sur tout l'écran
   jusqu'au combat suivant. `avant` : les compteurs d'écriture à l'image capturée (memory.c) ; nul, la capture
   se fait sur le fil du jeu, à son heure, et se pose entière. */
static void poser_capture(size_t debut, const uint8_t *src, size_t n, const uint32_t *avant, uint64_t image) {
    for (size_t o = 0; o < n;) {
        const size_t page = (debut + o) >> 14;
        const size_t fin = std::min(n, ((page + 1) << 14) - debut);
        if (!avant || g_vram_ecrit[page] == avant[page]) memcpy(g_vram + debut + o, src + o, fin - o);
        else {
            static int dites;
            if (dites++ < 3)
                rt_log("capture d'affichage de l'image %llu : la page %zu (banque %c) garde ce que le jeu y a écrit depuis",
                       (unsigned long long)image, page, 'A' + (int)((page << 14) / 0x20000));
        }
        o = fin;
    }
}

/* Posée dès que l'aperçu a rendu la mémoire (natif.c l'appelle, verrou tenu). */
extern "C" void capture_differee_poser(void) {
    if (!g_capture_n) return;
    poser_capture(g_capture_debut, g_capture_de_cote.data(), g_capture_n, g_capture_suivie ? g_capture_ecrit : nullptr,
                  g_capture_image);
    g_capture_n = 0;
}

const uint8_t *tex_slot_base(uint32_t slot) {
    static const uint32_t regs[4] = {0x240, 0x241, 0x242, 0x243};
    for (int b = 0; b < 4; b++) {
        uint8_t cnt = (uint8_t)(mmio_peek(regs[b] & ~3u) >> ((regs[b] & 3) * 8));
        if ((cnt & 0x87) == 0x83 && ((cnt >> 3) & 3) == slot) return g_vram + BANK_OFFSET[b];
    }
    return nullptr;
}

uint64_t g_tex_sans, g_tex_avec;     /* combien de lectures de texture tombent sur un instantané sans cache */

uint8_t tex_read8(uint32_t addr) {
    if (g_source) {
        if (g_source->textures) { g_tex_avec++; return g_source->textures->tex[addr & 0x7FFFFu]; }
        g_tex_sans++;
        return 0;
    }
    const uint8_t *base = tex_slot_base((addr >> 17) & 3);
    return base ? base[addr & 0x1FFFF] : 0;
}

uint16_t pal_read16(uint32_t addr) {
    // E : MST 3 -> 64 Ko en 0 ; F, G : MST 3 -> 16 Ko à (OFS&1)*0x4000 + (OFS>>1)*0x10000
    uint32_t off = addr & 0x1FFFF;
    if (g_source) { uint16_t v = 0; if (g_source->textures) memcpy(&v, g_source->textures->pal.data() + (off & 0x1FFFEu), 2); return v; }
    uint8_t e = (uint8_t)(mmio_peek(0x244) & 0xFF);
    if ((e & 0x87) == 0x83 && off < 0x10000) return *(const uint16_t *)(g_vram + BANK_OFFSET[4] + off);
    for (int b = 5; b <= 6; b++) {
        uint8_t cnt = (uint8_t)(mmio_peek(0x244) >> ((b - 4) * 8));
        if ((cnt & 0x87) != 0x83) continue;
        uint32_t ofs = (cnt >> 3) & 3;
        uint32_t base = (ofs & 1) * 0x4000 + (ofs >> 1) * 0x10000;
        if (off >= base && off < base + 0x4000) return *(const uint16_t *)(g_vram + BANK_OFFSET[b] + (off - base));
    }
    return 0;
}

inline uint32_t rgb555(uint16_t c, uint8_t a) {
    uint32_t r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
    return r | (g << 8) | (b << 16) | ((uint32_t)a << 24);
}

struct TexKey {
    uint32_t param, pltt;
    uint32_t flux;                     /* 0 : l'image qu'on dessine ; 1 : la précédente, pour ce qui s'efface */
    bool operator==(const TexKey &o) const { return param == o.param && pltt == o.pltt && flux == o.flux; }
};
struct TexKeyHash { size_t operator()(const TexKey &k) const { return (k.param * 31u + k.pltt) * 2u + k.flux; } };
struct TexEntry { GLuint id; uint32_t hash; int w, h; uint64_t stamp; };
std::unordered_map<TexKey, TexEntry, TexKeyHash> g_textures;
/* Une même texture sert à plusieurs polygones de la même image : ce cache évite de refaire l'empreinte
   (et, pour le format 4x4 compressé, le décodage complet) autant de fois qu'il y a de polygones. Il ne
   vaut que pour l'image en cours, mais on ne le vide plus : chaque entrée porte le numéro de l'image
   qui l'a remplie. À mille images par seconde, le vider et le remplir coûtait une allocation par
   texture et par image — le sixième du temps du fil d'affichage. */
struct TexDeLImage { TexEntry e; uint64_t image = 0; };
std::unordered_map<TexKey, TexDeLImage, TexKeyHash> g_frame_textures;
uint64_t g_image_textures = 1;         /* le numéro de l'image en cours de dessin */
void retenir_pour_l_image(const TexKey &k, const TexEntry &e) {
    TexDeLImage &d = g_frame_textures[k];
    d.e = e;
    d.image = g_image_textures;
}

uint32_t fnv(const uint8_t *p, size_t n, uint32_t h = 0x811C9DC5u) {
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x01000193u; }
    return h;
}

// Empreinte d'une texture : ses texels et sa palette, sans la décoder. Sert à savoir si la
// texture déjà envoyée au GPU est encore à jour — le décodage complet d'une 256x256 pour chaque
// polygone de chaque image coûtait plus cher que tout le reste du rendu.
uint32_t texture_hash(uint32_t param, uint32_t pltt, int &w, int &h) {
    uint32_t fmt = (param >> 26) & 7;
    if (fmt == 0) return 0;
    w = 8 << ((param >> 20) & 7);
    h = 8 << ((param >> 23) & 7);
    uint32_t addr = (param & 0xFFFF) << 3;
    uint32_t pal = pltt << (fmt == 2 ? 3 : 4);
    size_t texel_bytes = 0;
    switch (fmt) {
    case 1: case 4: case 6: texel_bytes = (size_t)w * h; break;
    case 2: case 5: texel_bytes = (size_t)w * h / 4; break;
    case 3: texel_bytes = (size_t)w * h / 2; break;
    case 7: texel_bytes = (size_t)w * h * 2; break;
    }
    uint32_t hash = 0x811C9DC5u;
    for (size_t i = 0; i < texel_bytes; i++) {
        uint8_t v = tex_read8(addr + (uint32_t)i);
        hash = (hash ^ v) * 0x01000193u;
    }
    if (fmt == 5) {
        /* 4x4 compressé : chaque bloc porte son propre index de palette, rangé dans un second
           emplacement de la mémoire de textures. Sans empreinte, ce format — le plus employé
           pour les décors — était redécodé et renvoyé au processeur graphique à chaque image :
           sur une mission chargée, cinquante-quatre textures sur soixante-quatorze y passaient,
           et l'image tombait de dix-sept à vingt et une millisecondes. */
        uint32_t slot = (addr >> 17) & 3;
        uint32_t idx_addr = 0x20000 + (addr & 0x1FFFFu) / 2 + (slot == 2 ? 0x10000u : 0);
        size_t blocks = (size_t)(w / 4) * (h / 4);
        uint32_t pal_lo = 0xFFFFFFFFu, pal_hi = 0;
        for (size_t b = 0; b < blocks; b++) {
            uint16_t info = (uint16_t)(tex_read8(idx_addr + (uint32_t)b * 2) |
                                       (tex_read8(idx_addr + (uint32_t)b * 2 + 1) << 8));
            hash = fnv((const uint8_t *)&info, 2, hash);
            uint32_t base = pal + (info & 0x3FFF) * 4;
            if (base < pal_lo) pal_lo = base;
            if (base + 8 > pal_hi) pal_hi = base + 8;
        }
        /* les couleurs que ces blocs désignent, d'un seul tenant */
        for (uint32_t a2 = pal_lo; a2 < pal_hi && pal_lo != 0xFFFFFFFFu; a2 += 2) {
            uint16_t c = pal_read16(a2);
            hash = fnv((const uint8_t *)&c, 2, hash);
        }
        return hash ? hash : 1u;
    }
    static const int PAL_ENTRIES[8] = {0, 32, 4, 16, 256, 0, 8, 0};
    int n = PAL_ENTRIES[fmt];
    for (int i = 0; i < n; i++) {
        uint16_t c = pal_read16(pal + (uint32_t)i * 2);
        hash = fnv((const uint8_t *)&c, 2, hash);
    }
    return hash ? hash : 1u;
}

// Décode la texture décrite par TEXIMAGE_PARAM / PLTT_BASE en RGBA8.
bool decode_texture(uint32_t param, uint32_t pltt, std::vector<uint32_t> &out, int &w, int &h, uint32_t &hash) {
    uint32_t fmt = (param >> 26) & 7;
    if (fmt == 0) return false;
    w = 8 << ((param >> 20) & 7);
    h = 8 << ((param >> 23) & 7);
    uint32_t addr = (param & 0xFFFF) << 3;
    bool color0 = (param >> 29) & 1;
    uint32_t pal = pltt << (fmt == 2 ? 3 : 4);
    out.assign((size_t)w * h, 0);
    size_t texel_bytes = 0;
    switch (fmt) {
    case 1: case 4: case 6: texel_bytes = (size_t)w * h; break;
    case 2: texel_bytes = (size_t)w * h / 4; break;
    case 3: texel_bytes = (size_t)w * h / 2; break;
    case 5: texel_bytes = (size_t)w * h / 4; break;
    case 7: texel_bytes = (size_t)w * h * 2; break;
    }
    std::vector<uint8_t> texels(texel_bytes);
    for (size_t i = 0; i < texel_bytes; i++) texels[i] = tex_read8(addr + (uint32_t)i);
    hash = fnv(texels.data(), texel_bytes);
    auto palette = [&](uint32_t index) { return pal_read16(pal + index * 2); };
    switch (fmt) {
    case 1:   // A3I5
        for (size_t i = 0; i < out.size(); i++) {
            uint8_t t = texels[i];
            out[i] = rgb555(palette(t & 31), (uint8_t)((t >> 5) * 255 / 7));
        }
        for (int i = 0; i < 32; i++) { uint16_t p = palette(i); hash = fnv((const uint8_t *)&p, 2, hash); }
        break;
    case 2:   // 4 couleurs
        for (size_t i = 0; i < out.size(); i++) {
            uint32_t idx = (texels[i / 4] >> ((i % 4) * 2)) & 3;
            out[i] = rgb555(palette(idx), (color0 && idx == 0) ? 0 : 255);
        }
        for (int i = 0; i < 4; i++) { uint16_t p = palette(i); hash = fnv((const uint8_t *)&p, 2, hash); }
        break;
    case 3:   // 16 couleurs
        for (size_t i = 0; i < out.size(); i++) {
            uint32_t idx = (texels[i / 2] >> ((i % 2) * 4)) & 15;
            out[i] = rgb555(palette(idx), (color0 && idx == 0) ? 0 : 255);
        }
        for (int i = 0; i < 16; i++) { uint16_t p = palette(i); hash = fnv((const uint8_t *)&p, 2, hash); }
        break;
    case 4:   // 256 couleurs
        for (size_t i = 0; i < out.size(); i++) {
            uint32_t idx = texels[i];
            out[i] = rgb555(palette(idx), (color0 && idx == 0) ? 0 : 255);
        }
        for (int i = 0; i < 256; i++) { uint16_t p = palette(i); hash = fnv((const uint8_t *)&p, 2, hash); }
        break;
    case 5: {  // 4x4 compressé : index de palette dans l'emplacement 1 (ou 1 + 64 Ko pour l'emplacement 2)
        uint32_t slot = (addr >> 17) & 3;
        uint32_t idx_addr = 0x20000 + (addr & 0x1FFFF) / 2 + (slot == 2 ? 0x10000 : 0);
        int bw = w / 4, bh = h / 4;
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++) {
                size_t blk = (size_t)by * bw + bx;
                uint32_t bits = texels[blk * 4] | (texels[blk * 4 + 1] << 8) | (texels[blk * 4 + 2] << 16) | (texels[blk * 4 + 3] << 24);
                uint16_t info = (uint16_t)(tex_read8(idx_addr + (uint32_t)blk * 2) | (tex_read8(idx_addr + (uint32_t)blk * 2 + 1) << 8));
                hash = fnv((const uint8_t *)&info, 2, hash);
                uint32_t pbase = pal + (uint32_t)(info & 0x3FFF) * 4;
                int mode = info >> 14;
                uint16_t c0 = pal_read16(pbase), c1 = pal_read16(pbase + 2), c2 = pal_read16(pbase + 4), c3 = pal_read16(pbase + 6);
                uint32_t colors[4];
                auto mix = [](uint16_t a, uint16_t b, int wa, int wb) {
                    uint32_t r = ((a & 31) * wa + (b & 31) * wb) / 8, g = (((a >> 5) & 31) * wa + ((b >> 5) & 31) * wb) / 8,
                             bl = (((a >> 10) & 31) * wa + ((b >> 10) & 31) * wb) / 8;
                    return (uint16_t)(r | (g << 5) | (bl << 10));
                };
                colors[0] = rgb555(c0, 255);
                colors[1] = rgb555(c1, 255);
                if (mode == 0) { colors[2] = rgb555(c2, 255); colors[3] = 0; }
                else if (mode == 1) { colors[2] = rgb555(mix(c0, c1, 4, 4), 255); colors[3] = 0; }
                else if (mode == 2) { colors[2] = rgb555(c2, 255); colors[3] = rgb555(c3, 255); }
                else { colors[2] = rgb555(mix(c0, c1, 5, 3), 255); colors[3] = rgb555(mix(c0, c1, 3, 5), 255); }
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 4; x++)
                        out[(size_t)(by * 4 + y) * w + bx * 4 + x] = colors[(bits >> ((y * 4 + x) * 2)) & 3];
            }
        break;
    }
    case 6:   // A5I3
        for (size_t i = 0; i < out.size(); i++) {
            uint8_t t = texels[i];
            out[i] = rgb555(palette(t & 7), (uint8_t)((t >> 3) * 255 / 31));
        }
        for (int i = 0; i < 8; i++) { uint16_t p = palette(i); hash = fnv((const uint8_t *)&p, 2, hash); }
        break;
    case 7:   // direct
        for (size_t i = 0; i < out.size(); i++) {
            uint16_t c = (uint16_t)(texels[i * 2] | (texels[i * 2 + 1] << 8));
            out[i] = rgb555(c, (c & 0x8000) ? 255 : 0);
        }
        break;
    }
    return true;
}

void write_png(const char *path, int w, int h, const uint8_t *rgba_bottom_up);

GLuint texture_for(uint32_t param, uint32_t pltt, int &w, int &h) {
    struct Mesure {
        Mesure() { if (profiling()) { g_t_tex.start(); g_prof_tex++; } }
        ~Mesure() { if (profiling()) g_t_tex.stop(); }
    } mesure;
    TexKey key{param & 0x3FFFFFFF, pltt, g_source && g_source == g_src_ancienne ? 1u : 0u};
    auto fit = g_frame_textures.find(key);
    if (fit != g_frame_textures.end() && fit->second.image == g_image_textures) {
        w = fit->second.e.w; h = fit->second.e.h; return fit->second.e.id;
    }
    if (profiling()) g_prof_tex_miss++;
    auto it0 = g_textures.find(key);
    /* Rien n'a été écrit en mémoire graphique depuis qu'on a pris son empreinte : la texture est
       forcément la même, inutile de la relire. Le jeu écrit rarement en mémoire vidéo, et ces
       lectures coûtaient quatre cent mille octets par image — de quoi vider le cache du
       processeur et ralentir la simulation elle-même.
       En affichage libre on dessine un instantané, en retard sur la machine : c'est sa version à lui
       qui compte. Se fier au compteur de la machine, c'était garder la vignette d'avant quand le
       portrait d'un dialogue ou une explosion changeait de texture — ou prendre celle d'après. */
    uint64_t gfx = g_source ? (g_source->textures ? g_source->textures->version : 0) : mem_gfx_writes();
    if (it0 != g_textures.end() && it0->second.stamp == gfx) {
        w = it0->second.w; h = it0->second.h;
        retenir_pour_l_image(key, it0->second);
        return it0->second.id;
    }
    uint32_t hash = texture_hash(param, pltt, w, h);
    if (profiling() && hash) { g_prof_tex_hash++; g_prof_tex_bytes += (uint64_t)w * h; }
    auto it = it0;
    if (hash && it != g_textures.end() && it->second.hash == hash) {
        it->second.stamp = gfx;
        w = it->second.w; h = it->second.h;
        retenir_pour_l_image(key, it->second);
        return it->second.id;      /* inchangée : rien à décoder ni à renvoyer au GPU */
    }
    std::vector<uint32_t> pixels;
    uint32_t decoded_hash = 0;
    if (profiling()) g_prof_tex_decode++;
    if (!decode_texture(param, pltt, pixels, w, h, decoded_hash)) return 0;
    if (!hash) hash = decoded_hash;      /* format sans empreinte rapide */
    GLuint id = it != g_textures.end() ? it->second.id : 0;
    static int dump_tex = -1;
    if (dump_tex < 0) dump_tex = getenv("SFC_DUMP_TEX") != nullptr;
    if (dump_tex) {
        /* SFC_DUMP_TEX=1 : chaque texture décodée (ou redécodée) en PNG, pour vérifier VRAM et palettes */
        std::vector<uint8_t> flipped((size_t)w * h * 4);
        for (int y = 0; y < h; y++)
            memcpy(&flipped[(size_t)(h - 1 - y) * w * 4], &pixels[(size_t)y * w], (size_t)w * 4);
        char name[96];
        snprintf(name, sizeof name, "build/tex_%08X_%04X_%08X.png", param, pltt, hash);
        write_png(name, w, h, flipped.data());
    }
    if (!id) glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    {   /* la répétition fait partie de la clé (bits 16 à 19 de TEXIMAGE_PARAM) : on la pose ici, une fois pour
           toutes, et le dessin n'a plus à la reposer à chaque lot */
        bool rep_s = (param >> 16) & 1, rep_t = (param >> 17) & 1, flip_s = (param >> 18) & 1, flip_t = (param >> 19) & 1;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, rep_s ? (flip_s ? GL_MIRRORED_REPEAT : GL_REPEAT) : GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, rep_t ? (flip_t ? GL_MIRRORED_REPEAT : GL_REPEAT) : GL_CLAMP_TO_EDGE);
    }
    if (tex_filter()) bleed_alpha(pixels, w, h);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (tex_filter() >= 2) {
        sfc::gfx::glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else if (tex_filter()) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    g_textures[key] = TexEntry{id, hash, w, h, gfx};
    retenir_pour_l_image(key, TexEntry{id, hash, w, h, gfx});
    return id;
}

// ------------------------------------------------------------------ entrées
struct Entrees { uint32_t held; int tdown, tx, ty, mx, my, bouton, ww, wh; Manette manette; };

/* Touche P : le journal des images. Un défaut qui dure un vingtième de seconde ne se rejoue pas — il
   dépend de l'instant où un tour d'aperçu tombe. On garde donc la trace des cent vingt dernières images
   montrées, et l'appui sur P les écrit : de quel tour elles venaient, d'un aperçu ou d'un balayage, dans
   quel ordre les écrans sortaient, et l'empreinte de chacun. Le joueur appuie juste après avoir vu. */
struct TraceImage { uint64_t image; double tour; int apercu, a_top; uint64_t h0, h1; size_t n3d[2]; };
static TraceImage g_trace[120];
static size_t g_trace_n;
std::atomic<bool> g_journal_demande{false};
/* d'où vient l'image qu'on s'apprête à montrer (posé juste avant le dessin) */
static uint64_t g_montre_image; static double g_montre_tour; static int g_montre_apercu;
static int g_src3d_apercu[2];        /* pour chaque écran : sa 3D vient-elle d'un tour d'aperçu ? */
/* d'où chaque écran tire sa 3D : les deux images du jeu entre lesquelles il est, et l'état de sa file */
struct SourceEcran { uint64_t de, vers; double tour_de, tour_vers; int atop_de, atop_vers, apercu_de, apercu_vers; size_t file; float a; };
static SourceEcran g_src3d[2];
static int g_jumeles_a_prendre;      /* un cas vient d'être vu : on photographie l'image une fois dessinée */

/* Touche F tenue : l'avance rapide. Le jeu pense huit fois plus vite — pour passer une scène qui ne se
   saute pas, la fin du jeu par exemple. Rien d'autre ne change : c'est le même jeu, plus tôt. */
std::atomic<bool> g_avance_rapide{false};

/* Les touches du clavier, une par commande, par son code de touche Windows. Le lanceur les change (onglet
   Contrôles) et les passe par SFC_TOUCHES : « a=0x5A,b=0x45,… » ; une commande absente garde la sienne, un
   code nul n'en donne aucune. Les douze premières sont les touches de la console dans l'ordre de KEYINPUT
   puis d'EXTKEYIN : le rang de chacune est son bit. */
struct Commande { const char *nom; int code; };
static Commande g_commandes[] = {
    {"a", 'Z'}, {"b", 'E'}, {"select", VK_TAB}, {"start", VK_SPACE},
    {"droite", VK_RIGHT}, {"gauche", VK_LEFT}, {"haut", VK_UP}, {"bas", VK_DOWN},
    {"r", 'W'}, {"l", 'Q'}, {"x", 'A'}, {"y", 'S'},
    {"rapide", 'F'}, {"journal", 'P'},
};
enum { NB_BOUTONS = 12, CMD_RAPIDE = 12, CMD_JOURNAL = 13 };

static bool lire_touches() {
    const char *e = getenv("SFC_TOUCHES");
    if (!e || !*e) return false;
    std::string liste(e);
    size_t debut = 0;
    while (debut < liste.size()) {
        size_t fin = liste.find(',', debut);
        if (fin == std::string::npos) fin = liste.size();
        std::string paire = liste.substr(debut, fin - debut);
        size_t egal = paire.find('=');
        bool connue = false;
        if (egal != std::string::npos) {
            std::string nom = paire.substr(0, egal);
            long code = strtol(paire.c_str() + egal + 1, nullptr, 0);
            for (auto &c : g_commandes)
                if (nom == c.nom && code >= 0 && code < 256) { c.code = (int)code; connue = true; }
        }
        if (!connue && !paire.empty()) rt_log("touches : « %s » ignorée", paire.c_str());
        debut = fin + 1;
    }
    rt_log("touches : %s", e);
    return true;
}

Entrees lire_entrees() {
    static const bool touches = lire_touches();
    (void)touches;
    const auto &in = g_window->input();
    g_avance_rapide = in.isNativeDown(g_commandes[CMD_RAPIDE].code);
    if (in.wasNativePressed(g_commandes[CMD_JOURNAL].code)) g_journal_demande = true;
    uint32_t held = 0;
    for (int i = 0; i < NB_BOUTONS; i++)
        if (in.isNativeDown(g_commandes[i].code)) held |= 1u << i;
    /* la manette : le fil du jeu en fait des touches et un stylet (manette.c) */
    Manette manette;
    manette_lire(&manette);
    // stylet : clic gauche dans la moitié basse de la fenêtre
    int ww = g_window->width(), wh = g_window->height();
    float sx = in.mouseX * 256.0f / (float)ww;
    float sy = (in.mouseY - wh / 2.0f) * 192.0f / (wh / 2.0f);
    bool down = in.mouseLeft && sy >= 0 && sy < 192;
    return Entrees{held, down ? 1 : 0, (int)sx, (int)sy, (int)in.mouseX, (int)in.mouseY, in.mouseLeft ? 1 : 0, ww, wh, manette};
}

/* Toujours sur le fil du jeu : le rejeu, l'enregistrement et les registres de la machine. */
void appliquer_entrees(const Entrees &e) {
    uint32_t held = e.held;
    int tdown = e.tdown, tx = e.tx, ty = e.ty;
    /* la manette — le script d'essai, s'il y en a un, la remplace, lu ici au rythme du jeu pour qu'un essai se
       rejoue à l'identique —, notée ou rejouée avec le reste, puis ses touches et ses commandes du cockpit */
    Manette manette = e.manette;
    manette_script(&manette);
    verify_input(hw_frames(), &held, &tdown, &tx, &ty, &manette);   // rejeu : remplace ; enregistrement : note
    manette_appliquer(&manette, &held, tdown);
    // SFC_TRACE_INPUT=1 : chaque changement d'état des touches ou du stylet
    static int trace_input = -1;
    if (trace_input < 0) trace_input = getenv("SFC_TRACE_INPUT") != nullptr;
    if (trace_input) {
        static uint32_t last_keys = 0; static int last_down = 0;
        if (held != last_keys || tdown != last_down) {
            rt_log("entrée : image %llu touches=%03X stylet=%d (%d,%d) ; souris=(%d,%d) bouton=%d fenêtre=%dx%d",
                   (unsigned long long)hw_frames(), held, tdown, tx, ty,
                   e.mx, e.my, e.bouton, e.ww, e.wh);
            last_keys = held; last_down = tdown;
        }
    }
    hw_set_keys(held);
    hw_set_touch(tdown, tx, ty);
    hw_set_pen(tdown);
}

}  // namespace

namespace { bool affichage_libre(); void lancer_fil_affichage(); bool epreuve_interpolation_gpu(); }

static bool video_init_ici() {
#ifdef _WIN32
    /* Pour tout ce qui dort encore par les moyens ordinaires (le son, les fils du jeu) : le tic du
       minuteur de Windows à une milliseconde au lieu de quinze. Les deux cadencements, eux, ne s'y fient
       pas (attente.c). */
    timeBeginPeriod(1);
#endif
    g_window = new Window();
    if (!g_window->create("Star Fox Command — portage natif", 512, 768)) { rt_log("vidéo : fenêtre impossible"); return false; }
    const char *missing = nullptr;
    if (!sfc::gfx::loadGlFunctions(&missing)) { rt_log("vidéo : fonction OpenGL manquante : %s", missing ? missing : "?"); return false; }
    g_window->setVSync(false);
    GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, FS);
    g_program = sfc::gfx::glCreateProgram();
    sfc::gfx::glAttachShader(g_program, vs);
    sfc::gfx::glAttachShader(g_program, fs);
    sfc::gfx::glLinkProgram(g_program);
    g_uni_tex = sfc::gfx::glGetUniformLocation(g_program, "tex");
    g_uni_use_tex = sfc::gfx::glGetUniformLocation(g_program, "useTex");
    g_uni_alpha_ref = sfc::gfx::glGetUniformLocation(g_program, "alphaRef");
    g_uni_mode = sfc::gfx::glGetUniformLocation(g_program, "mode");
    g_uni_fog_on = sfc::gfx::glGetUniformLocation(g_program, "fogEnable");
    g_uni_fog_alpha_only = sfc::gfx::glGetUniformLocation(g_program, "fogAlphaOnly");
    g_uni_fog_color = sfc::gfx::glGetUniformLocation(g_program, "fogColor");
    g_uni_fog_offset = sfc::gfx::glGetUniformLocation(g_program, "fogOffset");
    g_uni_fog_step = sfc::gfx::glGetUniformLocation(g_program, "fogStep");
    g_uni_fog_table = sfc::gfx::glGetUniformLocation(g_program, "fogTable");
    {   /* le pilote graphique, pour le journal : c'est la première chose à savoir d'un défaut d'affichage */
        auto chaine = [](GLenum n) { const GLubyte *s = glGetString(n); return s ? (const char *)s : "?"; };
        rt_log("OpenGL : %s — %s — %s — GLSL %s", chaine(GL_VENDOR), chaine(GL_RENDERER), chaine(GL_VERSION),
               chaine(GL_SHADING_LANGUAGE_VERSION));
    }
    {   /* l'interpolation sur le GPU : si le nuanceur de géométrie ne se lie pas, on reste au processeur */
        GLuint pv = compile(GL_VERTEX_SHADER, VS_PLAN), pg = compile(GL_GEOMETRY_SHADER, GS_PLAN), pf = compile(GL_FRAGMENT_SHADER, FS);
        const bool fonctions = charger_gl_plan();
        g_prog_plan = sfc::gfx::glCreateProgram();
        sfc::gfx::glAttachShader(g_prog_plan, pv);
        sfc::gfx::glAttachShader(g_prog_plan, pg);
        sfc::gfx::glAttachShader(g_prog_plan, pf);
        if (fonctions) {   /* ce que l'épreuve de départ relira (transform feedback) ; sans effet sur le dessin */
            const GLchar *sorties[] = {"gl_Position", "vColor", "vUv"};
            g_glp.TransformFeedbackVaryings(g_prog_plan, 3, sorties, GL_INTERLEAVED_ATTRIBS);
        }
        sfc::gfx::glLinkProgram(g_prog_plan);
        GLint lie = 0;
        sfc::gfx::glGetProgramiv(g_prog_plan, GL_LINK_STATUS, &lie);
        if (!fonctions) {
            rt_log("interpolation : il manque au pilote des fonctions d'OpenGL 3.0, elle reste au processeur");
            sfc::gfx::glDeleteProgram(g_prog_plan);
            g_prog_plan = 0;
        } else if (!lie) {
            char log[2048];
            sfc::gfx::glGetProgramInfoLog(g_prog_plan, sizeof log, nullptr, log);
            rt_log("interpolation : le programme du GPU ne se lie pas, elle reste au processeur (%s)", log);
            sfc::gfx::glDeleteProgram(g_prog_plan);
            g_prog_plan = 0;
        } else {
            auto u = [](const char *nom) { return sfc::gfx::glGetUniformLocation(g_prog_plan, nom); };
            g_up.tex = u("tex"); g_up.use_tex = u("useTex"); g_up.alpha_ref = u("alphaRef"); g_up.mode = u("mode");
            g_up.fog[0] = u("fogEnable"); g_up.fog[1] = u("fogAlphaOnly"); g_up.fog[2] = u("fogColor");
            g_up.fog[3] = u("fogOffset"); g_up.fog[4] = u("fogStep"); g_up.fog[5] = u("fogTable");
            g_up.f16 = u("f16"); g_up.tq = u("tq"); g_up.tf = u("tf");
            if (!epreuve_interpolation_gpu()) {
                sfc::gfx::glDeleteProgram(g_prog_plan);
                g_prog_plan = 0;
            }
        }
    }
    sfc::gfx::glGenVertexArrays(1, &g_vao);
    sfc::gfx::glGenBuffers(1, &g_vbo);
    sfc::gfx::glBindVertexArray(g_vao);
    sfc::gfx::glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    sfc::gfx::glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0);
    sfc::gfx::glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)(4 * sizeof(float)));
    sfc::gfx::glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)(8 * sizeof(float)));
    sfc::gfx::glEnableVertexAttribArray(0);
    sfc::gfx::glEnableVertexAttribArray(1);
    sfc::gfx::glEnableVertexAttribArray(2);
    auto tampon_de_sommets = [](GLuint &vao, GLuint &vbo) {
        sfc::gfx::glGenVertexArrays(1, &vao);
        sfc::gfx::glGenBuffers(1, &vbo);
        sfc::gfx::glBindVertexArray(vao);
        sfc::gfx::glBindBuffer(GL_ARRAY_BUFFER, vbo);
        sfc::gfx::glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0);
        sfc::gfx::glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)(4 * sizeof(float)));
        sfc::gfx::glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)(8 * sizeof(float)));
        sfc::gfx::glEnableVertexAttribArray(0);
        sfc::gfx::glEnableVertexAttribArray(1);
        sfc::gfx::glEnableVertexAttribArray(2);
    };
    tampon_de_sommets(g_vao_rects, g_vbo_rects);
    {   /* 0–5 : une couche 2D, la ligne 0 de la DS en haut (t inversé) ; 6–11 : la réduction du tampon hors écran */
        Vtx q[12] = {
            {-1, -1, 0, 1, 1, 1, 1, 1, 0, 1}, {1, -1, 0, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 0, 1, 1, 1, 1, 1, 1, 0},
            {-1, -1, 0, 1, 1, 1, 1, 1, 0, 1}, {1, 1, 0, 1, 1, 1, 1, 1, 1, 0}, {-1, 1, 0, 1, 1, 1, 1, 1, 0, 0},
            {-1, -1, 0, 1, 1, 1, 1, 1, 0, 0}, {1, -1, 0, 1, 1, 1, 1, 1, 1, 0}, {1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
            {-1, -1, 0, 1, 1, 1, 1, 1, 0, 0}, {1, 1, 0, 1, 1, 1, 1, 1, 1, 1}, {-1, 1, 0, 1, 1, 1, 1, 1, 0, 1},
        };
        sfc::gfx::glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
    }
    tampon_de_sommets(g_vao_fond, g_vbo_fond);
    sfc::gfx::glBindVertexArray(g_vao);
    sfc::gfx::glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glGenTextures(4, g_screen_tex);
    g_ready = true;
    return true;
}

/* La fenêtre et son contexte OpenGL appartiennent au fil qui les crée : celui du jeu d'ordinaire,
   le fil d'affichage quand l'affichage est libre. */
extern "C" bool video_init() {
    if (!affichage_libre()) return video_init_ici();
    lancer_fil_affichage();
    return g_ready;
}

// SWAP_BUFFERS : l'image 3D devient celle affichée jusqu'au prochain swap.
extern "C" void gx_present(const GxFrame *f) { g_last_3d = f; }

namespace {

// Fond de l'image 3D : la console remplit le tampon avec CLEAR_COLOR avant de dessiner, et son
// alpha laisse voir la couche 2D dessous (le ciel d'un stage, par exemple). Sans cela la scène
// 3D se détache sur du noir.
/* Le brouillard de la console : une couleur, un seuil de profondeur et une table de
 * trente-deux densités. C'est lui qui fond le décor lointain dans le ciel — sans lui le
 * terrain s'arrête net, puisque le jeu le dessine exactement jusqu'au plan lointain.
 *
 * SFC_NO_FOG=1 le coupe : l'image est alors dégagée jusqu'à la limite du décor. */
/* `u` : les emplacements du brouillard dans le programme qu'on utilise (UnisPlan::fog) ; nul, ceux du
   programme principal */
void set_fog(const GxFrame *f, const GLint *u = nullptr) {
    const GLint principal[6] = {g_uni_fog_on, g_uni_fog_alpha_only, g_uni_fog_color, g_uni_fog_offset, g_uni_fog_step, g_uni_fog_table};
    if (!u) u = principal;
    /* Éteint par défaut : la conversion de profondeur n'est pas encore juste — la brume noie
       les objets proches — et l'on cherche de toute façon à repousser la limite du décor
       plutôt qu'à la masquer. SFC_FOG=1 l'allume pour reprendre ce travail. */
    static int want = -1;
    if (want < 0) want = getenv("SFC_FOG") != nullptr;
    int on = want && (f->disp3dcnt & 0x80) ? 1 : 0;
    sfc::gfx::glUniform1i(u[0], on);
    if (!on) return;
    uint32_t c = f->fog_color;
    sfc::gfx::glUniform4f(u[2], (float)(c & 31) / 31.0f, (float)((c >> 5) & 31) / 31.0f,
                          (float)((c >> 10) & 31) / 31.0f, (float)((c >> 16) & 31) / 31.0f);
    sfc::gfx::glUniform1i(u[1], (f->disp3dcnt & 0x40) ? 1 : 0);
    /* La profondeur du brouillard se compte sur quinze bits et le pas de la table vaut 0x400
       décalé par DISP3DCNT ; on ramène le tout à la profondeur normalisée d'OpenGL. */
    const float full = 32767.0f;
    int shift = (int)((f->disp3dcnt >> 8) & 0xF);
    sfc::gfx::glUniform1f(u[3], (float)(f->fog_offset & 0x7FFF) / full);
    sfc::gfx::glUniform1f(u[4], (float)(0x400 >> shift) / full);
    float table[32];
    for (int i = 0; i < 32; i++) {
        /* densités rangées quatre par mot, sur sept bits : 127 vaut opaque */
        uint32_t byte = (f->fog_table[i / 4] >> ((i % 4) * 8)) & 0x7F;
        table[i] = (float)byte / 127.0f;
    }
    sfc::gfx::glUniform1fv(u[5], 32, table);
    static int traced;
    if (getenv("SFC_TRACE_FOG") && traced++ < 4) {
        char buf[256] = {0};
        for (int i = 0; i < 32; i++) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%d ", (int)(table[i] * 127));
        rt_log("brouillard : couleur=%08X décalage=%04X pas=%d profondeur=%s ; table %s",
               f->fog_color, f->fog_offset & 0x7FFF, 0x400 >> shift,
               (f->swap_flags & 2) ? "W" : "Z", buf);
    }
}

void draw_clear_color(const GxFrame *f, int vx, int vy, int vw, int vh) {
    uint32_t c = f->clear_color;
    uint32_t a = (c >> 16) & 31;
    if (!a) return;
    float r = (float)(c & 31) / 31.0f, g = (float)((c >> 5) & 31) / 31.0f, b = (float)((c >> 10) & 31) / 31.0f;
    float al = (float)a / 31.0f;
    glViewport(vx, vy, vw, vh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    sfc::gfx::glUseProgram(g_program);
    sfc::gfx::glBindVertexArray(g_vao_fond);
    sfc::gfx::glBindBuffer(GL_ARRAY_BUFFER, g_vbo_fond);
    sfc::gfx::glUniform1i(g_uni_use_tex, 0);
    sfc::gfx::glUniform1i(g_uni_mode, 0);
    sfc::gfx::glUniform1f(g_uni_alpha_ref, 1.0f / 512.0f);
    sfc::gfx::glUniform1i(g_uni_fog_on, 0);
    Vtx q[6] = {
        {-1, -1, 0, 1, r, g, b, al, 0, 0}, {1, -1, 0, 1, r, g, b, al, 0, 0}, {1, 1, 0, 1, r, g, b, al, 0, 0},
        {-1, -1, 0, 1, r, g, b, al, 0, 0}, {1, 1, 0, 1, r, g, b, al, 0, 0}, {-1, 1, 0, 1, r, g, b, al, 0, 0},
    };
    sfc::gfx::glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    sfc::gfx::glBindVertexArray(g_vao);
    sfc::gfx::glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
}

void draw_polygons(const GxFrame *f, int vx, int vy, int vw, int vh) {
    glViewport(vx, vy, vw, vh);
    sfc::gfx::glUseProgram(g_program);
    sfc::gfx::glBindVertexArray(g_vao);
    sfc::gfx::glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    sfc::gfx::glActiveTexture(GL_TEXTURE0);
    sfc::gfx::glUniform1i(g_uni_tex, 0);
    uint32_t ref_reg = g_source ? g_source->alpha_ref : mmio_peek(0x340);
    float alpha_ref = (f->disp3dcnt & 0x4) ? ((ref_reg & 31) / 31.0f) : 0.0f;
    sfc::gfx::glUniform1f(g_uni_alpha_ref, alpha_ref > 0 ? alpha_ref : 1.0f / 64.0f);
    set_fog(f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* Deux temps : on range d'abord les sommets de toute l'image, lot après lot — un lot par changement de
       texture, de mode ou de faces cachées —, puis on les envoie au GPU en une fois et l'on dessine les lots
       dans le même ordre, avec les mêmes réglages. Envoyer chaque lot à part, c'était une dizaine d'envois
       par image dans le même tampon, que le pilote devait chacun ranger à l'écart de ce que le GPU lisait
       encore. */
    struct Lot {
        int passe;
        GLuint tex; int mode; bool use_tex; uint32_t cull;
        GLint wrap_s, wrap_t;
        GLint premier, nombre;
    };
    static std::vector<Vtx> sommets;
    static std::vector<Lot> lots;
    sommets.clear();
    lots.clear();
    static int no_cull = -1;
    if (no_cull < 0) no_cull = getenv("SFC_NO_CULL") != nullptr;   /* diagnostic */
    auto clore = [&]() { if (!lots.empty()) lots.back().nombre = (GLint)sommets.size() - lots.back().premier; };
    for (int pass = 0; pass < 2; pass++) {
        GLuint cur_tex = 0; int cur_mode = -1; bool cur_use = false; uint32_t cur_cull = ~0u;
        bool ouvert = false;
        for (size_t i = 0; i < f->count; i++) {
            const GxPolygon &p = f->polys[i];
            if ((pass == 0) == (bool)p.translucent) continue;
            uint32_t alpha = (p.attr >> 16) & 31;
            if (alpha == 0) continue;
            int tw = 8, th = 8;
            if (g_src_ancienne) g_source = p.ancienne ? g_src_ancienne : g_src_recente;
            GLuint tex = texture_for(p.texparam, p.pltt_base, tw, th);
            int mode = (p.attr >> 4) & 3;
            uint32_t cull = (p.attr >> 6) & 3;
            if (!ouvert || tex != cur_tex || mode != cur_mode || (tex != 0) != cur_use || cull != cur_cull) {
                clore();
                cur_tex = tex; cur_mode = mode; cur_use = tex != 0; cur_cull = cull;
                ouvert = true;
                Lot l{pass, tex, mode, tex != 0, cull, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, (GLint)sommets.size(), 0};
                if (tex) {
                    bool rep_s = (p.texparam >> 16) & 1, rep_t = (p.texparam >> 17) & 1;
                    bool flip_s = (p.texparam >> 18) & 1, flip_t = (p.texparam >> 19) & 1;
                    l.wrap_s = rep_s ? (flip_s ? GL_MIRRORED_REPEAT : GL_REPEAT) : GL_CLAMP_TO_EDGE;
                    l.wrap_t = rep_t ? (flip_t ? GL_MIRRORED_REPEAT : GL_REPEAT) : GL_CLAMP_TO_EDGE;
                }
                lots.push_back(l);
            }
            auto emit = [&](const GxVertex &v) {
                Vtx o;
                o.x = v.x / 4096.0f; o.y = v.y / 4096.0f; o.z = v.z / 4096.0f; o.w = v.w / 4096.0f;
                o.r = v.r / 31.0f; o.g = v.g / 31.0f; o.b = v.b / 31.0f; o.a = alpha / 31.0f;
                o.s = (v.s / 16.0f) / (float)tw; o.t = (v.t / 16.0f) / (float)th;
                sommets.push_back(o);
            };
            if (p.count == 3) { emit(p.v[0]); emit(p.v[2]); emit(p.v[1]); }
            else { emit(p.v[0]); emit(p.v[2]); emit(p.v[1]); emit(p.v[0]); emit(p.v[3]); emit(p.v[2]); }
        }
        clore();
    }
    if (!sommets.empty())
        sfc::gfx::glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sommets.size() * sizeof(Vtx)), sommets.data(), GL_STREAM_DRAW);
    int passe = -1;
    for (const Lot &l : lots) {
        if (l.nombre <= 0) continue;
        if (l.passe != passe) { passe = l.passe; glDepthMask(passe == 0 ? GL_TRUE : GL_FALSE); }
        // POLYGON_ATTR bits 6-7 : afficher les faces arrière, afficher les faces avant. Les
        // triangles sont émis dans l'ordre inverse (v0, v2, v1) pour l'orientation du NDS,
        // ce qui échange l'avant et l'arrière du point de vue d'OpenGL.
        if (l.cull == 3 || no_cull) glDisable(GL_CULL_FACE);
        else { glEnable(GL_CULL_FACE); glCullFace(l.cull == 1 ? GL_BACK : l.cull == 2 ? GL_FRONT : GL_FRONT_AND_BACK); }
        glBindTexture(GL_TEXTURE_2D, l.tex);
        if (l.tex) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, l.wrap_s);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, l.wrap_t);
        }
        sfc::gfx::glUniform1i(g_uni_use_tex, l.use_tex ? 1 : 0);
        sfc::gfx::glUniform1i(g_uni_mode, l.mode);
        glDrawArrays(GL_TRIANGLES, l.premier, l.nombre);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
}

// Dessine une couche 2D (texture RGBA 256x192) sur une zone de la fenêtre. `envoyer` : la texture ne
// contient pas encore ces pixels — sinon, inutile de renvoyer 192 Ko au GPU.
void draw_layer(GLuint tex, const uint32_t *pixels, int vx, int vy, int vw, int vh, bool envoyer = true) {
    glBindTexture(GL_TEXTURE_2D, tex);
    if (envoyer) {
        static bool allouee[4];
        int k = -1;
        for (int i = 0; i < 4; i++) if (g_screen_tex[i] == tex) k = i;
        if (k >= 0 && allouee[k]) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DS_W, DS_H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, DS_W, DS_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (k >= 0) allouee[k] = true;
        }
    }
    glViewport(vx, vy, vw, vh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    // couches en alpha prémultiplié : opaque (a=1) et transparent (0) inchangés, et un fondu
    // vers la 3D (rgb*eva/16, a=1-evb/16) donne rgb + dessous*evb/16
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    sfc::gfx::glUseProgram(g_program);
    sfc::gfx::glActiveTexture(GL_TEXTURE0);
    sfc::gfx::glUniform1i(g_uni_tex, 0);
    sfc::gfx::glUniform1i(g_uni_use_tex, 1);
    sfc::gfx::glUniform1i(g_uni_mode, 0);
    sfc::gfx::glUniform1f(g_uni_alpha_ref, 1.0f / 512.0f);
    sfc::gfx::glUniform1i(g_uni_fog_on, 0);
    // quad plein écran ; la ligne 0 de la DS est en haut : t inversé (sommets 0–5 du tampon des rectangles)
    sfc::gfx::glBindVertexArray(g_vao_rects);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    sfc::gfx::glBindVertexArray(g_vao);
}

/* Ramène le tampon hors écran dans la fenêtre.
 *
 * Pas par glBlitFramebuffer : la fenêtre a un tampon multi-échantillonné, et OpenGL refuse de
 * réduire entre deux tampons dont le nombre d'échantillons diffère — l'image restait noire. On
 * dessine donc un quad texturé, ce qui laisse de toute façon le choix du filtre. */
void draw_offscreen(GLuint tex, int vw, int vh) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glViewport(0, 0, vw, vh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    sfc::gfx::glUseProgram(g_program);
    sfc::gfx::glBindVertexArray(g_vao);
    sfc::gfx::glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    sfc::gfx::glActiveTexture(GL_TEXTURE0);
    sfc::gfx::glUniform1i(g_uni_tex, 0);
    sfc::gfx::glUniform1i(g_uni_use_tex, 1);
    sfc::gfx::glUniform1i(g_uni_mode, 0);
    sfc::gfx::glUniform1f(g_uni_alpha_ref, 0.0f);
    sfc::gfx::glUniform1i(g_uni_fog_on, 0);
    sfc::gfx::glBindVertexArray(g_vao_rects);          /* sommets 6–11 : sans inversion */
    glDrawArrays(GL_TRIANGLES, 6, 6);
    sfc::gfx::glBindVertexArray(g_vao);
}

}  // namespace


// ------------------------------------------------------------------ capture PNG (blocs stockés, sans compression)
namespace {
uint32_t crc32_png(const uint8_t *p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    static uint32_t table[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1; table[i] = v; } init = true; }
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}
void put32(std::vector<uint8_t> &v, uint32_t x) { v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x); }
void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data) {
    put32(out, (uint32_t)data.size());
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32_png(body.data(), body.size()) ^ 0xFFFFFFFFu);
}
void write_png(const char *path, int w, int h, const uint8_t *rgba_bottom_up) {
    std::vector<uint8_t> raw;
    for (int y = h - 1; y >= 0; y--) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba_bottom_up + (size_t)y * w * 4, rgba_bottom_up + (size_t)(y + 1) * w * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = raw.size() - pos; if (n > 65535) n = 65535;
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(n & 0xFF); z.push_back(n >> 8); z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    put32(z, (b << 16) | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr; put32(ihdr, w); put32(ihdr, h); ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(out.data(), 1, out.size(), f); fclose(f); }
}
// Cadence de la console : 59,8262 images par seconde. Sans cela le jeu tourne aussi vite que la
// machine le permet et tout est accéléré. SFC_FAST=1 ou SFC_MAX_FRAMES (runs automatiques) lève
// la limite. L'attente est hybride : sommeil tant qu'il reste plus de deux millisecondes, puis
// attente active — `sleep_for` seul ne descend pas sous la quinzaine de millisecondes.
extern std::chrono::steady_clock::time_point g_pace_fin;

/* Dormir jusqu'à une échéance : le gros par le minuteur à haute résolution, la dernière demi-milliseconde
   en attente active. Un `sleep_for` d'une milliseconde durait quinze millisecondes sous Windows : le jeu
   rendait ses balayages en retard puis les rattrapait, et l'affichage libre, qui vise quatre millisecondes
   par image, en sautait trois et les rendait d'un coup — soixante images utiles par seconde au lieu de deux
   cent quarante, par paquets, et tout ce qui bouge se voyait double. */
void attendre_jusqua(std::chrono::steady_clock::time_point echeance) {
    using clock = std::chrono::steady_clock;
    for (;;) {
        auto reste = echeance - clock::now();
        if (reste <= clock::duration::zero()) return;
        if (reste > std::chrono::microseconds(700))
            attente_dormir_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(reste - std::chrono::microseconds(500)).count());
        else std::this_thread::yield();
    }
}

void pace_frame() {
    using clock = std::chrono::steady_clock;
    static int unlimited = -1;
    /* SFC_CADENCE=1 : garder la cadence de la console malgré SFC_MAX_FRAMES — un rejeu doit se dérouler
       comme la partie qu'il rejoue, sinon les appuis ne tombent plus au même endroit (le tracé d'une route
       au stylet rate d'une image et la mission ne part pas), et l'arrêt automatique reste utile. */
    if (unlimited < 0) unlimited = (getenv("SFC_FAST") || (getenv("SFC_MAX_FRAMES") && !getenv("SFC_CADENCE"))) ? 1 : 0;
    if (unlimited) {
        /* SFC_APERCUS_ESSAI=k : sans cadence (un rejeu vérifié), un tour d'aperçu à la part k à chaque attente
           du jeu — l'épreuve qu'un aperçu s'efface tout entier : les empreintes ne doivent pas bouger */
        static double essai = -2;
        if (essai == -2) { const char *e = getenv("SFC_APERCUS_ESSAI"); essai = e ? atof(e) : -1; }
        /* SFC_APERCUS_ESSAI_N=n : n aperçus d'affilée, aux parts k/n … k */
        static int essai_n = -1;
        if (essai_n < 0) { const char *e = getenv("SFC_APERCUS_ESSAI_N"); essai_n = e ? atoi(e) : 1; }
        /* SFC_APERCUS_ESSAI_BUDGET=ms : un garde-temps serré, pour que beaucoup d'aperçus soient abandonnés
           en plein travail — l'épreuve que l'abandon rend l'état aussi bien que l'aboutissement */
        static double budget = -2;
        if (budget == -2) { const char *e = getenv("SFC_APERCUS_ESSAI_BUDGET"); budget = e ? atof(e) / 1e3 : 0; }
        for (int j = 1; j <= essai_n && essai >= 0 && natif_apercu_possible(); j++)
            natif_apercu_jouer((int32_t)((getenv("SFC_APERCUS_ESSAI_FIXE") ? essai : essai * j / essai_n) * 65536.0), 0.0, 0x000u, budget);
        g_pace_fin = clock::now();
        return;
    }
    /* SFC_RAPIDE_JUSQUA=image : un rejeu file sans cadence jusqu'à cette image, puis reprend le temps réel —
       pour éprouver l'affichage sur une scène qui arrive après dix minutes de jeu */
    static long long rapide = -1;
    if (rapide < 0) { const char *e = getenv("SFC_RAPIDE_JUSQUA"); rapide = e ? atoll(e) : 0; }
    if ((long long)hw_frames() < rapide) { g_pace_fin = clock::now(); return; }
    static clock::time_point next{};
    const auto period = std::chrono::nanoseconds(16715120);
    auto now = clock::now();
    if (next.time_since_epoch().count() == 0 || now > next + std::chrono::milliseconds(250)) next = now;
    if (g_avance_rapide) {        /* touche F : huit fois plus vite, et pas de tours d'aperçu à y perdre */
        next = now + period / 8;
        attendre_jusqua(next);
        g_pace_fin = clock::now();
        return;
    }
    next += period;
    apercus_pendant(next);
    attendre_jusqua(next);
    g_pace_fin = clock::now();
}

void maybe_dump(int ww, int wh) {
    static int every = -1, from = 0, frame = 0, shot = 0;
    if (every < 0) {
        const char *e = getenv("SFC_DUMP"); every = e ? atoi(e) : 0;
        const char *f = getenv("SFC_DUMP_FROM"); from = f ? atoi(f) : 0;   /* n'enregistre qu'à partir de cette image */
    }
    if (every <= 0) return;
    /* par numéro d'image du jeu : la même capture, que l'affichage soit libre ou non */
    if (g_passe_exacte) return;
    frame = (int)(g_source ? g_image_montree : hw_frames());
    if (frame % every || frame < from) return;
    { static int deja = -1; if (frame == deja) return; deja = frame; }   /* une fois par image du jeu */
    std::vector<uint8_t> px((size_t)ww * wh * 4);
    glReadPixels(0, 0, ww, wh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    char name[64];
    snprintf(name, sizeof name, "build/frame_%04d.png", shot++);
    write_png(name, ww, wh, px.data());
    rt_log("capture : %s = image %d du jeu", name, frame);   /* l'affichage libre saute des images : on dit laquelle */
}
}  // namespace

// ------------------------------------------------------------------ capture d'affichage (DISPCAPCNT)
// Le moteur A peut copier son image finale (2D + 3D) dans une banque VRAM ; le jeu s'en sert pour
// afficher la 3D sur les deux écrans, en alternant les banques C et D d'une image à l'autre et en
// les montrant sur le moteur B comme fond bitmap. On relit la zone rendue et on la ramène au
// format de la console (RGB555, bit 15 levé) ; la capture est acquittée en effaçant le bit 31.
void display_capture(int vy, int vw, int vh) {
    if (g_source && !g_passe_exacte) return;      /* une image interpolée n'est pas celle que le jeu a demandée */
    uint32_t cap = g_source ? g_source->capture : mmio_peek(0x064);
    if (!(cap & 0x80000000u)) return;
    static const int cw[4] = {128, 256, 256, 256}, chh[4] = {128, 64, 128, 192};
    int size = (cap >> 20) & 3, w = cw[size], h = chh[size];
    uint32_t bank = (cap >> 16) & 3, woff = ((cap >> 18) & 3) * 0x8000;
    uint32_t src = (cap >> 29) & 3;
    static int warned;
    if ((src != 0 || (cap & (1u << 24))) && warned++ < 3)
        rt_log("capture d'affichage : source %u%s non simulée, image composite du moteur A utilisée",
               src, (cap >> 24) & 1 ? " (3D seule)" : "");
    std::vector<uint8_t> px((size_t)vw * vh * 4);
    glReadPixels(0, vy, vw, vh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    /* L'image se convertit à part, et ne se pose en mémoire vidéo que d'un bloc, sous le verrou : la
       composition 2D du balayage (fil du jeu), qui tient ce verrou, la voit ainsi tout entière ou pas du
       tout. Écrite point par point sous ses yeux, elle se déchirait — l'écran du bas montrait parfois le
       haut d'une capture et le bas de la précédente (SFC_BANDES_VERIF l'a pris sur le fait). */
    size_t taille = std::min((size_t)w * h * 2, (size_t)0x20000 - woff);
    std::vector<uint8_t> image(taille);   /* pas statique : à la sortie, ce fil capture encore quand les statiques meurent */
    for (int cy = 0; cy < h; cy++) {
        int gy = (int)(((DS_H - 1 - cy) + 0.5) * vh / (double)DS_H);
        if (gy >= vh) gy = vh - 1;
        for (int cx = 0; cx < w; cx++) {
            int gx = (int)((cx + 0.5) * vw / (double)DS_W);
            if (gx >= vw) gx = vw - 1;
            const uint8_t *q = &px[((size_t)gy * vw + gx) * 4];
            uint16_t c = (uint16_t)(0x8000u | (q[0] >> 3) | ((q[1] >> 3) << 5) | ((q[2] >> 3) << 10));
            size_t o = ((size_t)cy * w + cx) * 2;
            if (o + 1 < taille) { image[o] = (uint8_t)c; image[o + 1] = (uint8_t)(c >> 8); }
        }
    }
    std::lock_guard<std::mutex> verrou(g_mx_capture);
    const size_t debut = BANK_OFFSET[bank] + woff;
    const uint32_t *avant = g_source ? g_source->vram_ecrit : nullptr;   /* affichage libre : après coup */
    const uint64_t image_capturee = g_source ? g_source->image : hw_frames();
    if (g_apercu_en_cours) {     /* un aperçu tient la mémoire vidéo : on écrit de côté, il la rendra intacte */
        g_capture_de_cote.assign(image.begin(), image.end());
        g_capture_debut = debut;
        g_capture_n = taille;
        g_capture_suivie = avant != nullptr;
        if (avant) memcpy(g_capture_ecrit, avant, sizeof g_capture_ecrit);
        g_capture_image = image_capturee;
    } else poser_capture(debut, image.data(), taille, avant, image_capturee);
    if (!g_source) mmio_poke(0x064, cap & ~0x80000000u);   /* en affichage libre, le jeu l'a déjà acquittée */
}

// Une image complète, à chaque VBlank : les deux écrans. Deux temps : composer la 2D — qui lit la
// mémoire vidéo, donc sur le fil du jeu — puis dessiner, sur le fil qui tient OpenGL.
namespace {

/* Les deux moteurs 2D se composent par bandes de lignes, sur un atelier de fils qui durent — en créer à
   chaque image coûtait à lui seul un dixième du temps de la machine hôte. Le fil du jeu prend sa part des
   bandes au lieu d'attendre les autres. Une ligne ne dépend que de la mémoire vidéo et des registres, que
   personne n'écrit pendant ce temps : l'image est la même, point pour point, qu'on la compose d'un bloc
   ou en huit morceaux. */
struct Atelier2d {
    struct Bande { int moteur; Engine2dOutput *out; int y0, y1; };
    static constexpr int PAR_MOTEUR = 4;
    std::mutex m;
    std::condition_variable cv_travail, cv_fini;
    Bande bandes[2 * PAR_MOTEUR];
    int nb = 0, prochaine = 0, restantes = 0;
    explicit Atelier2d(int fils) {
        static const char *const noms[] = {"2D 1", "2D 2", "2D 3", "2D 4", "2D 5", "2D 6"};
        for (int i = 0; i < fils && i < 6; i++)
            std::thread([this, i] {
                rt_fil_nommer(noms[i]);
                std::unique_lock<std::mutex> l(m);
                for (;;) {
                    cv_travail.wait(l, [this] { return prochaine < nb; });
                    Bande b = bandes[prochaine++];
                    l.unlock();
                    video2d_render_lignes(b.moteur, b.out, b.y0, b.y1);
                    l.lock();
                    if (--restantes == 0) cv_fini.notify_all();
                }
            }).detach();
    }
    void composer(Engine2dOutput *out) {
        std::unique_lock<std::mutex> l(m);
        nb = prochaine = 0;
        for (int k = 0; k < PAR_MOTEUR; k++)
            for (int moteur = 1; moteur >= 0; moteur--)        /* le moteur B d'abord : c'est le plus lent */
                bandes[nb++] = Bande{moteur, &out[moteur], k * DS_H / PAR_MOTEUR, (k + 1) * DS_H / PAR_MOTEUR};
        restantes = nb;
        cv_travail.notify_all();
        while (prochaine < nb) {
            Bande b = bandes[prochaine++];
            l.unlock();
            video2d_render_lignes(b.moteur, b.out, b.y0, b.y1);
            l.lock();
            --restantes;
        }
        cv_fini.wait(l, [this] { return restantes == 0; });
    }
};

/* Les deux couches d'un écran, composées dans les tampons qu'on veut : ceux du balayage, ou ceux d'un
   tour d'aperçu, qui a ses propres sprites, fonds et HUD — à sa fraction de tour. */
void composer_2d_dans(Engine2dOutput *out) {
    if (profiling()) g_t_2d.start();
    /* La capture d'affichage écrit en mémoire vidéo depuis le fil d'affichage : on la tient à l'écart le temps
       de composer. Pendant un tour d'aperçu, elle écrit de côté et n'y touche pas. */
    std::unique_lock<std::mutex> tenue(g_mx_capture, std::defer_lock);
    if (!g_apercu_en_cours) tenue.lock();
    /* Les deux moteurs 2D sont indépendants, et chaque ligne de chacun aussi : ils lisent la mémoire
       vidéo sans l'écrire et remplissent chacun son tampon. Les composer en parallèle est l'un des
       endroits où une machine à plusieurs cœurs sert vraiment ici — la simulation d'un programme
       séquentiel, elle, ne se répartit pas. */
    if (parallel_2d()) {
        static Atelier2d *atelier = new Atelier2d([] {
            unsigned n = std::thread::hardware_concurrency();
            return n > 6 ? 4 : n > 3 ? (int)n - 2 : 1;
        }());
        atelier->composer(out);
        {   /* SFC_BANDES_VERIF=1 : chaque écran recomposé d'un bloc, sur ce fil, et comparé octet par octet */
            static int verif = -1; static uint64_t vues, fausses;
            if (verif < 0) verif = getenv("SFC_BANDES_VERIF") != nullptr;
            if (verif) {
                static Engine2dOutput ref[2];
                for (int m = 0; m < 2; m++, vues++) {
                    video2d_render(m, &ref[m]);
                    if (!memcmp(ref[m].below, out[m].below, sizeof ref[m].below) && !memcmp(ref[m].above, out[m].above, sizeof ref[m].above)
                        && ref[m].has_3d == out[m].has_3d && ref[m].display_mode == out[m].display_mode && ref[m].brightness == out[m].brightness)
                        continue;
                    int n = 0, y0 = DS_H, y1 = -1;
                    for (int k = 0; k < DS_W * DS_H; k++)
                        if (ref[m].below[k] != out[m].below[k] || ref[m].above[k] != out[m].above[k]) {
                            n++; y0 = std::min(y0, k / DS_W); y1 = std::max(y1, k / DS_W);
                        }
                    if (++fausses <= 30)
                        rt_log("2D par bandes : image %llu, moteur %c%s : %d points différents, lignes %d à %d",
                               (unsigned long long)hw_frames(), m ? 'B' : 'A', g_apercu_en_cours ? " (aperçu)" : "", n, y0, y1);
                }
                if (vues % 600 == 0) rt_log("2D par bandes : %llu écrans comparés, %llu différents", (unsigned long long)vues, (unsigned long long)fausses);
            }
        }
    } else {
        video2d_render(0, &out[0]);
        video2d_render(1, &out[1]);
    }
    if (profiling()) g_t_2d.stop();
}

void composer_2d() { composer_2d_dans(g_out2d); }

/* (SFC_DIAG_ACOUPS) ce que ce tour-ci a passé en tours d'aperçu, et ce qu'ils ont mordu sur le balayage */
double g_ms_apercus, g_ms_debord;
int g_n_apercus_tour, g_n_debords;

/* ------------------------------------------------------------------ l'interpolation sur le GPU
 * `interpoler` rend, pour un instant, les polygones entre deux images du jeu ; draw_polygons les convertit, en
 * cherche les textures une à une et les envoie. C'était, à chaque image affichée, le gros du travail du fil
 * d'affichage débridé. Le plan d'une paire d'images (sfc::planifier) part au GPU une fois ; à chaque image
 * affichée, le nuanceur de géométrie refait `interpoler` pour l'instant voulu — mêmes règles, même ordre,
 * mêmes seuils, les couleurs et les coordonnées de texture au même arrondi. Les lots de même état sont faits
 * une fois ; à chaque image on ne cherche plus que la texture de chacun, que texture_for revalide (le jeu a
 * pu la réécrire entre-temps). SFC_INTERPOLATION=cpu revient au processeur, pour comparer ; SFC_DIAG_COULEURS,
 * qui colore les polygones selon leur chemin, y revient aussi. */
struct VtxPlan {
    int32_t p0[4], p1[4];    /* les deux bouts, en repère de découpage (20.12) */
    uint8_t c0[4], c1[4];    /* leurs couleurs (0–31) ; c0[3] : l'alpha du polygone */
    int16_t st[4];           /* leurs coordonnées de texture (12.4) : s0, t0, s1, t1 */
    float extra[4];          /* ce qui vaut pour tout le polygone, réparti sur ses sommets : le déplacement de son
                                centre (sommet 0), ses drapeaux et le décalage de sa texture (1), l'échelle de sa
                                texture (2) */
};
struct LotPlan { int passe; uint32_t texparam, pltt; uint8_t ancienne; int mode; uint32_t cull; GLint premier, nombre; };
struct Plan {
    uint64_t de = 0, vers = 0;       /* les numéros des deux instantanés */
    bool apercu = false, pret = false;
    GLuint vao = 0, vbo = 0;
    std::vector<LotPlan> lots;       /* les opaques, puis les translucides — chacun dans l'ordre du dessin */
    size_t polys = 0;
    uint64_t servi = 0;
};
Plan g_plans[4];
uint64_t g_plans_servis, g_plans_construits;

bool interpolation_gpu() {
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("SFC_INTERPOLATION");
        v = g_prog_plan && !(e && !strcmp(e, "cpu")) && !getenv("SFC_DIAG_COULEURS");
    }
    return v;
}

void construire_plan(Plan &p, const Instantane *de, const Instantane *vers, const sfc::Lien &lien) {
    static std::vector<sfc::PolyPlan> pp;
    static std::vector<VtxPlan> sommets[2];
    static std::vector<LotPlan> lots[2];
    sfc::planifier(de->polys, vers->polys, lien, pp);
    for (int k = 0; k < 2; k++) { sommets[k].clear(); lots[k].clear(); }
    size_t n = 0;
    for (const sfc::PolyPlan &q : pp) {
        const GxPolygon &e = *q.etat;
        const uint32_t alpha = (e.attr >> 16) & 31;
        if (!alpha) continue;                                  /* draw_polygons ne les dessine pas */
        const int passe = e.translucent ? 1 : 0, mode = (int)((e.attr >> 4) & 3);
        const uint32_t cull = (e.attr >> 6) & 3;
        std::vector<LotPlan> &ls = lots[passe];
        if (ls.empty() || ls.back().texparam != e.texparam || ls.back().pltt != e.pltt_base || ls.back().ancienne != q.ancienne
            || ls.back().mode != mode || ls.back().cull != cull)
            ls.push_back(LotPlan{passe, e.texparam, e.pltt_base, q.ancienne, mode, cull, (GLint)(sommets[passe].size() / 4), 0});
        ls.back().nombre++;
        int tw = 8, th = 8;
        if ((e.texparam >> 26) & 7) { tw = 8 << ((e.texparam >> 20) & 7); th = 8 << ((e.texparam >> 23) & 7); }
        const int cnt = (q.drapeaux & sfc::PLAN_QUATRE) ? 4 : 3;
        for (int k = 0; k < 4; k++) {
            const int m = k < cnt ? k : cnt - 1;              /* le quatrième sommet d'un triangle : une copie, jamais montrée */
            VtxPlan v{};
            memcpy(v.p0, q.p0[m], sizeof v.p0);
            memcpy(v.p1, q.p1[m], sizeof v.p1);
            v.c0[0] = q.c0[m][0]; v.c0[1] = q.c0[m][1]; v.c0[2] = q.c0[m][2]; v.c0[3] = (uint8_t)alpha;
            v.c1[0] = q.c1[m][0]; v.c1[1] = q.c1[m][1]; v.c1[2] = q.c1[m][2];
            v.st[0] = q.s0[m]; v.st[1] = q.t0[m]; v.st[2] = q.s1[m]; v.st[3] = q.t1[m];
            if (k == 0) memcpy(v.extra, q.delta, sizeof v.extra);
            else if (k == 1) { v.extra[0] = (float)q.drapeaux; v.extra[1] = q.ds; v.extra[2] = q.dt; }
            else if (k == 2) { v.extra[0] = 1.0f / (16.0f * (float)tw); v.extra[1] = 1.0f / (16.0f * (float)th); }
            sommets[passe].push_back(v);
        }
        n++;
    }
    const GLint opaques = (GLint)(sommets[0].size() / 4);
    for (LotPlan &l : lots[1]) l.premier += opaques;
    p.lots.assign(lots[0].begin(), lots[0].end());
    p.lots.insert(p.lots.end(), lots[1].begin(), lots[1].end());
    sommets[0].insert(sommets[0].end(), sommets[1].begin(), sommets[1].end());
    if (!p.vao) {
        sfc::gfx::glGenVertexArrays(1, &p.vao);
        sfc::gfx::glGenBuffers(1, &p.vbo);
        sfc::gfx::glBindVertexArray(p.vao);
        sfc::gfx::glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
        const GLsizei pas = sizeof(VtxPlan);
        g_glp.VertexAttribIPointer(0, 4, GL_INT, pas, (void *)offsetof(VtxPlan, p0));
        g_glp.VertexAttribIPointer(1, 4, GL_INT, pas, (void *)offsetof(VtxPlan, p1));
        sfc::gfx::glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_FALSE, pas, (void *)offsetof(VtxPlan, c0));
        sfc::gfx::glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, pas, (void *)offsetof(VtxPlan, c1));
        sfc::gfx::glVertexAttribPointer(4, 4, GL_SHORT, GL_FALSE, pas, (void *)offsetof(VtxPlan, st));
        sfc::gfx::glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, pas, (void *)offsetof(VtxPlan, extra));
        for (GLuint a = 0; a < 6; a++) sfc::gfx::glEnableVertexAttribArray(a);
    } else {
        sfc::gfx::glBindVertexArray(p.vao);
        sfc::gfx::glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
    }
    sfc::gfx::glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sommets[0].size() * sizeof(VtxPlan)), sommets[0].data(), GL_STATIC_DRAW);
    p.de = de->numero; p.vers = vers->numero; p.apercu = lien.b_apercu; p.pret = true; p.polys = n;
    g_plans_construits++;
}

/* le plan d'une paire d'images : déjà fait s'il a servi récemment, sinon à la place du plus ancien */
const Plan *plan_pour(const Instantane *de, const Instantane *vers, const sfc::Lien &lien) {
    Plan *libre = &g_plans[0];
    for (Plan &p : g_plans) {
        if (p.pret && p.de == de->numero && p.vers == vers->numero && p.apercu == lien.b_apercu) { p.servi = ++g_plans_servis; return &p; }
        if (p.servi < libre->servi) libre = &p;
    }
    construire_plan(*libre, de, vers, lien);
    libre->servi = ++g_plans_servis;
    return libre;
}

/* draw_polygons pour un plan, à l'instant f16 (et t, que prennent les trajets tout tracés) : les mêmes lots,
   dans le même ordre, avec les mêmes réglages. */
void draw_plan(const Plan &p, const GxFrame *f, int32_t f16, float t, int vx, int vy, int vw, int vh) {
    glViewport(vx, vy, vw, vh);
    sfc::gfx::glUseProgram(g_prog_plan);
    sfc::gfx::glBindVertexArray(p.vao);
    sfc::gfx::glActiveTexture(GL_TEXTURE0);
    sfc::gfx::glUniform1i(g_up.tex, 0);
    uint32_t ref_reg = g_source ? g_source->alpha_ref : mmio_peek(0x340);
    float alpha_ref = (f->disp3dcnt & 0x4) ? ((ref_reg & 31) / 31.0f) : 0.0f;
    sfc::gfx::glUniform1f(g_up.alpha_ref, alpha_ref > 0 ? alpha_ref : 1.0f / 64.0f);
    set_fog(f, g_up.fog);
    sfc::gfx::glUniform1i(g_up.f16, f16);
    sfc::gfx::glUniform1f(g_up.tq, (float)f16 / 65536.0f);
    sfc::gfx::glUniform1f(g_up.tf, t);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    static int no_cull = -1;
    if (no_cull < 0) no_cull = getenv("SFC_NO_CULL") != nullptr;
    const Instantane *recente = g_src_recente, *ancienne = g_src_ancienne;
    /* Les lots se suivent dans le tampon : deux lots voisins de la même passe qui reviennent, cette image-ci,
       au même état (même texture, même mode, mêmes faces) se dessinent d'un seul appel. Et l'on ne redit à
       OpenGL que ce qui change. */
    int passe = -1, cull_mis = -1, use_mis = -1, mode_mis = -1;
    struct { GLuint tex; int cull, mode; GLint premier, nombre; } attente{0, 0, 0, 0, 0};
    auto vider = [&]() {
        if (!attente.nombre) return;
        if (attente.cull != cull_mis) {
            cull_mis = attente.cull;
            if (cull_mis == 3) glDisable(GL_CULL_FACE);
            else { glEnable(GL_CULL_FACE); glCullFace(cull_mis == 1 ? GL_BACK : cull_mis == 2 ? GL_FRONT : GL_FRONT_AND_BACK); }
        }
        glBindTexture(GL_TEXTURE_2D, attente.tex);   /* toujours : texture_for, qui envoie les textures neuves, a pu en lier une autre */
        const int use = attente.tex ? 1 : 0;
        if (use != use_mis) { use_mis = use; sfc::gfx::glUniform1i(g_up.use_tex, use); }
        if (attente.mode != mode_mis) { mode_mis = attente.mode; sfc::gfx::glUniform1i(g_up.mode, mode_mis); }
        glDrawArrays(GL_LINES_ADJACENCY, attente.premier * 4, attente.nombre * 4);
        attente.nombre = 0;
    };
    for (const LotPlan &l : p.lots) {
        if (l.passe != passe) { vider(); passe = l.passe; glDepthMask(passe == 0 ? GL_TRUE : GL_FALSE); }
        if (ancienne) g_source = l.ancienne ? ancienne : recente;
        int tw = 8, th = 8;
        const GLuint tex = texture_for(l.texparam, l.pltt, tw, th);
        const int cull = (l.cull == 3 || no_cull) ? 3 : (int)l.cull;
        if (attente.nombre && tex == attente.tex && cull == attente.cull && l.mode == attente.mode
            && l.premier == attente.premier + attente.nombre) { attente.nombre += l.nombre; continue; }
        vider();
        attente = {tex, cull, l.mode, l.premier, l.nombre};
    }
    vider();
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    if (profiling()) {   /* SFC_PROFILE : ce que coûte une image, en lots et en plans construits */
        static uint64_t images, lots, polys, plans_avant;
        static auto t0 = std::chrono::steady_clock::now();
        images++; lots += p.lots.size(); polys += p.polys;
        auto t = std::chrono::steady_clock::now();
        double s = std::chrono::duration<double>(t - t0).count();
        if (s >= 5.0) {
            rt_log("interpolation sur le GPU : %.0f lots et %.0f polygones par écran dessiné, %.0f plans construits par seconde",
                   (double)lots / (double)images, (double)polys / (double)images, (double)(g_plans_construits - plans_avant) / s);
            images = lots = polys = 0; plans_avant = g_plans_construits; t0 = t;
        }
    }
}

/* L'épreuve de départ. Le nuanceur de géométrie refait `interpoler` avec l'arithmétique entière du processeur ; ce
   que le pilote en fait réellement — NVIDIA, AMD et Intel ont chacun leur compilateur — ne se voit qu'en le lui
   demandant. Au démarrage, sur la carte du joueur, on lui fait donc calculer des polygones d'épreuve qui passent
   par tous ses chemins : pareil ordinaire, coordonnées géantes (le produit de 64 bits), interface à plat calée au
   pixel, forme raide, taille qui change vite, voisinage, changé de forme, sans pareil, disparus, retournement,
   deux états, polygone invisible, translucide, sommet qui passe derrière la caméra — à neuf instants, prédiction
   comprise, et B aperçu ou non. On relit ce qu'il en sort (transform feedback) et on le compare, sommet par sommet,
   à ce que rendraient `interpoler` et draw_polygons. Au moindre écart, l'interpolation reste au processeur et le
   journal dit où. SFC_INTERPOLATION_EPREUVE=echec simule un écart, pour éprouver ce repli. */
bool epreuve_interpolation_gpu() {
    const uint32_t TEX_A = (3u << 26) | (2u << 23) | (2u << 20) | (1u << 17) | (1u << 16) | 0x0100u;   /* 16 couleurs, 32×32, répétée */
    const uint32_t TEX_B = (TEX_A & ~0xFFFFu) | 0x0180u;                                                 /* la même, ailleurs */
    const uint32_t ATTR_OPAQUE = (31u << 16) | (3u << 6), ATTR_TRANSLUCIDE = (12u << 16) | (3u << 6), ATTR_INVISIBLE = 3u << 6;
    auto poly = [](int n, const int32_t (*p)[4], int graine, int16_t ds_, uint32_t attr, uint32_t texparam,
                   uint8_t proj, uint8_t translucide) {
        GxPolygon q{};
        q.count = (uint8_t)n;
        for (int k = 0; k < n; k++) {
            q.v[k].x = p[k][0]; q.v[k].y = p[k][1]; q.v[k].z = p[k][2]; q.v[k].w = p[k][3];
            q.v[k].r = (uint8_t)((3 + 5 * graine + k) & 31); q.v[k].g = (uint8_t)((7 + 3 * graine + 2 * k) & 31);
            q.v[k].b = (uint8_t)((11 + graine + 4 * k) & 31);
            q.v[k].s = (int16_t)(16 * (4 * k + graine) + ds_); q.v[k].t = (int16_t)(16 * (2 * k) + ds_ / 2);
        }
        q.attr = attr; q.texparam = texparam; q.pltt_base = 0; q.translucent = translucide; q.proj = proj;
        return q;
    };
    const int32_t Qo[4][4] = {{-2000, -1500, 1000, 8000}, {2000, -1500, 1000, 8000}, {2000, 1500, 1000, 8000}, {-2000, 1500, 1000, 8000}};
    const int32_t Qq[4][4] = {{-1500, -1700, 900, 7600}, {2300, -1400, 950, 7700}, {2100, 1800, 1100, 8200}, {-1800, 1300, 1050, 8100}};
    const int32_t Go[4][4] = {{-300000000, -225000000, 150000000, 1200000000}, {300000000, -225000000, 150000000, 1200000000},
                              {300000000, 225000000, 160000000, 1190000000}, {-300000000, 225000000, 160000000, 1190000000}};
    const int32_t Gq[4][4] = {{-225000000, -260000000, 140000000, 1140000000}, {345000000, -210000000, 150000000, 1150000000},
                              {315000000, 270000000, 170000000, 1230000000}, {-270000000, 195000000, 165000000, 1215000000}};
    const int32_t Po[3][4] = {{-993, 503, 0, 4096}, {1007, 503, 0, 4096}, {7, -697, 0, 4096}};           /* à plat, w = 1 */
    const int32_t Pq[3][4] = {{-942, 540, 0, 4096}, {1058, 540, 0, 4096}, {58, -660, 0, 4096}};
    const int32_t Ro[4][4] = {{800, 800, 500, 6000}, {1200, 800, 500, 6000}, {1200, 1200, 500, 6000}, {800, 1200, 500, 6000}};
    const int32_t Rq[4][4] = {{730, 710, 520, 6000}, {1370, 710, 520, 6000}, {1370, 1350, 520, 6000}, {730, 1350, 520, 6000}};
    const int32_t Co[4][4] = {{-900, -600, 700, 9000}, {900, -700, 700, 9000}, {1100, 800, 750, 9100}, {-800, 900, 750, 9100}};
    const int32_t Cq[3][4] = {{-600, -500, 720, 9050}, {1000, -400, 730, 9050}, {200, 1000, 760, 9150}};
    const int32_t To[3][4] = {{0, 0, 100, 4096}, {4000, 0, 100, 4096}, {0, 4000, 100, 4096}};             /* se retourne en chemin */
    const int32_t Tq[3][4] = {{0, 0, 100, 4096}, {-4000, 2000, 100, 4096}, {2000, -4000, 100, 4096}};
    const int32_t Wo[4][4] = {{-1000, -800, 300, 5000}, {1000, -800, 300, 5000}, {1000, 800, 300, 5000}, {-1000, 800, 300, 5000}};
    const int32_t Wq[4][4] = {{-1100, -900, 280, 1000}, {1100, -900, 280, 1000}, {1100, 900, 280, 1000}, {-1100, 900, 280, 1000}};
    std::vector<GxPolygon> a, b;
    auto paire = [&](GxPolygon o, GxPolygon q) { a.push_back(o); b.push_back(q); };
    paire(poly(4, Qo, 0, 0, ATTR_OPAQUE, TEX_A, 0, 0), poly(4, Qq, 0, 20, ATTR_OPAQUE, TEX_A, 0, 0));      /* 0 pareil ordinaire */
    paire(poly(4, Go, 1, 0, ATTR_OPAQUE, TEX_A, 0, 0), poly(4, Gq, 1, 36, ATTR_OPAQUE, TEX_A, 0, 0));      /* 1 coordonnées géantes */
    paire(poly(3, Po, 2, 0, ATTR_OPAQUE, TEX_A, 1, 0), poly(3, Pq, 2, 10, ATTR_OPAQUE, TEX_A, 1, 0));      /* 2 à plat */
    paire(poly(4, Qo, 3, 0, ATTR_OPAQUE, TEX_A, 0, 0), poly(4, Qq, 3, 160, ATTR_OPAQUE, TEX_A, 0, 0));     /* 3 raide (texture qui saute) */
    paire(poly(4, Ro, 4, 0, ATTR_OPAQUE, TEX_A, 0, 0), poly(4, Rq, 4, 20, ATTR_OPAQUE, TEX_A, 0, 0));      /* 4 grossit */
    paire(poly(4, Qo, 5, 0, ATTR_OPAQUE, TEX_A, 0, 0), poly(4, Qq, 5, 20, ATTR_OPAQUE, TEX_A, 0, 0));      /* 5 voisinage */
    paire(poly(4, Co, 6, 0, ATTR_OPAQUE, TEX_A, 0, 0), poly(3, Cq, 6, 0, ATTR_OPAQUE, TEX_B, 0, 0));       /* 6 changé de forme */
    paire(poly(4, Ro, 7, 0, ATTR_OPAQUE, TEX_B, 0, 0), poly(4, Qq, 7, 0, ATTR_OPAQUE, TEX_A, 0, 0));       /* 7 : A disparu, B sans pareil */
    paire(poly(3, To, 8, 0, ATTR_OPAQUE, TEX_A, 0, 0), poly(3, Tq, 8, 12, ATTR_OPAQUE, TEX_A, 0, 0));      /* 8 se retourne */
    paire(poly(4, Qo, 9, 0, ATTR_OPAQUE, TEX_B, 0, 0), poly(4, Qq, 9, 20, ATTR_OPAQUE, TEX_A, 0, 0));      /* 9 raide, deux états */
    paire(poly(4, Qo, 10, 0, ATTR_INVISIBLE, TEX_A, 0, 0), poly(4, Qq, 10, 20, ATTR_INVISIBLE, TEX_A, 0, 0));   /* 10 invisible */
    paire(poly(4, Ro, 11, 0, ATTR_TRANSLUCIDE, TEX_A, 0, 1), poly(4, Rq, 11, 20, ATTR_TRANSLUCIDE, TEX_A, 0, 1)); /* 11 translucide */
    paire(poly(4, Wo, 12, 0, ATTR_OPAQUE, TEX_A, 0, 0), poly(4, Wq, 12, 20, ATTR_OPAQUE, TEX_A, 0, 0));    /* 12 passe derrière la caméra */
    a.push_back(poly(3, Po, 13, 0, ATTR_OPAQUE, TEX_A, 2, 0));                                        /* 13 disparu, projection immobile */
    sfc::Lien lien{};
    const size_t nb = b.size(), na = a.size();
    lien.pareil = {0, 1, 2, 3, 4, 5, 6, -1, 8, 9, 10, 11, 12};
    lien.disparus = {7, 13};
    lien.par_voisinage.assign(nb, 0); lien.par_voisinage[5] = 1;
    lien.raide.assign(nb, 0); lien.raide[3] = 1; lien.raide[9] = 1;
    lien.etage.assign(nb, 0);
    lien.change.assign(nb, 0); lien.change[6] = 1;
    lien.objet_b.assign(nb, -1); lien.objet_b[6] = 0;
    lien.objet_a.assign(na, -1);
    auto translation = [](double tx, double ty) {
        std::vector<double> m(16, 0.0);
        m[0] = m[5] = m[10] = m[15] = 1.0;
        m[12] = tx; m[13] = ty;
        return m;
    };
    lien.objet_avant = {translation(0.02, 0.0)};
    lien.objet_arriere = {translation(-0.02, 0.01)};
    for (int k = 0; k < GX_PROJS_MAX; k++) {
        std::vector<double> id = translation(0, 0);
        memcpy(lien.avant[k], id.data(), sizeof lien.avant[k]);
        memcpy(lien.arriere[k], id.data(), sizeof lien.arriere[k]);
    }
    { std::vector<double> m = translation(-0.01, 0.0); memcpy(lien.avant[0], m.data(), sizeof lien.avant[0]); }
    { std::vector<double> m = translation(0.0, 0.015); memcpy(lien.arriere[0], m.data(), sizeof lien.arriere[0]); }
    lien.a_plat[1] = 1;

    auto de = nouvel_instantane(), vers = nouvel_instantane();
    de->polys = a; vers->polys = b;
    de->numero = ~0ull - 1; vers->numero = ~0ull;             /* jamais ceux d'un vrai instantané */
    static const float instants[] = {0.0f, 0.2f, 0.37f, 0.5f, 0.63f, 0.8f, 1.0f, 1.1f, 1.27f};
    std::vector<float> ref, gpu;
    std::vector<GxPolygon> out;
    GLuint tampon = 0, requete = 0;
    sfc::gfx::glGenBuffers(1, &tampon);
    g_glp.GenQueries(1, &requete);
    const bool simuler_echec = getenv("SFC_INTERPOLATION_EPREUVE") && !strcmp(getenv("SFC_INTERPOLATION_EPREUVE"), "echec");
    size_t sommets = 0, ecarts = 0;
    char premier_ecart[200] = "";
    for (int apercu = 0; apercu < 2; apercu++) {
        lien.b_apercu = apercu != 0;
        Plan plan;
        construire_plan(plan, de.get(), vers.get(), lien);
        GLint nv = 0;
        for (const LotPlan &l : plan.lots) nv = std::max(nv, (l.premier + l.nombre) * 4);
        for (float t : instants) {
            const int32_t f16 = (int32_t)(t * 65536.0f);
            /* ce que rendraient interpoler et draw_polygons */
            sfc::interpoler(a, b, lien, t, out);
            ref.clear();
            for (int passe = 0; passe < 2; passe++)
                for (const GxPolygon &p : out) {
                    if ((passe == 0) == (bool)p.translucent) continue;
                    const uint32_t alpha = (p.attr >> 16) & 31;
                    if (!alpha) continue;
                    int tw = 8, th = 8;
                    if ((p.texparam >> 26) & 7) { tw = 8 << ((p.texparam >> 20) & 7); th = 8 << ((p.texparam >> 23) & 7); }
                    auto emit = [&](const GxVertex &v) {
                        const float x[10] = {v.x / 4096.0f, v.y / 4096.0f, v.z / 4096.0f, v.w / 4096.0f,
                                             v.r / 31.0f, v.g / 31.0f, v.b / 31.0f, alpha / 31.0f,
                                             (v.s / 16.0f) / (float)tw, (v.t / 16.0f) / (float)th};
                        ref.insert(ref.end(), x, x + 10);
                    };
                    if (p.count == 3) { emit(p.v[0]); emit(p.v[2]); emit(p.v[1]); }
                    else { emit(p.v[0]); emit(p.v[2]); emit(p.v[1]); emit(p.v[0]); emit(p.v[3]); emit(p.v[2]); }
                }
            /* ce qu'en fait le GPU */
            sfc::gfx::glUseProgram(g_prog_plan);
            sfc::gfx::glBindVertexArray(plan.vao);
            sfc::gfx::glUniform1i(g_up.f16, f16);
            sfc::gfx::glUniform1f(g_up.tq, (float)f16 / 65536.0f);
            sfc::gfx::glUniform1f(g_up.tf, t);
            const size_t place = (size_t)nv * 2 * 10;          /* deux triangles de trois sommets au plus par polygone */
            sfc::gfx::glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, tampon);
            sfc::gfx::glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, (GLsizeiptr)(place * sizeof(float) + 64), nullptr, GL_STREAM_READ);
            g_glp.BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, tampon);
            glEnable(GL_RASTERIZER_DISCARD);
            g_glp.BeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, requete);
            g_glp.BeginTransformFeedback(GL_TRIANGLES);
            glDrawArrays(GL_LINES_ADJACENCY, 0, nv);
            g_glp.EndTransformFeedback();
            g_glp.EndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
            glDisable(GL_RASTERIZER_DISCARD);
            GLuint ecrits = 0;
            g_glp.GetQueryObjectuiv(requete, GL_QUERY_RESULT, &ecrits);
            gpu.assign((size_t)ecrits * 3 * 10, 0.0f);
            if (ecrits) g_glp.GetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, (GLsizeiptr)(gpu.size() * sizeof(float)), gpu.data());
            g_glp.BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
            if (simuler_echec && apercu == 1 && t > 1.2f && !gpu.empty()) gpu[0] += 1.0f;
            /* sommet par sommet */
            if (gpu.size() != ref.size()) {
                ecarts++;
                if (!premier_ecart[0])
                    snprintf(premier_ecart, sizeof premier_ecart, "à t = %.2f (B %s) : %zu sommets au lieu de %zu", t,
                             apercu ? "aperçu" : "vrai", gpu.size() / 10, ref.size() / 10);
                continue;
            }
            for (size_t i = 0; i < ref.size(); i++) {
                const int c = (int)(i % 10);
                const float e = std::fabs(gpu[i] - ref[i]);
                const float tol = c < 4 ? 2.5f / 4096.0f + 2e-7f * std::fabs(ref[i]) : c < 8 ? 1e-6f : 1e-6f + 1e-6f * std::fabs(ref[i]);
                if (!(e <= tol)) {
                    ecarts++;
                    if (!premier_ecart[0])
                        snprintf(premier_ecart, sizeof premier_ecart, "à t = %.2f (B %s), sommet %zu, composante %d : %.7g au lieu de %.7g",
                                 t, apercu ? "aperçu" : "vrai", i / 10, c, gpu[i], ref[i]);
                }
            }
            sommets += ref.size() / 10;
        }
        sfc::gfx::glDeleteVertexArrays(1, &plan.vao);
        sfc::gfx::glDeleteBuffers(1, &plan.vbo);
    }
    sfc::gfx::glDeleteBuffers(1, &tampon);
    g_glp.DeleteQueries(1, &requete);
    sfc::gfx::glBindVertexArray(0);
    if (ecarts) {
        rt_log("interpolation : le GPU ne rend pas ce que rend le processeur (%zu écarts, le premier %s) — elle reste au processeur",
               ecarts, premier_ecart);
        return false;
    }
    rt_log("interpolation : sur le GPU — épreuve de départ réussie (%zu sommets comparés, 9 instants, B vrai et aperçu)", sommets);
    return true;
}

/* Ce qu'un écran montre : les couches 2D d'un moteur, et la 3D entre les deux s'il en a une. */
struct VueEcran {
    const Engine2dOutput *o;
    const GxFrame *f3d;
    bool moteur_a;                                 /* c'est le moteur A : la capture d'affichage part de lui */
    const Instantane *recente, *ancienne;          /* d'où viennent les textures (affichage libre) */
    /* d'où viennent les couches 2D : le numéro de l'instantané fois deux, plus le moteur ; 0 si on ne le sait
       pas (la machine elle-même, dont les couches changent à chaque balayage) — elles partent alors toujours */
    uint64_t id2d = 0;
    /* la 3D interpolée sur le GPU : le plan de la paire d'images et l'instant ; f3d n'en porte que l'en-tête */
    const Plan *plan = nullptr;
    int32_t f16 = 0;
    float t = 0;
};

void dessiner_ecrans(const VueEcran vues[2]) {
    /* SFC_DIAG_OSCILLATION=1 : l'image qu'on s'apprête à montrer est-elle celle d'avant-avant, alors que
       celle d'avant était autre ? C'est un aller-retour — ce que l'œil appelle un papillotement. On échantillonne
       les couches 2D des deux écrans, dans l'ordre où elles vont sortir. */
    {   static int diag = -1; static uint64_t h1, h2, oscill, total, derniere_dite;
        if (diag < 0) diag = getenv("SFC_DIAG_OSCILLATION") != nullptr;
        {   /* la trace des dernières images montrées, et le journal quand on le demande */
            if (!g_passe_exacte && vues[0].o && vues[1].o) {
                uint64_t h[2] = {1469598103934665603ull, 1469598103934665603ull};
                size_t n3d[2] = {0, 0};
                for (int e = 0; e < 2; e++) {
                    const uint32_t *b1 = vues[e].o->below, *b2 = vues[e].o->above;
                    for (size_t k = 0; k < DS_W * (size_t)DS_H; k += 16) {
                        h[e] ^= b1[k]; h[e] *= 1099511628211ull;
                        h[e] ^= b2[k]; h[e] *= 1099511628211ull;
                    }
                    /* le gros de l'image est en 3D dans ce jeu, y compris ses menus : c'est elle qu'il faut voir —
                       interpolée sur le GPU, la paire d'images et l'instant la désignent */
                    if (vues[e].plan) {
                        n3d[e] = vues[e].plan->polys;
                        h[e] ^= vues[e].plan->de; h[e] *= 1099511628211ull;
                        h[e] ^= vues[e].plan->vers; h[e] *= 1099511628211ull;
                        h[e] ^= (uint64_t)(uint32_t)vues[e].f16; h[e] *= 1099511628211ull;
                    } else if (vues[e].f3d) {
                        const GxFrame *f3 = vues[e].f3d;
                        n3d[e] = f3->count;
                        h[e] ^= f3->count; h[e] *= 1099511628211ull;
                        for (size_t k = 0; k < f3->count; k += 4) {
                            const GxPolygon &q = f3->polys[k];
                            h[e] ^= (uint64_t)(uint32_t)q.v[0].x; h[e] *= 1099511628211ull;
                            h[e] ^= (uint64_t)(uint32_t)q.v[0].y; h[e] *= 1099511628211ull;
                            h[e] ^= (uint64_t)q.forme; h[e] *= 1099511628211ull;
                        }
                    }
                }
                /* les deux écrans montrent la même 3D : c'est le défaut, et on le dit tout de suite */
                if (h[0] == h[1] && n3d[0] > 0 && n3d[0] == n3d[1]) {
                    static uint64_t n, dite;
                    n++;
                    if (hw_frames() != dite && n3d[0] >= 20) {   /* en dessous, c'est un écran presque vide */
                        dite = hw_frames();
                        rt_log("écrans jumelés : image %llu, %zu polygones des deux côtés (%llu fois)",
                               (unsigned long long)hw_frames(), n3d[0], (unsigned long long)n);
                        g_jumeles_a_prendre = 1;
                        for (int e = 0; e < 2; e++)
                            rt_log("   écran %s : entre l'image %llu (tour %.1f, haut=%c%s) et l'image %llu (tour %.1f, haut=%c%s), a=%.2f, file de %zu",
                                   e == 0 ? "du haut" : "du bas",
                                   (unsigned long long)g_src3d[e].de, g_src3d[e].tour_de,
                                   g_src3d[e].atop_de ? 'A' : 'B', g_src3d[e].apercu_de ? ", aperçu" : "",
                                   (unsigned long long)g_src3d[e].vers, g_src3d[e].tour_vers,
                                   g_src3d[e].atop_vers ? 'A' : 'B', g_src3d[e].apercu_vers ? ", aperçu" : "",
                                   g_src3d[e].a, g_src3d[e].file);
                    }
                }
                TraceImage &t = g_trace[g_trace_n % 120];
                t.n3d[0] = n3d[0]; t.n3d[1] = n3d[1];
                t.image = g_montre_image ? g_montre_image : hw_frames();
                t.tour = g_montre_tour;
                t.apercu = g_montre_apercu;
                t.a_top = vues[0].moteur_a ? 1 : 0;
                t.h0 = h[0]; t.h1 = h[1];
                g_trace_n++;
            }
            if (g_journal_demande.exchange(false)) {
                rt_log("journal des images (touche P) : les %d dernières, la plus récente en dernier",
                       (int)std::min<size_t>(g_trace_n, 120));
                size_t n = std::min<size_t>(g_trace_n, 120);
                for (size_t k = 0; k < n; k++) {
                    const TraceImage &t = g_trace[(g_trace_n - n + k) % 120];
                    rt_log("   image %llu tour %.2f %s, haut = moteur %c, empreintes %04llX / %04llX, 3D %zu / %zu polygones%s",
                           (unsigned long long)t.image, t.tour, t.apercu ? "d'aperçu " : "du balayage",
                           t.a_top ? 'A' : 'B', (unsigned long long)(t.h0 & 0xFFFF), (unsigned long long)(t.h1 & 0xFFFF),
                           t.n3d[0], t.n3d[1],
                           t.h0 == t.h1 ? "  <-- les deux écrans montrent la même chose" : "");
                }
            }
        }
        /* SFC_DIAG_ECRANS=1 : les deux écrans montrent-ils la même chose ? C'est ce que les captures du
           joueur donnent à voir — deux fois le même écran, le temps d'un éclair. */
        {   static int d2 = -1; static uint64_t doubles, vus;
            if (d2 < 0) d2 = getenv("SFC_DIAG_ECRANS") != nullptr;
            if (d2 && !g_passe_exacte && vues[0].o && vues[1].o) {
                const Engine2dOutput *a0 = vues[0].o, *a1 = vues[1].o;
                uint64_t h[2] = {1469598103934665603ull, 1469598103934665603ull};
                size_t varie = 0;
                const uint32_t *p0 = a0->below, *p1 = a1->below;
                uint32_t premier = p0[0];
                for (size_t k = 0; k < DS_W * (size_t)DS_H; k += 16) {
                    h[0] ^= p0[k]; h[0] *= 1099511628211ull;
                    h[1] ^= p1[k]; h[1] *= 1099511628211ull;
                    varie += p0[k] != premier;
                }
                vus++;
                if (h[0] == h[1] && varie > 64) {   /* même image, et elle a de quoi être reconnue */
                    doubles++;
                    static uint64_t dite;
                    if (hw_frames() != dite) {
                        dite = hw_frames();
                        rt_log("écrans identiques : image du jeu %llu, source %s (%llu fois sur %llu)",
                               (unsigned long long)hw_frames(),
                               g_source && g_source->apercu ? "un aperçu" : "un balayage",
                               (unsigned long long)doubles, (unsigned long long)vus);
                    }
                }
            }
        }
        if (diag && !g_passe_exacte) {   /* la passe de capture est dessinée sans être montrée : elle ne compte pas */
            uint64_t h = 1469598103934665603ull;
            for (int s = 0; s < 2; s++) {
                const Engine2dOutput *o = vues[s].o;
                if (!o) continue;
                const uint8_t *b1 = (const uint8_t *)o->below, *b2 = (const uint8_t *)o->above;
                for (size_t k = 0; k < sizeof o->below; k += 64) { h ^= b1[k]; h *= 1099511628211ull; }
                for (size_t k = 0; k < sizeof o->above; k += 64) { h ^= b2[k]; h *= 1099511628211ull; }
            }
            total++;
            {   /* le motif, sur quelques images : ce qui sort vraiment, image après image */
                static long long depuis = -2; static int dites;
                if (depuis == -2) { const char *e = getenv("SFC_DIAG_OSCILLATION_SUITE"); depuis = e ? atoll(e) : -1; }
                if (depuis >= 0 && (long long)hw_frames() >= depuis && dites++ < 40)
                    rt_log("   suite : image du jeu %llu, 2D %04llX, a_top %d",
                           (unsigned long long)hw_frames(), (unsigned long long)(h & 0xFFFF),
                           vues[0].moteur_a ? 1 : 0);
            }
            if (h == h2 && h != h1) {
                oscill++;
                if (hw_frames() != derniere_dite) {
                    derniere_dite = hw_frames();
                    rt_log("papillotement : image du jeu %llu — l'affichage revient à l'image d'avant-avant (%llu fois sur %llu)",
                           (unsigned long long)hw_frames(), (unsigned long long)oscill, (unsigned long long)total);
                }
            }
            h2 = h1; h1 = h;
        }
    }
    const int win_w = g_window->width(), win_h = g_window->height();
    int scale = render_scale();
    /* Le rendu se fait dans le tampon hors écran quand on suréchantillonne, et le reste du code
       travaille à cette taille : les deux moitiés d'écran et la capture suivent d'elles-mêmes. */
    bool off = scale > 1 && offscreen_bind(win_w * scale, win_h * scale);
    if (!off) scale = 1;
    int ww = win_w * scale, wh = win_h * scale;
    glViewport(0, 0, ww, wh);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glDepthMask(GL_TRUE);   /* glClear respecte le masque de profondeur : la passe translucide le laissait à faux */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    /* une nouvelle image : les textures retenues pour la précédente ne valent plus (voir g_frame_textures) */
    if (++g_image_textures, g_frame_textures.size() > 4096) g_frame_textures.clear();
    /* ce que contient chaque texture de couche (VueEcran::id2d, puis la couche) : une couche inchangée d'une
       image affichée à la suivante — la 2D n'avance qu'avec le jeu — ne repart pas au GPU */
    static uint64_t couche_chargee[4];
    for (int screen = 0; screen < 2; screen++) {
        const VueEcran &v = vues[screen];
        const Engine2dOutput &o = *v.o;
        int vy = screen == 0 ? wh / 2 : 0, vh = wh / 2;
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, vy, ww, vh);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        /* diagnostic : SFC_NO_2D=1 n'affiche que la 3D, SFC_NO_ABOVE=1 omet la couche au-dessus de la 3D */
        static int no_2d = -1, no_above = -1;
        if (no_2d < 0) { no_2d = getenv("SFC_NO_2D") != nullptr; no_above = getenv("SFC_NO_ABOVE") != nullptr; }
        const uint64_t id_dessous = v.id2d ? v.id2d * 2 : 0, id_dessus = v.id2d ? v.id2d * 2 + 1 : 0;
        if (!no_2d) {
            draw_layer(g_screen_tex[screen * 2], o.below, 0, vy, ww, vh, !id_dessous || couche_chargee[screen * 2] != id_dessous);
            couche_chargee[screen * 2] = id_dessous;
        }
        if (o.has_3d && v.f3d) {
            if (profiling()) { g_t_3d.start(); g_prof_polys += v.plan ? v.plan->polys : v.f3d->count; }
            if (v.recente) { g_source = g_src_recente = v.recente; g_src_ancienne = v.ancienne; }
            draw_clear_color(v.f3d, 0, vy, ww, vh);
            if (v.plan) draw_plan(*v.plan, v.f3d, v.f16, v.t, 0, vy, ww, vh);
            else draw_polygons(v.f3d, 0, vy, ww, vh);
            if (profiling()) g_t_3d.stop();
        }
        if (!no_2d && !no_above) {
            draw_layer(g_screen_tex[screen * 2 + 1], o.above, 0, vy, ww, vh, !id_dessus || couche_chargee[screen * 2 + 1] != id_dessus);
            couche_chargee[screen * 2 + 1] = id_dessus;
        }
        if (v.moteur_a) display_capture(vy, ww, vh);
    }
    glDisable(GL_SCISSOR_TEST);
    if (off) {
        /* Réduction : c'est ici que les N×N points calculés deviennent un pixel affiché. */
        sfc::gfx::glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        draw_offscreen(g_off.color, win_w, win_h);
    }
    maybe_dump(win_w, win_h);
}

/* L'image telle que la machine la montre : le moteur A sur un écran, le B sur l'autre. */
void dessiner(const Engine2dOutput *out2d, int a_top, const GxFrame *f3d) {
    VueEcran vues[2];
    for (int screen = 0; screen < 2; screen++) {
        int engine = (screen == 0) == (a_top != 0) ? 0 : 1;
        vues[screen] = VueEcran{&out2d[engine], engine == 0 ? f3d : nullptr, engine == 0, nullptr, nullptr};
    }
    dessiner_ecrans(vues);
}

// ------------------------------------------------------------------ l'affichage libre : son propre fil
/* SFC_AFFICHAGE=libre. Le jeu avance par tour, pas par le temps (docs/plan_framerate.md) : on ne peut
 * pas le faire penser plus souvent sans l'accélérer. On découple donc l'affichage : à chaque balayage
 * vertical le fil du jeu dépose un instantané de ce qu'il y a à montrer, et repart ; le fil
 * d'affichage — qui possède la fenêtre et OpenGL — dessine à son rythme ce qu'il a de plus récent.
 * Une machine lente affiche moins d'images sans ralentir le jeu.
 *
 * Étape 1 : l'image est celle du dernier instantané, telle quelle. Reste un point de rendez-vous :
 * quand le jeu demande une capture d'affichage (la 3D des deux écrans, en vol), il attend que le fil
 * d'affichage l'ait faite — la capture retourne dans la mémoire vidéo, et le jeu la montre à
 * l'image suivante. */
bool affichage_libre() {
    static int v = -1;
    if (v < 0) { const char *e = getenv("SFC_AFFICHAGE"); v = e && !strcmp(e, "libre"); }
    return v != 0;
}

std::mutex g_verrou;
std::condition_variable g_nouvel_instantane, g_image_faite;
std::deque<std::shared_ptr<Instantane>> g_deposes;   /* ce que le jeu a déposé et que l'affichage n'a pas encore pris */
uint64_t g_image_dessinee;                     /* la dernière image que le fil d'affichage a finie */
std::atomic<bool> g_fermeture{false};
/* la manette, telle que le fil d'affichage l'a lue, pour le fil du jeu */
std::mutex g_mx_manette;
Manette g_in_manette;
/* video_arreter (plus bas) : le fil d'affichage s'arrête avant que le fil du jeu ne quitte */
std::atomic<bool> g_arret_demande{false}, g_fil_affichage_vivant{false};
std::thread::id g_id_fil_affichage;
std::atomic<uint32_t> g_in_held{0};
std::atomic<uint32_t> g_in_apercu{0};          /* les touches du clavier et de la manette, pour les tours d'aperçu */
std::atomic<int> g_in_tdown{0}, g_in_tx{0}, g_in_ty{0}, g_in_mx{0}, g_in_my{0}, g_in_bouton{0}, g_in_ww{512}, g_in_wh{768};
std::atomic<uint64_t> g_images_affichees{0};

/* ---- l'heure du jeu
 *
 * Le jeu avance d'un tour par image 3D — son numéro de série — et chaque tour vaut le même pas de
 * simulation. Mais ses images n'arrivent pas régulièrement : le tour est long, le balayage qui suit
 * est en retard et le suivant le rattrape ; une scène chargée présente ses images un balayage sur
 * deux, puis sur trois, puis sur un. Interpoler d'après l'heure d'arrivée, c'est rejouer cette gigue :
 * le mouvement avance par à-coups, et à 240 images par seconde un à-coup se voit comme un double.
 *
 * On compte donc le temps en tours. La durée d'un tour se mesure sur une fenêtre d'arrivées ; l'heure
 * idéale du dernier tour est l'enveloppe des arrivées — la plus tardive, ramenée à ce tour — de sorte
 * qu'une image est toujours là quand son heure sonne. */
struct HorlogeJeu {
    struct Releve { uint32_t serie; uint64_t image; double arrivee; };
    std::deque<Releve> releves;         /* une par image 3D nouvelle */
    std::deque<double> balayages;       /* l'arrivée de chaque instantané : un par balayage vertical */
    double T = 0;                       /* secondes par tour */
    double t_ref = 0;                   /* l'heure idéale du tour serie_ref */
    uint32_t serie_ref = 0;
    bool calee = false;
    int hors_rythme = 0;                /* relevés de suite qui ne sont plus au rythme mesuré */
    void oublier() { releves.clear(); calee = false; T = 0; hors_rythme = 0; }
    void balayage(double arrivee) {
        balayages.push_back(arrivee);
        if (balayages.size() > 96) balayages.pop_front();
    }
    /* La durée d'un tour : tant de balayages, de telle durée. Les deux se mesurent à part, et chacun sans ses
       valeurs aberrantes — mesurer le tour d'un bout à l'autre de la fenêtre, c'était y compter le moindre
       trou (un chargement de soixante millisecondes), allonger le tour d'un dixième, et laisser l'heure
       dériver d'autant à chaque image. */
    double mesurer() const {
        double somme = 0; int n = 0;
        for (size_t k = 1; k < balayages.size(); k++) {
            double d = balayages[k] - balayages[k - 1];
            if (d > 0 && d < 0.045) { somme += d; n++; }               /* le rattrapage d'un balayage en retard compte, un trou non */
        }
        if (n < 8) return 0;
        double balayage_s = somme / n;
        double images = 0, series = 0;
        for (size_t k = 1; k < releves.size(); k++) {
            double di = (double)(releves[k].image - releves[k - 1].image), ds = (double)(releves[k].serie - releves[k - 1].serie);
            if (di <= 4.0 * ds) { images += di; series += ds; }        /* le jeu ne descend pas sous quinze tours par seconde */
        }
        return series > 0 ? balayage_s * images / series : 0;
    }
    const char *raison_oubli = nullptr;   /* pour le diagnostic : pourquoi l'heure a été oubliée la dernière fois */
    double dernier_pas = 0, T_avant = 0;
    void noter(uint32_t serie, uint64_t image, double arrivee) {
        raison_oubli = nullptr;
        if (!releves.empty() && serie <= releves.back().serie) { oublier(); raison_oubli = "un tour qui n'avance pas"; }
        else if (!releves.empty() && arrivee - releves.back().arrivee > 0.15) { oublier(); raison_oubli = "un trou de plus de 150 ms"; }
        /* Le rythme a changé — du menu au vol le tour passe de 17 à 33 ms, sans une pause entre les deux.
           Ramener les anciens relevés au tour courant avec la nouvelle durée les projetait une demi-seconde
           dans le futur : l'heure prenait quinze tours de retard, l'affichage restait collé à la plus vieille
           image qu'il gardait et n'en montrait plus que trente par seconde, le temps que cela se résorbe. */
        if (T > 0 && !releves.empty()) {
            double pas = (arrivee - releves.back().arrivee) / (double)(serie - releves.back().serie);
            dernier_pas = pas; T_avant = T;
            hors_rythme = (pas > 1.4 * T || pas < 0.6 * T) ? hors_rythme + 1 : 0;
            if (hors_rythme >= 3) {
                Releve avant = releves.back();
                oublier();
                releves.push_back(avant);
                raison_oubli = "trois relevés de suite hors du rythme";
            }
        }
        releves.push_back(Releve{serie, image, arrivee});
        if (releves.size() > 24) releves.pop_front();
        if (releves.size() < 4) return;
        double mesure = mesurer();
        if (mesure <= 0) return;
        T = T > 0 ? T + 0.2 * (mesure - T) : mesure;
        double cible = -1e300;
        for (const Releve &r : releves) cible = std::max(cible, r.arrivee + (double)(serie - r.serie) * T);
        /* l'image la plus tardive n'a jamais plus d'un tour de retard : au-delà, c'est la mesure qui se trompe */
        cible = std::min(cible, arrivee + 0.75 * T) + 0.002;
        if (!calee) { t_ref = cible; calee = true; }
        else {
            /* elle avance d'un tour ; en retard, on la recale aussitôt ; en avance, on la laisse revenir
               doucement — sauf d'un tour entier, qu'on rend d'un coup */
            double predite = t_ref + (double)(serie - serie_ref) * T;
            t_ref = cible > predite || predite - cible > T ? cible : predite - 0.25 * (predite - cible);
        }
        serie_ref = serie;
    }
    double lire(double maintenant) const { return (double)serie_ref + (maintenant - t_ref) / T; }
};

/* SFC_IPS : le plafond d'images affichées par seconde (240 par défaut ; « vsync » : celui de l'écran). */
int plafond_ips() {
    static int v = -2;
    if (v == -2) { const char *e = getenv("SFC_IPS"); v = !e ? 240 : !strcmp(e, "vsync") ? -1 : atoi(e); if (v == 0) v = 100000; }
    return v;
}

typedef std::shared_ptr<Instantane> Image;

/* La 3D d'un écran entre deux images du jeu : `a` de 0 (de) à 1 (vers). */
void vue_entre(VueEcran &vue, GxFrame &f, std::vector<GxPolygon> &melange, const Instantane *de, const Instantane *vers,
               const sfc::Lien &lien, float a) {
    if (lien.coupure) {                                            /* deux scènes : l'une, puis l'autre */
        const Instantane *une = a < 0.5f ? de : vers;
        f = une->frame; f.polys = const_cast<GxPolygon *>(une->polys.data()); f.count = une->polys.size();
        vue.recente = une; vue.ancienne = nullptr;
    } else if (interpolation_gpu()) {                            /* le GPU refait interpoler à cet instant */
        vue.plan = plan_pour(de, vers, lien);
        vue.f16 = (int32_t)(a * 65536.0f);
        vue.t = a;
        f = vers->frame; f.polys = nullptr; f.count = 0;
        vue.recente = vers; vue.ancienne = de;
    } else {
        sfc::interpoler(de->polys, vers->polys, lien, a, melange);
        f = vers->frame; f.polys = melange.data(); f.count = melange.size();
        vue.recente = vers; vue.ancienne = de;
    }
    vue.f3d = &f;
}

/* SFC_AFFICHAGE_ESSAI=n : l'épreuve de l'interpolation, hors du temps. À partir de l'image SFC_DUMP_FROM,
   neuf images 3D consécutives d'un écran ; entre chacune et la suivante, n sous-images à pas égaux. On
   mesure de combien l'image change d'une sous-image à l'autre : un mouvement continu change d'autant à
   chaque pas ; un polygone qui saute à mi-chemin, ou aux images du jeu, fait un pic. SFC_AFFICHAGE_ESSAI_PNG=1
   les enregistre (build/essai_kk_jj.png). */
struct Essai {
    int n = 0, ecran = -1;
    uint64_t depuis = 0;
    std::vector<Image> cles;
};

/* SFC_AFFICHAGE_ESSAI_PREDICTION=1 : ce que coûte la prédiction. Pour chaque image k et chaque fraction x
   d'un tour : l'image prédite depuis (k-1, k) prolongée de x, contre celle que l'interpolation montrerait entre
   k et k+1 à la même fraction — la vérité, un tour plus tard. Et, pour comparer, l'image k tenue telle quelle. */
void essai_prediction(Essai &es) {
    const int ww = g_window->width(), wh = g_window->height();
    const int n = es.n, ec = es.ecran;
    std::vector<uint8_t> vrai((size_t)ww * (wh / 2) * 4), predit(vrai.size()), tenu(vrai.size());
    std::vector<GxPolygon> melange;
    auto rendre = [&](const Instantane *de, const Instantane *vers, const sfc::Lien &lien, float a, std::vector<uint8_t> &px) {
        VueEcran vues[2];
        GxFrame f;
        for (int screen = 0; screen < 2; screen++) {
            int engine = (screen == 0) == (de->a_top != 0) ? 0 : 1;
            vues[screen] = VueEcran{&de->out2d[engine], nullptr, false, de, nullptr};
        }
        vues[ec].o = &de->out2d[0];
        vue_entre(vues[ec], f, melange, de, vers, lien, a);
        g_source = de; g_src_recente = g_src_ancienne = nullptr; g_image_montree = de->image;
        dessiner_ecrans(vues);
        g_source = g_src_recente = g_src_ancienne = nullptr;
        glReadPixels(0, ec == 0 ? wh / 2 : 0, ww, wh / 2, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        g_window->swapBuffers();
    };
    auto ecart = [](const std::vector<uint8_t> &x, const std::vector<uint8_t> &y) {
        uint64_t d = 0;
        for (size_t i = 0; i < x.size(); i++) d += (uint64_t)std::abs((int)x[i] - (int)y[i]);
        return (double)d / (double)x.size();
    };
    double s_predit = 0, s_tenu = 0;
    size_t nb = 0;
    for (size_t k = 1; k + 1 < es.cles.size(); k++) {
        const Instantane *a0 = es.cles[k - 1].get(), *a1 = es.cles[k].get(), *a2 = es.cles[k + 1].get();
        sfc::Lien l01 = sfc::apparier(a0->frame, a0->polys, a1->frame, a1->polys);
        sfc::Lien l12 = sfc::apparier(a1->frame, a1->polys, a2->frame, a2->polys);
        double pas = (double)(a2->frame.serial - a1->frame.serial) / (double)(a1->frame.serial - a0->frame.serial);
        rendre(a1, a2, l12, 0.0f, tenu);
        std::string ligne;
        for (int j = 1; j <= n; j++) {
            float x = (float)j / (float)n;
            rendre(a1, a2, l12, x, vrai);
            static float gain = getenv("SFC_PRED_GAIN") ? (float)atof(getenv("SFC_PRED_GAIN")) : 1.0f;
            rendre(a0, a1, l01, 1.0f + gain * x * (float)pas, predit);
            double ep = ecart(predit, vrai), et = ecart(tenu, vrai);
            if (getenv("SFC_AFFICHAGE_ESSAI_PNG") && j == n) {
                char nom[64];
                snprintf(nom, sizeof nom, "build/pred_%02zu_predite.png", k); write_png(nom, ww, wh / 2, predit.data());
                snprintf(nom, sizeof nom, "build/pred_%02zu_vraie.png", k); write_png(nom, ww, wh / 2, vrai.data());
                snprintf(nom, sizeof nom, "build/pred_%02zu_tenue.png", k); write_png(nom, ww, wh / 2, tenu.data());
            }
            s_predit += ep; s_tenu += et; nb++;
            char t[40];
            snprintf(t, sizeof t, " %.2f/%.2f", ep, et);
            ligne += t;
        }
        rt_log("prédiction : images %llu -> %llu -> %llu%s ; écart prédite/tenue à chaque fraction :%s",
               (unsigned long long)a0->image, (unsigned long long)a1->image, (unsigned long long)a2->image,
               l01.coupure || l12.coupure ? " (COUPURE)" : "", ligne.c_str());
    }
    rt_log("prédiction : écart moyen à la vérité — prédite %.3f, image tenue %.3f", nb ? s_predit / nb : 0.0, nb ? s_tenu / nb : 0.0);
}

void essai_executer(Essai &es) {
    if (getenv("SFC_AFFICHAGE_ESSAI_PREDICTION")) { essai_prediction(es); return; }
    const int ww = g_window->width(), wh = g_window->height();
    const int n = es.n, ec = es.ecran;
    std::vector<uint8_t> px((size_t)ww * (wh / 2) * 4), avant;
    std::vector<GxPolygon> melange;
    bool png = getenv("SFC_AFFICHAGE_ESSAI_PNG") != nullptr;
    double somme_milieu = 0, somme_bord = 0, somme_reste = 0;
    size_t n_milieu = 0, n_bord = 0, n_reste = 0;
    for (size_t k = 0; k + 1 < es.cles.size(); k++) {
        const Instantane *de = es.cles[k].get(), *vers = es.cles[k + 1].get();
        sfc::Lien lien = sfc::apparier(de->frame, de->polys, vers->frame, vers->polys);
        std::string ligne;
        for (int j = 0; j < n; j++) {
            float a = (float)j / (float)n;
            VueEcran vues[2];
            GxFrame f;
            for (int screen = 0; screen < 2; screen++) {
                int engine = (screen == 0) == (de->a_top != 0) ? 0 : 1;
                vues[screen] = VueEcran{&de->out2d[engine], nullptr, false, de, nullptr};
            }
            vues[ec].o = &de->out2d[0];
            vue_entre(vues[ec], f, melange, de, vers, lien, a);
            g_source = de; g_src_recente = g_src_ancienne = nullptr; g_image_montree = de->image;
            dessiner_ecrans(vues);
            g_source = g_src_recente = g_src_ancienne = nullptr;
            glReadPixels(0, ec == 0 ? wh / 2 : 0, ww, wh / 2, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            if (png) {
                char nom[64];
                snprintf(nom, sizeof nom, "build/essai_%02zu_%02d.png", k, j);
                write_png(nom, ww, wh / 2, px.data());
            }
            g_window->swapBuffers();
            if (!avant.empty()) {
                uint64_t d = 0;
                for (size_t i = 0; i < px.size(); i++) d += (uint64_t)std::abs((int)px[i] - (int)avant[i]);
                double m = (double)d / (double)px.size();
                /* le pas j-1 -> j : j = 0 franchit une image du jeu, 2j = n franchit le mi-chemin */
                if (j == 0) { somme_bord += m; n_bord++; }
                else if (2 * j == n) { somme_milieu += m; n_milieu++; }
                else { somme_reste += m; n_reste++; }
                char t[32];
                snprintf(t, sizeof t, " %.2f%s", m, j == 0 ? "|" : 2 * j == n ? "*" : "");
                ligne += t;
            }
            avant = px;
        }
        rt_log("essai : images %llu -> %llu, %zu -> %zu polygones, %zu appariés dont %zu par voisinage, %zu disparus%s ; caméra : %zu sommets sûrs, %zu retenus, résidu %.5f ; pas :%s",
               (unsigned long long)de->image, (unsigned long long)vers->image, de->polys.size(), vers->polys.size(),
               lien.apparies, lien.seconds, lien.disparus.size(), lien.coupure ? ", COUPURE" : "",
               lien.sures, lien.retenues, lien.residu, ligne.c_str());
    }
    rt_log("essai : changement moyen par pas — courant %.3f, au mi-chemin %.3f, aux images du jeu %.3f",
           n_reste ? somme_reste / n_reste : 0.0, n_milieu ? somme_milieu / n_milieu : 0.0, n_bord ? somme_bord / n_bord : 0.0);
}

void fil_affichage(std::mutex *pret_m, std::condition_variable *pret_cv, bool *pret) {
    /* vivant jusqu'à la dernière instruction : l'objet est détruit après tous les autres de ce fil */
    struct Vivant {
        Vivant() { g_id_fil_affichage = std::this_thread::get_id(); g_fil_affichage_vivant = true; }
        ~Vivant() { g_fil_affichage_vivant = false; }
    } vivant;
    using horloge = std::chrono::steady_clock;
    using std::chrono::milliseconds;
    echantillonneur_fil_affichage();
    rt_fil_nommer("affichage");
    bool ok = video_init_ici();
    if (ok && plafond_ips() < 0) g_window->setVSync(true);
    { std::lock_guard<std::mutex> l(*pret_m); *pret = true; }
    pret_cv->notify_all();
    if (!ok) return;
    static int sans_interpolation = getenv("SFC_AFFICHAGE_BRUT") != nullptr;   /* l'étape 1 : l'image du jeu telle quelle */
    static const char *diag = getenv("SFC_AFFICHAGE_DIAG");
    const auto epoque = horloge::now();
    auto secondes = [&](horloge::time_point t) { return std::chrono::duration<double>(t - epoque).count(); };
    std::vector<Image> recents;                       /* les derniers instantanés, du plus ancien au plus récent */
    std::vector<Image> cles[2];                       /* par écran : les dernières images 3D distinctes */
    std::vector<sfc::Lien> liens[2];                  /* liens[k] : de cles[k] à cles[k+1] */
    std::vector<GxPolygon> melange[2];
    HorlogeJeu heure;
    uint32_t serie_vue = 0;
    double u_montre = 0;                              /* le tour qu'on montre : il ne recule jamais */
    bool etait_calee = false;
    uint64_t signature_affichee = ~0ull;
    auto prochaine = horloge::now();
    uint64_t n_apparies = 0, n_seconds = 0, n_polys = 0, n_images = 0, n_coupures = 0, n_gels = 0, n_liens = 0;
    uint64_t n_recalages = 0, n_retards = 0, n_raides = 0, n_realignes = 0;
    double latence = 1.0, maintenant_prec = 0;        /* voir plus bas : la latence suit la scène */
    bool scene_d_action = false;
    double residu_max = 0, somme_retard = 0;
    auto t_stat = horloge::now();
    /* SFC_AFFICHAGE_FILM=n : les n images affichées à partir de l'image du jeu SFC_DUMP_FROM, toutes, telles
       que l'écran les reçoit et à leur rythme — avec, pour chacune, l'heure et le tour du jeu qu'elle montre
       (build/film_nnnn.png, build/film.txt). C'est ce que l'œil voit et qu'une capture isolée ne montre pas. */
    struct Vue { double t, u, a; uint64_t image, de, vers; int vivante; };
    size_t film_n = 0;
    uint64_t film_depuis = 0;
    std::vector<std::vector<uint8_t>> film;
    std::vector<Vue> film_vues;
    if (const char *e = getenv("SFC_AFFICHAGE_FILM")) {
        film_n = (size_t)atoi(e);
        const char *d = getenv("SFC_DUMP_FROM");
        film_depuis = d ? (uint64_t)atoll(d) : 0;
    }
    Essai essai;
    if (const char *e = getenv("SFC_AFFICHAGE_ESSAI")) {
        essai.n = atoi(e);
        const char *d = getenv("SFC_DUMP_FROM");
        essai.depuis = d ? (uint64_t)atoll(d) : 0;
    }
    /* SFC_AFFICHAGE_DIAG=4 : où passe le temps d'un tour de ce fil, quand il dépasse six millisecondes */
    const bool chrono = diag && diag[0] == '4';
    horloge::time_point c0, c1, c2, c3, c4, c5;
    for (;;) {
        if (g_arret_demande) return;
        if (chrono) {
            auto c6 = horloge::now();
            auto ms = [](horloge::time_point a, horloge::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
            if (c0.time_since_epoch().count() && ms(c0, c6) > 6.0)
                rt_log("tour d'affichage de %.1f ms : fenêtre et file %.1f, appariement %.1f, préparation %.1f, dessin %.1f, échange %.1f, attente %.1f",
                       ms(c0, c6), ms(c0, c1), ms(c1, c2), ms(c2, c3), ms(c3, c4), ms(c4, c5), ms(c5, c6));
            c0 = c6;
        }
        if (!g_window->pumpEvents()) { g_fermeture = true; return; }
        Entrees e = lire_entrees();
        g_in_held = e.held; g_in_tdown = e.tdown; g_in_tx = e.tx; g_in_ty = e.ty;
        g_in_apercu = e.held | manette_touches(&e.manette);
        { std::lock_guard<std::mutex> l(g_mx_manette); g_in_manette = e.manette; }
        g_in_mx = e.mx; g_in_my = e.my; g_in_bouton = e.bouton; g_in_ww = e.ww; g_in_wh = e.wh;
        std::vector<Image> arrives;
        {
            std::unique_lock<std::mutex> l(g_verrou);
            if (g_deposes.empty() && recents.empty()) g_nouvel_instantane.wait_for(l, milliseconds(4));
            arrives.assign(g_deposes.begin(), g_deposes.end());
            g_deposes.clear();
        }
        Image a_capturer;
        if (chrono) c1 = horloge::now();
        for (Image &neuf : arrives) {
            if (diag && diag[0] == '3') {
                static horloge::time_point avant;
                rt_log("instantané : image %llu, +%.1f ms, série %u, A %s, 3D %d/%d, %zu polygones, capture %08X",
                       (unsigned long long)neuf->image, std::chrono::duration<double, std::milli>(neuf->quand - avant).count(),
                       neuf->frame.serial, neuf->a_top ? "en haut" : "en bas", (int)neuf->a_3d, (int)neuf->out2d[0].has_3d,
                       neuf->polys.size(), neuf->capture);
                avant = neuf->quand;
            }
            if (!neuf->precoce) { heure.balayage(secondes(neuf->quand)); sfc::noter_presence(neuf->polys); }
            recents.push_back(neuf);
            if (recents.size() > 16) recents.erase(recents.begin());
            /* une nouvelle image 3D pour l'écran où le moteur A la montre : elle s'ajoute à sa file */
            int ec = neuf->a_top ? 0 : 1;
            /* SFC_DIAG_FILES=de:a : chaque image reçue entre ces images du jeu, et ce qu'on en fait */
            static long files_de = -2, files_a = 0;
            if (files_de == -2) {
                const char *e = getenv("SFC_DIAG_FILES");
                files_de = e ? atol(e) : -1;
                files_a = e && strchr(e, ':') ? atol(strchr(e, ':') + 1) : files_de + 30;
            }
            const bool dire_files = files_de >= 0 && (long)neuf->image >= files_de && (long)neuf->image <= files_a;
            if (dire_files)
                rt_log("files : reçue image %llu tour %.3f écran %s%s%s, 3D %d/%d ; dernières clés : haut %.3f%s, bas %.3f%s",
                       (unsigned long long)neuf->image, neuf->tour, neuf->a_top ? "haut" : "bas", neuf->apercu ? " (aperçu)" : "",
                       neuf->precoce ? " (précoce)" : "", (int)neuf->a_3d, (int)neuf->out2d[0].has_3d,
                       cles[0].empty() ? -1.0 : cles[0].back()->tour, !cles[0].empty() && cles[0].back()->apercu ? " aperçu" : "",
                       cles[1].empty() ? -1.0 : cles[1].back()->tour, !cles[1].empty() && cles[1].back()->apercu ? " aperçu" : "");
            /* une image plus récente que la dernière de cet écran — publiée d'avance, sa présentation n'est plus
               qu'un doublon ; et une plus ancienne n'a rien à faire après elle */
            if (neuf->apercu && diag && diag[0] == '6') {
                static int n; if (n++ < 30) rt_log("aperçu reçu : tour %.3f, écran %d, 3D %d/%d, dernière clé %.3f", neuf->tour, ec, (int)neuf->a_3d, (int)neuf->out2d[0].has_3d, cles[ec].empty() ? -1.0 : cles[ec].back()->tour);
            }
            /* Le tour à venir, calculé d'avance : il remplace l'aperçu précédent du même tour, et la vraie
               image du jeu le remplace à son tour quand elle arrive. */
            if (neuf->a_3d && neuf->out2d[0].has_3d && !cles[ec].empty() && neuf->tour == cles[ec].back()->tour
                && cles[ec].back()->apercu) {
                cles[ec].back() = neuf;
                if (cles[ec].size() >= 2) {
                    if (!liens[ec].empty()) liens[ec].pop_back();     /* le lien qui menait à l'aperçu remplacé */
                    const Instantane *avant = cles[ec][cles[ec].size() - 2].get();
                    liens[ec].push_back(sfc::apparier(avant->frame, avant->polys, neuf->frame, neuf->polys));
                    liens[ec].back().b_apercu = neuf->apercu;
                }
            } else if (neuf->a_3d && neuf->out2d[0].has_3d && (cles[ec].empty() || neuf->tour > cles[ec].back()->tour)
                       && !(!cles[1 - ec].empty() && cles[1 - ec].back()->tour == neuf->tour)) {
                /* Le moteur 3D est unique : une image appartient à l'écran où il sort. Quand le jeu n'en a pas
                   fini de neuve au tour suivant, le balayage en reporte une déjà vue — rangée dans l'autre file,
                   elle y ferait montrer aux deux écrans la même chose. On ne la range qu'une fois. */
                if (!cles[ec].empty()) {
                    liens[ec].push_back(sfc::apparier(cles[ec].back()->frame, cles[ec].back()->polys, neuf->frame, neuf->polys));
                    liens[ec].back().b_apercu = neuf->apercu;
                    const sfc::Lien &l = liens[ec].back();
                    n_apparies += l.apparies; n_seconds += l.seconds; n_polys += neuf->polys.size();
                    n_raides += l.raides; n_realignes += l.realignes;
                    n_coupures += l.coupure ? 1 : 0; n_liens++;
                    if (l.residu > residu_max) residu_max = l.residu;
                }
                cles[ec].push_back(neuf);
                if (cles[ec].size() > 6) { cles[ec].erase(cles[ec].begin()); liens[ec].erase(liens[ec].begin()); }
                if (essai.n > 0 && neuf->image >= essai.depuis && (essai.ecran < 0 || essai.ecran == ec)) {
                    essai.ecran = ec;
                    essai.cles.push_back(neuf);
                    if (essai.cles.size() == 9) { essai_executer(essai); g_fermeture = true; return; }
                }
            }
            if (neuf->a_3d && !neuf->apercu && neuf->frame.serial > serie_vue) {
                serie_vue = neuf->frame.serial;
                heure.noter(serie_vue, neuf->image, secondes(neuf->quand));
                if (dire_files && heure.raison_oubli)
                    rt_log("files : l'heure est oubliée à l'image %llu (série %u%s) : %s ; T %.2f ms",
                           (unsigned long long)neuf->image, serie_vue, neuf->precoce ? ", publiée d'avance" : "", heure.raison_oubli, heure.T * 1e3);
                if (dire_files)
                    rt_log("files : relevé série %u image %llu%s — pas %.1f ms, tour attendu %.1f ms, hors rythme %d, %zu relevés, calée %d",
                           serie_vue, (unsigned long long)neuf->image, neuf->precoce ? " (d'avance)" : "",
                           heure.dernier_pas * 1e3, heure.T_avant * 1e3, heure.hors_rythme, heure.releves.size(), (int)heure.calee);
            }
            if (neuf->capture & 0x80000000u) a_capturer = neuf;
        }
        /* Le jeu a demandé la capture d'une image : on la lui fait, exacte, sans l'afficher — il ne
           l'attend plus, et elle sert aux écrans qu'il fige (une pause sur fond de scène). */
        if (a_capturer) {
            g_source = a_capturer.get(); g_passe_exacte = true; g_image_montree = a_capturer->image;
            GxFrame fx = a_capturer->frame; fx.polys = a_capturer->polys.data(); fx.count = a_capturer->polys.size();
            dessiner(a_capturer->out2d, a_capturer->a_top, a_capturer->a_3d ? &fx : nullptr);
            g_passe_exacte = false; g_source = nullptr;
        }
        if (recents.empty()) continue;
        if (chrono) c2 = horloge::now();
        const Instantane *dernier = recents.back().get();
        /* Quels écrans ont leur 3D ? Un seul d'ordinaire ; les deux quand le jeu alterne d'une image à
           l'autre — il montre alors sur l'autre écran la capture de l'image d'avant. Nous, nous
           redessinons chaque écran depuis sa propre file, et la capture ne se voit plus. */
        double maintenant = secondes(horloge::now());
        bool vivante[2] = {false, false};
        double retard = 0;                             /* en tours : l'écart entre deux images d'un même écran */
        for (int ec = 0; ec < 2 && !sans_interpolation && heure.calee; ec++) {
            if (cles[ec].size() < 2) continue;
            double ecart = 0;
            for (size_t k = cles[ec].size() > 4 ? cles[ec].size() - 4 : 1; k < cles[ec].size(); k++)
                ecart = std::max(ecart, cles[ec][k]->tour - cles[ec][k - 1]->tour);
            if (ecart <= 0 || ecart > 4) continue;
            if (maintenant - secondes(cles[ec].back()->quand) >= std::max(3.0 * ecart * heure.T, 0.1)) continue;   /* cette 3D ne bouge plus */
            vivante[ec] = true;
            retard = std::max(retard, ecart);
        }
        bool interpole = vivante[0] || vivante[1];
        {   /* SFC_DIAG_VIVANTE=de:a : chaque fois qu'un écran passe de « 3D vivante » à « immobile », et pourquoi */
            static long de_img = -2, a_img = 0;
            if (de_img == -2) {
                const char *e = getenv("SFC_DIAG_VIVANTE");
                de_img = e ? atol(e) : -1;
                a_img = e && strchr(e, ':') ? atol(strchr(e, ':') + 1) : de_img + 300;
            }
            static bool avant[2];
            if (de_img >= 0 && (long)dernier->image >= de_img && (long)dernier->image <= a_img)
                for (int ec = 0; ec < 2; ec++)
                    if (vivante[ec] != avant[ec]) {
                        double ecart = 0;
                        for (size_t k = cles[ec].size() > 4 ? cles[ec].size() - 4 : 1; k < cles[ec].size(); k++)
                            ecart = std::max(ecart, cles[ec][k]->tour - cles[ec][k - 1]->tour);
                        rt_log("vivante : écran %d %s à l'image %llu ; %zu clés, écart %.2f tours, dernière clé tour %.1f image %llu reçue il y a %.1f ms (seuil %.1f ms), horloge calée %d",
                               ec, vivante[ec] ? "reprend vie" : "s'arrête", (unsigned long long)dernier->image, cles[ec].size(), ecart,
                               cles[ec].empty() ? -1.0 : cles[ec].back()->tour, cles[ec].empty() ? 0ull : (unsigned long long)cles[ec].back()->image,
                               cles[ec].empty() ? 0.0 : 1e3 * (maintenant - secondes(cles[ec].back()->quand)),
                               1e3 * std::max(3.0 * ecart * heure.T, 0.1), (int)heure.calee);
                    }
            avant[0] = vivante[0]; avant[1] = vivante[1];
        }
        /* Le tour qu'on montre : celui de l'heure, moins l'écart entre deux images — il faut connaître la
           suivante pour se placer entre deux. Il ne recule jamais. */
        /* La latence : la part du tour d'écart qu'on garde derrière le jeu. 1 : on attend l'image suivante et l'on
           se place entre deux — le plus fluide, et l'interface y glisse aussi. 0 : on montre la plus récente dès
           qu'elle arrive et l'on prolonge son mouvement (entre_images.cpp) — aucun retard ajouté, mais l'interface,
           qui avance par sauts, reste à la cadence du jeu.
           Par défaut elle suit la scène. Dans les menus et le mode stratégie le jeu pense soixante fois par
           seconde : un tour de retard n'y coûte que seize millisecondes, et le joueur y préfère la fluidité. En
           vol il ne pense que trente fois : un tour y coûte trente-trois millisecondes, qu'il sentait sur les
           commandes, et la prédiction y est juste. On passe de l'une à l'autre en un tiers de seconde, le temps
           montré accélérant ou ralentissant à peine. SFC_LATENCE=0..1 la fixe. */
        /* Depuis que le vol publie ses images dès la fin du tour (video_publier_apercu), la fluidité n'y coûte
           plus rien : l'image paraît avant l'heure où la console l'aurait montrée. Elle est donc le défaut
           partout ; « auto » garde l'ancien choix selon la scène. */
        static const double latence_fixee = [] {
            const char *e = getenv("SFC_LATENCE");
            if (!e || !*e) return 1.0;
            if (!strcmp(e, "auto")) return -1.0;
            double v = atof(e);
            return v < 0 ? 0.0 : v > 1 ? 1.0 : v;
        }();
        if (heure.calee && heure.T > 0) {
            if (heure.T > 0.028) scene_d_action = true;
            else if (heure.T < 0.022) scene_d_action = false;
        }
        double latence_voulue = latence_fixee >= 0 ? latence_fixee : scene_d_action ? 0.0 : 1.0;
        /* le tour à venir est déjà calculé (un aperçu) : plus rien à attendre pour se placer entre deux */
        for (int ec = 0; ec < 2; ec++)
            if (vivante[ec] && !cles[ec].empty() && cles[ec].back()->apercu) latence_voulue = 0.0;
        {
            double dt = std::min(0.05, std::max(0.0, maintenant - maintenant_prec));
            double pas_max = dt / 0.3;
            latence += std::max(-pas_max, std::min(pas_max, latence_voulue - latence));
            maintenant_prec = maintenant;
        }
        double u = 0;
        if (interpole) {
            u = heure.lire(maintenant) - (double)retard * latence;
            if (etait_calee && u < u_montre) u = u_montre;
            /* …ni ne traîne : jamais plus d'un tour derrière ce qu'on pourrait montrer */
            double derniere = 0;
            for (int ec = 0; ec < 2; ec++) if (vivante[ec]) derniere = std::max(derniere, cles[ec].back()->tour);
            if (u < derniere - (double)retard - 1.0) {
                if (diag && diag[0] == '5') rt_log("recalage : u %.3f, dernière série %.0f, retard %u, T %.2f ms, heure de référence %+.1f ms après la dernière arrivée", u, derniere, retard, heure.T * 1e3, (heure.t_ref - heure.releves.back().arrivee) * 1e3);
                u = derniere - (double)retard - 1.0; n_recalages++;
            }
            u_montre = u;
            somme_retard += (derniere - u) * heure.T;
            n_retards++;
        }
        etait_calee = interpole;
        VueEcran vues[2];
        GxFrame f[2];
        /* l'image du jeu qui répond à ce tour-là : c'est elle qui donne la 2D */
        double image_voulue = (double)dernier->image;
        struct Segment { const Instantane *de, *vers; const sfc::Lien *lien; float a; } seg[2] = {};
        for (int screen = 0; screen < 2; screen++) {
            if (!vivante[screen]) continue;
            const std::vector<Image> &c = cles[screen];
            if (liens[screen].size() + 1 != c.size()) continue;        /* les deux files vont de pair */
            size_t k = 0;
            while (k + 2 < c.size() && c[k + 1]->tour <= u) k++;
            double s0 = c[k]->tour, s1 = c[k + 1]->tour;
            double a = (u - s0) / (s1 - s0);
            /* au-delà de la plus récente : la prédiction, jusqu'où l'attend la latence choisie et un quart de
               tour de plus pour une image un peu en retard ; au-delà, on attend */
            const double a_max = 1.0 + (1.0 - latence) * (double)retard / (s1 - s0) + (latence < 1.0 ? 0.25 : 0.0);
            if (a > a_max && k + 2 == c.size()) {                              /* l'image suivante n'est pas encore là */
                n_gels++;
                if (diag && diag[0] == '5') rt_log("attente : u %.3f dépasse la série %.0f (écran %d), T %.2f ms, référence %+.1f ms après la dernière arrivée, dernière arrivée il y a %.1f ms",
                                                   u, s1, screen, heure.T * 1e3, (heure.t_ref - heure.releves.back().arrivee) * 1e3, (maintenant - heure.releves.back().arrivee) * 1e3);
            }
            a = a < 0 ? 0 : a > a_max ? a_max : a;
            seg[screen] = Segment{c[k].get(), c[k + 1].get(), &liens[screen][k], (float)a};
            double im = (double)c[k]->image + a * (double)(c[k + 1]->image - c[k]->image);
            if (im < image_voulue) image_voulue = im;
        }
        /* L'écran sans 3D ne montre que de vraies images du jeu. Un tour d'aperçu n'y apporte rien — rien n'y
           est interpolé — et sa 2D est une prédiction, fausse une fois sur trois en mode stratégie (la carte
           de l'écran du bas) : glissée entre deux vraies images, elle faisait papilloter l'écran. */
        static int vraies = -1;                          /* SFC_2D_VRAIES=0 : comme avant, pour comparer */
        if (vraies < 0) { const char *e = getenv("SFC_2D_VRAIES"); vraies = !e || *e != '0'; }
        const Instantane *dernier_vrai = nullptr, *montre = nullptr;
        for (auto &r : recents) if (!r->apercu || !vraies) dernier_vrai = r.get();
        for (auto &r : recents) if ((!r->apercu || !vraies) && (double)r->image <= image_voulue) montre = r.get();
        if (!montre) montre = dernier_vrai ? dernier_vrai : recents.front().get();
        if (!interpole) montre = dernier_vrai ? dernier_vrai : dernier;
        /* L'image dont on montre la 2D ne recule jamais, pas plus que l'heure de la 3D. Quand le jeu cale une
           centaine de millisecondes — en mode stratégie, toutes les secondes et demie —, sa 3D passe pour
           immobile et l'on montre la plus récente ; quand elle repart, l'interpolation se place un tour
           derrière, et l'écran sans 3D revenait d'une image en arrière : c'est le sursaut que le joueur voyait
           sur l'écran du bas. On garde alors la dernière montrée jusqu'à ce que l'heure la rattrape. */
        static uint64_t montre_min, sien_min[2];
        static int monotone = -1;                        /* SFC_2D_MONOTONE=0 : comme avant, pour comparer */
        if (monotone < 0) { const char *e = getenv("SFC_2D_MONOTONE"); monotone = !e || *e != '0'; }
        if (!monotone) montre_min = sien_min[0] = sien_min[1] = 0;
        if (montre->image < montre_min)
            for (auto &r : recents) if ((!r->apercu || !vraies) && r->image >= montre_min) { montre = r.get(); break; }
        if (monotone && montre->image > montre_min) montre_min = montre->image;
        g_montre_image = montre->image; g_montre_tour = montre->tour; g_montre_apercu = montre->apercu;
        uint64_t signature = montre->image * 1000003ull;
        for (int screen = 0; screen < 2; screen++) {
            int engine = (screen == 0) == (montre->a_top != 0) ? 0 : 1;
            /* par défaut : ce que la machine montrait à cet instant */
            vues[screen] = VueEcran{&montre->out2d[engine], nullptr, false, montre, nullptr};
            vues[screen].id2d = montre->numero * 2 + (uint64_t)engine;
            if (engine == 0 && montre->a_3d) {
                f[screen] = montre->frame; f[screen].polys = const_cast<GxPolygon *>(montre->polys.data()); f[screen].count = montre->polys.size();
                vues[screen].f3d = &f[screen];
            }
            if (!vivante[screen]) continue;
            /* cet écran a sa 3D : ses couches 2D sont celles du moteur A la dernière fois qu'il y était */
            const Instantane *sien = nullptr;
            for (auto &r : recents) if ((double)r->image <= image_voulue && (r->a_top ? 0 : 1) == screen && r->out2d[0].has_3d) sien = r.get();
            if (!sien) for (auto &r : recents) if ((r->a_top ? 0 : 1) == screen && r->out2d[0].has_3d) { sien = r.get(); break; }
            if (!sien) continue;
            if (sien->image < sien_min[screen])            /* de même pour les couches 2D d'un écran à 3D */
                for (auto &r : recents)
                    if (r->image >= sien_min[screen] && (r->a_top ? 0 : 1) == screen && r->out2d[0].has_3d) { sien = r.get(); break; }
            if (monotone && sien->image > sien_min[screen]) sien_min[screen] = sien->image;
            vues[screen].o = &sien->out2d[0];
            vues[screen].id2d = sien->numero * 2;
            g_montre_image = sien->image; g_montre_tour = sien->tour; g_montre_apercu = sien->apercu;
            g_src3d_apercu[screen] = seg[screen].vers && seg[screen].vers->apercu;
            if (seg[screen].de && seg[screen].vers)
                g_src3d[screen] = SourceEcran{seg[screen].de->image, seg[screen].vers->image,
                                              seg[screen].de->tour, seg[screen].vers->tour,
                                              seg[screen].de->a_top, seg[screen].vers->a_top,
                                              seg[screen].de->apercu, seg[screen].vers->apercu,
                                              cles[screen].size(), seg[screen].a};
            vue_entre(vues[screen], f[screen], melange[screen], seg[screen].de, seg[screen].vers, *seg[screen].lien, seg[screen].a);
            signature = (signature * 31 + seg[screen].vers->image) * 8192 + (uint64_t)(seg[screen].a * 8191.0f);
            signature = signature * 31 + sien->image;
        }
        {   /* SFC_DIAG_BAS=de:a : l'empreinte de ce que montre l'écran du bas (ses couches 2D), à chaque
               changement, et d'où elle vient ; un retour à l'empreinte d'avant-avant est un aller-retour — le
               papillotement que l'œil voit. On compte les allers-retours et les changements. */
            static long de_img = -2, a_img = 0;
            if (de_img == -2) {
                const char *e = getenv("SFC_DIAG_BAS");
                de_img = e ? atol(e) : -1;
                a_img = e && strchr(e, ':') ? atol(strchr(e, ':') + 1) : de_img + 600;
            }
            if (de_img >= 0 && vues[1].o && (long)dernier->image >= de_img && (long)dernier->image <= a_img) {
                uint64_t h = 1469598103934665603ull;
                for (size_t k = 0; k < DS_W * (size_t)DS_H; k += 7) {
                    h ^= vues[1].o->below[k]; h *= 1099511628211ull;
                    h ^= vues[1].o->above[k]; h *= 1099511628211ull;
                }
                static uint64_t h1, h2, changements, retours;
                if (h != h1) {
                    changements++;
                    bool retour = h == h2;
                    retours += retour;
                    const Instantane *src = vues[1].recente;
                    rt_log("bas : image jeu %llu, empreinte %016llx%s, source image %llu%s ; %llu changements, %llu allers-retours",
                           (unsigned long long)dernier->image, (unsigned long long)h, retour ? " — RETOUR" : "",
                           src ? (unsigned long long)src->image : 0ull, src && src->apercu ? " (aperçu)" : "",
                           (unsigned long long)changements, (unsigned long long)retours);
                    h2 = h1; h1 = h;
                }
            }
        }
        if (signature == signature_affichee) {                             /* rien de neuf à montrer */
            std::this_thread::sleep_for(milliseconds(1));
            continue;
        }
        g_source = montre; g_src_recente = g_src_ancienne = nullptr;
        g_image_montree = montre->image;
        if (chrono) c3 = horloge::now();
        if (profiling()) g_t_image.start();
        dessiner_ecrans(vues);
        if (g_jumeles_a_prendre) {     /* la photo du cas, une fois l'image faite */
            g_jumeles_a_prendre = 0;
            static int pris;
            if (pris < 8) {
                const int lw = g_window->width(), lh = g_window->height();
                std::vector<uint8_t> px((size_t)lw * lh * 4);
                glReadPixels(0, 0, lw, lh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                char nom[80];
                snprintf(nom, sizeof nom, "build/jumeles_%02d.png", pris++);
                write_png(nom, lw, lh, px.data());
                rt_log("   photographié : %s", nom);
            }
        }
        if (profiling()) g_t_image.stop();
        if (chrono) c4 = horloge::now();
        g_source = g_src_recente = g_src_ancienne = nullptr;
        if (film_n && montre->image >= film_depuis) {
            const int fw = g_window->width(), fh = g_window->height();
            film.emplace_back((size_t)fw * fh * 4);
            glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, film.back().data());
            int ev = vivante[0] ? 0 : vivante[1] ? 1 : -1;
            film_vues.push_back(Vue{maintenant, u, ev >= 0 ? seg[ev].a : -1.0, montre->image,
                                    ev >= 0 ? seg[ev].de->image : 0, ev >= 0 ? seg[ev].vers->image : 0, ev});
            if (film.size() >= film_n) {
                FILE *ft = fopen("build/film.txt", "w");
                for (size_t k = 0; k < film.size(); k++) {
                    char nom[64];
                    snprintf(nom, sizeof nom, "build/film_%04zu.png", k);
                    write_png(nom, fw, fh, film[k].data());
                    const Vue &v = film_vues[k];
                    if (ft) fprintf(ft, "%zu t=%.4f u=%.4f a=%.4f image=%llu de=%llu vers=%llu ecran=%d\n", k, v.t, v.u, v.a,
                                    (unsigned long long)v.image, (unsigned long long)v.de, (unsigned long long)v.vers, v.vivante);
                }
                if (ft) fclose(ft);
                rt_log("film : %zu images enregistrées", film.size());
                g_fermeture = true;
                return;
            }
        }
        g_window->swapBuffers();
        if (chrono) c5 = horloge::now();
        g_images_affichees++;
        signature_affichee = signature;
        if (profiling() && ++n_images % 1200 == 0) {
            auto t = horloge::now();
            rt_log("affichage libre : %.0f images affichées par seconde ; tour de %.1f ms, latence %.2f, montré avec %.0f ms de retard (%llu recalages) ; appariés %.1f %% (dont %.1f %% par voisinage, %.2f %% qui glissent sans se déformer, %llu objets reconnus rang par rang) ; %llu coupures sur %llu ; %llu attentes de l'image suivante ; résidu de caméra %.5f ; 3D %s",
                   1200.0 / std::chrono::duration<double>(t - t_stat).count(), heure.T * 1000.0, latence,
                   n_retards ? 1000.0 * somme_retard / (double)n_retards : 0.0, (unsigned long long)n_recalages,
                   n_polys ? 100.0 * n_apparies / n_polys : 0.0, n_polys ? 100.0 * n_seconds / n_polys : 0.0,
                   n_polys ? 100.0 * n_raides / n_polys : 0.0, (unsigned long long)n_realignes,
                   (unsigned long long)n_coupures, (unsigned long long)n_liens, (unsigned long long)n_gels, residu_max,
                   vivante[0] && vivante[1] ? "sur les deux écrans" : interpole ? "sur un écran" : "immobile");
            {   static int diag = -1; static uint64_t sans, avec;
                if (diag < 0) diag = getenv("SFC_DIAG_CLIGNOTE") != nullptr;
                if (diag && (g_tex_sans != sans || g_tex_avec != avec)) {
                    rt_log("   textures : %llu lectures sans cache d'instantané sur %llu",
                           (unsigned long long)(g_tex_sans - sans), (unsigned long long)(g_tex_sans - sans + g_tex_avec - avec));
                    sans = g_tex_sans; avec = g_tex_avec;
                }
            }
            {   static uint64_t retournes;
                if (sfc::g_retournes != retournes) {
                    rt_log("   polygones retournés par l'interpolation (le moteur les élimine) : %llu",
                           (unsigned long long)(sfc::g_retournes - retournes));
                    retournes = sfc::g_retournes;
                }
            }
            {   static uint64_t vus;
                if (sfc::g_blocs_ambigus != vus) {
                    rt_log("   blocs refusés faute d'être francs (deux prétendants se valaient) : %llu",
                           (unsigned long long)(sfc::g_blocs_ambigus - vus));
                    vus = sfc::g_blocs_ambigus;
                }
            }
            t_stat = t; n_apparies = n_seconds = n_polys = n_coupures = n_gels = n_liens = n_recalages = n_retards = n_raides = n_realignes = 0;
            residu_max = somme_retard = 0;
        }
        /* le plafond : attente hybride, comme le cadencement du jeu */
        int ips = plafond_ips();
        if (ips > 0) {
            auto pas = std::chrono::nanoseconds(1000000000ll / ips);
            prochaine += pas;
            auto la = horloge::now();
            /* en retard de plus d'une image : on ne la rattrape pas — deux images coup sur coup ne se voient
               pas, elles décalent seulement l'instant des suivantes */
            if (prochaine < la - pas) prochaine = la;
            attendre_jusqua(prochaine);
        }
    }
}

}  // namespace

/* Le fil du jeu va quitter : le fil d'affichage finit son tour et s'arrête (une seconde au plus). */
extern "C" void video_arreter(void) {
    if (!g_fil_affichage_vivant || std::this_thread::get_id() == g_id_fil_affichage) return;
    g_arret_demande = true;
    g_nouvel_instantane.notify_all();
    for (int i = 0; i < 1000 && g_fil_affichage_vivant; i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

namespace {

void lancer_fil_affichage() {
    static std::mutex pret_m;
    static std::condition_variable pret_cv;
    static bool pret = false;
    std::thread(fil_affichage, &pret_m, &pret_cv, &pret).detach();
    std::unique_lock<std::mutex> l(pret_m);
    pret_cv.wait(l, [] { return pret; });
    rt_log("affichage : libre — la fenêtre et OpenGL ont leur fil, le jeu ne les attend plus");
}

/* L'espace texture et l'espace palette tels que le moteur 3D les voit, recopiés : le fil d'affichage
   décodera ses textures là-dedans pendant que le jeu réécrit la mémoire vidéo. */
void copier_textures(Textures &i) {
    i.tex.assign(0x80000, 0);
    for (uint32_t slot = 0; slot < 4; slot++)
        if (const uint8_t *base = tex_slot_base(slot)) memcpy(i.tex.data() + slot * 0x20000u, base, 0x20000);
    i.pal.assign(0x20000, 0);
    for (int b = 6; b >= 5; b--) {
        uint8_t cnt = (uint8_t)(mmio_peek(0x244) >> ((b - 4) * 8));
        if ((cnt & 0x87) != 0x83) continue;
        uint32_t ofs = (cnt >> 3) & 3, base = (ofs & 1) * 0x4000 + (ofs >> 1) * 0x10000;
        if (base + 0x4000 <= 0x20000) memcpy(i.pal.data() + base, g_vram + BANK_OFFSET[b], 0x4000);
    }
    uint8_t e = (uint8_t)(mmio_peek(0x244) & 0xFF);
    if ((e & 0x87) == 0x83) memcpy(i.pal.data(), g_vram + BANK_OFFSET[4], 0x10000);   /* E l'emporte, comme à la lecture */
}

double g_gain_commandes;                       /* (mesure) somme des attentes de cadencement, dont les commandes sont plus fraîches */

/* Les textures telles que le moteur 3D les voit maintenant : partagées avec l'instantané précédent tant que
   le jeu n'a rien écrit en mémoire vidéo. */
std::shared_ptr<const Textures> g_textures_dernieres;

std::shared_ptr<const Textures> textures_du_moment() {
    std::shared_ptr<const Textures> &dernieres = g_textures_dernieres;
    uint64_t version = mem_gfx_writes();
    uint32_t banques[2] = {mmio_peek(0x240), mmio_peek(0x244)};
    if (!dernieres || dernieres->ecritures != version || dernieres->banques[0] != banques[0] || dernieres->banques[1] != banques[1]) {
        auto t = std::make_shared<Textures>();
        copier_textures(*t);
        static uint64_t numero;
        t->version = ++numero; t->ecritures = version; t->banques[0] = banques[0]; t->banques[1] = banques[1];
        dernieres = t;
    }
    return dernieres;
}

/* L'écran du moteur A pour chacun des quatre derniers tours montrés — des tours distincts, non des
   balayages. Dans les menus, le jeu alterne sa 3D d'un écran à l'autre à chaque tour ; quand il prend du
   retard, le balayage remontre le même tour, sur le même écran, trois fois de suite. Comptés par balayage,
   ces doublons faisaient croire que l'alternance avait cessé : une image publiée d'avance partait alors sur
   l'écran d'où le jeu venait, la carte des systèmes s'affichait sur les deux écrans, et la vraie image du
   tour, arrivée ensuite sur le bon, était refusée comme doublon. [0] est le dernier ; -1 : pas de 3D. */
static int g_ecran_par_tour[4] = {-1, -2, -3, -4};
static uint32_t g_ecran_par_tour_serie;
static bool ecrans_stables() {
    return g_ecran_par_tour[0] >= 0 && g_ecran_par_tour[0] == g_ecran_par_tour[1] &&
           g_ecran_par_tour[1] == g_ecran_par_tour[2] && g_ecran_par_tour[2] == g_ecran_par_tour[3];
}
static bool ecrans_alternent() {
    for (int k = 0; k < 3; k++)
        if (g_ecran_par_tour[k] < 0 || g_ecran_par_tour[k + 1] < 0 || g_ecran_par_tour[k] == g_ecran_par_tour[k + 1]) return false;
    return true;
}
uint32_t g_derniere_publiee;

uint32_t g_apercu_serie[8];                   /* les dernières séries publiées d'avance, et quand */
std::chrono::steady_clock::time_point g_apercu_quand[8];
double g_avance_somme;
uint64_t g_avance_n;

/* SFC_DIAG_2D_APERCU=1 : ce qu'un tour d'aperçu dessine en 2D est-il ce que le vrai tour dessinera ?
   L'aperçu prédit le tour suivant ; si sa 2D en diffère, l'écran fait l'aller-retour entre les deux et
   l'œil y voit un entrelacement. On range l'empreinte de ce qu'il a dessiné, et le balayage la compare. */
uint64_t empreinte_2d(const Engine2dOutput *o) {
    uint64_t h = 1469598103934665603ull;
    auto avaler = [&h](const void *p, size_t n) {
        const uint8_t *b = (const uint8_t *)p;
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    };
    for (int e = 0; e < 2; e++) {
        avaler(o[e].below, sizeof o[e].below);
        avaler(o[e].above, sizeof o[e].above);
        avaler(&o[e].has_3d, sizeof o[e].has_3d);
    }
    return h;
}
static uint64_t g_emp_apercu[16], g_emp_apercu_image[16];
static int g_atop_apercu[16];               /* l'ordre des deux écrans que cet aperçu-là annonçait */

/* L'état d'affichage : ce qui dit comment les deux écrans sont montés — modes des deux moteurs, ordre des
   écrans, capture, découpage de la mémoire vidéo. Le jeu le refait quand il change de scène ou charge un
   décor ; pendant ce remaniement, qui tient en un tour, rien de ce qu'on lirait n'est une image à montrer. */
static uint64_t etat_affichage(void) {
    uint64_t h = mmio_peek(0x000);
    h = h * 1000003ull + mmio_peek(0x1000);
    h = h * 1000003ull + (mmio_peek(0x304) & 0xFFFFu);
    h = h * 1000003ull + (mmio_peek(0x064) & 0x7FFFFFFFu);
    for (uint32_t r = 0x240; r <= 0x248; r += 4) h = h * 1000003ull + mmio_peek(r);
    for (uint32_t r = 0x008; r <= 0x00E; r += 2) h = h * 1000003ull + mmio_peek(r);
    for (uint32_t r = 0x1008; r <= 0x100E; r += 2) h = h * 1000003ull + mmio_peek(r);
    return h;
}
static uint64_t g_etat_affichage_balayage;
static uint64_t g_apercus_ecartes;

/* On ne devine l'ordre du tour à venir que si les quatre derniers balayages alternent sans faute : à un
   changement de scène l'alternance s'interrompt, et une image d'aperçu qui aurait deviné l'inverse donnerait
   le même écran deux fois. Dans le doute, -1 : cet aperçu-là ne sera pas montré. */
static int atop_du_prochain_tour() {
    /* Ce jeu échange ses deux écrans à chaque tour dans les menus — c'est ainsi qu'il montre sa 3D tantôt en
       haut, tantôt en bas —, et le registre ne bascule qu'au balayage, qu'un tour d'aperçu ne joue pas. Si
       les quatre derniers tours ont alterné sans faute, le prochain ira sur l'autre écran que le dernier ;
       sinon on garde ce que le registre dit — mieux vaut une image qui peut se tromper d'écran qu'aucune
       image du tout (exiger la certitude écartait tous les aperçus de certaines scènes). */
    if (ecrans_alternent()) return !g_ecran_par_tour[0];
    return video2d_a_on_top();
}
static Engine2dOutput g_2d_apercu[2];        /* la dernière 2D d'aperçu, pour dire en quoi elle diffère */
static uint64_t g_2d_apercu_image;
/* le fond de l'écran du bas aux deux derniers balayages : [0] le dernier, [1] celui d'avant */
static uint32_t g_fond_b[2][DS_W * DS_H];
static int g_fond_b_n;
static uint64_t g_fond_b_rendus;

void deposer(std::shared_ptr<Instantane> i) {
    {
        std::lock_guard<std::mutex> l(g_verrou);
        g_deposes.push_back(std::move(i));
        while (g_deposes.size() > 16) g_deposes.pop_front();      /* la fenêtre ne répond plus : on ne garde pas tout */
    }
    g_nouvel_instantane.notify_all();
}

void vblank_libre() {
    if (g_fermeture) { rt_log("fenêtre fermée"); exit(0); }
    /* SFC_DIAG_ACOUPS=ms : les tours du jeu plus longs que ça, et d'où vient le temps. Un à-coup que l'œil
       voit dure une vingtaine de millisecondes de trop ; c'est ici qu'on le prend sur le fait. */
    {   static double seuil = -1;
        if (seuil < 0) { const char *e = getenv("SFC_DIAG_ACOUPS"); seuil = e ? atof(e) : 0; if (seuil <= 0) seuil = e ? 40 : 1e9; }
        static std::chrono::steady_clock::time_point avant;
        auto maintenant = std::chrono::steady_clock::now();
        if (avant.time_since_epoch().count()) {
            double ms = std::chrono::duration<double, std::milli>(maintenant - avant).count();
            if (ms > seuil)
                rt_log("à-coup : tour de %.1f ms à l'image %llu — %d aperçus (%.1f ms, dont %.1f ms mordus sur le balayage en %d fois), 3D %zu polygones",
                       ms, (unsigned long long)hw_frames(), g_n_apercus_tour, g_ms_apercus, g_ms_debord, g_n_debords,
                       g_last_3d ? g_last_3d->count : (size_t)0);
        }
        avant = maintenant;
        g_ms_apercus = g_ms_debord = 0; g_n_apercus_tour = g_n_debords = 0;
    }
    composer_2d();
    {   static int diag = -1; static uint64_t vus, pareils, depuis;
        if (diag < 0) { const char *e = getenv("SFC_DIAG_2D_APERCU"); diag = e != nullptr; depuis = e ? (uint64_t)atoll(e) : 0; }
        if (diag && hw_frames() >= depuis) {
            uint64_t img = hw_frames();
            if (g_emp_apercu_image[img & 15] == img) {
                vus++;
                pareils += g_emp_apercu[img & 15] == empreinte_2d(g_out2d);
                {   static uint64_t atop_faux;
                    if (g_atop_apercu[img & 15] != video2d_a_on_top()) {
                        atop_faux++;
                        rt_log("   image %llu : l'aperçu annonçait les écrans dans l'autre ordre (a_top %d au lieu de %d) — %llu fois",
                               (unsigned long long)img, g_atop_apercu[img & 15], video2d_a_on_top(),
                               (unsigned long long)atop_faux);
                    }
                }
                if (vus % 300 == 0)
                    rt_log("2D des aperçus : %llu prédictions comparées, %llu identiques au vrai tour (%.1f %%)",
                           (unsigned long long)vus, (unsigned long long)pareils, 100.0 * (double)pareils / (double)vus);
                /* en quoi diffèrent-elles ? le compte des points, écran par écran et couche par couche */
                if (g_2d_apercu_image == img && g_emp_apercu[img & 15] != empreinte_2d(g_out2d)) {
                    static int dits;
                    long n[4] = {0, 0, 0, 0};
                    for (int e = 0; e < 2; e++)
                        for (int k = 0; k < DS_W * DS_H; k++) {
                            n[e * 2] += g_2d_apercu[e].below[k] != g_out2d[e].below[k];
                            n[e * 2 + 1] += g_2d_apercu[e].above[k] != g_out2d[e].above[k];
                        }
                    if (dits == 0) {   /* la première : les deux fonds en images, pour voir ce qui change */
                        write_png("build/2d_apercu_B_dessous.png", DS_W, DS_H, (const uint8_t *)g_2d_apercu[1].below);
                        write_png("build/2d_vrai_B_dessous.png", DS_W, DS_H, (const uint8_t *)g_out2d[1].below);
                        write_png("build/2d_fond_precedent.png", DS_W, DS_H, (const uint8_t *)g_fond_b[0]);
                    }
                    if (dits++ < 40)
                        rt_log("   image %llu : points différents — écran A dessous %ld dessus %ld, écran B dessous %ld dessus %ld (sur %d)",
                               (unsigned long long)img, n[0], n[1], n[2], n[3], DS_W * DS_H);
                }
            }
        }
    }
    g_etat_affichage_balayage = etat_affichage();
    memcpy(g_fond_b[1], g_fond_b[0], sizeof g_fond_b[0]);
    memcpy(g_fond_b[0], g_out2d[1].below, sizeof g_fond_b[0]);
    if (g_fond_b_n < 2) g_fond_b_n++;
    {   static uint64_t n;
        static int diag = -1;
        if (diag < 0) diag = getenv("SFC_DIAG_2D_APERCU") != nullptr;
        if (diag && ++n % 600 == 0)
            rt_log("fond de l'écran du bas : %llu reculs d'aperçu rattrapés", (unsigned long long)g_fond_b_rendus);
    }
    auto i = nouvel_instantane();
    i->numero = ++g_numero_instantane;
    i->out2d[0] = g_out2d[0];
    i->out2d[1] = g_out2d[1];
    i->a_top = video2d_a_on_top();
    if (g_last_3d) {
        i->a_3d = true;
        i->frame = *g_last_3d;
        i->tour = g_last_3d->serial;
        i->polys.assign(g_last_3d->polys, g_last_3d->polys + g_last_3d->count);
        i->textures = textures_du_moment();
        { static long long d = -2; if (d == -2) { const char *e = getenv("SFC_DIAG_APERCU"); d = e ? atoll(e) : -1; }
          static int n; if (d >= 0 && (long long)hw_frames() >= d && n++ < 80) rt_log("balayage : image %llu présente la série %u (%zu polygones), a_top %d", (unsigned long long)hw_frames(), g_last_3d->serial, g_last_3d->count, video2d_a_on_top()); }
        uint32_t k = g_last_3d->serial & 7u;
        if (g_last_3d->serial && g_apercu_serie[k] == g_last_3d->serial) {
            g_avance_somme += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_apercu_quand[k]).count();
            g_avance_n++;
            g_apercu_serie[k] = 0;
        }
    }
    i->alpha_ref = mmio_peek(0x340);
    i->capture = mmio_peek(0x064);
    memcpy(i->vram_ecrit, g_vram_ecrit, sizeof i->vram_ecrit);
    i->image = hw_frames();
    i->quand = std::chrono::steady_clock::now();
    {   /* l'écran de chaque nouveau tour montré ; sans 3D, l'historique repart de rien */
        if (g_last_3d && g_out2d[0].has_3d) {
            if (g_last_3d->serial != g_ecran_par_tour_serie) {
                g_ecran_par_tour_serie = g_last_3d->serial;
                for (int k = 3; k > 0; k--) g_ecran_par_tour[k] = g_ecran_par_tour[k - 1];
                g_ecran_par_tour[0] = i->a_top;
            }
        } else {
            for (int k = 0; k < 4; k++) g_ecran_par_tour[k] = -1 - k;
            g_ecran_par_tour_serie = 0;
        }
    }
    /* La capture d'affichage : la console la termine avec l'image, le jeu ne l'attend donc pas. C'est
       le fil d'affichage qui la fera, dès qu'il verra cet instantané ; plus rien ne retient le jeu,
       même quand la fenêtre est lente, déplacée ou en train de se fermer. */
    if (i->capture & 0x80000000u) mmio_poke(0x064, i->capture & ~0x80000000u);
    deposer(i);
    if (profiling()) {
        static uint64_t n; static uint64_t affichees_avant; static std::chrono::steady_clock::time_point t0;
        if (++n % 300 == 0) {
            auto t = std::chrono::steady_clock::now();
            if (t0.time_since_epoch().count())
                rt_log("affichage libre : %.1f images de jeu et %.1f images affichées par seconde ; commandes lues %.1f ms plus tard qu'avant ; images 3D publiées %.1f ms avant leur présentation (%llu)",
                       300.0 / std::chrono::duration<double>(t - t0).count(),
                       (double)(g_images_affichees - affichees_avant) / std::chrono::duration<double>(t - t0).count(),
                       g_gain_commandes / 300.0, g_avance_n ? g_avance_somme / (double)g_avance_n : 0.0,
                       (unsigned long long)g_avance_n);
            t0 = t; affichees_avant = g_images_affichees; g_gain_commandes = 0; g_avance_somme = 0; g_avance_n = 0;
        }
    }
    /* Les commandes se lisent après l'attente qui cale le jeu sur le temps réel, non avant : le jeu les
       prend au début de l'image qui commence maintenant, et elles ont jusqu'à seize millisecondes de moins.
       C'est le même balayage de la machine : un rejeu donne exactement le même jeu. */
    auto avant_attente = std::chrono::steady_clock::now();
    pace_frame();
    if (profiling()) g_gain_commandes += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - avant_attente).count();
    Manette manette;
    { std::lock_guard<std::mutex> l(g_mx_manette); manette = g_in_manette; }
    appliquer_entrees(Entrees{g_in_held, g_in_tdown, g_in_tx, g_in_ty, g_in_mx, g_in_my, g_in_bouton, g_in_ww, g_in_wh, manette});
}

}  // namespace

/* Le jeu a fini son image et attend l'heure de la montrer (l'attente du tour en vol) : l'affichage libre la
   prend tout de suite. Sur la console elle ne paraîtrait qu'au balayage qui suit l'attente — jusqu'à trente
   millisecondes plus tard —, et c'est de là que venait l'essentiel du retard que l'interpolation ajoutait.
   Ses couches 2D restent celles du dernier balayage ; la présentation, elle, se fera comme avant et ne
   sera plus qu'un doublon. SFC_SANS_APERCU=1 revient à l'ancienne façon. */
void signature_3d(const GxFrame &f, double tour, const char *quoi);

static void publier(const GxFrame &f) {
    if (f.count == 0 || !g_out2d[0].has_3d || f.serial <= g_derniere_publiee) return;
    if (!ecrans_stables()) return;
    g_derniere_publiee = f.serial;
    auto i = nouvel_instantane();
    i->numero = ++g_numero_instantane;
    i->out2d[0] = g_out2d[0];
    i->out2d[1] = g_out2d[1];
    i->a_top = video2d_a_on_top();
    i->a_3d = true;
    i->frame = f;
    i->tour = f.serial;
    i->polys.assign(f.polys, f.polys + f.count);
    i->textures = textures_du_moment();
    i->alpha_ref = mmio_peek(0x340);
    i->capture = 0;
    i->image = hw_frames();
    i->quand = std::chrono::steady_clock::now();
    i->precoce = true;
    signature_3d(f, f.serial, "du jeu");
    g_apercu_serie[f.serial & 7u] = f.serial;
    g_apercu_quand[f.serial & 7u] = i->quand;
    { static long long d = -2; if (d == -2) { const char *e = getenv("SFC_DIAG_APERCU"); d = e ? atoll(e) : -1; }
      static int n; if (d >= 0 && (long long)hw_frames() >= d && n++ < 40) rt_log("aperçu : série %u publiée à l'image %llu (%zu polygones)", f.serial, (unsigned long long)hw_frames(), f.count); }
    deposer(std::move(i));
}

static bool apercus_permis() {
    if (!g_ready || !affichage_libre() || g_apercu_en_cours) return false;
    static int non = -1;
    if (non < 0) non = getenv("SFC_SANS_APERCU") != nullptr;
    return !non;
}

/* le tour que le jeu vient de finir, et quand : l'heure d'où partent les aperçus */
double g_tour_publie_quand;       /* secondes, horloge monotone */
uint32_t g_tour_publie;
double g_tour_duree = 1.0 / 30.0;

extern "C" void video_publier_apercu(void) {
    if (!apercus_permis()) return;
    GxFrame f = gx_geom_apercu();
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (g_tour_publie && f.serial == g_tour_publie + 1) {
        double d = t - g_tour_publie_quand;
        if (d > 0.005 && d < 0.1) g_tour_duree += 0.1 * (d - g_tour_duree);
    }
    g_tour_publie = f.serial;
    g_tour_publie_quand = t;
    publier(f);
}

/* L'image d'un tour d'aperçu : le tampon 3D que le tour a fini de construire, et les couches 2D telles que
   ce tour-là les a écrites — le HUD, les sprites, les fonds, tous à sa fraction de tour (natif.c l'appelle
   au moment où ce tour arrive à son attente). */
extern "C" void video_deposer_apercu(double tour) {
    GxFrame f = gx_geom_apercu();
    if (f.count == 0) return;
    /* Ce tour-là remanie l'affichage : son image n'est pas celle d'un écran monté, et la montrer donnerait
       un éclair — deux fois le même écran, un décalage, un bout de rien. On la laisse. */
    static int ecarter = -1;
    if (ecarter < 0) { const char *e = getenv("SFC_APERCU_ECARTER"); ecarter = !e || (*e != '0' && strcmp(e, "non")); }
    if (ecarter && etat_affichage() != g_etat_affichage_balayage) {
        static int diag = -1;
        if (diag < 0) diag = getenv("SFC_DIAG_APERCUS") != nullptr;
        if (++g_apercus_ecartes % 50 == 0 && diag)
            rt_log("aperçus écartés, l'affichage s'y remaniait : %llu", (unsigned long long)g_apercus_ecartes);
        return;
    }
    auto i = nouvel_instantane();
    i->numero = ++g_numero_instantane;
    /* SFC_APERCU_2D=0 : l'aperçu reprend les couches 2D du dernier balayage au lieu de composer les siennes.
       Ce que son tour dessine en 2D est une prédiction, et une prédiction fausse se voit : un élément qui
       clignote d'un tour à l'autre se met à papilloter à la cadence de l'affichage. */
    static int compose = -1;
    if (compose < 0) { const char *e = getenv("SFC_APERCU_2D"); compose = !e || (*e != '0' && strcmp(e, "non")); }
    if (compose) composer_2d_dans(i->out2d);
    else { i->out2d[0] = g_out2d[0]; i->out2d[1] = g_out2d[1]; }
    /* Le fond de l'écran du bas, c'est l'image 3D que la capture d'affichage y dépose, un tour plus tôt ; le
       jeu alterne pour cela deux banques de mémoire vidéo. Un tour d'aperçu, qui ne capture rien, lit celle
       que le jeu vient de choisir pour la capture à venir : elle porte encore l'image d'il y a deux tours.
       L'écran du bas reculait donc d'un tour à chaque image d'aperçu, puis sautait de deux — c'est
       l'entrelacement que l'œil y voyait. Quand ce fond est exactement celui de l'avant-dernier balayage,
       et que le dernier en montre un autre, c'est ce recul-là : on lui rend le dernier. */
    {   static int actif = -1;
        if (actif < 0) { const char *e = getenv("SFC_APERCU_FOND_B"); actif = !e || (*e != '0' && strcmp(e, "non")); }
        if (actif && g_fond_b_n >= 2
            && memcmp(i->out2d[1].below, g_fond_b[1], sizeof g_fond_b[1]) == 0
            && memcmp(g_fond_b[0], g_fond_b[1], sizeof g_fond_b[0]) != 0) {
            memcpy(i->out2d[1].below, g_fond_b[0], sizeof g_fond_b[0]);
            g_fond_b_rendus++;
        }
    }
    int a_top = atop_du_prochain_tour();
    {   /* combien d'aperçus sont écartés faute de savoir l'ordre des écrans : s'ils le sont par
           intermittence, l'affichage saute entre une image d'aperçu et son absence, et l'objet semble
           perdre des morceaux d'une image à l'autre. */
        static int diag = -1; static uint64_t vus, ecartes;
        if (diag < 0) diag = getenv("SFC_DIAG_CLIGNOTE") != nullptr;
        if (diag) {
            vus++; ecartes += a_top < 0;
            if (vus % 300 == 0)
                rt_log("aperçus : %llu écartés sur %llu faute de connaître l'ordre des écrans (%.1f %%)",
                       (unsigned long long)ecartes, (unsigned long long)vus, 100.0 * (double)ecartes / (double)vus);
        }
    }
    if (a_top < 0) {                     /* l'alternance des écrans s'interrompt : on ne montre pas ce tour-là */
        g_apercus_ecartes++;
        return;
    }
    i->a_top = a_top;
    i->a_3d = true;
    i->frame = f;
    i->tour = tour;
    i->polys.assign(f.polys, f.polys + f.count);
    i->textures = textures_du_moment();
    i->alpha_ref = mmio_peek(0x340);
    i->capture = 0;
    i->image = hw_frames();
    i->quand = std::chrono::steady_clock::now();
    i->precoce = true;
    i->apercu = true;
    {   /* SFC_DIAG_CLIGNOTE : un aperçu dessine-t-il autant de polygones qu'un vrai tour ? S'il en dessine
           moins, ce qui manque disparaît de l'écran le temps de son image — et l'objet semble cligner. */
        static int diag = -1; static uint64_t n, somme_apercu, somme_vrai, moins;
        if (diag < 0) diag = getenv("SFC_DIAG_CLIGNOTE") != nullptr;
        if (diag && g_last_3d) {
            n++; somme_apercu += f.count; somme_vrai += g_last_3d->count;
            moins += f.count + 8 < g_last_3d->count;
            if (n % 600 == 0)
                rt_log("aperçus : %llu polygones en moyenne contre %llu au dernier vrai tour ; %llu aperçus sur %llu en ont nettement moins",
                       (unsigned long long)(somme_apercu / n), (unsigned long long)(somme_vrai / n),
                       (unsigned long long)moins, (unsigned long long)n);
        }
    }
    {   static int diag = -1;
        if (diag < 0) diag = getenv("SFC_DIAG_2D_APERCU") != nullptr;
        if (diag) {
            uint64_t img = hw_frames() + 1;      /* le tour que cet aperçu prédit sera montré à ce balayage-là */
            g_emp_apercu[img & 15] = empreinte_2d(i->out2d);
            g_emp_apercu_image[img & 15] = img;
            g_atop_apercu[img & 15] = i->a_top;
            g_2d_apercu[0] = i->out2d[0]; g_2d_apercu[1] = i->out2d[1]; g_2d_apercu_image = img;
        }
    }
    signature_3d(f, tour, "d'aperçu");
    deposer(std::move(i));
}

/* SFC_DIAG_APERCU_SIG=image : la « position » d'une image 3D — la somme des coordonnées de ses cent premiers
   sommets —, pour voir la progression d'un tour à l'autre et des aperçus entre eux. */
void signature_3d(const GxFrame &f, double tour, const char *quoi) {
    static long long depuis = -2;
    if (depuis == -2) { const char *e = getenv("SFC_DIAG_APERCU_SIG"); depuis = e ? atoll(e) : -1; }
    if (depuis < 0 || (long long)hw_frames() < depuis) return;
    static int n;
    if (n++ > 60) return;
    double sx = 0, sy = 0, sz = 0;
    size_t k = std::min<size_t>(f.count, 100);
    for (size_t i = 0; i < k; i++) { sx += f.polys[i].v[0].x; sy += f.polys[i].v[0].y; sz += f.polys[i].v[0].z; }
    rt_log("image 3D %s : tour %.3f, %zu polygones, somme (%.0f %.0f %.0f)", quoi, tour, f.count, sx, sy, sz);
}

/* l'aperçu est effacé : la mémoire vidéo est revenue, les textures copiées pendant lui ne valent plus */
extern "C" void video_apercu_fini(void) { g_textures_dernieres.reset(); }

/* Les tours d'aperçu, pendant que le jeu attend l'heure du balayage : il calcule le tour suivant, avec les
   commandes de l'instant, et l'affichage glisse vers lui au lieu de l'attendre — autant de fois par tour que
   la cadence choisie en demande et que la machine en permet (leur coût est mesuré). SFC_APERCUS=0 les retire. */
void apercus_pendant(std::chrono::steady_clock::time_point echeance) {
    static int actifs = -1;
    if (actifs < 0) { const char *e = getenv("SFC_APERCUS"); actifs = !e || (*e != '0' && strcmp(e, "non")); }
    {   static int diag = -1; static uint64_t n, raisons[5];
        if (diag < 0) diag = getenv("SFC_DIAG_APERCUS") != nullptr;
        if (diag) {
            raisons[!actifs ? 0 : !affichage_libre() ? 1 : !natif_apercu_possible() ? 2 : !g_tour_publie ? 3 : 4]++;
            if (++n % 600 == 0) rt_log("planificateur des aperçus : %llu appels ; inactifs %llu, pas libre %llu, impossibles %llu, sans tour %llu, possibles %llu",
                                       (unsigned long long)n, (unsigned long long)raisons[0], (unsigned long long)raisons[1],
                                       (unsigned long long)raisons[2], (unsigned long long)raisons[3], (unsigned long long)raisons[4]);
        }
    }
    if (!actifs || !affichage_libre() || !natif_apercu_possible() || !g_tour_publie) return;
    using horloge = std::chrono::steady_clock;
    static double cout = 0.003;                               /* ce que coûte un aperçu, mesuré */
    static uint32_t tour_vu;
    static double k_dernier;
    if (tour_vu != g_tour_publie) { tour_vu = g_tour_publie; k_dernier = 0; }
    int ips = plafond_ips() > 0 ? plafond_ips() : 240;
    double pas_temps = std::max(1.0 / ips, 1.3 * cout);         /* entre deux aperçus */
    double pas = pas_temps / g_tour_duree;
    static uint64_t sorties[4], n_sorties;
    static int diag = -1;
    if (diag < 0) diag = getenv("SFC_DIAG_APERCUS") != nullptr;
    auto sortir = [&](int r, double k, double reste) {
        sorties[r]++;
        if (diag && ++n_sorties % 300 == 0)
            rt_log("   sorties : pas le temps %llu, tour fini %llu, attente nulle %llu ; dernière : k %.3f, reste %.2f ms, tour %.1f ms, coût %.2f ms",
                   (unsigned long long)sorties[0], (unsigned long long)sorties[1], (unsigned long long)sorties[2],
                   k, reste * 1e3, g_tour_duree * 1e3, cout * 1e3);
    };
    for (;;) {
        auto maintenant = horloge::now();
        double t = std::chrono::duration<double>(maintenant.time_since_epoch()).count();
        double reste = std::chrono::duration<double>(echeance - maintenant).count();
        double k = (t - g_tour_publie_quand) / g_tour_duree;
        if (reste < cout + 0.0015) { sortir(0, k, reste); return; }       /* l'heure du balayage passe avant tout */
        if (k >= 0.97) { sortir(1, k, reste); return; }
        if (k - k_dernier >= pas) {
            auto a = horloge::now();
            /* Un tour d'aperçu calcule le tour suivant en entier — la mise à l'échelle n'a pas prise sur les
               positions du jeu, qui ne passent pas par des écritures reconnaissables. C'est le tour d'après,
               avec les commandes de l'instant : l'affichage s'en sert comme du tour à venir et glisse vers lui,
               sans attendre que le jeu l'ait vraiment joué. Refait plus loin dans le tour, il tient compte des
               commandes les plus fraîches. */
            int pris = natif_apercu_jouer(65536, (double)g_tour_publie + 1.0, g_in_apercu, reste - 0.0015);
            double d = std::chrono::duration<double>(horloge::now() - a).count();
            if (!pris && d > reste - 0.002) k_dernier = 1.0;   /* il a manqué de temps : pas de second essai */
            g_ms_apercus += d * 1e3;
            g_n_apercus_tour++;
            if (d > reste) { g_ms_debord += (d - reste) * 1e3; g_n_debords++; }   /* il a mordu sur le balayage */
            /* un aperçu exceptionnel (le premier copie toute la mémoire suivie) ne dit rien des suivants */
            cout += 0.2 * (std::min(d, 2.0 * cout + 0.001) - cout);
            k_dernier = k;
            continue;
        }
        double attente = std::min(reste - cout - 0.0015, (k_dernier + pas - k) * g_tour_duree);
        if (attente <= 0) { sortir(2, k, reste); return; }
        /* Un sommeil simple, sans la demi-milliseconde d'attente active d'attendre_jusqua : l'heure du
           prochain aperçu n'a pas à être tenue à la microseconde, et le balayage garde sa marge (au moins
           une milliseconde et demie de plus que le coût d'un aperçu). Tourner à vide avant chacun des cent
           cinquante aperçus d'une seconde prenait trois pour cent d'un cœur. */
        attente_dormir_ns((int64_t)(attente * 1e9));
    }
}

/* Hors du vol, le jeu n'attend pas : il échange son image dès qu'il l'a finie, et la console ne la montre
   qu'au balayage suivant. Publiée à l'échange, elle gagne ce balayage. */
extern "C" void video_publier_echange(const GxFrame *f) {
    if (!apercus_permis() || !f) return;
    publier(*f);
}

extern "C" void video_vblank(void) {
    if (!g_ready) return;
    if (affichage_libre()) { vblank_libre(); return; }
    if (profiling()) {
        /* Le temps écoulé depuis la fin de l'image précédente : c'est la simulation, mesurée
           et non déduite par soustraction — une déduction impute au jeu tout ce qu'on a oublié
           de chronométrer. */
        if (g_hors_rendu.time_since_epoch().count())
            g_t_sim.total += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - g_hors_rendu).count();
        g_t_image.start();
    }
    if (!g_window->pumpEvents()) { rt_log("fenêtre fermée"); exit(0); }
    appliquer_entrees(lire_entrees());
    composer_2d();
    dessiner(g_out2d, video2d_a_on_top(), g_last_3d);
    if (profiling()) {
        g_t_image.stop();
        /* le travail d'une image, attente de cadencement exclue : c'est lui qui fait l'à-coup */
        auto maintenant = std::chrono::steady_clock::now();
        double travail = g_pace_fin.time_since_epoch().count()
                         ? std::chrono::duration<double, std::milli>(maintenant - g_pace_fin).count() : 0;
        double rendu = g_t_2d.total + g_t_3d.total;
        static double rendu_prec;
        profile_frame(travail, rendu - rendu_prec);
        rendu_prec = rendu;
    }
    profile_report();
    /* L'échange des tampons n'est ni du rendu ni de la simulation : c'est le pilote graphique
       qui reprend la main, et il peut attendre. Le compter à part évite de l'imputer au jeu. */
    if (profiling()) g_t_swap.start();
    g_window->swapBuffers();
    if (profiling()) g_t_swap.stop();
    pace_frame();
    if (profiling()) g_hors_rendu = std::chrono::steady_clock::now();
}
