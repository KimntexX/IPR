# Réduction de bruit — analyse photographique et diagnostic du moteur

**Date :** 4 octobre 2026. **Décision :** activité des réglages confirmée sur les
séries exploitables ; calibration et recette complète **non validées**.
Le moteur n'est pas modifié par cette analyse. Aucun RAW, TIFF, sidecar ou INI
utilisateur n'a été déplacé, corrigé ou écrasé.

**Suite au 5 octobre :** les [correctifs CPU](correctif-bruit-2026-10-05.md)
sont maintenant appliqués et testés automatiquement. Les mesures ci-dessous
restent celles **avant correction** ; elles ne valident pas le nouveau rendu.
La [campagne v2](../protocols/protocole-bruit-v2.md) doit encore être exécutée.

[Protocole](../protocols/protocole-validation-bruit-2026-10-04.md) ·
[Architecture](../../architecture/denoising-sharpening.md).

## 1. Conclusion opérationnelle

Les deux commandes IPR ont un effet mesurable. Le réglage de luminance conserve
bien les contours examinés à ISO faible/modéré, mais sa réponse peut plafonner
très tôt. À très haut ISO, la chrominance résiduelle reste importante : la
scène 9890 à ISO 25 600 conserve environ **81 % du proxy de bruit couleur**
de son neutre, au réglage 100. Les crops montrent encore des taches colorées.

Le diagnostic synthétique identifie trois pistes prioritaires dans le code :

1. rendre le seuillage continu à l'approche de zéro et revoir le plancher de bruit ;
2. empêcher le masque de contours de protéger le bruit comme un détail réel ;
3. contrôler la dérive lumineuse lors du seuillage et de la reconstruction RGB.

Ces recommandations ne signifient pas qu'il faut copier les valeurs Lightroom,
changer immédiatement d'algorithme ou intégrer une IA/GPU. Commencer par corriger
et mesurer la référence CPU actuelle. L'efficacité absolue, les halos, la
conservation de toutes les textures et la cohérence aperçu/export ne sont pas
encore validés. La netteté n'est pas testée dans cette campagne.

## 2. Corpus réellement reçu et audit

Quatre niveaux par composante : **25, 50, 75, 100**, pour luminance seule puis
couleur seule. Ce n'est pas la convention −50/−25/+25/+50 du mélangeur.

| Scène | ISO EXIF | Sujet observé | IPR | Lightroom retenu |
| --- | ---: | --- | --- | --- |
| 6V1A0043 | 100 | Fleur, fond flou | Deux séries avec neutre | Deux séries conformes |
| 6V1A0112 | 64 000 | Lune/ciel nocturne très bruité | Deux séries avec neutre explicitement associé | Deux séries conformes |
| 6V1A0327 | 100 | Escalade extérieure, rocher | Deux séries avec neutre | Deux séries conformes |
| 6V1A0624 | 400 | Chien noir, pelage et sangle | Deux séries avec neutre | Deux séries conformes après correction du dernier TIFF |
| 6V1A9890 | 25 600 | Escalade intérieure, mur et vêtement | Deux séries avec neutre | Deux séries conformes après mapping explicite des réexports |

Les métadonnées indiquent « Canon EOS R6 Mark III » ; ce libellé est transcrit
du corpus, pas vérifié comme identification indépendante du matériel.

Le dossier bruit contient **84 TIFF** : 40 IPR et 44 Lightroom. Quatre Lightroom
9890 sont d'anciens exports doublons à netteté 40 ; leurs réexports propres
portent les suffixes −5 à −8. La campagne retenue comporte donc **80 variantes
distinctes**, dix neutres disponibles et aucun état manquant.

Après sélection des bons réexports, **80 variantes** satisfont l'audit de
campagne : 40 IPR avec leur neutre déclaré et 40 Lightroom avec neutre conforme.
Les vingt séries attendues sont complètes. L'audit des fichiers n'est pas
une validation de la qualité photographique de l'algorithme.

### Compléments reçus pendant l'analyse

- Neutre IPR de 0112 reçu sous `tests/image/base/neutre/6V1A0112-IPR-5.tiff` :
  référence opérateur explicitement associée avec `--neutral-file`.
- Dans `bruit/luminance`, les quatre IPR de 9890 reçus :
  `6V1A9890-IPR.tiff`, `6V1A9890-IPR-2.tiff`, `6V1A9890-IPR-3.tiff`,
  `6V1A9890-IPR-4.tiff`, aux valeurs 25/50/75/100.
- Lightroom `bruit/luminance/6V1A0624-4.tif` réexporté : luminance 100,
  **couleur remise à 0**, XMP vérifié. La version précédente activait couleur 25.

Les Lightroom 9890 −5/−6/−7/−8 indiquent bien luminance 25/50/75/100,
couleur 0, netteté 0. Ils sont explicitement associés aux quatre états,
sans renommer ni supprimer les fichiers de l'utilisateur.

### Autres contrôles

Les TIFF retenus sont sRGB 16 bits SDR, orientation déjà appliquée.
Les dimensions IPR et Lightroom diffèrent d'un pixel par axe ; aucune
superposition interlogiciels n'est imposée. La géométrie est constante
à l'intérieur de chaque série mesurée.

Les paramètres secondaires Lightroom sont constants dans les états valides :
détail luminance 50, contraste 0 ; détail couleur 50 et lissage 50 lorsque
les contrôles couleur sont actifs. Les autres paramètres XMP sont comparés
au neutre. Aucun indicateur IA actif n'est détecté dans ces exports ; l'absence
de balise ne prouve pas l'absence d'un traitement amont. Aucune série IA
séparée n'a été fournie. Les valeurs IPR sont déclarées par l'opérateur :
leurs TIFF n'offrent pas une preuve XMP équivalente des réglages appliqués.

## 3. Méthode et portée des nombres

Lecture de **pixels natifs adjacents**, sans passage en 8 bits pour les mesures.
Chaque référence fournit six régions peu structurées et quatre régions de
détail de 256×256 pixels, coordonnées figées pour ses quatre variantes.
Les coordonnées et toutes les statistiques par région sont dans les JSON.

Après décodage sRGB, le proxy haute fréquence est le MAD du coefficient
diagonal Haar normalisé :

`HH = (p00 − p01 − p10 + p11)/2`,
`sigma = median(abs(HH − median(HH)))/0,67448975`.

Y est la luminance linéaire Rec.709 ; le proxy couleur combine Co et Cg
de YCoCg. Un ratio de 0,80 signifie 80 % du proxy initial restant,
**pas 80 % de bruit capteur ni 20 % de gain de qualité perceptuelle**.
Un proxy faible peut aussi provenir de l'effacement d'une texture,
d'un changement de corrélation ou d'un assombrissement.

Les tableaux sont des médianes sur six régions, sélectionnées séparément
par logiciel. Ils permettent des tendances internes, pas un classement
quantitatif sur des zones strictement équivalentes IPR/Lightroom. Ni MAE
interlogiciels, ni PSNR de reconstruction : aucune vérité sans bruit n'existe.
Le protocole précise les filtres, critères et limites.

## 4. Résultats IPR

Ratios au **neutre propre**, pour les dix séries IPR complètes.
Les valeurs sont arrondies à trois décimales ; un plateau arrondi n'implique
pas l'identité des pixels ou un blocage de l'interface.

| Scène et composante mesurée | 25 | 50 | 75 | 100 | Référence |
| --- | ---: | ---: | ---: | ---: | --- |
| 0043 — σY, luminance | 0,748 | 0,748 | 0,746 | 0,746 | 0 |
| 0043 — σC, couleur | 0,447 | 0,447 | 0,447 | 0,447 | 0 |
| 0327 — σY, luminance | 0,910 | 0,876 | 0,835 | 0,808 | 0 |
| 0327 — σC, couleur | 0,989 | 0,981 | 0,841 | 0,677 | 0 |
| 0624 — σY, luminance | 0,927 | 0,890 | 0,860 | 0,832 | 0 |
| 0624 — σC, couleur | 0,889 | 0,816 | 0,808 | 0,802 | 0 |
| 9890 — σY, luminance | 0,946 | 0,919 | 0,893 | 0,867 | 0 |
| 9890 — σC, couleur | 0,919 | 0,880 | 0,843 | 0,806 | 0 |
| 0112 — σY, luminance | 0,919 | 0,897 | 0,866 | 0,830 | 0 |
| 0112 — σC, couleur | 0,866 | 0,794 | 0,722 | 0,660 | 0 |

### Interprétation

- **0043 :** la fleur a très peu de bruit. Une grande partie du changement
  arrive avant ou à 25, puis le proxy varie peu ; ce n'est pas une bonne scène
  pour calibrer seule l'extrémité forte du curseur.
- **0327/0624 :** progression de luminance mesurée. Les ratios de gradient
  de contour à 100 sont respectivement 0,995 et 0,987. Les crops du pelage
  0624 conservent visuellement les poils examinés, avec lissage modéré des
  aplats ; cela ne valide pas toutes les textures ni tous les halos.
- **9890 :** le bruit couleur baisse d'environ 8 % à 25 à 19 % à 100,
  et le proxy de luminance baisse d'environ 13 % à 100. Les crops à un pixel affiché par pixel natif montrent
  beaucoup de bruit résiduel, notamment dans le vêtement et sur le mur.
- **0112 :** le ciel permet d'étudier le bruit, pas de prétendre valider
  la texture par ses gradients. Avec le neutre reçu, le proxy de luminance
  baisse d'environ 17 % à 100 et le proxy couleur d'environ 34 %.
  La moyenne Y monte d'environ **2,67 %** dans les aplats de la série
  luminance à 100 par rapport au zéro, et de 0,38 % dans la série couleur.
  Cette dérive confirme la nécessité de contrôler les tons très sombres.

Sur les huit séries IPR hors 0112, les médianes de dérive Y sont très
faibles (moins de 0,03 % en valeur absolue). Cela ne garantit pas l'absence
de dérive locale ; les mesures par région sont conservées.

## 5. Lightroom classique : repère, pas vérité absolue

| Scène et composante mesurée | 25 | 50 | 75 | 100 |
| --- | ---: | ---: | ---: | ---: |
| 0043 — σY, luminance | 0,623 | 0,535 | 0,473 | 0,461 |
| 0043 — σC, couleur | 0,452 | 0,454 | 0,459 | 0,457 |
| 0112 — σY, luminance | 0,505 | 0,374 | 0,257 | 0,215 |
| 0112 — σC, couleur | 0,043 | 0,043 | 0,043 | 0,043 |
| 0327 — σY, luminance | 0,584 | 0,452 | 0,333 | 0,238 |
| 0327 — σC, couleur | 0,337 | 0,334 | 0,332 | 0,331 |
| 0624 — σY, luminance | 0,532 | 0,383 | 0,249 | 0,167 |
| 0624 — σC, couleur | 0,172 | 0,170 | 0,171 | 0,169 |
| 9890 — σY, luminance, réexports −5 à −8 | 0,560 | 0,414 | 0,282 | 0,192 |
| 9890 — σC, couleur | 0,183 | 0,184 | 0,183 | 0,183 |

Les tendances et les crops montrent un débruitage couleur Lightroom beaucoup
plus marqué sur 9890, dès 25. Ce constat porte sur ce corpus, ces rendus
et ces zones ; les valeurs numériques des curseurs ne sont pas des unités
physiques communes. La baisse forte des gradients peut enlever du bruit
autant que des détails : aucune perte de texture absolue n'est déduite
du seul ratio de gradient.

Attention à **0112** : la luminance moyenne des aplats baisse d'environ
39 à 61 % dans les variantes Lightroom de luminance, et d'environ 41 %
dans celles de couleur. Les contrôles XMP audités restent isolés, mais
l'assombrissement contribue à la baisse du proxy linéaire. Le chiffre de
0,215 ne doit donc pas être interprété seul comme une réduction de bruit
à luminosité égale. Il faut des crops et une analyse locale tenant compte
du niveau de signal avant une comparaison qualitative définitive.

Le réexport corrigé Lightroom 0624 luminance 100 donne un ratio de **0,167**.
L'ancienne version (~0,018) activait également couleur 25 : historique
conservé dans la mesure v2, mais remplacé et exclu des conclusions finales.

## 6. Diagnostic reproductible des constantes du moteur

Le code étudié est [`DetailProcessing.cpp`](../../../src/core/DetailProcessing.cpp),
SHA-256 `1af77486a369b6c265d93bae97c9c24f313a4b26f7917fe67d512e59165c3048`.
Les versions exactes des exécutables ayant produit les TIFF ne sont pas
encodées dans ces fichiers : ce hash identifie le **code audité**, pas une
preuve de provenance de chacun des exports.

[Source de la sonde](../raw/sonde-bruit-2026-10-04.cpp) et
[sortie brute](../raw/sonde-bruit-2026-10-04.txt). Compilation et exécution
WSL/OpenCV réussies, sans changement du moteur. Trois aplats synthétiques,
graine fixe 812, 512×512 pixels ; niveaux 1/5/10/25/50/75/100.

Cette sonde historique inclut le fichier moteur à la compilation. Pour reproduire
ses anciens chiffres il faut le code au hash ci-dessus, pas le moteur corrigé
actuel. La nouvelle référence synthétique est `detailProcessingTests`.

### A. Seuil non nul dès l'activation

Le chemin à 0 ne filtre pas. Dès que la valeur est positive :

`lambda_fin = sigma * (0,35 + multiplicateur * valeur/100)`.

Le multiplicateur est 2,2 en luminance et 2,8 en couleur ; le seuil moyen
vaut 0,52 fois le seuil fin. La limite du seuil lorsque la valeur tend
vers zéro reste donc **0,35 sigma**, au lieu de zéro.

Le plancher `kNoiseFloor=0,00025` domine aussi une scène synthétique
à σ≈0,0001. À valeur 1, son proxy résiduel tombe déjà à **0,153** ;
à 5, à **0,031** ; à 25–100, à environ **0,009**. Cela démontre une
activation brutale et un plateau sur cet aplat de faible bruit.

**Recommandation :** seuil nul à force nulle, calibration séparée des
échelles et du plancher. Tester aussi une interpolation continue entre
l'image originale et filtrée ; ne pas choisir ses constantes uniquement
pour faire correspondre un seul TIFF Lightroom.

### B. Le bruit est protégé comme un contour

Le Sobel qui pilote la protection travaille directement sur la luminance
bruitée. Sur un aplat sans texture à σ=0,005, **70,25 %** des pixels ont
un poids de protection au moins 0,5 et **43,94 %** au moins 0,99.
Ce sont des structures dues au bruit, pas des bords à conserver.

**Recommandation :** guide de structure préfiltré ou gradient normalisé
par le bruit attendu, puis comparaison sur véritables bords et textures.
Ne pas simplement supprimer toute protection pour augmenter la baisse
de σ : cela risquerait d'effacer les poils et motifs fins.

### C. Relèvement possible des tons très sombres

La reconstruction YCoCg borne séparément les composantes RGB négatives à
zéro. Sur l'aplat sombre à bruit couleur de la sonde, la moyenne YCoCg
monte de **1,89 %** à luminance 100, contre **0,71 %** avant ce bornage.
Le seuillage est lui aussi contributeur : le clamp n'explique pas tout.

**Recommandation :** mesurer le biais sombre avant/après reconstruction,
étudier une contrainte de gamut préservant mieux la luminance, sans supprimer
aveuglément les protections de validité numérique. La sonde démontre un
mécanisme possible ; elle n'attribue pas à elle seule la totalité de la
dérive photographique de 0112.

### D. Coût, IA et GPU

Cette campagne ne chronomètre pas le rendu et ne mesure aucune accélération
GPU. Corriger le masque, les seuils et la reconstruction ne nécessite pas
immédiatement une IA. Si la qualité reste insuffisante à haut ISO après
calibration, comparer une option classique plus élaborée sur les mêmes
neutres et ROI, avec licence, coût CPU et préservation des détails audités.
Une IA nécessite une campagne et un budget propres, pas une équivalence
supposée à partir d'une seule valeur de curseur Lightroom.

## 7. Traçabilité et reproduction

- [Inventaire v5](../raw/inventaire-bruit-2026-10-04-v5.json) : audit final
  des 80 variantes et dix neutres, mappings explicites ; **aucune anomalie**.
- [Mesure v4](../raw/mesure-bruit-2026-10-04-v4.json) : passe finale complète,
  **seule référence des résultats consolidés**.
- [Mesure v2](../raw/mesure-bruit-2026-10-04-v2.json) : 76 variantes mesurées,
  dont anciennes Lightroom 9890 de luminance à netteté 40 ; historique seulement.
- [Mesure 9890 v3](../raw/mesure-bruit-9890-2026-10-04-v3.json) : sélection
  explicite −5 à −8 avant réception des IPR luminance ; historique seulement.
- Les inventaires v1/v2/v3/v4 et mesure v1 restent des instantanés historiques
  pendant les remplacements. Ils ne définissent pas le corpus final ;
  notamment les anciennes étiquettes Lightroom 0327 couleur étaient erronées.

La mesure v4 emploie les métriques de l'analyseur version 2, dont le SHA-256 est enregistré
dans chaque JSON. Le lecteur conserve les vrais niveaux XMP, vérifie tailles
et dates avant/après lecture et à la fin de la campagne, et refuse d'écraser
un fichier de mesure existant. Les neutres disponibles ont leur SHA-256.

Reproduction complète avec les noms réellement reçus :

```bash
python3 tests/analyse_noise.py --analyse \
  --neutral-file 6V1A0112 IPR tests/image/base/neutre/6V1A0112-IPR-5.tiff \
  --variant-file 6V1A9890 luminance LR 1 tests/image/base/bruit/luminance/6V1A9890-5.tif \
  --variant-file 6V1A9890 luminance LR 2 tests/image/base/bruit/luminance/6V1A9890-6.tif \
  --variant-file 6V1A9890 luminance LR 3 tests/image/base/bruit/luminance/6V1A9890-7.tif \
  --variant-file 6V1A9890 luminance LR 4 tests/image/base/bruit/luminance/6V1A9890-8.tif \
  --output NOUVELLE-MESURE-BRUIT.json
```

Les aperçus des ROI et crops diagnostiques sont dans `build-wsl-libraw` :
images de consultation 8 bits uniquement, statistiques natives 16 bits.
Inspection à taille native effectuée pour le pelage 0624 et le bruit couleur
9890. Les autres scènes demandent encore une recette visuelle plus exhaustive.

**Tests ciblés :** 15 tests bruit, 13 tests du lecteur/comparateur partagé,
7 tests de sélection de build réussis. La modification du seul analyseur
bruit sélectionne sa seule suite Python, aucun CTest. Le changement de
cartographie `scripts/test-rules.json` reste transversal et entraînera une
suite complète au prochain build automatique ; aucun checkpoint n'a été
avancé par cette campagne. Aucune recompilation Windows n'était nécessaire
pour cette analyse sans changement de l'application.

## 8. Suite proposée

1. Ajouter des régressions CPU pour identité à 0, activation 1/5/10,
   progression, aplats, marches, fins motifs et biais sombre.
2. Appliquer les corrections de seuil/masque/reconstruction dans une action
   distincte ; conserver ce corpus comme référence de l'ancienne version.
3. Réexporter les séries corrigées et comparer aux mêmes neutres, puis valider
   à 100 % peau/pelage/feuillage, halos, frontières de couleur et aperçu/export.
4. Tester ensuite le couplage bruit + netteté, ainsi que restauration sidecar.

## Bibliographie

- D. L. Donoho et I. M. Johnstone, *Ideal spatial adaptation by wavelet shrinkage*,
  Biometrika 81(3), 1994, 425–455 : [DOI](https://doi.org/10.1093/biomet/81.3.425),
  [rapport Stanford](https://statistics.stanford.edu/technical-reports/ideal-spatial-adaptation-wavelet-shrinkage).
  Référence du seuillage ; la mesure Haar est un proxy, pas une implémentation
  complète de cet algorithme.
- [Adobe — high ISO et outils manuels/IA](https://blog.adobe.com/en/publish/2023/04/20/lowdown-using-high-iso-settings-in-photography)
  et [Enhance/Denoise](https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/enhance-details.html).
  Les hypothèses expérimentales sont détaillées dans le protocole.
- Sources d'implémentation comparées : bibliographie de
  [l'architecture bruit/netteté](../../architecture/denoising-sharpening.md).
  Cette passe mesure les exports fournis ; elle ne renouvelle pas un benchmark
  de RawTherapee, DxO, IA ou GPU.
