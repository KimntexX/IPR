# Banc de séparation du développement RAW

Périmètre actuel : capture après `unpack()`, comparateur et sonde de module
raccordée à l'observateur C2 de `RawDevelopment`. La sélection de calibration
de production, le rendu DCP et les replis C3 restent à qualifier séparément.
Aucun PASS global n'est possible avec les seules frontières communes du module.

## Construction isolée

Configurer `tests/raw_bench` comme source CMake et `build-patch_raw_bench`
comme build, avec Qt6 Core, LibRaw et Python/NumPy. Aucun moteur de l'autre
worktree n'est lié et aucun cache CMake n'est réutilisé. Exemples Windows :

```powershell
cmake -S tests/raw_bench -B build-patch_raw_bench -DQt6_DIR=C:/Qt/6.11.2/msvc2022_64/lib/cmake/Qt6 -DLIBRAW_INCLUDE_DIR=C:/dev/vcpkg/installed/x64-windows/include -DLIBRAW_LIBRARY=C:/dev/vcpkg/installed/x64-windows/lib/manual-link/raw.lib
cmake --build build-patch_raw_bench --config Release
ctest --test-dir build-patch_raw_bench -C Release --output-on-failure
```

Choisir explicitement `Python3_EXECUTABLE` si nécessaire. Les DLL Qt/LibRaw
doivent être dans le PATH du processus de capture. Pas de déploiement ou
modification globale du PATH utilisateur.

## Capturer et comparer

```text
iprRawUnpackCapture --input image.CR3 --output capture.json --mode parity
iprRawUnpackCapture --input image.CR2 --output capture.json --roi 100,100,256,256
python tests/raw_bench/capture.py reference.json candidat.json --output comparaison.json
```

Sans ROI : trois carrés256 à20/50/80 % de largeur, mi-hauteur, origine paire.
`--full` capture explicitement toute la mosaïque visible. Le repère est celui
du capteur, avant orientation. Les pixels DN u16 sont little-endian, rangées
contiguës, avec SHA256 et contexte CFA. Les sorties existantes sont refusées.
Une erreur peut laisser un binaire incomplet : sans manifeste final valide,
il ne constitue pas une capture utilisable et ne doit pas être réutilisé.

Le comparateur exige par défaut `unpacked_cfa`, `pre_demosaic_cfa`,
`post_median_camera_rgb`, `working_rec2020`. Une comparaison volontairement
limitée à la mosaïque utilise `--require-stage unpacked_cfa` : ce PASS reste
strictement limité à cette frontière. Étapes ou ROI manquantes, contexte
différent, pixels altérés et NaN/Inf ne passent pas silencieusement.

Le fichier facultatif `--tolerances` mappe un nom d'étape sur `atol`/`rtol`.
La valeur par défaut est zéro ; les frontières u16 interdisent une tolérance
non nulle. Toute tolérance flottante devra être justifiée et convenue avant
la campagne. `--purpose measurement` mesure les différences attendues du
mode signé et donne `measured_not_parity`, jamais un PASS de parité A.

## Sonde C2 et campagne de parité ciblée

La configuration CMake utilise aussi OpenCV core/imgproc pour construire
`iprRawDevelopmentCapture`, avec les sources moteur commitées de ce worktree.
Elle ne lie pas un moteur compilé chez Claude.

```text
iprRawDevelopmentCapture --input image.CR3 --output reference.json --path reference --mode parity
iprRawDevelopmentCapture --input image.CR3 --output candidate.json --path candidate --mode parity
python tests/raw_bench/parity.py --input image.CR3 --runtime-dir DLL_QT --runtime-dir DLL_LIBRAW --runtime-dir DLL_OPENCV
```

Les deux chemins partagent les niveaux natifs de LibRaw, WB telle quelle et
repère capteur sans orientation. Le candidat émet les six étapes I2 ; la
référence capture les frontières réellement observables, sans inventer une
sortie LibRaw signée ou normalisée intermédiaire. Les cinq frontières communes
sont comparées exactement sur trois ROI256 : unpack, avant RCD, après RCD,
après médiane et caméra finale. Le DCP est absent de cette sonde.

Options `--black R,G,B,G2` et `--white DN` : niveaux explicites identiques
pour les deux chemins, le blanc explicite désactive son ajustement automatique.
`parity.py --calibration-captures capture-production.json` peut reprendre ces
valeurs pour les sources identifiées par SHA256 dans une campagne de captures
de production. Cela ne teste pas le choix des règles de calibration par C3.

Unités I2 confirmées : DN u16 après unpack, DN f32 signés après noir,
normalisé f32 après borne basse/diviseur, pré-RCD f32 sur grille1/65535,
RGB f32 avant médiane et RGB f32 sur grille1/65535 après médiane. Les
post-médiane entiers de l'oracle sont normalisés par division float explicite :
un `convertTo` à réciproque peut changer un ulp sans changer aucun compte.
La tentative initiale concernée reste conservée ; aucune tolérance élargie.

`compare_noise_baseline.py --parity RESULTAT_C2.json --baseline CAPTURES_BRUIT.json`
compare les deux noirs calibrés aux anciens buffers de production conservés.
Il sépare égalité stricte avant RCD/après médiane et représentation flottante
finale avant DCP ; les comptes16 récupérés doivent rester identiques. Un PASS
des frontières entières n'est pas une déclaration de bit-parité du f32 final.

Le comparateur indique les étapes hors du périmètre explicitement demandé.
Un PASS limité aux cinq frontières ne vaut pas PASS de sa liste globale
par défaut, qui inclut le rendu Rec.2020. Les erreurs interrompent la campagne
au premier fichier non conforme et produisent un verdict de portée explicite.

## Priorité bruit et limites

I2 doit préserver les valeurs signées avant borne et distinguer DN bruts,
normalisé, entiers16 représentés en f32 avant RCD et caméra après médiane.
Les deux verts sont distincts ; l'ordre couleur R/G/B/G2 n'est pas l'ordre
spatial des phases R/G1/G2/B. Ne pas les confondre dans les gains ou noirs.

`smoke.py` vérifie une lecture native contre rawpy sur les mêmes ROI. Les
deux reposent sur LibRaw : c'est un contrôle de lecture/sérialisation, pas
une vérité physique indépendante ou une validation de tous les formats.
Les références JSON canoniques vivent dans `docs/validation/raw/` ; les
pixels diagnostiques restent dans le build propre au banc. Ne pas supprimer
les RAW, références ou données des autres agents.
