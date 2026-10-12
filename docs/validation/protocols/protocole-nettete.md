# Protocole — Netteté et couplage au bruit

Version 1 — 10 octobre 2026. Statut : recette proposée, validation à effectuer.
Appliquer le [socle commun](protocole-commun.md).

## Corpus / séries

0043 pour fleur/feuillage, 0327 pour roche/peau/textile, 0624 pour pelage,
9421/9734 pour bruit et peau. Ajouter rampe, impulsion et bord synthétiques.
Texture/clarté/voile à zéro ; d'abord réduction de bruit à zéro.

| Contrôle | Série | Paramètres fixes |
| --- | --- | --- |
| `sharpening-amount` | 0, 25, 50, 100, 200 | Rayon et masquage relevés dans le manifeste |
| `sharpening-radius` | 0,3 ; 1 ; 2 ; 3 | Quantité `-SharpeningBase 50`, masquage fixe |
| `sharpening-masking` | 0, 25, 50, 100 | Quantité `-SharpeningBase 50`, rayon fixe |

Sans quantité non nulle, rayon/masquage n'ont pas de résultat observable.
Le neutre général est quantité zéro ; ajouter ou identifier l'ancre à quantité
50/support identique pour comparer les deux dernières séries. Déclarer les
paramètres secondaires LR ; ni IA ni netteté de sortie.

## Mesures et critères

ROI natives adjacentes : profils de bord, amplitudes de sur/sous-oscillation,
largeur des halos, gradients/détail et bruit des aplats. Quantité zéro doit
être identité ; le rayon ne doit pas décaler l'image, le masquage doit protéger
les zones peu structurées selon le modèle. Ne pas exiger que tous les
gradients croissent avec le rayon ; séparer contraste, fréquence et clipping.
Examen pleine résolution : peau sans effet plastique/accentuation de pores
excessive, feuillage/textile sans moiré ni halos colorés, bord sans doublement.

Couplage distinct : même scène/état, quatre cas (bruit 0/netteté 0), (bruit
50/0), (0/quantité 50), (bruit 50/quantité 50), composantes de bruit déclarées.
Le catalogue génère les séries isolées, **pas cette grille combinée** : recette
IHM ou sonde à préparer. Mesurer retour du bruit, perte de détails et halos,
sans confondre baisse du proxy Haar et preuve de fidélité absolue.

Génération et [analyseur natif/JSON](../tools/analyseurs-groupes.md) disponibles
via `tests/analyse_nettete.py` : gradients et résidus dans des ROI adjacentes.
Tests `detailProcessingTests` complémentaires. Profils de bord/halos et
combinaisons demandent une recette spécifique ; le proxy bruit seul n'est
pas un score de netteté. Rapport unique Netteté, avec couplage Bruit.
Socle pour IHM, restauration, mémoire et clôture.
