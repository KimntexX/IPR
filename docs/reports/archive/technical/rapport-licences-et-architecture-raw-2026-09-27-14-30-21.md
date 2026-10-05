# Rapport — Licences des dépendances et architecture des décodeurs RAW

**Date :** 27 septembre 2026, 14:30  
**Objectifs :** documenter les licences, comparer les licences envisageables pour IPR, rendre le décodage RAW extensible et préparer l’intégration DCP.

## Résumé exécutif

- Ajout d’un inventaire des licences et de leurs textes dans [`licenses/`](../../../../licenses/README.md).
- Ajout d’un registre de décodeurs RAW dans [`RawDecoder.h`](../../../../src/raw/RawDecoder.h) et [`RawDecoder.cpp`](../../../../src/raw/RawDecoder.cpp). CR2 et CR3 restent pris en charge par LibRaw ; un autre format peut être ajouté en enregistrant un callback pour son extension.
- Mise à jour du parcours d’ouverture, de la liste des images et du repli `ImageProvider` afin qu’ils utilisent le même registre.
- Ajout d’un test ciblé d’enregistrement et de dispatch d’un décodeur futur.
- Builds et tests WSL réussis avec et sans LibRaw.
- **Le SDK DNG n’est pas encore intégré au binaire et aucun DCP n’est appliqué.** L’analyse de licence est documentée, mais le raccordement correct d’un profil DCP à la sortie camera-RGB démosaïquée de LibRaw doit encore être conçu et validé. La licence Adobe est distincte des licences open source habituelles ; elle n’est pas incluse implicitement dans une licence LGPL d’IPR.

## Licences ajoutées

Le dossier [`licenses/`](../../../../licenses/README.md) contient les textes suivants :

- Qt : LGPL 3.0 et GPL 3.0 ;
- OpenCV : Apache 2.0 ;
- LibRaw : LGPL 2.1 et CDDL 1.0 ;
- SDK DNG : accord de licence Adobe fourni avec le SDK.

L’inventaire précise que les licences et notices des versions binaires effectivement redistribuées prévalent. Les obligations doivent être vérifiées pour chaque module Qt, paquet LibRaw et mode de liaison. Le projet ne possède toujours pas de fichier `LICENSE` racine : **aucune licence de projet n’a été choisie par ce rapport**.

### Recommandation de licence pour IPR

Pour un éditeur photo qui doit rester réutilisable commercialement tout en partageant ses modifications, LGPL-3.0-or-later est une option raisonnable. Elle convient mieux qu’une licence permissive si le maintien des changements dans le domaine libre est important. LGPL-2.1-or-later est une alternative historique, mais demande une vérification supplémentaire lors de combinaisons avec Apache-2.0.

GPL-3.0-or-later impose un copyleft plus fort et convient si l’objectif est que les versions distribuées de l’application dérivée restent sous des termes compatibles GPL. MIT et Apache-2.0 facilitent davantage la réutilisation fermée ; Apache-2.0 apporte en particulier des clauses de brevets explicites.

**Recommandation conditionnelle :** LGPL-3.0-or-later pour le code d’IPR, dépendances optionnelles et correctement séparées, après revue des obligations de redistribution. Cette recommandation n’ajoute pas la licence au projet et ne constitue pas un avis juridique.

### Réserve particulière au SDK DNG

Le SDK DNG est distribué sous l’accord Adobe reproduit dans [`ADOBE-DNG-SDK.txt`](../../../../licenses/ADOBE-DNG-SDK.txt), et non sous une licence approuvée OSI. Il accorde des droits d’utilisation et de redistribution, mais impose le maintien des notices et contient une clause d’indemnisation en cas de distribution du SDK dans un produit commercial. L’inclusion du texte de licence dans le dépôt ne rend pas le SDK open source.

Il faut donc distinguer les distributions :

1. une construction IPR sans le SDK DNG peut être évaluée selon la licence open source retenue pour IPR et les licences Qt/OpenCV/LibRaw ;
2. une construction avec le SDK DNG est une combinaison avec un composant soumis aux conditions Adobe, qui nécessite une revue juridique et un mécanisme clair de fourniture du SDK et de ses notices ;
3. il ne faut pas présenter l’ensemble des composants de la seconde construction comme étant exclusivement open source.

La licence Adobe complète n’est pas une alternative à l’examen de la licence de la version exacte du SDK que l’on distribue.

## Architecture RAW mise en place

### Registre d’extensions et callbacks

`RawDecoder::registerFormatHandler()` associe une ou plusieurs extensions à une fonction de décodage. Le callback reçoit le chemin, les options et un canal d’erreur explicite. Le registre est protégé par mutex ; le callback est invoqué après libération du verrou.

Le backend LibRaw est enregistré pour `.cr2` et `.cr3`. La configuration actuelle de balance automatique est commune aux deux formats, conformément au correctif précédent. Le registre sépare le choix du conteneur de l’implémentation du pipeline couleur et permet ensuite d’enregistrer un autre backend ou un traitement propre à une famille de boîtiers.

Les points d’entrée concernés sont :

- [`RawDecoder.h`](../../../../src/raw/RawDecoder.h) : options de décodage, type de callback, enregistrement et dispatch ;
- [`RawDecoder.cpp`](../../../../src/raw/RawDecoder.cpp) : registre protégé, backend LibRaw, normalisation et erreurs ;
- [`imageeditor.cpp`](../../../../src/editor/imageeditor.cpp) : ouverture et rechargement après changement de balance ;
- [`imageprovider.cpp`](../../../../src/providers/imageprovider.cpp) : décodage de secours cohérent ;
- [`README.md`](../../../../README.md) : architecture mise à jour.

La liste des filtres du navigateur est maintenant dérivée des extensions enregistrées. Un format ajouté au registre peut donc apparaître dans la liste et être décodé sans ajouter un nouveau `if` au pipeline d’ouverture.

### Extension future par appareil ou format

Pour ajouter un format RAW :

1. implémenter un callback qui retourne une `ColorPipeline::WorkingImage` et renseigne l’erreur si le décodage échoue ;
2. l’enregistrer pour ses extensions au démarrage du programme ;
3. ajouter les éventuelles options spécifiques dans le type de requête, en gardant leur sémantique indépendante des autres formats ;
4. ajouter un test de dispatch et, lorsque des fichiers redistribuables sont disponibles, un test d’intégration avec un RAW représentatif.

Pour gérer un profil particulier à un appareil, le prochain palier devrait séparer le backend de lecture RAW du transformeur de profil caméra. Le backend devra fournir des métadonnées de modèle et des données caméra suffisantes ; un fournisseur de profil pourra ensuite choisir une matrice ou un DCP d’après le modèle exact. Cette interface appareil/profil n’a pas encore été implémentée.

## État de l’intégration DNG / DCP

L’inspection du SDK et de LibRaw montre que parser un DCP n’est pas équivalent à appliquer le profil. La sortie actuelle de LibRaw est déjà démosaïquée, balance des blancs appliquée et convertie par sa matrice caméra vers RGB. Appliquer ensuite un DCP comme s’il recevait des valeurs mosaïquées de caméra risquerait de doubler la balance ou les matrices et de reproduire une dérive colorimétrique.

Le SDK contient des API de lecture de profils DNG/DCP, mais il faut encore concevoir un adaptateur caméra-RGB vers l’espace de travail qui respecte au minimum :

- l’ordre de balance des blancs et de matrice caméra ;
- le choix/interpolation des matrices en fonction de l’illuminant ;
- les éventuelles HueSatMap et LookTable du profil ;
- la courbe tonale DCP et son interaction avec les réglages tonaux IPR ;
- les valeurs d’entrée et l’espace colorimétrique effectivement renvoyés par LibRaw.

Aucun téléchargement du SDK n’a été conservé dans le dépôt, aucun code SDK n’est copié, et aucune dépendance DNG n’a été ajoutée au système de build. Il reste donc faux d’affirmer que les profils DCP sont pris en charge par IPR.

## Validation

Builds WSL effectués après les changements :

- LibRaw activé : compilation réussie ;
- LibRaw désactivé : compilation réussie.

Tests CTest dans les deux configurations :

- `colorPipelineTests` : réussi ;
- `rawDecoderTests` : réussi ;
- total : 2/2 par configuration.

Le nouveau test enregistre une extension factice, vérifie la visibilité de l’extension, la transmission des options, la production d’une image, l’erreur d’extension non prise en charge et le rejet des doublons.

## Étapes restantes

1. **Choisir et publier explicitement la licence d’IPR** en ajoutant un fichier `LICENSE` racine et en confirmant la titularité des contributions.
2. Faire valider les conditions Qt pour chaque module distribué et fournir les notices/source/possibilités de remplacement prévues par l’option LGPL choisie.
3. Confirmer pour chaque distribution quelle variante de LibRaw et quels fichiers tiers sont effectivement inclus.
4. Concevoir et tester l’adaptateur DCP séparément, d’abord sur des profils neutres ou libres et des données caméra connues ; garder l’option SDK désactivée tant que le rendu n’est pas fiable.
5. Décider si les distributions avec le SDK Adobe sont acceptables au regard de l’accord, particulièrement pour une distribution commerciale.
6. Ajouter une interface de choix de profil après que son application au pipeline est testée ; ne pas exposer un sélecteur DCP qui ne ferait que parser le fichier sans changer le rendu.

## Sources

- Qt, licences et obligations : <https://www.qt.io/development/open-source-lgpl-obligations> et <https://doc.qt.io/qt-6/licensing.html>
- OpenCV, licence Apache 2.0 : <https://github.com/opencv/opencv/blob/4.x/LICENSE>
- LibRaw, licences et notices du projet : <https://github.com/LibRaw/LibRaw/blob/master/COPYRIGHT>
- Adobe DNG SDK et son accord : <https://www.adobe.com/go/dng_sdk>
- Textes GNU : <https://www.gnu.org/licenses/lgpl-3.0.html>, <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>, <https://www.gnu.org/licenses/gpl-3.0.html>
- Apache 2.0 : <https://www.apache.org/licenses/LICENSE-2.0>
