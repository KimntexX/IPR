# Rapport — DNG, profils sous WSL et comparaison au CR3

**Date :** 27 septembre 2026, 20:24  
**Fichiers comparés :** `tests/image/6V1A1236.CR3` et `tests/image/6V1A1236.dng`

## Réponse sur WSL

Lancer IPR sous WSL ne change pas en soi les pixels décodés, mais cela peut changer les fichiers annexes que l'application trouve :

- le binaire Windows compilait la branche de recherche de profils Adobe sous `%ProgramData%` ;
- le binaire Linux/WSL ne compilait pas cette branche Windows. Même si les RAW se trouvaient sous `/mnt/c`, les profils Adobe n'étaient donc pas automatiquement recherchés.

La recherche par défaut détecte maintenant `/mnt/c/ProgramData/Adobe/CameraRaw` sous WSL si le chemin existe. La variable `IPR_ADOBE_CAMERARAW_DIR` permet de préciser une autre racine. Il faut recompiler et relancer le binaire WSL pour bénéficier de cette modification. Les dossiers C: doivent être adressés depuis WSL sous `/mnt/c/...`.

La comparaison réelle effectuée après ce changement confirme que le binaire WSL trouve et applique bien le DCP et le LCP. Le problème visuel ne se résume donc pas à un échec de lecture des profils. Les différences de rendu présentées ci-dessous proviennent des sorties de développement comparées, pas d'un réglage résiduel de l'interface.

## Profilage CR3

Le probe a trouvé automatiquement dans WSL :

- `Canon EOS R6 Mark III Adobe Standard.dcp` ;
- `Canon (SIGMA 70-200mm F2.8 DG OS HSM S018) - RAW.lcp`.

| Mesure linéaire | CR3 profilé | CR3 sans profils |
|---|---:|---:|
| Dimensions | 6959 × 4639 | 6959 × 4639 |
| Moyenne rouge | 0.169092 | 0.136672 |
| Moyenne verte | 0.167634 | 0.136661 |
| Moyenne bleue | 0.168671 | 0.136693 |

Écart absolu RGB moyen après profilage : `0.0321893`. Le test ne passe pas par `ImageEditor` et n'applique donc aucun curseur d'exposition, de contraste, de tons, de température ou de nuance.

## DNG intégré et comparaison

Le DNG `6V1A1236.dng` est maintenant enregistré comme format LibRaw `dng`. Il apparaît dans la liste des images et suit les mêmes chemins de décodage, profilage, balance des blancs, aperçu et export que CR2/CR3. Le même DCP et le même LCP sont sélectionnés pour le CR3 et pour le DNG, d'après leurs métadonnées caméra/objectif.

| Mesure linéaire | DNG profilé | DNG sans profils |
|---|---:|---:|
| Dimensions | 6982 × 4662 | 6982 × 4662 |
| Moyenne rouge | 0.0220075 | 0.0182499 |
| Moyenne verte | 0.0218531 | 0.0182022 |
| Moyenne bleue | 0.0216991 | 0.0182945 |

Écart absolu RGB moyen après profilage : `0.0092893`.

À géométrie commune, les aperçus sRGB profilés présentent un écart absolu moyen d'environ **76 niveaux sur 255**. Le CR3 est nettement plus clair et plus voilé ; le DNG est plus sombre et son contraste relatif est supérieur. Les écarts de taille du cadre sont également visibles : le DNG fait 23 pixels de plus en largeur et 23 pixels de plus en hauteur.

Cette différence importante ne peut pas être expliquée par un simple effet WSL. Le DNG est une conversion distincte avec ses propres niveaux, matrices, balance neutre et exposition de base ; les valeurs du DNG indiquent notamment `BaselineExposure = +0.28`. Le pipeline actuel traite les deux extensions par le même chemin LibRaw et sélectionne le même profil externe, mais ne lit pas encore explicitement les champs d'exposition de base et les métadonnées DNG embarquées pour adapter ce traitement. Il reste donc à rapprocher les deux rendus avec des données de référence avant de conclure que le DNG ou le CR3 est colorimétriquement juste.

## Modifications effectuées

- Enregistrement de `.dng` sur le décodeur LibRaw, sans créer un chemin de conversion séparé ou perdre la précision RAW.
- Inclusion automatique de cette extension dans les filtres du navigateur et l'ouverture du provider, par le registre existant.
- Extension du redécodage après changement de balance des blancs au format DNG.
- Mise à jour de l'étiquette QML, README, instructions de compilation Windows et messages CMake.
- Détection sous WSL du dossier des profils Adobe monté depuis Windows et ajout du paramètre `IPR_ADOBE_CAMERARAW_DIR`.
- Test synthétique de la surcharge du chemin de profils et probe qui accepte une recherche par chemins par défaut.

## Validation

- `build-wsl-libraw` : compilation réussie, 3/3 tests CTest réussis.
- `build-wsl-nolibraw` : compilation réussie, 3/3 tests CTest réussis. Comme pour les CR2/CR3, les DNG ne sont ouverts qu'avec LibRaw activé.
- `rawProfileIntegrationProbe` a décodé le CR3 et le DNG, trouvé DCP/LCP via la recherche WSL par défaut et mesuré un changement pixel mesurable par profilage.
- Aucune comparaison directe avec un rendu Lightroom/Adobe synchronisé (profil, balance des blancs, exposition, réglages de base) n'a été disponible dans ce test.

## Suite recommandée

1. Recompiler l'application utilisée sous WSL ou Windows ; un ancien exécutable ne prendra pas en charge `.dng` ni la recherche de profils WSL.
2. Confirmer que la barre d'information de l'application indique les noms DCP et LCP ; comparer séparément le rendu d'ouverture du CR3 et du DNG.
3. Faire une comparaison de référence Adobe avec balance des blancs, profil, exposition et correction d'objectif identiques.
4. Examiner et intégrer de façon explicite les champs DNG pertinents (`AsShotNeutral`, `BaselineExposure`, niveaux noir/blanc et matrices embarquées) avant d'essayer de rapprocher artificiellement les moyennes CR3/DNG.

