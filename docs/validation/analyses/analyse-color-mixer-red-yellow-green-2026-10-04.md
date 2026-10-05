# Analyse du mélangeur — Rouge, jaune et vert

**Date :** 4 octobre 2026. **Scène :** 6V1A0327.
**Ordre exécuté :** `red`, `yellow`, `green`.

## Conclusion

**Défaut confirmé à corriger en priorité : la luminance rouge agit sur les gris
neutres**, qu’elle transforme en noir à −100 et en blanc à +100 dans la sonde
synthétique. Ce n’est pas seulement une différence de rendu avec Lightroom.

Sur les pixels colorés de cette photo, le rouge et le jaune présentent une
réponse cohérente : teinte, saturation et luminance évoluent dans le sens attendu.
**Leur rendu n’est pas encore équivalent à Lightroom**, surtout en saturation
et en luminance. Le vert a été mesuré, mais le corpus est trop pauvre dans le
cœur de cette bande pour le valider ou calibrer sa réponse.

Cette passe n’autorise donc pas une validation globale du mélangeur. Elle
identifie les points à affiner après la réception des autres bandes, sans
modifier le moteur ni changer arbitrairement ses coefficients sur une scène.

**Campagne suivante :** le [corpus étendu](analyse-color-mixer-corpus-etendu-2026-10-04.md)
ajoute 6V1A0037 et 6V1A0043 et lève le manque de couverture du vert. Il analyse
aussi orange/cyan/bleu et les références violet/magenta, sans corriger le moteur.

## Références et traçabilité

- [Protocole et méthode](../protocols/protocole-validation-color-mixer-2026-10-04.md).
- [Mesure finale v2](../raw/mesure-color-mixer-red-yellow-green-2026-10-04-v2.json) :
  états par fichier, résultats et comparaison à chaque neutre.
- [Inventaire initial](../raw/inventaire-color-mixer-2026-10-04.json) : état avant
  correction de l’export Lightroom jaune +100.
- [Passe exploratoire](../raw/mesure-color-mixer-red-yellow-green-2026-10-04.json) :
  conservée pour traçabilité ; la **v2 fait référence** pour les résultats ci-dessous.
- Comparateur : `tests/analyse_color_mixer.py` ; contrôles déterministes dans
  `tests/test_analyse_color_mixer.py`.
- [Sonde sur gris neutres](../raw/sonde-color-mixer-gris-neutres-2026-10-04.json),
  exécutée par `colorMixerNeutralProbe` via le moteur réel, pas une réimplémentation Python.

## Audit des entrées

Les TIFF Lightroom incorporent la preuve des réglages : (1,2) teinte,
(3,4) saturation, (5,6) luminance, impair −100 et pair +100. Le fichier sans
suffixe constitue le n°1. Les autres réglages XMP sont identiques au neutre LR.

Le fichier Lightroom jaune `6V1A0327-6.tif` était initialement marqué −100.
L’utilisateur l’a régénéré ; le XMP de la passe finale confirme **+100**.
Il a également fourni le TIFF IPR neutre dans `base`. Les **18 états Lightroom**
de la passe finale sont cohérents avec les noms attendus.

Les TIFF LR sont RGB 16 bits sRGB, 4640 × 6960 ; les TIFF IPR sont RGBA
16 bits sRGB, 4639 × 6959. Cet écart connu vient de la zone active RAW et ne
désigne pas une perte introduite par l’export TIFF.
Le neutre LR est sans réglage HSL, mais conserve notamment netteté 40 et bruit
couleur 25. Les TIFF IPR n’incorporent pas de réglages XMP ; leurs états sont
identifiés par la convention fournie par l’utilisateur.

Un fichier supplémentaire `yellow/6V1A0327-IPR5.tiff` a été ignoré : seul
`6V1A0327-IPR-5.tiff` est pris comme variante n°5. Aucun fichier image ni
sidecar source n’a été déplacé ou modifié par l’analyse.

## Méthode et couverture

Mesures sur **1 291 776 pixels RGB réels**, échantillonnés tous les cinq pixels
après recadrage de la référence LR à la taille IPR, sans interpolation ni
conversion 8 bits. Les déplacements de teinte sont calculés en HSV du RGB
linéaire. Y est la luminance linéaire sRGB. Chaque réponse est rapportée au
neutre de son propre moteur ; le masque spatial commun vient du neutre LR.

| Bande | Pixels au cœur du masque | Part de l’échantillon | Capacité à conclure |
| --- | ---: | ---: | --- |
| Rouge | 79 228 | 6,133 % | Suffisant pour constater les tendances sur cette scène. |
| Jaune | 102 029 | 7,898 % | Suffisant pour constater les tendances sur cette scène. |
| Vert | 134 | 0,0104 % | Insuffisant pour valider/calibrer la bande. |

Le recadrage retenu élimine la première ligne et la dernière colonne LR.
Les quatre coûts de contours sont très proches : 0,386716 à 0,387236.
La correspondance de bord reste donc peu discriminante et la MAE est un
diagnostic échantillonné, pas une registration géométrique définitive.

## Rendu neutre : un écart préalable important

| Mesure globale | Lightroom | IPR |
| --- | ---: | ---: |
| Y moyen | 0,12045 | 0,04705 |

La MAE RGB linéaire neutre est **0,07337** et le ΔE76 moyen **15,64**.
IPR produit ici un neutre sensiblement plus sombre. Une comparaison directe
des TIFF modifiés mélange donc différences de base et différences HSL.
Ces chiffres ne suffisent pas à accuser un profil ou un réglage particulier ;
le rendu neutre reste un chantier distinct du comportement du mélangeur.

## Rouge

| Commande, −100 / +100 | Lightroom | IPR |
| --- | --- | --- |
| Déplacement moyen de teinte | −9,39° / +11,95° | −13,07° / +13,07° |
| Variation de saturation S | −0,282 / +0,106 | −0,456 / +0,239 |
| Rapport Y pour la luminance | ×0,451 / ×1,689 | ×0,276 / ×7,248 |

La direction des trois commandes est cohérente. La saturation IPR agit plus
fortement et modifie sensiblement Y : son réglage −100 donne ici ×1,821 de Y,
et +100 ×0,665. Ce n’est pas une inversion de saturation : S diminue/augmente
correctement, mais le modèle HSV à valeur conservée ne conserve pas Y.

La luminance +100 est nettement plus énergique que LR, même après soustraction
de la différence de neutre : la MAE de réponse RGB dans le masque atteint
**0,2920**. Ce point mérite une calibration dédiée si une réponse photographique
proche de Lightroom est l’objectif.

## Jaune

| Commande, −100 / +100 | Lightroom | IPR |
| --- | --- | --- |
| Déplacement moyen de teinte | −26,13° / +33,16° | −13,69° / +13,69° |
| Variation de saturation S | −0,290 / +0,168 | −0,414 / +0,404 |
| Rapport Y pour la luminance | ×0,522 / ×1,380 | ×0,224 / ×4,494 |

La teinte IPR évolue dans le bon sens mais a une amplitude plus faible que LR.
La saturation agit plus fortement, notamment à +100. La luminance est plus
agressive aux deux extrêmes. La MAE de réponse RGB pour luminance +100 atteint
**0,2919** dans le masque, ce qui confirme un écart propre à la réponse, au-delà
de la différence de rendu neutre.

Les valeurs de ces tableaux ne sont pas des facteurs de correction à recopier
dans le code : les masques, le rendu et les transformations ne sont pas identiques
entre les deux applications.

## Vert

| Commande, −100 / +100 | Lightroom | IPR |
| --- | --- | --- |
| Déplacement moyen de teinte | −58,30° / +46,16° | −4,00° / +3,83° |
| Variation de saturation S | −0,335 / +0,358 | −0,0966 / +0,0953 |
| Rapport Y pour la luminance | ×0,769 / ×1,318 | ×0,772 / ×2,657 |

Les extrêmes ne sont pas identiques et les trois réponses existent. Mais ces
résultats portent sur **134 pixels échantillonnés** seulement, définis à partir
du neutre LR, pas sur une plage verte propre et calibrée. Ils ne permettent ni
de conclure à une erreur du vert IPR, ni de déterminer son gain, ni de valider
sa parité. Il faut une seconde scène contenant une surface verte nette ou du
feuillage, accompagnée des mêmes six variantes et deux neutres.

## Explication technique des différences

L’implémentation actuelle dans `ImageEditor::renderWorkingImage()` utilise
des supports triangulaires entre centres de teinte du **RGB linéaire**, puis :

```text
H' = H + 0,18 × teinte × poids
S' = clamp(S × (1 + 0,01 × saturation × poids), 0, 1)
a  = 0,01 × luminance × poids
V' = max(0, V + a × (a >= 0 ? 1 - V : V))
```

- `0,18` limite le déplacement d’une bande centrale à ±18° avant les autres
  étapes du rendu : ce n’est pas une calibration uniforme sur Lightroom.
- Une saturation qui garde V peut changer Y. Le résultat rouge observé est
  cohérent avec cette propriété ; « luminance constante » n’est pas le contrat
  de cette transformation HSV.
- À luminance +100 et poids 1, V tend vers 1, même pour un pixel sombre. Cela
  explique pourquoi un neutre sombre peut recevoir un gain très important.
- La commande « luminance » agit donc actuellement sur **V HSV**, pas
  directement sur Y physique ou L* perceptuel.

Les algorithmes internes de Lightroom ne sont pas déduits de ces seuls exports.
Le fait que ses réponses diffèrent ne prouve pas qu’il applique une formule HSV,
Lab ou un autre modèle précis.

## Défaut isolé — Protection des gris dans la luminance rouge

La lecture du code a motivé une sonde complémentaire sur une image synthétique
de cinq gris `R=G=B`, sans profil RAW ni différence de cadrage. Elle passe par
les API réelles de chargement/réinitialisation/réglage/export PNG d’IPR et utilise
uniquement un répertoire temporaire.

| État | Valeurs exportées des cinq gris, 8 bits | Protection des neutres |
| --- | --- | --- |
| Neutre | 0, 64, 128, 192, 255 | Référence |
| Luminance rouge −100 | 0, 0, 0, 0, 0 | **Échec** |
| Luminance rouge +100 | 255, 255, 255, 255, 255 | **Échec** |
| Luminance jaune ±100 | 0, 64, 128, 192, 255 | Préservée |
| Luminance verte ±100 | 0, 64, 128, 192, 255 | Préservée |

Cause confirmée : `rgbToHsv()` donne H=0 lorsque la chroma est nulle. Le masque
rouge reçoit donc un poids 1, et la luminance sélective modifie V sans tenir
compte de l’absence de couleur. Au minimum V devient 0 ; au maximum V devient 1.
Même un noir parfait peut ainsi devenir blanc avec la luminance rouge +100.

Ce résultat impose une protection des achromatiques, avec une transition douce
sur les faibles chromas à étudier pour éviter une frontière visible. Le rendu
actuel ne doit pas être déclaré validé sur les neutres. La sonde diagnostique
termine avec succès si les mesures s’achèvent, mais **deux de ses six contrôles
de conservation échouent** : ce n’est pas un test de non-régression « vert ».

Reproduction :

```sh
cmake --build build-wsl-libraw --target colorMixerNeutralProbe -j 2
build-wsl-libraw/colorMixerNeutralProbe
```

Après correction autorisée, transformer ce cas en vrai test de non-régression
qui exige la conservation des gris, et ajouter des gris légèrement teintés,
des noirs et des valeurs intermédiaires. Aucun correctif moteur n’est appliqué
dans cette demande d’analyse.

## Actions proposées

1. Corriger en priorité la protection des gris après accord, puis revalider
   rouge/jaune/vert et les transitions à faible chroma. La correction de ce
   défaut doit être séparée de la calibration visant Lightroom.
2. Ajouter une scène verte suffisamment représentée et quelques états ±25/±50,
   pour étudier la progressivité et non seulement les extrêmes.
3. Après réception de toutes les bandes, traiter séparément le rendu neutre et
   le mélangeur. Pour ce dernier, étudier une saturation avec protection de Y
   et une luminance sélective dont le gain n’est pas une poussée automatique de
   V vers 1 ; tester le gamut et les transitions avant calibration des amplitudes.
4. Ne pas choisir un coefficient par couleur à partir de cette seule photo.
5. Distinguer le futur test violet de magenta : Lightroom possède huit bandes
   dont Purple, IPR n’a actuellement pas de violet indépendant.

## Vérifications automatiques

- `colorMixerQmlTests` : réussi, sélection des bandes sans contamination.
- `imageExportTests` : réussi, sept bandes × trois commandes, isolation des
  centres et persistance v3/migration.
- Comparateur Python : **3 tests réussis**, conventions, lecture native,
  échantillonnage/recadrage et calculs de couleur.
- Sonde moteur sur gris : six mesures réalisées ; **deux échecs de protection**
  confirmés pour la luminance rouge, quatre conservations pour jaune/vert.

Ces résultats automatisés et les signes mesurés ne remplacent pas la validation
photographique de toutes les bandes. Aucun rebuild applicatif n’est nécessaire
pour cette passe : le code de rendu n’a pas été modifié.
