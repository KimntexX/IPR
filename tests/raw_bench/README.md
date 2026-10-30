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

## Module expérimental de débruitage caméra (N2)

La cible `cameraDenoiseTests` du banc compile le module de ce worktree.
CTest `cameraDenoiseContract` vérifie le contrat signé, les forces séparées,
l'identité, les axes RGB déjà balancés, l'annulation et les ressources sur signaux synthétiques.
Pour une mesure locale du module seul :

```powershell
./build-patch_raw_bench/Release/cameraDenoiseTests.exe --benchmark 6000 4000
```

Le pic mémoire concerne tout ce processus synthétique, pas l'application ;
aucune image de mesure n'est écrite. Le modèle empirique multiscalaire ne
remplace pas une calibration physique capteur. La source signée pré-LCP,
l'IHM, le proxy et les scènes réelles relèvent de N3, pas de ce test natif.
`qualify_camera_denoise.py --help` décrit le lanceur des preuves : module,
test des profils, CTest et dossiers DLL explicites ; chaque exécution écrit
un nouveau JSON horodaté sans écraser les preuves précédentes.
`--module-only` omet explicitement le test profil si un raccordement moteur
reste à faire ; il refuse `--profile-tool` dans ce mode afin de ne pas utiliser
un ancien binaire. La preuve distingue ce contrôle d'une intégration réussie.
`--skip-benchmarks` conserve les contrats et CTest, mais omet les allocations
et chronométrages synthétiques2,8/24 Mpx pendant une autre campagne ; la
preuve écrit explicitement `benchmarks=not_run`. Ne pas en tirer un budget temps/RAM.
`camera_denoise_witnesses.py --help` décrit le smoke réel N3 sur trois boîtiers :
A/B0/B50/B100 et replays, en mémoire, un serveur fermé par scène avec watchdog.
Les écarts de rendu sRGB qu'il mesure ne prouvent pas la qualité du débruitage.

Études de couplage avant/après profil : `run_denoise_leakage.py --help` pilote
`cameraDenoiseLeakageProbe` (cible du sous-projet `production`) sur des régions
caméra fixes. La projection tangentielle de Y y est une expérience, pas du
code de production. `camera_denoise_exact_roi.py --help` mesure les régions
N3 convenues avec Claude, après filtrage et rendu de l'image entière. Il
distingue moyenne RGB, luma encodée pondérée et chroma R−G/B−G. Le mode
`--reference-proof` exige l'exécutable exact d'une preuve historique, sans
conserver d'implémentation legacy dans le module. Aucun TIFF n'est écrit.
Le prototype courant(a)+V1 utilise cinq échelles chroma et trois pour l'intensité,
directement dans le RGB caméra déjà balancé, sans annuler les gains de BdB ;
les gains de métriques ne valent pas validation visuelle.

N2b(a) : `signal_noise_survey.py --help` pilote `--signal-survey` de la sonde.
Il conserve81 régions par source, sépare les25 positions initiales des56
positions de contrôle et compare un modèle global à une interpolation de
variance par niveau de signal. `--replay` réanalyse une preuve existante sans
redécoder les images. MAD, autocorrélation et heuristique d'aplat ne sont pas
une vérité physique du bruit ; cette sonde ne change pas le filtre.
La sonde actuelle travaille dans les axes caméra déjà balancés de V1 et
observe les modèles effectivement utilisés via `Context::noiseModelObserver`.
Elle rapporte support, nœuds conditionnels, estimation globale et couverture
du guide sur les81 ROI. La sortie filtrée servant à l'observation est jetée.
Les preuves anciennes compensant la BdB gardent leurs axes historiques,
y compris lors d'un `--replay` ; ne pas les assimiler à ces nouvelles mesures.

`qualify_signal_denoise.py --help` compare le candidat conditionnel aux masques
figés de `loi-force-bruit-v001-20261029T185322Z.json`. L'argument obligatoire
`--variant-reference` fournit `loi-force-bruit-b-v1-v001-20261029T200937Z.json`
pour comparer directement à B-V1 ; l'empreinte de sa référence commune doit
correspondre, sinon le programme refuse la comparaison. Il contrôle les empreintes
RAW/TIFF et celles des masques, puis exige le même témoin B0. Trois scènes de
développement sont mesurées en mémoire. Les28 ont maintenant servi à la
campagne de Claude sur(a)+V1 : aucun jeu intact n'est revendiqué pour les
prochains candidats. Les sources et l'exécutable sont hachés dans la nouvelle preuve.
Le watchdog termine un serveur bloqué ; un échec arrête la campagne et reste
consigné. Les dispersions haute fréquence incluent des textures réelles : leur
baisse n'est ni une vérité bruit seul ni une validation visuelle.
Le premier candidat conditionnel est conservé en `0941dc4`, V1 seul en
`0ba4335`. Leur combinaison conserve les axes balancés et le modèle empirique
conditionnel ; ni les forces ni les bornes DCP ne sont modifiées.
Le nom du lanceur n'établit pas que le binaire fourni contient ce modèle :
vérifier les sources embarquées et l'identité d'exécutable de chaque preuve.
`--prior-reference` peut joindre la campagne figée B_a_V1 pour distinguer
le gain du plancher sombre de celui des axes V1. Le garde-fou actuel relève
les variances du premier nœud vers le global si nécessaire, pas celles des
autres nœuds ; ce n'est pas une calibration physique ni une extension du domaine.

Le candidat courant ajoute un repli sur covariance/σ globaux V1 lorsque le
modèle conditionnel manque ou que le guide est hors domaine. Le raccord est
extérieur au domaine conditionnel ; les statistiques locales des deux modèles
restent séparées. Sans témoins admissibles, l'abstention reste nécessaire.
L'observateur distingue `supported` et `globalFallbackAvailable` ; les ROI de
la sonde comptent les pixels susceptibles d'utiliser le repli. Ce n'est pas
une preuve que le filtre les corrige tous, ni une mesure de bruit physique.
Les77 contrôles natifs sont positifs ; les22 scènes signalées par Claude et
le coût des buffers supplémentaires restent à qualifier avant promotion.

L'esquisse `CameraDenoise::settingsFromAmount` convertit une barre entière
0..100 en `Settings` : L=M, C=arrondi(100*(1-(1-M/100)^6)). Elle est testée
sur les101 états mais n'est pas raccordée à l'éditeur. Défaut0 ; entrées
invalides refusées sans modifier la sortie. Les réglages avancés contournent
ce remappage pour éviter une double loi. La cible visuelle LR70–75 vers
barre80–90 n'est pas encore qualifiée ; les valeurs LR ne sont pas les forces
internes IPR. La preuve synthétique ne vaut pas étalonnage perceptuel.

Étude des moyennes et domaines : `run_denoise_leakage.py --mean-audit
--scene 7Y6A7599.CR3` utilise le même exécutable avec `--mean-audit`.
Filtre pleine image100/100, moments caméra globaux, puis rendu de rectangles
fixes indépendants : ils ne remplacent pas les masques Lightroom figés.
`--profile-root` doit désigner les profils réellement installés, pas un
répertoire vide. Pas d'exportTIFF ni de changement de filtre/DCP.
`study_noise_domain.py` produit des contrôles JSON gaussiens de moyenne
après borne/encodage et d'aller-retour VST naïf (contrôle négatif, pas inverse
non biaisée). Tests : `python -m unittest discover -s tests/raw_bench
-p test_noise_domain.py`. Aucun de ces contrôles ne qualifie un filtre CFA.

`audit_dark_mean.py --help` reconstruit les masques sombres figés de la preuve
de force, vérifie RAW/TIFF, compte et hash `packbits`, puis utilise
`cameraDenoiseLeakageProbe --mean-audit --dark-mask <fichier>` sur trois scènes.
Le masque temporaire est en uint8 0/1, à la taille caméra et sans interpolation.
Le filtrage précède la sélection ; le DCP pointwise est ensuite mesuré sur
les seuls pixels sélectionnés. Pas d'optiques ni de filtre spatial au rendu.
Le témoin à moyennes caméra égalisées est un diagnostic, pas un correctif.
Six tests : `python -m unittest discover -s tests/raw_bench -p test_dark_mean.py`.
`study_dark_selection.py` produit un contrôle d'oracle montrant le biais d'un
masque sélectionné sur un bruit corrélé ; ne pas en déduire une attribution
quantitative du défaut réel. Aucun de ces outils ne change la production.

`audit_dark_mean.py --coarse-cells` remplace la sélection pixel par pixel par
des cellules complètes128×128 de moyenneLR<25, en incluant tous leurs pixels.
Le masque fin est vérifié pour la provenance seulement ; les mesures portent
sur l'union des cellules et sur chaque rectangle. Sans cellule admissible,
la scène est marquée non mesurée, sans changer la taille ni le seuil.
`compare_coarse_lr.py --help` relit les références LR existantes sur ces mêmes
cellules (neutre,75/0/0,100/100/0), avec vérification des empreintes, sans
nouveau rendu IPR. Ces cellules issues du même RAW ne sont pas une référence
indépendante. Neuf tests masques/coordonnées sont disponibles.

Pont vers le CFA : `inventory_cfa_corpus.py --help` authentifie les sources
des anciens JSON `patch_noise` sans décoder les pixels ; partitions conservées,
réserve42 scellée aux14 dernières. `pilot_signed_cfa.py --help` refuse une
sélection non développement ou incomplète, écrit le gel avant captures puis
pilote six développements signés à niveaux DN explicites (2048/16383).
Trois ROI fixes, phases distinctes, moments/différences/corrélations et
invariance des trois dernières captures ; binaires diagnostiques dans le build.
Ce n'est pas la calibration de production ni un débruiteur CFA. La paire
éclairée n'est pas recalée et ne doit pas devenir un oracle de bruit.
Cinq tests : `python -m unittest discover -s tests/raw_bench -p test_cfa_corpus.py`.

`audit_jensen.py --help` reprend les moyennes des preuves de masques/cellules
et demande à la sonde `--jensen-proof` de calculer F(moyenne caméra), sans
relancer le débruitage. Il vérifie profil, manifeste, RAW et code DCP du parent,
puis décompose la variation rendue en déplacement de moyenne et changement
de l'écart E[F(X)]−F(E[X]). F est toute la chaîne DCP/rendu, pas une courbe
scalaire garantie concave. Aucune attribution causale d'un composant seul.
Cinq tests : `python -m unittest discover -s tests/raw_bench -p test_jensen.py`.

`cfaDenoisePrototype` (cible CMake du banc) et `run_cfa_prototype.py --help`
testent un préfiltre Wiener original3×3 par phase sur un signalRGB connu
et les résidus des captures de noirs de développement du pilote signé.
Gel avant mesure, variance prise sur l'autre noir,32cas, quatre branches
et témoins propres, ROI centrale96/256. Ni VST, ni bruit photonique, ni DCP.
Le prior noir externe est privilégié face au post aveugle : pas d'isolation
causale du placement seul. Les critères de poursuite sont enregistrés avant
mesure ; résultat négatif ne déclenche pas d'ajustement de fenêtre/force.
`ctest -R cfaDenoisePrototypeContract` vérifie les cinq contrats natifs ;
`python -m unittest discover -s tests/raw_bench -p test_cfa_prototype.py`
vérifie quatre contrôles de la décision. Ce code n'est lié qu'au banc.

`denoiseTonalGainProbe` et `study_tonal_noise_gain.py --help` étudient la
sensibilité du rendu tonal et la réutilisation globale du résidu maximal.
Six recettes sur gris Rec.2020, sans DCP, statistiques typiques : ce n'est
pas le placement de l'exposition RAW avant la courbe. Contrôle gaussien
avec σ nominal et réellement tiré, dérivée locale et écrêtage explicite.
Cinq forces maître sont aussi confrontées au vrai filtre sur un petit
témoin synthétique ; cela ne qualifie ni un cache applicatif ni des poids
spatiaux. Résultats JSON versionnés, pas de RAW/TIFF écrit ni filtre modifié.
`ctest -R denoiseTonalGainContract` vérifie la finitude des transferts et
la reconstruction globale dans ce témoin. Voir rapport évolutif §39.

`weakStructureProbe` et `study_weak_structure.py --help` observent les modèles
du filtre courant sur32 motifs propres/bruités : orientation, fréquence,
phase et amplitude figées. Aucun nouveau filtre. Bruit gaussien corrélé
binomial synthétique achromatique, pas bruit physique RCD. L'option
`--layout-control` garde le même motif central et sa marge80px, ajoute un
entourage uniforme distant et compare les estimations et gains sur96px.
Ce contrôle propre n'établit pas le comportement en bruit réel et change aussi
le découpage en tuiles/DC global. Gel JSON avant calcul, sources incluses.
Trois tests Python : `python -m unittest discover -s tests/raw_bench -p test_weak_structure.py`.
Rapport évolutif §40, aucune réserve de RAW lue ni export écrit.

`study_structure_prediction.py --pilot-proof <pilote signé>` reprend seulement
les quatre noirs de développement déjà capturés à1600/51200ISO. Prédiction
I depuis16 voisins à retards4/8, ajustement et contrôle en quadrants disjoints.
Comparateur ordinaire et correction de covariance avec le bruit cible depuis
l'autre noir, pas de vérité acceptée par la fonction d'ajustement.68cas,
signal périodique connu + résidu réel postRCD ajouté (pas simulation du CFA).
`--matched-noise` est un contrôle non physique à σI d'ajustement0,002, fixé
au niveau du§40, pour ne pas confondre ISO et rapport signal/bruit.

`audit_structure_residual.py --pilot-proof <pilote> --prediction-proof <json>`
(argument parent répétable) rejoue les poids enregistrés, sans réajustement.
Audit de la variance résiduelle compensée et de la formule utilisant seulement
la forme de corrélation du noir. Les poids parents ont encore utilisé
l'amplitude du prior : ne pas annoncer une chaîne entièrement indépendante
de cette amplitude. Les variances négatives restent explicites. Pas de filtre
ou seuil intégré, ni score photo. Six tests : `python -m unittest discover
-s tests/raw_bench -p test_structure_prediction.py`. Voir rapport§41.

`study_structure_amplitude_free.py --help` ajuste les16 voisins avec seulement
la covariance OBSERVÉE et un ridge relatif fixé ; aucune amplitude du noir
n'entre dans l'ajustement ni dans la formule résiduelle normalisée.64cas,
textures irrégulières et détails absents de l'ajustement, quatre formes de
prior, pixelI et trois échellesB3. La forme externe du bruit reste requise.
`white_shape` signifie explicitement coefficients supposés indépendants :
ce n'est pas la propagation d'un bruit caméra blanc à travers B3.

La cible CMake `structureBaselineProbe` reçoit le RGB observé/propre en JSON
base64 f32 par stdin, sans fichiers pixels ni TIFF. Elle exécute le vrai module
100/100 sur les deux entrées, retourne ses résumés de modèles et ses mesures
au contrôle. Les σ globaux observés ne sont pas le routage conditionnel exact
par pixel. Six tests : `python -m unittest discover -s tests/raw_bench
-p test_structure_amplitude_free.py`. Résultat et limites au rapport§42.

`study_noise_model_ablation.py --help` reprend exactement les64 entrées de
l'épreuve précédente. Le générateur `generate_noise_oracle.py` produit dans
`oracle-generated/` du build le source moteur courant plus UN hook après
estimation ; la source moteur n'est pas éditée. La cible `structureOracleProbe`
partage le même code de sonde que `structureBaselineProbe`, sans second filtre
maintenu. Le marqueur unique est vérifié avant génération ; toute dérive échoue.

Comparaison100/100 : modèle normal, même modèle limité au pooled, puis pooled
estimé sur bruit seul connu ou sur l'autre noir. Le dernier n'est pas normalisé
à l'amplitude de la réponse attendue. Les sorties aveugles instrumentées doivent
être identiques pixel à pixel aux sorties du module original ; le modèle nul
doit préserver exactement le témoin propre. L'observateur est appelé AVANT
le hook : ses résumés ne décrivent pas le modèle externe appliqué ; celui-ci
provient du run de capture de bruit seul correspondant. Les témoins propres
au prior non nul sont des stress de transfert, pas des entrées réellement
sans bruit qui devraient recevoir ce même prior. Quatre tests de contrat :
`python -m unittest discover -s tests/raw_bench -p test_noise_model_ablation.py`.
La preuve canonique conserve l'empreinte des octets originaux et le source
généré ; le SHA de son commentaire d'en-tête porte sur le texte normaliséLF,
pas sur les octetsCRLF du fichier d'origine. Résultats et limites au rapport§43.

`audit_signal_noise_bridge.py --history-root ../patch_noise --pilot-proof <json>`
audite la loi CFA historique figée sans la réajuster ni décoder de nouveaux
RAW. Filiation des JSON,48couples moyenne/écart-type sur les captures de noirs
actuelles,24pentes/72ancrages locaux et312moments ; transfert historique conservé
séparément avec ses durées et extrapolations. Les deux verts restent distincts.
Le résultat est un catalogue de RECHERCHE sous `docs/validation/raw/`, pas une
bibliothèque installée ou un fichier de configuration du moteur.

Normalisation testée : `X=g*(DN-B_runtime)/D`, `A=g*a/D`,
`B=g²*(b+a*(B_runtime-B_reference))/D²`. Les coefficients sont CFA AVANT RCD,
pas des σ RGB/B3 ; un modèle de variance ne se convertit pas comme une moyenne.
Les exemples de gains/noirs/diviseur sont ceux du pilote signé, pas ceux de
toutes les photos ou boîtiers. Cinq tests : `python -m unittest discover
-s tests/raw_bench -p test_signal_noise_bridge.py`. Rapport§44.

`study_cfa_signal_transport.py --bridge-proof <json> --pilot-proof <json>
--tool <cfaTransportProbe.exe> --runtime-dir <dll-dir>` (répéter le dernier
argument si nécessaire) propage le budget figé dans le RCD signé courant,
avant/après médiane, puis mesure les covariances I/Co/Cg au pixel et sur B3.
La cible `cfaTransportProbe` transporte uniquement des fenêtres CFA 256×256
par stdin/stdout base64 float32 ; aucun fichier de pixels ou moteur modifié.

Le résidu noir réel est centré sur le noir de référence figé ; seul le terme
gaussien de variance a·signal est ajouté, jamais b une seconde fois. Deux ISO,
deux niveaux positifs chacun, aplats et texture fine bornée ; graines de
préparation et contrôle distinctes et inversion des deux noirs. 100 transports
bruités, dix témoins propres, 128 comparaisons et 34 diagnostics de paires.
Comparer profils noir seul, aplat au même niveau et covariance affine depuis
le niveau bas ; matrices invalides signalées, jamais corrigées par projection.
Les simulations ne sont pas des prises indépendantes ni une qualification
physique. Pas de débruitage ou DCP dans cette épreuve. Rapport §45.
Tests : `cfaTransportProbe --self-test` et `python -m unittest discover
-s tests/raw_bench -p test_cfa_signal_transport.py` (six tests).

`study_signal_filter.py --help` poursuit la propagation à filtre inchangé :
80 cas issus du CFA signé, deux ISO, deux niveaux, aplats, textures fine et
grossière, contours achromatiques/colorés, deux noirs et deux graines de
contrôle. Le profil de signal est estimé sur le résidu d'un aplat simulé
indépendant à même niveau et sur l'autre noir. Le profil noir seul emploie
le même autre noir sans terme de signal. Le hook généré est celui déjà
qualifié, sans nouvelle implémentation du filtre.

`structureBaselineProbe` et `structureOracleProbe` conservent la comparaison
de chaque sortie noisy/clean par empreinte. Le champ facultatif
`read_reference` ajoute la capture du modèle noir et ses deux stress ; sans
ce champ, les anciennes recettes restent identiques. Les mesures incluent
MSE/biais séparés sur I/Co/Cg (axes caméra, pas luminance d'affichage).
Le modèle courant, son pooled aveugle, le profil noir seul et le profil
dépendant du signal sont comparés à 100/100 ; un modèle estimé sur le
bruit exact du cas est un diagnostic privilégié, pas un candidat exploitable.
Les stress propres avec budget non nul ne sont pas des situations sans
bruit réalistes. Le résumé expose aussi le pire cas, pas uniquement la médiane.
Cinq tests dans `test_signal_filter.py`, CTest `signalFilterContract`.

`--distribution scaled_poisson` remplace le tirage gaussien par un Poisson
centré/mis à l'échelle de même variance a·signal. C'est une épreuve de
sensibilité à l'asymétrie, pas une estimation de gain physique en électrons.
`signal_noise_draws.py` est testé sur les moments, le signal nul et l'identité
du flux gaussien historique de ce banc ; quatre tests.

`--external-covariance` ajoute une substitution directe de cinq covariances
B3 calculées sur quatre graines de calibration et sur l'autre noir, puis
contrôle 40 cas (première graine de contrôle, sans sélection par résultat).
Ni MAD ni choix des tuiles calmes pour cette variante. `fromCovariance`
et le corps du filtre restent ceux du module. Le profil est une moyenne de
covariances empiriques sur résidus simulés, pas un oracle physique exact.
La sonde refuse profil tronqué, asymétrique/non fini ou non PSD ; aucun
écrêtage spectral ne rend valide une matrice refusée. `signal_noise_covariance.py`
vérifie les trois premiers plans contre le transport précédent, les deux
plans supplémentaires et le signal constant ; trois tests.
`--covariance-draws 1` contrôle l'attribution du gain : même réalisation
de référence que le MAD, sans avantage de moyennage de quatre réalisations.
Le mode à quatre reste la préparation de profil initiale ; aucune valeur
intermédiaire ni recherche du nombre donnant le meilleur score.

`audit_signal_filter.py <preuves...>` vérifie grille, parités, finitude,
budget de threads, profils et identité des entrées/sorties de référence des
cas rejoués. Les variantes partielles gardent leurs nombres et graines
explicites. Il conserve les avertissements de métadonnées des preuves
historiques et produit un nouvel audit JSON sans réécrire les campagnes.
Les résumés ne déclarent jamais automatiquement une promotion de production.
CTest `signalFilterAuditContract`, `signalDistributionContract` et
`signalCovarianceContract` complètent les contrôles.

`camera_noise_budget.py` prépare le budget de recherche par caméra/ISO exacts
et normalisation signée : noir/blanc, diviseur et quatre gains R/G/B/G2.
Les deux verts restent distincts ; aucune interpolation ISO ni emprunt d'un
autre boîtier. La loi temporelle caractérisée reste exploratoire et le résidu
noir réel n'est pas remplacé par un second terme b synthétique.
La moyenne locale provient du seul RGB caméra observé (guide moyen31×31),
jamais de sa variance/MAD ni de la référence propre. Un signal latent estimé
négatif reçoit le plancher physique du bruit de lecture pour le calcul de
variance ; aucun pixel signé n'est borné. Cette règle reste un modèle de
recherche, pas une gestion des bornes du DCP.

`study_context_transfer.py --bridge-proof <json> --pilot-proof <json>
--transport-tool <cfaTransportProbe.exe> --runtime-dir <dll-dir>` compare
144 cas : deux ISO, trois contextes (métadonnées et témoins synthétiques de
BdB/normalisation), deux distributions, six contenus et deux noirs. Trois
zones gauche/transition/droite, cinq plans B3 ; trois graines de contrôle
distinctes de celle du guide et de celles de calibration. Chaque budget
utilise un aplat simulé indépendant au niveau/couleur estimés et l'autre noir.
Les métadonnées et sources figées précèdent le calcul ; toutes les phases
gardent leurs drapeaux d'extrapolation. `audit_context_transfer.py <preuve>`
contrôle complétude, finitude, covariances et normalisation affine sans
recalculer les pixels. Il expose les erreurs par contexte/contenu/zone/plan,
sans transformer une moyenne en validation universelle.

La version2 mesure chaque covariance de profil sur la même fenêtre que son
contrôle : le support des grandes bandes près d'un bord n'est pas celui du
centre. La géométrie fait aussi partie de la clé du profil. L'auditeur accepte
`--baseline <preuve-v001>` pour vérifier l'identité des entrées, guides et
covariances de contrôle ; seules les prédictions changent dans ce rejeu.

Le modèle est actuellement caractérisé pour le R6 Mark III seulement ; les
témoins BdB/noir/diviseur ne qualifient aucun autre boîtier, mode, compression,
température capteur ou durée d'exposition. Aucun filtre, cache/IHM de production
ou rendu DCP modifié. Contrats : `cameraNoiseBudgetContract` et
`contextTransferAuditContract`.

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
