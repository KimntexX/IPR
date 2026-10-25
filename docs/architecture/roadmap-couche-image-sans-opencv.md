# Roadmap — couche image sans OpenCV

**Décision :** [ADR-0018](../decisions/ADR-0018-independance-decodage-raw-et-couche-image.md),
20 octobre 2026. **État :** analyse ; aucun code. Chantier distinct de celui
des [décodeurs RAW](roadmap-decodeurs-raw.md), mené après lui, sur sa propre
branche durable, et pas avant la fin de l'intégration des fonctions de base.

## 1. Usage mesuré le 19 octobre 2026

Seuls les modules `core` et `imgproc` sont utilisés : 286 occurrences dans
28 fichiers de `src/`, surtout `DetailProcessing.cpp` (64),
`CameraProfileEngine.cpp` (62) et `ColorPipeline.cpp` (45).

| Usage | Occurrences | Remplacement |
| --- | --- | --- |
| `cv::Mat` | 109 | Type image IPR : largeur, hauteur, canaux, pas de ligne |
| `Vec3f`, `Vec3d`, `Vec4f`, `Vec2d`, `Matx33d`, `Matx33f`, `determinant` | ≈ 160 | Petites structures de calcul (≈ 150 lignes) |
| `GaussianBlur` | 7 | Flou séparable optimisé : **principal enjeu de performance** |
| `resize` (AREA, proxy), `remap` bilinéaire (LCP) | 2 chacun | Simples |
| `erode`, `dilate`, `Sobel`, `split`, `merge`, `extractChannel`, `checkRange`, `transform`, `norm`, `magnitude` | ≈ 30 | Triviaux |
| `parallel_for_` | 4 | QtConcurrent, déjà utilisé |

**Inutilisé :** `imgcodecs` est lié dans `CMakeLists.txt` et vcpkg installe les
options `jpeg`, `png` et `tiff` d'OpenCV, alors qu'aucun `imread`/`imwrite`
n'existe : les entrées-sorties passent par Qt et libtiff. Leur retrait est
autorisé dès maintenant par la règle de suppression des éléments inutiles
(ADR-0018), sans attendre ce chantier.

## 2. Paliers

| Palier | Travail | Critère de passage |
| --- | --- | --- |
| O0 | Retrait d'`imgcodecs` et des options vcpkg inutiles | Build et CI verts, empreintes inchangées |
| O1 | Audit du partage de données de `cv::Mat` (copies légères, `clone()`) | Liste des copies et des alias, budget mémoire du poste 16 Go |
| O2 | Type image et structures de calcul IPR | Tests unitaires ; empreintes inchangées à l'échange de type |
| O3 | Noyaux : flou, resize, remap, morphologie, Sobel | Tolérances chiffrées par noyau, performances au moins égales sur le poste cible |
| O4 | Migration module par module (ADR-0017), puis retrait d'OpenCV | Campagnes de détail, Lumières et optique revalidées ; validation utilisateur |

## 3. Risques et tests

- **Partage implicite :** `cv::Mat` compte ses références ; un type à copie
  profonde change la mémoire et peut révéler des alias. O1 précède tout échange.
- **Égalité impossible sur les flous :** arrondi des tailles de noyau et bords
  diffèrent ; les tests comparent avec des **tolérances** chiffrées, pas des
  SHA-256. Les autres noyaux visent l'égalité bit à bit.
- **Performance :** égaler la vectorisation (AVX2) d'OpenCV sur clarté, texture,
  voile et bruit ; mesurer temps et pic mémoire avant chaque fusion.
- **Robustesse :** ASan/UBSan sur les noyaux, bords et petites images.

Estimation interne : **4 à 8 semaines**, à remesurer après O2.

## 4. Bénéfices

Moins de DLL et un déploiement plus léger ; précision, parallélisme et mémoire
maîtrisés ; le décodeur RAW IPR écrit directement dans le type image IPR.
