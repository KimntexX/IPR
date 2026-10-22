# Protocole — Color grading

Version 1 — 10 octobre 2026. Statut : recette proposée, validation à effectuer.
Appliquer le [socle commun](protocole-commun.md).

## Corpus et états

Rampe grise continue + paliers gris, 0007 pour faibles chromas, 0327 pour
couleurs/peau, 9944/9947 pour transitions en contre-jour. Mélangeur, courbes
et couleur globale neutres. Tester **ombres, tons moyens, hautes lumières**
indépendamment ; autres zones à saturation zéro.

| Série générateur | Valeurs | Support fixe |
| --- | --- | --- |
| `grading-<zone>-hue` | 0, 60, 120, 180, 240, 300° | Saturation fixe 25 (pas une option CLI) |
| `grading-<zone>-saturation` | 0, 25, 50, 100 | Teinte fixe 30° (pas une option CLI) |

Zones : `shadows`, `midtones`, `highlights`. Le groupe `grading` couvre ces
séries. Pour comparer les teintes, utiliser aussi l'état de support commun
à saturation 25 : le neutre général est sans grading. Saturation zéro doit
être neutre quelle que soit la teinte enregistrée.

Compléments IHM/moteur : voisinage 359°/0° pour la circularité, mouvement
continu du pointeur sans perte de capture, combinaisons de deux puis trois
zones à couleurs opposées. Le générateur ne réalise pas ces combinaisons
arbitraires ; les enregistrer manuellement ou préparer une sonde dédiée.

## Mesures et décision

Sur la rampe : variation RGB/Y/chroma selon Y initial, profil de l'effet,
continuité entre zones, stabilité aux changements infinitésimaux et clipping.
Sur photos : masques tonals fixes du neutre, teinte circulaire seulement sur
chroma suffisante, peau, ombres/HL saturées et absence de halos/bandes.
Une variation de Y n'est pas automatiquement une erreur : expliciter la cible
du modèle courant et contrôler ses limites. Les supports LR peuvent différer,
donc un même angle/saturation ne démontre pas une parité.

Recette reset, sélection des trois zones, conservation des autres valeurs,
roues/champs de saisie, restauration et aperçu/export. L'[analyseur natif/JSON](../tools/analyseurs-groupes.md)
`tests/analyse_grading.py` mesure zones tonales et teinte/chroma ; circularité
359°/0°, supports LR et tolérances restent à contrôler.
Rapport unique Grading à créer ; socle pour mémoire et nettoyage.
