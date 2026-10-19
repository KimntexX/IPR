# ADR-0018 — Indépendance du décodage RAW et de la couche image

- **Date :** 2026-10-20
- **Statut :** Accepté comme orientation ; travaux **non planifiés avant la fin
  de l'intégration des fonctions de base**. Révise la conclusion du
  [rapport du 1er octobre 2026](../architecture/references/rapport-dependances-opencv-libraw-2026-10-01.md).
- **Décideurs :** utilisateur

## Contexte

Le rapport du 1er octobre recommandait de conserver LibRaw et OpenCV : leur
retrait ne réduisait pas le temps d'ouverture. Ce critère de performance
reste exact, mais il ne traite pas le risque de dépendance.

- **CR3 Canon :** LibRaw 0.22.2 expose, pour certains CR3 du R6 Mark III, un
  niveau noir nul ou incohérent alors que les pixels masqués conservent un
  piédestal. IPR compense par `RawBlackLevelEstimator` depuis le début du
  projet ([moteur de profils](../camera-profile-engine.md)). L'utilisateur a
  vérifié le 20 octobre 2026 que RawTherapee présente le même défaut de noirs :
  RawTherapee lit en effet ses RAW avec LibRaw depuis sa version 5.11.
- **Sony :** l'ARW6 (A7R VI, a7 V) illustre la course permanente aux nouveaux
  formats. Son codec repose sur une norme publique (SMPTE RDD 34) et LightCraft
  le décode déjà pour l'A7R VI ; l'a7 V n'est vérifié publiquement nulle part.
- **Nikon :** les NEF HE/HE\* reposent sur JPEG XS (technologie TicoRAW,
  brevetée par intoPIX). LibRaw 0.22 les refuse, darktable ne les prend pas en charge.
- **Précédent :** LightCraft (Rust, MIT/Apache-2.0) a écrit ses propres
  décodeurs pour une dizaine de formats, avec des oracles en boîte noire et des
  agents IA ; l'indépendance visée est donc atteignable
  ([analyse](../architecture/roadmap-decodeurs-raw.md#lightcraft-consulté-le-20-octobre-2026)).
- **Constat utilisateur :** dépendre d'un framework externe pour intégrer un
  format ou un nouveau boîtier est un problème sérieux pour IPR.

IPR réalise déjà lui-même la partie interprétative : noir optique estimé,
règles par mode capteur (ADR-0011), catalogue de caractérisations (ADR-0012),
couleur DCP, LCP, estimation de la balance des blancs. LibRaw ne fournit plus
que le conteneur, la décompression et le dématriçage (`dcraw_process`).
OpenCV n'est utilisé que pour `core` et `imgproc`.

## Décision

1. **Cible :** des décodeurs RAW propres à IPR, puis une couche image propre à
   IPR. Les deux séparations sont **deux chantiers distincts**, sur deux
   branches durables distinctes, dans cet ordre : LibRaw d'abord, OpenCV ensuite.
2. **Démarrage :** après l'intégration et la validation des fonctions de base
   (repère proposé : critère de passage en beta). Seule exception : le retrait
   immédiat d'éléments inutilisés d'OpenCV (`imgcodecs`, options vcpkg
   `jpeg`/`png`/`tiff`), qui relève de la règle de suppression des éléments
   inutiles et ne change aucun pixel.
3. **Progression par paliers à oracle :** chaque palier qui ne doit pas changer
   l'image est validé bit à bit (mosaïque, métadonnées) ; seul le palier qui
   change l'image (dématriçage, noyaux de filtrage) est validé par mesures
   d'écart puis par l'utilisateur.
4. **Pas de double implémentation livrée :** pendant le chantier, LibRaw reste
   le seul chemin de production et sert d'oracle sur la branche ; à la bascule,
   LibRaw est retiré de l'application et des tests (règle « pas de legacy »).
   Les empreintes de référence restent dans les JSON de preuve ; un outil
   externe hors dépôt fournit les références des boîtiers ajoutés ensuite.
5. **Écriture propre :** travailler à partir de descriptions publiques des
   formats et du comportement observable des outils, sans recopier de code
   LGPL (LibRaw, rawler/dnglab) ou GPL (RawTherapee, dcraw modifié). Aucune
   licence n'est encore choisie pour IPR : toute reprise de code sous licence
   réciproque exige d'abord ce choix. Le code et les documents sous licence
   permissive (LightCraft, MIT/Apache-2.0) peuvent être lus et portés avec
   mention d'origine ; l'oracle IPR les valide comme le reste.
6. **Intégration des boîtiers :** procédure outillée, corpus minimal défini,
   tests automatisés et agents IA, décrits dans la
   [roadmap des décodeurs RAW](../architecture/roadmap-decodeurs-raw.md).
   Les agents préparent, mesurent et documentent ; l'utilisateur valide.
7. **Périmètre des formats :** CR3 (R6 Mark III, R7, puis R5), CR2 (6D Mark II
   annoncé) et DNG ; Sony ensuite ; Nikon à réévaluer, sans HE/HE\* faute de
   voie libre.

## Conséquences

- **Bénéfices :** nouveaux boîtiers et modes au rythme d'IPR ; suppression du
  contournement du noir ; décompression par tuiles et dématriçage parallélisables
  (gain d'ouverture probable, non mesuré) ; maîtrise de la précision du rendu.
- **Coûts et risques :** environ 9 à 15 semaines pour le CR3 seul, 4 à 8
  semaines pour OpenCV (estimations internes, non mesurées). Chaque nouveau
  boîtier devient un travail IPR : c'est ce coût qui a ramené RawTherapee vers
  LibRaw. Le dématriçage et les flous changent le rendu et imposent de
  revalider Lumières, couleur et détail.
- **Compatibilité / migration :** les sidecars `.ipr` ne changent pas ; les
  règles du catalogue ADR-0012 portent `decoder_id`/`decoder_version` et devront
  être requalifiées pour le décodeur IPR, sans recopier les règles LibRaw.
- **Validation requise :** oracle bit à bit sur tout le corpus, tests de
  robustesse des lecteurs, campagnes de rendu, validation visuelle utilisateur
  avant toute fusion dans `dev`.

## Liens

- Roadmaps : [décodeurs RAW](../architecture/roadmap-decodeurs-raw.md),
  [couche image sans OpenCV](../architecture/roadmap-couche-image-sans-opencv.md)
- Code : `src/raw/RawDecoder.cpp`, `src/raw/RawBlackLevelEstimator.cpp`,
  `CMakeLists.txt`, `vcpkg.json`
- ADR liés : ADR-0011, ADR-0012, ADR-0013, ADR-0017
- Rapport révisé : [dépendances OpenCV et LibRaw, 1er octobre 2026](../architecture/references/rapport-dependances-opencv-libraw-2026-10-01.md)
