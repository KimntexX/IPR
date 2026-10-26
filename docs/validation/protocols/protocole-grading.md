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
| `grading-<zone>-hue` | 0, 60, 120, 180, 240, 300° | `-GradingSaturation 25` |
| `grading-<zone>-saturation` | 0, 25, 50, 100 | `-GradingHue 30` |

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

## 2. Campagne du 25 octobre 2026 — réponse de Lightroom

Version 2. Corpus large d'office : toutes les RAW de la racine de `base` (28 scènes,
trois boîtiers). Préréglages `IPR Grading` (`scripts/make_lr_presets.py --controls
grading`) : par zone, teinte tous les 30° à saturation 50, puis saturation 10, 25, 50,
75 et 100 à 30° et 210° (20 états par zone) ; fusion 50, balance 0, luminances à 0.
Exports TIFF 16 bits réduits à 2048 pixels de grand côté dans `grading/shadows`,
`grading/midtones`, `grading/highlights`, avec le neutre de même géométrie dans
`grading/neutre` (le grading agit pixel par pixel).

- `tests/study_grading_response.py --data-version N` : modèle w(L) c(état) en Oklab par
  zone, validation croisée par scène, pic des zones contre la clé de chaque image.
- `tests/study_grading_ipr.py --data-version N [--scene ...]` : rendu IPR en mémoire
  (`iprRenderServer`, grading quelconque) contre Lightroom, même géométrie, part de
  l'effet de Lightroom inexpliquée.

## 3. Fusion et balance, puis luminance et roue Global

Mêmes corpus (28 scènes), format d'export et neutre (`grading/neutre`) que la section 2.

- **Fusion et balance** (25 octobre 2026) : préréglages `IPR Grading fusion`
  (`--controls grading-blend`, 38 états), exports dans `grading/fusion` ;
  `tests/study_grading_blend.py` (courbes de zone par valeur, additivité) et
  `tests/study_grading_ipr.py --fusion`.
- **Luminance et roue Global** (26 octobre 2026) : préréglages `IPR Grading luminance`
  (`--controls grading-luminance`, 58 états), exports dans `grading/luminance`.
  Luminance seule de chaque zone et de la roue Global à −100, −50, −25, −10, +10, +25,
  +50 et +100 ; luminance −50 / +50 avec une couleur (30°, saturation 50) pour leur
  interaction ; roue Global : teinte tous les 30° à saturation 50, saturation 25, 75 et
  100 à 30° et 210°. Fusion 50, balance 0.
