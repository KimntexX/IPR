# Analyse — Courbes de tonalité

Rapport évolutif des courbes par points. Protocole :
[Courbes](../protocols/protocole-courbes.md). Les JSON sous `docs/validation/raw/`
font autorité ; ce rapport les interprète. Comparaison en boîte noire sur des
exports Lightroom, sans rétro-ingénierie.

## Campagne du 28 octobre 2026

36 préréglages `IPR Courbes` (une courbe modifiée par état, les autres écrites
linéaires) appliqués aux 28 scènes de la base : 1 008 exports TIFF 16 bits de
2048 pixels de grand côté dans `courbes`, neutre de même géométrie repris de
`grading/neutre`. L'étude lit les courbes inscrites dans chaque TIFF
(`crs:ToneCurvePV2012`, `…Red`, `…Green`, `…Blue`, points en 0–255).

## Ce que montrent les exports

`tests/study_curves.py` prédit chaque export depuis son neutre par la courbe
modifiée, pour trois espaces, quatre interpolations et deux modes, et mesure la
part du changement de Lightroom non expliquée
([etude-courbes-v001](../raw/etude-courbes-v001-20261028T125648Z.json)) :

| Combinaison | Courbe RVB (médiane / p90) | Courbes de canal |
| --- | --- | --- |
| ProPhoto linéaire encodé sRGB, spline naturelle, par canal | **0,004 / 0,04** | **0,014 / 0,11** |
| sRGB encodé, spline naturelle | 0,013 / 0,10 | — |
| ProPhoto encodé sRGB, cubique monotone (PCHIP) | 0,014 / 0,06 | 0,026 / 0,15 |
| ProPhoto encodé sRGB, Lagrange | 0,019 / 0,08 | 0,030 / 0,21 |
| ProPhoto encodé sRGB, courbe RVB sur le canal maximal | 0,028 / 0,14 | — |

Lightroom applique donc ses courbes par points **sur chaque canal du ProPhoto
linéaire encodé avec la courbe de transfert sRGB**, par **spline cubique
naturelle**, la courbe RVB sur les trois canaux puis la courbe du canal. Seul le
rouge fortement abaissé (point milieu à 64) reste mal prédit (médiane 0,57) : les
couleurs poussées hors du sRGB sont écrêtées dans les exports, ce que la
prédiction en ProPhoto ne reproduit pas ; IPR, lui, le suit bien (ci-dessous).

## IPR face à Lightroom

IPR appliquait un polynôme de Lagrange sur le Rec.2020 linéaire (point milieu
déjà très clair), la première courbe sur la luminance. Le moteur suit désormais
le modèle mesuré (plat au-delà des extrémités, pente de fin au-delà de 1 pour les
hautes lumières). Rendu en mémoire (`tests/study_curves_ipr.py`), 10 scènes,
36 états ([etude-courbes-ipr-v001](../raw/etude-courbes-ipr-v001-20261028T131723Z.json)) :

| | Poolée | Médiane | p90 | Amplitude IPR/LR |
| --- | --- | --- | --- | --- |
| Courbe RVB (18 états) | 0,029 | 0,032 | 0,19 | 1,00 |
| Courbes de canal (18 états) | 0,026 | 0,033 | 0,18 | 1,00 |

États les moins bien suivis : S doux et S inverse (0,12–0,13 poolée), RVB milieu
à 112 (0,10), bleu milieu à 160 (0,11). Le rouge milieu à 64 tombe à 0,04.

## Limites et suite

- Une seule courbe modifiée par état : l'ordre RVB puis canal et l'interaction
  avec les autres réglages ne sont pas mesurés.
- Les extrémités se déplacent dans l'IHM comme dans Lightroom (noir relevé,
  blancs coupés) depuis le 28 octobre ; le graphique est plus haut que large.
- La courbe paramétrique de Lightroom n'existe pas dans IPR.
