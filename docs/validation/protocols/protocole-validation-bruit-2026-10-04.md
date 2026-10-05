# Réduction de bruit — protocole photographique

**Date :** 4 octobre 2026. **Périmètre :** bruit de luminance et de couleur
de la première implémentation CPU IPR ; comparaison classique Lightroom,
IA éventuelle séparée. [Modèle du moteur](../../architecture/denoising-sharpening.md).

## Entrées et états

Pour chaque RAW, conserver son original, deux neutres propres IPR/Lightroom
et quatre exports par logiciel pour chacun des deux réglages.

| Fichier dans chaque série | Sans suffixe | −2 | −3 | −4 |
| --- | ---: | ---: | ---: | ---: |
| Valeur du réglage bruit | 25 | 50 | 75 | 100 |

Pas de valeurs négatives. Dossiers : `tests/image/base/bruit/luminance`,
`bruit/couleur` et `tests/image/base/neutre`. Lightroom `.tif`, IPR `-IPR.tiff`
ou `-IPR-N.tiff`. Le fichier sans suffixe de la série n'est pas un neutre.

- Neutre : luminance 0, couleur 0, gain de netteté 0 ; IA inactive.
- Série luminance : couleur 0, netteté 0, uniquement luminance modifiée.
- Série couleur : luminance 0, netteté 0, uniquement couleur modifiée.
- TIFF sRGB 16 bits SDR, pleine définition, pas de redimensionnement,
  netteté de sortie désactivée.
- EV0 ; texture/clarté/voile/grain à 0 ; mêmes autres réglages, profil,
  balance des blancs et géométrie entre les variantes d'une application.
- Paramètres secondaires Lightroom de bruit fixes, capturés et audités.

Un ancien neutre du mélangeur peut avoir du bruit couleur à 25 et une netteté
à 40 : il ne devient pas un neutre de bruit par son seul nom. Lire le XMP
incorporé aux TIFF ; conserver les historiques avant tout remplacement.
L'absence de balise IA n'est pas une preuve suffisante d'absence de traitement
amont : demander une capture du panneau et la version du logiciel.

## Scènes

Au moins une image peu bruitée, une scène bruitée avec aplats/ombres et une
avec détails fins naturels (peau, cheveux/pelage, feuillage, tissu).
ISO élevé ne suffit pas à qualifier le bruit ; examiner le neutre à 100 %.
Un ciel nocturne presque sans détail est utile pour les aplats, pas pour
valider à lui seul la conservation des textures.

## Audit et exclusions

Inventorier tous les états, les ICC, profondeurs et dimensions. Vérifier le
réglage exact Lightroom et le retour à zéro du second curseur. Marquer les
fichiers manquants ou incompatibles ; ne pas fabriquer de neutre.
Les réglages IPR ne sont pas tracés dans un XMP équivalent : les états restent
déclarés par l'opérateur et leurs noms.

Si le neutre Lightroom manque ou contient déjà du débruitage, l'analyseur
utilise le premier état disponible comme **ancre diagnostique non nulle**.
Les mesures ne deviennent pas des ratios à zéro. Les états XMP erronés
restent marqués `state_valid=false`, sans renumérotation ou correction des TIFF.
Une série à netteté 40 peut décrire une tendance mais ne valide pas le
protocole « bruit seul ». Les comparaisons IA doivent faire une campagne séparée.

## Méthodes

Contrairement aux analyses tonales, mesurer des **pixels adjacents**, pas un
pixel sur cinq : la décimation changerait le bruit et sa corrélation.

1. Lire les TIFF natifs non compressés en mémoire mappée, sans conversion 8 bits.
2. Choisir sur chaque référence 80 régions candidates de 256×256 pixels,
   six plages peu structurées et quatre régions structurées non dupliquées.
   Fixer ces coordonnées pour tous les états de la même série/application.
3. Écarter les candidats de luminance moyenne hors ]0,001;0,8[ ou trop écrêtés.
   Les « aplats » automatiques sont des candidats à vérifier visuellement,
   pas une annotation sémantique garantie.
4. Décoder sRGB en RGB linéaire et mesurer Y Rec.709, YCoCg, Co et Cg.
5. Estimer un proxy de bruit haute fréquence par le détail diagonal Haar :

   `HH = (p00 − p01 − p10 + p11)/2`,

   `sigma = median(|HH − median(HH)|)/0,67448975`.

   Le filtre a une norme quadratique unité ; le facteur MAD correspond à
   une loi gaussienne. Après dématriçage/débruitage, le bruit est corrélé et
   dépend du signal : il s'agit d'un **proxy de bruit de sortie**, pas d'une
   mesure physique exacte du bruit capteur.
6. Pour la chrominance : `Co=R−B`, `Cg=G−(R+B)/2`,
   `sigma_C = sqrt((sigma_Co²+sigma_Cg²)/2)`.
7. Mesurer les ratios au neutre **propre**, la dérive de luminance moyenne,
   la dérive moyenne Co/Cg et les changements par région.
8. Pour les contours, comparer les gradients de Y après deux flous binomiaux
   `[1,4,6,4,1]/16`, sur le masque fort fixé dans la référence (quantile 85 %).
   Une baisse de gradient peut enlever du bruit autant que du détail ; vérifier
   les crops à 100 % avant de conclure à une perte réelle de texture.

Les régions sont sélectionnées indépendamment par application : les médianes
de ratios décrivent des tendances internes, pas des régions forcément équivalentes
entre moteurs. Pas de MAE pixel-à-pixel directe IPR/Lightroom : le rendu neutre,
le dématriçage, les corrections optiques et les dimensions peuvent différer.
La MAE au neutre propre mesure le changement, **pas une erreur de reconstruction**.
Sans référence propre sans bruit, ne pas publier de PSNR ou prétendre mesurer
la fidélité absolue des détails à partir des seuls TIFF débruités.

## Reproduction

Python NumPy/Pillow, depuis la racine ; sorties versionnées non écrasables :

```bash
python3 -m unittest discover -s tests -p test_analyse_noise.py
python3 tests/analyse_noise.py --output NOUVEL-INVENTAIRE-BRUIT.json
python3 tests/analyse_noise.py --analyse --previews build-wsl-libraw/noise-diagnostics-NOUVELLE-PASSE --output NOUVELLE-MESURE-BRUIT.json
bash scripts/build-windows.sh --dry-run --paths tests/analyse_noise.py
```

Les aperçus diagnostiques PNG sont en 8 bits pour consultation ; ils ne servent
pas aux métriques, qui lisent les régions natives. L'analyse refuse d'écraser
une mesure existante et vérifie les dates/tailles des références et variantes
avant/après la lecture et à la fin de la campagne ; un remplacement invalide
la passe. Aucun RAW, TIFF, sidecar ou INI n'est modifié.

Les réexports avec suffixes de collision supplémentaires ne sont pas renommés
automatiquement. Les inventorier dans `unmapped_files`, vérifier le XMP, puis
utiliser un mapping explicite `--variant-file IMAGE_ID COMPOSANTE LR NUMERO CHEMIN`.
`NUMERO` vaut 1 à 4 (25/50/75/100) ; la valeur observée reste celle du XMP,
même en cas d'étiquette erronée. Exemple des mappings 9890 dans le rapport.
Pour un neutre explicitement identifié par l'opérateur mais nommé autrement,
utiliser `--neutral-file IMAGE_ID IPR CHEMIN` (ou `LR`, avec audit XMP).
L'association est enregistrée et le fichier source n'est pas renommé.
Le nom seul ne constitue pas une preuve du contenu des réglages IPR.

## Décision

Publier la couverture, les omissions et les valeurs de bruit/détail dans
[l'analyse](../analyses/analyse-bruit-2026-10-04.md).
Distinguer activité des commandes, efficacité de réduction de bruit,
progressivité, préservation des détails et comparaison Lightroom.
La netteté et le couplage bruit+netteté sont des recettes ultérieures.

## Références

- D. L. Donoho, I. M. Johnstone, *Ideal spatial adaptation by wavelet shrinkage*,
  Biometrika 81(3), 1994, 425–455, [DOI](https://doi.org/10.1093/biomet/81.3.425) ;
  [rapport original à Stanford](https://statistics.stanford.edu/technical-reports/ideal-spatial-adaptation-wavelet-shrinkage).
  Fondement du seuillage multi-échelle ; nos statistiques Haar ne réimplémentent
  pas à elles seules cet algorithme complet.
- [Adobe — débruitage manuel et IA](https://blog.adobe.com/en/publish/2023/04/20/lowdown-using-high-iso-settings-in-photography)
  : distinction luminance/couleur et examen à 100 % ;
  [Denoise actuel](https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/enhance-details.html).
