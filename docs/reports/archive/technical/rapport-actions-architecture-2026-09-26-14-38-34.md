# Rapport des actions et de l'architecture IPR

**Date du rapport :** 26 septembre 2026  
**Périmètre :** traitement d'image, ouverture CR2/CR3, interface couleur, validation de compilation et architecture de gestion des couleurs.

## Synthèse

La session a progressivement corrigé le pipeline tonal et l'ouverture des RAW Canon, ajouté des contrôles couleur dans l'interface QML et comparé l'approche de décodage CR3 à RawTherapee. L'architecture actuelle sépare l'interface, l'état des réglages, le décodage, le rendu et la sortie, mais le flux reste limité à des images RGB sRGB 8 bits après le décodage RAW.

Les améliorations de gestion des couleurs décrites ci-dessous sont une **architecture cible**, et non une fonctionnalité entièrement implémentée. En particulier, le pipeline flottant linéaire, les profils ICC/DCP, la sortie RAW haute précision et le correctif complet de l'export restent à réaliser.

## Actions menées

### 1. Réglages de lumière et de tonalité

- Vérification des formules d'exposition, de contraste, d'ombres, de noirs et de hautes lumières.
- Remplacement des multiplicateurs tonals identiques par des masques de luminance différenciés.
- Implémentation de l'exposition par facteur en stops.
- Implémentation du contraste autour du gris moyen.
- Ajout du réglage « Blancs », distinct des hautes lumières, dans le modèle C++, les interfaces QML et le reset.
- Conservation d'une plage flottante intermédiaire avant écrêtage et conversion en 8 bits.

### 2. Ouverture et décodage RAW

- Correction de la détection d'extension dans `ImageProvider`.
- Transmission de l'URL sélectionnée au provider.
- Centralisation des appels LibRaw dans `Cr3Decoder.cpp`.
- Séparation des fonctions publiques `loadCr2()` et `loadCr3()` ; l'extension choisit la fonction correspondante.
- Suppression de la correction de neutralisation RGB globale après développement, car elle travaillait sur une image sRGB déjà quantifiée et pouvait créer des dérives ou un voile.
- Séparation des paramètres LibRaw :
  - CR2 conserve le profil qui donnait déjà un rendu satisfaisant ;
  - CR3 utilise la balance caméra lorsque le mode automatique est demandé et désactive `no_auto_bright` pour limiter le voile blanc.
- Consultation de `canon_cr3_decoder.cc` et `dcraw.h` de RawTherapee. L'analyse a établi que RawTherapee sépare le parsing du conteneur CR3/flux CRX et le traitement RAW ; LibRaw fournit déjà un décodeur CRX similaire, ce qui rendait une copie directe du code RawTherapee non pertinente pour IPR.

### 3. Balance des blancs et interface couleur

- Ajout du contrôle « Balance des blancs automatique » dans le `ColumnLayout` « Couleur ».
- Ajout du curseur « Nuance vert ↔ magenta » et liaison à `ImageEditor::setTint()`.
- Remplacement du contrôle « Température » qui appelait à tort le réglage d'exposition.
- Reliaison de la case de balance des blancs au redécodage des RAW CR2/CR3.
- Synchronisation du composant `Setting.qml` avec `Main.qml`.
- Ajout de la remise à zéro de la nuance et du retour à la balance automatique.

La nuance actuelle est un ajustement RGB simple. Elle est utile comme contrôle initial, mais n'est pas encore une correction colorimétrique perceptuelle calibrée.

### 4. Correction de compilation et tests

Une erreur de nombre d'arguments dans l'appel `loadRawImage()` a été reproduite avec Ninja dans WSL. L'appel a été corrigé.

Deux configurations ont ensuite compilé avec succès dans WSL Ubuntu :

- `IPR_ENABLE_LIBRAW=OFF` ;
- `IPR_ENABLE_LIBRAW=ON`, avec LibRaw 0.21.2.

Ces builds vérifient la compilation des variantes avec et sans LibRaw. Ils ne remplacent pas une validation visuelle avec des CR2/CR3 réels sous Windows.

## Architecture actuellement implémentée

```text
Main.qml / Setting.qml
    │ interaction QML
    ▼
ImageEditor (API Q_INVOKABLE + état Settings)
    ├── ouverture des fichiers standard par QImageReader
    ├── dispatch CR2 -> loadCr2()
    └── dispatch CR3 -> loadCr3()
                │
                ▼
        Cr3Decoder.cpp / LibRaw
                │ RGB888 sRGB 8 bits
                ▼
        m_original : QImage RGBA8888
                │
                ├── rendu asynchrone d'aperçu
                └── export asynchrone
                         │
                         ▼
                 renderImage() commun
                 OpenCV, CV_32FC3 normalisé
                         │
                         ├── ImageProvider -> image://editor/... -> QML
                         └── QImageWriter -> fichier de sortie
```

### Responsabilités des composants

- **QML** : expose les curseurs et cases à cocher, transmet les changements au contrôleur.
- **`ImageEditor`** : conserve les paramètres, choisit le chemin d'ouverture, planifie le rendu et l'export asynchrones.
- **`Cr3Decoder`** : configure LibRaw selon l'extension RAW et retourne une image Qt.
- **`renderImage()`** : applique les réglages d'image communs à l'aperçu et à l'export.
- **`ImageProvider`** : expose l'image rendue à Qt Quick.
- **`QImageWriter`** : écrit le résultat rendu dans le format de sortie demandé.

### Limites actuelles importantes

1. LibRaw produit une image RGB sRGB 8 bits : les données linéaires RAW, la dynamique et une partie de la précision sont déjà perdues avant le pipeline OpenCV.
2. Le tampon `CV_32FC3` est issu de ces valeurs sRGB divisées par 255. Il n'est donc **pas linéaire**, même s'il est stocké en flottants.
3. La conversion et les profils d'entrée ICC ne sont pas gérés explicitement pour les images standard.
4. Le choix de matrice caméra est activé, mais aucun profil DCP utilisateur ni pipeline de profil caméra complet n'est implémenté.
5. La nuance est réalisée par des gains de canaux RGB ; elle n'est pas exprimée en axe perceptuel vert-magenta dans un espace de couleur adapté.
6. L'aperçu et l'export partagent la fonction de rendu, mais l'export n'a pas encore été validé sur les profils, les métadonnées, l'alpha, l'orientation et les différences de format.

## Architecture cible pour la gestion avancée des couleurs

Le point de gestion avancée des couleurs recommandé lors de la session est intégré ici comme cible d'évolution :

```text
Source (RAW ou image profilée)
    -> décodage sans perte évitable + profil d'entrée
    -> espace de travail linéaire flottant, large gamut et grande dynamique
    -> balance des blancs et corrections caméra
    -> transformations colorimétriques et réglages tonals
    -> conversion de gamut vers l'espace de sortie
    -> encodage du transfert de sortie (ex. sRGB)
    -> quantification / alpha / format
    -> aperçu et export issus du même rendu final
```

### Orientations techniques

1. **Remplacer l'intermédiaire RAW 8 bits** : évaluer une sortie LibRaw 16 bits et configurer explicitement la courbe de transfert pour obtenir des données linéaires. Confirmer par test les paramètres LibRaw exacts (notamment gamma, profondeur, matrice et balance) plutôt que supposer qu'une sortie 16 bits est automatiquement linéaire.
2. **Introduire un type d'image interne explicite** : stocker les pixels flottants et documenter primaires, transfert, ordre des canaux, plage dynamique et alpha. Une structure dédiée peut contenir `cv::Mat`, espace de couleur, transfert et métadonnées RAW.
3. **Convertir les entrées non RAW** : lire le profil ICC embarqué et convertir vers l'espace de travail ; utiliser sRGB uniquement lorsque le profil est absent et le comportement de repli clairement défini.
4. **Séparer balance et rendu** : appliquer les coefficients de balance RAW avant dématriçage lorsque possible ; laisser LibRaw prendre en charge les données RAW, puis proposer les réglages manuels dans une étape définie. Ne pas estimer une neutralité à partir d'une moyenne globale arbitraire.
5. **Organiser les traitements** :
   - corrections d'exposition dans un domaine linéaire ;
   - contraste et courbes selon une luminance définie ;
   - réglages de teinte/saturation dans un espace perceptuel adapté ou via une méthode conservant la luminance ;
   - réduction du bruit avant accentuation et avant la sortie ;
   - écrêtage et tone mapping uniquement à un stade contrôlé.
6. **Centraliser la sortie** : créer une étape unique de conversion d'espace, de gestion de gamut et d'encodage sRGB (ou profil cible). Le rendu destiné à l'écran et celui de l'export doivent consommer la même sortie ou le même pipeline paramétré.
7. **Tester avec des références** : utiliser une charte ColorChecker ou gris neutre, des images CR2/CR3 représentatives et un logiciel de référence. Mesurer histogrammes, niveaux, écarts couleur et cohérence aperçu/export.

Il faut procéder par étapes : d'abord définir les contrats et les tests colorimétriques, puis faire évoluer le décodage et le stockage, ensuite convertir les réglages et enfin les formats de sortie. Un simple remplacement de `CV_8UC3` par `CV_32FC3` ne suffit pas à créer un pipeline linéaire.

## Travaux restant à réaliser

### Gestion avancée des couleurs

- rendre disponible et conserver une sortie RAW 16 bits non écrêtée ;
- établir un espace de travail linéaire, flottant et de gamut suffisamment large ;
- intégrer les profils ICC des entrées et sorties, puis évaluer les profils DCP caméra ;
- replacer balance des blancs, nuance et matrice caméra dans des étapes colorimétriques documentées ;
- choisir et tester une transformation de gamut/tone mapping vers sRGB ;
- ajouter des tests basés sur charte et comparaison de références.

### Réduction du bruit

- ajouter des réglages distincts de luminance et de chrominance ;
- commencer avec un filtre guidé ou bilatéral, avec intensité nulle par défaut ;
- positionner la réduction avant accentuation/courbes ;
- permettre une évaluation à 100 % sans réduire le détail fin.

### Courbes de tonalités

- définir une courbe neutre par défaut et des points de contrôle ;
- utiliser une interpolation monotone pour éviter les oscillations ;
- modifier prioritairement la luminance afin de limiter les dérives de teinte ;
- fournir des préréglages et des tests de monotonie.

### Correctif et validation de l'export

- contrôler alpha et profondeur par format (JPEG sans alpha, PNG/TIFF selon le choix utilisateur) ;
- vérifier orientation et conservation souhaitée des métadonnées ;
- incorporer ou assigner explicitement le profil colorimétrique de sortie ;
- vérifier le réglage de qualité par format ;
- exporter, relire puis comparer dimensions, alpha, profils et histogrammes avec le résultat d'aperçu.

### Validation RAW

Constituer un jeu de CR2 et CR3 issus de plusieurs modèles Canon comprenant une charte neutre, des tons chair, des ombres profondes, des hautes lumières et plusieurs températures de lumière. Comparer les sorties d'IPR à RawTherapee, et consigner les paramètres LibRaw choisis pour chaque profil.

## Ordre de réalisation conseillé

1. Créer les tests d'export et le jeu de fichiers de référence RAW.
2. Spécifier les métadonnées et contrats du pipeline couleur.
3. Migrer le décodage RAW et le stockage interne vers une représentation haute précision dont la linéarité est confirmée.
4. Ajouter les conversions ICC et la conversion finale de sortie.
5. Revalider et ajuster balance des blancs, nuance, exposition, contraste et courbes.
6. Implémenter les courbes de tonalités puis la réduction du bruit.
7. Valider les formats d'export et les aperçus sur Windows.

