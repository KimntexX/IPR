# CI et livraison multiplateforme

Implémentation du 11 octobre 2026, branche durable `patch_ci_cd` issue de `dev`.
Les changements couleur locaux non validés ne font pas partie de cette livraison.
Le paquet « système » utilise les bibliothèques installées, sans les embarquer.

## Pipelines

| Workflow | Déclenchement | Contenu |
| --- | --- | --- |
| `ci.yml` | Push dev/main/patch_*, PR, manuel | Compilation et **tous** les CTest + tests Python, quatre plateformes |
| `delivery.yml` | Manuel ou tag conforme | Même validation complète, puis archives standalone et système, SHA-256 et mémoire JSON |
| `linux-distributions.yml` | Manuel | Debian 12 `.deb`, Fedora 43 `.rpm`, Arch `.pkg.tar.zst`, bibliothèques système et tests complets |

Windows x64 : MSVC 2022, Qt 6.8.3, vcpkg épinglé au commit du manifeste.
Linux standalone : Ubuntu 24.04 x64, Qt 6.8.3 ; pas un binaire universel glibc.
Linux système utilise Qt fourni par apt, sans imposer un SDK Qt externe à
l'utilisateur ; les paquets Debian/Fedora/Arch utilisent aussi leurs dépôts natifs.
macOS : runners `macos-15` arm64 et `macos-15-intel` x64, dépendances Homebrew.
Le minimum macOS effectif dépend des bouteilles Homebrew : ne pas annoncer
macOS 12 sur la seule base du support de Qt. Les versions installées figurent
dans les journaux ; Homebrew et Arch sont évolutifs, pas hermétiques.

Qt 6.8 couvre Windows 10 1809+ et Windows 11, mais le runner Windows Server
ne remplace pas une recette sur les deux systèmes utilisateurs.
[Plateformes Qt](https://doc.qt.io/qt-6.8/supported-platforms.html),
[runners GitHub](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

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
Un standalone exige Qt >= 6.8 ; les builds système conservent Qt >= 6.4.

Linux natif : `--native deb`, `--native rpm` ou `--native arch`, en mode système,
sur la distribution cible. Arch produit une recette à partir du commit courant ;
le workflow la compile sans root avec `makepkg`. Ne pas utiliser cette commande
sur des sources non commitées. Les archives existantes ne sont jamais écrasées.
Choisir un nouveau dossier de sortie pour répéter une livraison.

Dans GitHub, consulter **Actions → CI native full tests** après le push de la
branche. Les workflows manuels doivent exister dans la branche par défaut pour
apparaître dans l'interface de lancement ; leur préparation n'autorise pas à
fusionner dans `main`. Les paquets se récupèrent dans les artefacts du run.

## Tests et sécurité

En local, `build_windows.py` conserve la sélection conservative par fichiers
modifiés ; une modification transversale ou inconnue entraîne la suite complète.
La CI appelle directement `ci.py`, sans sélection réduite. Les tests de corpus
photographique privés ne sont pas remplacés par ces tests synthétiques.

Tous les exécutables de test Qt sont lancés offscreen/software ; le smoke test
du paquet se déroule hors du dépôt avec des préférences temporaires et termine
normalement. Les chemins SDK sont retirés pour le standalone. Ce contrôle ne
prouve pas une autonomie totale sur une machine vierge : faire une recette
Windows 10/11, macOS des deux architectures et Linux avec interface graphique.

Actions externes épinglées par SHA, permissions `contents: read`, pas de
`pull_request_target`, pas de secrets ni publication de release automatique.
Une livraison sur tag vérifie son format, son identité avec HEAD, sa présence
dans l'histoire de `origin/dev` et l'existence d'un commit de fusion. La validation
humaine reste indispensable : une ascendance Git n'est pas une preuve qualité.

Les tags suivent `v0.1.X-alpha`, puis `v0.Y.X-beta` (Y >= 2), puis `v1.Y.X`.
Le script ne crée ni tag ni fusion et ne calcule pas le nombre de patchs livrés.
Les labels manuels identifient des constructions, pas une validation stable.

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
restées intactes. Aucun merge ni tag de validation n'est livré avec cette branche.

## Références de déploiement

- [Qt : déploiement QML CMake](https://doc.qt.io/qt-6.8/qt-generate-deploy-qml-app-script.html).
- [Qt : dépendances à l'exécution](https://doc.qt.io/qt-6.8/qt-deploy-runtime-dependencies.html).
- [Qt : imports QML](https://doc.qt.io/qt-6.8/qt-deploy-qml-imports.html).
- [CMake : CPack Debian](https://cmake.org/cmake/help/latest/cpack_gen/deb.html).
- [CMake : CPack RPM](https://cmake.org/cmake/help/latest/cpack_gen/rpm.html).
- [Arch : création de paquets](https://wiki.archlinux.org/title/Creating_packages).
