/* Les mods du lanceur (onglet Mods) : des crochets posés aux points de modification (modifications.c),
   éteints par défaut. Sous licence MIT. */
#pragma once
#include "natif.h"

#ifdef __cplusplus
extern "C" {
#endif

/* lit les réglages SFC_MOD_… et dit dans le journal ce qui est allumé */
void mods_lire(void);
/* ce crochet appartient-il à un mod allumé ? */
int mods_veut(NativeHook crochet);
/* la manette décompte ses bombes elle-même : elle demande */
int mods_bombes_infinies(void);

void mod_laser_tir(Cpu *c);
void mod_laser_cadence(Cpu *c);
void mod_laser_son(Cpu *c);
void mod_laser_briefing(Cpu *c);
void mod_verrou_vehicule(Cpu *c);
void mod_verrou_cockpit_r4(Cpu *c);
void mod_verrou_cockpit_r0(Cpu *c);
void mod_jauge(Cpu *c);
void mod_vaisseau_chargement(Cpu *c);
void mod_vaisseau_pieces(Cpu *c);
void mod_turbo_depense(Cpu *c);
void mod_bombes_r0(Cpu *c);
void mod_bombes_r1(Cpu *c);
void mod_charge_max(Cpu *c);
void mod_degats_recus(Cpu *c);
void mod_degats_infliges(Cpu *c);

#ifdef __cplusplus
}
#endif
