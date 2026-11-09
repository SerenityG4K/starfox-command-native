/*
 * Entre deux images du jeu : apparier leurs polygones, et en tirer l'image d'un instant intermédiaire.
 *
 * Le jeu pense à son rythme — trente fois par seconde en vol — et l'affichage libre montre autant
 * d'images que la machine en produit (docs/plan_framerate.md). Ce module ne connaît ni OpenGL ni la
 * fenêtre : il reçoit deux listes de polygones en repère de découpage et rend celle d'entre les deux.
 *
 * Sous licence MIT.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gx_geom.h"

namespace sfc {

/* De l'image A (l'ancienne) à l'image B (la récente). */
struct Lien {
    std::vector<int> pareil;           /* pour chaque polygone de B : son rang dans A, ou -1 */
    std::vector<int> disparus;         /* ceux de A que personne n'a pris, par rang croissant */
    std::vector<uint8_t> par_voisinage; /* pour chaque polygone de B : apparié sans identité commune, par sa seule place */
    /* Les paires dont la forme ne doit pas être interpolée : d'une image à l'autre, leurs arêtes ne
       changent pas toutes dans le même rapport. Un rapprochement ou une rotation les change ensemble ;
       un pareil qui n'est pas le bon les change chacune de son côté, et le chemin entre les deux
       formes est une figure que le jeu ne montre jamais. Celles-là glissent sans se déformer. */
    std::vector<uint8_t> raide;
    std::vector<uint8_t> etage;        /* pour chaque polygone de B : l'étage qui l'a apparié (1 objet, 2 forme, 3 voisinage), 0 sinon */
    uint32_t serie_a = 0, serie_b = 0; /* les numéros des deux images 3D, pour les journaux */
    /* Les objets reconnus rang par rang bien que certains de leurs polygones aient changé de forme sur
       place (les bras du vaisseau mère, dont les panneaux s'allument et s'éteignent) : pour chacun, son
       propre mouvement d'une image à l'autre, et pour chaque polygone l'objet auquel il appartient. Un
       polygone qui a changé de forme ne se déforme pas : il passe de l'une à l'autre à mi-chemin, porté
       par le mouvement de son objet — et non par celui de la caméra. */
    std::vector<uint8_t> change;       /* pour chaque polygone de B : apparié par son rang, mais sa forme a changé */
    std::vector<int> objet_a, objet_b; /* l'objet réaligné de chaque polygone, -1 sinon */
    std::vector<std::vector<double>> objet_avant, objet_arriere;   /* 16 valeurs : de A vers B, de B vers A */
    size_t realignes = 0;              /* objets reconnus rang par rang */
    /* B est un tour d'aperçu, calculé d'avance. Il prédit bien le mouvement, mal ce que le jeu anime par
       l'interruption du balayage, qu'un aperçu ne joue pas : l'anneau du vaisseau mère y garde un tour de
       retard. Un polygone qui change de forme vers un aperçu garde donc la sienne ; il ne change que
       quand la vraie image du jeu arrive, comme sur la console. (Posé par l'appelant.) */
    bool b_apercu = false;
    size_t apparies = 0;               /* …dont `seconds` par le voisinage seul (seconde passe) */
    size_t seconds = 0;
    bool coupure = false;              /* deux scènes différentes : on ne mélange pas */
    /* Le mouvement de la caméra, par projection : ce qui ne bouge pas dans le monde passe de A à B
       par `avant`, de B à A par `arriere` — en repère de découpage, vecteur ligne × matrice. Un
       polygone sans pareil le suit : il ne saute plus avec la caméra, il reste où il est. */
    double avant[GX_PROJS_MAX][16];
    double arriere[GX_PROJS_MAX][16];
    /* Les projections à plat — l'interface, les boîtes de dialogue, les flèches. Ce qu'elles placent est du
       dessin au pixel : entre deux images du jeu il reste sur la grille des pixels de la console. */
    uint8_t a_plat[GX_PROJS_MAX] = {};
    /* pour les mesures */
    size_t sures = 0, retenues = 0;    /* sommets des paires certaines, et ceux que le mouvement explique */
    size_t raides = 0;                 /* combien de paires ne voient pas leur forme interpolée */
    double residu = 0;                 /* leur écart médian, relatif à la distance */
};

Lien apparier(const GxFrame &fa, const std::vector<GxPolygon> &a, const GxFrame &fb, const std::vector<GxPolygon> &b);

/* L'image de l'instant t, de 0 (A) à 1 (B). */
void interpoler(const std::vector<GxPolygon> &a, const std::vector<GxPolygon> &b, const Lien &lien, float t,
                std::vector<GxPolygon> &out);

/* Le chemin entre deux images, pour le GPU (video.cpp). `interpoler` rend l'image d'un instant, sur le
   processeur ; le plan dit, une fois pour la paire d'images, tout ce qu'il faut pour que le nuanceur de
   géométrie rende celle de n'importe quel instant — par les mêmes règles, dans le même ordre, aux mêmes
   seuils. Ce qui ne dépend que des deux images (voisinage, forme raide, taille qui change vite, texture
   qui défile, sens des faces) se décide ici ; ce qui dépend de l'instant (mi-chemin, prédiction,
   retournement, pixel) reste au nuanceur.

   Chaque rang de la liste d'`interpoler` y devient un polygone à trajet, ou deux quand la forme montrée à
   un instant ne porte pas l'état — texture, attributs — du polygone de B : celui qui porte l'état de A
   n'est dessiné que quand c'est sa forme qui se montre. */
enum : uint32_t {
    PLAN_CHEMIN      = 1u << 0,   /* un trajet tout tracé, de p0 à p1 selon t (sans pareil, disparu, changé de
                                     forme) ; sinon un pareil, o en p0 et q en p1, avec toutes ses règles */
    PLAN_VOISIN      = 1u << 1,   /* apparié par le seul voisinage : on ne le prolonge pas */
    PLAN_A_PLAT      = 1u << 2,   /* projection à plat : au pixel, et pas de prolongement */
    PLAN_GROSSIT     = 1u << 3,   /* change vite de taille : la prédiction ne prolonge que son déplacement */
    PLAN_RAIDE       = 1u << 4,   /* sa forme ne s'interpole pas : elle glisse */
    PLAN_UV          = 1u << 5,   /* ses coordonnées de texture s'interpolent */
    PLAN_RETOURNABLE = 1u << 6,   /* il regarde du même côté dans les deux images : l'interpolation ne doit pas le retourner */
    PLAN_SENS_O      = 1u << 7,   /* ce côté-là, dans l'image A : aire positive */
    PLAN_ETAT_A      = 1u << 8,   /* il se dessine quand la forme montrée est celle de A (raide ou retourné avant mi-chemin) */
    PLAN_ETAT_B      = 1u << 9,   /* …quand c'est une autre */
    PLAN_QUATRE      = 1u << 10,  /* quatre sommets */
    PLAN_AVANT_SEUL  = 1u << 11,  /* trajet d'un polygone changé de forme : la forme d'avant, jusqu'à mi-chemin */
    PLAN_APRES_SEUL  = 1u << 12,  /* …la nouvelle, ensuite */
    PLAN_B_APERCU    = 1u << 13,  /* B est un aperçu : la forme d'avant tient jusqu'au bout */
};

struct PolyPlan {
    const GxPolygon *etat;         /* le polygone dont il porte l'état : texture, attributs, translucidité */
    uint8_t ancienne;              /* ses textures viennent de l'image A */
    uint32_t drapeaux;
    int32_t p0[4][4], p1[4][4];    /* les sommets aux deux bouts, en repère de découpage (20.12) — entiers comme
                                      ceux du jeu : le nuanceur les interpole au bit près d'`interpoler` */
    uint8_t c0[4][3], c1[4][3];    /* leurs couleurs */
    int16_t s0[4], t0[4], s1[4], t1[4];   /* leurs coordonnées de texture */
    float ds, dt;                  /* un pareil : le décalage qui évite de rembobiner une texture qui défile (12.4) */
    float delta[4];                /* un pareil : le déplacement de son centre, de A à B (20.12) */
};

void planifier(const std::vector<GxPolygon> &a, const std::vector<GxPolygon> &b, const Lien &lien,
               std::vector<PolyPlan> &out);

extern uint64_t g_blocs_ambigus;
extern uint64_t g_retournes;
void noter_presence(const std::vector<GxPolygon> &polys);   /* les blocs refusés faute d'être francs */

}  // namespace sfc
