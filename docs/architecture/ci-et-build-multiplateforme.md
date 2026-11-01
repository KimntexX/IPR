# CI et livraison multiplateforme
*[Français](ci-et-build-multiplateforme.md) | [English](ci-et-build-multiplateforme_en.md)*

Implémentation du 11 octobre 2026, branche durable `patch_ci_cd` issue de `dev`.
Les changements couleur locaux non validés ne font pas partie de cette livraison.
Le paquet « système » utilise les bibliothèques installées, sans les embarquer.

## Pipelines

| Workflow | Déclenchement | Contenu |
| --- | --- | --- |
| `ci.yml` | Push dev/main/patch_*, PR vers dev/main, manuel | Tests ciblés sur les branches de groupe ; validation complète aux portes d'intégration |
| `delivery.yml` | CI réussie sur dev/main, ou manuel limité à dev/main | Commit testé exact, validation complète, puis archives standalone/système, SHA-256 et mémoire JSON |
| `linux-distributions.yml` | Manuel sur dev/main après CI réussie du commit exact | Debian 13 `.deb`, Fedora 43 `.rpm`, Arch `.pkg.tar.zst`, bibliothèques système et tests complets |

Windows x64 : MSVC 2022, Qt 6.8.3, vcpkg épinglé au commit du manifeste.
Linux standalone : Ubuntu 24.04 x64, Qt 6.8.3 ; pas un binaire universel glibc.
Ubuntu 24.04 ne fournit que Qt 6.4.2 : la CI Linux y compile donc avec le SDK
Qt 6.8.3 dans les deux modes, et aucune archive système Ubuntu 24.04 n'est produite.
Les paquets Debian 13, Fedora et Arch utilisent leurs dépôts natifs (Qt ≥ 6.8).
macOS : runners `macos-15` arm64 et `macos-15-intel` x64 ; Homebrew pour les
dépendances tierces et Qt système, SDK Qt officiel 6.8.3 pour le standalone.
Le minimum macOS effectif dépend des bouteilles Homebrew : ne pas annoncer
macOS 12 sur la seule base du support de Qt. Les versions installées figurent
dans les journaux ; Homebrew et Arch sont évolutifs, pas hermétiques.

Qt 6.8 couvre Windows 10 1809+ et Windows 11, mais le runner Windows Server
ne remplace pas une recette sur les deux systèmes utilisateurs.
[Plateformes Qt](https://doc.qt.io/qt-6.8/supported-platforms.html),
[runners GitHub](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

## Minimum Qt 6.8 effectif — 17 octobre 2026

Décision : [ADR-0016](../decisions/ADR-0016-minimum-qt-6-8.md). L'orientation
du 15 octobre ci-dessous est désormais appliquée, sur décision de l'utilisateur,
depuis `patch_light` :

- `find_package(Qt6 6.8)` pour tous les modes ; le contrôle propre au standalone
  dans `cmake/Deployment.cmake` devient inutile et est retiré.
- CI Linux : SDK Qt 6.8.3 (`install-qt-action`) en mode système comme standalone ;
  la combinaison Ubuntu 24.04 + système est exclue du packaging et de la livraison.
- Paquet `.deb` construit sur Debian 13 (Qt 6.8.2) au lieu de Debian 12 (6.4.2).
- Plus aucun chemin propre à Qt < 6.8 : le contournement de conversion RGBA64
  vers flottant, propre à Qt 6.4.2, est supprimé. Qt 6.8.3 convertit exactement.

La version de flux `QDataStream::Qt_6_4` des sidecars `.ipr` (ADR-0001) est un
**format de fichier**, pas un minimum de compilation : elle reste inchangée.

## Orientation de compatibilité Qt ≥ 6.8 — 15 octobre 2026

Orientation retenue à la suite de l'étude Linux/macOS et de l'accord utilisateur :
préparer Qt **6.8 minimum** comme cible de distribution, avec deux modes distincts.
À cette date, la trace était documentaire : CMake, CI et paquets restaient à
Qt 6.4. Elle est appliquée depuis le 17 octobre (section précédente).

- **Système** : toutes les bibliothèques Qt nécessaires à IPR doivent être
  disponibles dans une version cohérente ≥ 6.8 dans les dépôts retenus.
- **Standalone** : embarquer Qt et les dépendances applicatives permet de
  viser aussi des distributions dont le Qt système est plus ancien, sans le
  remplacer. Cela ne rend pas le paquet indépendant de la glibc, des pilotes
  graphiques et des dépendances de la plateforme.

### Cibles Linux à qualifier

Instantané documentaire au 15 octobre 2026, pas un engagement sur les versions
futures des dépôts ni une validation d'exécution d'IPR :

| Famille / cible | Qt des dépôts observé | Stratégie proposée |
| --- | --- | --- |
| Ubuntu 24.04 LTS | 6.4.2 | Standalone Qt ≥ 6.8 seulement ; plus d'archive système |
| Ubuntu 26.04 LTS | 6.10.2 | Paquet système et qualification standalone |
| Linux Mint 22.x | Base Ubuntu 24.04 | Standalone ; qualification propre à Mint requise |
| Debian 12 | 6.4.2 | Standalone seulement ; le `.deb` système passe à Debian 13 |
| Debian 13 | 6.8.2 | Cible `.deb` système depuis le 17 octobre 2026 |
| Fedora 43/44 | 6.10/6.11 | Cibles `.rpm` système ; qualifier chaque version encore maintenue au moment de livrer |
| Arch Linux | 6.12 au moment du relevé | Paquet système, avec suivi régulier des mises à jour rolling release |

Qt 6.8 référence aussi RHEL 8.10/9.6, openSUSE 15.5 et SLES 15 SP5 dans
sa matrice de configurations. Ce sont des références du framework, pas des
promesses de maintenance de ces OS par IPR, ni la preuve qu'ils distribuent
Qt 6.8 dans leurs dépôts. Les paquets actuels openSUSE n'ont pas été confirmés
dans cette étude ; cette famille reste à inventorier avant choix d'une cible.

Priorité Linux proposée : x86_64, Ubuntu 24.04/Debian 12 en standalone,
Debian 13/Ubuntu 26.04/Fedora récent avec bibliothèques système, puis Arch.
Vérifier séparément X11 et Wayland, les plugins QML/image et les autres
dépendances. Construire sur une base compatible avec le plus ancien système
annoncé ; ne pas déduire la compatibilité d'un paquet de sa seule extension
`.deb`, `.rpm` ou de la version Qt. ARM64 Linux reste une qualification distincte.

### Cible Apple/macOS à qualifier

Qt **6.8** indique macOS **12 Monterey minimum**, avec Intel x86_64 et Apple
Silicon arm64 ; sa construction demande Xcode 15 / SDK macOS 14 ou supérieur.
Apple Silicon est la priorité proposée pour IPR. Le build Intel existant
n'est ni supprimé ni déclaré validé par cette étude.

Pour une application `.app` autonome, Qt et toutes les dépendances doivent
être construits pour le minimum macOS annoncé, avec un deployment target
cohérent, puis testés, signés et notarisés selon le circuit de livraison.
Les bouteilles Homebrew actuelles peuvent imposer un minimum supérieur.
**macOS 12 est donc un plancher théorique avec Qt 6.8, pas une garantie IPR.**
Si les dépendances ne permettent pas de le tenir, retenir explicitement un
minimum plus récent après qualification.

Ne pas confondre « minimum Qt 6.8 » et « prendre automatiquement le dernier Qt » :
Qt 6.10 relève par exemple le minimum macOS à 13. Épingler le SDK de livraison
et tester la version minimale de l'OS ; les runners récents seuls ne suffisent pas.

### Sources de l'étude

Consultées le 15 octobre 2026 ; références techniques et catalogues officiels :

- [Qt 6.8, plateformes supportées](https://doc.qt.io/qt-6.8/supported-platforms.html),
  [macOS Qt 6.8](https://doc.qt.io/qt-6.8/macos.html) et
  [macOS Qt 6.10](https://doc.qt.io/qt-6.10/macos.html).
- [Ubuntu 24.04, qt6-base-dev](https://packages.ubuntu.com/noble/qt6-base-dev) et
  [Ubuntu 26.04, paquet source Qt Base](https://packages.ubuntu.com/en/source/resolute/s390x/qt6-base).
- [Linux Mint 22, base Ubuntu 24.04](https://www.linuxmint.com/rel_wilma_whatsnew.php).
- [Debian 12](https://packages.debian.org/bookworm/qt6-base-dev) et
  [Debian 13](https://packages.debian.org/trixie/qt6-base-dev).
- [Fedora, Qt Base](https://packages.fedoraproject.org/pkgs/qt6-qtbase/qt6-qtbase/) et
  [Arch, Qt Base](https://archlinux.org/packages/extra/x86_64/qt6-base/).

## Exécution

Depuis la racine du projet, Python 3.10+ et CMake/Ninja ou MSVC installés :

```bash
python -m pip install numpy Pillow PyYAML
python scripts/ci.py --action all --mode system --version 0.1.0-alpha
python scripts/ci.py --action all --mode standalone --build-dir build-portable --qt-root /chemin/Qt --version 0.1.0-alpha
python scripts/ci.py --action test --build-dir build-ci
python scripts/smoke_package.py /chemin/paquet-extrait --mode standalone
```

Windows : fournir `--qt-root`, les chemins CMake des dépendances via
`--cmake-arg=-D...`, et un ou plusieurs `--runtime-dir` contenant les DLL tierces.
Le wrapper `scripts/package-windows.ps1` appelle ce même moteur, sans implémentation
parallèle. `scripts/github_build.py` ajoute les chemins vcpkg/Homebrew des runners.
Tous les modes exigent Qt >= 6.8.

Linux natif : `--native deb`, `--native rpm` ou `--native arch`, en mode système,
sur la distribution cible. Arch produit une recette à partir du commit courant ;
le workflow la compile sans root avec `makepkg`. Ne pas utiliser cette commande
sur des sources non commitées. Les archives existantes ne sont jamais écrasées.
Choisir un nouveau dossier de sortie pour répéter une livraison.

Dans GitHub, consulter **Actions → CI scoped validation** après le push de la
branche. Les workflows manuels doivent exister dans la branche par défaut pour
apparaître dans l'interface de lancement ; leur préparation n'autorise pas à
fusionner dans `main`. Les paquets se récupèrent dans les artefacts du run.

## Tests et sécurité

En local, `build_windows.py` conserve la sélection conservative par fichiers
modifiés ; une modification transversale ou inconnue entraîne la suite complète.
La CI appelle `ci.py` avec un plan ciblé ou complet. Les tests de corpus
photographique privés ne sont pas remplacés par ces tests synthétiques.

### Politique de déclenchement — 11 octobre 2026

| Événement / modification | Validation |
| --- | --- |
| Push `patch_*`, documentation seule | Lecture UTF-8, JSON valides, liens locaux signalés ; aucune compilation native |
| Push `patch_*`, tests/analyse Python seulement | Suites Python concernées, sans installer Qt/OpenCV/LibRaw |
| Push `patch_*`, code photographique cartographié | Compilation des cibles et tests sélectionnés, Windows, Ubuntu et macOS ARM |
| Modification transversale, inconnue ou ≥20 fichiers de code | Tous les tests, quatre plateformes, Intel compris |
| Push `dev`/`main`, PR vers `dev`/`main`, lancement manuel | Tous les tests, quatre plateformes |
| Tag de version | Jalon Git immuable ; ne déclenche pas de livraison indépendante des branches dev/main |
| Push sur une branche hors `main`, `dev`, `patch_*` | Aucun déclenchement automatique ; une PR vers `dev`/`main` reste contrôlée |

`scripts/ci_scope.py` partage les règles de `scripts/test-rules.json` avec le
sélecteur local. La portée est **l'ensemble des changements de la branche depuis
son ancêtre commun avec `origin/dev`**, et non le seul dernier commit : une
validation précédente annulée ne fait pas disparaître les changements à tester.
Les renommages incluent ancien et nouveau chemins. Un historique inaccessible
impose une validation complète. Le nom `patch_documentation` n'exempte jamais
une modification de code des tests correspondants.

Le plan `ci-logs/test-plan.json` est conservé comme artefact et partagé entre les
jobs. Les noms de tests et cibles sont contrôlés ; une sélection CTest manquante
échoue au lieu de réussir avec seulement une partie de la sélection. Sans
`--test-plan`, `ci.py` exécute toujours la suite complète ; les actions de
packaging refusent un plan ciblé. Le sélecteur Windows local conserve ses
checkpoints existants ; il ne sert pas de checkpoint à GitHub.

Le job final **`CI validation`** vérifie que chaque contrôle attendu a réussi
et que seuls les contrôles réellement inutiles ont été ignorés. C'est le check
à rendre obligatoire dans les règles de protection de `dev`/`main`. Ce patch
ne modifie pas les réglages de protection du dépôt GitHub. On ne filtre pas les
PR par chemins, ce qui évite de laisser un check requis indéfiniment en attente.
[Syntaxe GitHub Actions](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax).

Le contrôle documentaire est en lecture seule, sans traduction ni compilation
LaTeX. UTF-8/JSON invalides font échouer le job ; les liens manquants sont pour
le moment des **avertissements explicites**, car les archives référencent aussi
des corpus privés supprimés et des sorties de build non versionnées. Un succès
ne certifie donc pas tous les liens, les PDF ou l'exactitude technique du texte.
La politique documentaire ne s'applique pas comme exception aux PR : celles-ci
effectuent bien la validation complète avant intégration, même pour des docs.

Validation locale du premier patch de sélection : compilation Windows réussie, **18 CTest et
163 tests Python réussis**, plus vérification réelle d'une compilation/exécution
ciblée limitée à `toneRecoveryTests`. Le code testé est `b434628` ; la
[mémoire JSON](../validation/raw/validation-ci-scope-v001-20261011T193330Z.json)
consigne les versions et limites. Ce premier patch a ensuite été publié et sa
[CI GitHub a réussi](https://github.com/KimntexX/IPR/actions/runs/38219071933).
La correction de packaging décrite ci-dessous nécessite une nouvelle validation.
Aucun nouveau tag ni réglage de protection du dépôt n'a été posé.

Tous les exécutables de test Qt sont lancés offscreen/software ; le smoke test
du paquet se déroule hors du dépôt avec des préférences temporaires et termine
normalement. Les chemins SDK sont retirés pour le standalone. Ce contrôle ne
prouve pas une autonomie totale sur une machine vierge : faire une recette
Windows 10/11, macOS des deux architectures et Linux avec interface graphique.

Actions externes épinglées par SHA, permissions `contents: read`, plus
`actions: read` seulement pour les jobs de livraison qui consultent la CI.
Pas de `pull_request_target`, de permission d'écriture ou de publication de
release automatique. Le token GitHub est utilisé en lecture seule, sans
être enregistré dans les artefacts.

Les tags suivent `v0.1.X-alpha`, puis `v0.Y.X-beta` (Y >= 2), puis `v1.Y.X`.
Le script ne crée ni tag ni fusion et ne calcule pas le nombre de patchs livrés.
Les labels manuels identifient des constructions, pas une validation stable.

### Builds récupérables réservés à dev/main — 12 octobre 2026

`delivery_policy.py` refuse toute livraison hors `dev`/`main`, y compris un
lancement manuel, un tag ou un événement de PR. Il vérifie via l'API GitHub
une CI du dépôt d'origine, terminée avec succès, portant le même SHA, avec
les quatre jobs natifs réussis et le check agrégé lorsqu'il existe. Les sources
suivies doivent être propres. Les jobs checkoutent ce **SHA**, jamais une
branche flottante qui aurait avancé pendant la validation. Le contrôle est
répété avant création des archives ; le mode local reste utilisable hors GitHub.

Les archives portent branche, SHA, plateforme et mode ; les labels alpha/beta
ne valent pas certification stable. Chaque paquet n'est téléchargeable qu'après
tests complets, déploiement et démarrage réussis. Un échec de plateforme n'est
pas masqué : les autres plateformes peuvent publier leurs paquets réussis.
Les tags restent des jalons des fusions approuvées, sans double déclenchement CD.
Les anciens runs et archives historiques ne sont pas supprimés ou réécrits.

Sur `patch_ci_cd`, un job de vérification couvre les huit combinaisons
plateforme/mode avec `--verify-package-only` : installation, contrôle et smoke,
**sans créer d'archive téléchargeable**. Seuls `test-logs-*` sont publiés par la
CI, y compris son plan de sélection. Les autres branches de groupe et PR ne
publient pas de binaires.

Activation : le workflow `workflow_run` et le lancement manuel doivent être
présents dans la branche par défaut (`main`) pour être disponibles. Actuellement
`main` ne contient pas ces workflows ; leur publication sur `patch_ci_cd`
déclenche la CI et les vérifications de packaging, pas une livraison publique.
Après validation, intégrer dans `dev`, puis déployer les workflows dans `main`
lors d'une livraison explicitement autorisée. Cette correction n'autorise
ni fusion anticipée, ni publication sur `main`.
[Événement workflow_run](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#workflow_run).

Correctifs issus des journaux des versions 16/17 :

- Windows système : chemins des DLL tierces rendus absolus avant le lancement
  depuis un répertoire temporaire hors dépôt.
- Linux standalone : déploiement depuis le chemin réel de l'exécutable compilé,
  avec le helper installé ; abandon du chemin erroné `/appIPR`.
- macOS standalone : un seul passage de `macdeployqt`, depuis le parent du
  bundle, avec scan QML ciblé et SDK Qt cohérent. Erreurs signalées par l'outil,
  exécutable manquant, liens cassés et références Mach-O à des bibliothèques
  externes non système font échouer le contrôle avant signature et publication.
  La double passe Qt/Homebrew précédente n'est pas conservée.

Sources techniques : [script de déploiement Qt](https://doc.qt.io/qt-6.8/qt-generate-deploy-script.html),
[dépendances runtime Qt](https://doc.qt.io/qt-6.8/qt-deploy-runtime-dependencies.html).
Ces changements sont des correctifs candidats : le succès Linux/macOS sera
confirmé par les jobs GitHub, pas déclaré sur les seuls tests Python.

Validation locale du correctif : **18 CTest et 173 tests Python réussis**,
Windows système et standalone déployés puis démarrés hors dépôt, sans créer
d'archive téléchargeable. Les dix tests de régression livraison contrôlent
notamment branches/SHA/CI, chemins DLL absolus et refus des dépendances macOS
externes. La [mémoire JSON](../validation/raw/livraison/audit-packaging-v001-20261012T084907Z.json)
consigne les limites. Source `9502dc8`, pas de changement du rendu photographique.
Les jobs GitHub seront lancés par le push ; à la demande de l'utilisateur,
leurs résultats ne sont pas attendus avant le compte rendu de cette action.

## Contenu des paquets et limites

Application, générateur CLI, configuration initiale, dossiers `profiles/dcp` et
`profiles/lcp` vides, licences, mémoire `package-manifest.json` (UTC, commit,
mode, dépendances, fichiers et empreintes). Aucun profil Adobe, RAW du corpus
ou `raw-normalization.json` privé. L'index public est généré depuis un modèle
vide indépendant de l'index local.

Standalone : bibliothèques dynamiques Qt et tierces ; sous Linux restent glibc,
chargeur et pilotes du système. Système : DLL/bibliothèques et modules QML
doivent être installés dans les versions ABI compatibles. Les paquets natifs
déclarent les dépendances ; les archives génériques ne les installent pas.
Windows système nécessite le runtime Visual C++ et Qt/OpenCV/LibRaw/TIFF dans
PATH. macOS système utilise Homebrew de la même architecture.

Pas de signature Authenticode, certificat Apple Developer ID ou notarisation
configurés. La signature macOS ad hoc n'est pas une signature de distribution.
Pas de publication publique automatique : choisir la licence IPR et vérifier
les notices, sources correspondantes et obligations LGPL avant une release.
Les fichiers de licences génériques ne constituent pas un audit juridique.

## Validation locale du 11 octobre 2026

Mémoire : [validation-ci-cd-20261011.json](../validation/raw/validation-ci-cd-20261011.json).
Windows MSVC/Qt 6.11.2 : 18 CTest réussis, 113 tests Python réussis ; archives
standalone et système produites, smoke tests réussis. Linux Ubuntu 24.04 sous
WSL/Qt 6.4.2 : mêmes 18 CTest et 113 tests Python réussis ; archive système et
paquet `.deb` produits, dépendances inspectées, smoke réussi.

Les essais ont nécessité deux corrections de cycle de vie : ne pas attendre un
futur Qt non démarré/consommé à la destruction de l'éditeur ; détruire les racines
QML avant leurs objets de contexte. Un test de fermeture sans image couvre le
premier cas ; le smoke de l'application couvre aussi l'ordre de destruction.
Les tests libèrent également leur memmap TIFF sous Windows et n'attendent pas
une boucle d'évènements après une erreur d'export synchrone.

Pas de validation locale macOS, Fedora, Arch ou standalone Linux Qt 6.8.3.
La matrice GitHub devra confirmer ces chemins ; ces résultats locaux ne valent
pas certification des machines cibles ni validation des fonctions photographiques.
La compilation actuelle de `patch_couleur` et les images de comparaison sont
restées intactes. Cet état local précédait la publication et la validation GitHub.

## Validation GitHub et livraison du 11 octobre 2026

La [CI native 38166255714](https://github.com/KimntexX/IPR/actions/runs/38166255714)
est réussie sur Windows, Ubuntu, macOS Apple Silicon et macOS Intel :
18 CTest et 113 tests Python par plateforme. La branche testée est
`patch_ci_cd`, commit `5682bcbd78534532c7cdc03262ce24d457167220`.
Le patch CI/CD est intégré dans `dev` avec fusion `--no-ff` et tag
`v0.1.16-alpha`, conformément au compteur confirmé par l'utilisateur.
`main` reste inchangée ; la branche de groupe est conservée.

Les corrections de portabilité concernent les chemins canoniques des profils
(`/var` et `/private/var` sur macOS) et la destruction du root QML avant sa
fenêtre dans le test de zoom/comparatif. Les compilations et tests natifs
ne certifient pas les paquets de livraison, les conteneurs Fedora/Arch,
ni l'ergonomie sur une machine utilisateur ; ces validations restent distinctes.

## Ajout du 21 octobre 2026 — installateur Windows

`--windows-installer` complète le packaging Windows standalone. Le constructeur
`scripts/windows_installer.py` compile `packaging/windows/ipr.iss` depuis le
staging déjà vérifié, sans redéployer Qt ou les DLL. Le ZIP reste disponible ;
les autres OS et le mode system sont inchangés.

La vérification de `patch_ci_cd` compile et teste le setup temporairement,
sans publier de binaire. Les livraisons `dev`/`main` ajoutent le setup et son
SHA-256 aux archives du commit validé. Inno Setup 6.7.3 est installé pour
Windows standalone seulement. Le test d’installation est réservé aux runners
GitHub hébergés jetables ; il refuse une installation IPR préexistante.
Il couvre installation, réinstallation, démarrage et désinstallation, pas
la mise à niveau depuis chaque version historique ni une validation photographique.

Les préférences installées passent par `QStandardPaths::AppConfigLocation`,
et les profils personnels par `AppDataLocation/profiles`, hors du désinstalleur.
La signature de distribution reste à organiser.
[Procédure, commandes et limites](build-windows-standalone.md).

## Références de déploiement

- [Qt : déploiement QML CMake](https://doc.qt.io/qt-6.8/qt-generate-deploy-qml-app-script.html).
- [Qt : dépendances à l'exécution](https://doc.qt.io/qt-6.8/qt-deploy-runtime-dependencies.html).
- [Qt : imports QML](https://doc.qt.io/qt-6.8/qt-deploy-qml-imports.html).
- [CMake : CPack Debian](https://cmake.org/cmake/help/latest/cpack_gen/deb.html).
- [CMake : CPack RPM](https://cmake.org/cmake/help/latest/cpack_gen/rpm.html).
- [Arch : création de paquets](https://wiki.archlinux.org/title/Creating_packages).
