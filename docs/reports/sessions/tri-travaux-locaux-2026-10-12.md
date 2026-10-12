# Tri préparatoire des travaux locaux — 12 octobre 2026

Statut : inventaire et plan de livraison, **pas intégration ni validation du code local**.
Périmètre : générateur de tests, points d'entrée d'analyse et documentation.
Aucune traduction, génération de photos, suppression, fusion dans dev/main ou publication.

## 1. Situation et mémoire de l'inventaire

Le worktree principal est sur `patch_couleur`, commit `4c24154`, avec une base
plus ancienne que `dev` (`1027092`, `v0.1.17-alpha`). Les références locales de
comparaison sont `patch_documentation` (`6cae5c4`) et `patch_ci_cd` (`7b0e7ea`).
Cette passe ne consulte pas les CI distantes et ne reprend pas l'intégration
suspendue à la demande de l'utilisateur.

La [mémoire JSON de tri](../../validation/raw/workspace/inventaire-travaux-locaux-v001-20261012T090522Z.json)
conserve chaque chemin, son état local, l'objet Git de son contenu, les branches
où il existe et celles dont il est identique. Les identifiants sont ceux des
blobs Git après filtres de normalisation (« clean »), pas des SHA-256 bruts ni des sauvegardes.
Les photos, builds et autres fichiers ignorés sont exclus. Les fichiers de
préparation ajoutés après le relevé ne sont pas comptés.

| Classe constatée | Fichiers | Traitement |
| --- | ---: | --- |
| Contenu déjà identique à dev | 39 | Ne pas relivrer comme nouveaux changements |
| Identique à patch_documentation, pas à dev | 105 | Déjà publié sur la branche documentaire ; intégration encore distincte |
| Autres écarts à examiner | 134 | Pas automatiquement 134 nouveautés : certains contenus sont plus anciens |
| Total | 278 | 96 changements de fichiers suivis + 182 fichiers non suivis dans ce worktree |

Répartition des 134 écarts : générateur 4 ; points d'entrée d'analyse 16 ;
sélection partagée 2 ; divergences de base 7 ; outils documentaires 2 ;
outils opérationnels historiques 4 ; documentation à réconcilier 99.
Le mot « non suivi » dépend de la branche : un fichier peut être non suivi ici
tout en étant déjà publié ailleurs.

## 2. Ce qui ne doit pas être livré à nouveau

Le socle `tests/analysis/*.py`, `analyse_groupes.py`, `analyse_couleur.py`, les
outils de comparaison WB et une partie de la conservation du contexte DCP
sont déjà dans dev. Le tri par contenu confirme notamment les en-têtes RAW,
`RawDecoder.cpp`, `DecodedImageCache.cpp` et `ColorPipeline.h`.

Les écarts suivants représentent surtout l'absence de correctifs plus récents :

| Fichier local | Risque d'une copie complète vers dev |
| --- | --- |
| `CMakeLists.txt` | Perte des contrats de version/package, finalisation Qt, installation et test de cycle de vie ; l'ajout local de wbCameraProbe est déjà livré |
| `src/editor/imageeditor.cpp` | Suppression des gardes de futures non démarrées ; risque de blocage Qt 6.4 à la destruction |
| `src/profiles/CameraProfileEngine.cpp` | Perte des chemins de profils des paquets installés |
| `tests/camera_profile_engine_test.cpp` | Réintroduction des comparaisons non canoniques qui échouaient sur macOS |
| `tests/image_export_test.cpp` | Attente de boucle après erreur synchrone, déjà corrigée |
| `tests/test_analyse_groupes.py`, `tests/test_compare_temperature_tint.py` | Perte de corrections de chemins/macOS et de fermeture des memmaps Windows |

Ces sept fichiers demandent une revue **par morceaux de diff**, pas un
nouveau patch couleur. Un `git diff dev` ne décrit pas à lui seul les fichiers
non suivis : il peut les afficher comme absents alors qu'ils existent sur disque.
Le JSON d'inventaire tient compte de leur contenu réel.

## 3. Lots proposés et dépendances

Utiliser la branche durable existante `patch_validation` pour l'outillage de
validation ; l'actualiser depuis dev avant tout portage. Utiliser
`patch_documentation` pour la documentation et les règles. Ne pas recréer de
branche, déplacer le worktree sale ou copier tout son contenu dans une branche.

| Lot | Branche | Changements à préparer | Dépendance |
| --- | --- | --- | --- |
| A — Générateur simple | patch_validation | Source C++, lanceur, tests et retrait du lanceur de campagne remplacé | Base dev actualisée ; contrat de dossiers vérifié |
| B — Analyseurs par groupe | patch_validation | Neuf points d'entrée manquants, suppression des sept doubles chemins legacy, adaptation au générateur courant | A pour la recette de bout en bout ; socle d'analyse déjà dans dev |
| C — Cartographie des tests | patch_validation | Reprise ciblée des règles et de leurs tests, sans écraser la CI récente | A/B et versions courantes des règles CI |
| D — Documents suivis et règles | patch_documentation | Navigation, renvois de rapports/protocoles, contrats, README, LaTeX et règles projet | A/B avant de déclarer la nouvelle procédure opérationnelle |

Les lots sont des unités de revue ; leur nombre ne fixe pas encore le compteur
des futurs tags. Le nombre de patchs effectivement livrés sera consigné lors
des fusions validées, pas déduit du nombre de commits de préparation.

### A. Générateur : quatre fichiers, plusieurs contrôles avant livraison

- `src/tools/generate_test_images.cpp` : nouveau contrat all/groupe, lecture
  non récursive de base, presets fixes, TIFF seuls, neutres par groupe.
- `scripts/generate-test-images.ps1` : lanceur à argument unique et restauration
  de son environnement enfant.
- `tests/test_image_generator_test.cpp` : tests de la seule implémentation courante.
- `scripts/generate-photographic-campaign.ps1` : retrait local du lanceur remplacé,
  à porter dans ce même lot après revue de ses références.

Points relevés dans le code local :

1. **Dossiers à aligner** : le générateur envoie vibrance/saturation à la racine
   de base, alors que `base/couleur/vibrance` et `base/couleur/saturation` existent.
   Utiliser les dossiers LR existants ; créer seulement les destinations absentes.
2. **Pont avec l'analyse** : le générateur ne produit plus de manifeste, mais
   la CLI d'analyse courante exige `--manifest` ou `--mapping`. Une génération
   simple ne fournit donc pas encore une entrée directe à l'analyseur.
3. **Supports WB** : le neutre de groupe est As Shot ; les séries température/
   nuance utilisent 5200 K / nuance 0. Pour ces séries, distinguer le neutre
   caméra des supports manuels 5200 et tint 0, sans assimiler le +5 As Shot LR
   à une erreur utilisateur.
4. **Volume de all** : il prend toutes les sources de base et toutes les séries.
   Le contrôle d'espace avec réserve de 512 Mio est effectué par fichier ; il
   ne garantit pas que la campagne entière tiendra sur le disque. Préparer une
   estimation préalable et garder les tests all sur de petites fixtures.
5. **Provenance** : noms/presets donnent contrôle et valeur, pas la preuve de
   tous les choix DCP/LCP/WB ni de l'identité du moteur qui a produit le TIFF.

Ne pas rétablir manifeste, journal ou fichier tiers du générateur. Pour la
provenance, évaluer des métadonnées **dans les TIFF**, ou une déclaration
explicite propre à l'analyseur. Ne jamais inventer profil/réglages manquants.
Ce choix d'interface reste à arrêter lors de l'implémentation, pas dans ce tri.

Tests nécessaires : découverte de racine, absence de récursion, dix groupes et
all, isolation des paramètres, neutres/supports, TIFF 16 bits/ICC/dimensions,
sources et sidecars inchangés, fichiers LR conservés, refus de collisions,
chemins redirigés hors base, reprise après erreur et espace disponible.
Exécuter `testImageGeneratorTests` avec le générateur reconstruit, puis
`imageExportTests` ; la première intégration au socle CI appelle la suite complète.

### B. Analyseurs : neuf wrappers réellement manquants

À préparer : `analyse_raw.py`, `analyse_lumiere.py`, `analyse_courbes.py`,
`analyse_melangeur.py`, `analyse_grading.py`, `analyse_effets.py`,
`analyse_bruit.py`, `analyse_nettete.py`, `analyse_optique.py`, sous tests/.
`analyse_couleur.py` et `analyse_groupes.py` sont déjà livrés.

Les sept fichiers locaux température, tint, vibrance, saturation, texture,
clarity et dehaze ajoutent un routage moderne **tout en conservant --legacy et
le calcul historique**. Cela contrevient à la règle de prototypage. Recommandation :
utiliser uniquement les points d'entrée par groupe, retirer les implémentations
remplacées et adapter leurs références actives. Garder Git et les JSON historiques,
pas une seconde branche d'exécution. Les sondes spécialisées qui testent une
autre hypothèse nécessitent une revue séparée : ne pas les supprimer en bloc.

Les wrappers seuls ne corrigent pas le pont d'entrée identifié dans A. Préparer
une association des noms IPR explicites, des références LR et des supports,
avec audits avant calcul. Les suffixes LR ne doivent pas être devinés universellement.
Les états non auditables doivent rester signalés/exclus, pas « validés » par défaut.

Tests nécessaires : --help/--list de chaque groupe, mauvais groupe, TIFF natifs
little/big endian, ICC, référence absente/ambiguë, variations parasites, optique,
états hors bornes, JSON immuable et --strict. Conserver les corrections de
fermeture des memmaps et de canonicalisation présentes dans dev.
Ajouter un test générateur → analyse sur petites images, sans génération du corpus.

### C. Sélection des tests : deux fichiers partagés

`scripts/test-rules.json` et `tests/test_build_windows.py` doivent être repris
par hunks. Le fichier local ne connaît pas tous les tests ajoutés par la CI
récente : ne pas remplacer le catalogue publié par sa version ancienne.
Réutiliser l'identifiant courant `group_analysis`, pas introduire un doublon
`grouped_analysis`. Garder les suites WB dépendantes du socle partagé et les
gardes complètes de CI, publication, déploiement et fichiers non cartographiés.
Ajouter les neuf wrappers et la règle du lanceur si elle est réellement utile.

Tester ciblage, ajouts/suppressions, absence de changement, repli inconnu et
transversal. Relancer les tests de sélection et de portée CI après rapprochement.

### D. Documentation suivie : réconcilier, ne pas remplacer

99 écarts documentaires restent à examiner, certains étant des copies plus
anciennes de corrections déjà sur patch_documentation. Séparer :

- Navigation et README : langue, liens vers les familles actuelles et capture.
- Rapports bruit/mélangeur : les rapports uniques existent déjà ; porter les
  renvois locaux des anciens fichiers, sans relancer la consolidation destructive.
- Protocoles : catalogue par groupe et renvois datés ; harmoniser avec les outils
  effectivement disponibles et la suppression manuelle des images.
- Procédure autonome/guide générateur : publier la recette simple seulement avec
  le programme correspondant et son pont d'analyse testé.
- Règles AGENTS : prototype/versionnage et suppression manuelle ; corriger les
  textes qui décrivent encore une suppression automatique. Les traductions
  restent à l'autre agent ; aucune nouvelle traduction dans cette passe.
- Contrats INI/sidecar et LaTeX : confronter aux sources actuelles ; distinguer
  descriptions historiques et exigences sans legacy. Vérifier diagrammes,
  12 pt/interligne 1,5 puis compiler le document français si ses sources changent.
- CSV/SVG locaux : plusieurs sont antérieurs à la correction LF de provenance
  livrée sur patch_documentation ; régénérer depuis les JSON, ne pas recopier
  les fichiers périmés. Ne pas écraser les audits immuables.

Contrôles : liens, UTF-8/JSON, `export_documentation_metrics.py --check`, tests
documentaires, vérification des ancres de renvoi et compilation LaTeX ciblée.
Un contrôle de liens réussi ne valide pas les équations, les traductions ou
les fonctionnalités décrites. Les statuts photographiques restent inchangés.

## 4. Outils à mettre hors livraison automatique

Deux utilitaires documentaires locaux demandent une décision :
`scripts/check_documentation.py` écrit un CSV daté et peut réécrire les liens ;
la CI a déjà un contrôleur en lecture seule. `scripts/consolidate_reports.py`
peut écraser les rapports regroupés et les anciens fichiers lors d'un --apply.
Ne pas les exécuter pour « nettoyer » le worktree. Reprendre seulement la
fonction utile, testée et non destructive, sans conserver de chemin remplacé.

Quatre scripts d'opérations historiques sont hors des lots A–D :
`cleanup-obsolete-test-images-20261009.ps1`, `cleanup-unused-builds-20261010.ps1`,
`inventory-lightroom-references-20261009.py`, `remove-standard-lightroom-references-20261009.ps1`.
Leurs audits sont conservés ; les scripts ne sont ni lancés ni publiés par ce tri.
Leur archivage ou retrait local demanderait une action séparée, sans effacer
les preuves ni utiliser des chemins destructifs larges.

## 5. Ordre de reprise et critères de clôture

1. Garder les fusions CI/documentation suspendues tant que leurs validations
   requises ne sont pas terminées ; ne pas attendre/poller pendant ce tri.
2. Après intégration autorisée des branches publiées, actualiser patch_validation
   et patch_documentation depuis dev ; conserver toutes les branches de groupe.
3. Porter A et ses tests depuis la base récente ; corriger dossiers et supports.
4. Préparer le pont d'analyse puis B sans --legacy ; tester de bout en bout.
5. Réconcilier C avec le catalogue de tests courant.
6. Porter D par sous-lots cohérents, régénérer uniquement les dérivés nécessaires.
7. Publier les branches préparées lors d'une action autorisée, puis fusionner
   --no-ff dans dev après tests et validation ; fixer le compteur de patchs livré.
8. Nettoyer les doublons de travail seulement après vérification de leur
   conservation et accord explicite ; aucun reset, stash ou git add -A global.

Clôture attendue : outils alignés sur les dix protocoles, absence de legacy
remplacé, fichiers de référence préservés, recette indépendante du worktree
historique, tests adaptés puis CI verte, JSON de preuve/versionnage cohérents.
Ce document prépare cette reprise ; il n'annonce pas que ces conditions sont remplies.
