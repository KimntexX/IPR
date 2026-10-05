# Analyse du réglage Nuance vert–magenta

**Date :** 2 octobre 2026  
**Corpus :** huit scènes TIFF sRGB neutres, deux extrêmes Lightroom et deux extrêmes IPR dans `tests/image/base/nuance`.

## Méthode

Chaque variante est comparée au TIFF neutre du même moteur. La mesure est effectuée en RGB linéaire après sous-échantillonnage uniforme. L'axe `Δ log(√(R×B)/G)` est négatif vers le vert et positif vers le magenta. Les mesures brutes sont générées par `tests/analyse_tint.py` et archivées dans `mesure-nuance-brut-2026-10-02.txt`.

## État avant correctif

| Moteur | Variante verte : R / G / B | `Y` | Axe vert–magenta | Clipping | Variante magenta : R / G / B | `Y` | Axe vert–magenta | Clipping |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Lightroom | `0,482 / 1,207 / 0,435` | `0,991` | `-1,726` | `3,708 %` | `1,646 / 0,676 / 2,207` | `0,992` | `1,527` | `16,382 %` |
| IPR | `0,809 / 1,920 / 0,774` | `1,600` | `-1,029` | `4,250 %` | `1,391 / 0,506 / 1,381` | `0,755` | `1,057` | `4,000 %` |

Le sens est correct dans les deux cas. L'amplitude IPR n'atteint toutefois qu'environ `60 %` (vert) et `69 %` (magenta) de Lightroom. Surtout, l'ancienne implémentation fait varier la luminance de `+60,0 %` vers le vert et `-24,5 %` vers le magenta, là où Lightroom reste quasi neutre (`-0,9 %` et `-0,8 %`).

## Correctif v1 appliqué

Les gains RGB de Nuance sont maintenant normalisés par leur gain de luminance Rec. 709 sur un gris neutre. Cette normalisation conserve exactement l'axe vert–magenta tout en annulant le changement d'exposition sur ce gris ; elle est couverte par `colorPipelineTests`.

Les seize TIFF IPR de l'état initial sont archivés dans `tests/image/base/nuance/archive-ipr-pre-correction-v1-2026-10-02`. Les huit scènes doivent être réexportées aux deux extrêmes pour valider la luminance et décider ensuite si l'amplitude chromatique nécessite un calibrage distinct.

## Vérification v2 — validation fonctionnelle et globale

Les seize TIFF IPR régénérés après normalisation ont été mesurés le 2 octobre 2026 :

| Moteur | Variante verte : R / G / B | `Y` | Axe vert–magenta | Clipping | Variante magenta : R / G / B | `Y` | Axe vert–magenta | Clipping |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Lightroom | `0,4821 / 1,2068 / 0,4352` | `0,9910` | `-1,7258` | `3,708 %` | `1,6464 / 0,6755 / 2,2067` | `0,9916` | `1,5266` | `16,382 %` |
| IPR v2 | `0,4629 / 1,2166 / 0,4519` | `1,0026` | `-1,1042` | `1,636 %` | `1,8056 / 0,6810 / 1,7583` | `0,9958` | `1,0365` | `5,770 %` |

La dérive d'exposition est corrigée : l'écart de luminance IPR/Lightroom est de `+1,2 %` vers le vert et `+0,4 %` vers le magenta, au lieu de `+60,0 %` et `-24,5 %`. Les gains moyens verts sont également proches de la référence ; le clipping IPR est plus protecteur.

Les signes sont corrects, mais l'axe local vert–magenta atteint `64,0 %` de Lightroom vers le vert et `67,9 %` vers le magenta. La Nuance est donc **validée fonctionnellement et globalement** (direction, stabilité de luminance, absence de régression de clipping), mais pas à parité locale stricte avec Lightroom. Comme pour Température, les moyennes déjà proches rendent contre-productif un nouveau gain RGB global. Une parité plus fine demanderait un réglage dans un espace perceptuel (par exemple une translation contrôlée dans CIE Lab/OKLab, avec gestion de gamut), à étudier dans le chantier colorimétrie. La mesure brute v2 est conservée dans `mesure-nuance-v2-brut-2026-10-02.txt`.
