# Diagnostic — Export DNG non affiché par Lightroom

**Date :** 3 octobre 2026. **Statut :** diagnostic ; exporteur non modifié dans cette intervention.

## Fichier et constat

Le fichier examiné est `tests/image/6V1A9933-IPR.dng`, à la racine du corpus
`tests/image/` : 193 739 509 octets, image 4639 × 6959, RGB 16 bits linéaire,
sans compression. L'utilisateur constate l'absence d'image dans Lightroom, sans
message d'erreur. La version de Lightroom et l'étape exacte (import ou affichage)
ne sont pas connues.

Les offsets et longueurs des bandes de pixels restent dans le fichier. LibRaw
réussit à décoder les pixels et restitue les dimensions attendues. Cela exclut une
troncature évidente mais ne démontre pas la conformité DNG ni l'acceptation Adobe.
Le probe de profils retourne 1 car il attend aussi des profils DCP/LCP : ce code
de sortie ne constitue pas une erreur de décodage dans ce cas.

## Cause probable du refus

Le tag TIFF **254 `NewSubfileType` est absent**. Le code source du SDK Adobe,
`dng_ifd::IsValidDNG()`, renvoie `false` lorsqu'il manque. C'est une cause probable
et concrète de rejet par un lecteur Adobe, plus solide que l'hypothèse d'une simple
absence de vignette. L'échec n'a cependant pas été reproduit ici dans Lightroom.
[Source primaire : code Adobe du SDK DNG, miroir AOSP, contrôle « Check NewSubFileType »](https://android.googlesource.com/platform/external/dng_sdk/+/80a267ed1ac714acff455e85ae28c1732777d5b6/source/dng_ifd.cpp).

## Autres défauts constatés

| Élément | Valeur actuelle | Conséquence / recommandation |
| --- | --- | --- |
| `NewSubfileType` | absent | Écrire explicitement 0 sur l'IFD principal linéaire. |
| `ColorMatrix1` | matrice RGB linéaire → XYZ D50 | Direction inversée par rapport à XYZ → couleurs caméra attendu ; remplacer par l'inverse cohérent. |
| `CalibrationIlluminant1` | 21 (D65) | Incohérent avec la matrice D50 actuelle ; choisir un couple matrice/illuminant cohérent, par exemple inverse D50 avec 23 (D50). |
| `ICCProfile` | profil sRGB gamma-encodé | Inadapté aux pixels linéaires ; ne pas étiqueter le plan RAW avec un profil d'affichage non linéaire. |
| Aperçu RGB | absent | Un aperçu sRGB séparé améliore l'interopérabilité, mais son absence seule n'invalide pas un DNG. |

La matrice et l'illuminant sont des défauts colorimétriques établis ; ils ne sont
pas une preuve supplémentaire du mécanisme exact de rejet. L'absence des tags de
recadrage n'est pas retenue : leurs valeurs par défaut couvrent toute l'image.
Le format `LinearRaw` est autorisé ; il n'est pas nécessaire de transformer
l'export en mosaïque Bayer pour qu'il soit lisible.
[Spécification Adobe DNG 1.7.1.0, sections SubIFD Trees, DefaultCropSize, ColorMatrix1 et CalibrationIlluminant1](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf).

## Localisation et périmètre du futur correctif

L'exporteur `writeLinearDng()` se trouve dans
[`src/editor/imageeditor.cpp`](../../../src/editor/imageeditor.cpp). Il écrit
une image **déjà développée et réglée** en RGB linéaire sRGB normalisé 16 bits,
pas le RAW original du capteur. L'exposition et le rendu sont incorporés et les
valeurs hors gamut sont ramenées au domaine d'export. Ce comportement doit rester
explicite pour ne pas confondre export de rendu et archivage du négatif original.

Le correctif minimal devra ajouter `NewSubfileType=0`, corriger matrice/illuminant,
retirer l'ICC inadéquat du plan linéaire et vérifier l'écriture finale de l'IFD.
Un aperçu séparé peut être ajouté ensuite ; ce n'est pas le premier correctif à
tester. Conserver l'ancien DNG comme témoin et exporter un nouveau fichier sans
l'écraser.

## Reproduction et validation

```sh
python3 tests/inspect_dng.py tests/image/6V1A9933-IPR.dng
build-wsl-libraw/rawProfileIntegrationProbe tests/image/6V1A9933-IPR.dng /tmp/ipr-empty-camera /tmp/ipr-empty-lens --camera-wb
```

Le script d'inspection est en lecture seule, limité au TIFF classique et aux
IFD chaînés ; ce n'est ni un validateur DNG complet ni un décodeur de pixels.

Après correction :

1. Ajouter des assertions de métadonnées au test `verifyDngOutput()` ; il ne
   vérifie aujourd'hui que création du fichier et décodage/dimensions LibRaw.
2. Exécuter `dng_validate` du SDK Adobe, puis vérifier une mire de gris/couleurs
   pour contrôler la direction de matrice et le point blanc.
3. Importer le nouveau DNG dans la même version de Lightroom et contrôler
   vignette, ouverture dans Développement, couleurs et dimensions.
4. Valider aussi l'import IPR/LibRaw. Ne pas annoncer la compatibilité Lightroom
   sur la seule base d'un export créé avec succès.

Le SDK et la spécification sont disponibles sur la
[page officielle Adobe DNG](https://www.adobe.com/support/downloads/dng/dng_sdk.html).
