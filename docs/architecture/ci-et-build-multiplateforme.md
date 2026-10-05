# CI et builds multiplateformes

**Statut :** proposition prête à implémenter lors de la création du dépôt GitHub.  
**Portée :** Windows 10/11, macOS, Debian/Ubuntu, Fedora et Arch Linux.

## Objectif

La CI (*continuous integration*) exécute automatiquement les vérifications à chaque proposition de changement : configuration CMake, compilation, tests, analyse de la documentation et conservation des journaux utiles. Elle ne publie pas un logiciel par défaut.

La CD (*continuous delivery*) est la seconde étape : pour un tag de version validé, elle produit les paquets installables, leur somme de contrôle et, plus tard, leur signature. Cette séparation évite de confondre un binaire de test avec une publication destinée aux utilisateurs.

## État actuel et écarts à traiter

Le projet est déjà portable dans son principe : CMake 3.21+, C++17, Qt 6.4+, OpenCV, LibTIFF et LibRaw optionnel. En revanche, il faut résoudre les écarts suivants avant de déclarer le support multiplateforme :

| Sujet | État actuel | Travail requis |
| --- | --- | --- |
| Tests | Les onze cibles synthétiques sont enregistrées sur Windows et UNIX avec labels et environnement Qt headless | Vérifier aussi macOS ; les sondes de corpus privé restent UNIX et hors du socle CI public. |
| Contrats Qt | Core/Gui/Quick/Concurrent sont demandés explicitement | Vérifier les futurs presets et leurs versions de dépendances. |
| Dépendances reproductibles | Les chemins et gestionnaires de paquets ne sont pas encore centralisés | Ajouter `CMakePresets.json`, un manifeste vcpkg pour Windows et des scripts d’amorçage Linux/macOS versionnés. |
| Installation | `install()` installe l’exécutable, mais pas le runtime Qt ni les bibliothèques tierces | Créer une phase de *staging* avec `cmake --install`, puis un script de déploiement spécifique à la plateforme. |
| Paquets | Aucun générateur de paquet ni métadonnée de distribution | Ajouter CPack et/ou les recettes natives après choix de licence, nom d’éditeur, icône et identifiant final. |
| Données privées | Le corpus RAW et les profils tiers ne doivent pas être publiés | Tests CI uniquement avec ressources synthétiques, profils autorisés et corpus public explicitement licencié. |

## Ce que la CI fera

### Politique locale déjà implémentée

[Le lanceur Bash Windows](tests-cibles-et-build-windows.md) compile via les outils
natifs et applique une sélection par zone depuis un checkpoint de code validé.
Les builds ordinaires exécutent seulement les tests concernés. Premier passage,
infrastructure/interface partagée, grosse modification ou zone inconnue : suite
complète. `GITHUB_ACTIONS=true` et `--full-tests` imposent aussi toute la liste.
Cette préparation ne provisionne pas encore les runners ou les dépendances GitHub.

### 1. Contrôles rapides à chaque pull request

1. Vérification de format et de cohérence CMake/QML/C++ ; `git diff --check`.
2. Contrôle des liens Markdown locaux et compilation de la documentation.
3. Configuration CMake, compilation et CTest du socle sans LibRaw sur Windows, macOS et Ubuntu.
4. Conservation en artefact des journaux CMake/CTest et, en cas d’échec, des rapports utiles.

Cette première barrière est courte et ne manipule ni corpus photo privé, ni profils DCP/LCP non redistribuables, ni secret de publication.

### 2. Intégration native

Une seconde matrice valide l’import RAW avec LibRaw et les exports TIFF/DNG :

| Cible | Exécution | But |
| --- | --- | --- |
| Ubuntu x86_64 | runner GitHub | compilation et tests LibRaw sur la plateforme Linux de référence |
| Windows x86_64 | runner GitHub | compilation MSVC et tests headless Windows |
| macOS x86_64 puis arm64 | runner GitHub si disponible pour le dépôt | compilation, bundle et test de démarrage headless |
| Debian/Ubuntu | conteneur Debian stable et Ubuntu LTS | vérifier paquets DEB et ABI système |
| Fedora | conteneur Fedora courant | vérifier RPM et dépendances RPM |
| Arch | conteneur Arch Linux | vérifier `PKGBUILD` et dépendances rolling release |

Les conteneurs valident la compatibilité de paquets Linux ; ils ne remplacent pas une recette d’interface graphique sur une machine utilisateur réelle. GitHub propose des runners hébergés Linux, Windows et macOS, mais leurs images évoluent chaque semaine : la CI installera donc les versions demandées plutôt que de dépendre implicitement des logiciels préinstallés.

### 3. Publication contrôlée par tag

Sur un tag signé `vX.Y.Z`, après passage de toute la matrice :

- générer les paquets et archives ;
- lancer un test de démarrage du répertoire déployé ;
- produire SHA-256, manifeste de composants et notes de version ;
- publier uniquement après validation manuelle du workflow ;
- signer et notariser seulement lorsque les certificats et la politique de publication auront été définis.

Les secrets de signature ne doivent jamais être accessibles à une pull request externe.

## Matrice de build recommandée

| Famille | Compilateur et architecture de départ | Distribution cible | Format de livraison initial |
| --- | --- | --- | --- |
| Windows 10/11 | MSVC 2022, x86_64 | Windows 10 22H2 et Windows 11 supportés par Qt | ZIP déployé, puis installateur MSIX ou NSIS/WiX |
| macOS | Apple Clang, arm64 et x86_64 | macOS supporté par la version Qt retenue | `.app` compressée ; DMG après signature/notarisation |
| Debian/Ubuntu | GCC, x86_64 | Debian stable, Ubuntu LTS | `.deb` et archive `tar.zst` |
| Fedora | GCC, x86_64 | Fedora stable courant | `.rpm` et archive `tar.zst` |
| Arch | GCC, x86_64 | Arch courant | `PKGBUILD` et archive `tar.zst` |

Le premier objectif est x86_64 sur toutes les plateformes. macOS arm64 doit être un build natif séparé ; un binaire `universal2` ne sera livré qu’après validation de ses deux architectures. ARM64 Windows et Linux peuvent être ajoutés plus tard comme cibles expérimentales, sans retarder les versions x86_64.

## Prérequis de compilation

| Plateforme | Outils communs | Qt | Bibliothèques IPR |
| --- | --- | --- | --- |
| Windows | CMake >= 3.21, Ninja, MSVC Build Tools 2022, vcpkg | Qt 6 MSVC x64, même toolchain que les dépendances | OpenCV, LibTIFF, LibRaw via vcpkg ou builds cohérents MSVC |
| macOS | CMake, Ninja, Xcode Command Line Tools, Homebrew ou gestionnaire Qt officiel | Qt 6 pour l’architecture ciblée | OpenCV, libtiff, LibRaw ; attention aux préfixes Homebrew arm64/x86_64 |
| Debian/Ubuntu | CMake, Ninja, GCC/G++, `pkg-config` | `qt6-base-dev`, `qt6-declarative-dev` | `libopencv-dev`, `libtiff-dev`, `libraw-dev` |
| Fedora | CMake, Ninja, GCC/G++, `pkgconf-pkg-config` | `qt6-qtbase-devel`, `qt6-qtdeclarative-devel` | `opencv-devel`, `libtiff-devel`, `LibRaw-devel` |
| Arch | CMake, Ninja, `base-devel`, `pkgconf` | `qt6-base`, `qt6-declarative` | `opencv`, `libtiff`, `libraw` |

Les noms de paquets sont les noms usuels à vérifier dans l’image ciblée lors de l’implémentation. Les scripts d’amorçage devront échouer clairement si un paquet ou une version est absent, plutôt que de continuer avec un ensemble incomplet.

## Déploiement et packaging

### Windows

Compiler avec une seule ABI, recommandée ici : **MSVC x64**. Ne jamais mélanger Qt MinGW et bibliothèques OpenCV/LibRaw MSVC. Après `cmake --install`, exécuter `windeployqt --qmldir <racine-qml>` sur `appIPR.exe`, puis ajouter les DLL OpenCV, LibRaw et LibTIFF non prises en charge par l’outil Qt. Le redistribuable officiel Microsoft Visual C++ est requis pour une distribution MSVC.

Le test de livraison s’exécute depuis un répertoire vierge, sans Qt dans le `PATH`, et vérifie au minimum le démarrage de l’application et le chargement QML. Le package final doit aussi préciser l’architecture x64 et la version.

### macOS

Produire un bundle `.app` avec `macdeployqt` ou le mécanisme de déploiement CMake de Qt, puis vérifier les dépendances avec `otool -L` et exécuter l’application depuis le bundle. La distribution hors Mac App Store requiert ensuite un compte Apple, une identité de signature Developer ID, l’horodatage et la notarisation ; ce sont des prérequis de publication, pas de CI ordinaire.

### Linux

Ne pas imposer un unique binaire portable à toutes les distributions lors de la première livraison. Livrer d’abord les formats natifs : DEB pour Debian/Ubuntu, RPM pour Fedora et recette `PKGBUILD` pour Arch. Chaque recette déclare les dépendances système Qt/OpenCV/LibTIFF/LibRaw et place les données d’application dans les emplacements standard. Une AppImage pourra être évaluée ultérieurement, après audit des licences et des bibliothèques embarquées.

CPack est un bon socle commun : il fabrique ses paquets à partir de ce qui est déclaré par `install()`. Il faut donc terminer la phase d’installation et de déploiement avant d’activer les générateurs CPack.

## Structure GitHub proposée

```text
.github/
  workflows/
    ci.yml                 # format, liens, build + CTest multiplateforme
    docs.yml               # LaTeX/Scholar, artefact PDF
    release.yml            # tags seulement, paquets et publication contrôlée
    codeql.yml             # à activer après le socle CI
cmake/
  packaging/               # CPack, scripts de staging et métadonnées
scripts/
  bootstrap/               # win, macOS, Debian, Fedora, Arch
  deploy/                  # win, macOS, Linux
CMakePresets.json
vcpkg.json
```

Les actions tierces seront épinglées à un commit SHA révisé, les jobs utiliseront `permissions: contents: read` par défaut et le cache ne portera que sur les téléchargements de dépendances. Les répertoires de build ne seront pas réutilisés comme cache de vérité.

## Séquence d’implémentation

1. **Portabilité CMake locale** : presets, modules Qt explicites, tests sans garde `UNIX`, exécution headless et corpus synthétique.
2. **Scripts d’amorçage** : une commande documentée par plateforme et build reproductible local.
3. **CI GitHub niveau 1** : Ubuntu, Windows et macOS ; compilation, CTest, liens et documentation ; aucun package publié.
4. **Packaging** : installation CMake complète, déploiement Qt, recettes DEB/RPM/PKGBUILD et artefacts de test.
5. **Release** : version, licence du projet, identifiants d’application, signature Windows/macOS, clés de dépôt Linux et politique de rétention.

## Conditions bloquantes avant diffusion publique

- Choisir et ajouter une licence pour le code IPR.
- Définir l’éditeur, le domaine inverse/identifiant d’application et les icônes définitives.
- Confirmer la politique de distribution des DLL et bibliothèques, en particulier OpenCV, LibRaw et Qt.
- Ne pas inclure les profils Adobe, les RAW privés ni les exports de test dans le dépôt ou les artefacts.
- Définir qui possède les certificats de signature et qui peut déclencher une release.

## Références

- [GitHub Actions — construire et tester du C++](https://docs.github.com/en/actions/use-cases-and-examples/building-and-testing/building-and-testing-cpp)
- [GitHub Actions — syntaxe des workflows](https://docs.github.com/en/actions/writing-workflows/workflow-syntax-for-github-actions)
- [GitHub Actions — runners hébergés](https://docs.github.com/en/actions/concepts/runners/github-hosted-runners)
- [GitHub Actions — cache de dépendances](https://docs.github.com/en/actions/concepts/workflows-and-actions/dependency-caching)
- [Qt — déploiement Windows](https://doc.qt.io/qt-6/windows-deployment.html)
- [Qt — déploiement macOS](https://doc.qt.io/qt-6/macos-deployment.html)
- [Qt — déploiement Linux](https://doc.qt.io/qt-6/linux-deployment.html)
- [Qt — déploiement avec CMake](https://doc.qt.io/qt-6/cmake-deployment.html)
- [CMake — CPack](https://cmake.org/cmake/help/latest/module/CPack.html)
