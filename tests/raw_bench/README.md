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
repère capteur sans orientation. Le candidat émet les sept étapes I2 ; la
référence capture les frontières réellement observables, sans inventer une
sortie LibRaw signée ou normalisée intermédiaire. Les cinq frontières du banc initial
sont comparées exactement sur trois ROI256 dans le premier banc C2 : unpack,
avant RCD, après RCD, après médiane et caméra finale. Le banc courant ajoute
la sixième frontière commune avant borne de sortie, avec C4 corrigé.
Le candidat émet maintenant sept étapes I2, plus la caméra finale du banc.
Le DCP est absent de cette sonde.

Options `--black R,G,B,G2` et `--white DN` : niveaux explicites identiques
pour les deux chemins, le blanc explicite désactive son ajustement automatique.
`parity.py --calibration-captures capture-production.json` peut reprendre ces
valeurs pour les sources identifiées par SHA256 dans une campagne de captures
de production. Cela ne teste pas le choix des règles de calibration par C3.

Unités I2 confirmées : DN u16 après unpack, DN f32 signés après noir,
normalisé f32 après borne basse/diviseur, pré-RCD f32 sur grille1/65535,
RGB f32 avant médiane et RGB f32 sur grille1/65535 après médiane. Les
post-médiane entiers de l'oracle sont maintenant normalisés par produit
réciproque float, représentation F1 historiquement reçue par le DCP.
Le premier C2 utilisait une division float explicite à cette frontière ;
la tentative et sa correction historiques restent conservées. Un ulp de
représentation n'est pas un compte16 différent, mais une parité f32 ne se
déclare pas en masquant cet écart : aucune tolérance élargie. Pré-RCD garde
la division float, conformément au contrat de cette autre frontière.

`compare_noise_baseline.py --parity RESULTAT_C2.json --baseline CAPTURES_BRUIT.json`
compare les deux noirs calibrés aux anciens buffers de production conservés.
Il sépare égalité stricte avant RCD/après médiane et représentation flottante
finale avant DCP ; les comptes16 récupérés doivent rester identiques. Un PASS
des frontières entières n'est pas une déclaration de bit-parité du f32 final.

Le comparateur indique les étapes hors du périmètre explicitement demandé.
Un PASS limité aux frontières caméra ne vaut pas PASS de sa liste globale
par défaut, qui inclut le rendu Rec.2020. Les erreurs interrompent la campagne
au premier fichier non conforme et produisent un verdict de portée explicite.

## Comparaison de production C3 et mesure des ressources

`production_campaign.py --help` décrit les entrées explicites du banc de
bout en bout. Le programme construit deux `iprProductionRawCapture` dans
des sous-builds propres : référence extraite d'un commit `dev` figé et
candidat C3 dans ce worktree. Aucun moteur d'un autre build n'est réutilisé.
La sonde appelle directement l'API publique `RawDecoder::decode`.

Les30 sources de l'inventaire contrôlé sont lues avec Camera Neutral puis
sans profil ; la recherche utilise un dossier explicitement vide et un DCP
absolu figé si demandé. WB boîtier, corrections optiques désactivées et
manifeste de calibration explicitement commun aux moteurs. La comparaison
exige même géométrie/type/hash de **toute** l'image linéaire Rec.2020,
mêmes métadonnées auditées et égalité exacte des trois ROI diagnostiques.
Une différence hors ROI échoue donc aussi.

Le timer natif couvre seulement `decode`, pas les hashes ni la sérialisation.
Pic mémoire : peak working set du processus relevé immédiatement après
decode (RSS maximale sur les plateformes POSIX). Processus neuf par cas,
ordre référence/candidat alterné ; le cache disque OS n'est pas purgé.
OpenCV1, OMP8, maximum8 workers RCD. Budget de temps annoncé :20,8s par
ouverture ; le pic mémoire est mesuré, pas assimilé à la RAM totale du
système ou à une preuve du contrôleur80 %. Pas de gain de vitesse statistique
sur une seule ouverture par cas, ni couverture automatique de tous les replis.

Les fichiers `status.json`, logs de configuration/build et `partial.json`
en cas d'erreur vivent sous `build-patch_raw_bench/production/<UTC>/`.
Le gel et le résultat canonique sont horodatés dans `docs/validation/raw/`.
La campagne s'arrête au premier écart de parité, sans modifier les sources,
le profil, la calibration ou les tolérances pour le faire disparaître.

## Pilote signé X4 (C4)

La sonde de module courante requiert l'interface C4 `Policy`. Pendant le
gel C3, compiler depuis une archive immuable C4 avec
`-DIPR_RAW_SOURCE_ROOT=CHEMIN_ARCHIVE_C4` et
`-DIPR_RAW_ENGINE_REVISION=COMMIT_C4_COMPLET`, dans un sous-build séparé.
Ne pas reconstruire le moteur C3 ni fusionner des sources pendant sa mesure.
Les révisions outil et moteur sont distinctes dans les captures.

```text
iprRawDevelopmentCapture --input image.CR3 --output B.json --path candidate --mode signed
python tests/raw_bench/signed_campaign.py --baseline CAPTURES_BRUIT.json --tool EXE_C4 --engine-source ARCHIVE_C4 --engine-revision COMMIT_C4_COMPLET --runtime-dir DLL_QT --runtime-dir DLL_LIBRAW --runtime-dir DLL_OPENCV
```

`signed` n'est pas accepté sur le chemin oracle LibRaw. La campagne mesure
les six paires historiques1600/51200 avec les niveaux effectivement appliqués
et vérifie sources, outils, contrats et paramètres A/B. Elle produit un gel
puis un JSON de mesures, jamais un PASS de parité pour B ou une validation
de qualité de débruitage. Statut/logs/partiel sous le build `signed/<UTC>`.

Le moteur courant du banc utilise F1 : l'entrée CFA reste signée pendant
RCD et la médiane en B, puis sort sans borne ni grille16. `pre_output_camera_rgb`
observe la médiane avant la grille A ; ce stage est obligatoire, avec les
six autres étapes I2 et la caméra finale. La sortie B doit être exactement
identique à cette capture, négatifs et valeurs >1 compris. Les anciens
pilotes C4 restent des preuves historiques, pas des modes à reconstruire.
`--previous-study JSON_X4_CORRIGE` contrôle l'invariance A sur les six
frontières avant sortie du précédent pilote corrigé. F1 représente sa grille
A finale par produit réciproque (entrée historique DCP), plus par division :
l'invariance du rendu entier est contrôlée dans le banc de production, sans
élargir les tolérances ni prétendre identiques deux représentations distinctes.
La variance de paires ne capture pas les composantes communes aux deux
prises ; moyenne, dérive, covariance, zéros et négatifs sont gardés séparés.
Pour les DN, `at_or_above_one_fraction` n'est pas une fraction au blanc.

## Qualification de la sortie flottante F1

La sonde de production accepte `--capture-camera` avec un DCP explicite :
entrée réellement retenue par `CameraSignal`, trois ROI et statistiques/hash
plein cadre, pas RGB reconstitué depuis Rec.2020. `float_qualification.py`
vérifie le contrat et, sur les noirs témoins seulement, les négatifs conservés.
Le chemin sans DCP n'expose pas ce contexte : ne pas fabriquer son entrée.

`production_campaign.py --capture-camera --candidate-signed-api ...` compare
A de bout en bout et exige aussi l'égalité exacte des entrées DCP. Référence
figée `ee8f26d`, candidat F1 figé ; même manifeste/profil/source et optiques
désactivées. `--candidate-signed-api` compile l'API mais **ne sélectionne pas B**
dans cette campagne d'invariance A.

Pour B, compiler la sonde contre F1 avec
`-DIPR_BENCH_SIGNED_DEVELOPMENT=ON` puis employer `--development signed`.
Un oracle pré-F1 refuse cette commande, jamais de substitution silencieuse.
`float_campaign.py --help` décrit le contrôle des six paires existantes :
finitude du master/caméra et négatifs témoins avant DCP, budgets, provenance.
`--module-study` exige aussi la comparaison exacte module→entréeDCP sur les
quatre noirs sans rotation ; les scènes tournées à ROI différents restent
explicitement hors de cette égalité géométrique.
Un succès ne valide ni le prolongement signé des tables/courbes DCP ni une
amélioration du débruitage. La quantification intermédiaire doit être vérifiée
avec l'audit du chemin et les captures de module, pas seulement la présence
de valeurs négatives en sortie. Aucun candidat F1 n'est supposé validé par
les contrôles de la seule référence.

`iprRawFloatContract` teste la frontière publique `fromLinearSrgb` : signal
fini négatif/>1 accepté et non modifié, NaN/±Inf refusés. Le défaut initial
F1 et son correctif `0adefe7` ont deux preuves distinctes. `float_witnesses.py`
vérifie trois boîtiers en A/B avec/sans DCP après ce garde, sans rejouer
inutilement les 60 succès de la campagne F1 complète. Les outils ne modifient
ni les règles du DCP ni le placement du débruitage en production.

## Limites du corpus et références

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
