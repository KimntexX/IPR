# Résumé de validation de la release

État au 1er novembre 2026, version `v0.1.86-alpha`. Ce résumé dit ce qui est
validé dans IPR et dans quelles limites. Les mesures, analyses et preuves
complètes restent sur la branche `dev`, au commit dont cette version est issue
(voir `release-manifest.json` à la racine) ; les liens ci-dessous y mènent.

## Comment IPR est validé

- **Parité avec Lightroom** : pour chaque réglage, IPR et Lightroom rendent les
  mêmes photos de référence aux mêmes valeurs ; l'écart est mesuré, puis
  l'utilisateur compare visuellement. Lightroom sert de repère, pas de cible :
  certains écarts sont des choix techniques assumés.
- **Validation visuelle de l'utilisateur** pour chaque réglage, avant sa
  livraison.
- **Tests automatiques** sur Windows, Ubuntu et macOS (arm64 et Intel) à chaque
  intégration, et audit du code (novembre 2026).

Le détail réglage par réglage est dans la
[matrice de validation](https://github.com/KimntexX/IPR/blob/72c57741011605dd65f9ad236f16b6683dd1492a/docs/validation/matrice-validation.md).

## Boîtiers couverts

Canon EOS R6 Mark III et EOS R7 (CR3), Canon EOS 60D (CR2), développés par IPR
lui-même (LibRaw reste en secours). Les autres boîtiers s'ouvrent par LibRaw,
sans validation dédiée.

## Réglages validés

| Groupe | Réglages | Réserves |
|---|---|---|
| Lumière | Exposition, contraste, ombres, hautes lumières, noirs, blancs | Exposition non mesurée au-delà de ±2 EV ([#11](https://github.com/KimntexX/IPR/issues/11)) ; pivot du contraste propre à chaque image chez Lightroom ([#12](https://github.com/KimntexX/IPR/issues/12)) |
| Balance des blancs | Température, nuance | Bleus extrêmes vers 2000 K ([#10](https://github.com/KimntexX/IPR/issues/10)) |
| Couleur | Vibrance, saturation, mélangeur TSL, étalonnage des couleurs, couleur du point, courbes | Grands ciels bleus saturés à +100 (vibrance) |
| Effets | Texture, clarté, correction du voile, grain, flou | Halos de clarté en contre-jour ; voile sur les scènes nocturnes |
| Détail | Réduction du bruit, netteté | IPR retire moins de bruit de luminance que Lightroom et garde plus de détail (choix technique) ; images très bruitées à améliorer ([#14](https://github.com/KimntexX/IPR/issues/14)) ; réglage Détail de la netteté ([#13](https://github.com/KimntexX/IPR/issues/13)) |
| Optique | Correction d'objectif, aberration chromatique latérale | Intensité non réglable |
| Base | Profil Camera Neutral à l'ouverture | Fidélité à Lightroom non mesurée |

## Flux de travail validés

Application de réglages et export par lot, sélection dans le menu du clic
droit, actualisation du dossier ouvert, réglages enregistrés dans les fichiers
`.ipr` (les anciennes versions restent lues), export JPEG, PNG, TIFF et DNG
linéaire. Le DNG est lu par Lightroom ; Photoshop et DxO ne sont pas vérifiés
([#9](https://github.com/KimntexX/IPR/issues/9)).

## Limites connues

- Au-delà de 100 %, le zoom agrandit l'aperçu de 2048 px au lieu de l'image en
  pleine résolution.
- Les messages de l'histogramme restent techniques ([#17](https://github.com/KimntexX/IPR/issues/17)).
- La validation porte sur les boîtiers ci-dessus, pas sur tous ceux du marché.
