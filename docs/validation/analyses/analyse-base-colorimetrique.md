# Base colorimétrique — Rapport d'analyse évolutif

**Date initiale :** 6 octobre 2026. **Dernière mise à jour :** 14 octobre 2026.
**Statut actuel :** Camera Neutral accepté visuellement ; module IHM et ZIP local
disponibles (§25–26). Normalisation limitée à cinq RAW exacts, désactivée par défaut.
La reprise du 14 octobre sur `patch_raw_opening` mesure 19 RAW (§29) ; le
prototype Couleur reste sauvegardé et non validé sur `patch_color`.
Standard, résidus et fidélité physique restent ouverts. Le nouveau cas
0081 confirme un écart neutre rouge/sombre à investiguer (§28), sans annuler
les appréciations historiques sur d'autres scènes. Les
[CSV et courbes](../metrics/README.md) sont dérivés de ces preuves.

**Historique des étapes :** absence de changement des exports
confirmée sur le lot du 6 octobre ; correctif d'espace DCP appliqué puis
mesuré sur les TIFF v2 du 7 octobre ; audit Canon et perte de précision
dans l'encodage Qt réalisés (§14) ; fidélité absolue non validée.
Correctifs de sortie sRGB et d'estimation des noirs appliqués et compilés
ensuite (§15), puis vérifiés sur les six TIFF v3 (§16) : précision des
faibles valeurs confirmée, écart global de rendu avec Lightroom persistant.
Expérience A/B des blancs Canon exécutée sur trente variantes (§17) :
gain partiel et risque de clipping ; pas d'intégration en production.
Investigation profils/rendu tonal exécutée (§18) : effet des profils non
uniforme, modèles ajustés insuffisants, piste d'une courbe de base implicite.
Nouveaux TIFF LR `-2` analysés (§19) : optique harmonisée, WB Custom
5200 K/0 à clarifier ; écart tonal persistant et modèles non intégrés.
Lot As Shot reçu et analysé (§20) : six Adobe Standard et cinq Camera
Neutral conformes aux contrôles ; comparaison au même profil désormais
possible, rendu IPR toujours plus sombre ; fidélité/parité non validées.
Courbe explicite Camera Neutral isolée (§21) : stabilisation de teinte utile
mais bénéfice non uniforme, déficit lumineux persistant ; aucun changement
de production. Ordre/offset/gamut isolés ensuite (§22) : exposition Adobe
de base +0,26/+0,28 EV observée dans six DNG de diagnostic ; sa prise en
compte expérimentale améliore les cinq références Neutral mais laisse un
résidu. Normalisation des blancs corroborée par les DNG puis courbe
préservant les teintes isolées (§23) : déficit lumineux moyen pratiquement
résolu sur les cinq références Neutral ; résidu colorimétrique persistant,
notamment sur 9421. Vérification Adobe Standard et intégration contrôlée
réalisées ensuite (§24) : normalisation par manifeste explicite lié aux
empreintes RAW/DCP, désactivée par défaut ; cinq décodages Windows vérifiés
à un code 16 bits près du témoin avec garde anti-inversion. Déploiement
général et parité Lightroom toujours non validés.

**Mesures :** [JSON v2](../raw/mesure-base-colorimetrique-v2-20261006.json).
**Dernière campagne :** [Adobe Standard](../raw/mesure-normalisation-adobe-standard-20261008.json)
et [décodages Windows intégrés](../raw/verification-normalisation-production-20261008.json), §24.
**Investigation suivante :** courbe implicite Adobe Standard et résidus de
9421 ; généralisation des métadonnées Canon après validation indépendante,
sans compensation constante apprise.
**Protocole :** [base colorimétrique v1](../protocols/protocole-base-colorimetrique-v1.md).
**Analyseur :** [analyse_base_colorimetrique_v2.py](../../../tests/analyse_base_colorimetrique_v2.py).

Ce document est désormais **l'unique rapport d'analyse de la base colorimétrique**.
Il regroupe la réanalyse des TIFF et l'investigation du pipeline du 6 octobre.
Les anciens rapports v1/v2 ont été fusionnés ici ; les JSON historiques restent
inchangés. Les erreurs de la v1 et leur correction sont consignées en section 4.
Les sections 1 à 7 décrivent la réanalyse initiale, les sections 8 à 10
l'investigation suivante. Aucun correctif du moteur de production n'a été
appliqué pendant ces deux étapes : seuls les outils de diagnostic ont été ajoutés.
La section 11 confronte ces résultats aux sources RawSpeed et Canon CR2/CR3.
La section 12 consigne le correctif ultérieur ; les mesures des sections
précédentes décrivent toujours le moteur avant ce correctif.
La section 13 analyse les nouveaux exports `-IPR-2.tiff`.
La section 14 isole les métadonnées Canon, les zones noires et l'encodage de sortie.

## 1. Résultat principal : les six nouveaux fichiers sont inchangés

Le dossier `tests/image/base/neutre` contient six TIFF IPR réexportés entre
21 h 52 et 21 h 53, ainsi qu'une archive `6V1A9950-IPR.zip` contenant les
six anciens TIFF (horodatages locaux des entrées autour de 12 h 51–12 h 56).
L'intégralité de chaque entrée décompressée a été lue, sans extraction ni
modification de l'archive. Les SHA-256 des **fichiers complets**, et pas
seulement de quelques pixels, ont été comparés aux nouveaux fichiers.

| Scène | Ancien/nouveau TIFF IPR | SHA-256 commun, préfixe |
| --- | --- | --- |
| 6V1A0007 | Identiques octet pour octet | `bf2b3a6a2a16f31a` |
| 6V1A0037 | Identiques octet pour octet | `b0304b6f6636967c` |
| 6V1A0043 | Identiques octet pour octet | `d5976169c8121348` |
| 6V1A0327 | Identiques octet pour octet | `d3c23b03ed54a0fc` |
| 6V1A9421 | Identiques octet pour octet | `fdf2340ca6682777` |
| 6V1A9950 | Identiques octet pour octet | `d83e5b0542a5f94a` |

Chaque TIFF IPR fait 258 263 634 octets. Les empreintes anciennes et nouvelles,
les dates, l'empreinte de l'archive, les ICC et les paramètres Lightroom
embarqués sont conservés dans le JSON v2. L'identité binaire implique une
MAE et un ΔE ancien/nouveau **nuls sur tous les pixels**. Ce résultat ne dépend
ni du calage Lightroom, ni de la précision de la formule colorimétrique.

**L'absence de différence constatée visuellement est donc réelle.** Elle ne
permet pas à elle seule d'identifier le binaire ayant produit les exports,
mais elle est cohérente avec le fonctionnement du code décrit ci-dessous.

## 2. Pourquoi 6500 → 5200 K ne change pas ces rendus neutres

`referenceWhiteBalanceKelvin.value_or(5200.0)` signifie « utiliser 5200 si
la température de référence est absente ». Cela ne remplace pas une valeur
déjà calculée depuis le RAW.

Le chemin du moteur est le suivant :

1. Dans [`RawDecoder.cpp`](../../../src/raw/RawDecoder.cpp), le développement
   non automatique active `use_camera_wb = 1` : LibRaw utilise les gains WB
   du boîtier. Ces gains n'ont pas été modifiés par le changement 6500/5200.
2. `estimateWhiteBalanceKelvin()` estime une température depuis `cam_mul` et
   la matrice caméra, puis remplit `referenceWhiteBalanceKelvin`.
3. Le profil DCP utilise cette estimation ; son propre repli est actuellement
   `sourceWhiteBalanceKelvin.value_or(5500.0)`. Il ne dépend pas des replis
   6500/5200 changés dans l'éditeur.
4. À l'ouverture fraîche et au reset, l'éditeur place le curseur à la
   référence arrondie. Les réglages restaurés d'un sidecar restent une autre
   possibilité lors d'une ouverture normale.
5. `renderWorkingImage()` compare la température cible à cette même référence.
   Avec les autres réglages neutres et la nuance à zéro, il renvoie directement
   la source : aucune adaptation WB supplémentaire n'est appliquée.

Exemple déjà tracé dans la campagne technique du générateur : la référence
de `6V1A0327.CR3` était environ **4924,21 K**, avec curseur à **4924 K**.
`value_or(6500)` et `value_or(5200)` donnent tous deux cette même référence
lorsqu'elle est disponible.

Même en l'absence de référence, changer **à la fois** le repli du curseur
neutre et le repli du rendu conserve leur égalité au reset : anciennement
6500/6500, maintenant 5200/5200. Cela ne constitue donc pas une correction
colorimétrique à l'ouverture. En revanche, un ancien réglage absolu restauré
sur une image sans référence pourrait être interprété différemment : le
changement n'est pas universellement anodin hors de ce corpus.

**5200 K n'est pas une référence « As Shot Canon » universelle.** Une WB As Shot
dépend de la prise de vue ; les valeurs affichées par deux moteurs peuvent
aussi différer pour des raisons de calibration. Le changement du repli ne
modifie ni les gains RAW, ni la référence D65 de l'espace sRGB, ni toutes les
composantes du profil DCP.

## 3. Audit des références Lightroom reçues

Les douze TIFF sont en 16 bits, avec ICC sRGB, orientation 1 et sans
compression. Les six Lightroom ont exposition, netteté et réductions de bruit
luminance/couleur à zéro, et le profil **Adobe Standard**. La WB et la
correction optique ne sont toutefois pas homogènes :

| Scène | WB XMP Lightroom | Température | Nuance | Profil objectif activé |
| --- | --- | ---: | ---: | --- |
| 0007 | Custom | 5200 K | +5 | Oui |
| 0037 | Custom | 5200 K | 0 | Oui |
| 0043 | Custom | 5200 K | 0 | Non |
| 0327 | As Shot | 5200 K | +5 | Oui |
| 9421 | As Shot | 5200 K | +5 | Oui |
| 9950 | As Shot | 5200 K | +5 | Oui |

Ce corpus permet d'observer des écarts de rendu. Il n'isole pas encore un
défaut de WB ou un défaut DCP. Les TIFF IPR n'embarquent pas l'état détaillé
des réglages ; leur identité avec l'archive est certaine, mais la provenance
exacte des paramètres doit être consolidée avec le manifeste du générateur
pour la prochaine campagne.

## 4. Corrections de méthode par rapport à la v1

### Précision et métrique

Le script v1 ouvre les TIFF via Pillow, puis `convert('RGB')`. Sur ces TIFF
couleur, le tableau devient **uint8** : la mesure n'était donc pas réalisée
sur les valeurs natives 16 bits annoncées.

De plus, `compare_regions()` calcule `norm(Lab_LR - Lab_IPR)`, donc un
**ΔE76**, pas un ΔE00. La fonction `delta_e_2000()` de la v1 n'était pas
appelée et contenait elle-même une formule incorrecte du terme de rotation.
La v2 lit directement les pixels uint16, calcule sRGB → XYZ → Lab D65 en
double précision et emploie CIEDE2000 avec `kL = kC = kH = 1`, vérifié sur
16 paires du jeu de référence de [Sharma, Wu et Dalal](https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/).

Les différences de teinte utilisent désormais la distance circulaire courte,
et excluent les pixels de chroma < 5 pour éviter des angles instables sur
les gris. Les différences a*/b*, L* ou chroma restent des **observations** :
elles ne sont pas automatiquement étiquetées « défaut WB », « défaut gamma »
ou « hors gamut ». Une chroma supérieure à 100 ne prouve pas une sortie de gamut.

### Cohérence du rapport avec les données brutes

Certaines colonnes du rapport v1 ne correspondent pas au JSON de calcul.
Exemple de sa première ROI, `6V1A0007 / voilier_blanc` :

| Quantité, LR − IPR | Texte du rapport v1 | JSON de calcul v1 |
| --- | ---: | ---: |
| ΔL* | −12,34 | +25,2505 |
| Δa* | +18,76 | −0,0346 |
| Δb* | −5,67 | −1,0022 |

La valeur ΔE76 de cette ROI, environ 25,52, correspond au calcul historique,
mais pas l'interprétation d'une forte dominante magenta. Il faut utiliser le
[JSON de calcul historique](../raw/mesure-base-colorimetrie-v1-20261006.json)
pour auditer ces valeurs, et distinguer celui-ci du
[JSON de synthèse historique](../raw/mesure-base-colorimetrique-v1-20261006.json).
Les deux restent conservés sans modification.

### Régions et géométrie

Quatre des 26 rectangles historiques dépassent l'image :

- `0327 / crashpad_rouge` : x2 = 5000, largeur IPR = 4639 ;
- `9421 / peau` : x2 = 5000, largeur = 4639 ;
- `9421 / textile` : x1 = 5000, donc rectangle entièrement hors image en x ;
- `9950 / ombre` : x2 = 5000, largeur = 4639.

La v1 les tronquait silencieusement, parfois jusqu'à une seule colonne. La v2
les rejette explicitement : **22 ROI géométriquement valides**, sans certifier
leur sujet. L'inspection de la planche montre aussi que les ROI nommées
« crashpads » de 0327 ne ciblent pas les tapis situés au bas de la photo.
Les noms historiques sont donc des identifiants, pas des mesures fiables
de cyan, vert, rouge ou peau.

IPR mesure 4639 × 6959 ou 6959 × 4639, Lightroom 4640 × 6960 ou 6960 × 4640.
Ce constat **ne prouve pas que Lightroom ajoute une bordure** ni qu'IPR
correspond exactement à la taille native du capteur. La v2 teste seulement
les quatre retraits possibles d'une ligne/colonne Lightroom, avec score
sur contours. Cela ne corrige pas les différences de distorsion, interpolation
ou cadrage entre moteurs. Les écarts pixel à pixel restent descriptifs,
pas une mesure de fidélité de laboratoire.

## 5. Résultats corrigés IPR / Lightroom

Grille régulière de **504 600 pixels natifs par scène**, pas spatial 8,
sans redimensionnement ni interpolation pour les mesures. Les ROI historiques
valides sont mesurées séparément au pas 4 dans le JSON. Convention : **LR − IPR**.

| Scène | ΔE00 moyen global | ΔE00 médian | ΔL* moyen | Δa* moyen | Δb* moyen |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0007 — voilier | 18,94 | 21,35 | +20,80 | −0,67 | −2,36 |
| 0037 — végétation | 11,42 | 8,52 | +4,28 | −2,79 | +3,79 |
| 0043 — fleur | 4,64 | 1,85 | +2,20 | −0,90 | +2,73 |
| 0327 — grimpeur et tapis | 11,55 | 9,39 | +11,34 | +1,05 | +3,41 |
| 9421 — salle d'escalade | 21,55 | 22,63 | +19,89 | +2,40 | +19,70 |
| 9950 — ciel et végétation | 12,06 | 8,53 | +11,69 | −0,89 | +1,84 |

**Ces nombres ne doivent pas être soustraits aux ΔE de la v1 pour annoncer
une amélioration** : métrique, précision, échantillonnage et périmètre spatial
ont changé. Les fichiers IPR, eux, sont identiques.

Les six ΔL* moyens positifs indiquent un rendu IPR **plus sombre** sur ce
corpus. La tonalité de base mérite donc d'être isolée avant de chercher une
correction globale de température. Sur le voilier, par exemple, la première
ROI valide donne ΔL* ≈ +25,15, Δa* ≈ −0,027 et Δb* ≈ −1,01 dans la v2 :
l'écart y est principalement lumineux, pas la forte dominante magenta
décrite dans la v1.

La salle 9421 conserve aussi un écart important de chrominance : Δb* moyen
≈ +19,70 et chroma LR supérieure en moyenne d'environ 21,22. Cet écart ne
peut pas être attribué à la seule WB à partir de ces TIFF : rendu tonal,
profil, éclairage, clipping et calibration interviennent conjointement.

La fleur 0043 reste la scène globalement la plus proche, mais sa médiane
faible ne valide pas la couleur de la fleur : le fond sombre occupe une
large partie de l'image. Son percentile 95 de ΔE00 vaut environ 19,64.

Attention également à distinguer « sombre » et « noir écrêté » : sur 0043,
30,84 % des pixels échantillonnés Lightroom ont **au moins un canal** à
≤ 1/65535, contre 0,45 % pour IPR. Ce n'est pas un pourcentage de pixels
entièrement noirs. Un rendu globalement plus sombre n'implique donc pas
automatiquement davantage d'écrêtage des trois canaux.

## 6. Plan établi après la réanalyse des TIFF

Les points 1 et 2 ont ensuite été investigués en section 8. Les points 3 à 5
restent nécessaires pour la validation contrôlée et physique.

1. **Ne pas considérer le passage du repli à 5200 K comme un correctif WB
   validé.** Conserver la WB de prise de vue comme base ; comparer les gains
   réellement appliqués, pas uniquement les nombres affichés en kelvins.
2. **Isoler le rendu tonal de base** sur 0007 et 9950 : tracer les gains RAW,
   le niveau noir/blanc, l'exposition de base, les étapes DCP et la luminance
   avant/après profil. L'assombrissement observé ne justifie pas encore un
   ajout fixe d'EV pour toutes les images.
3. **Préparer une comparaison WB/DCP contrôlée** : profil explicitement
   identifié, WB As Shot des deux côtés ou neutralisation sur une vraie
   référence grise, paramètres sauvegardés dans un manifeste, corrections
   optiques harmonisées. Une teinte de peau ou une roche n'est pas une
   référence grise certifiée.
4. **Redéfinir les ROI sur les sujets réels et vérifier le calage** avant une
   nouvelle mesure ciblée des tapis, prises ou carnations. Les couleurs
   physiques des prises ne se déduisent pas de ces rectangles historiques.
5. **Faire la phase charte quand les prises de vue sont disponibles.** Il
   n'est pas nécessaire d'obtenir ΔE00 < 3 contre Lightroom pour mesurer la
   fidélité à une charte. Lightroom est une référence de comparaison de
   rendu, pas une vérité physique.

Aucune correction du moteur n'a été ajoutée ici : l'objectif était de
déterminer l'effet du changement et de fiabiliser les mesures. Les prochaines
modifications doivent partir de traces du pipeline et de références contrôlées.

## 7. Reproduction et tests

```bash
python3 -m unittest discover -s tests -p test_analyse_base_colorimetrique.py
python3 tests/analyse_base_colorimetrique_v2.py \
  --archive tests/image/base/neutre/6V1A9950-IPR.zip \
  --output docs/validation/raw/mesure-base-colorimetrique-v2-nouvelle-execution.json \
  --contact-sheet build-colorimetrie-inventory/comparaison-v2-nouvelle-execution.png
```

Les sorties existantes sont refusées pour conserver les mesures historiques.
La planche locale de contrôle `build-colorimetrie-inventory/comparaison-v2.png`
est une visualisation réduite 8 bits avec rectangles historiques, ignorée par
Git, dont la disponibilité actuelle n'est pas garantie ; elle n'est jamais
utilisée comme source de mesure. La comparaison
cryptographique parcourt les fichiers entiers ; les statistiques Lightroom
utilisent les grilles documentées.

Six tests ciblés du nouvel analyseur ont passé : 16 paires CIEDE2000 de
référence, identité/symétrie, blanc/noir sRGB, incrément d'un code sur 65535,
rejet des ROI hors cadre, signe des écarts et identité des entrées ZIP.
Les règles de sélection incluent désormais cette suite pour les analyseurs
de base colorimétrique et pour leur lecteur TIFF partagé.
La vérification Python de fin de réanalyse a passé **46 tests** : 36 pour
les trois analyseurs (base, mélangeur, bruit) et 10 pour le sélecteur.
L'application n'a pas été recompilée dans cette étape d'analyse.

## 8. Investigation du pipeline RAW / WB / DCP / LCP

### 8.1 Méthode et traçabilité

**Preuves :** [mesures de l'investigation](../raw/investigation-base-tonale-20261006.json),
[sonde C++](../../../tests/tonal_base_probe.cpp),
[analyse et expérience DCP](../../../tests/investigate_tonal_base.py).

La sonde Windows a été compilée avec le moteur courant et LibRaw **0.22.2**.
Elle réutilise le code source exact des fonctions privées d'estimation WB/noir
dans un espace de noms de diagnostic, sans changer leur API ni le moteur.
Les options de développement reproduisent le chemin DCP : WB boîtier active,
WB automatique inactive, `output_color=0`, `use_camera_matrix=0`, sortie
16 bits linéaire, `no_auto_bright=1`.

Les six RAW sont développés intégralement. Une branche n'applique pas le LCP ;
l'autre l'applique **à pleine résolution**, avant tout échantillonnage. Les
étapes ponctuelles DCP sont ensuite calculées sur une grille native de pas 8,
soit 504 600 pixels par scène. Les RGB caméra ne sont pas assimilés à une
luminance sRGB. Les intermédiaires float32 restent dans le dossier local ignoré
`build-tonal-base-20261006` ; le JSON conserve leurs empreintes, les métadonnées
et les résultats. Aucun RAW, TIFF, sidecar ou profil Adobe installé n'a été modifié.

La réimplémentation Python limitée au profil étudié reproduit la sortie DCP
C++ à **1,22 × 10⁻⁶** près au maximum sur les composantes linéaires mesurées.
Elle sert uniquement à isoler l'espace des tables ; ce n'est pas un moteur
DNG de référence. L'encodage sRGB analytique de la sonde et les TIFF IPR existants
ne sont pas strictement identiques : ΔE00 moyen de **0,039 à 0,145**, écart maximal
jusqu'à environ **200 codes sur 65535**. Cette différence résiduelle n'a pas été
attribuée ; ne pas revendiquer une reproduction bit à bit de l'export.

### 8.2 Balance des blancs et niveaux RAW

Sur **les six fichiers**, LibRaw lit les mêmes gains `cam_mul` :
`[1938, 1024, 1733, 1024]`. Après traitement, `pre_mul` vaut
`[1,892578125, 1, 1,6923828125, 1]`. L'estimation IPR donne chaque fois
**4924,211 K**. Le repli 5200 K n'est donc utilisé sur **aucun** de ces RAW.
Cela confirme la conclusion de la section 2 par une mesure du corpus complet.
Ce constat ne généralise pas ces gains à toutes les photos de ce boîtier.

| Scène | Noir estimé R/G1/B/G2 | Maximum LibRaw après traitement |
| --- | --- | ---: |
| 0007 | 512 / 512 / 512 / 512 | 14618 |
| 0037 | 512 / 512 / 512 / 512 | 15871 |
| 0043 | 512 / 512 / 512 / 512 | 15871 |
| 0327 | 512 / 512 / 512 / 512 | 15679 |
| 9421 | 2052 / 2053 / 2053 / 2053 | 14331 |
| 9950 | 512 / 512 / 512 / 512 | 15871 |

Le maximum avant traitement est 16383. Le maximum après traitement est un état
interne LibRaw, **pas** une mesure indépendante du blanc physique du capteur.
Le niveau noir a été estimé depuis les marges sur les six fichiers ; son exactitude
physique n'est pas validée par cette campagne. Ne pas supprimer cette soustraction
pour simplement éclaircir le rendu.

Le champ `dng_levels.baseline_exposure` vaut **−999** partout. C'est la valeur
d'initialisation de LibRaw, donc une information non renseignée ici, **pas −999 EV**
à appliquer et pas la preuve d'un décalage Adobe manquant connu.
[Source LibRaw](https://github.com/LibRaw/LibRaw/blob/master/src/utils/init_close_utils.cpp).

### 8.3 Contenu du DCP réellement sélectionné

Profil : `Canon EOS R6 Mark III Adobe Standard.dcp`, empreinte SHA-256 :
`b4687f2d793a06c79c21aed9b9aaf61b274408c20766ad98ae87a7f5508d02e5`.

| Élément | Lecture du fichier |
| --- | --- |
| ForwardMatrix1 / 2 | Toutes deux présentes |
| Illuminants | A / D65 |
| HueSatMap | 90 × 30 × 1, deux tables |
| LookTable | 36 × 8 × 16 |
| Encodage des tables | Tags absents : valeur par défaut 0, linéaire |
| ProfileToneCurve | **Absente** |
| BaselineExposureOffset | **Absent**, repli du moteur 0 EV |

IPR applique donc les matrices et les tables, mais aucune courbe DCP pour ce
profil. L'absence de courbe n'est pas, à elle seule, une violation du format :
ce tag est optionnel. La spécification définit les tables HSV dans les
primaires **ProPhoto RGB linéaires**, pas sRGB.
[Adobe DNG 1.7.1, p. 51, 63–65 et 103](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf).

Dans le code courant, `DcpProfile::apply()` convertit d'abord XYZ D50 vers
sRGB, puis appelle `applyHueSatMap()` pour les deux tables : **l'espace des
tables est incorrect**. Autre point d'audit : pour un profil SDR encodé 1,
le code encode les trois composantes avant HSV, alors que la spécification
décrit l'encodage de V. Ce second point n'affecte pas ce corpus, encodé 0.
La courbe présente dans d'autres profils est interpolée linéairement dans IPR,
alors que le document recommande une spline cubique ; elle est absente ici.
[Source normative Adobe, mêmes sections](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf).

### 8.4 La correction optique explique-t-elle l'assombrissement ?

Y est calculé après DCP en sRGB linéaire, RGB bornés à [0,1]. La colonne
EV est `log2(moyenne(Y_LR) / moyenne(Y_IPR))` : un **rapport descriptif de
moyennes**, pas une correction d'exposition calibrée.

| Scène | Y DCP sans LCP | Y DCP avec LCP | Y TIFF IPR | Y TIFF LR | Rapport LR/IPR en EV |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0007 | 0,12831 | 0,12893 | 0,12882 | 0,34143 | +1,406 |
| 0037 | 0,02979 | 0,03679 | 0,03668 | 0,06533 | +0,833 |
| 0043 | 0,01406 | 0,01393 | 0,01382 | 0,02677 | +0,953 |
| 0327 | 0,04553 | 0,04717 | 0,04706 | 0,12113 | +1,364 |
| 9421 | 0,09074 | 0,09470 | 0,09459 | 0,28828 | +1,608 |
| 9950 | 0,05115 | 0,06895 | 0,06883 | 0,17685 | +1,361 |

**Le LCP n'est pas la cause globale recherchée.** Il augmente notamment la
moyenne Y d'environ 0,30 EV sur 0037 et 0,43 EV sur 9950. Les variations
incluent le remappage spatial et les corrections photométriques : elles ne
mesurent pas exclusivement le vignettage. Les moyennes LR de cette section
utilisent la grille à l'origine (0,0), sans le retrait de bordure choisi en
section 5, d'où de petits écarts entre les deux tableaux.

### 8.5 Expérience : changer uniquement l'espace des tables DCP

Même RGB caméra corrigé optiquement, même WB, mêmes matrices et tables,
aucune courbe ajoutée, aucun EV ajouté. On compare le chemin actuel en sRGB
à une branche de diagnostic passant par ProPhoto linéaire avant les tables,
puis revenant en sRGB. Les primaires et le blanc D50 sont documentés par
l'[ICC](https://registry.color.org/rgb-registry/files/ROMMRGB.pdf).
La branche garde les mêmes règles de clipping que le moteur : c'est une
expérience contrôlée, **pas** une implémentation DNG complète certifiée.

| Scène | ΔE00 moyen actuel ↔ essai | Y essai ProPhoto | ΔE00 actuel / LR | ΔE00 essai / LR |
| --- | ---: | ---: | ---: | ---: |
| 0007 | 0,47 | 0,12613 | 18,92 | 19,26 |
| 0037 | 0,33 | 0,03562 | 11,41 | 11,45 |
| 0043 | 0,55 | 0,01378 | 4,65 | 4,42 |
| 0327 | 0,37 | 0,04678 | 11,54 | 11,61 |
| 9421 | 1,18 | 0,09215 | 21,55 | 21,54 |
| 9950 | 0,50 | 0,06635 | 12,04 | 12,38 |

Le choix des primaires a un effet mesurable, mais **ne comble pas l'écart
lumineux** et n'améliore pas systématiquement la proximité Lightroom.
Les comparaisons LR de cette expérience sont non recalées optiquement,
avec WB non harmonisée : ce ne sont pas des scores d'acceptation.
La conformité de l'espace DCP reste un correctif justifié indépendamment
de ces scores ; elle ne doit pas être vendue comme la solution unique.

### 8.6 Piste tonale restante, et niveau de certitude

Le moteur applique ici un développement sans éclaircissement automatique,
puis un DCP sans courbe tonale explicite. Il faut maintenant définir et
tester la **transformation de rendu de base**, distincte de l'exposition
utilisateur et de la fidélité colorimétrique. L'écart de moyennes de
0,83 à 1,61 EV ne justifie pas un gain fixe choisi arbitrairement.

À titre de comparaison d'implémentation, RawTherapee traite les tables dans
ProPhoto et contient un mécanisme de courbe par défaut pour certains profils
Adobe sans courbe, identifié par leur copyright. Ce mécanisme confirme que
« même fichier DCP » n'implique pas « même rendu final ». Il ne prouve ni
l'algorithme exact de Lightroom actuel, ni que la condition RawTherapee
s'applique à notre profil, dont le tag copyright n'est pas présent dans
l'inventaire lu. Aucune courbe ni portion de code RawTherapee n'a été copiée.
[Source primaire RawTherapee, `dcp.cc`](https://github.com/Beep6581/RawTherapee/blob/dev/rtengine/dcp.cc).

**Bilan :** WB de repli écartée sur ce corpus ; LCP écarté comme explication
globale ; anomalie d'espace DCP confirmée ; tonalité de base encore à isoler
et à choisir. Ni la WB de référence estimée, ni les niveaux RAW, ni la
fidélité absolue ne sont pour autant validés physiquement.

## 9. Images à fournir et prochaine étape

### Pour poursuivre maintenant

**Aucun nouveau RAW nécessaire.** Les six scènes suffisent pour les essais
de conformité DCP et de rendu tonal. Ne pas réexporter toute la banque avant
qu'une modification du moteur soit prête.

Pour lever les ambiguïtés Lightroom, un petit lot complémentaire serait utile,
à partir des **mêmes RAW 0007, 9421 et 9950** :

1. Profil **Adobe Standard**, WB **Telle quelle / As Shot**, sans forcer 5200 K ;
   noter température et nuance réellement affichées.
2. Exposition et tous les réglages utilisateur à zéro ; courbe utilisateur
   linéaire ; netteté, bruit, masques, IA, réglages locaux et corrections
   optiques optionnelles désactivés. Noter toute correction intégrée imposée.
   Une courbe utilisateur linéaire ne signifie pas un rendu RAW sans tonalité
   interne Lightroom.
3. Export **TIFF 16 bits sRGB, SDR**, sans redimensionnement ni netteté de sortie,
   avec toutes les métadonnées. Garder les références actuelles intactes,
   par exemple dans `tests/image/base/neutre/controle-as-shot/`.
4. Fournir les réglages via XMP ou capture : profil, version de processus,
   WB, optique, tonalité, détail. IPR doit produire la branche correspondante
   **sans LCP** avec manifeste, sinon la comparaison reste déséquilibrée.

### Pour valider la fidélité physique ensuite

Prévoir si possible une charte colorimétrique avec valeurs de référence
connues, une plage grise neutre et plusieurs gris, photographiés en RAW sous
lumière uniforme sans reflets : une scène lumière du jour et une sous
éclairage artificiel stable, bas ISO, charte suffisamment grande et non
écrêtée. Une série bracketée −1 / 0 / +1 EV avec éclairage fixe aidera à
séparer le rendu tonal de l'exposition.

Sans charte, les scènes existantes restent utiles pour la régression et
la comparaison visuelle, mais les couleurs des prises vues sur Internet
ne remplacent pas une mesure de leurs réflectances sous l'éclairage réel.

**Ordre conseillé :** tests synthétiques DCP → correction de l'espace des
tables → comparaison contrôlée sans LCP → étude d'une tonalité de base
explicite → validation charte. Les corrections de production constituent
la prochaine action, pas un résultat déjà implémenté par cette investigation.

## 10. Reproduction de l'investigation

Sous Windows, dans l'environnement Qt/OpenCV/LibRaw habituel :

```powershell
cmake --build build-windows-msvc --config Release --target tonalBaseProbe
.\build-windows-msvc\Release\tonalBaseProbe.exe build-tonal-base-nouveau `
  tests/image/base/6V1A0007.CR3 tests/image/base/6V1A0037.CR3 `
  tests/image/base/6V1A0043.CR3 tests/image/base/6V1A0327.CR3 `
  tests/image/base/6V1A9421.CR3 tests/image/base/6V1A9950.CR3
```

Puis, depuis la racine du dépôt avec Python/NumPy/Pillow :

```bash
python3 tests/investigate_tonal_base.py --probe build-tonal-base-nouveau \
  --output docs/validation/raw/investigation-base-tonale-nouvelle-execution.json
python3 -m unittest discover -s tests -p test_analyse_base_colorimetrique.py
```

La sonde refuse un dossier de sortie existant ; l'analyse refuse d'écraser
un JSON existant. La cible CMake est manuelle, exclue du build par défaut et
de CTest : aucun corpus privé n'est requis par la CI. Les tests synthétiques
du parseur DCP, des tables et des transformations sont dans la suite de
base colorimétrique, sélectionnée par les règles des tests ciblés.

## 11. Pistes RawSpeed et Canon CR2/CR3 — 7 octobre 2026

### 11.1. Conclusion et portée de la recherche

**Oui, ces sources donnent des pistes utiles, mais elles ne justifient pas
de remplacer LibRaw pour corriger les couleurs.** Il faut distinguer le
décodage du fichier, la calibration du signal capteur et le rendu RAW/DCP.
Une mosaïque correctement décompressée ne garantit ni une bonne balance
des blancs, ni un rendu identique à Lightroom.

Cette étape est une lecture des sources et une confrontation avec le code
et les mesures de la section 8. **Aucun nouveau décodage du corpus,
parseur indépendant des MakerNotes ou correctif de production n'a été
exécuté ici.** Les causes nouvelles ci-dessous restent donc des hypothèses.

### 11.2. Ce que chaque source apporte réellement

| Source | Apport pour IPR | Limite |
| --- | --- | --- |
| RawSpeed | Exemple de séparation décodage/calibration et de traitement des métadonnées CR2 | Pas de rendu colorimétrique complet ; notre R6 Mark III est explicitement non pris en charge dans la base consultée |
| Clévy, CR2 | Explication des gains RGGB, du niveau noir et des pixels masqués | Les offsets et exemples historiques CR2 ne sont pas transposables au CR3 du R6 Mark III |
| Clévy, CR3 | Cartographie des conteneurs et métadonnées nécessaires à un audit indépendant | Travail de rétro-ingénierie, pas une spécification Canon exhaustive ni un profil colorimétrique |

RawSpeed annonce ne pas réaliser le dématriçage, la correction des couleurs
ou l'application de la WB. Son XML `develop` marque le **Canon EOS R6 Mark III
`supported="no"`**, comme plusieurs boîtiers CR3. Il ne constitue donc pas
un remplacement disponible pour ce corpus. Son décodeur CR2 sélectionne les
positions des gains, des quatre niveaux noirs et du blanc selon la version
`ColorData`, avec adaptation à la précision des données.
[README RawSpeed](https://github.com/darktable-org/rawspeed/blob/develop/README.rst),
[base des boîtiers](https://github.com/darktable-org/rawspeed/blob/develop/data/cameras.xml),
[décodeur CR2](https://github.com/darktable-org/rawspeed/blob/develop/src/librawspeed/decoders/Cr2Decoder.cpp).

Le site CR2 historique demandé n'a pas répondu lors de la consultation.
La version Markdown publiée par **le même auteur**, datée du 26 septembre
2026, a été utilisée. Elle décrit notamment les gains Canon en ordre RGGB
et leur réorganisation en RGBG dans dcraw, ainsi que la mesure des noirs
par position dans le motif Bayer. Les premières colonnes de bordure peuvent
être contaminées : les mesures doivent sélectionner des zones fiables,
pas simplement tous les pixels hors recadrage.
[Clévy, Understanding the Canon RAW CR2 file format](https://github.com/lclevy/canon_cr2/blob/main/readme.md).

Le document CR3 décrit un conteneur ISO BMFF, des blocs TIFF dont `CMT3`
pour les MakerNotes, et des métadonnées temporelles `CTMD`. Il renvoie au
décodeur CRX déjà intégré à LibRaw. Une autre décompression n'apporterait
donc pas, à elle seule, une nouvelle calibration des couleurs. La demande
de fichiers R6 Mark III CRAW/HEIF dans ce dépôt invite aussi à vérifier les
particularités de notre modèle plutôt qu'à reprendre les offsets d'un EOS R.
[Clévy, Describing the Canon Raw v3 file format](https://github.com/lclevy/canon_cr3/blob/master/readme.md).

Attention : les réglages boîtier « sRGB / Adobe RGB » ne doivent pas faire
assimiler la mosaïque RAW à une image déjà encodée dans ces espaces.
Dans notre branche DCP, le signal demeure en RGB caméra avant la
transformation vers XYZ et l'espace de travail (section 8).

### 11.3. Première piste : contrôler la calibration noir/blanc

Notre `RawBlackLevelEstimator::estimate()` utilise actuellement **tous les
pixels hors rectangle actif**, en regroupant leurs valeurs selon le CFA.
Il calcule une médiane par canal, avec des seuils de plausibilité. Ces
seuils protègent de certains résultats aberrants ; ils ne démontrent pas
que chaque bordure est une zone optiquement masquée. RawSpeed décrit au
contraire des `BlackAreas` explicites pour certains boîtiers. Les rectangles
d'un autre appareil ne doivent pas être copiés pour le R6 Mark III.
[Code IPR](../../../src/raw/RawBlackLevelEstimator.cpp),
[exemples de zones RawSpeed](https://github.com/darktable-org/rawspeed/blob/develop/data/cameras.xml).

Les mesures existantes passent d'un noir global absent et de petits
`cblack` à environ **512**, ou **2052–2053 pour 9421**. Cela rend l'audit
prioritaire, sans prouver que ces estimations sont fausses. Il faut comparer
les marges gauche/droite/haut/bas séparément, leurs médianes, dispersions,
bandes contaminées et éventuelles dérives par ligne, puis vérifier les
niveaux annoncés dans les métadonnées.

Modèle simplifié, avant WB et dématriçage, pour le canal capteur `c` :

```text
signal_c = (code_c - noir_c) / (blanc_c - noir_c)
signal_WB_c = gain_c × signal_c
```

Une erreur sur `noir_c` se voit surtout dans les faibles signaux ; si elle
diffère entre canaux, elle produit une dominante. Une erreur sur `blanc_c`
change l'échelle et la réserve avant saturation. Ce modèle explique les
effets attendus, mais ne décrit pas tous les détails de normalisation et
d'écrêtage de LibRaw. Il ne faut pas imposer automatiquement 16383 comme
blanc utile sous prétexte d'un codage 14 bits.

### 11.4. Deuxième piste : vérifier la véritable WB « As Shot »

Les six RAW ont fourni le même `cam_mul` **RGBG**
`[1938, 1024, 1733, 1024]`, et notre estimation est d'environ **4924 K**.
Ce résultat peut être parfaitement cohérent avec une WB fixe à la prise de
vue ; il peut aussi masquer une lecture de métadonnées inadaptée. Ce n'est
**pas une preuve d'erreur**. Les références Lightroom forcées à 5200 K ne
permettent pas de trancher entre ces possibilités.

ExifTool distingue les versions du tag Canon `0x4001`. Dans ses tables
`ColorData11` et `ColorData12`, un même index peut désigner respectivement
des gains Auto ou Daylight. Le nom d'un champ ne peut donc pas être déduit
de son seul offset. Les tables consultées ne suffisent pas à certifier la
variante de nos fichiers R6 Mark III.
[Source primaire ExifTool, Canon.pm](https://github.com/exiftool/exiftool/blob/master/lib/Image/ExifTool/Canon.pm).

Audit proposé : relever la version et la longueur `ColorData`, les gains
As Shot, Daylight et Kelvin, la température enregistrée, puis comparer aux
structures LibRaw **de la version réellement utilisée, 0.22.2**. Inspecter
`CMT3` et, si nécessaire, `CTMD` ; documenter l'origine et la priorité des
valeurs. Vérifier explicitement l'ordre RGGB/RGBG et la profondeur de
codage. ExifTool n'était pas disponible dans l'environnement inspecté :
cette comparaison indépendante reste à réaliser.

Enfin, notre estimation Kelvin utilise la matrice LibRaw alors que le rendu
emploie un DCP. Il faut vérifier la cohérence entre le neutre caméra, les
matrices de calibration du profil et l'interpolation des illuminants, sans
réduire toute la chromaticité à une température scalaire. La méthode DNG
de résolution du neutre et des matrices fournit le cadre de cet audit ;
une valeur Kelvin n'est pas universelle entre moteurs.
[Spécification DNG 1.7.1, sections sur la conversion des couleurs](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf).

### 11.5. Ordre recommandé et critères de décision

1. **Audit des métadonnées et des zones noires**, sans changer le rendu :
   conserver les valeurs originales, mesurer chaque bordure séparément et
   identifier les champs Canon avec leur version. Critère : concordance
   expliquée entre métadonnées, gains LibRaw et niveaux mesurés.
2. **Conformité DCP** : corriger l'espace des tables confirmé en section 8,
   avec tests synthétiques, puis comparer les mêmes RAW sans LCP et en WB
   réellement équivalente. Contrôler ΔE00, gris neutres, écrêtage et ombres.
3. **Rendu tonal de base** : définir une transformation explicite, distincte
   des réglages utilisateur. Le décodage et la calibration ne remplacent
   pas cette décision ; aucun gain EV arbitraire n'est recommandé.
4. **Évaluer un autre backend seulement sur un besoin mesuré** de couverture,
   de performance ou de décodage. RawSpeed n'est pas la piste immédiate
   pour la colorimétrie de ces CR3. Une intégration demanderait aussi une
   revue de sa licence et de ses dépendances ; aucun code externe n'a été
   incorporé dans IPR pendant cette recherche.

**Aucune nouvelle image indispensable pour les étapes 1 et 2.** Les RAW
actuels suffisent. Pour lever les ambiguïtés ensuite : même scène avec
plage grise, exposition et éclairage fixes, WB manuelle 3200/5200/6500 K ;
images noires avec bouchon aux ISO du corpus pour contrôler les bordures ;
éventuellement une paire RAW/CRAW de la même scène. Ces acquisitions sont
complémentaires au contrôle Lightroom As Shot déjà décrit en section 9.

**Décision proposée : conserver LibRaw, auditer ses entrées Canon et notre
normalisation, puis corriger le pipeline DCP.** Ces sources renforcent les
pistes de calibration ; elles ne démontrent pas encore que le format CR3
ou son décodeur explique le rendu trop sombre.

## 12. Correctif DCP appliqué — 7 octobre 2026

### Périmètre et changements

Le moteur applique maintenant les matrices vers XYZ D50, convertit vers
**ProPhoto linéaire**, applique HueSatMap, LookTable et la courbe du profil,
puis convertit vers sRGB linéaire D65. L'ancien chemin appliquait les
tables directement dans sRGB. La branche sans tables ni courbe conserve
la conversion matricielle directe ; aucun gain EV ou courbe Adobe implicite
n'a été ajouté.

Une seconde anomalie a été corrigée : pour les tables 3D encodées en sRGB,
seule la **Valeur HSV** est encodée/décodée, pas les canaux RGB. L'encodage
ne s'applique pas aux tables 2,5D (une division Valeur). Le profil Adobe
Standard inventorié utilise l'encodage linéaire ; son changement principal
vient donc de l'espace ProPhoto, pas de cette seconde correction.
[Algorithme documenté dans le SDK DNG](https://android.googlesource.com/platform/external/dng_sdk/+/refs/heads/main/source/dng_tag_values.h).

Les tables gardent leur domaine SDR [0,1], mais la conversion finale
n'ajoute pas d'écrêtage au gamut sRGB. La courbe conserve son interpolation
par canal existante, maintenant dans ProPhoto : ce n'est pas une
certification exhaustive du rendu tonal DNG. L'interpolation des matrices
et l'estimation Kelvin restent également les approximations décrites plus haut.

**Pas de changement des niveaux noirs, de la WB Canon ou du backend LibRaw.**
Les pistes de la section 11 nécessitent encore un audit indépendant des
métadonnées et des bordures ; modifier ces valeurs sans cette preuve
risquerait de compenser une erreur par une autre. Leur investigation reste
à faire, distinctement du correctif d'espace confirmé.

### Tests et préparation des nouveaux exports

Les tests synthétiques couvrent désormais une rotation de teinte de 120°
dans ProPhoto, l'encodage de la seule Valeur sur une table 3D, et l'absence
d'effet du tag d'encodage sur une table 2,5D, en plus des cas existants.
La comparaison utilise une tolérance de 1e-5 sur les nouveaux cas analytiques.

Commande de build Windows et tests ciblés (depuis WSL/Git Bash) :

```bash
bash scripts/build-windows.sh --paths src/profiles/CameraProfileEngine.cpp \
  tests/camera_profile_engine_test.cpp --jobs 2
```

Les cibles application et générateur sont reconstruites ensemble pour
éviter des exports issus d'un ancien moteur. Les suites ciblées sont
`cameraProfileEngineTests`, `rawDecoderTests` et `imageExportTests`.
**Résultat final : build Windows Release réussi pour `appIPR.exe` et
`iprGenerateTests.exe` ; 3/3 suites ciblées réussies (2,54 s).** Le premier
test analytique a également permis de corriger un coefficient erroné de
la matrice de référence du test, sans changer la matrice du moteur.
Ces succès sont à distinguer de la validation photographique, qui attend
les nouveaux TIFF. La suite complète n'a pas été relancée pour ce changement ciblé.

Pour la prochaine analyse :

1. Repartir des **RAW CR3**, pas des TIFF Lightroom, pour 0007, 0037, 0043,
   0327, 9421 et 9950. Conserver les anciens TIFF comme référence historique.
2. Utiliser la nouvelle application ou le nouveau `iprGenerateTests` ;
   réinitialiser les réglages et vérifier les éventuels sidecars `.ipr`.
   Ne pas appliquer automatiquement une ancienne recette non neutre.
3. Exporter IPR en TIFF 16 bits sRGB, SDR, taille originale, sans netteté
   de sortie, avec le même DCP/LCP et les mêmes options que le lot précédent.
   Placer le lot dans un sous-dossier identifié, par exemple
   `tests/image/base/neutre/dcp-prophoto-20261007/`.
4. Les TIFF Lightroom actuels peuvent servir à comparer l'effet du correctif
   sur le même protocole. Le contrôle As Shot sans LCP de la section 9 reste
   nécessaire pour attribuer précisément les écarts de WB et de rendu.
5. Comparer ΔE00 global et par zones pertinentes, neutralité des gris,
   luminance et écrêtage. Ne pas conclure à la fidélité physique à partir
   d'une simple baisse du ΔE vis-à-vis de Lightroom.

L'expérience précédente prévoyait un effet colorimétrique mesurable mais
pas la disparition de l'écart tonal. **Le correctif n'est donc pas annoncé
comme une résolution du rendu trop sombre** avant la nouvelle analyse.

## 13. Analyse des TIFF v2 après correctif DCP — 7 octobre 2026

### 13.1. Corpus et méthode

Les six fichiers `6V1Axxxx-IPR-2.tiff` sont présents dans `tests/image/base/neutre`.
Ils sont comparés aux anciens `-IPR.tiff` et aux références Lightroom `.tif`
du même dossier, sans modifier aucun fichier source. Les anciens IPR
correspondent toujours intégralement aux membres de l'archive historique.
**Les six v2 ont des SHA-256 différents et des pixels mesurés différents** :
le changement n'est donc pas seulement une modification des métadonnées.

Audit : TIFF 16 bits RGBA IPR, ICC sRGB identique entre ancien et nouveau,
orientation 1, dimensions inchangées (4639 × 6959 ou 6959 × 4639).
Les références Lightroom ont toujours une ligne et une colonne de plus.
Mesures sur la grille native de pas 8 : **504 600 échantillons RGB par scène**,
sans conversion préalable en 8 bits. Le calage Lightroom teste uniquement
les quatre recadrages 0/1 pixel, pas un recalage optique. Ancien et nouveau
sont évalués avec **le même recadrage Lightroom choisi sur le nouveau**,
afin de ne pas attribuer au correctif un changement de calage.

Les réglages Lightroom et les ambiguïtés As Shot/LCP de la section 3 restent
inchangés. Les TIFF seuls ne certifient pas les réglages ou le binaire IPR
ayant produit chaque export. Les anciennes ROI historiques restent présentes
dans le JSON, mais leurs noms ne sont pas des cibles physiques validées ;
la conclusion principale ci-dessous s'appuie sur les mesures globales.

Preuve : [mesures et empreintes du lot DCP ProPhoto](../raw/mesure-base-colorimetrique-dcp-prophoto-20261007.json).
Une planche de contrôle a également été examinée dans
`build-colorimetrie-dcp-prophoto-20261007/comparaison.png` (artefact local).
Elle confirme visuellement l'écart tonal persistant ; ses vignettes 8 bits
ne servent pas au calcul des métriques.

### 13.2. Résultats globaux

ΔE00 : plus bas signifie plus proche de Lightroom sur cette comparaison,
**pas nécessairement plus fidèle à la scène réelle**.

| Scène | ΔE00 ancien/LR | ΔE00 v2/LR | ΔE00 ancien/v2 | Variation de Y moyen IPR | Bilan vis-à-vis de LR |
| --- | ---: | ---: | ---: | ---: | --- |
| 0007 | 18,935 | 19,270 | 0,473 | −2,17 % | Écart accru |
| 0037 | 11,418 | 11,454 | 0,389 | −3,10 % | Écart légèrement accru |
| 0043 | 4,642 | 4,415 | 0,596 | −1,07 % | Écart réduit |
| 0327 | 11,552 | 11,622 | 0,381 | −0,83 % | Écart légèrement accru |
| 9421 | 21,549 | 21,531 | 1,189 | −2,70 % | Écart très légèrement réduit |
| 9950 | 12,058 | 12,391 | 0,545 | −3,76 % | Écart accru |

L'effet ancien/v2 est réel mais modéré : ΔE00 moyen **0,381 à 1,189**.
La MAE RGB baisse sur 0043 et 9421, augmente sur les quatre autres.
La correction n'améliore donc pas uniformément la ressemblance avec Lightroom.
Les anciens scores de la section 5 peuvent varier légèrement avec le
recadrage choisi : le tableau présent recalcule les deux versions avec
la même référence spatiale, il ne mélange pas deux campagnes.

La distance moyenne de teinte, sur les pixels suffisamment chromatiques,
baisse légèrement pour 0043 (5,75° → 5,55°), 0327 (6,84° → 6,70°) et
9950 (9,73° → 9,68°), mais monte pour 0007, 0037 et 9421.
Ce masque dépend du chroma de chaque paire : ces chiffres sont descriptifs,
pas un test de significativité ou une preuve isolée de justesse des teintes.
Sur les pixels quasi neutres de Lightroom, la distance dans le plan a*/b*
augmente légèrement dans les six scènes. La neutralité de base n'est donc
pas validée par cette campagne.

### 13.3. Tonalité et faibles signaux

Le rapport des luminances moyennes Lightroom/IPR, exprimé en log2, reste
entre **0,879 et 1,647 EV** sur les v2, contre **0,833 à 1,607 EV** sur les
anciens exports. Ce sont des rapports descriptifs de moyennes, **pas des
consignes de compensation d'exposition** ni des mesures d'exposition capteur.
IPR reste sensiblement plus sombre ; le correctif DCP réduit même légèrement
Y moyen dans chacune des scènes.

La fraction de pixels ayant au moins un canal quasi nul augmente dans les
six scènes. Le cas le plus visible est 0043 : **0,453 % → 1,315 %** sur
la grille mesurée. Un canal nul n'implique pas un pixel entièrement noir :
une couleur hors gamut sRGB peut aussi perdre un canal à l'export.
Ce résultat appelle un contrôle des faibles signaux et de l'écrêtage/gamut ;
il ne prouve pas une mauvaise estimation du niveau noir capteur. Les fractions
de canaux quasi saturés dans les hautes valeurs restent faibles et ne
montrent pas de hausse générale.

### 13.4. Conclusion et prochaine investigation

**Le correctif d'espace DCP est conservé pour sa justification technique
et ses tests analytiques, pas parce que Lightroom lui donne un meilleur
score dans toutes les scènes.** La campagne confirme l'effet attendu et
montre qu'il ne résout ni la tonalité de base ni toutes les dominantes.
La conformité complète DNG, la parité Lightroom et la fidélité physique
restent trois validations distinctes, toujours incomplètes.

La priorité suivante reste l'audit séparé **WB Canon / niveaux noir-blanc**
de la section 11, puis l'étude d'un rendu tonal de base explicite. Il faut
également contrôler le clipping entre ProPhoto, sRGB et l'export, en particulier
sur la fleur 0043. Aucun nouveau gain EV, changement du noir ou repli Kelvin
n'a été appliqué pendant cette analyse. Aucun nouveau RAW n'est requis
pour démarrer cet audit ; le contrôle Lightroom As Shot sans LCP de la
section 9 aidera à lever les différences de protocole.

### 13.5. Reproduction

```bash
python3 tests/analyse_base_colorimetrique_v2.py \
  --ipr-suffix=-IPR-2 --previous-ipr-suffix=-IPR \
  --archive tests/image/base/neutre/6V1A9950-IPR.zip \
  --output docs/validation/raw/mesure-base-colorimetrique-dcp-prophoto-nouvelle-execution.json
python3 -m unittest discover -s tests -p test_analyse_base_colorimetrique.py
```

Choisir un nouveau nom de sortie : l'analyseur refuse d'écraser les mesures.
Dans le JSON, `global` compare la v2 à Lightroom ;
`previous_vs_lr_same_crop` compare l'ancien à la même fenêtre Lightroom ;
`previous_vs_current` compare ancien/v2. Pour ce dernier objet uniquement,
les noms génériques `ipr`/`lr` signifient ancien/nouveau et le signe est
**nouveau moins ancien**, explicitement indiqué dans le champ associé.
`old_export` décrit le membre historique du ZIP, pas un fichier `-IPR-2`
dans l'archive. Les nouvelles options n'altèrent pas le fonctionnement
historique par défaut de l'analyseur.
**Validation ciblée : 13 tests de l'analyseur réussis**, dont les nouveaux
cas sur les suffixes de révision et le rejet des séparateurs de chemin.
Pas de nouveau build C++ requis : cette étape ne modifie que l'analyse et
la documentation.

## 14. Audit WB Canon, noir/blanc et export — 7 octobre 2026

### 14.1. Résultats établis et périmètre

L'audit est terminé sur les **six RAW existants**. Un lecteur indépendant
Python parcourt les boîtes CR3 et les tables d'échantillons CTMD, puis lit
le tag TIFF `0x4001`. Une sonde Windows LibRaw relève les métadonnées et
mesure les bordures avec le véritable `raw_pitch`, séparément par canal.
Les étapes flottantes DCP sont ensuite passées dans le vrai `ColorPipeline`
et la conversion Qt utilisée par l'export. Aucune image source ni moteur
de production n'a été modifié pendant cette investigation.

Deux anomalies concrètes ont été isolées : **mauvaise disposition ColorData
pour les niveaux noir/blanc dans LibRaw 0.22.2**, et **perte de précision
des faibles valeurs dans le chemin Qt d'encodage sRGB 16 bits**. Elles ne
prouvent pas, à elles seules, l'origine de tout l'écart tonal avec Lightroom.

Preuves conservées :

- [Lecture indépendante CTMD/ColorData](../raw/audit-canon-metadata-20261007.json).
- [Métadonnées LibRaw, bordures et flottants](../raw/investigation-calibration-canon-complete-20261007.json).
- [Export réel des échantillons et rampe Qt](../raw/audit-export-transfer-canon-20261007.json).

Les JSON initiaux intermédiaires restent conservés ; les liens ci-dessus
désignent les mesures complètes. Le lecteur expose des mots candidats,
pas une spécification exhaustive des MakerNotes Canon. Les SHA-256 des RAW
ont été vérifiés entre lecture indépendante et consolidation ; les étapes
flottantes sont également vérifiées par empreinte.

### 14.2. Balance des blancs : les gains sont cohérents

Chaque fichier possède un bloc ColorData de **3778 mots, version 66**, trouvé
dans `CTMD/sample0/record8`. À l'index `0x69`, les six fichiers contiennent
`[1938, 1024, 1024, 1733, 5200]` : les quatre gains en ordre RGGB et une
valeur de température cohérente avec 5200 K. Les gains réordonnés RGBG
correspondent **exactement** au `cam_mul` LibRaw `[1938,1024,1733,1024]`.

Le fait que les six gains soient identiques est donc présent dans les RAW,
pas une constante inventée par IPR. Cela écarte la mauvaise lecture de ces
gains comme explication du rendu sur ce corpus. L'estimation IPR d'environ
4924 K reste différente de la valeur enregistrée 5200 K : elle résulte de
la calibration matricielle et de l'approximation Kelvin, pas d'une preuve
que les gains WB appliqués sont erronés. La température seule ne remplace
pas les gains ni la nuance ; ne pas forcer 5200 K partout pour compenser.

La priorité hautes lumières Canon est lue à 0 sur les six fichiers. Auto
Lighting Optimizer vaut 2, mais sa présence ne démontre pas que Lightroom
applique automatiquement un traitement équivalent. Cela ne justifie pas
une courbe ou un gain de luminosité imposé dans IPR.

### 14.3. ColorData : mauvaise sélection de disposition dans LibRaw

La bibliothèque installée sélectionne la famille **11** pour la longueur
3778, sans la branche de distinction de version utilisée par ExifTool.
Pour la version 66 de nos RAW, elle lit les noirs à `0x27c`, les blancs à
`0x280/0x281`. Ces emplacements contiennent de petites valeurs incompatibles
avec les mesures du capteur. Les candidats de la disposition plus récente
se trouvent à `0x17f` et `0x290` pour les noirs, `0x294/0x295` pour les blancs.
Les deux groupes noirs concordent entre eux et avec la bordure gauche.

| Scène | Noir RGBG lu par LibRaw | Noir candidat récent RGGB | Blanc normal/spéculaire LibRaw | Candidats récents |
| --- | --- | --- | --- | --- |
| 0007 | 0 / 29 / 102 / 63 | 512 / 512 / 512 / 512 | 141 / 159 | 13200 / 13995 |
| 0037 | 0 / 45 / 130 / 96 | 512 / 512 / 512 / 512 | 155 / 167 | 13200 / 13995 |
| 0043 | 0 / 41 / 116 / 81 | 512 / 512 / 512 / 512 | 147 / 162 | 13200 / 13995 |
| 0327 | 0 / 46 / 126 / 92 | 512 / 512 / 512 / 512 | 154 / 169 | 13200 / 13995 |
| 9421 | 0 / 36 / 105 / 72 | 2048 / 2048 / 2048 / 2048 | 136 / 151 | 13535 / 14351 |
| 9950 | 0 / 46 / 134 / 98 | 512 / 512 / 512 / 512 | 159 / 170 | 13200 / 13995 |

Il faut distinguer le nom de famille du parseur (`ColorDataVer=11`) de la
version du bloc (`ColorDataSubVer=66`). La lecture indépendante prouve les
valeurs aux offsets ; l'interprétation de la disposition récente est
corroborée par les tables ExifTool et les noirs mesurés, sans certification
Canon générale pour tous les fichiers du modèle.
[Tables ExifTool Canon](https://github.com/exiftool/exiftool/blob/master/lib/Image/ExifTool/Canon.pm),
[LibRaw 0.22.2, sélection ColorData](https://github.com/LibRaw/LibRaw/blob/0.22.2/src/metadata/canon.cpp).

Le repli noir d'IPR masque déjà une grande partie de ce défaut. Par ailleurs,
le champ `maximum` n'est pas simplement le `SpecularWhiteLevel` mal lu : il
part de 16383 puis évolue pendant `dcraw_process`. Le code de la version
installée ajuste aussi le maximum selon les données avant normalisation.
Il serait donc incorrect d'attribuer tout l'assombrissement aux nombres
151–170, ou d'injecter les nouveaux blancs dans `maximum` sans test A/B.
[LibRaw, traitement](https://github.com/LibRaw/LibRaw/blob/0.22.2/src/postprocessing/dcraw_process.cpp),
[ajustement du maximum](https://github.com/LibRaw/LibRaw/blob/0.22.2/src/utils/utils_libraw.cpp).

### 14.4. Zones noires : distinguer zone masquée et simple marge

Les métadonnées Canon donnent les rectangles inclusifs suivants :

```text
Noir gauche : x=0..159, y=0..4751
Noir haut   : x=160..7143, y=0..95
Zone active : x=160..7143, y=96..4751
```

Le recadrage de développement LibRaw est plus petit que cette zone active.
Les pixels à droite et en bas du recadrage contiennent donc du signal de
scène ; la sonde le confirme. Exemple 0007 : médianes RGBG à droite
`[2002,3644,2603,3640]`, en bas `[1311,2236,1674,2235]`, contre 512 dans
les zones noires. L'estimateur actuel ne doit pas assimiler ces marges à
du noir optique.

Pour les cinq scènes hors 9421, la médiane de chaque canal est **512** dans
les zones Canon gauche/haut, et le reste après exclusion de deux pixels
aux bords. Le repli actuel donne aussi 512 : son résultat est confirmé
pour ces fichiers malgré un ensemble de pixels de mesure trop large.

Pour **9421**, la gauche donne **2048** sur chaque canal, le haut
`[2050,2050,2051,2051]`. Le repli actuel, qui mélange les marges, donne
`[2052,2053,2053,2053]`. Cela suggère une légère sur-soustraction de 4–5
codes face au candidat Canon et à la gauche, pas une erreur de milliers
de codes. Les différences gauche/haut demandent un choix explicite du
modèle de noir et éventuellement une vérification par images noires.

**Correctif recommandé :** privilégier des champs Canon correctement
identifiés et contrôlés, sinon une mesure dans les rectangles masqués
valides avec repli conservateur. Ne pas hardcoder 512 ou 2048 pour le modèle,
ni utiliser arbitrairement la moyenne de toutes les marges.

### 14.5. Écrêtage et encodage Qt : cause des faibles canaux isolée

Dans les flottants DCP+LCP de 0043, **0,0244 %** des échantillons ont au
moins un canal négatif. La simple quantification linéaire 16 bits prédit
environ **0,0273 %** de pixels avec un canal arrondi à zéro. Pourtant le
TIFF v2 en a **1,3149 %** à un code ≤1. Cela ne pouvait pas être expliqué
par le seul gamut négatif avant export.

La sonde rejoue la préparation gamut puis le vrai `ColorPipeline::toSrgb`
sur les échantillons flottants. Elle retrouve **exactement les fractions
de canaux bas et hauts des six TIFF v2**. Une rampe neutre isole ensuite
la conversion Qt 6.11.2 sur `QImage::Format_RGBA64`, sans DCP ni scène :

| Code linéaire 16 bits | Code sRGB analytique arrondi | Code obtenu par Qt |
| ---: | ---: | ---: |
| 1 | 13 | 0 |
| 8 | 103 | 0 |
| 15 | 194 | 0 |
| 16 | 207 | 207 |
| 17 | 220 | 207 |

Le code Qt de cette version utilise une table de résolution 4080 avec
réduction d'index des entrées 16 bits. La rampe établit la perte sur notre
chemin précis ; cela ne signifie pas que tous les chemins flottants/ICC
de Qt ont la même précision. Aucune implémentation Qt n'a été copiée.
[Source Qt 6.11.2, QColorTrcLut](https://github.com/qt/qtbase/blob/v6.11.2/src/gui/painting/qcolortrclut_p.h).

**Correctif recommandé :** pour le tampon déjà en sRGB linéaire, appliquer
explicitement la fonction de transfert sRGB sur les flottants, puis
quantifier une seule fois dans le format de sortie. Conserver la gestion
ICC des images d'entrée et le marquage ICC sRGB des sorties ; remplacer
ce passage précis n'exige pas de supprimer Qt ni la gestion des profils.
Ajouter des tests exhaustifs sur les codes faibles, une rampe complète,
alpha, aperçu/export et extrêmes de gamut.

La statistique « au moins un canal bas » ne signifie pas « image entièrement
noire » : sur 0043, aucun échantillon n'est entièrement noir dans cette
reproduction. Cette perte de précision explique des faibles canaux et des
paliers, **pas à elle seule le ΔE global ou l'écart tonal de 0,9–1,6 EV**.

### 14.6. Suites proposées et reproduction

1. Corriger l'encodage sRGB de sortie, avec tests de précision et non-régression.
2. Corriger la sélection/calibration Canon version 66 de manière ciblée,
   puis comparer séparément noirs et blancs en sonde avant de modifier le
   rendu de production. Éviter une correction générale de tous les CR3.
3. Harmoniser la référence Lightroom As Shot/sans LCP, puis étudier le
   rendu tonal de base ; ne pas remplacer cet audit par un gain fixe.

Le moteur de production reste inchangé pendant cette étape. Les outils
de diagnostic ont été compilés et exécutés sur les six RAW. **17 tests
Python ciblés réussissent**, couvrant TIFF endian, limites BMFF/ColorData,
mapping CTMD, clipping flottant et les métriques antérieures.

```bash
python3 tests/audit_canon_metadata.py --output audit-canon-nouvelle-execution.json \
  tests/image/base/6V1A0007.CR3 tests/image/base/6V1A0037.CR3 \
  tests/image/base/6V1A0043.CR3 tests/image/base/6V1A0327.CR3 \
  tests/image/base/6V1A9421.CR3 tests/image/base/6V1A9950.CR3
python3 tests/investigate_canon_calibration.py \
  --probe build-calibration-canon-20261007 \
  --metadata docs/validation/raw/audit-canon-metadata-20261007.json \
  --output investigation-calibration-nouvelle-execution.json
python3 -m unittest discover -s tests -p test_analyse_base_colorimetrique.py
```

Pour refaire le décodage : compiler `tonalBaseProbe`, puis utiliser la
commande de la section 10 avec un **nouveau** dossier de sortie.
Dans l'environnement DLL Qt/OpenCV/LibRaw Windows, la nouvelle sous-commande
rejoue l'export sans redécoder les RAW :

```powershell
.\build-windows-msvc\Release\tonalBaseProbe.exe --export-samples `
  build-calibration-canon-20261007/manifest.json audit-export-nouvelle-execution.json
```

Les sorties existantes sont refusées pour préserver les preuves. Les gros
flottants restent dans le dossier local `build-*` ; les JSON de synthèse
sont dans la documentation. Les nouveaux scripts sont rattachés à la
famille de tests ciblés de base colorimétrique. Aucun nouveau TIFF à
générer tant que les correctifs de production suivants ne sont pas appliqués.

## 15. Correctifs de précision sRGB et de noir optique — 7 octobre 2026

### 15.1. Changements appliqués

**Sortie sRGB :** `ColorPipeline::toSrgb` applique maintenant le transfert
sRGB analytique sur les flottants après le gamut mapping, puis quantifie
en RGBA 16 bits. L'alpha reste linéaire. Le passage de conversion colorimétrique
Qt qui écrasait les codes faibles est retiré de cette sortie uniquement.
La gestion ICC à l'entrée et le profil sRGB de sortie restent présents.
L'aperçu et les exports JPEG/PNG/TIFF bénéficient du chemin commun ; la
branche DNG linéaire n'est pas transformée par ce correctif.

**Noir Canon :** le repli utilise désormais une zone `LeftOpticalBlack`
valide, sinon `UpperOpticalBlack`, avec bornes contrôlées et `raw_pitch`.
Si aucune zone masquée explicite n'est valide, le repli historique reste
disponible. Les gardes préexistantes empêchant de remplacer des métadonnées
de noir exploitables demeurent. La médiane est mesurée, pas fixée selon ISO.

**Périmètre non corrigé :** la bibliothèque LibRaw 0.22.2 conserve sa lecture
incorrecte des champs ColorData 66. Le noir est contourné par mesure optique,
mais aucun parseur de métadonnées de production ni correctif de bibliothèque
externe n'a été ajouté. Les niveaux blancs, `maximum`, `adjust_maximum_thr`,
les gains WB et le calcul Kelvin restent inchangés. L'usage des blancs
candidats nécessite encore une expérimentation contrôlée : cela ne doit
pas être présenté comme une correction complète de calibration Canon.

### 15.2. Vérifications automatisées et sur le corpus

Build Windows Release réussi pour `appIPR.exe`, `iprGenerateTests.exe` et
la sonde. **6/6 suites ciblées réussies** lors du passage final (3,16 s) :
pipeline couleur, export, mélangeur neutre, décodage RAW, estimateur noir,
profils caméra. La suite complète n'a pas été lancée pour ce correctif ciblé.

Nouveaux tests : rampe sRGB exhaustive 65 536 entrées (écart analytique ≤1
code), monotonie, codes faibles, noir/blanc exacts, alpha droit ; estimateur
sur un tampon à pas mémoire paddé avec marges de scène lumineuses exclues,
rejet de zones hors bornes et de pas mémoire invalides.

Une sonde **calibration seule**, sans nouveau TIFF, a été exécutée sur les
six RAW : 512 sur les quatre canaux pour 0007/0037/0043/0327/9950 ; **2048
sur les quatre canaux pour 9421**, au lieu de 2052–2053. Les gains WB sont
inchangés. [Contrôle avec empreintes RAW](../raw/controle-noirs-correctif-20261007.json).

Les mêmes flottants DCP de la campagne précédente ont été réencodés avec
le nouveau chemin, afin d'isoler son effet des changements du noir :

| Scène | Pixels avec un canal sRGB ≤1, ancien export | Nouveau chemin, même flottant |
| --- | ---: | ---: |
| 0007 | 0,00099 % | 0,00099 % |
| 0037 | 0,19184 % | 0,07253 % |
| 0043 | 1,31490 % | 0,02517 % |
| 0327 | 0,02814 % | 0,00059 % |
| 9421 | 0,22889 % | 0,13991 % |
| 9950 | 0,11534 % | 0,00991 % |

[Mesures d'export après correctif](../raw/audit-export-correctif-srgb-20261007.json).
Ces résultats isolent l'encodage sur la grille de 504 600 points ; ils ne
sont pas encore les statistiques de nouveaux TIFF pleine résolution avec
les deux correctifs combinés. La rampe Qt brute incluse dans la sonde reste
volontairement inchangée pour documenter le comportement qui a été contourné.

### 15.3. Prochaine campagne TIFF

Le build Windows et le générateur sont prêts. Repartir des six **CR3**,
réinitialiser les réglages/sidecars et exporter avec les mêmes options
DCP/LCP en **TIFF 16 bits sRGB SDR**, sans redimensionnement ni netteté de
sortie. Conserver les v1/v2 ; nommer ce lot `-IPR-3.tiff` ou le placer dans
un dossier v3 distinct. Ne pas réutiliser des TIFF Lightroom comme entrée.

L'analyse suivante pourra utiliser `--ipr-suffix=-IPR-3` et
`--previous-ipr-suffix=-IPR-2` avec l'analyseur existant. Elle doit vérifier
ΔE00, gris, luminance, faibles canaux et dérive spécifique ISO 3200.
**Aucun gain arbitraire ni courbe tonale de base n'a été ajouté** : une
meilleure précision de sortie ne signifie pas que l'écart global de
luminosité avec Lightroom est résolu.

## 16. Analyse des six TIFF v3 — 7 octobre 2026

### 16.1. Corpus et contrôles de provenance

La campagne attendue en section 15.3 est maintenant réalisée sur les six
scènes 0007, 0037, 0043, 0327, 9421 et 9950. Les nouveaux fichiers
`6V1Axxxx-IPR-3.tiff` sont comparés aux `-IPR-2.tiff` et aux références
Lightroom `6V1Axxxx.tif` du dossier `tests/image/base/neutre`.

Les six exports v3 sont des TIFF RGBA **16 bits, sRGB**, orientation 1,
6959 × 4639 pixels ou dimensions transposées en portrait. Ils ont tous une
empreinte différente des v2. Les six fichiers v2 et les six références
Lightroom ont, eux, exactement les empreintes de la campagne précédente.
L'archive historique, désormais nommée `Archive-IPR.zip`, conserve également
la même empreinte : elle contient les exports initiaux v1, **pas les v2**.

Les empreintes, profils ICC et dates des exports sont conservés dans le
[JSON de mesures v3](../raw/mesure-base-colorimetrique-v3-20261007.json).
Les TIFF IPR ne contiennent pas de réglages de développement exploitables
par cet audit : l'absence de réglages XMP ne prouve donc pas, à elle seule,
que tous les curseurs étaient à zéro. La concordance avec la sonde isolant
le correctif apporte une preuve supplémentaire, décrite ci-dessous.

Méthode inchangée : lecture native uint16, grille de **504 600 pixels par
scène**, conversion sRGB vers Lab D65 et CIEDE2000. Les v2 et v3 utilisent
le même recadrage Lightroom de 0/1 pixel pour chaque scène. Il n'y a pas
de recalage optique complet ; les limites LCP/WB et les zones historiques
non certifiées restent celles des sections précédentes. Les statistiques
ne constituent pas une validation de fidélité physique absolue.

### 16.2. Précision des valeurs sombres : correctif confirmé

Le seuil ci-dessous signifie « au moins un des trois canaux RGB ≤1 sur
65535 ». Il **ne signifie pas** que le pixel entier est noir. Les nombres
portent sur la grille de mesure, pas sur un décompte exhaustif de l'image.

| Scène | Faibles canaux v2 | Faibles canaux v3 | ΔE00 moyen v2 → v3 |
| --- | ---: | ---: | ---: |
| 0007 | 0,00099 % | 0,00099 % | 0,0394 |
| 0037 | 0,19184 % | 0,07253 % | 0,1113 |
| 0043 | 1,31490 % | 0,02517 % | 0,1433 |
| 0327 | 0,02814 % | 0,00059 % | 0,0945 |
| 9421 | 0,22889 % | 0,08185 % | 0,3038 |
| 9950 | 0,11534 % | 0,00991 % | 0,0879 |

Pour les cinq scènes autres que 9421, les proportions v3 correspondent
exactement à celles prévues par le réencodage des anciens flottants en
section 15.2. Sur 0043, la proportion est divisée par environ **52**
(réduction de 98,1 %). Le correctif restaure des codes de sortie faibles
que l'ancienne conversion Qt écrasait ; il ne reconstruit pas des données
capteur déjà perdues.

Sur 9421 à ISO 3200, la proportion descend même sous les 0,13991 % prévus
par le seul correctif sRGB : le résultat est **cohérent avec** le changement
du noir optique de 2052–2053 à 2048 contrôlé sur le RAW. Le TIFF combinant
les deux correctifs ne permet pas d'attribuer séparément chaque pixel à
l'un ou l'autre. La différence v2/v3 est la plus forte de la série, mais
reste limitée (ΔE00 moyen 0,3038).

Il n'apparaît pas de hausse importante du clipping supérieur : la fraction
avec au moins un canal ≥65534 reste nulle pour 0007/0327/9950, à 0,00198 %
pour 0043 et 0,01506 % pour 9421. Sur 0037 elle passe de 0,52120 % à
0,52140 %, soit un pixel supplémentaire sur la grille.

**Conclusion technique :** la correction de précision sRGB est confirmée
sur ces exports photographiques. Les résultats de 9421 confortent le repli
sur la zone noire déclarée. Cela ne valide ni les niveaux blancs Canon,
ni le parseur ColorData 66 de LibRaw, toujours non corrigé en production.

### 16.3. Comparaison Lightroom : écart de rendu toujours présent

| Scène | ΔE00 moyen v2 / LR | ΔE00 moyen v3 / LR | ΔE00 p95 v3 / LR | Écart des moyennes Y, équivalent EV |
| --- | ---: | ---: | ---: | ---: |
| 0007 | 19,2700 | 19,2546 | 26,9233 | +1,436 |
| 0037 | 11,4536 | 11,4477 | 33,3931 | +0,874 |
| 0043 | 4,4150 | 4,4191 | 19,2685 | +0,958 |
| 0327 | 11,6217 | 11,6082 | 26,2985 | +1,372 |
| 9421 | 21,5313 | 21,5094 | 35,7637 | +1,640 |
| 9950 | 12,3907 | 12,3802 | 26,2263 | +1,415 |

L'équivalent EV est `log2(moyenne(Y_LR) / moyenne(Y_IPR))`, avec Y linéaire.
C'est un indicateur descriptif global : **pas une consigne d'exposition**,
ni la preuve d'un gain multiplicatif unique manquant dans IPR.

La luminance moyenne IPR augmente seulement de **0,09 à 0,80 %** par
rapport à v2 (0,48 % sur 9421). Les ΔE00 Lightroom diminuent légèrement
sur cinq scènes ; sur 0043 ils augmentent de 0,0041 malgré la correction
des faibles codes. La proximité Lightroom ne doit donc pas être le seul
critère pour juger un correctif mathématiquement justifié.

L'écart chromatique `a*b*` moyen sur les pixels neutres de Lightroom reste
entre 2,70 et 8,87 unités Lab ; l'écart moyen de teinte sur les pixels
suffisamment chromatiques reste entre 5,53° et 18,14°. Ces masques ne sont
pas des plages grises ou colorées mesurées physiquement. Les résidus de
couleur restent à investiguer indépendamment du défaut de sortie corrigé.

La planche locale v3/LR `build-colorimetrie-v3-20261007/comparaison.png`
montre encore le rendu IPR plus sombre, particulièrement sur le voilier,
l'escalade et le ciel. Cette planche 8 bits sert uniquement à la lecture
visuelle ; tous les chiffres viennent des TIFF 16 bits. L'artefact de build
n'est pas versionné et sa disponibilité actuelle n'est pas garantie.

### 16.4. Décision et étape suivante

**Conserver les deux correctifs.** Le lot v3 valide leur effet attendu sur
la précision de sortie et reste compatible avec les contrôles RAW ciblés.
La base colorimétrique et la parité Lightroom ne sont toutefois **pas
validées**. Aucun nouveau gain, changement Kelvin, courbe tonale ou
correctif de production n'a été ajouté pendant cette analyse.

La prochaine investigation recommandée est une expérience A/B des niveaux
blancs Canon, sur les **mêmes six RAW**, en gardant fixes WB, DCP, géométrie
et transfert sRGB. Comparer le chemin actuel aux niveaux candidats de
l'audit indépendant, tracer `maximum` avant/après les ajustements LibRaw,
la normalisation, les hautes lumières et le clipping. Les valeurs normales
et spéculaires ne doivent pas être échangées sans justification. Cette
expérience doit précéder toute intégration de blancs dans le décodeur.

Il n'est pas nécessaire de fournir d'autres images pour cette étape.
Une campagne ultérieure harmonisée As Shot/sans LCP, puis une charte
physique sous éclairage contrôlé, permettra de séparer calibration,
géométrie et choix de rendu tonal. Le corpus actuel est suffisant pour
le diagnostic, mais pas pour certifier la fidélité absolue des couleurs.

### 16.5. Reproduction et tests ciblés

Commande utilisée depuis la racine du projet sous WSL/Linux :

```bash
python3 tests/analyse_base_colorimetrique_v2.py \
  --ipr-suffix=-IPR-3 --previous-ipr-suffix=-IPR-2 \
  --archive tests/image/base/neutre/Archive-IPR.zip \
  --output docs/validation/raw/mesure-base-colorimetrique-v3-20261007.json \
  --contact-sheet build-colorimetrie-v3-20261007/comparaison.png
python3 -m unittest discover -s tests -p test_analyse_base_colorimetrique.py
```

**17/17 tests de l'analyseur réussis.** Aucun nouveau build C++ ni passage
de la suite complète n'était nécessaire pour cette analyse sans changement
de moteur. Pour reproduire la mesure, choisir de **nouveaux chemins de
sortie** : l'analyseur refuse d'écraser les preuves existantes.

## 17. Expérience A/B des blancs Canon — 7 octobre 2026

### 17.1. Protocole et validité du témoin

Le [protocole explicatif et reproductible](../protocols/protocole-blancs-canon.md)
distingue blanc de normalisation, balance des blancs et curseur Blancs.
La sonde a décodé **cinq variantes pour chacun des six CR3**, sans toucher
aux photographies, sidecars, exports ou moteur de production : chemin
actuel, blanc 14 bits fixe, blanc normal avec ajustement automatique,
blanc normal fixe et blanc spéculaire fixe. Noir optique, WB, DCP, LCP,
géométrie et transfert sRGB restent identiques entre variantes.

La sonde vérifie les empreintes RAW contre l'audit indépendant et
l'analyseur celles des fichiers flottants et références TIFF. Les gains
WB, noirs, profils sélectionnés et dimensions sont contrôlés entre
variantes. Le témoin reproduit **exactement les 504 600 pixels de la grille
de chaque TIFF v3** : écart maximal RGB nul pour les six scènes après
reconstitution des codes 16 bits transportés en float32. Cette observation
porte sur la grille, pas sur une identité exhaustive de fichiers TIFF.

La sortie encodée passe par le véritable `ColorPipeline::toSrgb`. Un
callback observe `maximum` et `data_maximum` juste avant `scale_colors`.
Les candidats sont placés dans les deux copies de couleur LibRaw avant
soustraction du noir ; `user_sat` n'est pas utilisé, car il intervient
plus tard dans LibRaw 0.22.2. Les valeurs ColorData 66 restent des
**candidats non certifiés**, pas une nouvelle spécification Canon.

### 17.2. Maximum réellement utilisé

Les valeurs ci-dessous sont celles observées **après soustraction du
noir**, juste avant mise à l'échelle. Elles ne sont donc pas les valeurs
brutes 13200/13995 ou 13535/14351 des métadonnées.

| Scène | Actuel | 14 bits fixe | Normal auto / fixe | Spéculaire fixe | `data_maximum` |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0007 | 14618 | 15871 | 12688 | 13483 | 14618 |
| 0037 | 15871 | 15871 | 12688 | 13483 | 15871 |
| 0043 | 15871 | 15871 | 12688 | 13483 | 15871 |
| 0327 | 15679 | 15871 | 12688 | 13483 | 15679 |
| 9421 | 14335 | 14335 | 11487 | 12303 | 14335 |
| 9950 | 15871 | 15871 | 12688 | 13483 | 4428 |

Le témoin 9421 utilise désormais 14335, cohérent avec 16383−2048 ; les
14331 observés auparavant appartenaient à l'estimation de noir précédente.

**Normal auto et normal fixe sont identiques sur les grilles mesurées**
pour les six scènes. L'ajustement automatique ne réduit pas davantage
ce maximum candidat : `data_maximum` est soit au-dessus, soit trop bas
pour satisfaire le seuil de réduction de LibRaw.

À l'inverse, désactiver l'ajustement sur le blanc 14 bits assombrit encore
0007 (Y −7,82 %) et 0327 (−1,21 %) ; les quatre autres scènes sont inchangées.
L'ajustement actuel éclaire donc déjà certaines scènes. Le supprimer seul
ne résout pas le rendu sombre, et peut aggraver l'écart.

### 17.3. Luminosité, proximité Lightroom et clipping

Les variantes normales auto/fixe ayant les mêmes mesures, une seule
colonne normale est présentée. Y est la luminance linéaire de la sortie
sRGB ; les gains sont relatifs au témoin v3.

| Scène | Gain Y normal | Gain Y spéculaire | ΔE00 actuel / LR | ΔE00 normal / LR | ΔE00 spéculaire / LR |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0007 | +15,06 % | +8,34 % | 19,2546 | 17,0241 | 17,9857 |
| 0037 | +25,20 % | +17,79 % | 11,4477 | 11,2988 | 11,3147 |
| 0043 | +24,93 % | +17,60 % | 4,4191 | 4,2865 | 4,2518 |
| 0327 | +23,51 % | +16,25 % | 11,6082 | 9,9862 | 10,4454 |
| 9421 | +24,85 % | +16,54 % | 21,5094 | 18,7167 | 19,5874 |
| 9950 | +24,66 % | +17,37 % | 12,3802 | 10,4492 | 10,9611 |

La normalisation explique la direction et l'ordre de grandeur du gain.
Par exemple, 15871/12688 ≈1,251 pour les scènes à noir 512. Les moyennes
RGB caméra ne suivent cependant pas un gain parfaitement uniforme si
des canaux atteignent la borne 1 : sur 0037, le bleu gagne environ 21,4 %
contre 25,0 % pour le vert. Il ne faut pas assimiler ce traitement à une
simple correction d'exposition réversible sur le rendu final.

Le gain normal représente seulement **+0,202 à +0,324 EV** sur les moyennes
Y ; le spéculaire +0,116 à +0,236 EV. Même avec le normal, le rapport des
moyennes Lightroom/IPR correspond encore à **0,550 à 1,320 EV**. Ces
indicateurs décrivent les images ; ils ne prescrivent pas un gain à coder.

Le clipping supérieur ci-dessous compte les pixels avec au moins un canal
encodé ≥65534. Les chiffres sont des pourcentages de la grille globale.

| Scène | Actuel | Normal | Spéculaire |
| --- | ---: | ---: | ---: |
| 0037 | 0,52140 % | 0,68966 % | 0,64090 % |
| 0043 | 0,00198 % | 0,00495 % | 0,00357 % |
| 9421 | 0,01506 % | 0,02378 % | 0,01605 % |
| 0007 / 0327 / 9950 | 0 % | 0 % | 0 % |

Avec le **même masque des 5 % de pixels les plus lumineux du témoin**,
le clipping de 0037 passe de 10,43 % à 13,79 % avec le normal et 12,82 %
avec le spéculaire. Les fractions globales peuvent donc cacher un effet
plus sensible sur les hautes lumières. L'absence de clipping sRGB sur une
scène ne certifie pas pour autant sa saturation physique RAW.

### 17.4. Conclusion et suite recommandée

**Hypothèse partiellement confirmée :** les blancs candidats permettent
un rendu plus lumineux et plus proche de Lightroom sur ce corpus. Ils
n'expliquent pas tout l'écart et augmentent le clipping sur trois scènes.
Le normal n'est pas systématiquement le meilleur compromis : sur 0043,
le spéculaire obtient un ΔE00 plus faible avec moins de clipping que le
normal. La proximité Lightroom seule ne permet pas de décider lequel
représente la bonne calibration physique.

**Décision : ne pas intégrer de niveau blanc en dur en production à ce
stade.** Conserver le chemin v3 corrigé. Le parseur ColorData 66 et la
sémantique des champs normaux/spéculaires doivent être confirmés avant
un correctif boîtier ; les blancs ne doivent pas servir à compenser
arbitrairement une différence de courbe tonale.

La prochaine étape utile est d'isoler le **rendu tonal de référence** :
campagne Lightroom harmonisée As Shot, sans correction optique, avec
profil et version de traitement tracés, et comparaison aux stages IPR
sans LCP. Vérifier séparément matrices/profil, éventuelle exposition de
base et courbe de rendu, sans chercher un gain unique par image. Les
flottants caméra/DCP avec et sans LCP de cette expérience sont déjà
disponibles pour poursuivre le diagnostic ; une charte contrôlée restera
nécessaire pour conclure sur la fidélité absolue.

### 17.5. Artefacts et vérifications

- [Mesures A/B, traces de maximum et métriques](../raw/mesure-blancs-canon-ab-20261007.json).
- Manifestes et grilles locales : `build-canon-blancs-ab-20261007`, cinq
  sous-dossiers par scène. Le dossier n'est pas destiné à être publié.
- [Sonde C++](../../../tests/tonal_base_probe.cpp) et
  [analyseur A/B](../../../tests/analyse_canon_white_levels.py) ; commandes
  dans le protocole. Les preuves existantes sont protégées de l'écrasement.
- Build Windows réussi pour **la sonde uniquement**. Le moteur/application
  n'ont pas été modifiés par cette expérience.
- **20/20 tests analyseur**, **10/10 tests de sélection** et **3/3 suites
  C++ ciblées** réussis : décodeur RAW, estimateur du noir et profils caméra.
  Nouveaux contrôles : formule de normalisation, masque de hautes lumières
  fixe et récupération exacte des codes 16 bits depuis le transport float32.

## 18. Investigation du rendu tonal et des profils — 7 octobre 2026

### 18.1. Observation complémentaire de l'utilisateur sur 9421

L'utilisateur a examiné l'image sur le boîtier et rapporte un aspect
**similaire à Lightroom à l'œil**. Son identifiant saisi `61VA9421` est
rattaché ici à **6V1A9421**, scène de salle d'escalade du corpus.
Le mode couleur à la prise de vue était **Neutre**, avec **Finesse = 2,
Seuil = 2 et les autres paramètres à 0**, d'après sa lecture du boîtier.
Ces valeurs sont une déclaration utilisateur, pas encore une extraction
indépendante des métadonnées Picture Style du RAW.

Cette comparaison est une **observation qualitative utile**, pas une
mesure : luminosité de l'écran du boîtier, écran de Lightroom, adaptation
visuelle et conditions d'observation ne sont pas contrôlés. Elle ne
certifie ni l'exposition, ni la colorimétrie, ni l'identité des rendus.

Le style Neutre ne signifie pas « aucune transformation ». Canon le
décrit comme un rendu à contraste réduit et couleurs naturelles. Finesse
et Seuil concernent l'accentuation des contours, pas une température ou
un gain d'exposition. [Manuel officiel EOS R6 Mark III — mode couleur et
réglages Picture Style](https://cam.start.canon/en/C022/manual/html/UG-04_Shooting_0260.html).

Le profil **Camera Neutral** d'Adobe vise une approximation du rendu
boîtier, contrairement au nom générique **Adobe Standard** qui ne désigne
pas le style Canon Standard. Adobe présente Camera Matching comme une
tentative de rapprochement des couleurs et tons du LCD/JPEG boîtier,
pas une identité garantie. Le style Canon Neutre, Camera Neutral et Adobe
Neutral ne sont donc pas trois noms interchangeables.
[Présentation officielle des profils Adobe et Camera Matching](https://blog.adobe.com/en/publish/2018/03/28/april-lightroom-adobe-camera-raw-releases-new-profiles).

### 18.2. Expérience réalisée et précautions

Les grilles RGB caméra du témoin v3 servent à rejouer **trois DCP installés**
avec le moteur réel : Adobe Standard, Camera Neutral et Camera Standard,
chacun avec et sans LCP, soit 36 variantes. Aucune modification du moteur
de production ni application automatique du style Canon n'est effectuée.
Les comparaisons sans LCP contre les TIFF Lightroom actuels restent
confondantes lorsque Lightroom applique sa correction optique.

Un second diagnostic compare deux modèles de luminance : gain seul,
puis `Y' = a × Y^g`. Chaque modèle est ajusté sur cinq scènes et évalué
sur la sixième, exclue de l'ajustement. Les scènes ont le même poids,
les pixels proches du noir et saturés sont exclus de l'ajustement, les
bornes des coefficients sont tracées. L'application conserve les rapports
RGB linéaires avant bornage ; elle n'est pas un nouveau rendu de production.

Une substitution de L* par celui de Lightroom dans Lab fournit aussi
un diagnostic de résidu chromatique. C'est un **oracle utilisant la
référence**, pas une correction déployable, ni une séparation additive
rigoureuse entre erreurs tonales et chromatiques. Les paramètres appris
ne doivent pas être copiés dans le logiciel à partir de ce petit corpus.

Les six témoins Adobe Standard avec LCP reproduisent exactement les
grilles v3 ; les empreintes des profils utilisés sont contrôlées et
conservées. Les deux modèles sont évalués en validation croisée par
scène : 4000 échantillons d'ajustement par scène d'apprentissage, cinq
scènes par pli. Cela évite d'ajuster et juger le modèle sur la même scène,
mais ne supprime ni les corrélations du corpus ni les différences WB/LCP.

### 18.3. Structure des DCP et effet observé

L'inspection des fichiers installés distingue nettement les profils :

| DCP | Courbe explicite | Offset d'exposition de profil | LookTable |
| --- | ---: | ---: | --- |
| Adobe Standard | Absente | Tag absent, traité comme 0 EV | 36 × 8 × 16, encodage linéaire |
| Camera Neutral | 128 points | −0,5 EV | 90 × 16 × 16, encodage sRGB |
| Camera Standard | 128 points | −0,5 EV | 90 × 16 × 16, encodage sRGB |

Les deux profils Camera Matching ont également `DefaultBlackRender = 1`.
Le moteur IPR n'est pas une implémentation certifiée de toutes les règles
de rendu DNG : sa courbe explicite est interpolée linéairement et appliquée
par canal, et ce dernier tag n'est pas exploité. Les résultats ci-dessous
mesurent donc **l'effet de ces profils dans IPR**, pas leur équivalence
à Lightroom ni au JPEG Canon. Plusieurs composantes changent simultanément
(matrices, tables, offset, courbe) : ce n'est pas une ablation de courbe seule.

Comparaison aux mêmes TIFF Lightroom **Adobe Standard**, avec LCP côté IPR :

| Scène | ΔE00 Adobe Standard | ΔE00 Camera Neutral | ΔE00 Camera Standard | Variation Y avec Camera Neutral |
| --- | ---: | ---: | ---: | ---: |
| 0007 | 19,2546 | 10,8226 | 9,7950 | +79,18 % |
| 0037 | 11,4477 | 13,2640 | 13,5215 | +11,43 % |
| 0043 | 4,4191 | 7,8127 | 7,7816 | −9,11 % |
| 0327 | 11,6082 | 11,2416 | 11,2058 | +32,46 % |
| 9421 | 21,5094 | 15,7593 | 15,2016 | +62,89 % |
| 9950 | 12,3802 | 10,1561 | 9,6645 | +68,31 % |

Camera Neutral rapproche quatre scènes et éloigne deux scènes. Sur 9421,
son résultat est compatible avec une influence importante du rendu de
profil, mais l'observation boîtier ne prouve pas que Lightroom avait
sélectionné ce profil : le TIFF conserve **CameraProfile = Adobe Standard**.
On ne doit pas basculer automatiquement vers Camera Neutral sur ce seul
constat, ni le présenter comme la correction universelle d'un RAW sombre.

### 18.4. Ce que révèlent les métadonnées Lightroom et l'ablation LCP

Les six TIFF Lightroom indiquent ProcessVersion **15.4**, courbe utilisateur
**Linear**, points RGB et composite `(0,0) → (255,255)`, et Adobe Standard.
L'exposition utilisateur est à 0. Les noms `ProfileName = Camera Settings`
présents dans les données de correction optique ne remplacent pas le
champ `CameraProfile` du développement RAW.

Une courbe utilisateur linéaire ne suffit pas à prouver que tout le rendu
RAW de Lightroom est linéaire : elle ne décrit pas à elle seule le rendu
de base, les tables du profil, le traitement des noirs et hautes lumières.

LCP est activé dans cinq références Lightroom, **désactivé sur 0043**.
Dans IPR, la variante sans LCP de 0043 donne ΔE00 **3,1858**, contre 4,4191
avec LCP. La correction optique non harmonisée explique donc une partie
de l'écart sur cette scène. Sur les autres, comparer IPR sans LCP à LR
avec LCP n'est pas un test propre de couleur pixel à pixel.

L'effet LCP sur Y moyenne IPR, avec Adobe Standard, varie de −0,90 % à
+33,87 % (9950), en incluant changement de cadrage et correction de
vignettage. Il ne faut pas attribuer toute cette variation à une erreur
de matrice ou à un niveau blanc manquant.

### 18.5. Diagnostic tonal sur scènes tenues à l'écart

Deux modèles volontairement simples sont testés sur la luminance linéaire :
`Y' = aY` et `Y' = aY^g`. Ils préservent les rapports RGB avant clipping,
mais **ne sont pas** une reconstitution du moteur Lightroom, une courbe
filmique bornée ni un choix recommandé pour IPR.

| Scène exclue de l'ajustement | ΔE00 actuel | Gain seul | Modèle puissance | Clipping actuel / puissance |
| --- | ---: | ---: | ---: | ---: |
| 0007 | 19,2546 | 9,6053 | 7,3730 | 0 % / 7,0420 % |
| 0037 | 11,4477 | 12,3148 | 11,7522 | 0,5214 % / 2,3823 % |
| 0043 | 4,4191 | 6,6027 | 3,5876 | 0,0020 % / 0,3920 % |
| 0327 | 11,6082 | 6,8029 | 6,1670 | 0 % / 0,0014 % |
| 9421 | 21,5094 | 13,7885 | 11,0496 | 0,0151 % / 5,5987 % |
| 9950 | 12,3802 | 6,9368 | 5,8724 | 0 % / 0,9243 % |

Clipping : au moins un canal sRGB ≥65534 sur la grille. Le gain seul
améliore quatre scènes et détériore deux ; le modèle puissance améliore
cinq scènes mais détériore 0037 et écrête trop fortement les hautes
lumières de 0007/9421. Les gains ajustés sont de 1,86 à 2,41 pour le
premier, et `a ≈4,77–5,83`, `g ≈1,27–1,34` pour le second. Ce sont des
coefficients de diagnostic **à ne pas recopier en production**.

Les ratios Y locaux confirment l'absence d'un simple gain unique : sur
9421, leurs percentiles 10/50/90 correspondent à environ
**+0,293 / +1,529 / +2,092 EV** ; sur 0043, −0,442 / −0,089 / +1,352 EV.
Les pixels proches du noir ou saturés sont exclus de ces distributions.
Ces ratios restent affectés par la WB, le profil et le recalage imparfait.

Même l'oracle remplaçant L* par celui de Lightroom laisse des ΔE00 moyens
de 3,047 / 6,907 / 2,922 / 4,660 / **10,010** / 4,089 respectivement.
Le rendu tonal représente donc une piste majeure, **mais pas l'unique
cause** : 9421 conserve un résidu chromatique important même dans ce
diagnostic non déployable. On ne peut pas soustraire ces ΔE pour obtenir
des pourcentages physiques d'erreur « lumière » et « couleur ».

### 18.6. Piste documentée : courbe de base implicite

Le code officiel RawTherapee ajoute une courbe par défaut lorsqu'un DCP
sans courbe porte un copyright contenant « Adobe Systems ». C'est une
preuve d'une **politique de compatibilité dans RawTherapee**, pas la preuve
de la courbe exacte utilisée par Lightroom 15.4 sur notre fichier.
[Source primaire `rtengine/dcp.cc`, branche dev consultée le 7 octobre 2026](https://github.com/RawTherapee/RawTherapee/blob/dev/rtengine/dcp.cc#L1216-L1255).

Attention : l'Adobe Standard installé ici n'a **pas** le tag copyright
50942. L'heuristique précise de cette source ne suffit donc pas à affirmer
que RawTherapee l'appliquerait automatiquement à notre fichier. Dans IPR,
l'absence de courbe explicite conduit actuellement à ne pas en appliquer.
Ce décalage de politique de rendu est une **hypothèse prioritaire à tester**,
et non une autorisation de copier une courbe ou d'introduire un gain arbitraire.

La documentation du développeur DCamProf décrit aussi l'usage de courbes
de rendu ACR et souligne l'importance d'une application stabilisant la
teinte plutôt que de traiter naïvement chaque canal.
[Documentation primaire DCamProf](https://rawtherapee.com/mirror/dcamprof/dcamprof.html).
Cette piste concerne à la fois la courbe absente d'Adobe Standard et la
fidélité d'application des courbes déjà présentes dans Camera Matching.

### 18.7. Décision, données utiles et reproduction

**Ne pas ajouter les modèles ajustés au moteur, ni imposer Camera Neutral.**
La suite utile est une comparaison harmonisée avec Lightroom et une
expérience isolant la courbe de base, avec comportement borné des hautes
lumières et stabilisation de la teinte. Conserver la calibration et le
rendu comme deux sujets distincts.

Les RAW existants suffisent. Pour lever les principaux facteurs confondants,
préparer ensuite six TIFF Lightroom depuis ces RAW, en **As Shot**, profil
Adobe Standard, courbe utilisateur Linear, tous curseurs à 0, corrections
optiques/aberration chromatique désactivées, sans netteté/bruit ni HDR,
TIFF 16 bits sRGB sans redimensionnement. Conserver les références actuelles.
Un export supplémentaire **9421 avec Camera Neutral**, mêmes options,
permettra une comparaison au même profil. Les variantes IPR sans LCP sont
déjà disponibles sous forme de grilles de diagnostic ; aucune nouvelle
prise de vue n'est nécessaire pour cette comparaison, mais une charte
physique reste nécessaire pour valider la fidélité absolue.

Artefacts : [mesures profils/rendu tonal](../raw/mesure-profils-rendu-tonal-20261007.json),
grilles locales `build-canon-profils-tonal-20261007`,
[analyseur](../../../tests/analyse_tonal_rendering.py). Les profils propriétaires
ne sont pas copiés ni publiés ; seuls chemins, empreintes et descriptions
de structure sont conservés dans les mesures.

```powershell
.\build-windows-msvc\Release\tonalBaseProbe.exe --profile-samples `
  build-canon-blancs-ab-20261007/manifest.json build-diagnostic-profils-raw-20261007 `
  'C:\ProgramData\Adobe\CameraRaw\CameraProfiles\Adobe Standard\Canon EOS R6 Mark III Adobe Standard.dcp' `
  'C:\ProgramData\Adobe\CameraRaw\CameraProfiles\Camera\Canon EOS R6 Mark III\Canon EOS R6 Mark III Camera Neutral.dcp' `
  'C:\ProgramData\Adobe\CameraRaw\CameraProfiles\Camera\Canon EOS R6 Mark III\Canon EOS R6 Mark III Camera Standard.dcp'
```

```bash
python3 tests/analyse_tonal_rendering.py \
  --probe build-diagnostic-profils-raw-20261007 \
  --reference docs/validation/raw/mesure-base-colorimetrique-v3-20261007.json \
  --output NOUVELLE_MESURE.json
python3 -m unittest discover -s tests -p test_analyse_base_colorimetrique.py
```

Build Windows de la sonde réussi ; **22/22 tests analyseur** et **3/3 suites
C++ ciblées** réussis (RAW, noir, profils). Les nouveaux tests vérifient
la récupération d'un modèle tonal connu, identité, noir et dépassements
du gamut. Aucun changement du moteur de production ; aucune suite
complète exécutée pour cette investigation ciblée.

## 19. Nouveaux TIFF Lightroom sans corrections optiques — 7 octobre 2026

### 19.1. Réception et audit du lot

Les six fichiers `6V1Axxxx-2.tif` sont disponibles dans
`tests/image/base/neutre`. Les références précédentes sont conservées.
Les nouveaux TIFF sont **RGB 16 bits, sRGB IEC61966-2.1**, dimensions
4640 × 6960 ou transposées, orientation 1. Les profils, réglages et
empreintes de chaque fichier sont enregistrés dans la
[mesure de contrôle](../raw/mesure-profils-rendu-harmonise-20261007-controle.json).

Les paramètres contrôlés montrent **Adobe Standard**, courbe utilisateur
Linear, exposition et curseurs lumière/couleur à zéro, netteté et réduction
de bruit à zéro, `LensProfileEnable = 0` et `AutoLateralCA = 0`.
Les courbes composite/RGB XMP restent linéaires.

**Réserve commune aux six scènes :** les TIFF indiquent
`WhiteBalance = Custom`, **5200 K et Tint = 0**, et non As Shot demandé
en section 18.7. Le lot est donc **harmonisé pour l'optique**, mais la WB
As Shot n'est pas validée. Ces mêmes nombres Kelvin ne prouvent pas une
égalité des gains caméra entre Lightroom et IPR. Une clarification a été
demandée à l'utilisateur ; l'analyse n'est pas bloquée, mais ses limites
doivent rester explicites.

**Réponse utilisateur reçue :** il ne voit pas As Shot dans les propositions,
seulement Auto, et ne trouve pas Camera Neutral dans Lightroom. Il n'est
donc pas établi qu'il ait volontairement choisi Custom. Le résultat XMP
reste factuel ; l'origine du choix et l'accessibilité de l'interface sont
à clarifier. Les deux références demandées ne doivent pas être considérées
comme des omissions volontaires.

L'export Lightroom supplémentaire de **9421 / Camera Neutral** n'a pas
été trouvé dans le corpus parcouru. La comparaison Camera Neutral IPR
contre ce nouveau lot reste une comparaison **entre profils différents**,
pas une validation de notre application de ce DCP au même profil dans LR.

Le dossier **`build-diagnostic-profils-raw-20261007`**, créé par les commandes de l'utilisateur
(anciennement `NOUVEAU_DOSSIER`, renommé le 9 octobre 2026 sans modifier les données),
contient les 36 variantes de profils attendues. Son manifeste a exactement
la même empreinte que l'ancien `build-canon-profils-tonal-20261007/manifest.json`.
Cette copie a été supprimée le 10 octobre après comparaison SHA-256 des 73
fichiers ; les commandes utilisent désormais le dossier conservé.
Les flottants et empreintes DCP sont vérifiés par l'analyseur. Les témoins
Adobe Standard avec LCP reproduisent encore exactement les grilles v3.
Le rendu sélectionné pour cette campagne est **Adobe Standard sans LCP**.

### 19.2. Méthode et contrôle de reproductibilité

Les six nouvelles références sont confrontées aux variantes sans LCP,
au pas 8 (504 600 points par scène). Les quatre retraits possibles d'une
ligne/colonne Lightroom sont notés et classés par signature de contours
contre la grille IPR **sans LCP**. Aucun redimensionnement ni recalage
optique complet n'est effectué. Les choix peuvent différer de ceux du
lot historique ; les anciennes et nouvelles mesures sont descriptives,
pas une comparaison physique parfaitement recalée.

L'analyseur accepte désormais `--lr-suffix=-2` et
`--geometry=without_lcp`. Il conserve les références historiques,
audite les nouveaux TIFF et signale les contrôles manquants/non conformes
au lieu de les considérer implicitement valides. Deux exécutions ont
donné des objets de résultats **strictement identiques pour les six
scènes**. La mesure `-controle.json` est la preuve de référence avec la
description méthodologique finale et l'empreinte de l'analyseur final.
La première mesure est conservée, mais ne remplace pas ce contrôle.

### 19.3. Comparaison au même profil Adobe Standard

La colonne historique ci-dessous utilise les références anciennes et les
variantes IPR sans LCP déjà mesurées en section 18. Les réglages LR et
certains recadrages ont changé : la baisse ne doit pas être attribuée
exclusivement à la correction optique ou à une correction du moteur,
**qui n'a pas changé**.

| Scène | ΔE00 ancien LR / IPR sans LCP | ΔE00 nouveau LR / IPR sans LCP | Écart des moyennes Y, équivalent EV | Résidu oracle L* égal |
| --- | ---: | ---: | ---: | ---: |
| 0007 | 19,3952 | 17,2524 | +1,350 | 2,7876 |
| 0037 | 12,2166 | 5,3167 | +0,984 | 3,3898 |
| 0043 | 3,1858 | 3,1997 | +0,945 | 2,0664 |
| 0327 | 12,2447 | 9,3941 | +1,275 | 3,8398 |
| 9421 | 22,7123 | 16,4367 | +1,405 | 6,9108 |
| 9950 | 15,7457 | 9,3186 | +1,365 | 3,0202 |

L'écart EV est `log2(moyenne(Y_LR)/moyenne(Y_IPR))`, pas une instruction
de correction d'exposition. IPR demeure plus sombre en moyenne sur les
six scènes, malgré le retrait des corrections optiques. Sur 0043, dont
le LCP Lightroom était déjà désactivé, le ΔE00 est pratiquement stable.
Les grands écarts de végétation/ciel sont réduits après harmonisation,
mais l'écart lumineux ne disparaît pas.

L'oracle remplace seulement L* IPR par L* LR dans Lab, sans modifier a*/b*.
Il n'est ni un rendu possible ni une séparation additive d'erreurs : le
résidu de **6,9108** sur 9421 confirme qu'une transformation de luminance
seule ne suffira pas à obtenir une concordance colorimétrique.

### 19.4. Profils alternatifs et modèles tonaux

Sans LCP, Camera Neutral dans IPR donne les ΔE00 suivants contre ce lot
LR Adobe Standard : 8,3113 / 8,1989 / 7,1190 / 9,0257 / 11,1209 / 8,6386
dans l'ordre 0007/0037/0043/0327/9421/9950. Il rapproche quatre scènes et
éloigne encore 0037/0043. L'observation utilisateur Canon Neutre demeure
consignée en 18.1 ; elle ne change pas le profil déclaré dans ces TIFF LR.

Les modèles `Y'=aY` et `Y'=aY^g` sont réajustés en validation croisée :
chaque scène évaluée est exclue de l'apprentissage sur les cinq autres.

| Scène | ΔE00 actuel | Gain seul | Puissance | Clipping actuel / puissance |
| --- | ---: | ---: | ---: | ---: |
| 0007 | 17,2524 | 8,9961 | 4,5521 | 0 % / 8,0775 % |
| 0037 | 5,3167 | 4,4410 | 2,9138 | 0,4215 % / 1,9790 % |
| 0043 | 3,1997 | 4,5378 | 1,7577 | 0,0012 % / 0,3981 % |
| 0327 | 9,3941 | 4,8517 | 2,9946 | 0 % / 0,0004 % |
| 9421 | 16,4367 | 9,8091 | 5,3246 | 0,0145 % / 6,2895 % |
| 9950 | 9,3186 | 5,0416 | 2,7161 | 0 % / 0,0010 % |

Le modèle puissance rapproche désormais **les six scènes**, mais les
8,08 % de pixels avec un canal écrêté sur 0007 et 6,29 % sur 9421 le
rendent inadapté à une intégration directe. Le gain seul détériore 0043.
Les coefficients de puissance (`a ≈5,19–6,21`, `g ≈1,31–1,36`) sont
des diagnostics sur ce corpus, **pas une calibration à conserver**.

Sur 9421, les ratios Y locaux non saturés ont des percentiles EV
10/50/90 d'environ **+0,493 / +1,348 / +1,556**. Leur variation démontre
à nouveau qu'un gain unique ne décrit pas tout le changement de rendu.
La WB Custom et le recalage imparfait limitent toutefois l'attribution
précise de cette différence au seul étage tonal.

### 19.5. Conclusion et suite

**Le lot confirme l'intérêt d'harmoniser l'optique et renforce la piste
d'une différence de rendu tonal de base.** Il ne valide ni la fidélité
absolue, ni la WB As Shot, ni la courbe exacte de Lightroom. Aucun gain
fixe, changement de profil par défaut ou modèle appris n'est intégré au
moteur de production.

Prochaine expérience technique : isoler une **courbe de base bornée**,
avec protection des hautes lumières et application stabilisant la teinte,
plutôt qu'une simple puissance suivie d'un clipping RGB. Tester séparément
son effet sur luminance, teinte et chroma, en conservant les contrôles
de RAW/noir/blanc. Cette direction reste une recommandation à vérifier,
pas un correctif déjà appliqué.

Côté références, clarifier le choix Custom 5200 K/0 avant de demander
une régénération : si c'est volontaire, conserver ce lot comme tel et
préparer un lot supplémentaire As Shot sans écraser celui-ci. Compléter
le TIFF 9421 Camera Neutral permettra le test au même profil. Aucune
nouvelle prise de vue n'est nécessaire ; le corpus RAW existant suffit.

### 19.6. Exécution autonome

```bash
python3 tests/analyse_tonal_rendering.py \
  --probe build-diagnostic-profils-raw-20261007 \
  --reference docs/validation/raw/mesure-base-colorimetrique-v3-20261007.json \
  --lr-suffix=-2 --geometry=without_lcp \
  --output NOUVELLE_MESURE_HARMONISEE.json
python3 -m unittest discover -s tests -p test_analyse_base_colorimetrique.py
```

`build-diagnostic-profils-raw-20261007` désigne le dossier de sondes réel,
initialement nommé `NOUVEAU_DOSSIER` par l'utilisateur et renommé le 9 octobre.
Ce n'est pas un dossier créé automatiquement par l'analyseur. Dans les commandes
précédentes, ce nom et `NOUVELLE_MESURE.json` étaient des exemples à
remplacer. Exécuter l'ancienne commande sans `--lr-suffix=-2` continue
à lire les anciens TIFF LR : elle ne découvre pas automatiquement les
nouveaux exports. Choisir un nouveau chemin de sortie à chaque campagne.

**24/24 tests ciblés de l'analyseur réussis** : deux tests supplémentaires
contrôlent les paramètres harmonisés manquants/non conformes et le rejet
de dimensions incompatibles. Pas de compilation ni changement C++
nécessaire pour cette réanalyse.

### 19.7. Retrouver les options dans Lightroom

Pour **Lightroom Classic**, Adobe nomme As Shot **« Tel quel »** dans sa
documentation française (l'interface peut afficher « Telle quelle »).
**Auto est différent** : il recalcule la WB depuis l'image.
Sélectionner le **CR3 original**, puis le menu de balance des blancs du
panneau Réglages de base, pas le bouton Auto des réglages de tonalité.
[Documentation Adobe — balance des blancs et profils](https://helpx.adobe.com/fr/lightroom-classic/desktop/process-and-develop-photos/image-tone-color.html).

Pour les profils, ouvrir **Profil → Parcourir → Camera Matching** ; vérifier
**Gérer les profils** si le groupe est masqué. Ces profils dépendent du
RAW sélectionné. L'absence des deux choix peut notamment venir du fichier
actif, d'un groupe masqué ou de la version ; ce sont des hypothèses, pas
un diagnostic établi de l'installation.
[Même documentation Adobe](https://helpx.adobe.com/fr/lightroom-classic/desktop/process-and-develop-photos/image-tone-color.html).

Adobe indique la disponibilité de profils Camera Matching pour l'EOS R6
Mark III à partir de Lightroom **9.0**, Lightroom Classic **15.0** et
Camera Raw **18.0**. Cela ne prouve pas leur présence dans la session
utilisateur ; les DCP trouvés dans ProgramData n'établissent pas, seuls,
ce que son interface Lightroom expose.
[Table officielle de compatibilité des appareils](https://helpx.adobe.com/camera-raw/desktop/dng-and-file-formats/camera-raw-plug-supported-cameras.html).

Si les menus restent indisponibles, fournir une capture avec le nom et
l'extension du fichier sélectionné, le panneau WB et l'explorateur de
profils, ainsi que la version exacte et l'édition (Lightroom ou Classic).
Ne pas remplacer As Shot par Auto ni modifier l'installation au hasard.
Le lot Custom 5200 K/0 reste utile et doit être conservé.

## 20. Lot As Shot et comparaison Camera Neutral au même profil — 7 octobre 2026

### 20.1. Corpus réellement reçu et conformité

Les six nouveaux `6V1Axxxx-LR-AdobeStandard-AsShot.tif` sont présents.
Des fichiers `-LR-CameraNeutral-AsShot.tif` sont également présents pour
0007, 0043, 0327, **9421** et 9950. Seul 0037 n'a pas de référence Camera
Neutral ; cela n'empêche pas le test demandé sur 9421 et les quatre
compléments fournis. Il n'est pas nécessaire de régénérer le lot actuel.

Les **onze TIFF** indiquent bien `WhiteBalance = As Shot`, **5200 K,
Tint = +5**, et le profil prévu par leur nom : Adobe Standard ou Camera
Neutral. Les contrôles automatisés ne signalent aucun écart sur les
paramètres vérifiés : courbe utilisateur Linear, curseurs lumière/couleur
contrôlés à zéro, absence de netteté/réduction de bruit, profil optique et
correction automatique d'aberration chromatique désactivés. Les fichiers
sont RGB 16 bits, sRGB, sans changement de dimensions.

Cette conformité porte sur les champs audités, pas sur une certification
exhaustive de toutes les opérations internes Lightroom. As Shot est
maintenant confirmé **dans les métadonnées** ; l'égalité numérique des
gains internes entre les deux moteurs n'est pas déduite des seuls Kelvin.
Le Kelvin estimé IPR reste distinct du Kelvin de présentation Lightroom.

Les anciens TIFF restent intacts. La comparaison utilise les variantes
IPR du dossier `build-diagnostic-profils-raw-20261007`, sans LCP, avec contrôle des empreintes
des flottants, profils et références v3. Le témoin IPR Adobe Standard
avec LCP reproduit encore exactement la grille v3, ce qui vérifie la
provenance de ces variantes sans demander de nouveaux exports IPR.

Preuve : [mesure As Shot et Camera Neutral](../raw/mesure-profils-as-shot-20261007.json).

### 20.2. Comparaisons au même profil

Chaque référence Adobe Standard est confrontée à **IPR Adobe Standard**,
et chaque référence Camera Neutral à **IPR Camera Neutral**, sans LCP.
Le recadrage 0/1 pixel est sélectionné contre la variante correspondante ;
il ne constitue toujours pas un recalage géométrique complet. Les mesures
portent sur 504 600 points par paire.

| Scène | ΔE00 Adobe Standard / Adobe Standard | ΔE00 Camera Neutral / Camera Neutral | Écart Y Adobe, équivalent EV | Écart Y Neutral, équivalent EV |
| --- | ---: | ---: | ---: | ---: |
| 0007 | 17,2034 | 5,9222 | +1,349 | +0,383 |
| 0037 | 5,1033 | Non fourni | +0,984 | — |
| 0043 | 3,1599 | 2,4276 | +0,947 | +0,698 |
| 0327 | 9,3517 | 5,7971 | +1,278 | +0,635 |
| 9421 | 16,3734 | **8,3671** | +1,406 | **+0,552** |
| 9950 | 9,1361 | 4,8969 | +1,363 | +0,536 |

L'équivalent EV décrit `log2(moyenne(Y_LR)/moyenne(Y_IPR))` ; ce n'est
toujours pas une consigne d'exposition. Le Camera Neutral IPR est plus
proche du Camera Neutral LR que la paire Adobe Standard correspondante
sur les cinq scènes disponibles, mais les **cibles changent aussi** : ce
n'est pas l'effet d'un correctif appliqué à un même rendu de référence.

Sur 9421, le p95 ΔE00 de la paire Neutral reste **12,6502** et sa médiane
**8,3886**. Lightroom est encore plus lumineux : ΔL* moyen **+8,5961**,
Δa* **−1,1932**, Δb* **+4,5561**. Il subsiste donc un écart de lumière et
de couleur, pas uniquement une température à corriger. La comparaison
visuelle boîtier/LR et le style Neutre rapportés en section 18.1 restent
utiles, mais ne valident pas la fidélité du rendu IPR.

À titre descriptif, les deux exports **Lightroom** Adobe Standard/Camera
Neutral diffèrent aussi : ΔE00 moyen **3,9673** sur 9421. Neutre est bien
une cible de rendu différente, pas un label interchangeable avec Adobe
Standard ou une référence physique universelle.

### 20.3. Ce que change réellement la WB As Shot

Par rapport au lot Custom 5200 K/Tint 0 de la section 19, les nouveaux
Adobe Standard As Shot donnent des ΔE00 légèrement plus faibles sur les
six scènes. La luminance moyenne Lightroom ne change que de **moins de
0,2 %** par scène. Le passage à As Shot élimine donc une réserve de
protocole, sans expliquer le grand déficit lumineux IPR.

Les six écarts Adobe Standard restent de +0,947 à +1,406 EV sur les
moyennes Y. Il n'est pas justifié de régler la température par défaut,
la nuance ou les niveaux blancs pour compenser ce seul résultat. Les
corrections de précision sRGB/noir de la v3 restent acquises et distinctes
de cette investigation du rendu de base.

### 20.4. Hautes lumières et modèles de diagnostic

Le modèle puissance ajusté sur cinq scènes et évalué sur la sixième
rapproche encore les six paires Adobe Standard : ΔE00 de
**4,1913 / 2,6475 / 1,6966 / 2,7889 / 5,1403 / 2,3470** dans l'ordre
0007/0037/0043/0327/9421/9950. Le gain seul détériore toujours 0043.

Le modèle puissance écrête cependant au moins un canal sur **8,1098 %**
des pixels échantillonnés de 0007 et **6,2586 %** de ceux de 9421.
Ces coefficients ne sont donc **pas intégrables en l'état**, même si
le ΔE00 moyen baisse. Un ajustement à Lightroom ne remplace pas un
rendu tonal contrôlant les hautes lumières et stabilisant la teinte.

Même le Camera Neutral natif doit être vérifié dans les basses valeurs :
sur 9421, **9,8561 %** des points IPR ont au moins un canal ≤1/65535,
contre **7,4915 %** dans LR Neutral. Cela ne signifie pas que ces pixels
entiers sont noirs ; cela signale une différence à surveiller dans le
traitement des couleurs sombres. Les fractions avec un canal ≥65534
sont respectivement **0,00159 %** et **0,03805 %** pour cette paire.

### 20.5. Conclusion et prochaine investigation

**Le protocole As Shot/sans LCP est désormais conforme sur ce lot.** La
comparaison Camera Neutral au même profil est réalisée, y compris sur
9421 : elle soutient la piste d'une différence de rendu des profils,
mais ne valide ni leur compatibilité complète ni une parité Lightroom.
Aucun nouveau profil par défaut ni correctif de production n'est appliqué.

La prochaine expérience recommandée doit séparer :

- **Courbe explicite Camera Neutral :** application stabilisant la teinte
  et interpolation, puis contrôles des gris, couleurs sombres et hautes
  lumières. Les cinq nouvelles références permettent maintenant un test
  au même profil, au lieu d'une comparaison à Adobe Standard.
- **Courbe implicite Adobe Standard :** hypothèse de compatibilité de
  la section 18.6 à tester indépendamment, avec courbe bornée et sans
  coefficients appris copiés du corpus. L'absence de tag explicite ne
  suffit pas à identifier la courbe exacte de Lightroom.

Les RAW et les nouveaux TIFF suffisent pour cette étape. Le Camera
Neutral 0037 pourrait compléter la couverture de végétation, mais il
n'est pas un préalable pour avancer. Une charte physique restera
nécessaire pour valider la fidélité absolue, indépendamment de LR/Canon.

### 20.6. Reproduction autonome et tests

```bash
python3 tests/analyse_tonal_rendering.py \
  --probe build-diagnostic-profils-raw-20261007 \
  --reference docs/validation/raw/mesure-base-colorimetrique-v3-20261007.json \
  --lr-suffix=-LR-AdobeStandard-AsShot --geometry=without_lcp \
  --neutral-suffix=-LR-CameraNeutral-AsShot \
  --output NOUVELLE_MESURE_AS_SHOT.json
python3 -m unittest discover -s tests -p test_analyse_base_colorimetrique.py
```

L'option Neutral est facultative : les scènes absentes sont enregistrées
explicitement, pas remplacées silencieusement par un autre profil. Les
mesures antérieures ne sont pas écrasées. L'audit choisit le profil attendu
selon la série, vérifié par un nouveau test ciblé.

**25/25 tests analyseur réussis** ; empreinte de l'analyseur conforme à
la mesure enregistrée. Aucun changement C++ ni compilation nécessaire.

## 21. Courbe explicite Camera Neutral — investigation du 8 octobre 2026

### 21.1. Périmètre et statut du moteur de production

Le moteur de production **sait lire et appliquer le DCP Camera Neutral**
par `CameraProfileEngine::applyDcp`. Les variantes précédentes et celles
de cette expérience réutilisent réellement cette fonction. En revanche,
**la sélection automatique d'un profil selon le Picture Style Canon n'est
pas implémentée** : la sélection actuelle dépend du manifeste local, du
cache ou du premier DCP compatible trouvé après tri des chemins. Le lot
de production étudié sélectionnait Adobe Standard ; ce n'est pas une
préférence explicite pour ce nom codée dans le moteur.

Il faut donc distinguer support générique du DCP, choix du profil et
compatibilité complète de son rendu. La présente investigation ne modifie
ni le choix par défaut ni `src/`, et ne nécessite pas de nouveau build IPR.

L'expérience porte uniquement sur **l'opérateur de courbe explicite**,
après les tables du profil. Elle ne prétend pas résoudre la courbe
implicite Adobe Standard, la normalisation RAW, la WB ou toutes les
différences entre Lightroom et IPR.

### 21.2. Protocole isolé, témoins et provenance

Les cinq références Camera Neutral As Shot de la section 20 sont réutilisées,
sans LCP, avec les **mêmes recadrages 0/1 pixel**, 504 600 points par paire.
0037 est enregistré comme absent, sans remplacement silencieux.

1. Une copie locale de Camera Neutral est créée dans un dossier `build-*`
   ignoré par Git. Seul l'identifiant du tag de courbe `50940` devient un
   tag de diagnostic inconnu `65000` ; tous les autres octets sont identiques.
   Le profil installé dans ProgramData n'est jamais écrit. La copie ne
   doit pas être distribuée ni utilisée dans le catalogue de production.
2. La sonde C++ existante applique ce DCP sans courbe aux grilles caméra
   historiques. Les empreintes caméra et le Kelvin doivent être identiques
   à ceux de l'expérience originale. Matrices, LUT, offset −0,5 EV et WB
   restent inchangés.
3. Les flottants sRGB linéaires sont ramenés en ProPhoto avec **les mêmes
   constantes de matrice arrondies que la production**. La courbe originale
   est réappliquée suivant quatre variantes : par canal/linéaire, par
   canal/cubique, teinte stabilisée/linéaire, teinte stabilisée/cubique.
   La sortie sans courbe est aussi mesurée comme ablation, pas comme correctif.
4. Le gamut mapping SDR et l'encodage sRGB 16 bits sont reproduits, avec
   contrôle contre le témoin de production. L'écart maximal du témoin est
   **un seul niveau 16 bits**, à l'arrondi flottant près ; l'erreur moyenne
   reste inférieure à **0,00053 niveau**. Les ΔE00 du témoin et des recadrages
   reproduisent ceux de la section 20 à 10⁻¹⁰ près.
5. Les TIFF, DCP et flottants utilisés sont contrôlés par SHA-256. Aucun
   coefficient de courbe n'est ajusté aux photographies. Deux régions
   fixes, définies par LR, complètent la moyenne globale : Y ≤0,02 et les
   5 % de points les plus lumineux. Il ne s'agit pas de ROI sémantiques.

Le profil contient **128 points**, dont **quatre segments très légèrement
décroissants** (plus petite variation d'ordonnée ≈−0,00010020). Ils sont
conservés, pas remplacés par une courbe globalement monotone inventée.
L'interpolation cubique expérimentale préserve la forme locale ; ce
n'est **pas** une affirmation de conformité à la spline Adobe.

Preuve de contrôle faisant référence :
[mesure courbe Neutral contrôlée](../raw/mesure-courbe-dcp-neutral-20261008-controle.json).
La [première exécution](../raw/mesure-courbe-dcp-neutral-20261008.json) est
conservée ; le contrôle utilise l'encodage en double précision avant
quantification et ajoute la vérification des métriques historiques.

### 21.3. Modèle mathématique et sources

Pour un pixel ProPhoto borné, on pose `m = min(R,G,B)`, `M = max(R,G,B)`.
La production applique `T(R), T(G), T(B)` indépendamment. L'alternative
applique la courbe aux extrêmes, puis, pour chaque canal `c` :

```text
c' = T(m) + (c − m) / (M − m) × [T(M) − T(m)]
si M = m : c' = T(m).
```

La position relative du canal médian est conservée. Lorsque les extrêmes
transformés restent ordonnés, la teinte HSV/HSL est préservée **dans
ProPhoto**, mais pas nécessairement la teinte Lab après changement de
gamut ; la saturation et la luminance peuvent changer. Aucun pixel du
corpus testé ne présente d'inversion des extrêmes due aux petites
décroissances de la courbe.

Cette approche est décrite par l'auteur de
[DCamProf, section sur les courbes de rendu](https://rawtherapee.com/mirror/dcamprof/dcamprof.html)
et vérifiable dans
[RawTherapee, opérateur AdobeToneCurve](https://github.com/RawTherapee/RawTherapee/blob/dev/rtengine/curves.h#L1794-L1892).
Ces sources justifient **l'expérience**, pas une parité avec la version
de Lightroom installée. Le diagnostic est une implémentation NumPy de
la formule ci-dessus, sans intégration du code GPL RawTherapee au moteur.

### 21.4. Résultats au même profil

| Scène | ΔE00 production | ΔE00 teinte stabilisée | p95 production → stabilisé | Écart moyen Y, EV, production → stabilisé |
| --- | ---: | ---: | ---: | ---: |
| 0007 | 5,9222 | 5,9313 | 7,7456 → 7,7289 | +0,383 → +0,384 |
| 0043 | 2,4276 | 2,4282 | 8,5283 → 8,2637 | +0,698 → +0,688 |
| 0327 | 5,7971 | **5,5920** | 9,6709 → 9,3461 | +0,635 → +0,627 |
| 9421 | 8,3671 | **8,1087** | 12,6502 → 11,6551 | +0,552 → +0,556 |
| 9950 | 4,8969 | 4,9480 | 9,5465 → 9,5788 | +0,536 → +0,542 |

La moyenne ΔE00 baisse sur **deux scènes sur cinq** ; le p95 baisse sur
quatre. L'écart moyen de teinte Lab sur les pixels éligibles baisse sur
les cinq : 0043 de 5,670° à **2,620°**, 0327 de 7,004° à **3,593°**,
9421 de 8,572° à **7,270°**. Cette amélioration n'est donc pas équivalente
à une amélioration uniforme du rendu global.

La variante cubique n'apporte pas de gain supplémentaire : ses ΔE00
avec teinte stabilisée sont **5,9324 / 2,4331 / 5,5978 / 8,1105 / 4,9542**.
L'interpolation seule n'explique pas le déficit constaté. Retirer
complètement la courbe détériore les cinq comparaisons : la courbe
explicite est déjà active et utile dans IPR, elle n'est pas « oubliée ».

### 21.5. Ombres, gamut et hautes lumières

Sur 9421, les points avec au moins un canal ≤1/65535 passent de **9,8561 %
à 8,8052 %**, mais ceux avec au moins un canal ≥65534/65535 passent de
**0,00159 % à 0,00515 %**. Le ΔE00 de la région sombre baisse de 3,7143
à 3,5926, tandis que celui des hautes lumières augmente de **6,5308 à
6,9843**. Le p95 global meilleur ne suffit donc pas à valider la protection
des hautes lumières.

Sur 0327, les points avec un canal très bas passent au contraire de
**0,03131 % à 0,49623 %** ; son ΔE00 de hautes lumières augmente légèrement
de 6,6869 à 6,7613. Cela impose d'étudier le couplage entre l'opérateur,
les couleurs hors gamut et la compression sRGB. Ces fractions ne signifient
pas que les pixels entiers sont noirs ou blancs.

### 21.6. Conclusion et suite recommandée

**La courbe par canal constitue bien une différence d'implémentation
pertinente, mais sa substitution seule ne corrige pas le rendu de base.**
Le déficit lumineux reste presque inchangé ; l'interpolation cubique ne
résout pas non plus l'écart. Aucun correctif de production, choix de
profil automatique ou compensation EV n'est appliqué à l'issue de ce test.

L'investigation suivante doit séparer le **rendu tonal de base manquant**
et la **normalisation/préparation des valeurs en entrée du profil**, avec
contrôle de l'ordre offset d'exposition/LUT/courbe et de la référence DNG.
Il faut aussi isoler le gamut mapping sur les couleurs sombres et les
hautes lumières avant de proposer un nouvel opérateur de production.
Pour Adobe Standard sans courbe explicite, toute courbe de compatibilité
reste une hypothèse à documenter : on ne peut pas déduire la courbe exacte
Lightroom des seuls tags absents, ni copier les coefficients appris sur ce lot.

**Aucune nouvelle image n'est nécessaire pour poursuivre ces diagnostics.**
0037 Camera Neutral reste un complément facultatif ; une charte demeure
nécessaire pour une validation physique absolue.

### 21.7. Reproduction et tests ciblés

Les dossiers/sorties existants sont refusés : choisir de nouveaux noms
pour une nouvelle campagne. Ne pas copier le DCP expérimental dans `profiles/`.

```bash
python3 tests/investigate_dcp_curve.py --prepare \
  --profile '/mnt/c/ProgramData/Adobe/CameraRaw/CameraProfiles/Camera/Canon EOS R6 Mark III/Canon EOS R6 Mark III Camera Neutral.dcp' \
  --diagnostic-profile build-canon-courbes-20261008/neutral-no-curve.dcp
```

Puis, sous PowerShell Windows avec les DLL Qt/OpenCV/LibRaw dans `PATH` :

```powershell
.\build-windows-msvc\Release\tonalBaseProbe.exe --profile-samples `
  build-canon-blancs-ab-20261007/manifest.json `
  build-canon-courbes-20261008/grids `
  "$PWD\build-canon-courbes-20261008\neutral-no-curve.dcp"
```

Enfin, sous WSL/Linux depuis la racine du dépôt :

```bash
python3 tests/investigate_dcp_curve.py \
  --profile '/mnt/c/ProgramData/Adobe/CameraRaw/CameraProfiles/Camera/Canon EOS R6 Mark III/Canon EOS R6 Mark III Camera Neutral.dcp' \
  --diagnostic-profile build-canon-courbes-20261008/neutral-no-curve.dcp \
  --probe build-canon-courbes-20261008/grids --original-probe build-diagnostic-profils-raw-20261007 \
  --reference docs/validation/raw/mesure-profils-as-shot-20261007.json \
  --output NOUVELLE_MESURE_COURBE.json
python3 -m unittest discover -s tests -p test_analyse_base_colorimetrique.py
python3 -m unittest discover -s tests -p test_build_windows.py
```

**31/31 tests analyseur et 10/10 tests de sélection réussis.** Six nouveaux
tests couvrent les permutations de canaux, gris/identité, interpolation
bornée, décroissances locales, modification isolée du tag dans les deux
endiannesses et gamut mapping. La sonde est ajoutée à la cartographie des
tests ciblés. Aucun C++ n'a changé ; pas de recompilation ni de suite
complète nécessaire. L'empreinte de l'analyseur correspond à la mesure
de contrôle conservée.

## 22. Ordre DCP, exposition de base Adobe et gamut — reprise du 8 octobre 2026

### 22.1. Question et périmètre

La reprise sépare trois hypothèses : l'ordre offset/LUT/courbe, la
compensation d'exposition manquante en amont, et la compression du gamut
de sortie. Les cinq références Camera Neutral As Shot conformes sont
réutilisées, aux mêmes recadrages et au pas natif 8 ; **504 600 points
par scène**, sans nouvel ajustement géométrique, WB ou courbe apprise.
0037 reste absent des comparaisons Neutral, mais son RAW participe à
l'audit des métadonnées DNG.

**Précision importante : ce DCP Neutral ne contient pas de HueSatMap.**
Il contient une LookTable 90×16×16, encodée sur l'axe Value en sRGB,
128 points de courbe et un BaselineExposureOffset de −0,5 EV. L'ordre
HueSatMap/exposition ne peut donc pas expliquer un écart sur ce profil
précis. Les variantes déplacent l'offset autour de la **LookTable**.
Ce constat ne s'étend pas à Adobe Standard, qui possède une HueSatMap.

### 22.2. Expérience indépendante et contrôles

L'outil [investigate_dcp_order.py](../../../tests/investigate_dcp_order.py)
repart des grilles caméra historiques et reproduit les matrices arrondies,
l'interpolation des illuminants, la LookTable, la courbe linéaire et
la sortie sRGB. Le calcul d'encodage de la table affecte seulement
**Value**, pas les composantes RGB ni Hue/Saturation.

Deux témoins sont requis avant toute comparaison : la sortie C++ sans
courbe de la section 21, et la sortie historique avec courbe. L'erreur
maximale du premier est **4,522×10⁻⁷** en flottant ; celle du second est
**un niveau 16 bits**, avec une moyenne ≤**0,003914 niveau**. Le ΔE00
du témoin et les recadrages reproduisent les valeurs historiques à 10⁻¹⁰
près. Les empreintes des grilles, profils, références et dépendances
de l'analyseur sont conservées dans les mesures.

Variantes isolées :

- production reconstruite : `T(L(2^e × P))`, avec `e = −0,5` ;
- offset scalaire après LookTable : `T(2^e × L(P))` ;
- offset négatif avec épaule protégeant le blanc, puis la même courbe ;
- même expérience avec opérateur de courbe stabilisant la teinte ;
- offset neutralisé, **ablation uniquement**, sans modification du DCP ;
- exposition de base lue dans les DNG, ajoutée à l'offset du profil ;
- sur la sortie de production inchangée : clipping simple ou compression
  conservant la luminance signée avant réduction de chroma.

`P` désigne ici le RGB ProPhoto linéaire avant offset et table, `L` la
LookTable et `T` la courbe. Les variantes ne sont pas un rendu SDK complet.
L'épaule expérimentale est C1, de pente `s = 2^e` jusqu'à `x = 1/4`, puis
`s*x + (1−s)/(3/4)^2 * (x−1/4)^2`, de façon à conserver le blanc.

Le [SDK DNG, source épinglée](https://android.googlesource.com/platform/external/dng_sdk/+/de700ad461e35af50b28b861943a0b0753b10929/source/dng_render.cpp)
distingue rampe d'exposition et correction négative dans la courbe.
Notre expérience en isole une forme mathématique, sans reproduire
Stage3Gain, la rampe de noir ni toutes ses interpolations. Ce n'est
pas une preuve du pipeline interne de Lightroom.

La [spécification DNG 1.7.1.0](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf),
rubriques ProfileLookTableData et BaselineExposureOffset, distingue
la compensation du négatif et celle du profil ; la LookTable se situe
après les compensations d'exposition et avant la courbe. Une équivalence
avec Lightroom ne découle pas du seul respect de cet ordre.

### 22.3. Information nouvelle : exposition de base dans les DNG Adobe

Adobe DNG Converter **18.6**, déjà installé, a converti les six RAW en
copies mosaïquées compressées sans perte dans
`build-canon-dng-reference-20261008/`, avec `-c -p0 -d DOSSIER RAW`.
Aucun RAW, TIFF, sidecar ni profil installé n'a été modifié. Ces DNG
restent locaux et ignorés : ils contiennent les photographies et des
profils embarqués, **ils ne doivent pas être distribués avec le dépôt**.

L'[audit contrôlé](../raw/audit-dng-reference-20261008-controle.json)
conserve les SHA-256 des RAW, DNG, convertisseur et DCP, les métadonnées
IFD0 et SubIFD RAW. L'[outil de lecture](../../../tests/audit_dng_baseline.py)
ne décode pas les pixels et ne publie pas les tables du profil.

| RAW | BaselineExposure du DNG | Offset du profil | Somme indicative, avant prise en compte de Stage3Gain |
| --- | ---: | ---: | ---: |
| 0007, 0037, 0043, 0327, 9950 | +0,26 EV | −0,50 EV | −0,24 EV |
| 9421 | +0,28 EV | −0,50 EV | −0,22 EV |

Le profil embarqué est nommé Camera Neutral. Ses ForwardMatrix1/2,
dimensions/données de LookTable, encodage, courbe et offset sont
**numériquement identiques** au DCP Neutral installé : sept contrôles
par fichier, soit 42 correspondances sur les six DNG. Les tables sont
comparées en mémoire et seules leurs empreintes numériques sont conservées.

Les DNG contiennent aussi des matrices CameraCalibration et des niveaux
noir/blanc. Sur 9421, par exemple, la calibration bleue vaut 1,0017 et
le SubIFD RAW annonce noir 2048 et blanc 14351. Ces valeurs ne justifient
pas de remplacer automatiquement les estimations de production : la
normalisation et le stade exact d'application restent à réconcilier.

Le champ LibRaw à −999 constaté auparavant signifie toujours **inconnu**.
L'audit apporte enfin des valeurs Adobe observables ; il ne transforme
pas ce marqueur en une constante +0,26 ou +0,28 valable pour tous les
fichiers, modèles, ISO ou versions de convertisseur.

### 22.4. Résultats : l'exposition contribue beaucoup plus que l'ordre seul

ΔE00 moyen ; les valeurs plus faibles indiquent une meilleure proximité
avec les TIFF LR du même profil, pas une fidélité physique absolue :

| Scène | Production | Offset après LookTable, scalaire | Base DNG ajoutée, offset conservé | Offset neutralisé, ablation |
| --- | ---: | ---: | ---: | ---: |
| 0007 | 5,9222 | 5,9810 | **2,0119** | 2,1619 |
| 0043 | 2,4276 | 2,3921 | **1,2776** | 0,4573 |
| 0327 | 5,7971 | 5,7354 | **3,0371** | 1,2841 |
| 9421 | 8,3671 | 7,9856 | **4,7581** | 3,1163 |
| 9950 | 4,8969 | 4,9055 | **2,4614** | 0,7407 |

La prise en compte expérimentale de la base DNG améliore les cinq
moyennes et les cinq p95. Ses p95 sont respectivement **2,9937 / 4,6839 /
5,0046 / 9,1840 / 4,6059**. Le résidu lumineux reste toutefois mesurable :

| Scène | `log2(Y_LR moyen / Y_IPR moyen)`, production | Avec base DNG |
| --- | ---: | ---: |
| 0007 | +0,383 EV | +0,113 EV |
| 0043 | +0,698 EV | +0,331 EV |
| 0327 | +0,635 EV | +0,283 EV |
| 9421 | +0,552 EV | +0,230 EV |
| 9950 | +0,536 EV | +0,243 EV |

Ce rapport de moyennes n'est pas une exposition à appliquer pixel par
pixel. Neutraliser l'offset est encore meilleur sur quatre scènes,
mais ne correspond pas à la compensation réellement observée dans
les DNG ; **cela ne prouve pas que le tag DCP est erroné**. Cette
ablation indique une composante tonale/normalisation résiduelle à isoler.

L'épaule seule, offset maintenu à −0,5, donne **5,9817 / 2,3923 / 5,7354 /
7,9831 / 4,9055** : pas de résolution du déficit. La variante combinée
avec stabilisation de teinte améliore 9421 à 7,6735, mais reste loin
du gain apporté par la préparation d'exposition.

### 22.5. Contrôles des basses valeurs, hautes lumières et gamut

La variante avec base DNG réduit le ΔE00 des 5 % de points LR les plus
lumineux sur les cinq scènes. Sur 9421, cette moyenne passe de **6,5308
à 3,0771**, mais la fraction avec au moins un canal haut passe de
**0,00159 % à 0,04340 %** : bénéfice et risque doivent être conservés
ensemble. La fraction avec un canal bas passe de **9,8561 % à 10,0888 %**.
Sur 0327, cette dernière passe de **0,03131 % à 0,47107 %**. Il ne s'agit
pas de fractions de pixels entièrement noirs ou blancs.

Déplacer l'offset scalaire après la LookTable porte cette fraction basse
à **21,3155 %** sur 9421 et **4,0135 %** sur 0327. L'ordre ne se juge
donc pas uniquement au ΔE00 moyen.

Avec la sortie de production inchangée, la compression conservant la
luminance signée ne résout rien : ΔE00 9421 **8,3671 → 8,3923** et
variations très faibles sur les autres scènes. Le clipping simple ne
réduit pas davantage le déficit global. Cela écarte ces **deux variantes
isolées** comme solution principale, pas toute amélioration possible
du gamut, ni leur interaction avec un futur rendu plus lumineux.

### 22.6. Conclusion et prochaine étape

**Une contribution d'exposition Adobe manquante dans le chemin étudié
est désormais étayée par des métadonnées et une ablation contrôlée.**
L'exposition de base caméra et l'offset DCP sont deux informations
différentes ; appliquer uniquement l'offset négatif du profil n'est
pas une comparaison complète au rendu Adobe.

Ce résultat ne justifie encore ni de supprimer l'offset, ni d'ajouter
une compensation fixe au moteur. Aucun fichier `src/`, profil du
catalogue ou réglage par défaut n'a changé dans cette reprise.

Suite recommandée, avec les fichiers déjà disponibles :

1. Réconcilier les valeurs caméra/WB, CameraCalibration, normalisation
   des blancs et Stage3Gain avec les DNG produits, sans recompter un gain.
2. Vérifier l'origine exploitable de BaselineExposure pour les CR3
   natifs ; traiter explicitement les valeurs inconnues. Ne pas utiliser
   Adobe DNG Converter à chaque ouverture : c'est un témoin de diagnostic.
3. Refaire cette expérience avec Adobe Standard au même profil, en
   isolant sa courbe implicite ; les gains Neutral ne s'y extrapolent pas.
4. Seulement ensuite proposer une intégration avec un contrat d'exposition
   explicite, contrôles de clipping/teinte et tests de non-régression.

**Aucune nouvelle image utilisateur n'est nécessaire à ce stade.** La
charte demeure nécessaire pour une validation physique absolue.

### 22.7. Reproduction et preuves conservées

Première ablation, sans référence DNG :
[mesure initiale](../raw/mesure-ordre-dcp-neutral-20261008.json).
Complément utilisant les expositions lues dans les DNG :
[mesure contrôlée](../raw/mesure-ordre-dcp-neutral-20261008-dng-controle.json).
Son entrée DNG est l'[audit initial](../raw/audit-dng-reference-20261008.json) ;
l'[audit de contrôle](../raw/audit-dng-reference-20261008-controle.json)
ajoute les 42 comparaisons avec le DCP, sur les mêmes fichiers DNG.
Les mesures initiales restent immuables ; leurs empreintes désignent
la version exacte de l'outil exécutée, pas nécessairement sa version courante.

Depuis la racine, sous WSL, avec un **nouveau nom de sortie** :

```bash
python3 tests/investigate_dcp_order.py \
  --profile '/mnt/c/ProgramData/Adobe/CameraRaw/CameraProfiles/Camera/Canon EOS R6 Mark III/Canon EOS R6 Mark III Camera Neutral.dcp' \
  --camera-probe build-canon-blancs-ab-20261007 \
  --original-probe build-diagnostic-profils-raw-20261007 \
  --no-curve-probe build-canon-courbes-20261008/grids \
  --reference docs/validation/raw/mesure-profils-as-shot-20261007.json \
  --dng-audit docs/validation/raw/audit-dng-reference-20261008-controle.json \
  --output NOUVELLE_MESURE_ORDRE_DCP.json
python3 -m unittest discover -s tests -p test_analyse_base_colorimetrique.py
python3 -m unittest discover -s tests -p test_build_windows.py
```

**37/37 tests analyseur et 10/10 tests de sélection réussis.** Les six
nouveaux tests couvrent épaule/continuité/blanc, encodage Value seul,
table à une division Value, gamut signé, non-commutativité de l'ordre
et lecture de métadonnées sans décodage ni écriture des pixels.
Les deux nouveaux diagnostics sont cartographiés dans les tests ciblés.
Pas de recompilation Windows ni de suite native complète : aucun code
de production n'a changé lors de cette expérience.

## 23. Normalisation caméra et courbe sur une entrée correctement exposée — 8 octobre 2026

### 23.1. Résultat et périmètre

**Le déficit de luminosité moyenne est expliqué sur les cinq références
Camera Neutral disponibles.** Deux contributions distinctes manquaient
dans l'expérience initiale : le niveau blanc de normalisation du RAW et
l'exposition de base du négatif. Sur cette entrée corrigée, la courbe
préservant les teintes améliore aussi les cinq ΔE00 moyens et leurs p95.
Cela ne valide ni une fidélité physique absolue, ni tous les profils,
ni une règle universelle de décodage Canon.

L'expérience utilise les décodages indépendants `specular_fixed` de §17 :
ils ont réellement été produits avec le blanc corrigé avant la mise à
l'échelle et le dématriçage, et l'ajustement automatique du maximum désactivé.
Leur blanc est maintenant corroboré par les six DNG Adobe. On ne remplace
pas ces décodages par une multiplication tardive des pixels RGB.
Les RAW, DNG, profils, grilles et références sont contrôlés par empreintes.
0037 participe à l'audit des métadonnées, mais n'a pas de référence TIFF
Camera Neutral : elle n'est pas comptée dans les cinq comparaisons.

### 23.2. Séparer les gains et éviter une double balance des blancs

Pour les noirs uniformes de ce lot, l'échelle linéaire utilise le dénominateur
`maximum − noir`. Le gain correspondant au changement de blanc est donc :

```text
gain_blanc = ancien_dénominateur / (blanc_DNG − noir)
gain_base = 2^BaselineExposure
```

`no_auto_bright` désactive la luminosité automatique liée à l'histogramme,
mais pas à lui seul l'ajustement du maximum de LibRaw. Cela explique notamment
pourquoi le gain de 0007 diffère des autres scènes ayant le même blanc DNG.
L'inspection porte sur la source locale LibRaw **0.22.2**, dont les fichiers
sont hachés dans la mesure : aucune application de `baseline_exposure` n'a
été identifiée dans le chemin de post-traitement audité. Le CR3 natif annonce
toujours −999, c'est-à-dire une valeur inconnue. Voir les sources primaires
[mise à l'échelle](https://raw.githubusercontent.com/LibRaw/LibRaw/0.22.2/src/postprocessing/postprocessing_utils_dcrdefs.cpp)
et [chaîne de traitement](https://raw.githubusercontent.com/LibRaw/LibRaw/0.22.2/src/postprocessing/dcraw_process.cpp).

La WB LibRaw et l'inverse de `AsShotNeutral` des DNG concordent sur les six
scènes à moins de `4,30 × 10⁻⁷` en erreur relative. Pour la construction
ForwardMatrix, on vérifie mathématiquement :

```text
A = AnalogBalance × CameraCalibration
ReferenceNeutral = inverse(A) × AsShotNeutral
WB_reference = diag(1 / ReferenceNeutral) × inverse(A)
```

Avec les matrices diagonales de ce lot, cela se simplifie en
`diag(1 / AsShotNeutral)` : le coefficient bleu 1,0017 ne constitue pas
un gain supplémentaire à appliquer isolément. Le résidu sur une grille
déjà équilibrée par LibRaw est presque l'identité et ne change pas le
résultat à quatre décimales. Cette vérification ne remplace pas la gestion
complète des signatures, matrices non diagonales et illuminants décrite
par la [spécification DNG 1.7.1, construction CameraToXYZ_D50](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf).

**Stage3Gain n'a pas été mesuré à l'exécution du SDK DNG.** Aucun décodage
des pixels DNG ni rendu SDK n'est réalisé ici. Son rôle comptable ne permet
pas de transposer un gain SDK à LibRaw sans vérifier où il a déjà été appliqué.
L'offset Camera Neutral −0,5 EV reste conservé : il ne faut ni le supprimer,
ni remplacer ces deux contributions par une compensation empirique +0,5 EV.

### 23.3. Blancs corroborés et contributions d'exposition

| Scène | Noir | Blanc DNG = candidat Canon specular | Ancien dénominateur | Gain blanc, EV | Base DNG, EV | Somme, EV |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 0007 | 512 | 13995 | 14618 | +0,1166 | +0,26 | +0,3766 |
| 0037 | 512 | 13995 | 15871 | +0,2353 | +0,26 | +0,4953 |
| 0043 | 512 | 13995 | 15871 | +0,2353 | +0,26 | +0,4953 |
| 0327 | 512 | 13995 | 15679 | +0,2177 | +0,26 | +0,4777 |
| 9421 | 2048 | 14351 | 14335 | +0,2205 | +0,28 | +0,5005 |
| 9950 | 512 | 13995 | 15871 | +0,2353 | +0,26 | +0,4953 |

Il s'agit du candidat **specular**, pas du candidat **normal**, plus bas
et plus agressif. La concordance concerne les six fichiers audités et la
disposition ColorData 66 étudiée ; elle n'autorise pas une lecture aveugle
d'un offset MakerNote sur tous les boîtiers ou ISO.

### 23.4. Ablation et nouvelle comparaison de courbe

Chaque comparaison porte sur 504 600 points issus des mêmes recadrages
natifs, au pas 8, avec TIFF Lightroom 16 bits, même profil et WB As Shot.
Le témoin reconstruit reproduit le ΔE00 de §22. Le témoin avant changement
d'opérateur de courbe est identique au code 16 bits près, et ici son erreur
maximale est exactement zéro sur les cinq grilles.

| Scène | ΔE00 moyen témoin | Base seule | Blanc + base | + courbe teinte stable, linéaire | p95 final | Écart Y moyen final, EV |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 0007 | 5,9222 | 2,0119 | 0,7982 | **0,7236** | 1,8680 | +0,000214 |
| 0043 | 2,4276 | 1,2776 | 0,4522 | **0,2047** | 0,7604 | +0,000170 |
| 0327 | 5,7971 | 3,0371 | 1,2381 | **0,7152** | 2,0531 | −0,000195 |
| 9421 | 8,3671 | 4,7581 | 3,1186 | **2,8498** | 7,4015 | −0,001449 |
| 9950 | 4,8969 | 2,4614 | 0,7325 | **0,5314** | 1,5754 | +0,000109 |

L'écart EV est `log2(moyenne(Y_LR) / moyenne(Y_expérience))` : il ne s'agit
ni d'une erreur EV par pixel, ni d'une preuve de superposition parfaite.
Blanc + base seuls ramènent déjà sa valeur absolue sous 0,009 EV ; avec
la courbe préservant les teintes, elle reste sous 0,00145 EV.

La variante cubique donne respectivement 0,7237 / 0,2043 / 0,7150 /
2,8493 / 0,5310 en ΔE00 moyen. Son avantage est minime et non uniforme ;
ce n'est pas une reproduction certifiée de la spline Adobe.
Le bénéfice de l'opérateur de courbe est désormais uniforme sur ce lot,
contrairement à §21 où l'entrée conservait le déficit de normalisation.
Les anciennes mesures restent valides pour leur propre configuration.

### 23.5. Résidus et risques

9421 reste le cas à investiguer : ΔE00 moyen 2,8498, p95 7,4015, et 2,7601
dans le masque sombre fixe Lightroom (`Y ≤ 0,02`). Son écart ne peut plus
être décrit comme un simple déficit de luminosité globale. La courbe réduit
son erreur globale, mais son ΔE00 dans les 5 % de pixels les plus lumineux
passe de 1,4327 à 1,5084 : amélioration globale ne signifie pas amélioration
dans chaque région.

Après blanc + base + courbe linéaire, les fractions avec au moins un canal
de sortie bas sont 3,6199 % sur 0043, 0,9612 % sur 0327 et 9,1383 % sur
9421. Sur 9421, la fraction avec un canal haut est 0,0418 %, contre
0,0380 % dans Lightroom. Ces compteurs de sortie ne prouvent pas à eux seuls
une saturation du capteur. Ils justifient de conserver une analyse séparée
du clipping RAW, du gamut et des zones sombres, sans conclure que la
normalisation règle tout.

Une simple remise à l'échelle des anciennes grilles diffère du vrai
décodage corrigé (MAE de quelques codes 16 bits, environ 6,55 sur 9421),
notamment à cause de l'écrêtage, du dématriçage et des arrondis. Elle ne
constitue donc pas un correctif de production équivalent.

### 23.6. Décision et suite

**Aucun code `src/`, profil installé ou réglage par défaut n'a changé lors
de cette campagne.** L'outil ajouté est un diagnostic, pas un nouveau moteur.

1. Vérifier le même contrat de normalisation avec les références Adobe
   Standard déjà disponibles, sans extrapoler la courbe Neutral à un
   profil dépourvu de courbe explicite.
2. Définir l'origine et la priorité des niveaux blancs et de l'exposition
   de base des CR3, avec garde sur boîtier/version/disposition/valeurs,
   traçabilité et repli si la métadonnée est inconnue. Ne pas injecter
   +0,26 ou +0,28 EV universellement ni lancer DNG Converter à l'ouverture.
3. Isoler les résidus chromatiques/localisés de 9421 à exposition moyenne
   harmonisée avant de décider d'un traitement de gamut ou de table.
4. Proposer ensuite une intégration protégée et une campagne de
   non-régression incluant WB, détails, hautes lumières et export.

**Aucune nouvelle image n'est requise pour la prochaine investigation.**
Une référence Neutral de 0037 reste facultative ; une charte mesurée reste
nécessaire pour parler de fidélité physique, plutôt que de proximité Lightroom.

### 23.7. Reproduction, preuves et tests ciblés

Outil : [investigate_dcp_normalization.py](../../../tests/investigate_dcp_normalization.py).
Preuves immuables : [ablation initiale](../raw/mesure-normalisation-dcp-neutral-20261008.json),
[contrôle et audit des sources](../raw/mesure-normalisation-dcp-neutral-20261008-controle.json),
[complément courbe contrôlé](../raw/mesure-normalisation-dcp-neutral-20261008-rendu-controle.json).
Les empreintes relient chaque mesure à sa version exacte de l'outil.

Depuis la racine sous WSL, avec un nom de sortie non existant :

```bash
python3 tests/investigate_dcp_normalization.py \
  --probe build-canon-blancs-ab-20261007 \
  --reference docs/validation/raw/mesure-profils-as-shot-20261007.json \
  --dng-audit docs/validation/raw/audit-dng-reference-20261008-controle.json \
  --order-measure docs/validation/raw/mesure-ordre-dcp-neutral-20261008-dng-controle.json \
  --profile '/mnt/c/ProgramData/Adobe/CameraRaw/CameraProfiles/Camera/Canon EOS R6 Mark III/Canon EOS R6 Mark III Camera Neutral.dcp' \
  --libraw-source '/mnt/c/dev/vcpkg/buildtrees/libraw/src/0.22.2-fabf993e5f.clean' \
  --output NOUVELLE_MESURE_NORMALISATION.json
python3 -m unittest discover -s tests -p test_analyse_base_colorimetrique.py
python3 -m unittest discover -s tests -p test_build_windows.py
```

**42/42 tests analyseur et 10/10 tests de sélection réussis.** Les cinq
tests ajoutés couvrent le gain blanc avec noir soustrait, l'annulation
diagonale de calibration/WB, le cas non diagonal, les entrées invalides
et l'unicité des variantes. Le diagnostic est déclaré dans les règles
de sélection ciblée. Pas de build Windows ni de suite native complète :
aucune modification de production pendant cette étape.

## 24. Vérification croisée et intégration contrôlée — 8 octobre 2026

### 24.1. Conclusion corrigée : portée Neutral, pas tous les profils

La conclusion de §23 est confirmée **pour Camera Neutral et le lot mesuré**.
La vérification croisée montre qu'elle ne peut pas être étendue telle quelle
à Adobe Standard. Le profil Standard installé possède des tables, mais
pas de courbe tonale explicite ; aucune courbe implicite n'a été ajoutée
pour imiter Lightroom. Ce rendu manquant reste une piste, pas une cause
exclusivement démontrée de tout l'écart.

L'analyse indépendante Standard reproduit son témoin C++ avec une erreur
maximale d'un code 16 bits et une erreur moyenne inférieure à 0,0016 code.
Les mêmes blancs corroborés et expositions DNG améliorent les six ΔE00
moyens, mais presque pas 0043, et laissent un déficit tonal important :

| Scène | ΔE00 moyen témoin Standard | Blanc + base | p95 après | Écart Y moyen après, EV |
| --- | ---: | ---: | ---: | ---: |
| 0007 | 17,2034 | 12,7471 | 16,4199 | +0,9760 |
| 0037 | 5,1033 | 4,0598 | 13,8892 | +0,4984 |
| 0043 | 3,1599 | 3,1516 | 11,3590 | +0,4541 |
| 0327 | 9,3517 | 6,5748 | 16,6871 | +0,8019 |
| 9421 | 16,3734 | 11,6579 | 17,7251 | +0,9040 |
| 9950 | 9,1361 | 6,5205 | 16,4783 | +0,8784 |

Preuve : [mesure Standard](../raw/mesure-normalisation-adobe-standard-20261008.json).
Outil : [investigate_standard_normalization.py](../../../tests/investigate_standard_normalization.py).
Ces chiffres ne comparent pas deux profils différents : chaque sortie
Standard est confrontée à sa référence Lightroom Standard As Shot.

### 24.2. Intégration retenue : explicite et réversible

Le nouveau module [RawNormalization](../../../src/raw/RawNormalization.h)
résout un manifeste JSON **optionnel**, sans réinterpréter aveuglément les
MakerNotes Canon. Chaque entrée est liée aux empreintes SHA-256 exactes du
RAW et du DCP, au boîtier, à l'ISO, au noir RGBG mesuré et à la version
LibRaw. Elle porte un blanc, une exposition de base et une provenance DNG.
Ce manifeste n'est pas une base générale de calibration par modèle/ISO.

Garde-fous :

- Aucune lecture du manifeste ou du RAW supplémentaire si le mode est désactivé.
- CR3 Canon, WB caméra, DCP présent, exposition native inconnue (−999),
  noirs uniformes sans motif ni noir global ; sinon, pas de correction.
- Manifeste limité à 1 Mio et 256 entrées, schéma/version vérifiés ;
  valeurs finies, blanc entier borné, exposition limitée à ±2 EV,
  provenance et empreinte DNG requises, ambiguïtés refusées.
- Le blanc est installé dans les deux copies LibRaw **avant** le traitement ;
  l'ajustement automatique du maximum est désactivé pour cette calibration.
- L'exposition caméra est ajoutée une seule fois à l'offset DCP avant les
  tables. Aucun gain bleu additionnel, aucune modification du curseur EV.
- En l'absence de correspondance ou en cas de refus, le décodage historique
  demeure utilisé, sans appliquer seulement une moitié de la calibration.
- Les métadonnées de sortie indiquent application, blanc, exposition et
  provenance ; le cache de l'éditeur inclut l'empreinte du manifeste.

Le DNG lui-même n'est pas lu à l'ouverture et le convertisseur Adobe n'est
pas appelé. L'empreinte DNG est une trace d'audit, **pas** une authentification
cryptographique du contenu d'un manifeste édité manuellement. Le manifeste
local reste une configuration de confiance à préparer depuis les preuves.

### 24.3. Courbe : garde anti-inversion pixel par pixel

`CameraProfileEngine::DcpRenderOptions` sépare l'exposition caméra de celle
du profil et permet l'opérateur préservant la teinte HSV ProPhoto. L'appel
historique reste inchangé par défaut ; le nouveau mode est activé avec
la calibration contrôlée, pas pour toutes les images.

Pour les courbes bornées couvrant [0,1], on transforme les deux extrêmes
RGB, puis on interpole le canal intermédiaire selon sa position initiale.
Les gris évitent toute division par zéro. Les courbes non bornées ou ne
couvrant pas le domaine gardent l'opérateur historique.

**Correction issue du contrôle d'intégration :** Camera Neutral contient
quatre très petites décroissances, déjà repérées en §21. Refuser toute
courbe non monotone désactivait donc le nouvel opérateur sur ce profil.
La production vérifie désormais, pour chaque pixel, que les extrêmes
transformés ne sont pas inversés ; sinon elle utilise localement la courbe
par canal. Cela évite d'introduire une inversion de teinte.

Sur les entrées normalisées, ce repli concerne 12/504 600 points sur 9421
et 2/504 600 sur 0043, aucun sur les trois autres scènes. Par rapport à
l'expérience sans ce garde-fou de §23, la différence maximale est de
7 codes sur 9421 et 4 sur 0043. Les preuves historiques ne sont pas
remplacées ni présentées comme des mesures strictement identiques.

Le [SDK DNG, chaîne de rendu](https://android.googlesource.com/platform/external/dng_sdk/+/de700ad461e35af50b28b861943a0b0753b10929/source/dng_render.cpp)
sert à distinguer exposition et courbe de profil ; le code intégré est une
construction mathématique indépendante, pas une copie de sa spline ou
une promesse de parité avec Lightroom.

### 24.4. Vérification du vrai chemin de production Windows

Les cinq RAW ont été décodés intégralement par `RawDecoder`, avec LibRaw
0.22.2, le profil Camera Neutral, WB caméra et sans LCP. La sonde prend
ensuite une grille au pas 8 et utilise la vraie sortie SDR 16 bits du moteur.
Le témoin indépendant réutilise les décodages A/B frais, les mêmes gains
et un repli anti-inversion explicite ; ce n'est pas une comparaison de
deux fonctions C++ qui partageraient la même erreur.

| Scène | Écart maximal, codes 16 bits | MAE, codes 16 bits |
| --- | ---: | ---: |
| 0007 | 1 | 0,004012 |
| 0043 | 1 | 0,000859 |
| 0327 | 1 | 0,002832 |
| 9421 | 1 | 0,004153 |
| 9950 | 1 | 0,002411 |

Preuve : [vérification production](../raw/verification-normalisation-production-20261008.json).
Les pixels complets et manifestes de sonde restent locaux dans
`build-canon-normalization-20261008/*-prod-windows-v2` ; leurs empreintes
et celles des sources intégrées sont conservées dans le JSON.
La sonde ne modifie ni les RAW ni les TIFF de référence. Elle copie le DCP
dans un répertoire temporaire de diagnostic, supprimé automatiquement ;
aucune table propriétaire n'est ajoutée aux documents ou au dépôt.

La tentative sous WSL a été refusée par le contrat : cette installation
lie LibRaw 0.21.2, et non la version 0.22.2 de la calibration. Le garde-fou
n'a pas été affaibli pour faire passer ce test ; les contrôles corpus ont
été exécutés avec le binaire Windows correspondant à la preuve.

### 24.5. Activation pour essais et retour arrière

Le manifeste local a été préparé dans
`build-canon-normalization-20261008/normalization.json`. Il autorise
uniquement 0007, 0043, 0327, 9421 et 9950 avec le **DCP Neutral exact**.
0037, sans référence Neutral, en est volontairement exclue. Le manifeste
n'est pas installé automatiquement dans le catalogue de profils.

Pour le préparer à nouveau, sous WSL, avec un nouveau nom de sortie :

```bash
python3 tests/prepare_raw_normalization.py \
  --measure docs/validation/raw/mesure-normalisation-dcp-neutral-20261008-rendu-controle.json \
  --probe build-canon-blancs-ab-20261007 \
  --audit docs/validation/raw/audit-dng-reference-20261008-controle.json \
  --profile '/mnt/c/ProgramData/Adobe/CameraRaw/CameraProfiles/Camera/Canon EOS R6 Mark III/Canon EOS R6 Mark III Camera Neutral.dcp' \
  --output build-diagnostic-profils-raw-20261007/normalization.json
```

Pour une session Windows de test, définir la variable **avant le lancement** :

```powershell
$env:IPR_RAW_NORMALIZATION_MANIFEST = (Resolve-Path build-canon-normalization-20261008/normalization.json).Path
& .\build-windows-msvc\Release\appIPR.exe
```

Attention : le profil sélectionné doit être Camera Neutral, conforme à
l'empreinte du manifeste, et la WB automatique désactivée. Ce mécanisme
ne change pas la sélection du profil à ta place. Si le catalogue choisit
Adobe Standard ou un autre profil, la calibration Neutral **ne s'applique
pas**. Il n'existe pas encore de nouveau sélecteur de profil dans l'IHM.
Vérifier la trace `IPR RAW normalization` et la provenance lors du test.
Les sidecars peuvent aussi restaurer des réglages non neutres : les isoler
ou les remettre à zéro pour une comparaison de base.

Pour tester indépendamment du catalogue et des sidecars, la sonde dédiée
force un répertoire temporaire ne contenant que le DCP indiqué :

```powershell
& .\build-windows-msvc\Release\rawNormalizationCorpusProbe.exe `
  tests/image/base/6V1A9421.CR3 `
  'C:\ProgramData\Adobe\CameraRaw\CameraProfiles\Camera\Canon EOS R6 Mark III\Canon EOS R6 Mark III Camera Neutral.dcp' `
  build-canon-normalization-20261008/normalization.json `
  NOUVEAU_DOSSIER_DE_SONDE
```

Retour arrière : fermer l'application, retirer la variable avec
`Remove-Item Env:IPR_RAW_NORMALIZATION_MANIFEST -ErrorAction SilentlyContinue`,
puis relancer. Aucun RAW, TIFF, profil installé ou sidecar n'a besoin d'être
restauré. Les valeurs de calibration ne sont pas des constantes codées
en dur dans le moteur.

### 24.6. Validation et reste à faire

- Build Windows Release de `appIPR.exe` et `iprGenerateTests.exe` réussi.
- Cinq suites natives Windows RAW/profils/export réussies.
- Huit suites ciblées WSL pipeline/réglages/générateur/RAW/profils/export
  réussies, avant la suite complète liée au changement CMake.
- **Suite native complète WSL : 16/17 réussis, pas entièrement verte.**
  `tonalReferenceComparisonProbe` échoue sur 6V1A9933 avec les réglages
  historiques codés dans la sonde (EV −0,49, contraste 41, plusieurs
  curseurs à −100) : MAE RGB caméra 25,1519, seuil 25 ; MAE luminance
  24,9015. Ce RAW n'appartient pas au manifeste calibré, et la variable
  d'activation n'était pas définie. Cet échec est donc hors du mode
  calibré ; faute d'exécution témoin avant cette session, son antériorité
  n'est pas certifiée. Le seuil n'a pas été relevé et le test n'a pas
  été retiré. Une investigation séparée reste nécessaire.
- **46/46 tests Python d'analyse et 11/11 de sélection réussis.**
  [Synthèse des exécutions](../raw/validation-integration-normalisation-20261008.txt).
- Tests synthétiques ajoutés : résolution et refus du manifeste,
  invalidation du cache, permutations RGB, gris/noir/blanc, exposition
  avant tables, exposition invalide sans mutation et repli anti-inversion.
- Outils de préparation/diagnostic et nouveaux composants cartographiés
  dans les tests ciblés. L'ajout CMake conserve le déclenchement complet
  prévu par les règles du projet.

La généralisation aux autres fichiers/boîtiers/ISO, l'activation par défaut,
la courbe implicite Standard, les résidus de 9421, les interactions LCP,
la comparaison JPEG 9933, la validation visuelle et la fidélité physique
restent à traiter. **Le
build contient l'intégration contrôlée ; il ne prétend pas avoir activé
une nouvelle calibration pour toutes les images.**

### 24.7. Orientation demandée : préparer le remplacement de LibRaw si nécessaire

**Orientation utilisateur consignée le 8 octobre 2026 :** si LibRaw
continue de poser des difficultés de compatibilité, de maintenance ou
si des problèmes de stabilité sont confirmés, prévoir de s'en passer
en préparant un comportement de décodage remplaçable. Il ne s'agit pas
d'une décision de suppression immédiate ni d'une demande d'implémentation
dans cette étape documentaire.

Les anomalies de métadonnées Canon et les différences entre versions
observées dans ce rapport justifient de réduire le couplage à LibRaw.
En revanche, elles ne démontrent pas à elles seules une instabilité
générale : aucun crash imputable à LibRaw n'est établi ici, et l'échec
JPEG 9933 n'est pas attribué à cette bibliothèque.

La préparation devra séparer explicitement :

1. **Le conteneur et les métadonnées RAW** : lecture bornée des structures,
   mosaïque, géométrie/CFA, orientation, noirs/blancs, WB et provenance.
2. **Le développement de la mosaïque** : normalisation, pixels défectueux,
   reconstruction des hautes lumières et dématriçage. Définir quelles
   étapes relèvent du backend et lesquelles appartiennent au moteur IPR.
3. **Le rendu IPR indépendant** : profils DCP/LCP, exposition de base,
   espaces couleur, réglages et export, sans gain implicite ni dépendance
   à des structures LibRaw dans les interfaces publiques.

Le registre `RawDecoder::registerFormatHandler()` constitue un point
d'extension existant, mais ne suffit pas à garantir l'interchangeabilité
des étapes internes. Prévoir un contrat de backend documenté avec unités,
précision, gestion des erreurs et responsabilités explicites, puis un
adaptateur LibRaw et un backend alternatif ou une implémentation interne.
Le choix de ce remplacement reste à étudier ; il n'est pas arrêté ici.

Avant toute bascule, comparer les deux chemins sur le même corpus :
intégrité des pixels décodés, métadonnées, noirs/blancs, WB, rendu couleur,
clipping, mémoire, temps de chargement et comportement sur fichiers
tronqués ou malformés. Vérifier également couverture CR2/CR3/DNG,
licences, coût et maintenance multi-OS. Conserver un retour arrière
pendant la transition. Un décodeur interne doit être évalué comme un
chantier distinct, pas comme un simple remplacement de dépendance.

**Action future :** formaliser cette séparation et les critères de
remplacement dans une décision d'architecture après investigation ;
aucun remplacement ni nouveau backend n'a été réalisé à ce stade.

## 25. Module de développement RAW dans l'IHM — 8 octobre 2026

Le choix du profil et l'activation contrôlée sont maintenant accessibles
dans **Réglages → Développement RAW / Profil**, sous l'histogramme.
La procédure en variables d'environnement du §24.5 décrit l'intégration
antérieure ; pour l'IHM actuelle, utiliser ce panneau. La normalisation
reste désactivée au démarrage, même si une variable d'environnement existe.

### Procédure pour voir la correction

1. Lancer le nouveau `build-windows-msvc/Release/appIPR.exe`.
2. Ouvrir le RAW `6V1A9421.CR3` du corpus, pas un TIFF déjà développé.
3. Dans le nouveau panneau, choisir **Canon EOS R6 Mark III Camera Neutral**.
4. Laisser **Balance des blancs automatique** décochée et remettre les
   réglages photographiques à zéro pour comparer la base.
5. Cocher **Normalisation RAW — expérimentale**. Si aucun manifeste n'est
   trouvé, choisir `build-canon-normalization-20261008/normalization.json`.
6. Attendre la fin du décodage. Sur 9421, le statut doit afficher
   **Normalisation appliquée — blanc 14351, base 0.28 EV**, avec la provenance.
7. Décocher puis recocher pour comparer sans/avec la correction, au même DCP.

La sélection DCP conserve la sélection LCP automatique. Profil, activation
et chemin du manifeste sont conservés uniquement pour la session, sans
migration des sidecars. Le bouton de réinitialisation ne désactive pas le
profil ou la calibration. Pour revenir entièrement au comportement
automatique, choisir le profil automatique et décocher la normalisation.

Le manifeste couvre seulement **6V1A0007, 6V1A0043, 6V1A0327, 6V1A9421 et
6V1A9950**, leurs DCP exacts et LibRaw 0.22.2-Release. Toute autre combinaison
conserve le chemin non calibré et affiche un statut de non-application.
Cela ne constitue toujours pas une validation globale de la colorimétrie.

### Intégration et vérification

- Nouveau composant `src/qml/RawDevelopmentModule.qml`, déclaré dans CMake,
  avec section repliable et largeur contrainte pour éviter le défilement horizontal.
- Découverte des profils compatibles hors du thread IHM ; liste conservée
  en cache pour la session. Redémarrer après ajout de nouveaux profils.
- Recharge asynchrone et clés de cache distinctes selon le profil et le
  manifeste ; désactivation explicite de l'activation implicite par environnement.
- Test synthétique `editorRawDevelopmentTests` : choix du profil, activation,
  retour arrière, invalidation du cache après modification du manifeste et
  refus d'un choix absent de la liste. Tests QML du panneau et de sa largeur.
- Vérification Windows réelle sur une **copie temporaire de 9421** : le rendu
  normalisé diffère du témoin Neutral, et la désactivation restitue exactement
  l'empreinte précédente. Aucun sidecar du RAW d'origine n'a été modifié.
  [Preuve et empreintes](../raw/verification-module-raw-ihm-20261008.json).

Ce test exerce les commandes et le rendu du moteur utilisés par le panneau ;
il ne remplace pas une validation visuelle manuelle de l'IHM.
Les preuves du §24 restent historiques et ne sont pas réécrites.

Validation de cette étape : build Windows Release réussi ; cinq suites
natives Windows RAW/profils/export/éditeur et le test QML des réglages
réussis. Le test QML Windows nécessite `QT_PLUGIN_PATH` vers les plugins
du kit Qt et `QML_IMPORT_PATH` vers son dossier `qml` : le déploiement local
ne contenait pas le plugin `offscreen`. Sans ces chemins, l'exécution a été
interrompue après blocage à l'initialisation Qt, puis relancée avec succès.
Sous WSL, **17/18 suites réussies** : seul le même test
`tonalReferenceComparisonProbe` échoue sur 9933 (MAE RGB 25,1519, seuil 25 ;
voir §24.6). Les **76 tests Python d'analyse** et **12 de sélection des tests**
réussissent. La suite complète n'est donc pas déclarée entièrement verte.

## 26. Derniers exports et acceptation visuelle Camera Neutral — 8 octobre 2026

**Retour utilisateur :** le rendu à l'ouverture avec Camera Neutral convient
visuellement ; le profil Adobe Standard ne convient pas. Cette acceptation
porte sur l'usage visuel, pas sur une parité pixel à pixel avec Lightroom,
une calibration physique ou tous les profils du boîtier.

Les nouveaux exports nommés `-IPR-CameraNeutral-AsShot.tiff` et
`-IPR-AdobeStandard-AsShot.tiff` ont été comparés aux TIFF LR de même intitulé.
Les onze paires disponibles sont en sRGB 16 bits ; les références LR déclarent
As Shot, le profil attendu et EV 0. Les empreintes et paramètres sont conservés
dans [la mesure finale](../raw/comparaison-exports-profils-20261008.json).
L'analyse emploie la grille au pas 8, CIEDE2000 et le recadrage 0/1 pixel
déjà documentés ; elle ne corrige pas les différences de géométrie optique.

| RAW | ΔE00 moyen Neutral | MAE RGB Neutral /255 | ΔE00 moyen Adobe Standard |
| --- | ---: | ---: | ---: |
| 0007 | 5,533 | 12,224 | 17,865 |
| 0043 | 2,557 | 4,625 | 4,410 |
| 0327 | 6,113 | 13,729 | 10,879 |
| 9421 | 9,320 | 18,218 | 18,142 |
| 9950 | 7,391 | 17,462 | 9,416 |

Neutral est plus proche de LR que Standard sur les cinq paires comparables.
Les différences résiduelles ne permettent pas de déclarer une équivalence
colorimétrique. La luminance moyenne LR moins IPR exprimée en rapport EV
est de −0,006 ; +0,009 ; −0,067 ; −0,067 ; −0,463 respectivement : 9950 reste
sensiblement plus lumineux dans cet export IPR. Les TIFF IPR ne contiennent
pas un relevé complet de la sélection DCP et de l'activation du manifeste ;
les intitulés correspondent à la déclaration utilisateur, sans prouver à eux
seuls l'état exact de la calibration à l'export. Ne pas ajuster le moteur
sur ces écarts sans vérifier cet état et harmoniser l'optique.

0037 Standard donne ΔE00 10,232 ; aucun TIFF
`6V1A0037-LR-CameraNeutral-AsShot.tif` n'est présent. Le TIFF `6V1A0037-2.tif`
n'a pas été assimilé automatiquement à cette référence : il reste à identifier
si une comparaison Neutral de cette scène est souhaitée. 0037 n'appartient
de toute façon pas au manifeste calibré.

**Conclusion :** Camera Neutral est accepté comme rendu de travail visuel
par l'utilisateur. Adobe Standard et les résidus identifiés restent ouverts,
sans correctif empirique supplémentaire dans cette étape. Une version locale
Windows autonome est préparée avec les dépendances, les profils locaux et la
configuration ; elle ne constitue pas une version publiable avec les profils
Adobe. Voir la [procédure de packaging](../../architecture/build-windows-standalone.md).

### Build local autonome

Livrable : `dist/IPR-Windows-x64-20261008.zip`, à extraire entièrement.
Le dossier contient `appIPR.exe`, Qt/QML et plugins, OpenCV, LibRaw, TIFF,
les runtimes, `profiles/`, `config/`, les notices et l'inventaire SHA-256.
Cette copie privée inclut le DCP Camera Neutral du R6 Mark III et les LCP
Canon locaux ; ne pas publier cette archive avec les profils tiers.
Le script `scripts/package-windows.ps1` n'inclut les profils Adobe qu'avec
l'option explicite `-IncludeLocalAdobeProfiles`.

Adaptations portables : recherche des profils à côté de l'exécutable,
détection du manifeste `config/raw-normalization.json`, stockage IHM local
si `config/ipr-ui.ini` est présent. La normalisation reste désactivée par
défaut et n'est pas généralisée aux autres RAW.

Vérifications : build Release réussi ; cinq suites ciblées Windows
(réglages, préférences et QML, profils, développement RAW) réussies ;
76 tests Python d'analyse et 12 de sélection réussis. Suite complète WSL
17/18, même échec JPEG 9933 et mêmes métriques qu'au §25.
`scripts/smoke-windows-standalone.ps1` a démarré la copie empaquetée pendant
huit secondes en mode offscreen depuis le dossier temporaire, avec PATH
limité aux dossiers système Windows et sans variables Qt/QML de développement.
Aucune erreur QML ni bibliothèque Qt/OpenCV/RAW/TIFF issue d'un répertoire
externe n'a été détectée. Ce test vérifie le démarrage autonome sur la machine
actuelle, pas le rendu GPU ni une certification sur des Windows 10/11 propres.

## 27. Pistes externes — PhotoCraft et LightCraft (9 octobre 2026)

**Origine :** dépôt [storytold/photocraft](https://github.com/storytold/photocraft/) proposé par l'utilisateur comme point d'appui potentiel. Examen préliminaire des sources publiées sur `main`, sans compilation ni campagne comparative. Figer un commit avant toute expérience reproductible : les liens ci-dessous suivent une branche évolutive.

### Périmètre et intérêt pour IPR

PhotoCraft se présente comme une réimplémentation indépendante de **Photoshop**, en Rust, et non de Lightroom. Son README décrit des réglages non destructifs, un compositeur CPU de référence comparé au GPU, des tuiles copy-on-write, la gestion ICC et une interface de commandes commune à l'IHM et au CLI. Il signale aussi un statut alpha : une commande présente ne garantit pas la parité de comportement. Ces déclarations constituent des pistes à vérifier, pas des résultats de tests IPR. [Source : README](https://github.com/storytold/photocraft/blob/main/README.md).

Le manifeste distingue notamment `color`, `cms`, `raw`, `raster`, `compose`, `gpu`, `algo` et `testkit`. Cette séparation est intéressante pour étudier les contrats du moteur, les tests synthétiques et une future accélération GPU, sans décider d'intégrer Rust ni de remplacer Qt/OpenCV. [Source : Cargo.toml](https://github.com/storytold/photocraft/blob/main/Cargo.toml).

### Point concret : le module RAW

Le module expose `identify`, `decode` vers les données capteur, puis `develop`/`develop_sensor`. Sa documentation décrit les niveaux noir/blanc, la WB, le dématriçage et la conversion couleur. Il annonce une sortie **ProPhoto RGB, blanc D50, gamma 1,8** : elle n'est pas directement comparable au sRGB linéaire flottant d'IPR. Toute comparaison doit harmoniser espace, encodage, WB, profil, tonalité et cadrage.

**Limite déterminante pour notre corpus :** `identify` reconnaît les CR3, mais `decode` retourne explicitement `Unsupported` pour leur décodage ISO BMFF/CRX. Reconnaissance du format et extraction d'un aperçu ne prouvent pas le développement du capteur. Ce module ne constitue donc pas actuellement un remplacement du backend LibRaw pour nos CR3. Les chemins CR2/DNG restent des pistes de diagnostic indépendant. [Source : crates/raw/src/lib.rs](https://github.com/storytold/photocraft/blob/main/crates/raw/src/lib.rs).

### LightCraft : analyse prioritaire, sans utilisation ni intégration

À la demande de l'utilisateur, l'objectif est de comprendre les choix techniques, **pas d'utiliser LightCraft, de reprendre son code ou de changer le backend IPR**. Lecture des sources publiques le 9 octobre 2026 ; les vérifications ciblées sont figées au commit [`c435d143de921e8dc724245065500bc191f91102`](https://github.com/storytold/lightcraft/commit/c435d143de921e8dc724245065500bc191f91102). Aucun binaire compilé, aucun test externe exécuté, aucune comparaison sur nos photos. Les résultats publiés par les auteurs restent leurs résultats, pas une validation indépendante.

#### Décodage : séparer conteneur, capteur et développement

Le contrat distingue reconnaissance du format, lecture des en-têtes sans décompression, décodage des échantillons et développement. `RawImage` conserve CFA, géométrie, orientation, niveaux noir/blanc, WB et données couleur. La normalisation suit le principe `(échantillon − noir)/(blanc − noir)` ; elle ne constitue pas une correction colorimétrique. Cette séparation permet de diagnostiquer les pixels avant d'attribuer un problème aux profils. [Contrat RAW](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/raw/src/lib.rs).

Contrairement à **PhotoCraft**, **LightCraft décode réellement certains CR3** avec un code Rust indépendant : lecture des pistes ISO BMFF, des descripteurs CMP1, des zones CDI1/IAD1 et des métadonnées CTMD. Il sélectionne la piste principale, pas le delta Dual Pixel, et ignore les versions de métadonnées inconnues plutôt que d'inventer une WB. [Adaptateur Canon](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/raw/src/vendor/cr3.rs).

Le décompresseur CRX s'appuie sur prédiction MED, codage Rice adaptatif et longueurs de plages ; le C-RAW ajoute quantification et reconstruction de sous-bandes. Les auteurs citent descriptions publiques et brevets Canon, et déclarent ne pas avoir consulté de décodeur tiers. Certaines familles de marqueurs et variantes restent explicitement non prises en charge. [Décompresseur CRX](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/raw/src/vendor/crx.rs).

La documentation rapporte six comparaisons du capteur sur M50, R100 et R8 contre un oracle externe, **pas sur notre R6 Mark III**. En cas d'échec CR3, le chargement peut se rabattre sur le JPEG intégré avec avertissement : voir une image à l'écran ne prouve donc pas que les données RAW ont été développées. [Périmètre CR3 et preuves déclarées](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/docs/cr3.md).

Pour le dématriçage Bayer, AHD est le choix qualité par défaut : sélection de candidats horizontaux/verticaux selon leur homogénéité en CIELab. PPG et bilinéaire sont également disponibles ; les autres CFA disposent d'une interpolation directionnelle. Des tests synthétiques vérifient couleurs constantes, conservation des échantillons connus et PSNR. Ce sont des invariants utiles, pas une preuve de supériorité visuelle. [Dématriçage](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/raw/src/demosaic/mod.rs).

#### Profils : quatre notions différentes

| Couche | Fonction dans LightCraft | Point d'attention pour IPR |
|---|---|---|
| Modèle couleur DNG | Matrices capteur, WB, adaptation chromatique | Correction de caractérisation, pas style créatif |
| Look embarqué DNG | Tables HSV et courbe du fichier | Respecter espace et ordre des opérations |
| Profil boîtier JSON | Matrice et table apprises du JPEG boîtier | Approximation d'un rendu, pas mesure spectrale |
| Look « Neutral », « Vivid », etc. | Décalages internes de réglages | Pas un DCP Canon Camera Neutral |

Le modèle DNG interpole les matrices à deux illuminants selon l'inverse de la température : `g = (1/T − 1/T2)/(1/T1 − 1/T2)`, borné entre 0 et 1. Il combine AnalogBalance, CameraCalibration et ColorMatrix, utilise ForwardMatrix lorsqu'elle est exploitable, sinon inversion et adaptation Bradford. La destination de travail est **Rec.2020 D65 linéaire**, différente du sRGB linéaire IPR. Sans matrice, le module signale un repli générique « caméra ≈ sRGB », qui n'est pas une caractérisation du capteur. [Modèle couleur](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/raw/src/color.rs).

Les tables embarquées sont appliquées en HSV de **ProPhoto linéaire D50**, avec interpolation trilinéaire et traitement de l'encodage de l'axe valeur. HueSatMap, LookTable et courbe ont des rôles et positions distincts ; les gain maps sont conservées mais non rendues dans ce chemin. Ce lecteur de données intégrées au DNG ne prouve pas à lui seul une gestion générale des fichiers `.dcp` Adobe externes. [Tables de profil](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/raw/src/profile.rs).

Pour plusieurs RAW propriétaires sans matrice, dont Canon CR3, le moteur ajuste les pixels capteur au **JPEG intégré au même fichier**. Il sépare couleur et tonalité : matrice régularisée de chromaticité, correction HSV des résidus, courbe tonale monotone et correction de chroma. Une partie des correspondances est réservée à la vérification ; un ajustement insuffisant est rejeté. Le JPEG sert de référence d'apprentissage, pas de remplacement des pixels RAW. [Implémentation de l'ajustement](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/engine/src/camera_preview.rs).

Les fichiers JSON permettent de mutualiser les observations de plusieurs photos d'un modèle ; ils sont validés puis mis en cache par processus. Au commit examiné, les profils embarqués concernent ILCE-7M4, X-H2S et X-T4 : **pas notre Canon**. [Profils boîtier](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/engine/src/camera_profiles.rs).

**Interprétation :** reproduire le JPEG peut rapprocher du Picture Style choisi, mais aussi apprendre ses courbes, son éclairage local et ses traitements. Cela ne remplace pas une mire caractérisée. La documentation distingue explicitement WB relative autour du rendu As Shot et Kelvin physiques non établis sans calibration ; cette distinction est pertinente pour notre investigation. [Analyse des auteurs](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/docs/camera-preview-colour.md).

Les looks intégrés sont des deltas de contraste, saturation, courbes et grading composés avec les réglages utilisateur. Par exemple `lc.neutral` réduit notamment contraste et saturation ; ce nom ne désigne donc pas le profil Canon que nous avons validé visuellement. [Looks internes](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/pipeline/src/profiles.rs).

L'**ICC** est une autre couche : décodage matrice/TRC vers XYZ D50, conversion CMS `moxcms` pour profils LUT/CMYK, génération de profils d'export. Cela gère l'interprétation et la restitution des RGB, pas le style Canon. [Gestion ICC](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/codecs/src/icc.rs).

Pour l'optique, le chemin examiné utilise corrections intégrées DNG ou données constructeur et réglages manuels, pas une collection Adobe LCP. Les coordonnées et rayons normalisés permettent une correction cohérente entre aperçu et export. [Optique](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/pipeline/src/optics.rs).

#### Débruitage manuel : filtrage guidé et chromaticité

La luminance est filtrée dans le domaine logarithmique par filtre autoguidé : localement `a = variance/(variance + ε)`, `b = moyenne·(1 − a)`, puis `q = moyenne(a)·p + moyenne(b)`. La correction RGB est un gain `2^(k·(q − p))`. Les contours restent davantage protégés qu'avec un flou uniforme ; les textures faibles peuvent néanmoins être lissées.

Le bruit couleur est traité en lissant la chromaticité `RGB/Y`, puis en réappliquant la luminance. Rayon et mélange dépendent des curseurs et du rapport de résolution aperçu/source. Les constantes de rayon, epsilon et intensité sont des **choix de réglage empiriques**, pas un modèle mesuré Poisson-Gauss du capteur. Il faut contrôler bavures chromatiques et détails à 100 %. [Code du débruitage manuel](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/pipeline/src/local.rs).

L'import RAW initialise netteté à 40 et réduction du bruit couleur à 25. Un rendu sans action utilisateur n'est donc pas nécessairement un rendu sans traitement de détails ; c'est important pour les comparaisons neutres. [Valeurs initiales RAW](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/develop/src/lib.rs).

#### Débruitage IA : Bayer vers RGB, avant interprétation couleur

Le contrat du modèle prend quatre plans Bayer normalisés **avant WB** et retourne trois plans RGB caméra de résolution double par axe. Le code adapte les quatre phases Bayer, reflète les bords, traite des tuiles chevauchantes, remet leur échelle en cohérence et protège les échantillons saturés. Il s'agit donc aussi d'une reconstruction RGB, pas simplement d'un filtre sur le JPEG rendu. [Disposition Bayer](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/denoise-core/src/bayer.rs), [traitement des tuiles](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/denoise-core/src/run.rs).

L'exécuteur CPU traduit les convolutions en petits blocs im2col et produits matriciels GEMM `faer`, réutilise les buffers selon leur durée de vie et laisse le parallélisme aux tuiles. Ce n'est pas un runtime ONNX universel : seul un sous-ensemble contrôlé d'opérateurs est pris en charge. [Exécuteur CPU](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/crates/denoise/src/cpu.rs).

Le parcours IA est optionnel, sans poids embarqués ; l'offre RawNIND dépend d'une option de compilation et d'une politique de distribution séparée. Le produit nettoyé est un cache recréable, et le curseur mélange rendu ordinaire et nettoyé. La documentation décrit exécution GPU WGSL/wgpu, contrôle CPU/GPU et repli, mais limite le contrat à Bayer RAW. Les temps publiés sont historiques, excluent plusieurs coûts, et ne prédisent pas les performances d'IPR. Aucune parité de qualité Lightroom n'est revendiquée. [Documentation IA et limites](https://github.com/storytold/lightcraft/blob/c435d143de921e8dc724245065500bc191f91102/docs/denoise.md).

#### Conclusion utile pour nos investigations

Les points d'appui sont **méthodologiques** : vérifier les échantillons capteur avant la couleur ; conserver pixels et métadonnées ensemble ; distinguer WB, caractérisation, style et ICC ; tester les replis explicitement ; séparer débruitage manuel et IA ; vérifier aperçu/export et CPU/GPU.

Le rapprochement avec le JPEG boîtier est particulièrement pertinent comme **diagnostic** de notre rendu Canon Neutral, mais ce JPEG n'est ni une vérité colorimétrique physique ni une référence Lightroom. Rien dans cette lecture ne justifie de remplacer LibRaw ou de modifier notre rendu déjà accepté. Aucun code de production changé, aucun résultat IPR nouveau, aucune utilisation ou intégration de LightCraft prévue.

## 28. Fleur orange 6V1A0081 — comparaison neutre du 14 octobre 2026

### Contexte et preuve

L'utilisateur décrit une fleur orange vive, assombrie par la prise de vue,
et observe un rendu IPR plus rouge. L'examen des aperçus dérivés confirme
visuellement cette direction : pétales rouge-orangé plus sombres dans IPR,
orange plus jaune et plus lumineux dans LR. Ce constat n'est pas une
mesure physique de la fleur ni une déclaration que LR est la vérité.

Mémoire canonique : [inspection neutre v001, 08:44:58 UTC / 10:44:58 Paris](../raw/inspection-neutre-couleur-v001-20261014T084458701161Z.json).
Les deux inspections antérieures du même jour restent préliminaires
(vue globale puis ROI, avant ajout complet du contexte déclaratif utilisateur).
Outil : [inspection indépendante de deux neutres](../../../tests/inspect_neutral_colour_pair.py),
empreinte stockée dans le JSON ; trois tests mathématiques synthétiques réussis.

Entrées, empreintes et réglages :

- `tests/image/base/neutre/6V1A0081.tif` : LR, sRGB 16 bits, 4640 × 6960,
  Camera Neutral, As Shot **5200 K / +5**, exposition et corrections
  photographiques nulles dans le XMP ; correction objectif/CA déclarées à zéro.
- `tests/image/base/neutre/6V1A0081-IPR.tiff` : IPR, sRGB 16 bits RGBA opaque,
  4639 × 6959, aucune déclaration embarquée des réglages ou profils.
  L'utilisateur indique **4924 K, autres réglages à zéro**, dont nuance 0.
  Le profil exact et la normalisation ne sont pas attestés dans ce TIFF.
  Les valeurs utilisateur sont distinctes des métadonnées vérifiées dans la mémoire.

Les valeurs Kelvin/nuance affichées ne sont pas identiques ; elles ne
démontrent cependant pas à elles seules des gains caméra physiquement
différents, ni la cause de l'écart. L'optique IPR n'est pas attestée non plus.

### Mesures de couleur et luminosité

Lecture native sans conversion 8 bits, Y linéaire sRGB et CIELAB D65.
ROI centrale choisie visuellement : `(x=0,25 ; y=0,48 ; largeur=0,50 ; hauteur=0,28)`
des dimensions de chaque image, grille native de pas 4, **282 460 points par
moteur**. Mesures globales au pas 8. Pas de recalage ni comparaison pixel
à pixel ; la différence de géométrie et les éventuelles corrections optiques
limitent l'interprétation. Les statistiques du rectangle entier sont
privilégiées ici ; le masque chaud indépendant reste un diagnostic secondaire.

| Mesure, rectangle central | IPR | LR |
| --- | ---: | ---: |
| Luminance linéaire moyenne Y | 0,03070 | 0,05988 |
| Clarté moyenne L* (D65) | 17,05 | 25,57 |
| Angle moyen de teinte CIELAB, chromas > 5 | 36,12° | 49,60° |
| Angle moyen HSV en RGB linéaire, saturation > 0,1 | 5,90° | 11,08° |
| Rapport des moyennes G/R en RGB linéaire | 0,0799 | 0,1544 |

IPR a **48,73 % de Y moyen en moins** sur cette ROI (rapport équivalent
−0,964 EV), et **45,80 % en moins** globalement. Ce rapport n'est pas une
consigne de compensation uniforme : il ne décrit pas toute la courbe tonale.
L'angle CIELAB IPR est plus proche de l'axe rouge, LR davantage vers le jaune.
Les angles HSV linéaires et le rapport G/R vont dans le même sens. Les angles
de différents espaces ne sont pas des unités perceptuelles interchangeables.

Un simple gain d'exposition linéaire conserve les rapports RGB et la teinte
HSV linéaire, en l'absence d'écrêtage. L'écart observé comporte donc une
**composante chromatique**, pas seulement une image uniformément plus sombre.
Les distributions indépendantes ne permettent pas de prouver la dérive de
chaque pétale ni de calculer une erreur perceptuelle appariée.

### Conclusion et investigation à poursuivre

Le retour utilisateur est confirmé sur ces exports : **plus sombre et plus
rouge dans IPR**. Ce n'est pas une validation d'un choix de rendu rouge pour
une fleur orange. Il faut contrôler le contexte du neutre avant de modifier
un réglage global : profil DCP exact, gains As Shot, normalisation et optique.
Si vibrance/saturation valent bien zéro comme déclaré, le nouveau bloc A1/A2
est une identité et **ne peut pas expliquer cet écart neutre** à lui seul.

Suite recommandée : reproduire un neutre IPR avec contexte attesté, puis
séparer par ablations balance des blancs, traitement DCP et courbe/gamut de
sortie. Éviter une correction arbitraire du canal rouge ou de la vibrance sur
la seule base de cette scène. Aucun code de production modifié ni image source
supprimée dans cette inspection.

## 29. Reprise exhaustive du corpus d'ouverture — 14 octobre 2026

### État Git et périmètre

À la demande de l'utilisateur, l'état A1/A2 et ses preuves sont sauvegardés
dans le commit local **`8abcd12` de `patch_color`**. La validation
photographique Couleur est suspendue, sans tag ni fusion dans `dev`.
La branche locale **`patch_raw_opening`** est créée depuis `dev` actualisé,
commit `7297197`. Elle reprend l'outil et les preuves du diagnostic neutre
0081, pas le code du prototype Couleur. Aucun push demandé ou exécuté.

Le rendu neutre est une identité dans le bloc de réglages, indépendamment
du changement de modèle Couleur. Le programme de diagnostic compile
uniquement une sonde et des tests ; l'exécutable IHM A1/A2 précédemment
construit n'est pas une nouvelle livraison du rendu d'ouverture.

### Inspection de la carte et ajouts du corpus

Autorisation de lecture/copie donnée pour `D:/DCIM/100EOSR6` : **500 CR3**
inventoriés, **60 JPEG intégrés** répartis dans la série examinés visuellement.
Les métadonnées de ces 500 fichiers indiquent R6 Mark III. Les JPEG servent
à sélectionner les sujets, jamais à mesurer la fidélité du développement RAW.
Quatre copies nouvelles, sans XMP/ACR associé, sont déposées à la racine de
`tests/image/base`, environ **157 MiB** au total, SHA-256 source/destination
identiques. Aucun fichier existant écrasé, déplacé ou supprimé.

| RAW ajouté | ISO lu | Intérêt observé |
| --- | ---: | --- |
| 6V1A0318 | 100 | Peau au soleil, rocher et crashpads colorés : carnations et tons chauds à faible bruit. |
| 6V1A0524 | 400 | Contre-jour, ciel et rochers sombres : séparation luminosité/gamut et réserve tonale. |
| 6V1A9608 | 16000 | Prise bleue sur fond jaune : interactions de couleurs saturées ; bruit à contrôler. |
| 6V1A9616 | 1600 | Prise rouge sur mur clair : zone chaude et fond peu chromatique. |

Le corpus contient désormais **19 RAW** : seize R6 Mark III (0007, 0037,
0043, 0054, 0081, 0182, 0318, 0327, 0524, 0830, 9421, 9608, 9616, 9890,
9940, 9950) et trois R7 (7Y6A7003, 7Y6A7298, 7Y6A7599). Deux objectifs
rapportés par le décodeur : RF24-105mm F2.8 L IS USM Z et Sigma 70-200mm
F2.8 DG OS HSM Sports 018. La couverture n'est donc pas universelle.

### Preuves et contrôles

- [Audit du corpus, 09:27:42 UTC / 11:27:42 Paris](../raw/audit-corpus-ouverture-v001-20261014T092742619468Z.json) :
  19 sources RAW, sidecars présents mais non appliqués, **37 TIFF neutres**
  inspectés, contextes/empreintes/statistiques et preuves des quatre copies.
- [Synthèse canonique v002, 09:39:38 UTC / 11:39:38 Paris](../raw/synthese-ouverture-v002-20261014T093938429634Z.json) :
  références aux **19 JSON individuels** de la sonde, extraction indépendante
  des candidats ColorData Canon, comparaisons LR, exclusions, unités et décision.
  Les synthèses v001 restent préliminaires ; v002 ajoute les garde-fous,
  le protocole et la provenance de branche sans modifier leurs données.
- Outils : [sonde native](../../../tests/raw_opening_probe.cpp),
  [audit](../../../tests/audit_opening_corpus.py),
  [synthèse](../../../tests/summarize_raw_opening.py).

**19/19 décodages réussis** avec les profils Camera Neutral appropriés aux
deux boîtiers. WB caméra, normalisation désactivée, optique/CA désactivées,
aucune restauration de retouche. Échantillonnage natif au pas 8, sans
redimensionnement. Sur le RGB caméra échantillonné, le rejeu du DCP reproduit
le tampon issu du décodage complet avec **erreur flottante maximale nulle
sur les 19 scènes**. Les ablations sont donc isolées sur un témoin exact.
La sortie sRGB 16 bits est celle de production. Les PNG d'inspection restent
dans un dossier de build et ne servent pas aux mesures.

Tests : **4/4 CTest ciblés** (sonde, décodeur, profils, sortie couleur),
**5 tests Python de synthèse**, **3 d'inspection couleur**, **15 de sélection
de tests** réussis. Ce ne sont pas des validations photographiques.

La ROI nommée `flower_ROI` dans la sonde est le rectangle proportionnel
défini pour 0081, appliqué aux autres images comme simple région exploratoire.
Elle ne constitue pas une annotation de peau, de fleur ou de prise sur ces
autres scènes ; aucune conclusion sémantique automatique n'en est tirée.

### Luminosité : résidu confirmé, pas de compensation globale proposée

Huit scènes disposent d'une référence LR Camera Neutral neutre sans
correction optique déclarée. Pour 9421, ce fichier indique **Custom**,
5200/+5 ; son autre TIFF As Shot a l'optique activée. Le premier reste donc
une comparaison diagnostique, pas une validation As Shot. Le TIFF générique
9950 Adobe Standard est exclu ; la référence Camera Neutral est retenue.
Les fichiers IPR historiques de diverses générations ne sont pas mélangés
au témoin fraîchement décodé.

Variation de la luminance moyenne du nouveau témoin par rapport au TIFF LR :

| Scène | Témoin actuel sans optique | Courbe DCP préservant la teinte | IPR réglé à 5200/+5 |
| --- | ---: | ---: | ---: |
| 0007 | −23,31 % | −23,41 % | −22,68 % |
| 0037 | −30,83 % | −30,94 % | −30,21 % |
| 0043 | −38,35 % | −37,94 % | −37,41 % |
| 0081 | −47,60 % | −42,93 % | −45,14 % |
| 0327 | −35,63 % | −35,24 % | −34,49 % |
| 9421 (WB Custom) | −31,83 % | −31,99 % | −30,43 % |
| 9890 | −34,84 % | −34,64 % | −33,47 % |
| 9950 | −31,03 % | −31,32 % | −30,50 % |

Moyenne des écarts relatifs par scène : **−34,18 %**, médiane −33,34 %.
Ce n'est pas un facteur d'exposition à appliquer ni un score perceptuel.
La nouvelle optique désactivée diffère du contexte Couleur du 13 octobre,
où IPR appliquait géométrie/vignettage : ne pas comparer directement ces
moyennes comme une régression. Les gains de vignettage peuvent expliquer
une partie des variations historiques, notamment 0037/9950 ; cette attribution
n'est pas une nouvelle ablation optique mesurée ici. Les corrections imposées
par LR et la géométrie restent à vérifier. Aucun recalage ou MAE inter-moteurs.

### Teinte : piste DCP isolée sur la fleur orange

La production applique actuellement la courbe DCP **canal par canal** lorsque
la normalisation expérimentale est désactivée. L'option existante
`preserveToneCurveHue` interpole le canal intermédiaire entre les extrema
transformés en ProPhoto et conserve la teinte HSV de cet espace lorsque
les conditions de validité sont réunies. Elle n'est pas une conservation
garantie de L/h Oklab. Dans `RawDecoder.cpp`, elle est aujourd'hui activée
avec une calibration de normalisation, pas par défaut pour un neutre.

La sonde ne change que cette option, sans normaliser le RAW, changer ses gains
WB ou modifier le DCP. Sur la ROI centrale de 0081 :

| Indicateur | Témoin actuel | Courbe préservant la teinte | Référence LR, inspection §28 |
| --- | ---: | ---: | ---: |
| Rapport des moyennes linéaires G/R | 0,0799 | 0,1386 | 0,1544 |
| Y moyen | 0,03061 | 0,03415 | 0,05988 |

Les grilles/géométries diffèrent légèrement de l'inspection TIFF du §28 :
ces statistiques ne sont pas des correspondances pixel à pixel. L'aperçu
de l'ablation est visuellement plus orange, cohérent avec l'augmentation
relative du vert. La modification de courbe est donc une **cause contributive
isolée du rouge**, mais elle ne suffit pas à corriger la luminosité globale.
Son effet sur Y moyen varie de **−1,20 % à +8,91 %** selon les 19 scènes,
médiane −0,079 %. La variation de taux de clipping va de −0,001 à +0,169
point de pourcentage : aucune innocuité universelle n'est déclarée.

Recopier les nombres LR 5200/+5 dans IPR augmente légèrement Y mais fait
baisser le G/R de la fleur à **0,0698** : la teinte ne se rapproche pas ici
de l'orange LR. Les Kelvin/nuances de deux moteurs ne sont pas des gains
caméra interchangeables. Les Kelvin estimés sont 4924 pour les seize R6,
5314 pour les trois R7. Les candidats ColorData sont constants par modèle
dans ce corpus et cohérents numériquement avec les rapports de gains lus ;
les offsets ne sont pas tous certifiés. Ce constat **ne démontre pas** que
la constance provient d'un défaut de WB plutôt que des réglages de prise de vue.

### Ce qui reste nécessaire

**Onze scènes n'ont pas encore de référence LR neutre exploitable dans
`base/neutre`** : 0054, 0182, 0318, 0524, 0830, 9608, 9616, 9940,
7Y6A7003, 7Y6A7298, 7Y6A7599. Elles sont décodées et mesurées dans IPR,
mais aucune fidélité LR n'est annoncée. Les préparer selon le
[protocole RAW/profils v2](../protocols/protocole-raw-profils.md) : Camera Neutral,
As Shot sans imposer une température universelle, sRGB 16 bits, paramètres
photographiques à zéro, optique/bruit/netteté contrôlés. Refaire également
9421 As Shot sans optique si LR permet ce contexte ; sinon tracer le contexte imposé.

Prochaine investigation recommandée : séparer **préservation de teinte de la
courbe DCP** et **niveau lumineux de base**. Le premier mécanisme doit pouvoir
être étudié sans activer une calibration de normalisation par fichier. Pour
le second, reprendre les niveaux noir/blanc, l'exposition de base caméra,
la courbe DCP et le mapping de sortie, sans gain global ajusté sur ces seules
moyennes. Les ablations actuelles sont diagnostiques ; aucun comportement
de production n'est changé sur cette branche à ce stade. Maintenir la pause
des réglages Couleur et préserver tous les RAW/JSON.

## Bibliographie et preuves

- [PhotoCraft — dépôt et documentation](https://github.com/storytold/photocraft/), [manifeste](https://github.com/storytold/photocraft/blob/main/Cargo.toml) et [contrat/décodage RAW](https://github.com/storytold/photocraft/blob/main/crates/raw/src/lib.rs), consultés le 9 octobre 2026 ; pistes et limites en §27.
- [LightCraft — présentation et périmètre](https://github.com/storytold/lightcraft/blob/main/README.md), consulté le 9 octobre 2026 ; projet voisin à examiner séparément.

- Sharma, Wu, Dalal, *The CIEDE2000 Color-Difference Formula: Implementation
  Notes, Supplementary Test Data, and Mathematical Observations*, 2005 :
  [article et outils](https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/),
  [jeu de vérification](https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/dataNprograms/ciede2000testdata.txt).
- [Mesures v2 et empreintes](../raw/mesure-base-colorimetrique-v2-20261006.json).
- [Investigation RAW/DCP/LCP et expérience d'espace des tables](../raw/investigation-base-tonale-20261006.json).
- [Tests de l'analyseur](../../../tests/test_analyse_base_colorimetrique.py).
- [Décodage RAW et estimation WB](../../../src/raw/RawDecoder.cpp),
  [rendu et reset](../../../src/editor/imageeditor.cpp),
  [générateur de tests](../../../src/tools/generate_test_images.cpp).
