# Analyse — Couleur du point

Rapport évolutif de la couleur du point. Protocole :
[Couleur du point](../protocols/protocole-couleur-point.md). Les JSON sous
`docs/validation/raw/` font autorité ; ce rapport les interprète. Comparaison en
boîte noire sur des exports Lightroom, sans rétro-ingénierie.

## Campagne du 27 octobre 2026

Six modèles enregistrés dans Lightroom par l'utilisateur (sidecars de 6V1A0327,
9539, 0064, 0032, 9112 et 9141, valeurs repères 11/22/33/44/66), 66 préréglages
(26 états pour 0327, 8 pour chacune des cinq autres sources), 1 848 exports sur
les 28 scènes du neutre `grading/neutre`. 14 exports portent l'état d'un autre
préréglage que celui attendu (préréglage non appliqué avant export) : l'étude lit
l'état inscrit dans chaque TIFF, donc « 0327 L+25 » compte 15 scènes et
« 0064 T+50 » 27 ; rien n'est faussé.

## Ce que montrent les exports

- **Codage** : `crs:PointColors` porte 19 nombres par point, dont la source en
  trois nombres ; la variance est dans `crs:ColorVariance`. Sur les six sources,
  ces trois nombres correspondent à la teinte (radians), la saturation
  (max − min)/max et la valeur max du **ProPhoto linéaire**, mesurées sur les
  pixels prélevés.
- **Espace de travail** : saturation −100 rapproche la couleur du gris de même
  valeur maximale (clarté atteinte retrouvée à 0,001–0,003 près sur les six
  sources ; moyenne, TSL ou luminance s'en écartent nettement). Lightroom ne va
  pas jusqu'au gris : il garde 52,6 % de la saturation, identique sur cinq
  sources sur six.
- **Teinte** : +50 tourne la teinte TSV de 0,305 rad sur les six sources ;
  réponse linéaire, ±0,61 rad à ±100.
- **Luminance** : agit sur la valeur seule (log de la valeur +0,42 à +50 pour
  0327, saturation presque inchangée).
- **Voisinage** : un trapèze par axe (teinte, saturation, valeur^(1/2,2)), dont le
  plateau s'élargit avec la gamme, surtout de 0 à 50. Le produit des trois axes
  et une distance commune se valent ; le plus grand écart est moins bon
  (`etude-couleur-point-forme-v001`).

Mémoires : `etude-couleur-point-v001` (vide : hypothèse « −100 rend gris »
fausse), `v002` (Oklab, amplitude mesurée), `etude-couleur-point-forme-v001`,
`etude-couleur-point-tsv-v001` (trapèzes et réponses en TSV).

## IPR face à Lightroom

Rendu en mémoire (`tests/study_point_color_ipr.py`), 10 scènes, 66 états : part
du changement Oklab de Lightroom non expliquée par celui d'IPR, et rapport des
amplitudes.

| | v001 (Oklab) | v002 (TSV ProPhoto) |
| --- | --- | --- |
| Teinte | 0,50 | 0,14 |
| Saturation | 0,43 | 0,19 |
| Luminance | 0,22 | 0,17 |
| Gamme | 0,52 | 0,25 |
| Variance | 0,79 | 0,53 |
| Amplitude IPR/LR | 0,65–0,77 | 1,03–1,09 |

Par source (v002) : 0064 0,12 ; 9539 0,15 ; 0327 0,17 ; 9112 0,40 ; 0032 0,46 ;
9141 0,56. Les 90ᵉ centiles restent élevés (0,4 à 2,4) : sur les scènes où
Lightroom ne change presque rien, un faible changement d'IPR pèse lourd dans ce
rapport. Mémoires : `etude-couleur-point-ipr-v001`, `-v002`.

## Limites et suite

- Réponses des curseurs mesurées sur 0327 seule (sauf teinte, identique partout) ;
  variance la moins bien reproduite.
- Trapèzes médians des six sources : le bleu 9141, le vert sombre 0032 et le
  rouge 9112 s'en écartent le plus ; examiner leur forme avant une loi par
  source.
- Non mesurés : sous-gammes dépliées, deux points qui se recouvrent, visualisation
  des plages ; pas de validation visuelle utilisateur.
- IHM raccordée le 28 octobre (pipette, pastilles, curseurs ; IHM-CONN-011) ; la
  visualisation des plages reste à faire.
