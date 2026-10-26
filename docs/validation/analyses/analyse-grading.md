# Color grading — rapport consolidé

## Bilan actuel — 25 octobre 2026

**Dernier résultat :** le color grading d'IPR suit maintenant la réponse mesurée de
Lightroom. Ses trois zones sont des courbes de la clarté Oklab ; chaque teinte a sa
couleur et son effet de clarté, et la force est proportionnelle à la saturation. Dans
le rendu réel, la part de l'effet de Lightroom laissée inexpliquée vaut, en médiane,
0,075 dans les ombres, 0,071 dans les tons moyens et 0,129 dans les hautes lumières.
L'ancienne implémentation, jamais calée sur Lightroom, était à 14,7, 0,76 et 0,99.
Fusion, balance, luminance des zones et roue Global suivent aussi Lightroom (médiane 0,076 et
0,017). Validation visuelle de l'utilisateur à venir ([conclusion](#conclusion-actuelle)).

Protocole : [protocole Grading](../protocols/protocole-grading.md), section 2. Branche :
`patch_color`.

<a id="grading-20261025-campagne"></a>

## Campagne du 25 octobre 2026

### 1. Corpus et exports

Corpus large dès le départ, suivant la règle de l'utilisateur (corpus larges, points de
comparaison nombreux, adaptation par image à vérifier) : **28 scènes**, toutes les RAW à
la racine de `base`, sur les trois boîtiers (R6 Mark III, R7, 60D), des scènes très
sombres (IMG_7958, médiane −10,5 IL) aux scènes brumeuses (IMG_6954), des plus
saturées (6V1A9141, IMG_7953) à la chair (IMG_7423, IMG_3135, IMG_4002).

Préréglages `IPR Grading` (`scripts/make_lr_presets.py --controls grading`) : par zone, un
tour de teinte tous les 30° à saturation 50 et la saturation 10, 25, 50, 75, 100 à 30° et
210°, soit 20 états par zone, plus le neutre. Le grading agit pixel par pixel : exports
TIFF 16 bits réduits à 2048 pixels de grand côté, avec un neutre de la même géométrie
(`grading/neutre`). 1 680 états et 28 neutres, vérifiés (réglages intégrés, format,
géométrie) ; 26 doublons d'un export répété supprimés, deux états manquants réexportés.

### 2. Réponse de Lightroom

`tests/study_grading_response.py`,
[etude-grading-v001](../raw/etude-grading-v001-20261025T173113Z.json). Le changement de
chaque pixel en Oklab (état moins neutre, échelle 1/2) est modélisé par zone comme
w(L) c(état) : w une courbe de la clarté Oklab L du neutre, commune aux états de la zone,
c le changement (clarté, a, b) de l'état. Validation croisée par scène :

| Zone | Part inexpliquée | Pic de la zone | Pic contre clé de l'image | Scène la moins bien expliquée |
| --- | ---: | ---: | ---: | --- |
| Ombres | 0,057 | L 0,35 | r = +0,21 | 7Y6A7599 (0,20) |
| Tons moyens | 0,039 | L 0,60 | r = −0,10 | IMG_7953 (0,27) |
| Hautes lumières | 0,053 | L 0,85 | r = +0,36 | 6V1A9141 (0,32) |

- **Les zones ne s'adaptent pas à l'image** : leur pic ne suit pas la clé tonale de la scène
  (corrélations faibles). Contrairement au voile, une loi commune convient ici.
- **Force proportionnelle à la saturation** : 0,20, 0,50, 1, 1,5 et 2 à 10, 25, 50, 75 et 100
  (par rapport à 50) ; seules les ombres bleues fléchissent un peu à 100 (1,67).
- **Chaque teinte a sa chroma et son effet de clarté** : le vert et le magenta poussent deux
  fois plus que l'orange et le bleu ; dans les tons moyens, le jaune éclaircit (+0,041 L) et
  le bleu assombrit (−0,040 L). La direction en Oklab suit la teinte demandée avec un
  léger décalage (30° donne environ 40°).
- Les scènes les moins bien expliquées sont les plus saturées.

### 3. État livré

- `src/core/ColorGrading` : pour chaque pixel, la clarté Oklab donne le poids de chaque zone ;
  la couleur de la teinte, interpolée sur le cercle, multipliée par la force de la
  saturation, est ajoutée en Oklab. Tables générées depuis etude-grading-v001. Remplace le
  grading de `ColorAdjustments` (zones sur la luminance linéaire, décalage constant en
  lumière linéaire), retiré sans option de repli ; `ColorAdjustments` garde le mélangeur.
- Oklab directement depuis l'espace de travail Rec.2020 (`WorkingColor::workingToOklab`) :
  une seule matrice, la matrice sRGB → LMS d'Ottosson composée avec Rec.2020 → sRGB,
  identique à l'Oklab sRGB (écart 3 × 10⁻¹⁶) et neutre sur les gris ; sa matrice publiée
  XYZ → LMS teintait les gris de 10⁻⁴. Remarque de l'utilisateur à l'origine de cette voie.
- `iprRenderServer` accepte un grading quelconque (`"grading": {"hue": [3], "saturation": [3]}`).
- Test `colorGradingTests` : place des zones, direction de la couleur, saturation 25 égale
  à la moitié de 50, continuité 359°/0°, cohérence de l'Oklab direct.

### 4. Rendu IPR contre Lightroom

`tests/study_grading_ipr.py`,
[etude-grading-ipr-v001](../raw/etude-grading-ipr-v001-20261025T181103Z.json) : dix scènes
(dont les plus saturées), 14 états par zone (12 teintes à 50, 30° et 210° à 100), rendus en
mémoire et ramenés à la géométrie des exports.

| Zone | Ancien grading, médiane | Nouveau, médiane | Nouveau, 90e centile |
| --- | ---: | ---: | ---: |
| Ombres | 14,7 | 0,075 | 0,234 |
| Tons moyens | 0,76 | 0,071 | 0,272 |
| Hautes lumières | 0,99 | 0,129 | 0,514 |

L'ancien grading est émulé sur les mêmes neutres IPR (formule retirée, chiffres
indicatifs, sans preuve JSON). Par scène, médianes de 0,018 (6V1A0007) à 0,35 (IMG_7953) ;
états les plus éloignés : hautes lumières à 240°, 60° et 210°.

**Réserves** : hautes lumières moins fidèles, très probablement parce que les neutres d'IPR
et de Lightroom diffèrent dans les tons clairs (le poids de zone dépend de la clarté du
pixel) ; scènes très saturées moins bien rendues (IMG_7953, IMG_7958), effet possible de la
saturation déjà présente ; fusion (50) et balance (0) de Lightroom à leurs valeurs par
défaut, combinaisons de zones non mesurées ; réglages de luminance par zone et global de
Lightroom absents d'IPR.

<a id="grading-20261025-fusion"></a>

## Fusion et balance — 25 octobre 2026

### 5. Exports

Préréglages `IPR Grading fusion` (`scripts/make_lr_presets.py --controls grading-blend`),
28 scènes, 2048 pixels : chaque zone seule à 30°/50 sur la fusion 0, 25, 75, 100 et la
balance −100 à +100 ; ombres 210° + hautes lumières 30° sur la fusion et à balance ±50 ;
les trois zones ensemble. Deux préréglages appliqués deux fois à la place du suivant ont
été réexportés ; les doublons restants comptent une fois.

### 6. Réponse de Lightroom

`tests/study_grading_blend.py`, [etude-grading-fusion-v001-20261025T202111Z](../raw/etude-grading-fusion-v001-20261025T202111Z.json) : courbe de poids de la zone par
état, à vecteur de changement fixé à celui de la campagne principale.

- **Fusion** : à 0, les ombres et hautes lumières se resserrent (la queue des ombres
  disparaît au-delà de L 0,5) ; à 100 elles s'élargissent vers le milieu et se renforcent
  (pic 1,42 pour les ombres à L 0,45, 1,50 pour les hautes lumières) ; les tons moyens
  bougent à peine.
- **Balance** : vers le positif, les ombres se replient vers le noir (pic à L 0,10 à +100),
  les tons moyens descendent (L 0,25), les hautes lumières s'étendent et se renforcent
  (2,09) ; l'inverse vers le négatif.
- Validation croisée par zone et par valeur : 0,04 à 0,10 de part inexpliquée, contre
  jusqu'à 10 à 29 avec la courbe par défaut aux balances extrêmes.
- **Additivité** : les trois zones ensemble 0,049 ; ombres + hautes lumières 0,067 à 0,122
  (0,097 déjà aux réglages par défaut : deux zones opposées qui se chevauchent sont un peu
  tempérées par Lightroom), contre 0,10 à 0,63 avec les courbes par défaut.

### 7. État livré

- `ColorGrading` : pour chaque zone, 5 courbes de fusion et 7 de balance, interpolées entre
  les valeurs mesurées ; courbe = défaut + (fusion − défaut) + (balance − défaut), calculée
  une fois par rendu. Réglages `gradingBlending` (0–100, 50) et `gradingBalance`
  (−100–100, 0) dans la recette, le fichier annexe (version 7 ; les fichiers antérieurs
  prennent les valeurs par défaut), `ImageEditor`, le serveur de rendu et l'interface
  (curseurs Fusion et Balance du module Color grading).
- Rendu IPR contre Lightroom ([etude-grading-ipr-fusion-v001-20261025T203247Z](../raw/etude-grading-ipr-fusion-v001-20261025T203247Z.json), 6 scènes, 228 états) : médiane
  0,076 ; ombres 0,046 à 0,060, tons moyens 0,067 à 0,076, trois zones 0,043, ombres +
  hautes lumières 0,081 à 0,086 ; point faible : hautes lumières à fusion 0 (0,47), zone
  étroite où l'écart des neutres dans les tons clairs pèse le plus.

<a id="grading-20261026-luminance"></a>

## Luminance et roue Global — 26 octobre 2026

### 8. Exports

Préréglages `IPR Grading luminance` (`scripts/make_lr_presets.py --controls grading-luminance`,
58 états), 28 scènes, 2048 pixels, neutre de `grading/neutre` : luminance seule de chaque
zone et de la roue Global à −100, −50, −25, −10, +10, +25, +50, +100 ; luminance ±50 avec
une couleur (30°, saturation 50) ; roue Global : teinte tous les 30° à saturation 50,
saturation 25, 75, 100 à 30° et 210°. 1 596 exports lisibles : **Global T090 S050** jamais
appliqué (aucune scène), **Global L−100** absent de 7 scènes (7Y6A7599, IMG_3135,
IMG_4002, IMG_4173, IMG_4345, IMG_6248, IMG_6954), remplacé par un second « Global L−50
T030 S050 », compté une fois.

### 9. Réponse de Lightroom

`tests/study_grading_luminance.py`, [etude-grading-luminance-v001-20261026T135833Z](../raw/etude-grading-luminance-v001-20261026T135833Z.json). Luminance : décalage de
clarté g(L) et gain de chroma h(L) de la clarté Oklab du neutre, validation croisée par scène.

| Réglage | Comportement | Part inexpliquée (décalage seul) |
| --- | --- | ---: |
| Luminance des ombres | positive : relève surtout le noir (+0,17 à L 0 pour +50) et désature ; négative : assombrit vers L 0,30 | 0,001 à 0,016 (0,009 à 0,025) |
| Luminance des tons moyens | décalage centré sur L 0,60, proportionnel à la valeur | 0,034 (0,039) |
| Luminance des hautes lumières | vers L 0,85 à 0,95, chroma dans le même sens | 0,008 à 0,016 (0,026 à 0,043) |
| Luminance Global | décalage centré sur L 0,60–0,65 | 0,005 (0,008) |
| Couleur de la roue Global | w(L) c comme les zones : faible au noir (0,20 à L 0,1), maximale vers L 0,65, 0,54 à L 0,9 ; force 0,50 / 1 / 1,49 / 1,97 à 25 / 50 / 75 / 100 | 0,023 |

Luminance et couleur ensemble : effets additifs (0,012 à 0,055) ; la couleur pondérée par
la clarté après décalage fait un peu mieux (0,009 à 0,065, meilleure sur cinq états sur huit).

### 10. État livré

- `ColorGrading` : quatre zones (ombres, tons moyens, hautes lumières, Global). Par zone,
  tables de g et h aux huit valeurs mesurées, interpolées ; les luminances s'additionnent,
  puis chaque couleur est pondérée par la clarté décalée. La roue Global a sa courbe propre ;
  fusion et balance ne s'appliquent qu'aux trois zones. Teinte 90° de la roue Global
  interpolée entre 60° et 120°.
- Réglages `gradingHue`, `gradingSaturation` (quatre valeurs) et `gradingLuminance` dans la
  recette, le fichier annexe (version 8 ; les fichiers antérieurs prennent 0), `ImageEditor`
  (`setColorGradingLuminance`), le serveur de rendu, le cache des étapes et l'interface
  (onglet Global, barre Luminance).
- Rendu IPR contre Lightroom ([etude-grading-ipr-luminance-v001-20261026T141801Z](../raw/etude-grading-ipr-luminance-v001-20261026T141801Z.json), 6 scènes, 342 états) : médiane
  **0,017** ; luminance seule 0,001 à 0,017 selon la zone, couleur Global 0,018, luminance
  avec couleur 0,018 à 0,088. Points faibles : IMG_7953 (scène saturée, écart des neutres
  dans les tons clairs : hautes lumières +100 à 0,72) et tons moyens −50 avec couleur
  (médiane 0,13).
- Réserve : fusion et balance combinées à la luminance non exportées ; la luminance n'en
  dépend pas dans le modèle.

<a id="conclusion-actuelle"></a>

## Conclusion actuelle et prochaines actions — 25 octobre 2026

- Grading recalé sur Lightroom ; validation visuelle de l'utilisateur à faire.
- Fusion et balance intégrées (§5 à §7) ; luminance et roue Global intégrées (§8 à §10).
- Pistes : écart des neutres dans les hautes lumières, dépendance à la saturation du pixel
  sur les scènes saturées, tempérament de deux zones opposées qui se chevauchent.
- Limites : fusion et balance combinées mesurées seulement sur ombres + hautes lumières ;
  trois boîtiers Canon.
