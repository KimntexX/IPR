# Rapport — effets

Date : 30 septembre 2026

## Réglages livrés

La section **Effets** du panneau de réglages comporte quatre traitements
non destructifs. Ils utilisent la même image de travail linéaire que l'aperçu
et les exports.

| Réglage | Plage | Principe | Effet principal |
|---|---:|---|---|
| Texture | -100 à +100 | contraste local à petit rayon | micro-détails |
| Clarté | -100 à +100 | contraste local à grand rayon, protégé dans les extrêmes | relief des tons moyens |
| Correction du voile | -100 à +100 | canal sombre local et transmission atmosphérique bornée | contraste de scène voilée |
| Grain | 0 à 100 | bruit monochrome déterministe, pondéré dans les ombres | texture photographique |

## Ordre du pipeline

Texture, clarté et correction du voile sont appliqués après les réglages
tonals et de couleur, mais avant les courbes. Le grain est appliqué après le
flou gaussien : un flou demandé par l'utilisateur ne ramollit donc pas
artificiellement le grain.

Les effets de texture et de clarté modifient une luminance cible, puis
appliquent le même ratio aux canaux RGB ; cette méthode préserve la teinte.
La clarté réduit son influence dans les noirs et les blancs pour limiter les
halos. La correction du voile est volontairement globale et bornée : elle
améliore un voile homogène, sans prétendre segmenter la profondeur de la
scène.

## Stabilité et validation

Le grain repose sur un hachage des coordonnées de pixel, pas sur un générateur
aléatoire par rendu. L'image ne scintille donc ni lors des aperçus asynchrones,
ni entre aperçu et export.

`imageExportTests` génère un motif synthétique comportant gradient, détails
fins et voile coloré. Il vérifie que chacun des quatre réglages modifie
effectivement l'export PNG.
