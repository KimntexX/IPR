# Effets — rapport consolidé

## Bilan actuel — 24 octobre 2026

**Dernier résultat :** la [texture](#effets-20261024-texture) reproduit Lightroom à
±0,02 de 1 à 16 pixels à toutes les valeurs ; la [clarté](#effets-20261024-clarte)
est **validée dans un premier état, avec réserve** : son contraste local suit
Lightroom échelle par échelle, mais sa composante tonale adaptée à chaque image
n'est pas reproduite. Le [voile](#effets-20261024-voile) suit Lightroom en
lumière linéaire et par ton, avec une réserve sur les scènes nocturnes et un
excès à +100. Grain et flou restent à traiter ([conclusion](#conclusion-actuelle)).

Protocole : [protocole Effets](../protocols/protocole-effets.md). Branche :
`patch_effects`. Les anciennes analyses Texture, Clarté et Voile du 2 octobre 2026
restent des preuves historiques.

<a id="effets-20261024-campagne"></a>

## Campagne du 24 octobre 2026 — corpus et première mesure

### 1. Corpus et exports

Racine de `base` réduite aux scènes de la campagne (les autres RAW sont dans
`base-hors-protocole`). Exports Lightroom des préréglages `IPR Effets`
(`scripts/make_lr_presets.py --controls texture clarity dehaze grain` ; grain à
la taille 25 et à la rugosité 50 par défaut, compagnons et graine ignorés lors de
l'association au neutre) :

- texture et clarté ±25, ±50, ±100 : 6V1A0327, 6V1A0043, 6V1A9947 (contre-jour),
  _MG_7580 (vallée sous la brume, 60D) ;
- clarté, 14 scènes ajoutées depuis le stock de l'utilisateur, choisies par
  éloignement maximal de la clé tonale, de la dynamique, des parts d'ombres et de
  hautes lumières et du contraste local et global : _MG_7840, IMG_7961, IMG_7953,
  IMG_6954, IMG_4173, IMG_8473, 6V1A0032, IMG_6979, IMG_6248, 6V1A9153,
  6V1A0544, 6V1A9539, 6V1A9112, 6V1A0064 ;
- voile ±25, ±50, ±100 : _MG_7580, IMG_4345 (mer voilée, 60D), 6V1A0043 (témoin
  sans voile), 6V1A0327 ;
- grain 25, 50, 100 : 6V1A0327, 6V1A0007.

### 2. Mesure générique

[mesure-effets-v001](../raw/mesure-effets-v001-20261024T150336863214Z.json),
240 états mesurés. Clarté : Lightroom change fortement la luminosité et le contraste
globaux (6V1A0043 : Y 0,79 à −100, 1,39 à +100), IPR presque pas (0,97 à 1,03).
Voile : même sens, IPR plus faible, surtout pour ajouter du voile (IMG_4345 à −100 :
contraste ×0,44 chez Lightroom, ×0,91 chez IPR).

### 3. Réponse par échelle

`tests/study_effects_response.py` compare chaque état au neutre du même moteur,
pixel à pixel, sur le log de la luminance découpé en bandes de différences de
gaussiennes (1 à 256 pixels) : gain de chaque bande, réponse tonale par niveau.
[etude-reponse-effets-v001](../raw/etude-reponse-effets-v001-20261024T151007Z.json),
médiane des quatre scènes :

| Bande (pixels) | fin | 1–2 | 2–4 | 4–8 | 8–16 | 16–32 | 32–64 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Texture +100, Lightroom | 1,56 | 1,48 | 1,43 | 1,34 | 1,19 | 1,07 | 1,02 |
| Texture +100, IPR initial | 2,05 | 1,56 | 1,19 | 1,05 | 1,01 | 1,00 | 1,00 |
| Clarté +100, Lightroom | 1,89 | 1,85 | 1,77 | 1,64 | 1,48 | 1,34 | 1,25 |
| Clarté +100, IPR initial | 1,09 | 1,08 | 1,07 | 1,06 | 1,03 | 1,01 | 1,00 |

La texture initiale (masque flou de 1,2 pixel en lumière linéaire) exagérait les
deux premiers pixels et manquait 2 à 16 ; la clarté initiale (8 pixels, extrêmes
protégés) n'agissait presque pas.

### 4. Gain selon l'amplitude

Sur ses propres paires, Lightroom amplifie bien plus les petites variations que
les grandes : clarté +100, bande 8–16 pixels de 6V1A0043, gain 1,51 à 1,64 sous la
médiane d'amplitude, 0,98 sur le 1 % le plus fort. Modèle ajusté par
`tests/study_effects_amplitude.py` pour chaque bande d (dans le domaine de
l'effet) : d + k d / (1 + (|d|/A)²), k par valeur, A par signe.
[etude-amplitude-effets-v001](../raw/etude-amplitude-effets-v001-20261024T165011Z.json)
(log2) : clarté +100, k 0,83 à 0,92 jusqu'à 32 pixels puis 0,48 à 256, A 0,5 à
0,8 IL au-delà de 4 pixels ; clarté négative sans dépendance à l'amplitude.

<a id="effets-20261024-texture"></a>

## Texture — 24 octobre 2026

- `src/core/LocalContrast` : neuf bandes de 1 à 256 pixels (grandes échelles à
  résolution réduite), gain d'amplitude ci-dessus ; remplace le masque flou de
  `DetailProcessing`, retiré sans option de repli.
- Domaine : dans le log, l'effet de Lightroom par niveau variait d'un facteur 0,28
  à 1,21 autour du modèle ; dans la **racine cubique** de la luminance (proche de
  la clarté CIE L\*), de 0,64 à 1,12. La texture y est ajustée
  ([etude-amplitude-effets-v002](../raw/etude-amplitude-effets-v002-20261024T172402Z.json)),
  puis chaque k est recalé par le rapport entre l'effet de Lightroom et l'effet
  rendu par IPR.
- Résultat ([etude-reponse-effets-v002](../raw/etude-reponse-effets-v002-20261024T173938Z.json)) :

| Texture, bande (pixels) | fin | 1–2 | 2–4 | 4–8 | 8–16 | 16–32 |
|---|---:|---:|---:|---:|---:|---:|
| +100, Lightroom | 1,56 | 1,48 | 1,43 | 1,34 | 1,19 | 1,07 |
| +100, IPR | 1,57 | 1,47 | 1,42 | 1,32 | 1,20 | 1,11 |
| −100, Lightroom | 0,49 | 0,57 | 0,62 | 0,70 | 0,83 | 0,94 |
| −100, IPR | 0,48 | 0,58 | 0,63 | 0,72 | 0,82 | 0,91 |

Réponse tonale à moins de 0,03 IL de Lightroom à tous les niveaux.

<a id="effets-20261024-clarte"></a>

## Clarté — 24 octobre 2026, premier état validé avec réserve

### 5. État livré

Même opérateur que la texture, dans le log2 de la luminance, où le modèle par
bandes explique le mieux la clarté de Lightroom, neuf bandes jusqu'à 256 pixels :
gains moyens de Lightroom reproduits à chaque échelle (clarté +100 : 1,91 contre
1,89 en fin, 1,23 contre 1,19 à 64–128 pixels), petites variations amplifiées,
bords épargnés. Halo mesuré à 8–56 pixels d'un bord de 4 IL : 1,0 IL à +100,
du même ordre que les anneaux de Lightroom sur 6V1A9947 (0,7 à 1,0 IL).

### 6. Réserve — composante tonale adaptée à chaque scène

**Validé par l'utilisateur le 24 octobre 2026 comme premier état, avec la réserve
suivante.** Au-delà du contraste local, la clarté de Lightroom déplace les tons
de façon propre à chaque image : elle éclaircit les images sombres (6V1A0043,
médiane −8,8 IL : +0,29 IL en moyenne ; 6V1A0032 : +0,32) et assombrit les claires
(IMG_6248 : −0,29 ; 6V1A9153 : −0,24), et ses anneaux près des bords sont
dissymétriques selon les tons. IPR ne reproduit pas cette composante.

[etude-clarte-adaptation-v001](../raw/etude-clarte-adaptation-v001-20261024T175927Z.json),
20 scènes : à résolution réduite d'un facteur 4, le modèle par bandes laisse
72 % de la variance de l'effet de Lightroom à +100 (74 % à +50, 64 % à −100).
Explications testées en validation croisée par scène, sans succès :

| Hypothèse | Part de l'effet de Lightroom inexpliquée |
|---|---:|
| Courbe commune du niveau | ≥ 1,00 |
| Courbe du niveau moins la médiane de l'image | ≥ 1,00 |
| Même courbe normalisée par la dynamique | ≥ 1,00 |
| Table niveau × écart à la moyenne locale (64 px), commune | ne se transpose pas |
| Filtre laplacien local (Paris 2011, Aubry 2014), 3 paramètres | 0,84 |

Aucune statistique globale du neutre ne prédit cette adaptation : un calcul de
poids par image à l'ouverture n'a, en l'état, rien de fiable à calculer.
Recherche bibliographique en cours pour d'autres familles d'opérateurs.

<a id="effets-20261024-voile"></a>

## Voile — 24 octobre 2026

### 7. Ce que fait Lightroom

Sur ses propres paires, le voile de Lightroom est une transformation affine de
chaque canal, lisse dans l'espace (a et b lissés à environ 128 pixels expliquent
98 à 100 % de son changement) : c'est le modèle physique du voile,
I = J t + A (1 − t), avec une transmission qui suit le canal sombre. Dans les
ombres, il ne retranche pourtant pas le voile : hors brume (h < 0,2), +100 baisse
tous les tons sombres d'environ 1,2 IL, de façon multiplicative ; la soustraction
n'apparaît que sous la brume.

### 8. Lois essayées

`tests/study_dehaze_law.py`, quatre scènes, résolution réduite d'un facteur 4,
validation croisée par scène :

| Loi | Preuve | Part inexpliquée (validation croisée) | Défaut |
|---|---|---|---|
| a − 1 et b/A polynômes de degré 2 de h | [v001](../raw/etude-loi-voile-v001-20261024T185925Z.json) | 0,15 à 0,25 en lumière linéaire, 0,55 à −100 | extrapole à contresens près de h = 1 : −100 retire du voile |
| Modèle physique, t = 1 − ω h, décalage | [v002](../raw/etude-loi-voile-v002-20261024T191121Z.json) | 0,09 à 0,22 en lumière linéaire | écrase les ombres au noir (jusqu'à −5 IL au 10e centile, contre −1 à −2,4 chez Lightroom) |
| Gain en IL f(log2(y/A), h), table bilinéaire | [v003](../raw/etude-loi-voile-v003-20261024T194506Z.json) | 0,14 à 0,21 en IL pour les négatifs, 0,44 à 0,59 pour les positifs | réserve §10 |

Le modèle physique, ajusté en lumière linéaire, ignore presque les ombres : y − A
devient négatif dès que y < A (1 − t). La loi retenue est ajustée en IL, sur une
grille de 13 niveaux × 6 valeurs de h, avec une pénalité de courbure ; le gain est
borné au 1er–99e centile du changement observé chez Lightroom, ce qui contient le
coin sombre et voilé, vide de données.

### 9. État livré

- `src/core/Dehaze` : A = max(0,10 ; 99e centile du max RGB), canal sombre érodé
  à 1/4 de résolution, lissé à 128 pixels, h = canal sombre / A ; chaque canal
  reçoit y 2^f(log2(y/A), h), table interpolée linéairement entre les valeurs
  mesurées, nulle à 0. Multiplicatif, il ne rend jamais un canal négatif. Remplace
  `DetailProcessing::dehaze`, retiré sans option de repli.
- Test `dehazeTests` : paysage synthétique voilé sous un ciel à l'air ambiant ;
  +100 augmente le contraste de détail et assombrit, −100 l'inverse.
- Rendu IPR en mémoire contre Lightroom
  ([etude-reponse-effets-v003](../raw/etude-reponse-effets-v003-20261024T195513Z.json)),
  réponse tonale à ±100 (IL, niveaux 0,008 / 0,04 / 0,14 / 0,39 / 0,78) :

| Scène | Valeur | Lightroom | IPR |
|---|---:|---|---|
| 6V1A0327 | −100 | +3,23 +1,97 +1,16 −0,00 | +3,06 +1,87 +1,17 −0,05 |
| 6V1A0327 | +100 | −1,67 −1,00 −0,56 +0,01 | −1,55 −1,27 −0,81 +0,05 |
| IMG_4345 | −100 | +1,75 +0,85 +0,60 | +1,71 +0,82 +0,42 |
| IMG_4345 | +100 | −1,84 −1,97 −0,96 | −3,14 −2,05 −1,61 |
| _MG_7580 | −100 | +3,85 +3,20 +2,21 +0,69 +0,24 | +3,34 +2,73 +2,05 +0,70 +0,24 |
| _MG_7580 | +100 | −1,15 −1,92 −2,51 −2,36 −0,74 | −1,59 −1,63 −4,60 −1,80 −1,00 |
| 6V1A0043 | −100 | +2,02 +0,86 +0,32 +0,01 −0,11 | +3,25 +1,65 +0,72 −0,16 −0,28 |
| 6V1A0043 | +100 | −0,50 −0,21 −0,05 −0,00 +0,02 | −1,44 −1,00 +0,07 +0,04 +0,08 |

Part du changement de luminance de Lightroom (IL) laissée inexpliquée par le rendu
IPR, toutes scènes : 0,07 à −100, 0,09 à −50, 0,10 à −25, 0,25 à +25, 0,22 à +50,
0,38 à +100 (même corpus que l'ajustement : mesure en échantillon, la validation
croisée du §8 donne l'estimation hors échantillon).

### 10. Réserve

- Scène nocturne (6V1A0043, luminance médiane 0,001) : Lightroom n'y touche
  presque pas les ombres profondes, IPR les déplace 2 à 3 fois plus. Avec quatre
  scènes, la table mêle cette scène et les autres ; l'adaptation de Lightroom à
  une image sombre n'est pas modélisée, comme pour la clarté (§6).
- +100 dépasse Lightroom sur certains tons (IMG_4345 dans les noirs, _MG_7580
  vers 0,14) : la table moyenne des scènes qui réagissent différemment.
- Mesures en luminance ; la saturation ajoutée par le voile suit les rapports
  de canaux de la loi par canal, non vérifiée séparément.

<a id="conclusion-actuelle"></a>

## Conclusion actuelle et prochaines actions — 24 octobre 2026

- Texture : conforme à Lightroom, validation visuelle de l'utilisateur en cours.
- Clarté : premier état validé avec la réserve du §6.
- Voile : loi en IL ajustée sur Lightroom, réserve du §10 ; validation visuelle de
  l'utilisateur à venir.
- Prochaines actions : recherche bibliographique sur la clarté, soumise à
  l'utilisateur ; puis grain (statistiques face à Lightroom) et flou (IPR seul).
- Limites : quatre scènes pour la texture et le voile, vingt pour la clarté ;
  trois boîtiers Canon ; mesures en luminance (la chrominance suit les rapports de
  canaux) ; halo jugé sur un bord synthétique et sur les anneaux de Lightroom.
