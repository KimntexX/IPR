# Module neuronal RAW — intégration bêta-1

État au 11 novembre 2026 : module autonome testé ; raccordement au moteur,
préférences et recettes encore à faire après alignement de `patch_noise` sur `dev`.
Le modèle de production n'est pas encore livré. Les graphes de contrôle ne sont
pas des modèles de débruitage et ne doivent pas être distribués comme tels.

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

Le modèle d'environ 33 Mo reste hors Git. Son futur manifeste
`packaging/neural-model.json` est volontairement incomplet : l'acquisition
échoue tant que son identifiant, URL et empreinte ne sont pas validés. Un
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
n'est pas imposée à la construction de production MSVC, qui reste à qualifier.
La concordance avec le réseau PyTorch réel, la mémoire pleine définition et
la latence DirectML restent ouvertes.

Sources : [paquets C++ officiels](https://onnxruntime.ai/docs/get-started/with-cpp),
[contraintes DirectML](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html).
