"""Fabrique des commandes pour que le jeu s'explore tout seul.

Le dictionnaire des types se construit en regardant le jeu vivre : un écran jamais ouvert n'a jamais
créé ses objets, et rien de ce qu'il contient n'entre dans le relevé. Plutôt que de demander à
quelqu'un d'ouvrir chaque écran, on écrit ses commandes — une ligne par image, comme un enregistrement.

    image  touches  stylet  x  y

Les touches sont le masque de la console : A=1, B=2, Select=4, Start=8, droite=16, gauche=32,
haut=64, bas=128, R=256, L=512, X=1024, Y=2048.

L'exploration est volontairement bête : elle valide, recule, se promène dans les listes et touche
l'écran du bas à des endroits variés. Elle n'essaie pas de jouer — elle essaie d'ouvrir des portes.

    python tools/explorer.py --images 20000 --graine 1 --sortie build/explore_1.txt

Sous licence MIT.
"""
import argparse
import random
from pathlib import Path

A, B, SELECT, START = 1, 2, 4, 8
DROITE, GAUCHE, HAUT, BAS = 16, 32, 64, 128
R, L, X, Y = 256, 512, 1024, 2048

# Ce qu'on tente, et à quelle fréquence. Valider domine : c'est ce qui ouvre les écrans.
GESTES = (
    [(A, 22)] * 5 + [(START, 6)] * 2 + [(B, 10)] * 2 +
    [(BAS, 8), (HAUT, 8), (DROITE, 8), (GAUCHE, 8), (X, 6), (Y, 6), (L, 6), (R, 6), (SELECT, 6)]
)


def ecrire(chemin, images, graine, touches_par_geste=None):
    al = random.Random(graine)
    lignes = []
    image = 1
    while image <= images:
        # un geste, tenu quelques images, puis un temps mort pour laisser l'écran répondre
        touches, duree = al.choice(GESTES)
        duree = max(2, duree + al.randint(-2, 6))
        repos = al.randint(8, 40)
        # de temps en temps, le stylet : beaucoup d'écrans de ce jeu ne répondent qu'à lui
        stylet = al.random() < 0.25
        tx, ty = al.randint(16, 240), al.randint(16, 176)
        for _ in range(duree):
            if image > images:
                break
            lignes.append(f"{image} {touches} {1 if stylet else 0} {tx} {ty}")
            image += 1
        for _ in range(repos):
            if image > images:
                break
            lignes.append(f"{image} 0 0 {tx} {ty}")
            image += 1
    Path(chemin).write_text("\n".join(lignes) + "\n", encoding="utf-8")
    return len(lignes)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--images", type=int, default=20000)
    ap.add_argument("--graine", type=int, default=1)
    ap.add_argument("--sortie", default="build/explore.txt")
    a = ap.parse_args()
    n = ecrire(a.sortie, a.images, a.graine)
    print(f"{n} images de commandes -> {a.sortie} (graine {a.graine})")


if __name__ == "__main__":
    main()
