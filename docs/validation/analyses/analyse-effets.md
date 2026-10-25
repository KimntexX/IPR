# Effets — rapport consolidé

## Bilan actuel — 24 octobre 2026

**Dernier résultat :** la [texture](#effets-20261024-texture) reproduit Lightroom à
±0,02 de 1 à 16 pixels à toutes les valeurs ; la [clarté](#effets-20261024-clarte)
est **validée dans un premier état, avec réserve** : son contraste local suit
Lightroom échelle par échelle, mais sa composante tonale adaptée à chaque image
n'est pas reproduite. Le [voile](#effets-20261024-voile) suit Lightroom en
lumière linéaire et par ton, avec une réserve sur les scènes nocturnes et un
excès à +100. Le [grain](#effets-20261024-grain) reproduit celui de Lightroom.
Le flou reste à traiter ([conclusion](#conclusion-actuelle)).

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

### 6 bis. Pistes bibliographiques — 24 octobre 2026

La formule de Lightroom n'est pas publique ; les descriptions connues (Schewe :
hybride d'un masque flou à grand rayon et d'un contraste des tons moyens, Thomas
Knoll) ne valent que pour de petites corrections. Pistes recensées, présentées à
l'utilisateur et ordonnées par proximité avec la réserve :

1. **Masquage non linéaire de Moroney** (HP, CIC 2000 ; brevets US 6 822 762 et
   US 8 238 687 ; réimplémentation IPOL 2011) : masque = luminance inversée très
   floutée, y = x^γ avec γ = 2^((128 − masque)/128). Éclaircit les régions
   sombres et assombrit les claires, localement : le sens de la réserve. Un
   développeur de RawTherapee décrit le même comportement adaptatif en imitant la
   clarté (forum pixls.us).
2. **Opérateur photographique de Reinhard** (2002) : clé de la scène et plus grand
   voisinage sans bord fort par pixel ; version locale non testée.
3. **Lissages préservant les bords** : moindres carrés pondérés (Farbman 2008),
   filtre guidé, filtre guidé incrémental (RawTherapee/PhotoFlow) ; candidats pour
   la dissymétrie des anneaux.
4. **Filtre laplacien local** (Paris 2011, Aubry 2014) : déjà testé, 0,84.
5. **Approches statistiques ou apprises** : grille de coefficients affines locaux
   prédite en basse résolution (HDRNet, Gharbi 2017), tables 3D adaptées à l'image
   (Zeng 2020), réglages tonals appris sur paires (Bychkovsky, Paris, Chan,
   Durand 2011). Étape proposée : ajuster une grille affine locale sur chaque
   paire Lightroom, puis rechercher par corrélation matricielle les
   caractéristiques du neutre qui prédisent ses coefficients.
6. **Tons foncés/Tons clairs de Photoshop** : courbes de puissance à travers des
   masques de tons floutés (rétro-ingénierie communautaire), cousin de la piste 1.

Décision de l'utilisateur : étudier la piste 1, puis directement la piste 5 si
elle ne suffit pas.

### 6 ter. Pistes 1 et 5 étudiées — 24 octobre 2026

Même cible que le §6 : ce que le modèle par bandes d'IPR laisse de l'effet de
Lightroom (changement du log2 de la luminance, 20 scènes, résolution réduite d'un
facteur 4), part de la variance de l'effet de Lightroom inexpliquée, validation
croisée par scène.

**Piste 1, masquage non linéaire**
([etude-clarte-masquage-v001](../raw/etude-clarte-masquage-v001-20261024T201353Z.json),
`tests/study_clarity_masking.py`) : premier ordre du modèle de Moroney,
L (α + β m), masque m flouté de 32 à 512 pixels en gamma 2,2 ou en log2. Le sens
est le bon (à +100, il éclaircit les régions sombres et assombrit les claires),
mais l'apport est négligeable : 0,73 → 0,73 à 0,75 pour les valeurs positives,
0,67 à 0,70 → 0,65 pour les négatives. **Écartée.**

**Piste 5, grille affine locale**
([etude-clarte-grille-affine-v001](../raw/etude-clarte-grille-affine-v001-20261024T205100Z.json),
`tests/study_clarity_affine_grid.py`) : le résidu de chaque scène est ajusté par
Δ = a(x) L + b(x), a et b lissés (moindres carrés locaux pondérés, structure d'une
grille bilatérale de coefficients affines comme HDRNet) ; puis a et b sont prédits
d'une scène à l'autre par régression linéaire sur des caractéristiques du
neutre (moyennes locales du niveau à 16, 64 et 256 pixels, écart type local à 64
pixels, quantiles globaux 10, 50 et 90 %).

| Valeur | Modèle par bandes seul | Grille propre à la scène (64 / 128 / 256 px) | Grille prédite du neutre (64 / 128 / 256 px) |
|---|---:|---|---|
| −100 | 0,67 | 0,27 / 0,35 / 0,43 | 1,61 / 1,14 / 0,84 |
| −50 | 0,69 | 0,26 / 0,35 / 0,44 | 1,78 / 1,24 / 0,89 |
| −25 | 0,70 | 0,26 / 0,35 / 0,44 | 1,89 / 1,31 / 0,92 |
| +25 | 0,73 | 0,27 / 0,31 / 0,36 | 0,83 / 0,85 / 0,77 |
| +50 | 0,73 | 0,28 / 0,32 / 0,37 | 0,84 / 0,87 / 0,79 |
| +100 | 0,73 | 0,27 / 0,31 / 0,36 | 0,88 / 0,92 / 0,82 |

Lecture : la composante adaptative de Lightroom **est** une transformation
affine locale et lisse du log de la luminance, comme son voile : une grille propre
à chaque scène en explique les deux tiers (0,73 → 0,27). Mais une combinaison
linéaire des statistiques locales et globales du neutre ne la prédit pas (0,77 à
1,89, pire que rien) : les coefficients dépendent du contenu de façon non
linéaire. Pistes suivantes, à soumettre à l'utilisateur : prédicteurs non
linéaires (tables par niveau et écart local, comme pour le voile), hypothèse
d'une égalisation locale d'histogramme (CLAHE), dont la pente locale est l'inverse
de la dynamique locale.

### 6 quater. Égalisation locale d'histogramme (CLAHE) — 25 octobre 2026

Choix de l'utilisateur : étudier CLAHE, jusqu'à un état suffisant ou une
impasse. [etude-clarte-clahe-v001](../raw/etude-clarte-clahe-v001-20261024T215027Z.json),
`tests/study_clarity_clahe.py` : égalisation du log2 de la luminance du neutre par
tuiles (128 à 1024 pixels), histogrammes limités (1,5 à 4 fois le compte moyen,
ou sans limite), courbes interpolées entre les centres des tuiles ; l'effet,
brut ou lissé à la moitié d'une tuile, est ajusté au résidu avec une force par
valeur, en validation croisée par scène.

- Meilleur réglage (tuiles de 1024 pixels, limite 1,5, effet brut) : part
  inexpliquée 0,73 → 0,62 à +100, 0,67 → 0,67 à −100 ; contrôle complémentaire
  à 2048 et 4096 pixels : 0,60 à 0,61 à +100, sans gain pour les négatifs ;
  égalisation globale : 0,64.
- Lissé, l'effet n'explique plus rien (0,71 à 0,73) : ce que CLAHE capte est du
  contraste local, déjà rendu en partie par les bandes, pas la composante tonale.

**Conclusion de l'étude de la clarté** : ni le masquage non linéaire, ni CLAHE,
ni une régression des coefficients affines locaux ne prédisent l'adaptation de
Lightroom à chaque image, alors qu'une grille affine propre à la scène en
explique les deux tiers. Faute de piste qui se transpose d'une scène à l'autre,
la clarté reste au premier état validé (§5) avec la réserve du §6 ; l'utilisateur
la juge visuellement meilleure que celle de Lightroom.

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

<a id="effets-20261025-voile-couleur"></a>

### 10 bis. Reprise du voile : couleur — 25 octobre 2026

**Retour de l'utilisateur** : sur des images sombres comme 6V1A9539, le voile vers
+100 tire les couleurs vers le jaune et l'image paraît délavée.

**Cause** : la loi v003 appliquait un gain en IL à chaque canal selon son propre
niveau. Dans une image sombre, R, G et B tombent dans des cases différentes de la
table, irrégulière dans les ombres profondes (quatre scènes) : sur 6V1A9539 à +100,
le bleu baissait de 1,99 IL et le rouge montait de 0,56 IL par rapport au vert
dans les tons moyens-sombres, saturation +2,6 IL. La couleur du voile n'avait
jamais été mesurée (réserve du §10).

**Ce que fait Lightroom de la couleur** (paires à ±100, rapports log2 R/G et B/G par
ton) : il retire ou ajoute une dominante propre à la scène (bleue sur 6V1A0327,
chaude sur _MG_7580) et sature à +100 (+0,8 à +1,9 IL d'écart entre canaux). La
couleur qu'il ajoute dans les ombres à −100 dépend de chaque scène (magenta sur
6V1A0043, verte sur 6V1A0327) ; aucun estimateur simple de l'air ambiant ne la
retrouve.

**Modèles comparés** (`tests/study_dehaze_law.py`, part du changement de
couleur de Lightroom inexpliquée, validation croisée par scène, avec la luminance
de Lightroom pour juger la couleur à part) :

| Modèle | −100 | −50 | −25 | +25 | +50 | +100 |
|---|---:|---:|---:|---:|---:|---:|
| Rapports conservés | 1,00 | 1,00 | 1,00 | 1,00 | 1,00 | 1,00 |
| Gain par canal (v003) | 0,49 | 0,53 | 0,58 | 0,73 | 0,68 | 1,24 |
| Part β de lumière à la couleur de l'air ambiant ([v004](../raw/etude-loi-voile-v004-20261024T215525Z.json)) | 0,48 | 0,58 | 0,54 | 0,84 | 0,92 | 1,08 |
| Expansion de chroma κ h centrée sur le gris ([v005](../raw/etude-loi-voile-v005-20261024T221133Z.json) ; [v006](../raw/etude-loi-voile-v006-20261024T221611Z.json) bornée) | | | | 0,86 | 0,78 | 0,80 |
| Expansion bornée centrée sur l'air ambiant ([v007](../raw/etude-loi-voile-v007-20261024T222843Z.json)) | | | | 0,84 | 0,75 | 0,74 |

Au-delà de 1,00, le modèle fait pire que de ne rien changer : le gain par canal à
+100 se transpose mal, d'où le jaunissement. Non bornée, l'expansion faisait
croître une couleur forte de 4,5 IL (Lightroom : 1,9 au plus) ; centrée sur le
gris, elle accentuait la dominante du voile (B/G −0,53 dans les tons clairs de
_MG_7580, que Lightroom débarrasse de sa dominante chaude).

**État livré** (`src/core/Dehaze`, [v007](../raw/etude-loi-voile-v007-20261024T222843Z.json)) :

- luminance : gain en IL de la luminance f(log2(Y/A), h), sans changer les
  rapports de canaux (validation croisée en luminance 0,15 à 0,19 pour les
  négatifs, 0,29 à 0,34 pour les positifs, contre 0,44 à 0,59 par canal) ;
- couleur, en sRGB linéaire comme l'ajustement : pour ajouter du voile, une part β
  (0,35 à 0,95) de la variation devient de la lumière à la couleur de l'air
  ambiant (moyenne des 0,1 % de pixels au canal sombre le plus clair, He et al.
  2009) ; pour le retirer, les rapports log2 R/G et B/G, pris depuis ceux de l'air
  ambiant, gagnent κ h d / (1 + d²), κ de 1,3 à 10,5 : la dominante du voile est
  retirée, ses couleurs pâles saturées, les couleurs fortes presque pas.

Rendu IPR en mémoire contre Lightroom : part du changement de luminance (IL)
inexpliquée 0,07 à −100, 0,08 à −50 et −25, 0,14 à +25, +50 et +100 (en
échantillon). 6V1A9539 à +100 : B/G 0,00 à +0,02, R/G −0,10, saturation −0,09 à
+0,16 (avant : −1,99, +0,56, +2,6) ; _MG_7580, tons clairs : B/G −0,19 (avant
−0,53 ; Lightroom +0,13).

**Réserves** : la couleur que Lightroom donne aux ombres, propre à chaque scène,
n'est pas reproduite (6V1A0327 à +100 : R/G +0,86 à +0,97 chez Lightroom, environ
0 chez IPR) ; pas d'export Lightroom de 6V1A9539 au voile pour juger son rendu ;
quatre scènes.

<a id="effets-20261025-voile-detail"></a>

### 10 ter. Reprise du voile : contraste local — 25 octobre 2026

**Retour de l'utilisateur** (6V1A9539, côte à côte avec Lightroom) : la couleur est
meilleure, mais à +100 l'image paraît lissée au lieu d'être exacerbée, et à −100
le voile n'est pas assez poussé (Lightroom noie l'image et adoucit même les
contours).

**Cause** : la loi appliquait un gain à chaque pixel selon son propre ton, ce qui
revient à une courbe de tons modulée par le voile ; elle changeait surtout les
échelles moyennes. Lightroom change le contraste de toutes les échelles presque
également : ses gains par bande sont presque constants de 1 à 256 pixels
([etude-reponse-effets-v004](../raw/etude-reponse-effets-v004-20261025T095336Z.json) :
bande fine 1,92 contre 1,21 pour IPR sur _MG_7580 à +100, 0,12 contre 0,47 à −100).

**Pistes écartées** (prototypes hors dépôt) : affine local linéaire avec tables
du ton et de la pente sur la moyenne locale (gains par bande faux d'une scène à
l'autre) ; affine pivotant autour de l'air ambiant (le pivot mesuré n'est pas A :
proche de 0 dans les ombres, de −0,3 A à +1,3 A dans les tons moyens) ; gain du
détail fonction du changement de ton local (corrélations faibles).

**Loi du détail** ([etude-detail-voile-v001](../raw/etude-detail-voile-v001-20261025T104351Z.json),
`tests/study_dehaze_detail.py`) : le gain local G du détail de log2 Y est presque
constant par scène et par valeur, et suit d'abord le voile local h et le ton local
u : log2 G = c0 + ch h + cu u. En validation croisée, l'erreur sur la médiane par
scène de log2 G tombe de 1,27 (gain constant) à 1,09 à −100, et de 0,68 à 0,39 à
+100. À −100, les zones claires sont voilées plus fort (G ≈ 0,07 près de l'air
ambiant) ; à +100, les zones claires et voilées gagnent plus de ×3.

**État livré** (`src/core/Dehaze`) : le changement de ton de la loi v007 est lissé
en un fond à 64 pixels ; autour, log2 Y gagne G à toutes les échelles, puis la loi
de couleur du §10 bis s'applique. Un fond à 256 pixels mêlait des régions
différentes (zone voilée et moitié claire du test synthétique) et éclaircissait
toute une région amplifiée ; une limite d'amplitude empêchait de voiler assez.

Rendu IPR en mémoire contre Lightroom
([etude-reponse-effets-v005](../raw/etude-reponse-effets-v005-20261025T103019Z.json)),
erreur médiane |log2| des gains par bande : −100 0,57 → 0,42, −50 0,15 → 0,13,
−25 0,06 → 0,05, +25 0,06 → 0,05, +50 0,12 → 0,14, +100 0,23 → 0,29 ; part du
changement de luminance inexpliquée : 0,06 à 0,08 en négatif, 0,17 à 0,24 en
positif (avant 0,14). Sur 6V1A9539 (sans export Lightroom), bande fine 0,17 à −100
et 1,81 à +100, ombres −1,2 IL à +100 : le sens de la comparaison de l'utilisateur.

**Réserves** : à +100 le détail dépasse Lightroom sur la scène nocturne
(6V1A0043 : 1,53 contre 1,15) et reste en deçà sur la mer voilée (IMG_4345 : 2,4
contre 3,6) ; quatre scènes ; validation visuelle de l'utilisateur à refaire.

<a id="effets-20261025-voile-corpus"></a>

### 10 quater. Voile sur onze scènes et air ambiant de luminance — 25 octobre 2026

**Corpus** : l'utilisateur a exporté le voile ±25, ±50, ±100 de six scènes de plus,
choisies pour combler les trous du corpus (6V1A0032 très sombre, 6V1A9112 sombre
sans voile, 6V1A0544 à dynamique extrême, IMG_7953 très saturée, 6V1A9153 claire
peu contrastée, IMG_6954 brumeuse), et de 6V1A9539 (intérieur sombre) : onze
scènes en tout.

**Retour de l'utilisateur** sur 6V1A9539 avec la loi du §10 ter : moins bon que
Lightroom à −100 ; à +100 environ 3 % trop faible, mais visuellement proche.

**Cause** à −100 : Lightroom relève les ombres (+2,2 IL) en gardant les tons
clairs (−0,01 IL) ; IPR relevait aussi les tons clairs (+0,49 IL). L'air ambiant
A, 99e centile du maximum RVB, était tiré par les prises rouges saturées (0,31
contre 0,10 en luminance) : le mur clair passait pour un ton à relever.
**A est désormais le 99e centile de la luminance** (plancher 0,03). En validation
croisée sur onze scènes, le biais des tons clairs de 6V1A9539 passe de +0,81 à
−0,06 IL à −100 et de −0,46 à +0,11 IL à +100, et la part inexpliquée de la
luminance de 0,09 à 0,07 à −100.

Preuves : [etude-loi-voile-v008](../raw/etude-loi-voile-v008-20261025T112001Z.json)
et [etude-detail-voile-v002](../raw/etude-detail-voile-v002-20261025T113506Z.json)
(onze scènes, ancien air ambiant),
[etude-loi-voile-v009](../raw/etude-loi-voile-v009-20261025T123104Z.json) et
[etude-detail-voile-v003](../raw/etude-detail-voile-v003-20261025T124720Z.json)
(air ambiant de luminance, état livré). Validation croisée de la luminance :
0,07 à 0,11 en négatif, 0,22 à 0,29 en positif (quatre scènes : 0,15 à 0,34).

Rendu IPR en mémoire contre Lightroom
([etude-reponse-effets-v006](../raw/etude-reponse-effets-v006-20261025T114101Z.json)
ancien air ambiant,
[etude-reponse-effets-v007](../raw/etude-reponse-effets-v007-20261025T125315Z.json)
état livré), onze scènes :

| | −100 | −50 | −25 | +25 | +50 | +100 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Luminance inexpliquée, v007 | 0,05 | 0,07 | 0,09 | 0,22 | 0,27 | 0,23 |
| Erreur des gains par bande, v006 | 0,36 | 0,17 | 0,07 | 0,04 | 0,08 | 0,18 |
| Erreur des gains par bande, v007 | 0,48 | 0,21 | 0,09 | 0,04 | 0,08 | 0,16 |

6V1A9539, rendu livré (ombres / tons moyens / tons clairs, IL ; bande fine) :
−100 Lightroom +2,22 / +0,81 / −0,01, 0,15 ; IPR +1,79 / +0,60 / −0,07, 0,18.
+100 Lightroom −1,51 / −0,60 / +0,02, 1,85 ; IPR −1,03 / −0,29 / +0,25, 1,78.

**Réserves** : sur 6V1A9539, le voile reste un peu faible dans les ombres (−0,4 IL
à −100, +0,5 IL à +100) et les tons clairs remontent légèrement à +100 ; le
détail est juste. Sur l'ensemble, l'erreur des gains par bande à −100 remonte
(0,36 → 0,48) avec l'air ambiant de luminance. Validation visuelle de
l'utilisateur à refaire.

<a id="effets-20261024-grain"></a>

## Grain — 24 octobre 2026

### 11. Ce que fait Lightroom

Sur ses paires (6V1A0007, 6V1A0327 ; taille 25, rugosité 50), le grain de
Lightroom s'ajoute aux valeurs encodées (gamma sRGB) : écart type 0,062 à 100,
proportionnel à la valeur, identique à tous les tons sauf les plus sombres, où le
noir l'écrête ; monochrome ; fin (corrélation 0,64 à 1 pixel, 0,13 à 2, nulle à 4 :
un bruit blanc sous une gaussienne d'environ 0,75 pixel).

### 12. État livré

- `src/core/Grain` : bruit gaussien déterministe (le même pixel reçoit le même
  grain à chaque rendu), flouté à 0,75 pixel, normalisé, ajouté aux valeurs
  encodées de chaque canal. Remplace `DetailProcessing::addGrain` (bruit blanc d'un
  pixel ajouté en lumière linéaire et renforcé dans les ombres), retiré sans
  option de repli.
- [etude-grain-v001](../raw/etude-grain-v001-20261024T205112Z.json), rendu IPR en
  mémoire :

| Scène | Valeur | Écart type Lightroom | Écart type IPR | Corrélation 1 px LR / IPR | 2 px LR / IPR |
|---|---:|---:|---:|---|---|
| 6V1A0007 | +25 | 0,0155 | 0,0155 | 0,65 / 0,63 | 0,13 / 0,17 |
| 6V1A0007 | +50 | 0,0309 | 0,0311 | 0,65 / 0,63 | 0,13 / 0,17 |
| 6V1A0007 | +100 | 0,0618 | 0,0621 | 0,65 / 0,63 | 0,13 / 0,17 |
| 6V1A0327 | +100 | 0,0574 | 0,0595 | 0,64 / 0,63 | 0,13 / 0,17 |

Par ton, IPR suit Lightroom à ±0,0003 hors des tons les plus sombres (< 0,05
encodé), où Lightroom écrête davantage (0,023 à 0,035 contre 0,043 à 0,047).
Réserves : deux scènes, taille et rugosité de Lightroom à leurs valeurs par
défaut (IPR n'a pas ces réglages).

<a id="effets-20261024-apercu"></a>

## Échelle de l'aperçu — 24 octobre 2026

Défaut d'affichage relevé pendant l'étude : l'aperçu interactif est calculé sur
un proxy de 2048 pixels, alors que les rayons des effets sont définis en pixels
de l'image entière. Sur un fichier de 6000 pixels (échelle 0,34), la texture
agissait à l'écran sur 3 à 48 pixels de l'image, la clarté et le voile sur des
structures trois fois trop grandes, et le grain paraissait trois fois plus
gros. L'export et les mesures, en pleine résolution, n'étaient pas touchés.

Correction : `ImageEditor` transmet l'échelle du proxy (`pixelScale`) à
`LocalContrast`, `Dehaze`, `Grain` et au flou, qui convertissent leurs rayons ;
une bande plus fine que le proxy disparaît, le grain y prend l'écart type que
laisse la moyenne de blocs du grain réel. À l'échelle 1, le rendu est inchangé.
Tests : sur un proxy au tiers ou au quart, l'effet correspond à la réduction de
l'effet calculé en pleine résolution. Au zoom, l'aperçu reste le proxy agrandi :
le rendu de la zone visible en pleine résolution est un futur chantier.

<a id="conclusion-actuelle"></a>

## Conclusion actuelle et prochaines actions — 24 octobre 2026

- Texture : conforme à Lightroom, validation visuelle de l'utilisateur en cours.
- Clarté : premier état validé avec la réserve du §6 ; étude de l'adaptation
  close (§6 ter et 6 quater), sans piste transposable.
- Voile : loi en IL sur la luminance et loi de couleur (§10 bis), après le
  retour de l'utilisateur sur 6V1A9539 ; réserves des §10 et 10 bis ; validation
  visuelle à refaire.
- Grain : conforme à Lightroom (§12).
- Aperçu : effets à l'échelle de l'image entière sur le proxy.
- Prochaines actions : flou (IPR seul) ; export Lightroom de 6V1A9539 au voile
  si l'utilisateur souhaite juger sa couleur sur une scène sombre.
- Limites : quatre scènes pour la texture et le voile, vingt pour la clarté ;
  trois boîtiers Canon ; mesures en luminance (la chrominance suit les rapports de
  canaux) ; halo jugé sur un bord synthétique et sur les anneaux de Lightroom.
