*[Français](denoising-sharpening.md) | [English](denoising-sharpening_en.md)*

# Réduction de bruit et netteté

**État au 23 octobre 2026 :** premier prototype affine adaptatif intégré sur
`patch_noise`, bouton d'estimation et réduction globale. Validation synthétique
et fonctionnelle locale ; appréciation photographique utilisateur en attente.
La netteté n'est pas modifiée. Les campagnes ci-dessous du 4/5 octobre sont
historiques et ne valident pas le rendu de ce prototype.

La [campagne photographique du 4 octobre](../validation/analyses/analyse-bruit-2026-10-04.md)
mesure 80 variantes retenues et dix neutres. Elle confirme l'activité des réglages mais relève
des plateaux à faible bruit et un bruit couleur résiduel important à haut ISO.
La sonde CPU identifie un seuil d'activation non nul, une protection de structure
pilotée par le bruit et un biais sombre possible à la reconstruction RGB.
Les mesures du 4 octobre restent une référence de l'ancienne version.
Les [corrections désormais appliquées](../validation/analyses/correctif-bruit-2026-10-05.md)
sont testées synthétiquement. La [revalidation v2](../validation/analyses/analyse-bruit-v2-2026-10-05.md)
couvre douze scènes, 24 neutres et 145 variantes : les 24 séries IPR standards
sont progressives, et le biais sombre diminue. Le bruit couleur à haut ISO
reste modérément réduit ; la qualité globale, le couplage des composantes et
l'aperçu/export restent à valider. Aucun recalibrage supplémentaire ni
changement de netteté n'a été appliqué pendant cette analyse.
Le [protocole](../validation/protocols/protocole-validation-bruit-2026-10-04.md)
distingue mesures au neutre, séries diagnostiques et comparaison IA séparée.

## Décision

IPR ne tente pas d'imiter un modèle propriétaire de Lightroom ou DxO. La première
version utilise un débruitage multi-échelle en espace RGB linéaire, avec séparation
luminance/chrominance, puis une netteté à masque de contraste et limitation des halos.
Le chemin est déterministe, fonctionne sous Windows, Linux et macOS sans pilote GPU
particulier, et ne modifie pas l'image lorsque les curseurs sont à zéro.

Les réglages sont :

- **Estimer le bruit** : analyse asynchrone de l'entrée du débruitage sur l'aperçu ;
- **Réduction globale** : 0–100, coordonne les deux composantes sans modifier leur
  ratio de seuils ; apparaît après estimation ou restauration d'une intensité non nulle ;
- **Bruit de luminance** : atténue le grain monochrome, avec protection des contours ;
- **Bruit de couleur** : atténue les taches colorées dans deux axes de chrominance ;
- **Gain de netteté**, **rayon** et **masquage** : accentuent les détails à contraste
  suffisant sans renforcer systématiquement le bruit.

Les deux intensités sont conservées dans le sidecar courant `.ipr` v6.
Le global les écrit ensemble dans une seule modification, sans nouveau champ.
Les réglages séparés restent disponibles en accès avancé. Ni les coefficients
estimés ni les pixels d'analyse ne sont enregistrés dans le sidecar. Le prototype
change le rendu des intensités non nulles ; zéro reste neutre. L'identité moteur
porte `noise-model=1`. Aucun sidecar utilisateur n'est réécrit par le build.

## Modèle mathématique mis en œuvre

Le débruitage utilise la luminance du master linéaire Rec.2020 D65 et les deux axes
Co/Cg. Ce n'est plus exactement la transformée YCoCg classique :

\[
Y=0{,}2627R+0{,}6780G+0{,}0593B,\quad Co=R-B,\quad
Cg=G-\tfrac12(R+B).
\]

Chaque plan est séparé par deux flous gaussiens successifs. Les résidus fin et moyen
sont contractés par seuillage doux :

\[
S_\lambda(d)=\operatorname{sgn}(d)\max(|d|-\lambda,0).
\]

Les sigmas gaussiens sont 0,8 puis 1,8 pixels, le second flou appliqué au
premier résultat. Dans le prototype, les bruits Y/Co/Cg sont estimés séparément par le MAD du
détail diagonal Haar `(p00-p01-p10+p11)/2`, divisé par 0,67448975.
L'échantillonnage conserve les quatre pixels adjacents de chaque bloc ; les
blocs parfaitement constants sont ignorés pour qu'un grand bord noir ne
désactive pas le traitement du reste de l'image. La distribution après
dématriçage étant corrélée, cette estimation n'est pas un modèle exact du capteur.

`NoiseEstimation` échantillonne au plus une grille de 24×24 tuiles natives de
16×16 pixels : ce sont des limites de coût, pas une taille de cellule capteur.
Les tuiles à variance exactement nulle sont exclues de l'ajustement des autres
zones ; une image entièrement constante reste à variance zéro. Les points sont
triés par signal Y, répartis en groupes d'environ un huitième du corpus
(au moins quatre points par groupe), puis la moitié la moins structurée de
chaque groupe est retenue. La structure utilise les directions Haar horizontale
et verticale ; elle ne donne pas une probabilité de bruit.

Chaque plan ajuste `v = a×max(Y,0)+b`, avec `a,b≥0`, par moindres carrés en
erreur relative de variance. Le signal explicatif est Y pour les trois plans,
pas la chrominance signée. Avec moins de huit points sélectionnés, une plage
de signal trop faible, un système mal conditionné ou une erreur relative moyenne
d'ajustement supérieure à 0,5, le modèle devient constant : médiane des variances
retenues. Ce seuil et ce repli sont expérimentaux, pas une certification.

Le sigma local est `sqrt(a×Y_guide+b)`, borné à la plage de signal observée,
où `Y_guide` est le guide lissé déjà utilisé par le filtre. Les seuils ne dépendent
donc pas directement de la valeur potentiellement aberrante du pixel seul.
Un cache FIFO de huit résultats, protégé par mutex et indexé par SHA-256 des
échantillons et de la géométrie, conserve seulement les statistiques. Une
variation des pixels échantillonnés invalide sa clé ; aucun tampon image n'est
conservé par ce cache.

Pour `s=clamp(valeur,0,100)/100`, les seuils sont
`lambda_fin = sigma_local*s*m`, avec `m=2,55` pour Y et `m=3,15` pour Co/Cg,
puis `lambda_moyen=0,52*lambda_fin`. Aucun seuil 0,35*sigma à l'activation.
Le résultat corrigé est aussi mélangé à l'original par `s` pour garder une
progressivité lorsque le seuillage sature. Le traitement à zéro est court-circuité
avant toute transformation, bit à bit identique ; 1/5/10 ont un effet volontairement doux.

Le masque de contours vient d'un guide gaussien à sigma 0,8, pas du signal
bruité directement. Le Sobel est normalisé par 1/8 ; son amplitude traverse
un smoothstep de 0,5*sigma à 2*sigma. Pour Co/Cg, le maximum du guide Y et
du guide propre du plan protège aussi les frontières de couleur isoluminantes.
La réduction du seuil par la protection reste plafonnée à 86 % pour Y et
72 % pour Co/Cg. Les cartes ne partagent pas de tampon de sortie mutable.

Le seuillage peut changer la moyenne. Les changements positifs/négatifs sont
rééquilibrés : si leurs sommes en amplitude sont P/N, multiplier les positifs
par `min(1,N/P)` et les négatifs par `min(1,P/N)`. Les cas de somme nulle
sont traités explicitement. Cela préserve la composante continue du plan,
sans ajouter un offset aux noirs inchangés, ni renormaliser l'exposition.
Les résultats Y candidats sont non négatifs ; Co/Cg restent signés.

La reconstruction est `RGB=Y+d`. Avec les poids de luminance `wR,wG,wB` et
`k=wR+wG/2`, on a
`d=((1-k)Co-wG×Cg ; (0,5-k)Co+(1-wG)Cg ; -k×Co-wG×Cg)`.
Si un canal serait négatif, réduire `d` par `t=min(1,Y/(-min(d)))`.
Ce rapprochement du gris garde Y constant ; aucun clamp supérieur SDR
n'est introduit. Le réglage couleur seul conserve ainsi la luminance linéaire
par pixel à la précision flottante. Les moyennes locales et le rendu de sortie
doivent néanmoins être revalidés sur les TIFF.

Les coefficients d'intensité/protection/échelle sont des choix empiriques,
pas des constantes physiques ni une calibration Lightroom. `1e-7` n'est
qu'un garde-fou numérique pour un plan sans bruit mesurable. Le plancher
historique `0,00025` reste uniquement dans l'estimateur de la netteté,
inchangé pour isoler les deux recettes.

## Contrat du bouton et limites du prototype

`estimateImageNoise()` analyse sur worker le signal après WB, exposition,
lumières et couleur, mais avant le débruitage et les effets de détail. La mémoire
transitoire est réservée au contrôleur existant puis libérée par RAII. Le bouton
n'applique aucune correction ; `setDenoiseGlobal()` écrit les deux intensités
et programme un rendu unique. Les résultats périmés après navigation ou édition
amont sont ignorés ; la fermeture annule l'attente mémoire et rejoint le worker.

L'aperçu et l'export sont estimés séparément à leur résolution native respective,
sans réduire l'image dans l'estimateur. Le modèle de l'aperçu ne calibre pas celui
du master : le redimensionnement a changé le bruit. Le résultat du bouton décrit
l'aperçu. Les intensités sont restaurées et copiées/collées par les recettes
existantes, mais chaque cible est estimée sur ses propres pixels.

Les coefficients RAW de l'étude du 22 octobre ne sont pas injectés. Ce premier
prototype ne contient ni détecteur probabiliste de pixels, ni fusion des cinq
voisinages, ni nouvelle IA. Textures fines, bruit corrélé, hauts ISO, cohérence
aperçu/export et rendu photographique restent à qualifier avant livraison.

La netteté est un masque flou adaptatif sur la luminance :

\[
Y'=Y+a\,(Y-G_\rho(Y))\,M,
\]

où \(G_\rho\) est un flou gaussien de rayon \(\rho\), \(a\) le gain et \(M\) un
masque qui s'ouvre seulement au-dessus d'un seuil relatif au bruit. Le résultat est
borné près du minimum et maximum local : cela limite les halos clair/foncé.

## Pourquoi ce choix plutôt que les solutions de référence

| Approche | Qualité potentielle | Coût / risque | Décision IPR |
| --- | --- | --- | --- |
| Bilatéral ou gaussien simple | Rapide, mais lisse les contours ou crée des aplats | Faible | Insuffisant seul. |
| NLM / BM3D | Très bon débruitage classique, préserve des motifs répétés | Calcul coûteux ; BM3D absent d'OpenCV de base, licences et implémentations à auditer | Candidat qualité pour une option export non temps réel. |
| IA RAW (Lightroom, DeepPRIME) | Meilleur résultat sur bruit très fort | Modèle, corpus RAW propre, GPU, droits et validation multi-boîtiers requis | Hors périmètre de la première version. |
| Multi-échelle avec masque de structure | Bon compromis détail, prévisibilité et portabilité | CPU seulement, moins performant qu'une IA à très haut ISO | Choisi pour l'aperçu et l'export actuels. |
| Déconvolution Richardson--Lucy | Corrige un flou connu | Sensible au PSF erroné, bruit et halos | À proposer plus tard en mode expert. |

RawTherapee applique la netteté de capture après dématriçage, en gamma linéaire, et
utilise un seuil de contraste afin de ne pas accentuer le bruit. Il propose également
un masque flou et une déconvolution Richardson--Lucy. Lightroom expose un gain, rayon,
détail et masquage, et recommande de réduire le bruit avant les outils de détail. Ces
principes ont directement guidé l'ordre du pipeline IPR : débruitage, outils de
contraste local, puis netteté masquée.

DxO et Lightroom emploient des modèles IA lourds directement sur les données RAW ;
leur qualité ne vient pas d'un unique filtre matriciel mais d'un entraînement massif et
d'une exécution GPU. Une réimplémentation sans données d'apprentissage serait
visuellement et juridiquement risquée.

## GPU : décision différée

Les opérations ajoutées sont principalement des convolutions, gradients et passes
pixel-à-pixel, donc parallélisables. Toutefois, le pipeline d'IPR conserve actuellement
les pixels dans `cv::Mat` sur CPU. Un passage OpenCL/CUDA/Vulkan immédiat ajouterait des
copies hôte--GPU et un second chemin de calcul à comparer, alors que le proxy interactif
est limité à 2048 px. La référence CPU sert donc de vérité de non-régression.

Avant une accélération, mesurer sur le matériel cible : temps de transfert, temps de
rendu du proxy, export pleine définition et écart numérique CPU/GPU. Une future
implémentation devra garder le repli CPU et être facultative à la configuration CMake.

## Validation attendue

1. Un RAW faible ISO, un RAW ISO élevé avec aplats sombres, une scène avec peau et une
   scène à feuillage/texture fine ; export TIFF sRGB 16 bits neutre, bruit seul et
   bruit + netteté.
2. Vérifier à 100 % : baisse du bruit dans les aplats, conservation des bords, absence
   de saignement de couleur, de texture cireuse et de halos.
3. Mesurer bruit résiduel, énergie de gradient, chrominance des gris, clipping et MAE
   par rapport au neutre. Comparer les mêmes paramètres à Lightroom comme repère, sans
   exiger une parité pixel à pixel.
4. Vérifier les valeurs `0`, les extrêmes, la réouverture du sidecar et l'export pleine
   définition.

## Sources

- [RawTherapee — Capture Sharpening](https://rawpedia.rawtherapee.com/Capture_Sharpening)
  : traitement linéaire après dématriçage et seuil de contraste.
- [RawTherapee — Sharpening](https://rawpedia.rawtherapee.com/Sharpening) : masque flou,
  contrôle des halos et Richardson--Lucy.
- [RawTherapee — About Noise Reduction](https://rawpedia.rawtherapee.com/About_Noise_Reduction)
  : débruitage par ondelettes, DCT et masques de structure.
- [Adobe Lightroom Classic — Detail](https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/retouch-photos.html)
  : rayon, détail et masquage de la netteté.
- [Adobe Lightroom Classic — Denoise](https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/enhance-details.html)
  : débruitage IA sur RAW et coût GPU.
- [DxO — comparaison HQ, PRIME et DeepPRIME](https://support.dxo.com/hc/en-us/articles/4432857457041-Which-of-the-different-DxO-denoising-technologies-should-I-use)
  : compromis qualité, coût et GPU.
- [Buades, Coll, Morel — Non-Local Means Denoising](https://doi.org/10.5201/ipol.2011.bcm_nlm)
  : fondement du débruitage non local.
