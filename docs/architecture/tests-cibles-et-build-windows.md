*[Français](tests-cibles-et-build-windows.md) | [English](tests-cibles-et-build-windows_en.md)*

# Build Windows et sélection des tests

## Commande quotidienne

Depuis la racine du projet, dans **WSL ou Git Bash** :

```bash
bash scripts/build-windows.sh
```

Le lanceur Bash utilise Python 3.9+ pour sélectionner les tests, puis PowerShell
et les outils **Windows** CMake/MSVC pour compiler. Un lancement depuis WSL
produit bien `appIPR.exe` et `iprGenerateTests.exe` Windows, pas des binaires Linux. Par défaut :
`build-windows-msvc`, `Release`, deux jobs. Fermer l'exécutable de ce dossier
avant de le relier ; le script refuse de l'arrêter automatiquement. Avant de
compiler, il vérifie qu'aucun `.exe` de `<build>/<config>` ne tourne (IPR,
générateur, sonde ou test). Sinon, il s'arrête avec le nom du programme à fermer :
Windows verrouille un exécutable lancé, et l'ancien binaire resterait en place.

Prérequis : MSVC/Visual Studio Build Tools 2022 x64, CMake et CTest Windows,
Qt MSVC, OpenCV/LibTIFF/LibRaw de la même ABI. Le cache CMake existant est réutilisé.
Sur une machine neuve, fournir les chemins de dépendances, par exemple :

```bash
bash scripts/build-windows.sh \
  --cmake-arg=-DCMAKE_PREFIX_PATH=C:/Qt/VERSION/msvc2022_64 \
  --cmake-arg=-DOpenCV_DIR=C:/opencv/build/x64/vc16/lib
```

Ajouter de la même façon la toolchain vcpkg ou les chemins LibRaw/LibTIFF requis.
Ces exemples ne constituent pas une installation automatique des dépendances.
Les tests Python du comparateur nécessitent NumPy et Pillow dans le Python du
lanceur ; le sélecteur lui-même n'utilise que la bibliothèque standard.

Le script ajuste le PATH/plugins Qt de ses seuls processus enfants pour les
tests. Il ne modifie ni l'installation, ni les préférences IPR de l'utilisateur.
Il ne réalise pas le déploiement/packaging des DLL pour une distribution autonome.

## Politique des tests

Les règles versionnées sont dans [scripts/test-rules.json](../../scripts/test-rules.json).
Les tests synthétiques CTest sont enregistrés sous Windows comme sous
Linux/WSL. Le nombre évolue : consulter `ctest --test-dir BUILD -C Release -N`
pour l'inventaire du build étudié, pas un compteur documentaire historique.
Les sondes photographiques privées restent séparées.

Le sélecteur compare les SHA-256 des fichiers de code/configuration à la dernière
validation réussie, conservée dans `build-windows-msvc/.ipr-tested-snapshot.json`.
Il inclut les fichiers non suivis par Git et détecte ajouts, suppressions et
renommages. Les photos, archives, documents et sorties générées ne sont pas du
code et ne déclenchent pas CTest. Ce choix évite de relancer toute la suite à
chaque build d'un worktree déjà sale, sans nécessiter de commit préalable.

| Zone modifiée | Exemples de vérifications |
| --- | --- |
| Préférences/INI | préférences C++, restauration QML, panneau des réglages |
| Mélangeur | mathématiques, neutres, pastilles QML, export, comparateur Python |
| Générateur d'images (`src/tools/`) | groupes/all, dossiers partagés, isolation, provenance TIFF, sources préservées, export 16 bits |
| Décodage RAW | décodeur, niveau noir, profils, export |
| Profils locaux | moteur DCP/LCP, décodeur, export |
| Réglages QML | contraintes de largeur, contrôles, restauration globale |
| Zoom de l'aperçu QML | clic, glisser, molette, raccourcis, bornes, rafraîchissement et chargement de la fenêtre |
| Un fichier de test isolé | ce test, pas toute la suite |

Pour le lanceur Windows local, une validation complète est imposée dans les cas suivants :

- premier passage ou checkpoint/configuration invalide : établissement du socle ;
- demande `--full-tests` ou environnement `GITHUB_ACTIONS=true` ;
- modification CMake/toolchain/workflow/infrastructure de tests ou interface
  transversale `imageeditor.h` ;
- au moins vingt fichiers de code/configuration modifiés ;
- fichier de code inconnu des règles : repli conservateur, à compléter ensuite.

GitHub utilise le même catalogue de règles, avec une portée calculée depuis
l'ancêtre commun de la branche et `origin/dev`. Les pushes de groupe sont
ciblés ; les PR vers `dev`/`main`, les pushes d'intégration, les changements
transversaux/importants et les validations manuelles sont complets. Voir la
[politique CI](ci-et-build-multiplateforme.md#politique-de-déclenchement--11-octobre-2026).
La CI ne modifie pas le checkpoint local et ne supprime aucune image.

Les dépendances de tests sont une cartographie maintenue, pas une preuve formelle
du graphe C++/QML. Les tests existants de détail/bruit, cache et vignettes ne
constituent pas encore une couverture dédiée complète : l'export/IHM fournit
un contrôle de régression, et la recette photographique reste nécessaire.

Le checkpoint n'avance qu'après compilation et succès de tous les tests retenus.
Une erreur, un build `--no-tests` ou des sources changées pendant l'exécution ne
marquent jamais le code comme testé. Une sélection manuelle `--paths` n'avance
pas non plus le checkpoint global, sauf si une suite complète est effectivement
exécutée. La première validation complète ne se répète pas à chaque lancement.

## Options utiles

```bash
# Afficher les tests prévus sans compiler, écrire de checkpoint ou exécuter.
bash scripts/build-windows.sh --dry-run

# Tester une zone précise (chemins relatifs au dépôt).
bash scripts/build-windows.sh --paths src/app/UiPreferences.cpp

# Laisser IPR ouverte : ne compiler que les cibles de tests sélectionnées.
bash scripts/build-windows.sh --tests-only --paths src/qml/Setting.qml

# Grosse modification : toute la suite.
bash scripts/build-windows.sh --full-tests

# Compilation seule, sans déclarer le code validé.
bash scripts/build-windows.sh --no-tests
```

Après succès, le script conserve aussi le plan dans `.ipr-test-plan.json`.
CTest vérifie que chaque test demandé est réellement enregistré ; un test
introuvable ou en échec entraîne un code de sortie non nul. Le format Release
est le parcours vérifié localement ; Debug requiert les dépendances correspondantes.

## GitHub

Le même lanceur force la suite complète si `GITHUB_ACTIONS=true`, même avec
`--paths`, et refuse `--no-tests` dans ce contexte. Un futur job Windows devra
d'abord installer/configurer les dépendances, puis exécuter le script.
Les workflows hébergés et le packaging restent à provisionner selon
[le plan multiplateforme](ci-et-build-multiplateforme.md) : aucun workflow
incomplet ni dépendance aux chemins personnels n'est ajouté ici.

Les compilations CI/Linux peuvent utiliser directement `cmake --build` puis
`ctest --test-dir BUILD --output-on-failure` pour toute la liste enregistrée.
Les labels `unit`, `pipeline`, `export`, `color-mixer`, `preferences`, `raw`,
`profiles`, `settings`, `qml`, `viewport`, `detail`, `noise` sont disponibles pour diagnostiquer une famille.
La validation CI complète ne doit pas devenir une exécution sélective.

## Vérification locale — 4 octobre 2026

- Premier checkpoint : build Windows Release réussi, onze tests CTest natifs
  et treize tests Python réussis (six sélecteur, sept comparateur historique).
- Contraintes de largeur des contrôles QML corrigées après détection par les
  tests natifs ; aucune assertion de largeur supprimée.
- Après extension du seul comparateur à douze états : sélection automatique
  de sa seule suite Python, **treize tests réussis** après ajout du cas de
  non-régression à douze états, aucun CTest relancé ;
  build de l'application et nouveau checkpoint réussis.
- Après succès, `--dry-run` sans changement retourne des listes vides.

Cette vérification confirme le lanceur et la sélection sur la configuration
locale ; elle ne remplace pas un essai d'installation sur une machine vierge.

### Extension aux mesures de bruit — 4 octobre 2026

La suite Python `noise_analysis` couvre `tests/analyse_noise.py` et ses tests.
Le lecteur partagé `tests/analyse_color_mixer.py` sélectionne désormais les
deux suites d'analyse (mélangeur et bruit). Le fichier de tests du mélangeur
seul n'entraîne pas la suite bruit. Sept tests du sélecteur, treize tests du
comparateur partagé et quinze tests bruit réussissent dans cette campagne.
Un plan explicite pour l'analyseur bruit ne sélectionne que `noise_analysis`.

La modification du fichier de règles reste un changement transversal : le
prochain build automatique doit faire la suite complète. Les tests ciblés de
l'analyse ne réenregistrent pas un checkpoint et ne constituent pas une
nouvelle validation complète de l'exécutable Windows.

### Correctif bruit — consolidation du 5 octobre 2026

`detailProcessingTests` est enregistré sous Windows et Linux, labels
`unit;pipeline;detail;noise`. Un changement de `DetailProcessing.*`
sélectionne cette cible, `colorPipelineTests`, `imageExportTests` et la suite
Python `noise_analysis`. Le seul test détail sélectionne sa propre cible.

Après modification CMake/cartographie, build Windows Release et validation
**complète réussis : 12 CTest, 36 tests Python** (13 comparateur, 15 bruit,
8 sélecteur). Checkpoint actualisé après succès. La fixture d'export contient
un bruit déterministe pour tester effectivement les deux réglages ; aucun
critère d'effet sur l'export supprimé. Recette photo de la nouvelle version
toujours attendue ; ce checkpoint ne valide pas sa qualité perceptuelle.

### Zoom d'aperçu — 5 octobre 2026

La nouvelle cible `imageViewportQmlTests` est enregistrée sous Windows et
Linux/WSL, labels `unit;viewport;qml`. Le composant `ImageViewport.qml`
sélectionne cette cible et `uiPreferencesQmlTests` ; son seul fichier de test
sélectionne uniquement sa cible. `Main.qml` inclut également cette régression.

Après intégration CMake et mise à jour des règles : **build Windows Release,
13/13 CTest et 39 tests Python réussis** (13 comparateur, 17 bruit,
9 sélecteur). Checkpoint actualisé après succès. Les deux tests QML ciblés
réussissent également sous WSL. Cette validation porte sur l'interaction et
les régressions ; le zoom n'ajoute pas de rendu RAW pleine résolution.

### Comparatif origine / actuel — 5 octobre 2026

Les mêmes cibles QML couvrent désormais la séparation mobile, Ctrl Maj C,
le zoom commun et des captures de sources opaques/translucides. La cible
`imageExportTests` couvre le provider d'origine et les réglages restaurés.
Modification de l'interface `imageeditor.h` : suite complète Windows
**13 CTest / 39 Python réussis**. Après le seul renforcement QML des clips,
le lanceur sélectionne uniquement `imageViewportQmlTests` et
`uiPreferencesQmlTests` : **2/2 réussis**, build et checkpoint actualisés.
