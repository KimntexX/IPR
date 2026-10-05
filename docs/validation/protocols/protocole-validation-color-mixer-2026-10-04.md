# Protocole — Mélangeur de couleurs

**Campagne active après archivage des extrêmes :** suivre le
[protocole intermédiaire](protocole-color-mixer-intermediaires-2026-10-04.md)
pour les douze nouveaux états ±25/±50. Les numéros 1–6 de ce document décrivent
l'ancienne série ±100, désormais dans l'archive, pas les nouveaux TIFF actifs.

## Mise à jour — campagne après correctif huit bandes

Le moteur courant sépare **violet 270°** et **magenta 300°**, protège les neutres
et borne la luminance sélective à ±0,5 EV en RGB linéaire. La suite du document
décrit aussi la campagne historique à sept bandes : ses TIFF ne sont pas une
validation du nouveau moteur et ne doivent pas être renommés ni réinterprétés.

1. Utiliser le nouveau `appIPR.exe`, réinitialiser tous les réglages et vérifier
   les huit bandes à zéro (les anciens sidecars peuvent restaurer des valeurs).
2. Conserver les anciens exports comme référence historique, puis fournir une
   série IPR **entièrement régénérée** par couleur : 1/2 teinte −100/+100, 3/4
   saturation −100/+100, 5/6 luminance −100/+100. Toutes les autres bandes à zéro.
   Export TIFF sRGB 16 bits SDR, dimensions natives, autres réglages identiques
   au neutre de la même application. Ne pas mélanger anciennes/nouvelles passes.
3. Exporter violet et magenta indépendamment, particulièrement sur 6V1A0043.
   Refaire vert sur 6V1A0037, puis orange/cyan/bleu/rouge/jaune sur 6V1A0327.
   Les références LR cohérentes restent réutilisables ; compléter bleu +100 si
   le TIFF n°6 valide n'a pas encore été fourni. Ne pas substituer le violet n°7.
4. Régénérer un neutre IPR par scène pour tracer l'exécutable et l'état utilisés.
   Le correctif seul ne change pas le rendu neutre RAW ; ce neutre permet de le
   contrôler. Les écarts de rendu de base avec LR restent un sujet distinct.
5. Mesurer explicitement la **nouvelle** passe avec `--ipr-mixer-version eight` :

```sh
python3 tests/analyse_color_mixer.py violet magenta --image-id 6V1A0043 \
  --ipr-mixer-version eight --analyse \
  --output docs/validation/raw/mesure-color-mixer-huit-bandes-0043.json
```

Par défaut, le comparateur garde `legacy-seven` pour reproduire l'ancienne
campagne et marquer ses exports violet non équivalents. L'option `eight` est une
déclaration de l'opérateur, pas une preuve déduite des métadonnées IPR.
Les supports bleu et magenta sont aussi différents depuis l'insertion du violet.

Après les extrêmes, vérifier ±25/±50, gradients entre bleu/violet/magenta,
achromatiques et faibles chromas, couleurs très saturées, gamut et cohérence
aperçu/export. Les tests mathématiques garantissent Y et les gains **avant** le
gamut mapping ; les TIFF peuvent s'écarter de ces valeurs sur les pixels hors
gamut. La parité Lightroom et le choix définitif des constantes restent ouverts.
Voir [ADR-0005](../../decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md).

### Résultat de la première passe huit bandes

Voir [l'analyse après correction](../analyses/analyse-color-mixer-huit-bandes-2026-10-04.md).
Le gain de luminance est confirmé sur les séries RAW exploitables et les gris
sont préservés. La première passe rouge/orange/jaune avait les dimensions du
TIFF LR ; les trois séries ont été régénérées depuis le RAW et remesurées.
Le neutre IPR frais de 0327 et les fichiers bleus manquants ont été reçus et
mesurés. Le LR bleu n°6 contenait d'abord deux luminances actives ; son réexport
corrigé est conforme. Les 27 paires IPR montrent une réponse moyenne dans le
bon sens : fonctionnement aux extrêmes confirmé sur ce corpus. Les résidus de
Y rouge/bleu persistent malgré le neutre frais ; ±25/±50, aperçu/export et
calibration photographique restent ouverts.
Le comparateur permet un diagnostic explicite
`--without-ipr-reference` : paires seules, sans métriques relatives au neutre IPR.
Ce mode ne remplace pas un neutre cohérent et ne valide pas une série RAW.

## Campagne historique — moteur sept bandes

## Corpus et convention

RAW : `tests/image/base/6V1A0327.CR3`. Neutres du mélangeur :
`6V1A0327.tif` (Lightroom) et `6V1A0327-IPR.tiff` (IPR), directement dans `base`.
Les variantes sont dans `tests/image/base/color-mixer/<couleur>/`.

| Numéro | Composante isolée | Valeur | Lightroom | IPR |
| --- | --- | ---: | --- | --- |
| 1, sans suffixe | Teinte | −100 | `6V1A0327.tif` | `6V1A0327-IPR.tiff` |
| 2 | Teinte | +100 | `6V1A0327-2.tif` | `6V1A0327-IPR-2.tiff` |
| 3 | Saturation | −100 | `6V1A0327-3.tif` | `6V1A0327-IPR-3.tiff` |
| 4 | Saturation | +100 | `6V1A0327-4.tif` | `6V1A0327-IPR-4.tiff` |
| 5 | Luminance | −100 | `6V1A0327-5.tif` | `6V1A0327-IPR-5.tiff` |
| 6 | Luminance | +100 | `6V1A0327-6.tif` | `6V1A0327-IPR-6.tiff` |

Le fichier sans suffixe dans une couleur n’est **pas neutre**. Une seule
composante d’une seule bande est modifiée par export. Vérifier le retour à zéro
des autres composantes et bandes avant chaque export ; conserver les mêmes
autres réglages que le neutre de chaque application.

## Audit préalable

1. Vérifier les six états par application, RGB/RGBA 16 bits, ICC sRGB et orientation.
2. Lire le XMP **incorporé dans chaque TIFF Lightroom**, pas seulement le XMP
   associé au RAW : ce dernier reflète l’état de travail le plus récent.
3. Confirmer que tous les réglages non-mélangeur sont identiques au TIFF neutre
   de leur application. Le Lightroom de ce corpus utilise notamment netteté 40
   et réduction du bruit couleur 25 : « mélangeur neutre » ne signifie donc pas
   que tous les paramètres Lightroom sont nuls.
4. Les TIFF IPR ne contiennent pas de preuve XMP des états du mélangeur : les
   noms et la déclaration de l’opérateur constituent la convention, à compléter
   par un manifeste ou un export des paramètres dans une future campagne.
5. Écarter les fichiers supplémentaires ambigus ; ne jamais les substituer
   automatiquement à une variante numérotée attendue.

## Comparaison reproductible

Depuis la racine du dépôt, environnement Python avec NumPy et Pillow :

```sh
python3 -m unittest discover -s tests -p test_analyse_color_mixer.py
python3 tests/analyse_color_mixer.py red yellow green --analyse \
  --output docs/validation/raw/mesure-color-mixer-NOUVELLE-PASSE.json
ctest --test-dir build-wsl-libraw -R colorMixerQmlTests --output-on-failure
ctest --test-dir build-wsl-libraw -R imageExportTests --output-on-failure
cmake --build build-wsl-libraw --target colorMixerNeutralProbe -j 2
build-wsl-libraw/colorMixerNeutralProbe
```

Utiliser un nouveau nom à chaque passe pour conserver les mesures brutes. Ne
pas remplacer les TIFF pendant une mesure. Le lecteur refuse les mises en page
non prises en charge et les TIFF incomplets ; il vérifie aussi que chaque fichier
n’a pas changé pendant la lecture de ses pixels.

Le lecteur NumPy conserve les valeurs 16 bits natives des TIFF non compressés
à bandes contiguës. Il n’effectue ni conversion 8 bits ni redimensionnement.
L’alpha IPR est ignoré pour comparer les trois canaux RGB ; la présence d’alpha
non opaque doit être contrôlée séparément avant toute future campagne avec
transparence. Le présent corpus provient d’exports RAW opaques.

Lorsque Lightroom dépasse IPR d’un pixel sur chaque axe, les quatre recadrages
de bord possibles sont comparés sur leurs contours de luminance normalisés.
Le meilleur est retenu ; c’est une hypothèse de correspondance, **pas une
registration géométrique certifiée**. Une faible différence entre candidats,
des corrections optiques différentes ou une retouche locale empêchent de
revendiquer une MAE pixel-à-pixel définitive.

Depuis l’extension de campagne, le neutre IPR est privilégié comme ancre de
géométrie ; à défaut, un état IPR disponible est utilisé, puis la référence LR
pour une série LR seule. Le chemin choisi est enregistré dans `geometry_anchor`.

Le pas spatial vaut `ceil(longueur_max / 1392)` par défaut : seuls des pixels
réels régulièrement espacés sont mesurés. La campagne 6V1A0327 donne 928 × 1392
échantillons, pas une mesure exhaustive des 32 millions de pixels.

## Grandeurs et limites

Après décodage analytique sRGB, la luminance relative est :

`Y = 0,2126 R + 0,7152 G + 0,0722 B`.

Les masques d’analyse sont calculés dans le RGB linéaire du neutre Lightroom :
teinte HSV à ±15° du centre, saturation au moins 0,15 et `0,01 < Y < 0,9`.
Les centres sont ceux d’IPR : 0°, 30°, 60°, 120°, 180°, 240° et 300°.
Ces zones servent à observer les mêmes pixels dans les deux applications ;
elles ne reconstruisent **pas** les masques internes avant rendu d’IPR ou les
masques propriétaires de Lightroom.

- Teinte : déplacement angulaire circulaire par rapport au neutre, mesuré
  uniquement quand les couleurs restent suffisamment saturées.
- Saturation : variation de `S = (max(R,G,B) − min(R,G,B)) / max(R,G,B)`.
- Luminance : rapport des sommes de Y dans le masque entre variante et neutre
  **de la même application**.
- Réponse de paire : différence entre +100 et −100 ; contrôle du sens de variation.
- MAE RGB : écart absolu moyen direct et écart entre les réponses
  `(IPR_variante − IPR_neutre) − (LR_variante − LR_neutre)`.
- ΔE76 : distance Lab, blanc D65 ; ce n’est pas ΔE2000, ni un score absolu de
  qualité de développement. Les deux TIFF sont déjà identifiés comme sRGB.

Les écarts de rendu neutre, de profil, de bruit/netteté et de gamut ne doivent pas
être interprétés comme des défauts du seul mélangeur. Les chiffres hors support
sur le masque Lightroom sont diagnostiques, pas une preuve de contamination
entre bandes IPR. Une teinte qui ne conserve pas Y n’est pas nécessairement un
défaut : HSV n’impose pas de conservation de luminance physique.

## Conditions de conclusion

Séparer trois résultats : commande fonctionnelle, couverture suffisante du
corpus, proximité photographique avec Lightroom. Aucun seuil global arbitraire
de MAE ne valide ces trois aspects à lui seul.

Vérifier également des achromatiques parfaits et quasi neutres : une commande
de couleur sélective ne doit pas traiter un gris comme un rouge au seul motif
que H=0 par convention HSV. La sonde moteur documente l’état actuel ; son code
de sortie zéro signifie « mesure achevée », pas « conservation des neutres validée ».

Une bande peu représentée ne peut être calibrée sur quelques pixels de bord ou
de bruit. Pour la prochaine campagne, viser au minimum environ 1 000 échantillons
propres et une zone continue visible par bande : seuil pratique de préparation,
pas une norme. Ajouter pour le vert une scène avec feuillage ou objets verts et,
si possible, une mire multicolore, ainsi que des valeurs intermédiaires ±25/±50.

Lightroom distingue violet (`Purple`) et magenta. IPR dispose actuellement de
sept bandes, sans violet indépendant ; un dossier `violet` ne peut pas être
assimilé automatiquement à `magenta`.

## Extension — Violet, magenta et séries incomplètes

Le comparateur accepte `--image-id 6V1A0037` ou `6V1A0043`, ainsi que le groupe
`violet`. Ce dernier utilise un centre diagnostique 270° et audite les clés XMP
`*AdjustmentPurple`. Il n’insère pas un centre 270° dans les sept supports IPR.
La comparaison violet/IPR est marquée non équivalente tant qu’une bande dédiée
Purple n’est pas identifiée dans le moteur.

L’opérateur confirme utiliser le dernier cercle pour violet, alors que le code
le nomme Magentas et le centre à 300°. Cette information doit accompagner toute
interprétation de la campagne ; ne pas changer automatiquement les noms des
dossiers ou la signification des anciens sidecars.

Une série incomplète peut être mesurée : le JSON expose `missing_files` et les
paires absentes ne sont pas synthétisées. Une série LR seule produit uniquement
ses réponses LR, sans verdict ou MAE IPR. Les métadonnées non-mélangeur sont
comparées au neutre LR ; les incohérences apparaissent dans `audit`. Les profils
hors sRGB sont refusés, car le décodage analytique employé ne les convertit pas.

Les états IPR n’ont pas de métadonnées de réglages permettant cet audit : la
convention de noms reste une déclaration de l’opérateur, non une preuve moteur.

`--inspect-file CHEMIN` identifie un TIFF isolé. Un fichier placé ailleurs peut
être utilisé par `--variant-file BANDE LR NUMERO CHEMIN` après identification
explicite ; le chemin de substitution est enregistré et l’audit XMP reste actif.
Ne pas renuméroter implicitement un fichier n°7 ni supposer qu’il s’agit du bleu
+100 : il peut contenir plusieurs réglages simultanés.
