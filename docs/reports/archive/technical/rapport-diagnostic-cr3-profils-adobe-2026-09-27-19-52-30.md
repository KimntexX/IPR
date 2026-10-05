# Rapport de diagnostic — CR3 et profils Adobe

**Date :** 27 septembre 2026, 19:52  
**Image testée :** `tests/image/6V1A1236.CR3`  
**Appareil/objectif rapportés par LibRaw :** Canon EOS R6 Mark III / `70-200mm F2.8 DG OS HSM | Sports 018`

## Conclusion

Le CR3 n'utilisait initialement aucun DCP ni LCP, malgré la présence des fichiers Adobe correspondants. Après correction, le test réel sélectionne :

- DCP : `Canon EOS R6 Mark III Adobe Standard.dcp`
- LCP : `Canon (SIGMA 70-200mm F2.8 DG OS HSM S018) - RAW.lcp`

Le même test a aussi révélé que l'application directe de la formule LCP de vignettage assombrissait fortement les bords. Cette interprétation a été corrigée : l'atténuation optique est maintenant inversée pour calculer le gain de compensation et le rayon tient compte de la focale. Les aperçus profilés isolés ne montrent plus l'assombrissement circulaire artificiel observé avant ce changement.

Les réglages de l'éditeur ne restent pas appliqués aux images suivantes : `ImageEditor::loadImage()` réinitialise `m_settings` avant le rendu ; les valeurs neutres de `renderWorkingImage()` ne modifient pas l'image. Le signal `adjustmentsReset` remet également les contrôles QML à leur état neutre. Le probe RAW compare le décodage profilé directement à une référence sans profil et ne passe pas par les réglages de l'éditeur.

## Causes trouvées

### DCP Adobe non reconnu

1. Plusieurs tables Adobe utilisent des dimensions `LookTableDims` de `90 × 16 × 16`; le lecteur rejetait toute dimension supérieure à 64.
2. Le nombre d'éléments de table (`69120`) dépassait la limite de lecture numérique de 65536.
3. Le DCP Adobe Standard du boîtier ne contient pas de `CameraModelRestriction`. Le repli ne vérifiait qu'une égalité exacte entre le nom de fichier et le modèle caméra, alors que le nom est préfixé par le fabricant et complété par le nom du profil.

### LCP Sigma non reconnu / correction excessive

1. Les LCP Adobe exposent plusieurs noms pour un même objectif (`Lens` et `LensPrettyName`). Le profil Sigma utilise notamment la désignation normalisée `SIGMA … S018`, tandis que LibRaw retourne `… | Sports 018`.
2. Les coefficients Adobe de vignettage valides peuvent dépasser 100 ; la validation générique les rejetait.
3. L'ancien calcul appliquait les paramètres directement comme `1 + polynôme` sur un rayon image normalisé uniquement par la taille du cadre. Pour cet objectif, cela créait une atténuation forte au lieu de compenser le vignettage.

## Corrections réalisées

- Étendu la lecture bornée des tables DCP et l'acceptation des tables Hue/Look jusqu'à 360 divisions de teinte, avec saturation et valeur plafonnées à 64.
- Ajouté un repli de correspondance DCP sur un nom de fichier préfixé par fabricant et modèle quand la restriction interne manque.
- Conservé séparément `Lens` et `LensPrettyName`, avec priorité au nom technique `Lens` rapporté par LibRaw et repli sur `LensPrettyName` lorsqu'il est seul.
- Assoupli les bornes LCP spécifiquement pour les coefficients de vignettage, tout en gardant des limites distinctes pour géométrie et aberration chromatique.
- Inversé la fonction d'atténuation calculée par le profil pour obtenir un gain de correction, avec normalisation du rayon par la focale et la taille de l'image.
- Ajouté `profileDirectories` dans les options de `RawDecoder` pour permettre un test reproductible avec les profils installés sans les copier dans le dépôt.
- Ajouté `rawProfileIntegrationProbe`, qui compare l'image profilée à une référence décodée sans profils, imprime les noms des profils et peut enregistrer un aperçu.
- Étendu les tests synthétiques aux DCP Adobe sans restriction caméra et aux alias de noms d'objectifs LCP.

## Mesures du test réel

Le probe a décodé l'image à **6959 × 4639**. Après corrections, les profils affichés sont ceux cités plus haut. Les moyennes RGB linéaires de l'image décodée étaient :

| Chemin | Moyennes RGB |
|---|---|
| Sans profil | `0.136672, 0.136661, 0.136693` |
| DCP seul | `0.137202, 0.137102, 0.137122` |
| LCP seul corrigé | `0.168591, 0.167363, 0.168343` |
| DCP + LCP | `0.169092, 0.167634, 0.168671` |

L'écart absolu RGB moyen entre le décodage DCP+LCP et la référence sans profil est `0.0321893`, donc l'application des profils a un effet mesurable. Avant la correction de vignettage, le chemin LCP seul avait une moyenne proche de `0.0633`, confirmant une atténuation non voulue. Les moyennes RGB ne sont pas une mesure complète de fidélité couleur ; elles servent ici à localiser le changement d'exposition et à vérifier qu'il n'y a plus de forte chute globale.

## Traitement restant et limites

- La correction LCP réelle est maintenant vérifiée sur ce CR3, mais le moteur n'est toujours pas une implémentation certifiée de toute la spécification Adobe.
- La normalisation actuelle de vignettage suppose un format 35 mm. Les facteurs de recadrage pour les boîtiers APS-C/Micro 4/3 restent à intégrer et à valider.
- La correspondance et l'application doivent être testées avec d'autres modèles Canon et objectifs Sigma, ainsi qu'avec plusieurs focales/ouvertures/distances.
- L'utilisation du DCP Adobe Standard est établie ; ce test ne juge pas la préférence de rendu entre Adobe Standard et les profils Camera Standard/Portrait/etc.

## Validation

Build et tests exécutés sous WSL Ubuntu après les corrections :

- `build-wsl-libraw` : compilation réussie, 3/3 tests CTest réussis ;
- `rawProfileIntegrationProbe` : profils DCP et LCP détectés sur le CR3 réel, décodage et comparaison réussis ;
- `build-wsl-nolibraw` : compilation réussie, 3/3 tests CTest réussis.

L'exécution du probe dans l'application Windows requiert la recompilation du binaire avec les modifications. Après ouverture de l'image, la barre d'information doit afficher les noms `DCP:` et `LCP:` ; leur absence indique que le binaire ou sa configuration ne contient pas encore les changements récents.
