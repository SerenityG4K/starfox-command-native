# Format `.bmg` — les dialogues du jeu

66 archives de messages au format Nintendo `MESGbmg1`, soit **38 415 messages**
non vides : tout le scénario, les répliques de combat, les écrans de stratégie,
les noms d'ennemis et les textes publicitaires, en cinq langues.

```bash
python tools/bmg.py assets/cooked -o assets/text                        # tout extraire
python tools/bmg.py --texte battle_Fr.bmg -o battle_Fr.json               # un fichier, à modifier
python tools/bmg.py --reecrire battle_Fr.bmg battle_Fr.json -o neuf.bmg   # le réécrire
```

Les fichiers produits (`assets/text/<Langue>/<archive>.json`) contiennent du
texte protégé : ils restent hors du dépôt, comme tout ce qui sort de la ROM.
Quand une archive range deux fois le même nom — `Fr_child` a
`2D/MessageText/battle_Fr.bmg` et `Fr/2D/MessageText/battle_Fr.bmg` —, ce qui
précède `2D/` s'ajoute au chemin (`Fr_child/Fr/battle_Fr.json`).

Pour modifier un texte dans le jeu, voir [la procédure](assets.md).

## Où sont les textes

| Emplacement | Contenu |
|---|---|
| `_srl/<Langue>/2D/MessageText/scenario_<L>.bmg` | le scénario : 1 652 messages par langue |
| `…/battle_<L>.bmg` | les répliques de combat : 610 |
| `…/strategy_<L>.bmg` | l'écran de stratégie : 521 |
| `…/enemy_<L>.bmg`, `…/advertise_<L>.bmg` | noms d'ennemis, textes de démonstration |
| `_srl/<Langue>_child/…` | 2 456 messages par langue, un sous-ensemble |
| `2D/MessageText/*_Du.bmg` | variante à la racine (3 004 messages) |
| `corneria.bmg` | 23 messages gros-boutistes en Shift-JIS : vestige de développement |

Les langues sont `Fr`, `De`, `Es`, `It` et `Uk` ; chaque archive `.srl` de
langue est une image ROM montée par le jeu, comme celles des planètes.
**Le jeu en français lit `Fr.srl`** : une réplique de combat changée dans
chacun des huit exemplaires de `battle_Fr.bmg` (dans `Fr.srl`, `Fr_child.srl`
deux fois, `child.srl` et les autres `_child.srl`) s'est affichée, dans la
partie `obs52` rejouée, avec le texte de celui de `Fr.srl`.

## Structure

| Zone | Contenu |
|---|---|
| en-tête, 32 octets | magie `MESGbmg1`, taille du fichier, nombre de sections, encodage à l'octet 16 |
| `INF1` | en-tête de 16 octets — étiquette, taille, nombre de messages (`u16`), taille d'entrée (`u16`), 4 octets —, puis une entrée par message dont les 4 premiers octets sont l'offset dans `DAT1` |
| `DAT1` | les chaînes, terminées par un caractère nul ; la première, vide, sert aux messages vides |

Les sections sont complétées à un multiple de 32 octets. Un message se désigne
par son rang dans `INF1`, à partir de 0 : c'est l'`id` des fichiers JSON.
(Jusqu'au 29/09/2026, l'extracteur lisait les entrées 16 octets trop loin : il
sautait les quatre premiers messages et numérotait les autres quatre trop bas.
Les JSON produits avant cette date sont à refaire.)

Encodages : `1` CP1252, `2` UTF-16, `3` Shift-JIS, `4` UTF-8. Les archives de
langue sont en **UTF-16 petit-boutiste** ; `corneria.bmg`, produit par une
chaîne d'outils GameCube, est **gros-boutiste** — le boutisme se détecte en
comparant le champ de taille à la taille réelle du fichier.

### Séquences de contrôle

Le caractère `0x1A` ouvre une séquence : sa longueur totale en octets, un
groupe, un type sur `u16`, puis des arguments. Elles portent les couleurs, les
pauses, l'insertion du nom du joueur. L'extracteur les conserve telles quelles
sous la forme `{groupe:type:arguments}` pour ne rien perdre, et la réécriture
les rend à l'identique ; leur sémantique reste à établir en confrontant les
occurrences à ce que le jeu affiche.

## Réécrire

`--reecrire` part du fichier d'origine et ne change que les messages du JSON
dont le texte diffère : le nouveau texte est ajouté à la fin de `DAT1`, et
l'entrée du message le désigne. Tout le reste est gardé octet pour octet — un
aller-retour sans changement redonne les 53 fichiers des archives octet pour
octet, et les 175 messages à séquences de contrôle se relisent à l'identique.
Seuls s'affichent les caractères que la police du jeu (`.nftr`) connaît : les
lettres accentuées du français, oui ; le `°`, non.

## Pour la suite de l'histoire

Les répliques sont adressées par leur index dans l'archive ; ce sont ces index
que les commandes de script `MemberTalks`, `PutMessage` et `SetBattleMessage`
citent. Écrire un nouveau chapitre reviendra à ajouter des messages à
`scenario_<L>.bmg` et à les appeler depuis de nouveaux scripts de mission ;
l'ajout de messages (et non leur seul remplacement) reste à écrire.
