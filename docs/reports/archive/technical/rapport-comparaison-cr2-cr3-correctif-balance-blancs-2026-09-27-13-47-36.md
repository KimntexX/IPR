# Compte rendu — Comparaison CR2/CR3 et correctif de balance des blancs

**Date :** 27 septembre 2026, 13:47  
**Sujet :** analyse des trois RAW fournis et correction du comportement de la balance automatique.

## Résumé

La comparaison a révélé que le réglage « balance des blancs automatique » ne sélectionnait pas la même méthode LibRaw selon l’extension : le CR2 utilisait la balance automatique, tandis que le CR3 utilisait les coefficients de balance enregistrés par l’appareil. Sur le CR3 analysé, ces coefficients produisaient une importante dominante magenta.

Le décodeur de production applique maintenant la même sémantique aux deux formats : en mode automatique, LibRaw calcule la balance ; en mode non automatique, il utilise la balance caméra. Les constructions et tests disponibles réussissent avec LibRaw activé comme désactivé.

## Fichiers analysés

- `tests/image/IMG_9223.CR2` — Canon EOS 60D, ISO 4000, f/8, 70 mm.
- `tests/image/IMG_9224.CR2` — Canon EOS 60D, mêmes paramètres rapportés, pris 22 secondes après le premier CR2.
- `tests/image/6V1A1236.CR3` — Canon EOS R6 Mark III, ISO 4000, f/8, 70 mm, pris environ 20 minutes avant les deux CR2.

Les fichiers CR2 proviennent du même boîtier ; le CR3 vient d’un boîtier différent. Les différences globales de luminosité et de couleur entre boîtiers ne doivent donc pas être attribuées uniquement à l’extension RAW.

## Actions menées

1. Relevé des métadonnées disponibles : boîtier, ISO, ouverture, focale, horodatage, dimensions de sortie LibRaw et coefficients de balance caméra.
2. Décodage comparatif du CR3 avec balance caméra et balance automatique, en conservant les mêmes autres paramètres LibRaw et la même conversion sRGB.
3. Mesure des moyennes RGB et inspection des rendus normalisés.
4. Modification du sélecteur de balance des blancs dans [RawDecoder.cpp](../../../../src/raw/RawDecoder.cpp) pour que CR2 et CR3 utilisent les mêmes drapeaux LibRaw.
5. Vérification des appels de production : [imageeditor.cpp](../../../../src/editor/imageeditor.cpp) passe le réglage automatique lors de l’ouverture et le réutilise lorsqu’il change ; les deux formats passent par le décodeur correspondant. L’état de ce réglage est conservé dans [imageeditor.h](../../../../src/editor/imageeditor.h).
6. Compilation et tests ciblés des deux configurations WSL disponibles.

## Résultats de la comparaison

Les moyennes ci-dessous sont celles du rendu LibRaw RGB 16 bits linéaire ; les valeurs sRGB sont les moyennes des aperçus normalisés.

| Fichier et mode | Moyenne RGB 16 bits | Moyenne RGB sRGB |
|---|---:|---:|
| `IMG_9223.CR2`, automatique | 1518 / 1481 / 1494 | 38 / 39 / 38 |
| `IMG_9224.CR2`, automatique | 1572 / 1535 / 1548 | 38 / 39 / 38 |
| `6V1A1236.CR3`, balance caméra (ancien comportement) | 23080 / 5174 / 16708 | 160 / 79 / 138 |
| `6V1A1236.CR3`, automatique | 8957 / 8956 / 8958 | 103 / 103 / 103 |

La balance caméra du CR3 analysé présente un déficit marqué du canal vert, cohérent avec la dominante magenta observée. Avec la balance automatique, les trois canaux sont pratiquement équilibrés et cette dominante disparaît. Cette comparaison confirme l’origine du problème pour cet échantillon ; elle ne prouve pas que la balance caméra sera incorrecte pour tous les CR3.

Les niveaux entre CR2 et CR3 ne sont pas directement comparables comme mesure d’exposition : les boîtiers sont différents, les captures ne sont pas simultanées et LibRaw peut appliquer des traitements de rendu propres au modèle.

## Correctif appliqué

Dans `loadRaw()`, les options LibRaw sont désormais sélectionnées indépendamment de l’extension :

```cpp
raw.imgdata.params.use_camera_wb = automaticWhiteBalance ? 0 : 1;
raw.imgdata.params.use_auto_wb = automaticWhiteBalance ? 1 : 0;
```

Ainsi :

- `automaticWhiteBalance == true` sélectionne `use_auto_wb` ;
- `automaticWhiteBalance == false` sélectionne `use_camera_wb`.

Les fonctions d’entrée `loadCr2()` et `loadCr3()` restent séparées afin de préserver le point d’extension pour d’éventuelles différences de décodage RAW spécifiques aux formats. Les autres paramètres LibRaw et le pipeline couleur n’ont pas été changés.

## Validation

- Build WSL avec LibRaw : réussi.
- Build WSL sans LibRaw : réussi.
- CTest avec LibRaw : `colorPipelineTests`, 1/1 réussi.
- CTest sans LibRaw : `colorPipelineTests`, 1/1 réussi.
- Recherche dans le code : les deux drapeaux de balance LibRaw de production sont définis ensemble dans le décodeur commun.

Les tests automatisés existants couvrent le pipeline couleur, pas le décodage de ces RAW réels. Les rendus et mesures comparatifs étaient des artefacts temporaires ; ils ont été supprimés après l’analyse.

## Étapes restantes

1. Ouvrir les trois RAW dans l’application compilée et confirmer visuellement que le CR3 s’ouvre désormais avec la balance automatique attendue.
2. Comparer des zones neutres identiques (charte grise ou objets connus) afin de mesurer la neutralité, idéalement avec un rendu de référence RawTherapee.
3. Ajouter, si les fichiers de test peuvent être redistribués dans le dépôt, un test d’intégration LibRaw couvrant la sélection des deux modes de balance pour CR2 et CR3.
4. Évaluer séparément le voile et l’exposition sur des références équivalentes, sans confondre ces écarts avec la correction de balance des blancs.
