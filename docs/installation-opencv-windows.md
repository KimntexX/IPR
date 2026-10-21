# Installer OpenCV sous Windows pour IPR

Ce guide couvre deux méthodes : **vcpkg** (recommandée si le projet utilise le kit Qt MSVC) et la **compilation manuelle** (recommandée si le projet conserve le kit Qt MinGW déjà installé).

> La règle non négociable : Qt, OpenCV, LibRaw et l'exécutable doivent être compilés avec un compilateur et une architecture compatibles. Ne liez jamais une bibliothèque MSVC à une application Qt MinGW, ni une bibliothèque 32 bits à une application 64 bits.

## 1. Identifier le kit Qt utilisé

Dans Qt Creator : `Édition > Préférences > Kits`, puis regarder le kit sélectionné par le projet.

| Kit Qt | Compilateur | Méthode conseillée | Triplet vcpkg |
| --- | --- | --- | --- |
| `Desktop Qt … MSVC 64-bit` | Microsoft Visual C++ | vcpkg | `x64-windows` |
| `Desktop Qt … MinGW 64-bit` | MinGW-w64 fourni par Qt | Compilation manuelle avec ce MinGW | Ne pas utiliser `x64-windows` |

Le projet IPR était configuré avec un kit `Desktop Qt 6.7.2 MinGW 64-bit`, désormais inférieur au minimum Qt 6.8 ([ADR-0016](decisions/ADR-0016-minimum-qt-6-8.md)) : choisir un kit Qt 6.8 ou plus récent. Le chemin **sans vcpkg / MinGW** de ce document reste le plus direct avec un kit MinGW. Si vous préférez vcpkg, installez aussi un kit Qt `MSVC 64-bit` dans le Qt Online Installer et ouvrez IPR avec ce kit.

## 2. Méthode recommandée avec vcpkg (Qt MSVC)

### Conditions préalables

Installer avant de commencer :

- Windows 10/11 64 bits ;
- [Git for Windows](https://git-scm.com/download/win) ;
- [CMake](https://cmake.org/download/) 3.21 ou supérieur, accessible dans `PATH` ;
- Visual Studio 2022 Community ou Build Tools, avec la charge **Desktop development with C++**, MSVC v143 et un Windows SDK ;
- Qt 6.8 ou plus récent, installé avec un kit **MSVC 64-bit** compatible avec Visual Studio ;
- accès Internet à GitHub lors du bootstrap et de l'installation des ports ;
- suffisamment d'espace disque : OpenCV et ses dépendances peuvent nécessiter plusieurs Go pendant la compilation.

Ouvrir ensuite **Developer PowerShell for VS 2022**. Éviter `C:\Program Files`, les répertoires synchronisés et les chemins très longs ; `C:\dev\vcpkg` est un emplacement simple.

### Installer vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
Set-Location C:\dev\vcpkg
.\bootstrap-vcpkg.bat -disableMetrics

# Variable disponible dans le terminal courant et dans les futurs terminaux.
$env:VCPKG_ROOT = 'C:\dev\vcpkg'
[Environment]::SetEnvironmentVariable('VCPKG_ROOT', 'C:\dev\vcpkg', 'User')

& "$env:VCPKG_ROOT\vcpkg.exe" version
```

`bootstrap-vcpkg.bat` télécharge le binaire vcpkg officiel. Si un proxy d'entreprise bloque cette étape, le configurer pour Git/WinHTTP selon les règles de l'organisation ; ne pas désactiver la vérification TLS pour contourner le problème.

### Installer les bibliothèques

Depuis n'importe quel dossier :

```powershell
& "$env:VCPKG_ROOT\vcpkg.exe" install opencv4:x64-windows tiff:x64-windows

# Nécessaire à IPR pour ouvrir CR2/CR3/DNG.
& "$env:VCPKG_ROOT\vcpkg.exe" install libraw:x64-windows

& "$env:VCPKG_ROOT\vcpkg.exe" list | Select-String 'opencv4|libraw|tiff'
```

Le port `opencv4` inclut les modules nécessaires à IPR : `core`, `imgproc` et `imgcodecs`. Le port `tiff` fournit LibTIFF, nécessaire à l’export DNG linéaire. N'ajouter `opencv4[contrib]` que si le projet emploie réellement des modules contrib.

### Configurer et compiler IPR

Depuis la racine du projet, avec le kit Qt MSVC actif :

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DIPR_ENABLE_LIBRAW=ON

cmake --build build-msvc --config Debug
```

Dans Qt Creator, sélectionner le kit MSVC puis ajouter ces deux entrées dans les variables CMake du kit/projet :

```text
CMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake
VCPKG_TARGET_TRIPLET=x64-windows
```

Ces variables doivent être présentes **avant la première configuration** CMake. Si vous les ajoutez après une configuration, supprimer seulement le dossier de build concerné (par exemple `build-msvc`) puis reconfigurer.

### Exécution et déploiement

Le toolchain vcpkg copie normalement les DLL de dépendances près de l'exécutable Windows (`VCPKG_APPLOCAL_DEPS` est activé par défaut). Il ne déploie pas Qt. Pour une distribution, lancer `windeployqt` provenant du **même** kit Qt MSVC sur le dossier contenant `appIPR.exe`, puis vérifier que les DLL OpenCV, LibRaw et LibTIFF sont également présentes.

## 3. Avec Qt MinGW : état de vcpkg

vcpkg offre des triplets communautaires `x64-mingw-dynamic` et `x64-mingw-static`. Toutefois, ils recherchent normalement des outils préfixés `x86_64-w64-mingw32-gcc` / `x86_64-w64-mingw32-g++`, typiquement dans un environnement MSYS2. Le MinGW livré par le Qt Online Installer utilise généralement `gcc.exe` et `g++.exe` sans ce préfixe.

Par conséquent, ne mélangez pas automatiquement les paquets `x64-mingw-*` de vcpkg avec le kit MinGW de Qt. Deux options cohérentes existent :

1. passer le projet au kit Qt MSVC et suivre la méthode vcpkg ci-dessus ;
2. conserver le kit Qt MinGW et compiler OpenCV manuellement avec **exactement** le compilateur de ce kit (section suivante).

Une configuration avancée vcpkg + MinGW nécessite un triplet/toolchain personnalisé ; elle sort du cadre d'un premier environnement reproductible.

## 4. Sans vcpkg : compiler OpenCV avec le MinGW de Qt

### Conditions préalables

- Git et CMake 3.21+ installés ;
- Ninja recommandé ;
- un kit Qt MinGW 64 bits déjà fonctionnel ;
- le dossier `bin` de ce MinGW ajouté temporairement au `PATH` pour le terminal de build ;
- accès Internet pour cloner OpenCV.

Adapter les deux chemins ci-dessous à la version réellement installée par Qt. Pour le kit IPR historique, ils sont généralement :

```text
C:\Qt\Tools\mingw1120_64\bin\gcc.exe
C:\Qt\Tools\mingw1120_64\bin\g++.exe
```

Vérifier avant de poursuivre :

```powershell
$env:Path = 'C:\Qt\Tools\mingw1120_64\bin;' + $env:Path
gcc --version
g++ --version
cmake --version
ninja --version
```

### Récupérer et compiler OpenCV

Dans PowerShell :

```powershell
git clone --depth 1 --branch 4.x https://github.com/opencv/opencv.git C:\dev\opencv

$env:Path = 'C:\Qt\Tools\mingw1120_64\bin;' + $env:Path

cmake -S C:\dev\opencv -B C:\dev\opencv-build-mingw -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER='C:/Qt/Tools/mingw1120_64/bin/gcc.exe' `
  -DCMAKE_CXX_COMPILER='C:/Qt/Tools/mingw1120_64/bin/g++.exe' `
  -DCMAKE_INSTALL_PREFIX='C:/Libraries/opencv-qt-mingw' `
  -DBUILD_SHARED_LIBS=ON `
  -DBUILD_TESTS=OFF `
  -DBUILD_PERF_TESTS=OFF `
  -DBUILD_EXAMPLES=OFF `
  -DBUILD_opencv_apps=OFF

cmake --build C:\dev\opencv-build-mingw --parallel
cmake --install C:\dev\opencv-build-mingw
```

Ce réglage construit les DLL partagées d'OpenCV et les modules utilisés par IPR. Ne compilez pas OpenCV avec une version différente de MinGW « parce qu'elle est déjà dans PATH » : l'ABI et les runtimes C++ peuvent diverger.

### Indiquer OpenCV à IPR

Trouver le fichier de configuration généré :

```powershell
Get-ChildItem C:\Libraries\opencv-qt-mingw -Recurse -Filter OpenCVConfig.cmake
```

La valeur de `OpenCV_DIR` est le **dossier qui contient** ce fichier. Exemple, à adapter au résultat de la commande précédente :

```powershell
$env:Path = 'C:\Qt\Tools\mingw1120_64\bin;C:\Libraries\opencv-qt-mingw\x64\mingw\bin;' + $env:Path

cmake -S . -B build-mingw -G Ninja `
  -DCMAKE_PREFIX_PATH='C:/Qt/6.7.2/mingw_64' `
  -DCMAKE_C_COMPILER='C:/Qt/Tools/mingw1120_64/bin/gcc.exe' `
  -DCMAKE_CXX_COMPILER='C:/Qt/Tools/mingw1120_64/bin/g++.exe' `
  -DOpenCV_DIR='C:/Libraries/opencv-qt-mingw/x64/mingw/lib' `
  -DIPR_ENABLE_LIBRAW=OFF

cmake --build build-mingw --parallel
```

Dans Qt Creator, utiliser le kit MinGW correspondant et ajouter `OpenCV_DIR=<dossier contenant OpenCVConfig.cmake>` aux variables CMake. Laisser `IPR_ENABLE_LIBRAW=OFF` tant que LibRaw ne subit pas le même traitement de compilation/compatibilité.

### Ajouter LibRaw manuellement (optionnel)

Pour CR2/CR3/DNG sans vcpkg, compiler/installer LibRaw avec le même MinGW, puis ajouter son dossier `include` et sa bibliothèque à la recherche CMake. Le `CMakeLists.txt` d'IPR recherche automatiquement `libraw/libraw.h` et une bibliothèque nommée `raw` ou `libraw` ; lorsqu'il les trouve, il active `IPR_WITH_LIBRAW`.

Si CMake ne le détecte pas, renseigner explicitement :

```powershell
cmake -S . -B build-mingw `
  -DLIBRAW_INCLUDE_DIR='C:/Libraries/libraw-qt-mingw/include' `
  -DLIBRAW_LIBRARY='C:/Libraries/libraw-qt-mingw/lib/libraw.lib' `
  -DIPR_ENABLE_LIBRAW=ON
```

Les noms et extensions de bibliothèque varient selon le build MinGW (`.a`, `.dll.a`, `.lib`) : utiliser le fichier réellement généré, pas nécessairement l'exemple ci-dessus.

## 5. Configuration CMake attendue dans IPR

Le projet contient déjà les lignes nécessaires :

```cmake
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

target_link_libraries(appIPR
    PRIVATE
        Qt6::Quick
        Qt6::Concurrent
        ${OpenCV_LIBS}
)
```

Ne pas ajouter manuellement `include_directories` ou `link_directories` à l'aveugle. `find_package(OpenCV ...)` exploite `OpenCVConfig.cmake` et fournit les chemins corrects pour le build actif.

## 6. Dépannage

| Symptôme | Cause probable | Correction |
| --- | --- | --- |
| `Could not find OpenCVConfig.cmake` | OpenCV absent ou `OpenCV_DIR` incorrect | Avec vcpkg, passer le toolchain à CMake ; sans vcpkg, pointer `OpenCV_DIR` vers le dossier du fichier trouvé. |
| `LNK2038`, `unresolved external`, `undefined reference` | Compilateurs, architecture ou runtime différents | Recompiler toutes les dépendances avec le kit Qt sélectionné ; utiliser un nouveau dossier de build. |
| `appIPR.exe` ne démarre pas, DLL manquante | DLL OpenCV / Qt absentes du chemin d'exécution | Ajouter les dossiers `bin` au `PATH` pour le développement ; utiliser `windeployqt` et copier les DLL à la distribution. |
| CMake garde l'ancien OpenCV ou ancien compilateur | Cache CMake | Supprimer le seul dossier de build concerné puis reconfigurer avec toutes les options. |
| vcpkg MinGW ne trouve pas le compilateur | Triplet communautaire incompatible avec le MinGW de Qt | Utiliser Qt MSVC + `x64-windows`, ou la compilation manuelle MinGW. |
| TIFF ne s'ouvre/exporte pas | Plugin Qt Image Formats absent au runtime | Installer/déployer les plugins Qt `imageformats`, puis vérifier les codecs de l'installation Qt. |

## Références

- [vcpkg avec CMake — Microsoft Learn](https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/cmake-integration)
- [Triplets MinGW de vcpkg — Microsoft Learn](https://learn.microsoft.com/en-us/vcpkg/users/platforms/mingw)
- [Dépôt officiel vcpkg](https://github.com/microsoft/vcpkg)
- [Tutoriel officiel OpenCV : build Windows avec MSYS2](https://docs.opencv.org/5.0/tutorials/introduction/windows_msys2_vscode/window_install_opencv.html)
