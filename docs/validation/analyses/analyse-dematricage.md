# Dématriçage — rapport consolidé

## Bilan actuel — 24 octobre 2026

**Dernier résultat :** l'ouverture des RAW Bayer passe d'AHD (LibRaw, un cœur)
à **RCD**, une interpolation écrite pour IPR, suivie d'**une passe de médiane
couleur à tous les ISO** ([intégration](#dematricage-20261024-integration)).
Sur le neutre, mesuré face à 65 neutres Lightroom sans réduction de bruit, le
bruit couleur fin d'IPR passe de **1,28 à 0,64 fois celui de LR** en
médiane, celui de la luminance de 1,03 à 0,79 ; `dcraw_process` descend de
2,6 s à 0,8 à 1,0 s sur 32 Mpx. **Validation visuelle de l'utilisateur en attente**
avant fusion dans `dev`. Points ouverts : R7 et 60D au-delà d'ISO 6400 restent
au-dessus de LR, corpus R7 limité aux ISO ≥ 12800, choix de la médiane à
confirmer à l'œil ([conclusion](#conclusion-actuelle)).

Lecture rapide : [point de départ](#dematricage-20261023-depart),
[résultats LibRaw et RCD](#dematricage-20261023-resultats),
[écart au neutre LR, estimation](#dematricage-20261023-neutre-lr),
[intégration](#dematricage-20261024-integration),
[ISO 400 à 1250](#dematricage-20261024-iso-moyens),
[mesure intégrée face à LR](#dematricage-20261024-mesure-lr),
[conclusion actuelle](#conclusion-actuelle).

<a id="dematricage-20261023-depart"></a>

## Étude du 23 octobre 2026 — modèles LibRaw et RCD

Demande de l'utilisateur : traiter le bruit à l'ouverture par le dématriçage,
comparer les modèles de LibRaw, puis écrire les nôtres si nécessaire. L'étude a
commencé sur `patch_noise` (consignée alors dans le
[rapport Bruit](analyse-bruit.md)) ; elle a été
transférée le 24 octobre sur `patch_raw_decoder` avec sa sonde
(`demosaicProbe`, exclue de CTest), ses lanceurs et ses preuves. Les sections
1 à 6 décrivent l'étude ; elles ne modifiaient pas encore le décodeur.

### 1. Point de départ

- IPR ne fixe pas `user_qual` : le dématriçage de production est **AHD**.
- La LibRaw 0.22.2 de vcpkg est compilée **sans OpenMP** : tout `dcraw_process`
  tourne sur un cœur (2,6 à 2,7 s mesurés à l'ouverture d'un 32 Mpx).
- Les modules GPL de dématriçage (AMaZE, LMMSE, VCD, AFD…) ont quitté LibRaw
  en 0.20 ; restent bilinéaire, VNG, PPG, AHD, DCB, DHT, AAHD et les options
  `med_passes` (médiane 3 × 3 sur R − G et B − G) et `fbdd_noiserd`.
- `interpolate_bayer_cb` permet de remplacer la seule étape d'interpolation
  (noir, balance et conversion restent à LibRaw). Le membre est protégé ;
  une sous-classe de `LibRaw` suffit pour le régler.

### 2. Banc

Pour chaque RAW décompressé une fois, chaque variante dématrice la même
mosaïque ; sortie RGB caméra linéaire, balance boîtier, sans matrice ni
rotation. Mesures :

- **coût** de `dcraw_process` par Mpx ; nos interpolations sont découpées en
  tuiles de 512 pixels (marge 12) et réparties sur **8 threads** (base retenue) ;
- **bruit** sur 40 tuiles planes de 128 pixels et **détail** sur 20 tuiles
  texturées, choisies une fois sur AHD : HH de Haar et bande moyenne (G1 − G4)
  de Y = (R + 2G + B)/4 et de Co/Cg, énergie fine x − G1 en zone texturée ;
  rapports à AHD, médiane des tuiles ;
- **précision** sur une mosaïque synthétique : la zone visible regroupée 2 × 2
  (R, vert moyen, B, unités RAW) sert de vérité, floutée de 0,6 pixel
  (passe-bas optique), remosaïquée dans le quart supérieur gauche du même RAW
  puis dématricée ; gain et décalage par canal ajustés sur AHD ; pixels proches
  du blanc exclus (canal > 0,9, dilaté de 3 pixels). Valable à bas ISO
  seulement : à haut ISO la vérité contient le bruit.

Lanceur : `tests/study_demosaic.py --data-version N [--variants …] [--threads 8[,12,16]]`.

### 3. Campagnes

- [v001](../raw/etude-dematricage-v001-20261023T120017Z.json), 16 scènes
  (60D, R6 Mark III, R7, ISO 100 à 32000), variantes LibRaw : **historique pour
  la précision**. Sans flou ni exclusion du blanc, l'erreur était dominée par
  les canaux écrêtés par la balance, identique pour tous les algorithmes
  (vérifié indépendamment avec rawpy). Les mesures de bruit et de coût restent
  valables.
- [v002](../raw/etude-dematricage-v002-20261023T124650Z.json), mêmes scènes,
  test synthétique corrigé, variantes LibRaw et nos RCD et OpenCV EA.
- [v003](../raw/etude-dematricage-v003-20261023T133034Z.json), 32 scènes
  uniques dont 16 nouvelles du R6 Mark III (ISO 1600, 3200, 16000), variantes
  RCD, LMMSE et sélection automatique (6V1A9616 y figure deux fois, présent
  dans les deux racines).

<a id="dematricage-20261023-resultats"></a>

### 4. Résultats — v002, 16 scènes

Coût par Mpx (LibRaw sur un cœur, nos interpolations à 8 threads) ; bruit et
détail : rapports à AHD ; précision : sept scènes ISO ≤ 400, écart de CPSNR et
rapports d'erreur à AHD.

| Variante | ms/Mpx | Chroma HH | Chroma bande moy. | Luma HH | Détail luma | ΔCPSNR | Erreur chroma | Erreur luma contours |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| AHD (production) | 81 | 1 | 1 | 1 | 1 | 0 | 1 | 1 |
| Bilinéaire | 14 | 1,46 | 1,10 | 0,44 | 0,52 | −0,86 | 1,11 | 1,48 |
| VNG | 79 | 0,77 | 0,88 | 1,15 | 0,97 | −0,60 | 1,05 | 1,27 |
| PPG | 18 | 0,88 | 1,06 | 1,05 | 0,97 | +0,15 | 0,99 | 0,98 |
| AHD + médiane ×1 | 203 | 0,48 | 0,80 | 0,98 | 1,00 | −0,55 | 1,11 | 0,98 |
| AHD + médiane ×3 | 404 | 0,33 | 0,65 | 0,98 | 1,00 | −0,87 | 1,19 | 1,01 |
| AHD + FBDD 1 | 343 | 0,54 | 0,70 | 0,74 | 0,84 | −0,52 | 1,10 | 1,11 |
| DCB | 116 | 1,15 | 1,11 | 0,78 | 0,87 | +0,54 | 0,91 | 0,90 |
| DCB 2 itérations + renfort | 402 | 0,83 | 0,83 | 1,00 | 0,94 | +0,75 | 0,93 | 0,83 |
| DHT | 123 | 1,23 | 0,96 | 1,28 | 1,05 | −0,46 | 1,04 | 1,05 |
| AAHD | 182 | 0,86 | 0,89 | 1,31 | 1,10 | −0,26 | 1,03 | 1,01 |
| OpenCV EA (nous) | 10 | 1,58 | 1,11 | 0,49 | 0,56 | −0,90 | 1,14 | 1,20 |
| **RCD (nous)** | **27** | 1,18 | 0,97 | 0,79 | 0,85 | **+1,46** | **0,87** | **0,81** |
| **RCD + médiane ×1 (nous)** | **30** | **0,44** | **0,76** | 0,74 | 0,86 | +0,30 | 1,07 | 0,83 |
| RCD + médiane ×3 (nous) | 32 | 0,31 | 0,62 | 0,74 | 0,87 | −0,44 | 1,17 | 0,83 |

- **Aucune variante LibRaw ne suffit** : les médianes et FBDD retirent du bruit
  couleur, mais coûtent 2,5 à 5 fois AHD sur un cœur et perdent en précision ;
  DCB renforcé est précis mais 5 fois plus lent qu'AHD.
- **RCD**, écrit d'après la description publiée (vert par estimations
  directionnelles corrigées par rapports, direction tranchée vers le voisin
  diagonal le plus décidé, rouge et bleu par différences de couleur pondérées),
  est **le plus précis** et **3 fois plus rapide qu'AHD** : 0,9 s au lieu de
  2,6 s pour 32 Mpx. Sa bande moyenne chromatique égale celle d'AHD ; à
  ISO 32000 (7Y6A7298), les planches montrent un grain plus fin sans les
  labyrinthes colorés d'AHD.
- Sa **médiane couleur parallèle** (3 × 3 sur R − G et B − G, par tuile) ne
  coûte que 3 ms/Mpx au lieu de 120 ms/Mpx par passe dans LibRaw ; une passe
  divise le HH chromatique par 2,7, mais rend à bas ISO l'avantage de précision
  couleur de RCD (erreur 1,07 × AHD au lieu de 0,87).
- Le détail luminance des zones texturées baisse à 0,85 avec RCD : sur la
  mosaïque synthétique, l'erreur des contours baisse de 19 % ; l'énergie
  retirée est donc surtout l'artefact d'AHD, pas du détail réel.

### 5. Résultats — v003, 32 scènes ; variantes écartées

- RCD et RCD + médiane ×1 confirment v002 sur les 16 nouvelles scènes ; aux
  ISO 1600–3200 du R6 Mark III, la bande moyenne chromatique tombe à 0,45–0,77
  avec une passe, et l'erreur couleur reste au niveau d'AHD ou meilleure
  (0,59 à 1,00).
- **LMMSE** (d'après Zhang et Wu, 2005, version simplifiée sans raffinement)
  retire beaucoup de chrominance (bande moyenne 0,44 à 0,62) mais **multiplie
  le bruit de luminance par 1,4 à 1,9** et reconstruit moins bien qu'AHD à bas
  ISO : écarté. Le défaut persiste sans l'étape de réduction : la fusion douce
  des deux directions est en cause.
- **Sélection automatique par le bruit de la mosaïque** (écart des deux verts
  d'une cellule 2 × 2) : l'indice suit la texture plutôt que l'ISO (60D ISO 100
  au-dessus du R6 Mark III ISO 3200, même après retrait du déséquilibre des
  verts et normalisation photonique) : écartée. Les deux variantes ont quitté la
  sonde après la v003 ; leurs résultats restent dans la preuve.

<a id="dematricage-20261023-neutre-lr"></a>

### 6. Écart au neutre Lightroom — estimation avant intégration

[etude-bruit-neutre-lr-v001](../raw/etude-bruit-neutre-lr-v001-20261023T134413Z.json),
65 scènes (les 49 du corpus et 16 nouveaux neutres du R6 Mark III), neutres LR
exportés avec réduction de bruit à 0. Neutre IPR rendu en mémoire
(`iprRenderServer`, AHD). Sans recalage entre moteurs : sur chaque image, les
15 % de tuiles de 64 pixels les plus planes, groupées par niveau de luminance ;
HH de Haar comparé classe par classe en sRGB linéaire.

| Groupe | Scènes | Luma IPR/LR | Chroma IPR/LR | Chroma estimée RCD + médiane ×1 |
|---|---:|---:|---:|---:|
| 60D ISO ≤ 400 | 15 | 1,14 | 1,28 | 0,56 (4) |
| 60D ISO 800–3200 | 4 | 0,84 | 1,24 | 0,45 (2) |
| 60D ISO ≥ 6400 | 2 | 0,86 | 1,80 | 0,94 (1) |
| R6 Mark III ISO ≤ 400 | 12 | 1,21 | 1,27 | 0,62 (3) |
| R6 Mark III ISO 800–3200 | 19 | 0,86 | 1,25 | 0,46 (15) |
| R6 Mark III ISO ≥ 6400 | 5 | 0,92 | 1,41 | 0,62 (5) |
| R7 ISO ≥ 6400 | 8 | 1,41 | 1,77 | 1,02 (2) |

Médiane générale : chrominance 1,28 (quartiles 1,17 et 1,51), luminance 1,03.
La dernière colonne multiplie l'écart mesuré par le rapport RCD + médiane ×1 /
AHD de la même scène (entre parenthèses, scènes mesurées des deux côtés) : c'est
une **estimation**, à confirmer par un rendu IPR intégré ; elle ne couvre que le
HH et suppose que matrice et profil transmettent les rapports.

### 7. Montée en charge de l'interpolation

[etude-dematricage-v004](../raw/etude-dematricage-v004-20261023T154732Z.json),
quatre scènes (6V1A0327, 6V1A9608, 7Y6A7298 de 32 Mpx ; IMG_4002 de 18 Mpx),
poste au repos, RCD de la sonde sur le parallélisme d'OpenCV. Par image de
32 Mpx : RCD 2,5 s sur un thread, 0,42 à 0,44 s à 8, 0,33 à 0,36 s à 12,
0,32 à 0,34 s à 16 ; avec une médiane : 2,9 s, 0,53 à 0,57 s, 0,47 à 0,49 s,
0,44 à 0,48 s. `dcraw_process` complet : 0,65 à 0,68 s au lieu de 2,3 à 2,6 s
avec AHD. Le gain s'arrête vers 12 threads ; la base de 8 en donne l'essentiel.

<a id="dematricage-20261024-integration"></a>

## Intégration du 24 octobre 2026 — RCD dans le décodeur

### 8. Implémentation

- `src/raw/Demosaic.h/.cpp` : fonctions libres `Demosaic::interpolate` et
  `Demosaic::workerThreads`, sans état. RCD et médiane sont ceux de l'étude ;
  la sonde appelle désormais ce module (plus de copie de l'algorithme).
- Tuiles de 512 pixels (marge 12, réflexion de même parité au bord), réparties
  par un compteur atomique sur **au plus 8 threads**, un cœur laissé à
  l'interface (`hardware_concurrency − 1`), le thread appelant compris ; tous
  les threads sont joints sur tous les chemins (RAII), exceptions comprises.
  Seul le thread appelant consulte l'annulation (rappel non supposé
  thread-safe) ; les autres lisent un drapeau atomique.
- `RawDecoder` : sous-classe `RawProcessor` de `LibRaw` qui règle
  `interpolate_bayer_cb` (membre protégé) et `user_qual = 0` ; une exception du
  module ne traverse jamais LibRaw, l'échec ou l'annulation fait échouer le
  décodage. Noir, balance, normalisation et conversion couleur sont inchangés ;
  les fichiers X-Trans ou non Bayer gardent le chemin de LibRaw. **AHD quitte le
  chemin de production**, sans option de repli.
- Ouverture des RAW par le chemin large de LibRaw sous Windows : les dossiers
  accentués (`tests/image/colorimétrie`) se décodent désormais.
- Premier pool à 8 bandes OpenCV : charge mal répartie (27 ms/Mpx) ;
  avec le compteur partagé, **15 ms/Mpx** pour l'interpolation, 22 ms/Mpx pour
  tout `dcraw_process` (0,48 et 0,71 s sur 32 Mpx).
- Tests : `demosaicTests` (couleur uniforme exacte sur les quatre motifs, bords
  et jointures de tuiles compris ; dégradé ; absence de fausse couleur près
  d'un contour gris ; bornes des threads ; annulation ; entrées dégénérées).
  `rawDecoderTests`, `editorRawDevelopmentTests`, `imageExportTests`,
  `imageImportTests`, `rawNormalizationTests`, `rawBlackLevelEstimatorTests`
  et `cameraProfileEngineTests` passent.
- Vérification : la sonde, branchée sur le module, redonne exactement les
  rapports et la précision de l'étude sur IMG_4002 (ΔCPSNR +1,59 dB).

<a id="dematricage-20261024-iso-moyens"></a>

### 9. ISO 400 à 1250 — scènes du stock

L'inventaire des en-têtes de 3 250 RAW (dossier Images, carte CFexpress,
`colorimétrie`) montre que le stock couvre bien ces ISO pour le 60D et le
R6 Mark III ; **le R7 n'y existe qu'à ISO ≥ 12800**. Deux scènes par boîtier et
par ISO, de dossiers différents, copiées dans `base-hors-protocole` (originaux
intacts) : 60D à 400, 500, 640, 800, 1000, 1250 ; R6 Mark III à 400, 800
(une Lune, une scène `colorimétrie`), 1000, 1250 ; R7 à 12800.
Preuve : [etude-dematricage-v005](../raw/etude-dematricage-v005-20261024T092959Z.json).

| Groupe | Chroma bande moy. RCD | + médiane | Erreur chroma RCD | + médiane | ΔCPSNR RCD | + médiane |
|---|---:|---:|---:|---:|---:|---:|
| 60D ISO 400–1250 (12) | 0,95–0,98 | 0,70–0,82 | 0,91–0,99 | 1,05–1,15 | +0,1 à +1,3 | −0,6 à +0,3 |
| R6 Mark III ISO 400–1250 (8) | 0,18–0,61 | 0,14–0,48 | 0,07–0,89 | 0,07–0,99 | +1,2 à +5,0 | +0,6 à +5,0 |
| R7 ISO 12800 (2) | 1,01 | 0,82–0,83 | 0,97 | 1,08–1,09 | +0,4 à +0,5 | −0,4 à −0,5 |

Sur le R6 Mark III, AHD est nettement en retrait à ces ISO. À ISO moyen, la
vérité synthétique contient encore du bruit : la part d'erreur ajoutée par la
médiane y est en partie du bruit retiré ; ce test ne peut pas fixer seul un
seuil.

<a id="dematricage-20261024-mesure-lr"></a>

### 10. Mesure intégrée face aux neutres Lightroom

Même méthode qu'au §6, neutre IPR rendu en mémoire avec le décodeur intégré.

- [v002](../raw/etude-bruit-neutre-lr-v002-20261024T092816Z.json) : RCD, médiane
  à partir d'ISO 800 (règle initiale).
- Contre-essai non versionné : médiane à tous les ISO, sur les 27 scènes
  ISO ≤ 400 ; il a conduit à retirer la règle ISO.
- [v003](../raw/etude-bruit-neutre-lr-v003-20261024T095021Z.json) : état livré,
  une passe de médiane à tous les ISO.

| Groupe | Scènes | Chroma IPR/LR AHD (v001) | RCD + médiane (v003) | Luma IPR/LR AHD | RCD + médiane |
|---|---:|---:|---:|---:|---:|
| 60D ISO ≤ 400 | 15 | 1,28 | **0,60** | 1,14 | 0,83 |
| 60D ISO 800–3200 | 4 | 1,24 | **0,57** | 0,84 | 0,64 |
| 60D ISO ≥ 6400 | 2 | 1,80 | 1,13 | 0,86 | 0,67 |
| R6 Mark III ISO ≤ 400 | 12 | 1,27 | **0,63** | 1,21 | 0,93 |
| R6 Mark III ISO 800–3200 | 19 | 1,25 | **0,63** | 0,86 | 0,66 |
| R6 Mark III ISO ≥ 6400 | 5 | 1,41 | **0,67** | 0,92 | 0,70 |
| R7 ISO ≥ 6400 | 8 | 1,77 | 1,11 | 1,41 | 1,03 |

- Médiane générale de la chrominance : **1,28 → 0,64** (quartiles 0,58 et 0,74) ;
  luminance : **1,03 → 0,79**. Scènes au-dessus de LR en chrominance : 61 sur 65
  avec AHD, 11 avec RCD + médiane : six R7 (1,0 à 1,42, ISO 20000 à 32000),
  deux 60D à ISO 4000 et 6400 (1,07 et 1,19), 6V1A9940 (R6 Mark III ISO 100,
  1,28) et IMG_6222 (60D ISO 100, 1,00).
- v002 (médiane à partir d'ISO 800) laissait sous ISO 800 1,54 (60D) et 1,41
  (R6 Mark III) : RCD seul garde un grain chromatique fin plus fort qu'AHD
  (1,18 fois en HH). Le contre-essai puis la v003 ramènent ces scènes à 0,60 et
  0,63 ; les estimations du §6 se vérifient à 0,2 près, la mesure étant un peu plus haute.
- Comme au §6, la mesure porte sur le HH des zones planes, sans recalage ; elle
  ne juge ni les fausses couleurs sur détail ni la bande moyenne.

### 11. Temps d'ouverture

[latence-ouverture-v008-20261024T100501855988Z](../raw/latence-ouverture-v008-20261024T100501855988Z.json), protocole de la v007 (trois
scènes : 6V1A0081, 6V1A0043, 7Y6A7599 ; trois ouvertures chacune ; profil
Camera Neutral, normalisation active). Les deux R6 Mark III et le R7 étant
désormais dans `base-hors-protocole`, le lanceur les y cherche.

| Étape, ms | v007 (AHD, 17 octobre) | v008 (RCD + médiane) |
|---|---:|---:|
| `dcraw_process` (étape `unpack_and_process`) | 2 597 à 2 683 | **799 à 968** |
| Import complet (`import_ms`) | 3 735 à 4 062 | **2 038 à 2 306** |

L'ouverture gagne 1,7 s environ (−43 %). Constat sans rapport avec le
dématriçage : la **première** ouverture de 7Y6A7599 a pris 56 s, dont 54 s dans
`find_profiles` (recherche du profil d'objectif Sigma 70-200 dans le dossier
LCP d'Adobe, mise en cache ensuite : 7 ms aux ouvertures suivantes). Cette
recherche à froid est à traiter séparément.

<a id="conclusion-actuelle"></a>

## Conclusion actuelle et prochaines actions — 24 octobre 2026

### Décision proposée

RCD avec une passe de médiane couleur à tous les ISO remplace AHD à
l'ouverture. Il est plus précis qu'AHD sur la mosaïque synthétique (ΔCPSNR
+0,3 dB, erreur des contours −17 % ; erreur couleur 1,07 fois à bas ISO), plus
rapide (0,8 à 1,0 s au lieu de 2,6 s sur 32 Mpx) et ramène le bruit du neutre sous
celui de Lightroom sur le 60D et le R6 Mark III. La règle ISO de l'étude
(médiane à partir de 800) est retirée : sous ISO 800 elle laissait 1,41 à 1,54
fois le bruit couleur de LR.

### À valider par l'utilisateur

- Rendu à l'ouverture face à LR et à l'ancienne version, à plusieurs ISO :
  grain, fausses couleurs sur détail fin, moiré, couleurs saturées.
- Perte éventuelle de petites touches de couleur (la médiane 3 × 3 retire des
  détails colorés d'un ou deux pixels).
- Ressenti du temps d'ouverture.

### Limites du corpus et des mesures

- Trois boîtiers Canon à matrice de Bayer ; aucun X-Trans (chemin LibRaw
  conservé, non mesuré), aucun autre fabricant.
- R7 : aucune scène sous ISO 12800, ni dans le corpus ni dans le stock.
- Précision mesurée sur une vérité synthétique (regroupement 2 × 2 flouté),
  fiable à bas ISO seulement ; aucune référence sans bruit à ISO élevé.
- Comparaison à Lightroom sur le HH des zones planes, sans recalage entre
  moteurs ; ni la bande moyenne ni le détail coloré n'y sont comparés.
- Planches de la sonde en RGB caméra, sans matrice ni profil.
- Un seul poste de mesure (20 threads logiques) ; aucune machine de 4 à
  6 cœurs mesurée.
- RCD écrit d'après la description publiée, sans comparaison au code de
  référence (GPL, non consulté).

### Besoins

- Prises du R7 à ISO 100 à 6400, avec leurs neutres LR.
- Neutres LR des 22 scènes ajoutées (ISO 400 à 1250, R7 12800) pour étendre
  la comparaison à LR.
- Scènes à détail fin coloré (tissus, feuillages, textes, motifs répétitifs).

### Directives

- Avant fusion : validation visuelle de l'utilisateur ; puis fusion de
  `patch_raw_decoder` dans `dev` en `--no-ff`, tag selon le compteur de patchs,
  CI verte, publication autorisée.
- Après fusion, le débruitage reprend sur `patch_noise` : signature spectrale et
  vérité par paires à remesurer avec RCD (voir le
  [rapport Bruit](analyse-bruit.md#conclusion-actuelle)).
- Toute évolution du dématriçage passe par `Demosaic` et `demosaicTests` ;
  pas de second algorithme en parallèle ni d'option de repli.
- Les threads restent issus de l'hôte et plafonnés à 8 ; mesurer sur une
  machine plus modeste avant d'élever ce plafond.

### Prochaines actions

1. Validation visuelle de la nouvelle ouverture par l'utilisateur.
2. Fusion dans `dev`, tag et publication après accord.
3. Reprise du débruitage sur `patch_noise`.
