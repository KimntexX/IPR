# Fonctionnement de `ImageEditor::renderWorkingImage`

## Objet et contrat

**Directive cible, non implémentée (15 octobre 2026) :** séparer le master de
travail, l'aperçu converti vers le profil ICC écran et la conversion d'export.
Voir [ADR-0008](decisions/ADR-0008-separation-travail-affichage-export.md) et
l'[architecture d'affichage](architecture/gestion-couleurs-affichage.md).
Le master courant est **Rec.2020 linéaire D65** depuis le port du 15 octobre 2026
([ADR-0009](decisions/ADR-0009-espace-travail-rec2020-lineaire.md)).
Le [plan de port](architecture/integration-rec2020.md) distingue l'étude
préparatoire, l'implémentation locale et les validations encore nécessaires.

`ImageEditor::renderWorkingImage()` construit une nouvelle image de travail à partir :

- d'une image source déjà développée et normalisée ;
- d'un instantané des réglages d'édition.

Sa signature est :

```cpp
static ColorPipeline::WorkingImage renderWorkingImage(
    const ColorPipeline::WorkingImage &source, const Settings &settings);
```

L'entrée est une image RGBA flottante en Rec.2020 linéaire D65 (`CV_32FC4`), avec alpha droit. La fonction renvoie la même représentation logique après les réglages, ainsi que son espace colorimétrique et la provenance de la transformation source. Elle ne charge pas le fichier, ne modifie pas le provider QML et n'écrit rien sur disque. Ces opérations appartiennent respectivement au chargement, au rendu d'aperçu et à l'export. Les entrées de type/espace incorrect ou non finies sont refusées, y compris sur le chemin neutre.

Une source vide donne immédiatement un `WorkingImage` vide (`return {}`). Une erreur d'application température/nuance est également signalée par un avertissement puis par une image vide. Les étapes suivantes de conversion doivent vérifier cette image vide ; l'aperçu signale alors l'échec et l'export renvoie un message d'erreur de conversion.

Lorsque tous les réglages sont neutres (y compris la température de référence),
la fonction retourne directement le `WorkingImage` source. Cette copie est
superficielle pour `cv::Mat` : aucun tampon RGB, aucune séparation/fusion de
canaux et aucune boucle par pixel ne sont alors alloués ou exécutés. Dès qu'un
réglage a un effet, seules les étapes concernées sont parcourues ; les étapes
neutres restent court-circuitées.

## Déroulement du rendu

`renderWorkingImage()` ne contient plus de traitement d'image : c'est un
cadenceur qui enchaîne des modules, chacun avec son interface (`.h`) et son
implémentation (`.cpp`) sous `src/core/`
([ADR-0017](decisions/ADR-0017-imageeditor-cadenceur-modules.md)). Ordre
effectif ; les réglages sont non destructifs (`m_original` reste la base) et
chaque module ignore ses réglages neutres.

| Étape | Traitement | Module ou fonction |
| --- | --- | --- |
| 1 | Tampon RGB (l'alpha est repris à la fin) | cadenceur (`cv::cvtColor`) |
| 2 | Exposition et balance des RAW avec DCP | `CameraProfileEngine::renderFromCameraSignal` |
| 3 | Niveaux de gris | `ColorAdjustments::toGrayscale` |
| 4 | Exposition des sources sans signal caméra | cadenceur (gain `2^EV`) |
| 5 | Contraste | `Contrast::apply` |
| 6 | Température et nuance sans DCP | `ColorPipeline::applyTemperatureTint` |
| 7 | Ombres, noirs, hautes lumières, blancs | `ToneRecovery::apply` |
| 8–9 | Saturation et vibrance, mélangeur, color grading (une seule étape en Oklab) | `PerceptualColour::apply` (`Vibrance`, `ColorMixer`, `ColorGrading`) |
| 10 | Bruit | `DetailProcessing::reduceNoise` |
| 11 | Texture et clarté, puis voile | `LocalContrast::apply`, `Dehaze::apply` |
| 12 | Courbes | `ToneCurve::apply` |
| 13 | Flou | cadenceur (`cv::GaussianBlur`) |
| 14 | Netteté | `DetailProcessing::sharpen` |
| 15 | Grain | `Grain::apply` |
| 16 | Reconstruction RGBA | cadenceur |

L'aperçu calcule ensuite son histogramme avec `Histogram::compute`.

### Séparation alpha et tampon RGB

La fonction copie R, G et B de `source.linearRgba` dans `adjusted`
(`cv::cvtColor`, `COLOR_RGBA2RGB`), sauf si l'aperçu reprend une étape gardée
en cache (voir [Cache des étapes de l'aperçu](#cache-des-étapes-de-laperçu)).
L'alpha d'origine n'est modifié par aucun réglage. La source, passée par
référence constante, n'est pas modifiée.

### Exposition

L'image de travail est linéaire : l'exposition est un gain exprimé en EV
(`gain = 2^brightness`). Pour un RAW avec profil DCP, elle s'applique au signal
caméra **avant** la courbe du profil, avec la balance des blancs
(`renderFromCameraSignal`) : les hautes lumières gardent l'épaule du profil.
Les autres sources reçoivent le gain directement. Le setter `setBrightness()`
borne la valeur à `[-5, +5]` EV ; les curseurs QML ont un pas de `0,01` EV.

### Contraste

`Contrast::apply` : courbe en S à extrémités fixes dans un domaine logarithmique
(16 EV sous le blanc), pivot `0,32`, appliquée à chaque canal RGB. Le noir et le
blanc restent fixes ; par canal, un contraste positif sature aussi les couleurs
claires, comme Lightroom. Forces : `0,60` en positif, `0,45 × 1,08` en négatif,
ajustées sur les références Lightroom et validées par l'utilisateur (rapport
Lumières).

### Température et nuance

Pour les sources sans DCP, la fonction délègue cette étape à `ColorPipeline::applyTemperatureTint()`, qui attend un tampon `CV_32FC3`, après l'exposition et le contraste ; les RAW avec DCP la reçoivent au signal caméra (étape 2). Les réglages changent les gains RGB, pas l'alpha.

Le contrat actuel du pipeline attend une température en kelvins dans `[2000, 50000]` et une nuance dans `[-150, 150]`. Une erreur entraîne un journal d'avertissement et l'abandon du rendu. Les gains précis sont définis dans `ColorPipeline.cpp` ; ils ne représentent pas nécessairement un calcul physique complet de balance des blancs depuis l'illuminant de la scène.

Le point neutre (le pivot autour duquel les gains rouge/bleu s'annulent) n'est plus une constante fixe à 6500 K : `applyTemperatureTint()` reçoit désormais une **température de référence** explicite, `source.referenceWhiteBalanceKelvin`, qui correspond à la balance des blancs détectée sur l'image source :

- Pour les fichiers RAW décodés par LibRaw, cette valeur est estimée par `RawDecoder::estimateWhiteBalanceKelvin()` à partir des multiplicateurs **`cam_mul`** (balance « telle que prise de vue », propre à chaque photo) et de **l'inverse** de la matrice `cam_xyz` (approximation de McCamy), et propagée dans `ColorPipeline::WorkingImage::referenceWhiteBalanceKelvin`.
  - `cam_xyz` exprime la réponse des canaux caméra à une couleur XYZ (`RVB_cam = cam_xyz × XYZ`), et non l'inverse : il faut donc inverser cette matrice 3×3 pour convertir un RVB neutre caméra en XYZ. Une ancienne version du code utilisait `cam_xyz` directement (sans inversion) et les multiplicateurs `pre_mul` (constante « lumière du jour » fixe par modèle d'appareil, indépendante de la prise de vue) au lieu de `cam_mul` (balance réellement mesurée/réglée pour la photo). Ce double défaut produisait une température quasi constante et anormalement élevée (~12 000-13 000 K) pour toutes les photos d'un même modèle d'appareil, au lieu de la température « As Shot » réelle (ex. ~5200 K rapportée par Lightroom). Le correctif inverse désormais la matrice et utilise `cam_mul` en priorité (`pre_mul` reste un repli défensif si `cam_mul` est indisponible), et est couvert par le test `whiteBalanceEstimationTest`.
- Pour les formats standards (JPEG/PNG/TIFF…), une estimation best-effort est tentée via les métadonnées textuelles exposées par `QImageReader` (`readWhiteBalanceKelvin()` dans `imageeditor.cpp`) ; en l'absence de métadonnée exploitable, la valeur reste indéterminée.
- Si aucune valeur n'est disponible, le rendu retombe sur `5200 K` comme référence neutre par défaut (`.value_or(5200.0)`). Ce repli d'interface n'est pas une calibration RAW ni une modification de la WB caméra.

`Settings::temperature` est initialisé, au chargement de l'image et lors de `resetAdjustments()`, à cette même référence détectée (arrondie à l'entier le plus proche), et non plus à une constante fixe (`5200` précédemment). Le signal `adjustmentsReset(int temperatureKelvin, int tint)` transmet cette valeur au QML afin que le curseur de température affiche d'emblée la balance des blancs réelle de la photo plutôt qu'un neutre arbitraire, ce qui évite une mauvaise interprétation initiale des couleurs.

Côté QML, `Setting.qml` possède **deux** mécanismes de réinitialisation distincts qui doivent rester synchronisés avec cette référence détectée : la réinitialisation globale (`Connections::onAdjustmentsReset`) et le bouton de réinitialisation individuel du curseur de température (`AdjustmentControl.resetValue`). Ce dernier utilisait auparavant une constante `6500` codée en dur au lieu de la référence réelle ; `Setting.qml` expose désormais `root.referenceTemperatureKelvin`, mise à jour par `onAdjustmentsReset` et utilisée comme `resetValue` du curseur, afin que les deux boutons de réinitialisation retombent sur la même valeur.

Le gain RGB appliqué pour une température/référence donnée n'est plus un gain diagonal dérivé d'un `log(Kelvin)` normalisé par toute la plage du curseur (`[2000, 50000]`) : cette ancienne formule diluait fortement l'effet perçu près des températures de prise de vue courantes (~5000-6500 K), rendant les réglages extrêmes (ex. 2000 K) insuffisamment saturés comparés à Lightroom/RawTherapee/DxO. `applyTemperatureTint()` calcule désormais une matrice 3×3 d'adaptation chromatique physiquement motivée :

1. La chromaticité du locus de Planck est calculée pour la référence et pour la température cible via l'approximation rationnelle de Krystek (1985), valide sur toute la plage `[2000, 50000]` K utilisée ici.
2. Ces chromaticités (x, y) sont converties en XYZ (Y = 1), puis en espace cônique LMS via la matrice de Bradford (adaptation chromatique standard des profils ICC).
3. Le gain par canal est `gain = LMS_référence / LMS_cible`, normalisé pour que le gain du canal M (vert/luminance) reste à 1 : le curseur de température ne redistribue donc l'énergie qu'entre rouge et bleu, le vert/magenta restant du ressort exclusif de la nuance (`tint`).
4. Les étapes 1 à 3 sont combinées en une seule matrice `netMatrix = XYZtoRGB × Bradford⁻¹ × diag(gain) × Bradford × RGBtoXYZ`, précalculée une fois par appel puis appliquée par un simple produit matrice-vecteur par pixel (coût identique au gain diagonal précédent).
5. Pour les seules cibles plus froides que la référence, une calibration empirique, issue du corpus Lightroom/IPR de huit scènes, est composée après Bradford. Son intensité augmente continûment en logarithme des kelvins jusqu'à 2000 K ; à cette borne, elle applique les coefficients RGB `(0,35 ; 1,19 ; 0,84)`. Ces valeurs v3 ont été ajustées après une première validation v2, car la composition matricielle et le rendu hors gamut atténuent l'effet des coefficients directs. Elles compensent la sous-atténuation rouge observée sans changer la branche chaude, déjà proche de Lightroom. Cette partie doit rester identifiée comme une calibration de rendu et être revalidée à chaque évolution du pipeline, à la différence de l'adaptation Bradford qui est le modèle colorimétrique de base.

La Nuance emploie des gains symétriques sur R/B (magenta) et opposés sur G (vert). Avant application, ils sont normalisés par leur luminance Rec. 709 sur un gris neutre (`0,2126 R + 0,7152 G + 0,0722 B`). Le curseur reste donc chromatique : à nuance négative il va vers le vert, à nuance positive vers le magenta, sans provoquer le décalage d'exposition qui affectait auparavant les deux extrêmes.

Cas particulier : si la température demandée est strictement égale à la référence détectée et que la nuance est nulle, la fonction retourne immédiatement sans passer par la matrice (évite un bruit flottant négligeable mais non nul introduit par l'aller-retour via l'inverse de Bradford, et garantit un résultat bit-à-bit identique à l'image source dans ce cas neutre).

Ce nouveau modèle a été validé numériquement par comparaison avec la table de calibration réelle du capteur (`raw.imgdata.color.WBCT_Coeffs`, exposée par LibRaw) et par rendu visuel de `6V1A0027.CR3` : à 2000 K, l'image devient fortement bleue (comportement cohérent avec Lightroom à ce réglage extrême) ; à 50000 K, elle devient fortement orangée — les deux extrêmes du curseur produisent désormais un effet net, là où l'ancienne formule restait quasi neutre même en butée.

Pour les RAW, l'ouverture et la réinitialisation utilisent la balance boîtier
(*As Shot*) par défaut, comme la référence Lightroom du corpus. La balance
automatique reste un choix explicite dans l'interface ; elle redécode le RAW et
peut modifier sensiblement un ciel ou une scène dont les zones lumineuses ne
sont pas neutres.

### Ombres, noirs, hautes lumières et blancs

`ToneRecovery::apply` (plan B du 18 octobre 2026) : chaque réglage déplace les
tons d'une quantité en EV lue sur les courbes de transfert de Lightroom
(`ToneRecoveryModel.h`, généré par `scripts/fit_tone_recovery.py`). Ombres, HL
et blancs positifs s'adaptent à l'image : leur effet est multiplié par un poids
prédit à partir des statistiques de luminance mesurées une fois à l'ouverture
(`ToneRecovery::measure`, portées par l'image de travail dans le cache et le
master d'export). Ombres, HL et blancs négatifs agissent sur la luminance ; un
éclaircissement atteint sa luminance et rapproche du blanc une couleur qui
sortirait du gamut, un assombrissement garde puis amplifie la couleur des tons
clairs. Noirs et blancs positifs agissent sur chaque canal sRGB linéaire. La
courbe est échantillonnée et rendue croissante : aucune inversion de tons.
Modèle, mesures et limites : [roadmap Lumières](architecture/roadmap-lumieres.md)
et [rapport Lumières](validation/analyses/analyse-validation-lumieres-base-2026-10-02.md).

### Vibrance et saturation

Après les réglages tonals, `PerceptualColour::apply` (`src/core/PerceptualColour.cpp`)
passe chaque pixel du master Rec.2020 linéaire en Oklab une seule fois
(`WorkingColor::workingToOklab`, une matrice directe, neutre sur les gris), y
applique dans l'ordre saturation et vibrance (`Vibrance::adjust`), le mélangeur
(`ColorMixer::adjust`) et le color grading (`ColorGrading::adjust`), puis revient en
Rec.2020, sans écrêtage. Vibrance et mélangeur laissent les gris exacts inchangés ;
le zéro de tous les curseurs et les entrées vides ou d'un autre type ne touchent pas
le tampon. Saturation et vibrance sont bornées à `[-100, +100]`.

- **Mélangeur** : tables de réponse Lightroom par bande, composante et valeur
  ([rapport mélangeur](validation/analyses/analyse-color-mixer.md)).
- **Color grading** : ombres, tons moyens, hautes lumières et roue Global, avec
  fusion, balance et luminance, calés sur Lightroom
  ([rapport grading](validation/analyses/analyse-grading.md)).

- **Saturation** multiplie la chroma Oklab par `1 + s/100` et conserve L.
  Mesurée sur 49 scènes, elle suit LR à moins de 0,05 de variation de chroma.
- **Vibrance** suit les tables de réponse LR de `src/core/VibranceModel.h`,
  générées par `scripts/build_vibrance_model.py` : gains log de chroma et de
  clarté Oklab et rotation de teinte, appliquée par une rotation plane de
  `(a, b)`, selon la saturation ProPhoto linéaire `(max − min)/max`, la
  clarté et la teinte Oklab du pixel, mesurés aux valeurs ±25, ±50, ±100 sur les
  49 scènes de la base. Interpolation trilinéaire entre centres de cellules
  (teinte circulaire), linéaire entre valeurs mesurées, zéro à zéro. Un facteur
  par valeur atténue la réponse des quasi-gris ; le gain de clarté et la rotation
  s'effacent sous une chroma Oklab de 0,01 pour rester continus vers le gris.

LR ne s'adapte pas à la scène : saturation, clarté et teinte du pixel expliquent
93–95 % de la variance de son gain positif, l'identité de la scène 2 % de plus.
La réponse positive est maximale sur les tons moyens modérément saturés et
protège les couleurs déjà saturées ; au négatif, les couleurs faibles sont
presque entièrement désaturées et les couleurs saturées assombries. Les détails
et la validation croisée sont dans le
[rapport Couleur](validation/analyses/analyse-couleur.md) ; la démarche complète
est expliquée dans le [cours 12](scholar/cours-12-vibrance-saturation-oklab.tex).

### Effets de détails et d'atmosphère

Avant les effets locaux, `DetailProcessing::reduceNoise()` traite le RGB
linéaire si l'un des deux bruits est non nul. Depuis le correctif consolidé
le 5 octobre, le modèle emploie Y Rec.709 et Co/Cg, des seuils Haar/MAD
proportionnels au curseur, des guides de structure préfiltrés, un rééquilibrage
des changements pour conserver la moyenne et une compression vers le gris
préservant Y si la reconstruction produit des canaux négatifs. Pas de clamp
HDR supérieur ; à bruit 0/0, la fonction ne touche pas le tampon.
La netteté masquée est appliquée plus tard et reste inchangée.
Les [équations et constantes](architecture/denoising-sharpening.md) et
le [correctif/tests](validation/analyses/correctif-bruit-2026-10-05.md)
précisent les limites ; les anciens exports bruit ne valident pas cette version.

Les effets sont appliqués en RGB linéaire après la couleur perceptuelle, calés sur
la réponse de Lightroom mesurée par échelle et par amplitude
([rapport Effets](validation/analyses/analyse-effets.md)). Leurs rayons sont définis
sur l'image entière : l'aperçu les réduit par `pixelScale`.

- **Texture et clarté** (`LocalContrast`) : la luminance est découpée en bandes de
  différences de gaussiennes de 1 à 256 pixels, en racine cubique pour la texture et
  en logarithme pour la clarté ; dans chaque bande, les petites variations reçoivent
  le gain de Lightroom à la valeur du curseur, les grandes (les bords) beaucoup
  moins, ce qui évite les halos. Les couleurs gardent leurs rapports de canaux.
- **Correction du voile** (`Dehaze`) : la luminance gagne un nombre d'IL fonction de
  son rapport à l'air ambiant `A` (99ᵉ centile de la luminance) et du voile local
  (canal sombre lissé), lissé en une base locale ; autour de la base, le détail reçoit
  un gain à toutes les échelles ; la couleur suit la loi mesurée sur Lightroom
  (ajouter du voile ajoute de la lumière de la couleur de l'air ambiant, en retirer
  ôte sa dominante). Les pixels plus clairs que leur fond près de `A` ne gagnent pas
  de contraste vers le haut.
- **Grain** (`Grain`) : bruit monochrome déterministe, fin, de même écart-type à tous
  les tons, ajouté aux valeurs encodées pour l'affichage, comme celui de Lightroom
  (taille 25, rugosité 50) ; ajouté après le flou pour rester net, il ne scintille
  pas entre deux rendus.

Texture, clarté et correction du voile acceptent `[-100, +100]`. Le grain est
intentionnellement limité à `[0, 100]`.

### Courbes de tonalité et histogramme

Après les effets, `ToneCurve::apply` applique quatre courbes à points :

1. une courbe **Luminance** ; elle calcule une luminance cible puis applique un
   ratio RGB commun, ce qui préserve la chromaticité ;
2. les courbes **Rouge**, **Vert** et **Bleu**, appliquées dans cet ordre aux
   composantes linéaires.

Les courbes passent exactement par toutes les poignées avec un polynôme global
de **Lagrange**, évalué sous sa **forme barycentrique** (Berrut & Trefethen,
*Barycentric Lagrange Interpolation*, 2004) plutôt que sous la forme classique
à coefficients monomiaux :

```text
p(x) = [ Σᵢ wᵢ/(x - xᵢ) · yᵢ ] / [ Σᵢ wᵢ/(x - xᵢ) ]
```

où `(xᵢ, yᵢ)` sont les poignées (extrémités fixes `(0,0)`/`(1,1)` incluses) et
`wᵢ` les poids barycentriques classiques du polynôme interpolateur. Cette
formulation reste numériquement stable quel que soit le nombre de poignées,
contrairement à la résolution directe d'un système de Vandermonde qui devient
mal conditionnée dès que le degré augmente. Les extrémités fixes `(0,0)` et
`(1,1)` forment la courbe affine de degré 1 sans poignée interne ; chaque
poignée interne ajoute un degré : une poignée donne un polynôme de degré 2,
deux poignées un degré 3, etc. Il n'y a donc ni cassure de pente ni raccord
entre segments dans l'intervalle `[0,1]`.

Les poignées internes sont triées et gardent un écart minimal de `0,001` sur
l'axe des entrées ; leurs sorties restent bornées à `[0,1]`. Les valeurs HDR
supérieures à un sont extrapolées avec la dérivée exacte du polynôme au dernier
point, ce qui conserve une tangente continue au-delà de la plage éditable. Une
courbe vide est la diagonale d'identité. Cela évite de supprimer les hautes
valeurs linéaires avant la compression de gamut. Des poignées placées avec des
variations très fortes peuvent toutefois produire une courbe raide : la
continuité mathématique ne remplace pas une correction tonale modérée.

L'interface permet de sélectionner l’une des quatre courbes, d'ajouter une
poignée par double-clic, de la déplacer, de supprimer une poignée interne par
clic droit et de réinitialiser une courbe ou l’ensemble. Le Canvas échantillonne
le même polynôme afin que la courbe affichée ne soit pas une approximation par
segments. Les changements sont non destructifs, déclenchent le même rendu
asynchrone que l’aperçu et sont repris par les exports.

L'aperçu produit ensuite, avec `Histogram::compute`, un histogramme de 256 bins pour la luminance, le rouge,
le vert et le bleu. Les bins utilisent des valeurs encodées sRGB pour rester
lisibles face à l’aperçu ; ils décrivent désormais les pixels sRGB 8 bits
réellement affichés après gamut mapping. La luminance est calculée après
décodage de ces canaux sRGB, puis réencodée pour son bin. L'alpha n'est pas
un canal de l'histogramme.

### Compression de gamut de sortie

Les réglages utilisent Rec.2020 linéaire, sans bornage global du master ; les
bornages propres à certains opérateurs restent explicites. Avant
l'aperçu et les exports JPEG/PNG/TIFF/DNG, `ColorPipeline::mapToSrgbGamut()`
convertit les valeurs vers sRGB linéaire, puis ramène les couleurs hors gamut
dans le cube de sortie. Le mapping actuel borne d'abord les composantes
négatives, recalcule Y, réduit les Y supérieurs à un, puis la chroma vers le gris
de même Y jusqu'à ce que les
trois canaux soient représentables. Contrairement à un `clamp` indépendant de
R, G et B, cette compression préserve la direction de teinte autant que le
gamut le permet et évite les franges colorées dans les blancs très lumineux.

### Flou gaussien

Pour `blur > 0`, un flou gaussien est appliqué au tampon RGB avec `sigma = blur / 2`. OpenCV choisit la taille du noyau (`Size(0, 0)`). L'alpha n'est pas flouté, car il est réintégré depuis les canaux source à la fin.

### Reconstruction RGBA et retour

Les canaux RGB traités sont séparés, puis l'alpha source (`cv::extractChannel`) est ajouté et les quatre canaux sont fusionnés dans un nouveau tampon. Le retour contient :

```cpp
return {rgbaOutput, source.colorSpace, source.sourceProfile,
        source.referenceWhiteBalanceKelvin};
```

Les pixels sont nouveaux ; les métadonnées de couleur/provenance restent celles de l'image source. La fonction ne réencode pas l'image en sRGB gamma ni en 8 bits.

## Partage entre aperçu et export

La fonction est commune aux deux destinations, ce qui empêche les réglages de diverger :

```text
aperçu QML :
  renderWorkingImage(source, settings)
  → ColorPipeline::toSrgb(..., Preview8Bit)
  → ImageProvider / QML

export (ImageEditor::writeImage) :
  renderWorkingImage(source, settings)
  → ImageExporter::write
     ├─ JPEG/PNG/TIFF : ColorPipeline::toSrgb(...) → QImageWriter
     └─ DNG : RGB linéaire 16 bits → LibTIFF + balises DNG
     puis publication sous le premier nom libre
```

Les conversions finales et la quantification dépendent du format d'affichage ou d'export ; les transformations de développement et les réglages sont partagés. Le DNG contient un rendu RGB linéaire 16 bits avec les réglages intégrés : il n'est pas une copie du RAW mosaïque source et ne conserve donc pas sa latitude de développement ni ses métadonnées de prise de vue. Le dialogue de sauvegarde est prérempli avec `nom-source-IPR.<format>` dans le dossier de la source. Le backend accepte les URLs `file:` et les chemins absolus natifs que certains dialogues Windows retournent sous la forme `C:/…`. L'appel d'export capture l'image source et les réglages avant le travail asynchrone, de sorte que le rendu utilise un instantané cohérent même si l'interface change ensuite.

### Précision du transfert sRGB de sortie — correctif du 7 octobre 2026

`ColorPipeline::toSrgb()` conserve le gamut mapping existant puis applique
la fonction de transfert sRGB directement aux canaux RGB flottants :
`12,92 × C` pour `C ≤ 0,0031308`, sinon `1,055 × C^(1/2,4) − 0,055`.
La quantification 16 bits intervient ensuite, une seule fois. Alpha reste
linéaire et non prémultiplié. L'image encodée est marquée ICC sRGB ; la
conversion ICC des images sources reste inchangée. Cette opération est
commune à l'aperçu et aux sorties JPEG/PNG/TIFF, pas à la branche DNG linéaire.

L'ancien chemin quantifiait d'abord en linéaire 16 bits puis utilisait une
conversion entière Qt à précision de table réduite. Dans notre environnement,
les codes linéaires 1–15 devenaient zéro. Le nouveau chemin rend 13 pour
le code 1 et 194 pour le code 15. Une rampe exhaustive de 65 536 entrées
vérifie monotonie, extrêmes, neutralité RGB et conservation alpha, avec une
tolérance maximale d'un code face à la formule analytique.

### Proxy d'édition pour l'aperçu interactif

Sans cache, `renderWorkingImage()` recalcule l'intégralité du pipeline à chaque modification de réglage ; son coût est donc proportionnel au nombre de pixels traités. Pour l'aperçu, `ImageEditor::startRender()` ne part pas de l'image source pleine résolution (`m_original`) mais d'un second tampon, `m_editProxy`, produit par `ImageEditor::buildEditProxy()` :

- si le plus grand côté de l'image source ne dépasse pas 2048 px, le proxy est une copie superficielle de la source (aucun redimensionnement, aucune allocation supplémentaire) ;
- sinon, l'image est réduite par `cv::resize(..., cv::INTER_AREA)` jusqu'à ce que son plus grand côté atteigne 2048 px, en conservant son espace colorimétrique, sa provenance et sa température de référence.

Le proxy est calculé une seule fois par chargement d'image (dans `loadFromDisk()`, donc déjà hors du thread UI) et réutilisé pour tous les rendus d'aperçu suivants, jusqu'au prochain chargement. L'export (`exportImage()`/`writeImage()`) continue d'utiliser `m_original` sans aucune dégradation : seule la réactivité de l'aperçu est affectée, pas la qualité du résultat final. L'histogramme affiché à l'écran est calculé sur ce même proxy ; sa forme reste représentative de l'image, mais ses comptages exacts (en particulier les pixels en butée à 0 ou 255) peuvent légèrement différer de ceux d'un calcul en pleine résolution, par effet de moyennage du redimensionnement.

### Cache des étapes de l'aperçu

Ajout du 25 octobre 2026. L'aperçu passe à `renderWorkingImage()` un
`RenderStageCache` (`src/cache/RenderStageCache`), possédé par `ImageEditor`
et vidé à chaque changement ou libération de l'image. L'export n'en reçoit
jamais.

- Cinq étapes coûteuses peuvent être gardées, dans l'ordre du pipeline :
  tons (signal capteur, noir et blanc, exposition, contraste, balance, ombres,
  noirs, hautes lumières, blancs), couleur perceptuelle, réduction du bruit,
  texture et clarté, voile.
- La clé d'une étape contient ses réglages et ceux de toutes les étapes
  précédentes ; une entrée appartient à une source (adresse du tampon du
  proxy, retenu par le cache) et à un `pixelScale`.
- Le rendu reprend après la dernière étape dont la clé n'a pas changé, sur
  une copie privée de sa sortie, puis exécute la suite.
- L'étape éditée est la première dont la clé diffère du rendu précédent. Le
  cache garde au plus deux sorties : celle qui la précède, pour les rendus
  suivants du même glissement, et celle des tons. Cela représente environ
  34 Mo chacune sur le proxy de 2048 px.
- Les pixels sont identiques au bit près avec ou sans cache
  (`renderStageCacheTests`, 18 éditions successives sur toutes les étapes).

Rendu complet de l'aperçu sur 6V1A0327 (proxy 1365 × 2048, réglages actifs
sur toutes les étapes), moyenne de cinq rendus d'un même glissement :

| Curseur glissé | Sans cache | Avec cache |
| --- | ---: | ---: |
| Exposition | 1136 ms | 1139 ms |
| Color grading | 1150 ms | 863 ms |
| Réduction du bruit | 1129 ms | 811 ms |
| Clarté | 1145 ms | 757 ms |
| Voile | 1143 ms | 664 ms |
| Netteté | 1118 ms | 454 ms |

Le reste incompressible est dominé par la conversion de sortie
`ColorPipeline::toSrgb` (environ 250 ms : correspondance de gamut et
quantification) et par l'histogramme.

### Comparaison origine / actuel et zoom d'affichage

`ImageViewport.qml` gère le zoom et la comparaison sans modifier les
réglages photo. Son bouton Avant/après (Ctrl Maj C) affiche l'origine à
gauche et le rendu actuel à droite, séparés par une frontière mobile.
Les deux images partagent la même transformation et sont clipsées dans
des régions complémentaires pour ne pas doubler la composition alpha.
Le zoom reste celui du proxy, pas un affichage du RAW pleine résolution.

`renderPreview(source, settings, includeOriginal)` encode également la
source neutre en sRGB 8 bits lorsque la référence n'est pas encore prête.
Cette conversion contourne `renderWorkingImage()` et donc les réglages
de l'éditeur/sidecar, mais conserve le développement, les profils et la
balance des blancs déjà appliqués au décodage. Elle est exécutée avec le
premier rendu accepté, pas à chaque mouvement de curseur. L'histogramme
continue de représenter uniquement le rendu actuel.

`ImageProvider::setImages()` publie les deux QImage sous le même verrou.
`originalImageSource` utilise `image://editor/original?revision=…`, stable
pendant les réglages ; l'URL courante continue d'être révisée pour chaque
rendu. Les résultats périmés ne sont pas publiés. Au changement de source,
les deux URL et les références du provider sont effacées ; le prochain
premier rendu recrée l'origine, y compris après un redécodage automatique
de balance des blancs. Le provider conserve au maximum un aperçu neutre
supplémentaire de 16 Mio de pixels CPU (2048×2048×4), hors ressources Qt Quick.
Cette référence n'est ni persistée ni conservée dans le LRU de RAW.

Le [contrat de zoom/comparaison](specifications/zoom-image.md) détaille
les raccourcis, les limites et la recette. Les exports ne sont pas modifiés.

### Cache mémoire des RAW inactifs

Au changement d'image, `ImageEditor` transfère l'ancien RAW décodé et son proxy
dans `DecodedImageCache`, puis libère immédiatement ses références actives
`m_original` et `m_editProxy` avant un nouveau décodage. Le cache est un LRU
borné à **256 Mio** par défaut ; son budget est configurable via la propriété
`decodedImageCacheBudgetBytes`. Sa clé inclut le chemin canonique, la taille,
la date de modification et le mode de balance des blancs automatique, ce qui
évite de réutiliser une version modifiée du fichier ou des pixels développés
avec un autre mode RAW. Une consultation remet l'entrée en tête du LRU et le
dépassement du budget libère les entrées les moins récemment utilisées.

Les copies de `cv::Mat` restent superficielles. En particulier, pour une image
qui ne nécessite pas de réduction, l'original et le proxy partagent le même
tampon et ne sont comptés qu'une fois dans le budget du cache.

## Pourquoi retourner `ColorPipeline::WorkingImage` par valeur ?

### Le type de retour décrit exactement le résultat

Le résultat ne se limite pas aux pixels. `WorkingImage` regroupe :

- le `cv::Mat` RGBA linéaire ;
- le `QColorSpace` associé ;
- la chaîne de provenance du profil ou de la transformation.

Retourner cette structure évite de séparer les pixels de leurs métadonnées, et rend visible à l'appelant que le résultat est une image de travail conforme au contrat colorimétrique.

### La valeur exprime la propriété et la durée de vie

La fonction fabrique un nouveau tampon `rgbaOutput`. Retourner un objet valeur signifie que le résultat appartient à l'appelant et reste valide après la sortie de la fonction. Il n'est pas nécessaire de gérer manuellement `new`/`delete`, ni de se demander si un pointeur désigne une variable locale.

Avec un pointeur vers `WorkingImage`, il faudrait préciser :

- qui alloue et détruit l'objet ;
- si le pointeur peut être nul ;
- si la fonction remplace le contenu existant ou le modifie ;
- combien de temps l'objet reste valide ;
- si l'appelant doit fournir un objet initialisé.

Un pointeur vers un `cv::Mat` local serait particulièrement incorrect : sa durée de vie se terminerait à la sortie de la fonction. Un pointeur de sortie vers un objet possédé par l'appelant serait possible, mais ajouterait un protocole de mutation et de durée de vie sans bénéfice ici.

### Le retour par valeur n'implique pas une copie complète des pixels

`cv::Mat` utilise une gestion de mémoire à comptage de références. Sa copie ordinaire est généralement superficielle : les copies partagent le stockage et incrémentent le compteur de références ; la mémoire reste valide tant qu'une instance la possède. En C++17, la construction du résultat temporaire est en outre généralement élidée. Même si le petit objet `cv::Mat`/`QString`/`QColorSpace` est déplacé ou copié, cela ne signifie pas qu'on recopie toute l'image pixel par pixel.

Dans cette fonction, `rgbaOutput` est un nouveau résultat produit par `cv::merge()` ; le `WorkingImage` retourné en conserve la propriété de manière sûre. La représentation par valeur ne dispense toutefois pas de tenir compte du partage de stockage de `cv::Mat` : modifier une autre copie superficielle du même tampon peut affecter les pixels partagés. Ici, le résultat est construit séparément de la source avant d'être rendu au code appelant.

### Fonctionnement avec les tâches asynchrones

Le rendu d'aperçu copie `m_original` et `Settings` avant de lancer le travail avec `QtConcurrent`. Copier `WorkingImage` transmet les métadonnées par valeur et copie le handle `cv::Mat` avec son comptage de références ; le tampon source reste vivant pendant le travail asynchrone. La fonction de rendu lit la source et produit un nouveau tampon : elle ne modifie donc pas `m_original`.

Ce modèle évite d'exposer un pointeur brut entre le thread UI, le worker et le résultat. Il est simple à composer et rend les durées de vie plus sûres. Il ne rend pas automatiquement les écritures concurrentes sur une même matrice sûres : la règle reste de ne pas modifier simultanément un tampon `cv::Mat` partagé.

### Valeur vide comme échec

`WorkingImage` dispose de `isNull()`. Un retour vide (`{}`) permet d'exprimer l'absence de résultat sans valeur de pixel fictive, et les appelants contrôlent explicitement cet état. Pour un traitement futur qui exige de différencier plusieurs classes d'erreurs, un type résultat dédié (valeur + erreur) pourrait rendre le contrat encore plus explicite ; un pointeur brut ne fournirait pas à lui seul cette information.

## Persistance `.ipr` et application par lot

Le sidecar courant v6, lu et écrit par le module `Sidecar` (données dans `EditRecipe.h`), est un flux `QDataStream` binaire placé à côté du fichier source sous le nom `source.ext.ipr`. Son en-tête contient la signature `IPR1`, la version du format, la taille et la date de modification du source. Cette signature empêche d'appliquer silencieusement des réglages devenus incohérents après le remplacement d'une image.

La v5 ajoute DCP, manifeste, leurs SHA-256, activation de normalisation et version du moteur/LibRaw ; la v6 ajoute identité/SHA-256 du LCP, résolution explicite et activations objectif/CA latérale. V1 à v6 restent lues. Les options RAW et la WB automatique sont restaurées avant le décodage. Le LCP résolu est figé, et les corrections sont suspendues si ses octets sont absents/modifiés. Le changement DCP utilise un unique tampon caméra actif, plafonné à 384 Mio, lorsque WB/calibration et options/empreinte optiques le permettent ; sinon un redécodage conserve le rendu visible. Voir [ADR-0006](decisions/ADR-0006-restauration-raw-et-retouche-profil.md) et le [contrat optique](camera-profile-engine.md#persistance-et-commandes-optiques-9-octobre-2026).

L'écriture est différée de 450 ms puis atomique via `QSaveFile`. Le payload stocke les réglages du rendu, les courbes, les annotations, le mélangeur huit bandes, le grading (trois zones et roue globale) et, depuis la version 9, les points de la couleur du point (source en TSV ProPhoto linéaire comme Lightroom, changements, variance, gamme). À l'ouverture, un sidecar valide remplace les réglages neutres et synchronise l'interface QML. Le ruban peut appliquer les réglages courants à plusieurs images : seules leurs sidecars sont écrites, jamais les sources, et les annotations existantes de la cible sont conservées.

Les v1/v2/v3 sont migrées : l'ancien dernier triplet est magenta (7), jamais
violet (6). Les nouvelles bandes restent neutres. Le calcul sélectif est isolé
dans `ColorMixer::apply`, après saturation/vibrance globales et avant grading :
masques triangulaires sur H source, protection smoothstep de S=0 à 0,05,
commandes cumulées, normalisation à Y source et gain de luminance borné ±0,5 EV.
Il n'écrête pas le RGB flottant ; les canaux négatifs sont laissés intacts.
Le rendu tous réglages HSL à zéro reste inchangé, mais les anciens réglages
non nuls peuvent produire un autre rendu. Détails, constantes et compatibilité :
[ADR-0005](decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md).

## Limites et points de vigilance

- Les opérations colorées sont des ajustements empiriques du RGB de travail ; la température ne constitue pas à elle seule une balance des blancs RAW physiquement complète.
- Les réglages tonals dépendent des statistiques mesurées à l'ouverture : une même valeur de curseur agit différemment selon l'image, comme dans Lightroom. L'ordre des modules fait partie du comportement visible de l'éditeur.
- Le clipping n'est pas réalisé dans `renderWorkingImage()` ; il intervient lors de la quantification/conversion de sortie. L'image flottante peut donc conserver des valeurs hors de `[0, 1]`.
- Le retour par valeur sécurise la propriété de l'objet résultat, mais `cv::Mat` copie ses données de façon superficielle lors d'une copie ordinaire. Il faut utiliser `clone()` si une copie indépendante des pixels est nécessaire.
- Vérifier les valeurs par défaut des réglages lors de leur modification : la température neutre définie dans la fonction couleur doit coïncider avec la valeur initiale et la valeur de réinitialisation de `Settings` et du QML.
