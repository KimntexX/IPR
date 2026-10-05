# Traitement d'image avec Qt/QML et C++

> Document de cadrage pour le projet **IPR**, relu le 9 septembre 2026.

## Objectif et choix conseillé

Pour un éditeur d'images dont l'interface est réalisée avec Qt Quick/QML, le choix le plus cohérent est :

- **C++17** pour le moteur, les accès aux pixels et l'intégration Qt (C++20 est un bon supplément si le compilateur du projet le permet) ;
- **Qt 6** (`Qt Gui`, `Qt Quick`, `Qt Concurrent`) pour le chargement, l'affichage et l'interface ;
- **OpenCV** pour les algorithmes classiques de traitement d'image et de vision ;
- **QML** uniquement pour la présentation et les interactions utilisateur, jamais pour une boucle qui traite les pixels un à un.

Cela garde une application native, performante et maintenable. Python reste excellent pour expérimenter un algorithme avec NumPy, scikit-image ou OpenCV, puis le réécrire/intégrer en C++ une fois validé. Rust est une alternative intéressante pour un moteur neuf très orienté sûreté mémoire, mais son intégration à Qt et l'écosystème de vision sont aujourd'hui moins directs que C++ dans ce projet.

### Sélection des bibliothèques

| Besoin | Bibliothèque | Recommandation |
| --- | --- | --- |
| Chargement, pixels, dessin simple | Qt Gui : `QImage`, `QImageReader`, `QImageWriter`, `QPainter` | À utiliser systématiquement. `QImage` est optimisé pour les E/S et la modification de pixels ; `QPixmap` est destiné à l'affichage. |
| Interface et rendu | Qt Quick / QML | À conserver pour l'UI. Afficher une prévisualisation avec `Image`; utiliser un provider pour exposer une image calculée par C++. |
| Filtres, géométrie, morphologie, contours, vision | OpenCV (`core`, `imgproc`, `imgcodecs`) | Le meilleur choix par défaut pour compléter Qt. Commencer avec ces trois modules, sans ajouter les modules de vision avancée prématurément. |
| Tâches en arrière-plan | Qt Concurrent ou `QThreadPool` | Indispensable dès que l'image dépasse une petite vignette. |
| Algèbre linéaire (facultatif) | Eigen | Utile pour matrices, homographies et calculs numériques propres ; OpenCV couvre déjà beaucoup de besoins usuels. |
| Images très grandes / traitement par lot | libvips | À considérer pour TIFF géants, pyramides et conversion de masse : calcul à la demande, multi-cœur et faible mémoire. Ne pas l'ajouter au premier prototype. |
| Imagerie scientifique, médicale, 3D / N-D | ITK | Spécialisé en segmentation, recalage et images N-D. Trop lourd pour un éditeur photo 2D généraliste. |

OpenCV 4.5 et ultérieur est sous licence Apache 2.0. Vérifier séparément la licence de Qt choisie (LGPL/GPL ou commerciale) et celle des codecs distribués avec l'application.

## Fondamentaux

### 1. Image numérique

Une image matricielle est une grille de pixels. Pour une image de largeur `W`, hauteur `H`, `C` canaux et `B` octets par canal, son ordre de grandeur mémoire est :

```text
mémoire = W × H × C × B
```

Exemple : une image 6 000 × 4 000 en RGBA 8 bits occupe environ `6000 × 4000 × 4 = 96 Mo` une fois décodée, même si son JPEG sur disque ne pèse que quelques Mo. Une application d'édition doit donc éviter de garder de nombreuses copies pleine définition.

Les propriétés essentielles sont :

- **Résolution** : nombre de pixels. Elle détermine les détails disponibles, pas la taille physique d'impression à elle seule.
- **Canaux** : gris (1), RGB (3), RGBA (4). L'alpha décrit la transparence, pas la luminosité.
- **Profondeur** : 8 bits/canal (0–255) est le minimum courant ; 16 bits/canal et flottant évitent des pertes dans les corrections exigeantes ou les sources RAW/HDR.
- **Disposition mémoire** : intercalée (`RGBRGB…`) ou planes séparés. Elle doit être connue avant de partager un tampon entre bibliothèques.
- **Pas de ligne** (`bytesPerLine` / stride) : une ligne peut contenir du remplissage ; ne jamais supposer `largeur × nombre_de_canaux`.

### 2. Couleur, gamma et alpha

RGB est adapté à l'affichage, mais pas toujours à l'opération voulue :

- **Niveaux de gris** : une luminance perceptuelle s'obtient souvent par `Y ≈ 0,2126 R + 0,7152 G + 0,0722 B` en espace linéaire (coefficients Rec. 709/sRGB).
- **HSV/HSL** : pratiques pour un sélecteur de teinte et certaines corrections visuelles, moins pour des calculs physiques.
- **Lab** : plus proche de la perception ; utile pour séparer luminosité et chrominance.
- **YCrCb / YUV** : fréquent en vidéo et en détection de peau.

Les fichiers sRGB sont encodés avec une courbe de transfert (souvent appelée « gamma »). Additionner, flouter ou mélanger des couleurs directement dans cet espace peut créer des résultats trop sombres. Pour les opérations qui modélisent la lumière, convertir vers un espace RGB linéaire, calculer, puis revenir vers sRGB.

Avec un alpha prémultiplié, les composantes RGB sont déjà multipliées par alpha. C'est efficace pour la composition, mais il ne faut pas le confondre avec un RGBA droit : mélanger les deux produit des franges sombres ou claires. Pour le rendu Qt, `QImage::Format_ARGB32_Premultiplied` est généralement un bon format ; pour les échanges avec OpenCV, un format 8 bits explicite comme RGBA/BGRA est plus simple à contrôler.

Un profil ICC indique l'espace colorimétrique réel de l'image. Une image sans profil ne devrait pas être traitée arbitrairement comme si elle était une photo sRGB : adopter une règle documentée, par exemple « supposer sRGB si aucun profil n'est embarqué ».

### 3. Convolution et filtrage spatial

Un filtre spatial calcule chaque pixel à partir de son voisinage. Pour un noyau `K`, la convolution discrète est :

```text
g(x, y) = somme(i, j) K(i, j) × f(x - i, y - j)
```

Les cas courants :

| Objectif | Exemples | Point d'attention |
| --- | --- | --- |
| Réduire le bruit | moyenne, gaussien, médian, bilatéral | Le flou réduit aussi les détails. Le médian est efficace contre le bruit impulsionnel. |
| Accentuer les détails | Laplacien, unsharp mask, high-pass | Contrôler le bruit et l'écrêtage après accentuation. |
| Détecter les bords | Sobel, Scharr, Canny | Le seuil dépend du bruit, du contraste et de la profondeur. |
| Transformer la géométrie | rotation, redimensionnement, perspective | Employer une interpolation : nearest, bilinéaire, bicubique ou Lanczos. |

Les bords de l'image exigent une convention : réplication, réflexion, constante ou recadrage. Ce choix est visible, surtout avec de gros flous ; il doit être identique dans l'aperçu et à l'export.

### 4. Histogrammes et corrections de tonalité

L'histogramme compte les pixels par niveau d'intensité. Il aide à détecter :

- une sous-exposition (valeurs regroupées à gauche) ;
- une surexposition ou de l'écrêtage (valeurs collées à droite) ;
- un contraste faible (histogramme étroit).

Les corrections usuelles sont les niveaux (noir, blanc, gamma), les courbes, le contraste et l'égalisation d'histogramme. Toute opération doit annoncer si elle peut écrêter des valeurs ; un traitement en 16 bits ou flottant diminue les pertes entre les étapes.

### 5. Morphologie et segmentation

La morphologie travaille sur des régions, à l'aide d'un élément structurant :

- **érosion** : rétrécit les zones claires/objets ;
- **dilatation** : les agrandit ;
- **ouverture** : érosion puis dilatation, pour supprimer de petits objets ;
- **fermeture** : dilatation puis érosion, pour boucher de petits trous.

Une chaîne classique de segmentation est : prétraitement → seuillage → morphologie → composantes connexes ou contours → mesure/filtrage des régions. Le seuil peut être fixe, adaptatif ou déterminé automatiquement (Otsu).

### 6. Domaine fréquentiel

La transformée de Fourier représente les variations lentes et rapides de l'image. Un filtrage passe-bas atténue les hautes fréquences (détails/bruit) ; un passe-haut accentue les détails. C'est puissant pour certains motifs périodiques, mais la plupart des outils d'un éditeur classique se réalisent plus simplement et plus efficacement dans le domaine spatial.

## Principes d'implémentation

### Pipeline non destructif

Ne modifiez pas définitivement l'original à chaque clic. Conserver :

```text
Image originale (lecture seule)
        │
        ├── opérations paramétrées : recadrage, niveaux, flou, rotation…
        │
        ├── rendu de prévisualisation, réduit si nécessaire
        │
        └── rendu d'export, pleine définition
```

Une opération est une petite structure sérialisable : son type, ses paramètres, son état activé/désactivé et éventuellement un masque. L'historique contient les opérations, pas une image complète par action. Cela rend annuler/rétablir fiable, permet de sauvegarder un projet et évite les copies mémoire inutiles.

Exemple de modèle C++ :

```cpp
enum class OperationKind { Crop, Grayscale, Levels, GaussianBlur, Rotate };

struct Operation {
    OperationKind kind;
    QVariantMap parameters;
    bool enabled = true;
};

class ImageDocument {
public:
    QImage original;                 // Ne jamais modifier après ouverture
    QVector<Operation> operations;   // Historique éditable
    QImage render(const QSize& targetSize) const;
};
```

Pour une première version, rendre depuis l'original à chaque modification est simple et correct. Plus tard, mettre en cache des étapes intermédiaires, avec une clé dépendant de l'opération et de sa révision.

### Responsabilités Qt/QML

| Couche | Responsabilité |
| --- | --- |
| QML | Menus, panneaux, curseurs, raccourcis, zoom/panoramique, état visuel. |
| Contrôleur C++ (`QObject`, `QML_ELEMENT`) | Commandes utilisateur, validation des paramètres, signaux de progression et d'erreur. |
| Moteur C++ | Chargement, modèle de document, calcul des opérations, export. Ne dépend pas de QML. |
| Worker / pool de threads | Rendu de la prévisualisation et export coûteux. |
| Adaptateur d'affichage | Expose le résultat au `Image` QML, par provider ou texture. |

`QImage` est approprié au travail sur pixels et peut être utilisé avec `QPainter` hors du thread GUI. Réserver `QPixmap` et les objets graphiques Qt Quick au rendu/au thread approprié. Copier un `QImage` est bon marché tant qu'il reste partagé ; une modification déclenche une copie des données (copy-on-write). Ce mécanisme est pratique mais ne dispense pas de surveiller la mémoire.

### Prévisualisation dans QML

Pour afficher une image produite par C++, un `QQuickImageProvider` est adapté. Il est enregistré une fois dans `main.cpp`, puis consommé par une URL `image://` dans QML. L'URL doit évoluer après chaque rendu, par exemple `image://document/preview?revision=42`, afin d'éviter un résultat périmé dans le cache, ou l'élément `Image` doit avoir `cache: false`.

```qml
Image {
    id: canvas
    source: "image://document/preview?revision=" + imageController.revision
    cache: false
    asynchronous: true
    fillMode: Image.PreserveAspectFit
}
```

Pour les petites opérations, le provider `QImage` suffit. Pour des calculs longs et plusieurs requêtes concurrentes, utiliser `QQuickAsyncImageProvider` avec un `QThreadPool` : un provider synchrone ne doit jamais bloquer l'interface. Limiter `sourceSize` quand une image sert seulement d'aperçu, car les images décodées peuvent être la principale source de mémoire de l'interface.

### Chargement et export robustes

Préférer `QImageReader` à `QImage::load()` dans une application d'édition : il fournit les erreurs, la taille, la lecture d'une région et le redimensionnement lors du décodage. Activer `setAutoTransform(true)` pour respecter l'orientation EXIF. Vérifier systématiquement l'échec de lecture et afficher `errorString()`.

```cpp
QImage loadImage(const QString& fileName)
{
    QImageReader::setAllocationLimit(512); // limite globale, à ajuster
    QImageReader reader(fileName);
    reader.setAutoTransform(true);       // orientation EXIF

    const QImage image = reader.read();
    if (image.isNull())
        throw std::runtime_error(reader.errorString().toStdString());
    return image;
}
```

Pour l'export, choisir explicitement format, qualité et chemin. Préserver ou définir délibérément le profil ICC et les métadonnées utiles. Les images de provenance inconnue sont des entrées non fiables : imposer des limites de dimensions/mémoire, gérer les erreurs de décodeur, et ne pas déduire le type seulement de l'extension.

## Interopérabilité Qt ↔ OpenCV

`QImage` et `cv::Mat` peuvent partager un même tampon seulement si format, ordre des canaux, stride et durée de vie sont maîtrisés. Sinon, faire une conversion explicite : elle est plus sûre et souvent négligeable devant un filtre lourd.

```cpp
#include <QImage>
#include <opencv2/imgproc.hpp>

QImage toGray(const QImage& input)
{
    const QImage rgba = input.convertToFormat(QImage::Format_RGBA8888);

    cv::Mat rgbaMat(rgba.height(), rgba.width(), CV_8UC4,
                    const_cast<uchar*>(rgba.constBits()),
                    rgba.bytesPerLine());
    cv::Mat gray;
    cv::cvtColor(rgbaMat, gray, cv::COLOR_RGBA2GRAY);

    // copy() donne au QImage une mémoire qu'il possède réellement.
    return QImage(gray.data, gray.cols, gray.rows, gray.step,
                  QImage::Format_Grayscale8).copy();
}
```

La vue `cv::Mat` ci-dessus n'est valable que tant que `rgba` existe et n'est pas détachée/modifiée. Pour une opération en arrière-plan, capturer une copie ou transférer la propriété de manière explicite. Vérifier également les conversions : OpenCV utilise fréquemment BGR/BGRA, tandis que le format choisi dans l'exemple est RGBA.

Exemple de filtre OpenCV :

```cpp
cv::GaussianBlur(source, destination, cv::Size(0, 0), sigmaX);
cv::Canny(gray, edges, thresholdLow, thresholdHigh);
cv::morphologyEx(binary, cleaned, cv::MORPH_OPEN, kernel);
```

## Dépendances CMake minimales

Le projet utilise actuellement Qt Quick 6.4. Pour ajouter le socle OpenCV :

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Quick Gui Concurrent)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

target_link_libraries(appIPR
    PRIVATE
        Qt6::Quick
        Qt6::Gui
        Qt6::Concurrent
        ${OpenCV_LIBS}
)
```

Sous Windows, un gestionnaire de dépendances tel que vcpkg simplifie l'installation reproductible d'OpenCV. Épingler la configuration utilisée par le projet et documenter l'outilchain CMake ; ne pas copier de DLL manuellement dans le dépôt. Ajouter des composants OpenCV seulement lorsque leur usage est réel (`features2d`, `calib3d`, `dnn`, etc.).

## Feuille de route réaliste pour IPR

1. **Socle Qt propre** — ouvrir avec `QImageReader`, conserver l'original, afficher via une seule source de vérité C++ et corriger l'orientation EXIF.
2. **Prévisualisation** — zoom/panoramique, redimensionnement de prévisualisation, tâche de rendu annulable ou ignorée si une révision plus récente existe.
3. **Premières opérations** — niveaux/gris, rotation, recadrage, redimensionnement ; paramètres visibles dans QML et opérations non destructives.
4. **OpenCV** — flou gaussien, médian, Canny, seuil/morphologie ; comparer le résultat avec de petites images de référence.
5. **Fiabilité** — annuler/rétablir, sauvegarde de projet, export JPEG/PNG, messages d'erreurs, limites mémoire.
6. **Optimisation ciblée** — cache d'étapes, traitement par tuiles pour les grands fichiers, éventuellement libvips ou rendu GPU après profilage.

L'application met désormais ce flux en pratique : `ImageEditor` conserve l'original et les réglages, `ImageProvider` expose le rendu sous `image://editor/preview?revision=…`, et l'élément `Image` QML consomme cette URL. Le numéro de révision et `cache: false` empêchent l'affichage d'un aperçu dépassé après un nouveau réglage.

## Règles de qualité

- Garder l'original immuable et appliquer les opérations à partir d'un modèle explicite.
- Définir le format, la profondeur, l'espace couleur et l'alpha attendus à l'entrée et à la sortie de chaque opération.
- Ne pas exécuter de rendu pleine définition dans le thread de l'interface.
- Séparer prévisualisation et export : même algorithme, résolution et priorité différentes.
- Tester avec des images couleur, grises, transparentes, orientées EXIF, très grandes et corrompues.
- Pour chaque filtre, fournir une image de référence et une tolérance numérique ou un test visuel reproductible.
- Mesurer avant d'optimiser : temps de décodage, de rendu, mémoire maximale et réactivité de l'UI.

## Références officielles

- [Qt — QImage](https://doc.qt.io/qt-6/qimage.html) : formats, accès mémoire, partage implicite et conversion.
- [Qt — QImageReader](https://doc.qt.io/qt-6/qimagereader.html) : lecture contrôlée, orientation et limites d'allocation.
- [Qt — QQuickImageProvider](https://doc.qt.io/qt-6/qquickimageprovider.html) : URLs `image://`, cache et chargement asynchrone.
- [Qt — type QML Image](https://doc.qt.io/qt-6/qml-qtquick-image.html) : `asynchronous`, `sourceSize` et impact mémoire.
- [OpenCV — documentation](https://docs.opencv.org/) et [licence OpenCV](https://opencv.org/license/).
- [libvips](https://github.com/libvips/libvips) : traitement à la demande et grandes images.
- [ITK](https://docs.itk.org/en/latest/index.html) : traitement scientifique, segmentation et recalage N-D.
