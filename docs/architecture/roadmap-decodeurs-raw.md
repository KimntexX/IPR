# Roadmap — décodeurs RAW propres à IPR

**Décision :** [ADR-0018](../decisions/ADR-0018-independance-decodage-raw-et-couche-image.md),
20 octobre 2026. **État :** analyse ; aucun code. Le chantier ne commence
qu'après l'intégration et la validation des fonctions de base. Il se fera
sur une branche durable dédiée, créée depuis `dev` à jour à son démarrage.
La séparation d'OpenCV est un chantier distinct :
[roadmap de la couche image](roadmap-couche-image-sans-opencv.md).

## 1. Pourquoi

Un framework externe décide aujourd'hui des formats et des boîtiers qu'IPR
peut ouvrir, et de la justesse de leurs niveaux. Constats du 20 octobre 2026 :

- **CR3 :** LibRaw 0.22.2 laisse, sur certains CR3 du R6 Mark III, un noir nul
  ou incohérent alors que les pixels masqués portent un piédestal ; IPR le
  corrige par `RawBlackLevelEstimator`. Testé par l'utilisateur : RawTherapee
  présente le même défaut de noirs, puisqu'il lit ses RAW avec LibRaw.
- **Sony :** le nouveau format ARW6 (A7R VI, a7 V) reste un risque pour qui
  dépend d'un tiers. Son codec repose toutefois sur une norme publique
  (SMPTE RDD 34, LLVC) et LightCraft le décode déjà (§3) : la voie propre existe.
- **Nikon :** HE/HE\* (JPEG XS, technologie TicoRAW brevetée) n'a aucune voie
  libre ; les autres NEF sont à réévaluer plus tard.

## 2. Ce que LibRaw fait aujourd'hui

| Étape | Réalisée par | Remplacement |
| --- | --- | --- |
| Conteneur CR3 (ISO BMFF), EXIF, objectif, ISO, date | LibRaw `open_file` | Lecteur IPR |
| Données Canon : noir, blanc, zones de noir optique | LibRaw `makernotes.canon` | Lecteur IPR, tables par boîtier |
| Décompression CRX vers mosaïque | LibRaw `unpack` | Décodeur CRX IPR |
| Noir du R6 Mark III | IPR `RawBlackLevelEstimator` | Devient la lecture normale |
| Règles par mode capteur (ADR-0011), catalogue (ADR-0012) | IPR | Inchangé, requalifié pour le décodeur IPR |
| Soustraction du noir, WB, dématriçage AHD, sortie 16 bits | LibRaw `dcraw_process` | Traitement flottant IPR ; **seule étape qui change l'image** |
| Couleur DCP, LCP, WB estimée | IPR | Inchangé |
| Vignette embarquée | LibRaw (extraction) et Qt (JPEG) | Extraction IPR, Qt inchangé |
| Import DNG | LibRaw | Lecteur IPR (libtiff + LJ92) ou abandon : décision ouverte |

## 3. Leçons des projets existants

### RawTherapee

- Jusqu'en 2024, RawTherapee lisait les RAW avec un dcraw modifié et ses propres
  décodeurs (CR3 en 2019, C-RAW en 2022, réalignés sur LibRaw et ART). Les
  données propres aux boîtiers sont dans `camconst.json`, principe comparable
  au catalogue JSON d'IPR.
- Depuis la 5.11, il lit les RAW avec LibRaw et ne revient à dcraw que pour les
  formats refusés ; ses notes de version jugent l'entretien de dcraw trop lourd
  pour suivre les nouveaux boîtiers et annoncent LibRaw obligatoire.
- Il garde tout ce qui suit la mosaïque : une dizaine de dématriçages (AMaZE,
  RCD, DCB, LMMSE, IGV, VNG4, double dématriçage, pixel shift) et un traitement
  flottant.

Conséquences pour IPR : l'entretien d'un décodeur est le coût principal, et
c'est lui que les tests automatisés et les agents (§7 et §8) doivent réduire.
Le partage « LibRaw pour lire, traitement maison ensuite » reste une option
de repli : indépendance du rendu, mais pas des boîtiers. RawTherapee est sous
GPLv3 : s'en inspirer, ne pas recopier, tant qu'IPR n'a pas choisi sa licence.

### LightCraft (consulté le 20 octobre 2026)

Logiciel de retouche en Rust sans dépendance C/C++, en alpha, sous licence
**MIT ou Apache-2.0**, développé en grande partie par des agents IA. Il fait
déjà ce que vise IPR : des décodeurs propres pour une dizaine de formats
(545 boîtiers vérifiés sur un vrai fichier, 651 non vérifiés, sur les 1 446 de
la liste Adobe).

| Sujet | Ce que fait LightCraft | Ce qu'IPR en retient |
| --- | --- | --- |
| Organisation | Un fichier par fabricant (`cr2`, `cr3`, `crx`, `crx_wavelet`, `arw`, `arw6`, `nef`, `nefc`…) au-dessus de briques communes (`ljpeg`, `tiffraw`, `dng`, `jxl`) ; dématriçages AHD, PPG, bilinéaire, X-Trans | Même découpage pour `src/raw/` : socle commun P0, puis un module par format |
| Écriture propre | Code de LibRaw, rawspeed, rawler, RawTherapee, darktable interdit en lecture ; décodeurs tiers utilisés seulement comme **exécutables oracles** (DNGLab 0.8.0 pour le CR3, LibRaw pour l'ARW6) | Confirme le point 5 de l'ADR-0018 ; DNGLab est un oracle CR3 possible après le retrait de LibRaw |
| Licence | MIT/Apache-2.0 | Lecture du code et des documents permise ; portage en C++ possible avec mention d'origine (fichier NOTICE), quelle que soit la future licence d'IPR. Leur provenance « propre » reste une affirmation : l'oracle IPR valide tout |
| CR3 | CRX 0x100 sans perte et C-RAW en tuiles horizontales ; 0x200 C-RAW en tuile unique, QP adaptatif 131–175 ; autres variantes refusées explicitement, repli sur le JPEG embarqué avec avertissement | Liste de variantes de départ pour P2 ; même politique de refus explicite |
| R6 Mark III | `ColorData` version 66 : noir au mot 383, **égal aux colonnes masquées** (512 à bas ISO, 2047–2048 au-delà) ; le blanc du mot 661 n'est pas l'écrêtage, il est déduit des données | Cohérent avec les fenêtres ADR-0011 et le noir optique de 2048 contrôlé ; la lecture de `ColorData` 66 pourrait remplacer l'estimateur. **À vérifier** par l'essai du §10 |
| R7 | C-RAW sans référence externe chez eux | Notre corpus R7 pourra combler ce manque côté IPR |
| Sony ARW6 | Décodé pour l'A7R VI à partir de SMPTE RDD 34 (ondelettes 5/3, tuiles, courbe de compression 12 vers 14 bits mesurée) ; codes 12 bits identiques à LibRaw ; a7 V non vérifié ; mode compressé 12 bits non pris en charge ; ≈ 896 Mo de pic mémoire | Le format est documenté ; budget mémoire à surveiller sur le poste 16 Go |
| Nikon HE/HE\* | Détecté par les marqueurs JPEG XS `FF10 FF50`, message clair et JPEG embarqué | Même détection et même message pour IPR |
| Robustesse | Entrées traitées comme hostiles : calculs vérifiés, récursion bornée, allocations plafonnées, erreur exploitable plutôt que plantage | Règles à reprendre pour le §7 |
| Tests | Corpus CC0 hors Git, épinglé par SHA-256 et téléchargé par commande ; test impossible = **ignoré**, jamais réussi ; encodeurs de test synthétiques ; tableau de couverture régénéré, la vérification échoue s'il est périmé | Confirme le §7 ; ajouter le tableau de couverture vérifié |
| Couleur | Profils Adobe interdits par leurs règles : couleur estimée sur le JPEG du boîtier, calibration mesurée absente | Avantage IPR : DCP locaux (ADR-0003) et références Lightroom |
| Limites | Alpha ; 45 % de boîtiers non vérifiés ; bugs de niveaux par boîtier récurrents ; confiance de décodage CR3 la plus basse (0,6) | La largeur ne remplace pas la profondeur : IPR reste sur un corpus restreint et validé |

## 4. Formats

| Format | Variantes à couvrir | Coût estimé | Limites |
| --- | --- | --- | --- |
| Socle commun | Lecteur TIFF/EXIF, JPEG sans perte (LJ92), oracle, robustesse | 2 à 3 semaines | Prérequis de tous les formats |
| **CR3** (R6 Mark III, R7, puis R5) | ISO BMFF, CRX sans perte et C-RAW, données Canon par version de `ColorData` | Inclus dans 9 à 15 semaines avec dématriçage et validation | Le corpus de 19,5 à 53 Mo laisse supposer du C-RAW : à confirmer à l'inventaire |
| **CR2** (6D Mark II annoncé) | TIFF, LJ92 en bandes ; sRAW/mRAW en option | 1 à 2 semaines avec le socle | Ne valide pas le CR3 ; règles CR3 non transposables |
| **DNG** | LJ92, deflate (flottant), JPEG avec perte | 1 à 2 semaines avec le socle | JPEG XL (DNG 1.7) refusé explicitement tant qu'il n'est pas traité |
| **Sony ARW** | Non compressé 12/14 bits ; cRAW avec perte ; sans perte LJ92 (depuis l'A1, 2021) ; YUV420 des rafales A1 ; ARW6 (SMPTE RDD 34) | 2 à 4 semaines, plus l'ARW6 à estimer à partir de la documentation LightCraft, puis 1 à 3 jours par boîtier | ARW6 de l'a7 V non vérifié publiquement ; mode compressé 12 bits non documenté |
| **Nikon NEF** | Non compressé ; compressé sans ou avec perte (Huffman), WB chiffrée par numéro de série et nombre de déclenchements, courbe de linéarisation | 3 à 5 semaines, puis 1 à 3 jours par boîtier | HE/HE\* (JPEG XS) : brevet intoPIX ; voie SDK Nikon propriétaire à étudier, sinon détection (`FF10 FF50`) et refus explicite |

Estimations internes, à remesurer sur le premier format livré. Un format
ou une variante non couvert est refusé explicitement, jamais décodé au hasard
(ADR-0012).

## 5. Paliers du chantier

| Palier | Travail | Critère de passage | Change l'image ? |
| --- | --- | --- | --- |
| P0 | Socle : TIFF/EXIF, LJ92, format des références JSON, tests de robustesse | Tests synthétiques et fuzzing verts | Non |
| P1 | Conteneur et métadonnées CR3 | Métadonnées identiques à l'oracle sur tout le corpus | Non |
| P2 | Décompression CRX (sans perte, C-RAW) | Mosaïque identique **bit à bit** sur tout le corpus | Non |
| P3 | Noir, WB, dématriçage flottant | Écarts mesurés (ΔE, EV, détail) puis validation visuelle utilisateur | **Oui** |
| P4 | Bascule : retrait de LibRaw, requalification du catalogue | CI verte sans LibRaw, campagnes revalidées | Non (déjà validé en P3) |

On peut s'arrêter après P2 sans risque si le coût paraît trop élevé. Pendant
P1–P3, LibRaw reste le seul chemin de production ; il ne sert d'oracle que sur
la branche. Après P4, les références sont produites par un outil externe hors
dépôt (`dcraw_emu`, dnglab) et conservées dans les JSON de preuve.

## 6. Corpus minimal par boîtier

Chaque RAW compte pour un seul rôle. Un boîtier n'atteint un niveau que si
tous les rôles de ce niveau sont couverts ; sinon il reste au niveau inférieur.

| Niveau | Ce qu'il autorise | Rôles requis | Minimum |
| --- | --- | --- | --- |
| **A — Ouverture** | Décodage et chemin non calibré | 1 RAW par mode d'enregistrement utilisé (compression × profondeur × obturateur mécanique/électronique × recadrage), au moins 2 ; 2 images noires (bouchon monté) à l'ISO de base et à l'ISO natif maximal | **4 RAW** |
| **B — Traitement** | Règles noir/blanc par mode (ADR-0011) et réglages | Niveau A + échelle ISO d'une même scène neutre (base, 800, 3200, maximum natif) ; 2 images en saturation (zone claire uniforme, ISO de base et haut) | **10 RAW** |
| **C — Rendu validé** | Validation des panneaux sur ce boîtier | Niveau B + 6 scènes avec référence Lightroom : neutre (charte ou gris), ciel saturé, fleurs contrastées, basse lumière, contre-jour, lumière artificielle | **16 RAW** (20 recommandés) |

Pour une **nouvelle variante de format** : au moins 1 RAW par variante et par
boîtier, sur 2 boîtiers dès qu'ils existent. Une image noire supplémentaire
est requise pour tout mode de lecture qui modifie le noir (par exemple
l'obturateur électronique). Repère : le corpus actuel compte 19 CR3 pour deux
boîtiers ; le niveau atteint par chacun (images noires et de saturation
comprises) sera établi par le premier inventaire, pas supposé.

Les RAW ne sont pas versionnés dans Git. Leur inventaire l'est : SHA-256,
boîtier, mode, ISO, rôle, provenance et licence, sur le modèle de
`tests/corpus/raw-black-level/`. Les échantillons publics (raw.pixls.us)
complètent les boîtiers que l'utilisateur ne possède pas ; ils ne valent
jamais validation de rendu (niveau C).

## 7. Tests automatisés

| Test | Rôle | Où |
| --- | --- | --- |
| Synthétiques | Flux générés par un petit encodeur de test (LJ92, ARW non compressé, cRAW) ; extraits réels pour le CRX | CI |
| Oracle bit à bit | Par RAW : SHA-256 du fichier et de la mosaïque, dimensions, CFA, noir ×4, blanc, WB, recadrage, zones masquées | Local sur tout le corpus ; CI sur les échantillons publics |
| Métadonnées | Comparaison à ExifTool lors de la création des références | Création des références |
| Robustesse | Fuzzing des lecteurs, fichiers tronqués ou corrompus : erreur explicite, ni plantage, ni blocage, mémoire bornée ; ASan/UBSan | CI (job borné en durée) |
| Rendu | Empreintes du master pour les paliers sans changement d'image ; tolérances chiffrées (ΔE2000 p95, EV, détail) pour P3 | Local, sonde d'équivalence existante |
| Performance | Temps d'ouverture et pic mémoire par mégapixel, sur le poste 16 Go | Local, avant fusion |
| Couverture | Tableau des boîtiers, formats, variantes et niveaux (§6), régénéré depuis les JSON ; la vérification échoue s'il est périmé | CI |

Un RAW absent du poste est signalé comme **ignoré**, jamais compté comme réussi.
La CI télécharge un jeu public figé (URL et SHA-256) et le met en cache.

## 8. Agents IA pour intégrer un boîtier

Les agents travaillent dans un worktree isolé, sur la branche du chantier.
Ils préparent, mesurent et documentent ; ils ne suppriment aucune image, ne
fusionnent, ne publient et ne posent aucun tag sans autorisation.

| Agent | Entrée | Sortie |
| --- | --- | --- |
| Inventaire | Nouveaux RAW | Inventaire JSON (ExifTool + sonde IPR), niveau atteint (§6), liste précise des prises de vue manquantes |
| Format | Inventaire, structure du fichier | Variante connue, ou note d'analyse et projet de décodeur à partir de descriptions publiques ; **sans lire de code LGPL/GPL** |
| Caractérisation | Images noires, saturation, échelle ISO | Noir par canal, blanc, domaine ISO ; projet d'entrée du catalogue avec provenance, sans valeur inventée |
| Validation | Branche et corpus | Tests du §7, JSON de preuve horodatés, section du rapport évolutif |
| Revue | Diff | Revue indépendante : ressources, pointeurs, règles d'AGENTS.md |

L'oracle bit à bit donne aux agents un retour objectif immédiat : c'est ce
qui rend la mise au point d'un décompresseur adaptée à un agent. Gain visé,
à mesurer sur le premier boîtier (R5 ou 6D Mark II) : de 1 à 3 jours de
travail manuel à quelques heures de travail d'agents, plus 1 à 2 heures de
revue et de validation par l'utilisateur. Le temps réel de chaque intégration
est consigné dans le rapport pour vérifier ce gain.

## 9. Procédure d'intégration d'un boîtier

Complète la procédure de l'[ADR-0012](../decisions/ADR-0012-formats-extensibles-et-caracterisations.md#ajouter-un-nouveau-boîtier),
qui reste la référence pour le catalogue.

1. **Utilisateur :** fournit les RAW (§6).
2. **Agent inventaire :** inventaire, niveau atteint, prises manquantes.
3. **Agent format :** variante connue ou travail de décodeur (§5).
4. **Agent caractérisation :** projet d'entrée du catalogue.
5. **Agent validation :** tests et JSON de preuve ; **agent revue :** revue.
6. **Utilisateur :** validation visuelle pour le niveau C.
7. **Livraison :** workflow de branches habituel, après autorisation.

## 10. Décisions ouvertes

- **DNG :** lecteur IPR ou abandon de l'import DNG.
- **Dématriçage :** rester au plus près de l'AHD actuel ou accepter un nouveau
  rendu revalidé ; algorithme à écrire à partir des publications.
- **Nikon :** périmètre hors HE/HE\*, et étude de la licence du SDK Nikon.
- **CR2 :** priorité à fixer à la réception des fichiers du 6D Mark II.

**Essai prévu le 21 octobre 2026 :** passer l'outil en ligne de commande de
LightCraft, en boîte noire, sur les 19 CR3 du corpus. Comparer ses noirs et
blancs par canal à LibRaw, à `RawBlackLevelEstimator` et aux colonnes masquées,
par ISO et par boîtier ; noter les variantes CRX refusées (C-RAW du R7). Les
résultats vont dans un JSON de preuve horodaté ; l'essai ne modifie pas le
moteur. Il dira si la lecture de `ColorData` 66 suffit pour le R6 Mark III.

## Sources consultées les 19 et 20 octobre 2026

- [RawTherapee — notes de version 5.11/5.12](https://rawtherapee.com/downloads/5.12/) :
  LibRaw comme lecteur depuis la 5.11, repli dcraw, LibRaw annoncé obligatoire.
  [Wikipédia](https://en.wikipedia.org/wiki/RawTherapee) date ce passage à la 5.12 ;
  les notes officielles sont retenues.
- [RawTherapee — dépôt](https://github.com/RawTherapee/RawTherapee),
  [CR3 (#5319)](https://github.com/Beep6581/RawTherapee/issues/5319),
  [C-RAW (#6434)](https://github.com/Beep6581/RawTherapee/pull/6434).
- [lclevy/sony_raw](https://github.com/lclevy/sony_raw) : variantes ARW, LJ92 depuis l'A1, ARW 6.0 de l'a7 V.
- Nikon HE/HE\* : [darktable #15873](https://github.com/darktable-org/darktable/issues/15873),
  [boîtiers darktable](https://www.darktable.org/resources/camera-support/),
  [LibRaw 0.22 refuse le HE](https://github.com/pdcgomes/redlamp/issues/168),
  [licence TicoRAW](https://github.com/storytold/lightcraft/pull/603),
  [TICO](https://en.wikipedia.org/wiki/TICO_(codec)).
- [LibRaw — API C++](https://www.libraw.org/docs/API-CXX.html).
- LightCraft : [dépôt](https://github.com/storytold/lightcraft),
  [AGENTS.md](https://github.com/storytold/lightcraft/blob/main/AGENTS.md),
  [couverture des boîtiers](https://github.com/storytold/lightcraft/blob/main/docs/raw-parity.md),
  [CR3](https://github.com/storytold/lightcraft/blob/main/docs/cr3.md),
  [ARW6](https://github.com/storytold/lightcraft/blob/main/docs/arw6-compression.md),
  [détection Nikon HE (#603)](https://github.com/storytold/lightcraft/pull/603).
