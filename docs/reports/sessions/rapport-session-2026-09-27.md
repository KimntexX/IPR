# Rapport de session — bilan du 27 septembre 2026

**Date :** 27 septembre 2026  
**Périmètre :** développement RAW, profils DCP/LCP, prise en charge DNG, diagnostic du rendu CR3 Canon et feuille de route du pipeline photo.

## Synthèse

La session a étendu et vérifié le chemin de développement RAW d’IPR, du chargement des profils Adobe jusqu’à l’affichage QML. La découverte des profils a été réparée pour Windows/WSL, le format DNG a été ajouté au registre LibRaw et le moteur DCP/LCP a été ajusté pour les profils Adobe réellement installés.

Le voile observé sur le CR3 `6V1A1236.CR3` a ensuite été localisé dans son niveau noir : LibRaw ne fournissait pas de niveau noir exploitable pour ce CR3, alors que le DNG issu des mêmes données embarquait un motif de noir à 2048. Les valeurs mosaïque décodées par LibRaw sont identiques pixel à pixel. Un repli ciblé estime maintenant le niveau noir depuis les marges masquées du capteur pour les CR3 Canon concernés. L’utilisateur a confirmé que le résultat visuel est bon.

Les deux configurations de compilation WSL — avec et sans LibRaw — ont compilé et passé leurs trois tests CTest. Les sondes d’intégration réelles confirment l’application du repli au CR3, la conservation du traitement DNG existant et la sélection des profils DCP/LCP. La validation graphique finale relève de l’observation de l’utilisateur ; le binaire Windows n’a pas été recompilé ou vérifié dans cette session.

## Travaux réalisés

### 1. Vérification des réglages tonals et de l’ouverture

- Vérification de la distinction entre exposition, contraste, ombres, noirs, hautes lumières et blancs dans le pipeline de rendu.
- Confirmation que les réglages de l’éditeur sont réinitialisés à l’ouverture et que le probe RAW n’applique pas les contrôles de l’interface.
- Vérification du parcours `ImageEditor` → `RawDecoder` → `ColorPipeline` → `ImageProvider` → QML. Le provider sert l’aperçu développé par l’éditeur ; il n’effectue pas de correction spéciale CR3 dans le parcours habituel.
- Harmonisation du comportement de balance des blancs LibRaw et maintien d’un mode automatique par défaut. Le mode de balance caméra a été comparé sur les deux fichiers et s’est avéré inadapté au CR3 échantillon ; il n’explique pas le voile constaté.

### 2. Moteur de profils DCP/LCP

Le moteur interne a été raccordé à l’ouverture RAW et renforcé pour les profils Adobe trouvés sur le poste :

- lecture des dimensions et tailles de tables DCP Adobe rencontrées ;
- sélection du DCP Adobe Standard Canon même en l’absence de restriction caméra explicite dans le fichier ;
- correspondance du nom d’objectif Sigma entre les métadonnées LibRaw et les alias des profils LCP Adobe ;
- lecture des coefficients LCP de vignettage et correction de leur application pour éviter une atténuation globale excessive ;
- prise en charge des tables DCP secondaires, repli par ColorMatrix lorsque nécessaire, interpolation multi-illuminants simplifiée, courbe tonale et compensation DCP de base ;
- prise en charge des variantes LCP pour calibrations multiples, distorsion, vignettage et aberration chromatique rouge/bleue, avec limites décrites dans la documentation.

Pour `6V1A1236`, le moteur trouve :

- DCP : `Canon EOS R6 Mark III Adobe Standard` ;
- LCP : `Canon (SIGMA 70-200mm F2.8 DG OS HSM S018) - RAW`.

Ces résultats confirment la sélection et l’application des profils, mais ne constituent pas une certification de conformité DNG/LCP ni une comparaison colorimétrique exhaustive avec Adobe.

### 3. Recherche Adobe sous WSL et prise en charge DNG

- Ajout de la recherche automatique sous `/mnt/c/ProgramData/Adobe/CameraRaw` sous WSL, ainsi que de `IPR_ADOBE_CAMERARAW_DIR` pour un emplacement personnalisé.
- Ajout de `.dng` au registre des extensions LibRaw. Le DNG suit le même trajet d’ouverture, de balance des blancs, de profilage, d’aperçu et d’export que les autres RAW.
- Mise à jour du filtre du navigateur, de l’étiquette de formats, des messages de compilation et de la documentation.
- Ajout d’une option `--camera-wb` au probe d’intégration pour comparer la balance automatique et la balance enregistrée dans le fichier.

Le probe confirme que le même DCP et le même LCP sont trouvés pour le CR3 et le DNG. Les profils sont réellement appliqués, mais leur sélection n’expliquait pas à elle seule l’écart de rendu initial.

### 4. Diagnostic et correction du niveau noir CR3 Canon

Les fichiers appariés `tests/image/6V1A1236.CR3` et `tests/image/6V1A1236.dng` ont été étudiés avec LibRaw :

- mêmes paramètres de prise de vue rapportés : ISO 4000, 1/800 s, f/8, 70 mm ;
- données de mosaïque identiques pixel à pixel à la résolution RAW commune de 7144 × 4760 ;
- DNG : motif de noir lu par LibRaw à 2048, niveau blanc déclaré à 14351 ;
- CR3 : niveau noir lu à zéro par LibRaw malgré un piédestal mesuré dans les marges optiques ;
- médianes mesurées dans ces marges, séparément par position CFA : 2053 à 2054.

Le correctif de production dans `RawDecoder.cpp` est volontairement conditionnel :

1. il ne concerne que les fichiers CR3 dont le fabricant est Canon ;
2. il ne s’active que si LibRaw ne fournit ni niveau noir global, ni motif noir, ni niveau de motif déjà utilisable ;
3. il exige une image mosaïque CFA et des dimensions de zone active/marges valides ;
4. il estime une médiane indépendante pour les quatre positions CFA dans les marges hors zone active ;
5. il rejette les estimations absentes ou hors d’une plage plausible, puis transmet les niveaux acceptés à LibRaw avant le dématriçage ;
6. il laisse le chemin des DNG inchangé lorsqu’un motif noir est déjà présent.

Le rendu CR3 profilé sans correction avait des moyennes RGB linéaires d’environ `0,169 / 0,168 / 0,169`. Après correction, elles sont d’environ `0,0183 / 0,0182 / 0,0181` ; le DNG de référence est à `0,0220 / 0,0219 / 0,0217`. Sans profils, le CR3 passe d’environ `0,1367` à `0,0153`, contre `0,0182` pour le DNG. Les moyennes sont un indicateur de diagnostic, pas une métrique complète de qualité d’image.

Le DNG contient aussi `BaselineExposure = +0.28 EV`. Cela peut expliquer une partie de l’écart résiduel de luminosité ; aucune compensation n’a été imposée au CR3 en l’absence de métadonnée équivalente.

L’interface ajoute un indicateur lorsque le niveau noir a été estimé et le probe affiche la provenance du niveau noir. La documentation générale et celle du moteur décrivent le périmètre et les limites du repli.

### 5. Documentation et licences

- Mise à jour du README et de la documentation technique DCP/LCP, notamment sur les profils sous WSL, le DNG et le niveau noir CR3.
- Rapports de diagnostic et d’intégration générés sous `docs/rapport`, conformément à la préférence de rangement indiquée.
- Maintien de l’inventaire des licences des dépendances et de la comparaison des options de licence du projet. Aucune licence racine IPR n’a été sélectionnée par cette session.
- Aucun profil constructeur n’a été fabriqué ; seuls des profils disponibles localement ont été sélectionnés.

## Architecture actuelle

```text
Main.qml
  └─ imageEditor.loadImage(fileUrl)
       ├─ Image standard : QImageReader → ColorPipeline
       └─ RAW : RawDecoder (registre extension)
            └─ LibRaw : open → unpack → niveaux noir si nécessaire
                 → développement RGB 16 bits linéaire
                 → LCP (corrections optiques)
                 → DCP (couleur caméra)
       └─ Image de travail RGBA flottante, sRGB linéaire
            ├─ rendus non destructifs asynchrones / OpenCV
            ├─ aperçu QML via ImageProvider
            └─ export via le même pipeline de rendu
```

Responsabilités principales :

- `RawDecoder` choisit le backend à partir de l’extension, configure LibRaw et prépare les données RAW et métadonnées ;
- `CameraProfileEngine` découvre et applique les profils DCP/LCP ;
- `ColorPipeline` gère l’image de travail, les conversions sRGB et les formats de sortie ;
- `ImageEditor` porte l’état d’édition, les réglages, le rendu et l’export asynchrones ;
- `ImageProvider` remet l’aperçu au moteur QML.

Le traitement interne actuel utilise une image RGBA flottante sRGB linéaire. Les profils d’entrée ICC, la gestion complète des primaires larges, les courbes d’édition, un module dédié de réduction du bruit et un cache de préchargement borné ne sont pas encore des fonctions complètes.

## Évolutions proposées

Les évolutions ci-dessous constituent une feuille de route, non des fonctionnalités livrées dans cette session. La priorité est d’abord la justesse mesurable du pipeline, puis l’ergonomie et la performance.

### Priorité 1 — Sécuriser et documenter le développement RAW

1. Ajouter des tests reproductibles du correctif de niveau noir : tests unitaires des conditions de garde, puis tests d’intégration sur plusieurs CR3 Canon, et tests négatifs sur DNG/CR2 et CR3 qui possèdent déjà des niveaux.
2. Vérifier les marges optiques et les médianes sur plusieurs modèles Canon et niveaux ISO/température, car les marges peuvent être absentes, recadrées ou ne pas représenter un noir stable pour d’autres capteurs.
3. Contrôler avec un outil de référence les niveaux noir/blanc, `AsShotNeutral`, `BaselineExposure`, les matrices et les zones de recadrage des DNG.
4. Ajouter si possible des RAW de test redistribuables ; à défaut, fournir un corpus local documenté et une sonde de diagnostic versionnée, sans incorporer d’images privées au dépôt.
5. Produire une sortie de comparaison avec paramètres identiques (profil, WB, optique, exposition) et charte neutre afin de séparer dérive de couleur et exposition.

**Critère d’acceptation :** aucune modification imprévue des DNG/CR2 ou des CR3 avec niveau noir fourni ; rendu reproductible sur plusieurs CR3 concernés et rendu cohérent avec une référence sur des plages neutres et les hautes lumières.

### Priorité 2 — Gestion des couleurs de bout en bout

- Rendre le type de pixels interne explicite : primaires, point blanc, fonction de transfert, ordre des canaux, alpha, plage/dynamique et provenance du profil.
- Lire et respecter les profils ICC embarqués pour JPEG/TIFF/PNG ; documenter le repli sRGB quand le profil manque ou est invalide.
- Étudier la conversion des RAW caméra vers un espace de travail linéaire à gamut plus large que sRGB. Choisir l’espace (par exemple un espace de travail linéaire large gamut) après tests des profils DCP et du traitement des valeurs hors gamut, plutôt que de changer les primaires sans mesurer les conséquences.
- Séparer strictement les étapes : dématriçage et balance RAW, transformation caméra/DCP, espace de travail, corrections créatives, conversion de gamut et transfert de sortie.
- Valider la sortie écran, aperçu QML et export avec les mêmes transformations, métadonnées ICC, alpha, profondeur et orientation.
- Remplacer progressivement les approximations actuelles de température, teinte et interpolation des matrices par un traitement documenté et testé ; comparer l’application du DCP à des rendus de référence Adobe/RawTherapee.

**Critère d’acceptation :** images de référence connues donnent les mêmes valeurs et le même rendu en aperçu et export, à la tolérance définie ; les conversions ICC préservent l’apparence entre sRGB et les autres profils d’entrée pris en charge.

### Priorité 3 — Courbes de tonalité et analyse de l’image

- Ajouter un histogramme RGB/luminance calculé sur l’image avant édition et, si utile, sur l’aperçu après édition.
- Ajouter une courbe de tonalité utilisateur avec points contrôlables, interpolation monotone, prévisualisation rapide et bouton de remise à zéro.
- Spécifier précisément l’ordre de la courbe par rapport à l’exposition, au contraste, aux contrôles de tons et à la courbe éventuelle du DCP afin d’éviter les doubles courbes.
- Définir les règles d’écrêtage et préserver une marge flottante/HDR avant encodage final ; afficher les seuils de noirs et hautes lumières écrêtés.
- Ajouter des tests de monotonie, bornes, identité de la courbe neutre et cohérence des rendus aperçu/export.

**Critère d’acceptation :** courbe neutre bit-à-bit ou à faible tolérance identique au pipeline sans courbe ; courbes personnalisées déterministes, sans inversions ni dépassements non documentés.

### Priorité 4 — Réduction du bruit

- Distinguer bruit de luminance et bruit chromatique, et exposer une commande simple avant d’ajouter des réglages avancés.
- Évaluer le débruitage RAW avant ou pendant le dématriçage lorsque le backend le permet (options LibRaw et alternatives spécialisées), puis comparer à un débruitage RGB après développement.
- Garder le grain fin et les détails ; ne pas lisser les contours ou annuler la correction de bruit par la netteté.
- Concevoir des jeux d’images à ISO bas/élevé et des cibles de détail/plages uniformes, avec mesures de bruit, conservation des bords et temps de calcul.
- Traiter le coût mémoire et la parallélisation pour les images RAW pleine résolution.

**Critère d’acceptation :** réduction mesurable du bruit sur plages uniformes avec perte de détail bornée sur les contours et temps de calcul acceptable sur matériel cible.

### Priorité 5 — Préchargement, vignettes et cache

- Séparer l’image de travail pleine résolution de la vignette de navigation ; utiliser d’abord l’aperçu intégré RAW lorsqu’il est adapté, sinon un décodage réduit.
- Précharger de manière asynchrone uniquement les voisins proches de l’image sélectionnée, avec une limite de concurrence.
- Mettre en place un cache LRU borné en mémoire (et éventuellement sur disque), indexé par fichier, date/taille, paramètres de décodage et version des profils.
- Annuler ou ignorer les travaux obsolètes lorsqu’une nouvelle image est choisie ; éviter qu’un ancien décodage ne remplace l’aperçu courant.
- Afficher l’état de chargement et garder l’interface réactive ; prévoir la libération mémoire lors de fermeture de dossier ou de changement de corpus.

**Critère d’acceptation :** navigation rapide après première ouverture, mémoire maximale bornée, image sélectionnée toujours prioritaire et absence d’aperçu périmé après navigation rapide.

### Priorité 6 — Ergonomie et robustesse de production

- Regrouper les curseurs par sections repliables (lumière, couleur, détails, optique) et synchroniser valeur numérique, slider et réinitialisation.
- Enregistrer des réglages non destructifs par image ou en sidecar, sans modifier les originaux.
- Ajouter annuler/rétablir et une comparaison avant/après.
- Vérifier les erreurs utilisateur (profil absent/illisible, RAW corrompu, mémoire insuffisante), éviter les succès silencieux et rendre les avertissements de traitement explicites.
- Définir un plan de licence racine avant distribution et vérifier les licences des binaires réellement embarqués, notamment Qt, LibRaw et tout éventuel SDK DNG.

## Validation effectuée

| Configuration WSL | Compilation | Tests |
|---|---|---:|
| LibRaw activé | Réussie | 3/3 CTest réussis |
| LibRaw désactivé | Réussie | 3/3 CTest réussis |

Les sondes réelles ont également vérifié :

- la sélection des deux profils Adobe sur le CR3 et le DNG ;
- l’estimation du niveau noir sur `6V1A1236.CR3` (R/G1 = 2053, B/G2 = 2054) ;
- le maintien du niveau noir DNG par LibRaw, sans déclenchement du repli ;
- la baisse cohérente du voile et des moyennes du CR3 après le retrait du piédestal.

La capture d’écran fournie après correction et le retour de l’utilisateur confirment que le résultat visuel est satisfaisant. L’observation valide l’objectif visuel sur cet échantillon ; elle ne remplace pas une validation multi-appareils.

## Limites connues et précautions

- Le repli de niveau noir est limité aux CR3 Canon et doit être éprouvé sur davantage de modèles avant d’être considéré générique.
- Les moyennes RGB sont des indicateurs de diagnostic ; elles ne prouvent pas à elles seules la fidélité colorimétrique.
- Le DNG et le CR3 ont des zones actives et des tailles de rendu légèrement différentes.
- Le DNG contient `BaselineExposure = +0.28 EV` ; aucune correction correspondante n’est appliquée au CR3.
- Le moteur DCP/LCP reste une implémentation partielle ; profil trouvé ne signifie pas rendu certifié conforme à Adobe.
- Les validations de compilation ont été effectuées sous WSL. La reconstruction et le contrôle du binaire Windows restent à faire si celui-ci est le binaire distribué.

## Ordre recommandé pour la prochaine session

1. Ajouter les tests automatisés de niveau noir et évaluer le repli sur plusieurs CR3 Canon.
2. Définir le contrat colorimétrique du tampon de travail et les transformations ICC/DCP d’entrée/sortie.
3. Ajouter histogramme et courbe tonale, avec tests partagés entre aperçu et export.
4. Prototyper et mesurer le débruitage sur RAW de plusieurs ISO.
5. Concevoir le préchargement des vignettes et le cache borné avant de paralléliser le développement pleine résolution.
6. Recompiler le binaire Windows cible et vérifier le parcours complet avec un petit corpus CR2/CR3/DNG/JPEG/TIFF.

