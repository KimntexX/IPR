# Module neuronal RAW — intégration bêta-1

État au 11 novembre 2026 : service CFA raccordé sur `patch_noise_cfa`, préférence
désactivée par défaut, force et identifiant dans la recette, installation du modèle
vérifiée par SHA-256. Le réseau reste un candidat en qualification ; aucune
livraison dans `dev` n'est implicite. Les graphes de contrôle ne sont pas des
modèles de débruitage.

## Contrat avec le développement RAW

`BayerPlanes` contient quatre plans contigus **R, G1, G2, B**, chacun de
`width × height` flottants. Les dimensions sont celles des plans, pas celles de
la mosaïque. Les valeurs signées et supérieures à un sont conservées. Le
diviseur de normalisation et les coefficients `a`, `b` proviennent du RAW et
du catalogue de bruit : aucune constante propre au R6 III dans le module.

Le modèle reçoit `planes` et `sigma`, chacun de forme `[1,4,H,W]`, et retourne
`corrected` dans les mêmes unités. La moyenne 5 × 5 de l'observation exclut les
positions hors image, comme `avg_pool2d(count_include_pad=False)` du banc Python.
Cette carte est calculée une seule fois sur l'image complète, avant tuilage.
Chaque tuile comprend un cœur de 1024 cellules et une marge de 192 ; les
positions extérieures sont réfléchies sans répéter le pixel de bord. Seul le
cœur est recopié, sans moyenne entre tuiles. La sortie garde les dimensions
d'origine, y compris si elles ne sont pas multiples de huit.

La force appartient à `[0,1]`. Zéro copie exactement la source sans charger le
modèle ni exiger une loi de bruit ; les autres valeurs interpolent la prédiction.
Une annulation retourne un statut et aucune image partiellement corrigée.
Le callback est consulté avant et après chaque inférence, pas pendant un appel
ONNX : le délai d'annulation comprend donc au plus une inférence de tuile.

## Dépendance facultative

Le module compile sans ONNX Runtime. `IPR_ENABLE_ONNX_RUNTIME=ON` active le
runtime ; sous Windows, `IPR_NEURAL_DIRECTML=ON` essaie DirectML puis le CPU.
`engine()` rapporte le fournisseur sélectionné après le premier traitement.
Le chargement est paresseux ; un modèle absent ne perturbe pas la force zéro.
Les appels d'une même session sont sérialisés. Aucun téléchargement implicite
au lancement de l'application ou lors de la configuration CMake.

Acquisition explicite des paquets officiels épinglés :

```powershell
python scripts/neural_assets.py runtime --output build-patch_noise-neural/runtime
```

Le manifeste `packaging/neural-runtime.json` fixe les URLs et SHA-256.
La dépendance DirectML possède ses propres conditions Microsoft, distinctes
de la licence MIT d'ONNX Runtime ; leurs textes sont conservés sous `licenses/`.
DirectML.dll doit accompagner onnxruntime.dll ; le pilote seul ne remplace pas
cette dépendance du paquet. Aucun toolkit CUDA n'est requis pour cette voie.

Le modèle d'environ 33 Mo reste hors Git. Le manifeste
`packaging/neural-model.json` épingle l'identifiant et l'empreinte du candidat ;
l'acquisition distante reste refusée tant que l'URL de release manque. Un
chemin local peut être fourni directement à `SessionOptions` en développement.

## Banc autonome

Le sous-répertoire possède un point d'entrée CMake utilisable sans Qt et sans
le moteur historique du worktree :

```powershell
cmake -S src/neural -B build-patch_noise-neural/check -DBUILD_TESTING=ON
cmake --build build-patch_noise-neural/check
ctest --test-dir build-patch_noise-neural/check --output-on-failure
```

Pour le runtime, fournir `IPR_ENABLE_ONNX_RUNTIME`, `IPR_ONNXRUNTIME_ROOT` et
`IPR_DIRECTML_ROOT`. Le générateur `tests/make_neural_identity.py` produit deux
graphes ONNX : identité et, avec `--filter`, moyenne spatiale suivie d'une
soustraction de sigma. Les variables `IPR_NEURAL_TEST_MODEL` et
`IPR_NEURAL_SPATIAL_TEST_MODEL` les raccordent aux tests. Le second vérifie
réellement la cohérence du voisinage et de la carte sigma entre tuiles.

Qualification locale : GCC 13.1 / MinGW, C++17 ; le SDK DirectML demande
`-D_Maybenull_=` pour son annotation SAL absente de ce SDK MinGW. Cette option
n'est pas imposée à la construction MSVC, validée ensuite par Claude sans ce
drapeau. Sur une vraie vue ISO 6400, Claude mesure une erreur maximale
ONNX/PyTorch d'environ 7,5e-8 CPU et 6,0e-8 DirectML. Les autres ISO, les scènes
réelles et les ressources pleine définition restent des qualifications partielles.

## Préférences, recettes et installation

Le panneau RAW avancé expose l'activation, la force 0–100 (100 par défaut),
et le coût du repli CPU après identification du moteur. Les clés INI sont
`neural/enabled`, `neural/strength` et `neural/modelPath`. Le chemin local peut
aussi être fourni par `IPR_NEURAL_MODEL`. Sinon le service lit
`<appdir>/models/<model_id>.onnx`.

La recette conserve `neuralStrength` et `neuralModelId`. Une ancienne recette
sans ces champs reste désactivée, même si la préférence est active. Une nouvelle
photo sans recette reprend la force des préférences. L'autorisation globale
désactivée interdit l'inférence sans effacer les paramètres d'une recette reçue.
Un identifiant enregistré n'est pas remplacé silencieusement par un autre modèle.

Un changement explicite d'activation ou de force sur l'image ouverte redéveloppe
le RAW. Le cache distingue la force et le modèle ; le raccourci DCP ne réutilise
pas un signal débruité pendant une transition. Le préchargement spéculatif est
suspendu lorsque le neuronal est autorisé. L'annulation est rattachée à la factory
au moment du chargement ; les exports pleine résolution et par lot utilisent cet
étage. Le catalogue de cette bêta-1 concerne le R6 Mark III. La balance automatique
empruntant encore un chemin qui peut ignorer cet étage, son association au
neuronal est temporairement refusée explicitement.

`IPR_NEURAL_MODEL` est disponible en CMake même sans les tests. L'installation
vérifie le SHA-256 et copie le modèle sous son identifiant. Le lanceur CI accepte
`--neural-sdk-dir <SDK> --neural-model <ONNX>` : il transmet les chemins CMake et
les répertoires de DLL, sans téléchargement implicite ni dépendance obligatoire
pour les autres constructions.

Sources : [paquets C++ officiels](https://onnxruntime.ai/docs/get-started/with-cpp),
[contraintes DirectML](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html).
