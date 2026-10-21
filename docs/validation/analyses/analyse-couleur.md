# Analyse évolutive — Couleur

Groupe : température, nuance, vibrance, saturation, niveaux de gris.
Protocole : [Couleur](../protocols/protocole-couleur.md).
Les campagnes ci-dessous vérifient aussi l'outillage ; elles ne remplacent
pas les validations photographiques historiques ni ne certifient la fidélité.

## Premier jet A1/A2 intégré en production locale — 13 octobre 2026

**Point d'arrêt du 14 octobre 2026 :** à la demande de l'utilisateur, l'état
A1/A2 et ses preuves sont sauvegardés sur `patch_color`. Les validations
visuelles et le réglage des coefficients Couleur sont suspendus : le cas
neutre 0081 montre un écart rouge/sombre à traiter d'abord dans le rendu
d'ouverture. Les tests mathématiques/fonctionnels restent utiles, mais
ne constituent pas une validation photographique. Une nouvelle branche
`patch_raw_opening`, issue de `dev`, porte cette investigation, sans fusion
du prototype Couleur dans `dev`, sans tag de validation et sans push implicite.
La future correction du rendu de base nécessitera de rejouer les comparaisons
Couleur avec des neutres compatibles ; les JSON actuels sont conservés.

À la demande de l'utilisateur, `Vibrance::apply()` remplace l'ancien bloc
RGB radial du rendu partagé par l'IHM et les exports sur `patch_color`.
L'implémentation reprend le candidat **A2 étudié, qui contient déjà A1** :
correction d'intensité aux extrémités −3,5 % / +6,5 %, pondération OKLCh,
proxy de tons chauds et saturation multiplicative en chroma Oklab.
Pas de sélecteur d'ancien modèle ni d'import du modèle ProPhoto B.

La clarté L et la teinte h sont conservées par l'opérateur avant sortie ;
Y n'est plus un invariant. Le zéro et les gris restent exactement inchangés.
Le gamut de sortie est volontairement inchangé pour comparer le candidat
étudié ; sa dérive de teinte positive connue n'est pas annoncée corrigée.
La protection chaude n'est pas une détection de peau. Les constantes
exploratoires sont conservées, sans nouvelle calibration silencieuse.

**Les courbes/JSON de l'étude et v003 restent des preuves historiques** :
« current » y désigne l'état mesuré avant cette intégration. Ils ne sont pas
réécrits et ne certifient pas les nouveaux exports de production. Dans la
sonde recompilée, « current » suit désormais la production A1/A2 ; le
comparateur refusera son rapprochement avec le témoin v003 devenu différent.
Il faudra une nouvelle campagne/neutres compatibles avant nouvelle mesure.

Tests natifs ciblés : **7/7 réussis** (mathématiques A1/A2, correspondance
avec le candidat étudié, export, générateur, gamut, mélangeur et neutralité).
L'ancien test de conservation de Y est remplacé par les invariants L,
zéro/gris, finitude HDR/RGB signés, intensité monotone et désaturation complète
à saturation −100. L'application Windows et le générateur sont recompilés.
Validation visuelle utilisateur encore attendue ; aucun tag, fusion ou push.

Pour voir le correctif : fermer l'ancien IPR, lancer
`build-windows-msvc/Release/appIPR.exe`, garder Camera Neutral, normalisation
RAW désactivée et mêmes réglages secondaires que les références LR. Tester
les commandes séparément à −100, −50, −25, 0, +25, +50, +100 puis ensemble.
Observer les peaux, jaunes/oranges/rouges, bleus, gris, ombres et aplats proches
du gamut. Le standalone déjà présent dans `dist` n'est pas mis à jour par
cette compilation. Aucun nouveau TIFF de corpus n'est généré ou supprimé.

### Référence As Shot du corpus complémentaire

Information communiquée par l'utilisateur le 13 octobre 2026 : pour
**7Y6A7599**, Lightroom affiche en balance des blancs **As Shot** une
température de **5 350 K** et une nuance de **+8**. Ces valeurs sont propres
à cette image, pas des défauts globaux ni un réglage manuel erroné.
Elles sont déclaratives, à confirmer dans les métadonnées des futurs exports
et à consigner dans leur prochaine mémoire JSON, sans réécrire les campagnes
antérieures. Ne pas appliquer à cette scène l'ancienne référence 5200/+5.

Complément communiqué par l'utilisateur : IPR affiche **5 314 K**, soit
**36 K de moins** que Lightroom. Au rendu neutre, les deux images lui
paraissent semblables, sans différence réellement marquante. Il s'agit
d'une appréciation visuelle favorable sur cette scène, pas d'une mesure
de fidélité colorimétrique ni d'une validation des réglages non neutres.
La nuance affichée dans IPR n'a pas été précisée.

Pour la fleur orange **6V1A0081**, l'inspection des nouveaux exports neutres
du 14 octobre relève un écart de teinte et de luminosité ; les mesures et
limites sont conservées uniquement dans le
[rapport Base colorimétrique, §28](analyse-base-colorimetrique.md#28-fleur-orange-6v1a0081--comparaison-neutre-du-14-octobre-2026).
Ne pas attribuer ce résidu au bloc A1/A2 lorsque ses deux curseurs sont à zéro.

## Étude comparative des modèles — 13 octobre 2026

### Objectif et conclusion

Le produit vise un comportement photographique cohérent, mathématiquement
explicable, et visuellement proche des usages de Lightroom, **pas sa copie**.
Deux études ont été exécutées : famille A (ajustement asymétrique puis modèle
perceptuel adaptatif), famille B (opérateurs de LightCraft PR #598).
Le rendu courant sert de témoin ; les candidats restent diagnostiques.
**Aucune nouvelle modification du code de production n'est livrée par cette étude.**

Recommandation : essayer **A1, correction douce des extrémités**, avant un
remplacement de modèle. A2 est une piste de fond pour préserver les teintes,
à associer à une gestion de gamut perceptuelle. B n'est pas recommandé tel
quel dans IPR : son couplage couleur/luminosité est trop important ici.
Cette recommandation n'est ni une validation visuelle ni un classement de
fidélité physique. Les coefficients A restent des hypothèses à valider.

### Preuves et méthode

- [Sonde native v001 — 14:01:15 UTC / 16:01:15 Paris](../raw/sonde-modeles-couleur-v001-20261013T140115169Z.json).
- [Comparaison v001 — 14:02:27 UTC / 16:02:27 Paris](../raw/comparaison-modeles-couleur-v001-20261013T140227537090Z.json).
- Référence LR et témoin : [campagne Couleur v003](../raw/mesure-couleur-v003-20261013T103940441925Z.json).
- [Courbes dérivées : chroma faible, fort et luminance](../metrics/curves/etude-modeles-couleur-v001.svg).

Les sept RAW 0007, 0037, 0043, 0327, 9421, 9890 et 9950 sont décodés avec
le chargeur courant, DCP Camera Neutral, sans normalisation RAW. La sonde
échantillonne les pixels natifs tous les huit pixels, **avant la sortie**,
puis applique les quatre modèles et la même conversion de production en
sRGB 16 bits : **392 états** (7 scènes × 4 modèles × 2 commandes × 7 valeurs).
Elle ne génère pas de TIFF et ne touche ni les images ni les profils.
Les JSON gardent les empreintes RAW/DCP/LCP, outils, exécutables et paramètres.

Le témoin retrouve les mesures v003 avec un écart absolu maximal de
**3,1653 × 10⁻¹³**. Les réponses LR sont reprises du JSON audité, pas recalculées
sur de nouveaux pixels. Les corrections optiques diffèrent entre moteurs ;
les masques de chroma sont propres à chaque moteur et varient avec les états.
On compare donc des réponses statistiques, **pas une MAE, un Delta E de
pixels superposés ou une couleur absolue de référence**. Aucun ajustement
automatique aux mesures LR, aucune recherche de paramètres n'a été effectué.

### Étude A : du changement minimal au modèle perceptuel

**A1 — extrémités RGB.** Avec `a = curseur/100`, on définit
`b = smoothstep(0,75 ; 1 ; |a|)` et :

```text
a_effectif = a × (1 − 0,035 b)     si a < 0
a_effectif = a × (1 + 0,065 b)     si a > 0
RGB' = Y + (RGB − Y) × max(0, (1 + saturation) × facteur_vibrance)
```

Les pourcentages concernent **l'intensité de commande**, non un pourcentage
garanti de chroma en sortie. La courbe ne change pas jusqu'à |a| = 0,75,
puis raccorde continûment les extrémités. Les états ±25 et ±50 restent ceux
de production ; la saturation n'est pas modifiée. On ne transpose donc pas
aveuglément la demande visuelle de +2–3 % de saturation : certains forts
chromas sont déjà plus renforcés qu'avec LR.

**A2 — OKLCh adaptatif.** Le RGB linéaire passe en Oklab puis en coordonnées
polaires `(L, C, h)`. La chroma relative exploratoire est
`q = clamp(C / max(|L|, 0,05), 0, 1)`. Le gain conserve `L` et `h` avant
sortie, et multiplie `C` par `max(0, (1+s)(1+a_effectif w))`.
Pour a positif, `w = (1−q)² × protection_ch chaude` ; pour a négatif,
`w = 1−0,1q`. La protection chaude est une gaussienne autour de 50° Oklab,
largeur 25°, amplitude 0,4, adoucie selon L et C. Ce n'est **pas un détecteur
de peau** : prises d'escalade, bois et objets chauds peuvent être protégés.
Ces constantes exploratoires ne sont ni physiques ni calibrées pour tous
les boîtiers. Les détails et matrices sont dans `tests/colour_model_study.h`.

Conserver la clarté perceptuelle Oklab `L` ne signifie pas conserver la
luminance photométrique `Y`. De même, préserver `h` avant sortie ne garantit
pas sa conservation après le traitement de gamut RGB de production.

### Étude B : transposition contrôlée de LightCraft PR #598

La [PR #598](https://github.com/storytold/lightcraft/pull/598) est ouverte,
non fusionnée lors de la consultation ; état étudié :
`b9e312a748fde0b0e5b0eeb070a69b7ecf2df59f`.
Elle propose vibrance HSV et saturation autour du gris en ProPhoto linéaire
D50, avec adaptation Bradford. Ses paramètres sont ajustés sur des rendus
Lightroom, **pas des constantes physiques**. Sa validation annoncée ne vaut
pas pour IPR. Nous réexprimons ses opérateurs, sans importer le logiciel ou
son pipeline complet. Le couplage saturation/valeur HSV peut modifier Y.
La branche principale utilise une autre approche OKLCh : ne pas confondre
le contenu d'une PR et le comportement livré.

Dans notre sonde, sRGB linéaire D65 → XYZ → Bradford D50 → ProPhoto linéaire,
puis transformation et trajet inverse. Aucune courbe gamma ProPhoto n'est
appliquée dans le calcul linéaire. Cette conversion ne rend pas les bases
IPR et LightCraft identiques. La gestion des masques et les combinaisons de
réglages ne sont pas évaluées ; la parité CPU/GPU annoncée n'est pas testée ici.

### Résultats descriptifs

Médianes sur sept scènes des variations relatives de chroma forte
(`C ≥ 0,5` au neutre), après la même sortie sRGB :

| Modèle | Vibrance −100 | Vibrance +100 | Variation Y médiane à −100 |
| --- | ---: | ---: | ---: |
| Production courante | −89,84 % | +19,29 % | −0,008 % |
| A1 extrémités RGB | −85,70 % | +20,48 % | −0,008 % |
| A2 OKLCh adaptatif | −92,15 % | +31,84 % | −0,067 % |
| B ProPhoto / PR598 | −57,77 % | +31,21 % | −18,43 % |
| Lightroom, descriptif | −86,19 % | +25,29 % | Voir mémoire par scène |

A1 répond à la direction du retour visuel sur −100 sans toucher aux
intermédiaires. Mais la proximité de cette médiane avec LR n'est pas une
preuve de progrès perceptuel : l'écart absolu médian de réponse ΔC forte
ne passe que de 0,03302 à 0,03294. À +100 il passe de 0,06595 à 0,06097.

A2 limite remarquablement la dérive de teinte perceptuelle à −100 : moyenne
absolue Oklab entre **0,066° et 0,444°** selon la scène, contre **1,596° à
7,866°** pour la production. En revanche sa désaturation est ici trop forte
pour le retour utilisateur. À +100, la dérive atteint **5,256° sur 9421**
après sortie, malgré la conservation de h par l'opérateur : indication qu'il
faut traiter conjointement opérateur et mapping de gamut, pas remplacer une
formule isolément. A2 n'est pas une amélioration systématique.

B change déjà fortement Y **avant** le mapping final : à −100 son ratio
de luminance moyenne est entre **0,743 et 0,962** ; à +100 entre **0,769 et
1,052**. Après sortie, cela représente jusqu'à −25,71 % à −100 et −22,02 %
à +100. Ce n'est donc pas seulement un écrêtage final. La valeur HSV n'est
pas Y : augmenter V et S ne garantit pas d'augmenter Y. Ce résultat concerne
la transposition isolée sur notre base, **pas un défaut démontré de LightCraft**.

Pour la saturation +100, les médianes de ΔC/C forte sont +36,56 % (courant),
+34,52 % (A2), +35,15 % (B). Les trois opérateurs ne conservent pas la même
grandeur : Y sRGB pour RGB radial, L Oklab pour A2, Y ProPhoto D50 pour B.
Le gamut de sortie et ces choix expliquent des variations de luminosité
différentes ; aucune loi ne garantit toutes ces invariances simultanément.

### Robustesse, coût et suite proposée

Les **56 progressions de moyennes de chroma faible** sont monotones sur les
sept états observés. Les tests synthétiques couvrent 11 couleurs × 4 modèles
× 201 valeurs de vibrance : identité au zéro, gris, HDR, RGB signés, valeurs
finies et monotonie de saturation HSV sur ces échantillons. Ils ne prouvent
pas la monotonie de tous les pixels, la qualité des peaux ou les associations
avec les autres commandes.

À vibrance +100 : 2 901 nouveaux points écrêtés pour le courant, 3 025 pour
A1, 4 569 pour A2 et 2 714 pour B, sur les mêmes grilles échantillonnées.
Ces comptes ne sont pas des nombres de pixels de l'image entière ; la baisse
de B n'annule pas son changement de luminance. Aucun nouvel écrêtage à −100.

Temps médians de l'opérateur sur les pixels échantillonnés, hors décodage et
sortie : courant 25,64 ms ; A1 29,13 ms ; A2 114,64 ms ; B 91,15 ms. Ce sont
des mesures exploratoires CPU, pas un benchmark contrôlé, ni des délais IHM
pleine résolution, ni une justification suffisante pour un port GPU.

Vérifications de clôture : **2/2 CTest ciblés** (`vibranceMathTests`,
`colourModelStudyTests`) et **5/5 tests Python du comparateur** réussis.
Les tests de sélection vérifient que les modifications de la sonde native
ou du comparateur déclenchent leurs suites ciblées. La vérification
documentaire passe sans erreur de fichier ou lien local ; elle ne contrôle
pas les liens externes, ne compile pas LaTeX et ne vaut pas validation visuelle.

Suite recommandée, **à décider avant modification de production** :

1. A1 : contrôler visuellement les extrémités sur les scènes actuelles,
   les neutres, jaunes/rouges et peaux, puis vérifier les combinaisons de réglages.
2. A2 : définir un mapping de gamut qui préserve mieux h/L, diminuer la
   désaturation négative, tester séparément le proxy de tons chauds.
3. B : retenir les idées d'asymétrie et de fondu près des gris, mais éviter
   d'adopter ses coefficients ajustés à LR sans étude de luminance propre à IPR.
4. Étendre ensuite à d'autres illuminants, boîtiers et objectifs. Les sept
   scènes actuelles ne suffisent pas à déclarer une généralisation matérielle.

### Bibliographie de l'étude

- [LightCraft PR #598](https://github.com/storytold/lightcraft/pull/598) : hypothèse B,
  contexte d'ajustement empirique, limites revendiquées ; consultation du 13/10/2026.
- [Code du commit étudié](https://github.com/Exalted5373/lightcraft/blob/b9e312a748fde0b0e5b0eeb070a69b7ecf2df59f/crates/pipeline/src/colorops.rs) : opérateurs et coefficients.
- [Code de la branche principale](https://github.com/storytold/lightcraft/blob/main/crates/pipeline/src/colorops.rs) : comparaison de structure, lien évolutif.
- [B. Ottosson, Oklab](https://bottosson.github.io/posts/oklab/) : définition et matrices
  d'un espace perceptuel ; ne prescrit pas notre heuristique de vibrance.
- [W3C, CSS Color 4 — conversions](https://www.w3.org/TR/css-color-4/#color-conversion-code) :
  matrices sRGB/XYZ/ProPhoto et adaptation D65/D50 utilisées par la sonde.

## Vibrance et saturation — validation quantitative v003, 13 octobre 2026

Mémoire canonique : [Couleur v003, 10:39:40 UTC / 12:39:40 Paris](../raw/mesure-couleur-v003-20261013T103940441925Z.json).
Cette campagne référence v002, sans modifier les preuves antérieures.
Elle mesure le correctif de vibrance négative avec protection résiduelle `0,1C`.
Les propositions visuelles de réduction/renforcement de quelques pourcents
restent **consignées, mais non appliquées**.

### Audit et couverture

Après correction par l'utilisateur, les cinq fichiers LR de saturation −100
ne sont plus présents dans la série vibrance et le −100 de 9950 est conforme
au contexte Camera Neutral de son zéro. L'audit préalable ne relève aucun
état manquant, doublon, changement de réglage secondaire ou mauvais profil.
Les 98 variantes IPR déclarent le générateur corrigé, SHA-256
`81d9aa4d575869c4628c424bca19f1f4542540da7860c6cda8ac31986dce3c5f`,
et des neutres compatibles. L'audit natif des TIFF/empreintes passe ensuite :
**196 états mesurés, zéro diagnostic, zéro exclusion, 98 paires**.
Les sept scènes couvrent les sept valeurs −100…+100 pour les deux commandes.

Méthode inchangée : TIFF 16 bits sRGB, RGB linéarisé, points natifs espacés
de huit, sans recalage inter-moteurs. Les références sont propres à chaque
moteur. Les 98 paires gardent `different_optical_development` (IPR corrigé
géométrie/vignettage, LR sans correction objectif). Ce résultat valide des
réponses internes observées, **pas une conformité pixel/perceptuelle Lightroom**.

### Non-régression et effets du correctif

- Les **14 zéros IPR** restent identiques à leur neutre sur les points mesurés.
- Les **28 progressions de chroma faible** (sept scènes × deux moteurs × deux
  commandes) sont croissantes sur les états observés, tolérance `1e-4`.
  Cela ne prouve pas la monotonie de chaque pixel ou de toutes les teintes.
- Comparées à v002, les métriques des **49 états IPR saturation** et des
  **28 états IPR vibrance ≥ 0** sont exactement identiques. Le correctif
  est donc isolé aux 21 états négatifs de vibrance mesurés.
- Les 98 états LR ont également des métriques identiques à v002 : les sept
  fichiers de vibrance 9950 ont été réexportés (empreintes différentes),
  sans différence des statistiques stockées. Cela ne prouve pas une identité
  exhaustive de leurs pixels.
- Aucun nouvel écrêtage mesuré sur les 21 états IPR de vibrance négative ;
  dérive maximale de luminance moyenne **0,0993 %**, sur 9890.

### Vibrance négative : rapprochement et résidus

Médianes des moyennes de variation relative `ΔC/C` par scène, pour les forts
chromas (`C ≥ 0,5` au neutre), en pourcentage :

| Vibrance | IPR v002 avant correctif | IPR v003 | Lightroom |
| --- | ---: | ---: | ---: |
| −100 | −21,51 % | −89,84 % | −86,19 % |
| −50 | −10,44 % | −35,82 % | −43,36 % |
| −25 | −5,15 % | −16,25 % | −21,38 % |

Les valeurs v003/LR proviennent des paires v003 ; la colonne historique
provient des paires v002. Les JSON conservent leur précision complète.
À −100, l'écart absolu médian entre les réponses appariées de ΔC des forts
chromas passe de **0,41015 à 0,03302**. Cette réduction concerne une statistique
de réponse par scène, pas une MAE de pixels ni une calibration physique.

La médiane confirme le retour visuel « −100 légèrement trop fort » : IPR
désature davantage que LR. Mais les écarts ne sont pas uniformes ; les valeurs
par scène empêchent de généraliser un ajustement global à partir de la seule médiane :

| Scène | Variation relative moyenne IPR à −100, forts chromas | Lightroom |
| --- | ---: | ---: |
| 0007 | −90,54 % | −86,19 % |
| 0037 | −91,72 % | −88,36 % |
| 0043 | −89,84 % | −90,56 % |
| 0327 | −87,59 % | −78,81 % |
| 9421 | −86,96 % | −62,16 % |
| 9890 | −84,46 % | −67,53 % |
| 9950 | −91,36 % | −86,97 % |

9421 et 9890 restent les cas prioritaires ; 0043 est légèrement moins
désaturée dans IPR. Les contextes optiques, bases et masques différents
limitent l'attribution de ces écarts à la seule formule. Une différence
de 3,65 **points de pourcentage** entre médianes à −100 ne démontre pas
qu'une réduction de 3–4 % d'un coefficient corrigerait toutes les scènes.
Les états −25 et −50 désaturent encore moins que LR en médiane : éviter
de réduire uniformément toute la branche négative pour corriger son endpoint.

### Positifs et saturation

Les demandes visuelles restent pertinentes comme cibles de recette, mais
ne sont pas des pourcentages déduits directement de ces métriques.
À vibrance +100, les médianes de `ΔC/C` sont : faibles chromas **+72,11 % IPR
contre +85,25 % LR**, forts chromas **+19,29 % contre +25,29 %**.
L'écart signé médian de ΔC des forts chromas est −0,06087 ; sa médiane
absolue vaut 0,06595 et les écarts vont de −0,26381 à +0,08488 selon la scène.
Un renforcement uniforme de 6–7 % n'est donc pas une calibration démontrée,
ni une commande sélective jaune/rouge. Vérifier les carnations et le gamut.

La saturation reste fonctionnellement cohérente et inchangée. À +100,
ΔC médian des forts chromas vaut **+0,24051 IPR contre +0,22537 LR**, tandis
que les faibles chromas donnent +0,14579 contre +0,14412. Le souhait visuel
d'augmenter de 2–3 % ne découle pas d'un manque global mesuré : l'agrégat
IPR est déjà supérieur pour les forts chromas. Garder ce choix esthétique
distinct d'une recherche de parité LR, et contrôler la luminance/écrêtage.

### Conclusion et suite

**Correctif négatif fonctionnel et nettement plus proche de LR sur ce corpus** ;
zéro, positifs, saturation et progressivité observée préservés. La recette
visuelle n'est pas déclarée définitivement validée : les petits ajustements
proposés et les résidus par scène restent à examiner. Pour la prochaine
itération, cibler préférentiellement l'extrémité négative, sans affaiblir
automatiquement −25/−50, et traiter séparément positifs et saturation.

Aucune nouvelle modification de production, génération ou suppression d'image
pendant cette analyse. Pas de push/fusion/tag. Rapport unique et dérivés mis
à jour ; les mémoires JSON antérieures, dont l'audit interrompu, sont conservées.

## Reprise après génération — arrêt sur erreurs Lightroom, 13 octobre 2026

Précontrôle XMP à **12:26:34 Paris / 10:26:34 UTC**, sur 103 TIFF LR
des dossiers vibrance/saturation. À la demande de l'utilisateur, **arrêt
avant les mesures dès détection d'erreurs LR** : pas de campagne comparative
v003 publiée lors de cette tentative. [Trace JSON du précontrôle](../raw/audit-couleur-lr-v001-20261013T102634Z.json)
avec chemins relatifs à `tests/image/base`, valeurs, motifs et SHA-256.

| Fichier dans `couleur/vibrance/` | Défaut constaté dans le XMP | Action requise |
| --- | --- | --- |
| `6V1A0007-8.tif` | Vibrance 0, Saturation −100 | Sortir de la série vibrance ou retirer cet export supplémentaire |
| `6V1A0037-8.tif` | Vibrance 0, Saturation −100 | Même action |
| `6V1A0043-8.tif` | Vibrance 0, Saturation −100 | Même action |
| `6V1A0327-9.tif` | Vibrance 0, Saturation −100 | Même action |
| `6V1A9421-8.tif` | Vibrance 0, Saturation −100 | Même action |
| `6V1A9950.tif` | Vibrance −100, profil Adobe Standard et contexte différent du zéro de série | Réexporter à −100 avec Camera Neutral et les autres paramètres identiques au zéro `6V1A9950-4.tif` |

Les cinq premiers fichiers étaient déjà diagnostics supplémentaires en v002,
mais la nouvelle consigne impose de s'arrêter, sans les ignorer. Ne pas les
déplacer en écrasant un fichier de saturation existant : ils ne remplacent
aucun état vibrance attendu. Le −100 de 9950 est maintenant incompatible,
alors qu'il était admissible dans v002 ; les empreintes/mesures historiques
restent celles de leurs fichiers d'alors.

Aucune autre erreur d'association ou de réglages XMP relevée dans ce
précontrôle ; cela ne valide ni les pixels ni les TIFF IPR fraîchement générés.
Aucune image supprimée, déplacée ou corrigée automatiquement, aucun nouveau
changement de production. Reprendre l'audit après correction de ces six fichiers.

## Correctif de production — vibrance négative, 13 octobre 2026

Appliqué localement sur `patch_color` après autorisation de l'utilisateur.
**Validation photographique en attente** : les mesures v002 ci-dessous
décrivent le rendu avant ce correctif, pas celui des nouveaux exécutables.
Aucun nouveau TIFF du corpus n'a été généré pendant cette intégration.

Le facteur est désormais centralisé dans `src/core/Vibrance.h` et utilisé
par `ImageEditor::renderWorkingImage`, donc par l'aperçu, l'export et le
générateur. Pour `v = vibrance/100`, la pondération vaut `1−C` au positif,
inchangée, et `1−0,1C` au négatif. À −100, le facteur résiduel est `0,1C`,
au lieu de C : forte réduction aussi sur les couleurs déjà saturées.
La loi de saturation et le mélangeur ne changent pas.

Le coefficient nommé `negativeChromaProtection = 0.1` est un réglage
explicite de prototype : il conserve un faible résidu afin que vibrance −100
ne soit pas strictement la même commande que saturation −100. Il ne vient
pas d'une loi physique ou d'un poids commercial DCP, et ne signifie pas 10 %
de chroma HSV restant. La prochaine campagne doit évaluer ce choix sur les
sept scènes, notamment peaux et gradients ; il pourra être ajusté après mesure.
Voir les [équations et limites](../../imageeditor-render-working-image.md#7-vibrance-et-saturation).

Vérifications : **13/13 CTest ciblés réussis**, dont nouveau `vibranceMathTests`,
export négatif, génération sur petits fixtures temporaires, RAW/DCP, IHM,
cycle de vie, Lumières et mélangeur. Tests mathématiques : monotonie du gain,
identité au zéro, continuité au changement de signe, loi positive inchangée,
bornes, gris/noirs inchangés, luminance et absence de dépassement pour une
réduction seule. La régression d'export vérifie une forte réduction à −100
avec un résidu couleur faible. Sélection de tests : **15 tests Python réussis** ;
comparateur : **cinq tests réussis**. Règles de sélection et CMake complétés.

Application Windows et générateur Release recompilés. SHA-256 du générateur :
`81d9aa4d575869c4628c424bca19f1f4542540da7860c6cda8ac31986dce3c5f`.
Utiliser `./scripts/generate-test-images.ps1 couleur` après retrait manuel
des anciens exports IPR Couleur et de leurs neutres Couleur ; conserver les
références LR, le mélangeur et les JSON. Le générateur refuse les destinations
existantes : il ne remplacera pas silencieusement les anciens résultats.
La future analyse doit porter une nouvelle version et référencer v002.
Pas de nouvelle parité Lightroom déclarée, ni de fusion/tag/push déclenché.

### Retour visuel utilisateur — 13 octobre 2026

Après le correctif, l'utilisateur juge la désaturation à **vibrance −100
légèrement trop forte**. Il propose de réduire son intensité d'environ
**3 à 4 %**, donc de conserver un peu plus de couleur. Vers **+100**, il
propose de renforcer l'effet d'environ **6 à 7 %**, avec pour objectif des
jaunes et rouges plus présents.

Pour la **saturation**, l'utilisateur propose également, sur appréciation
visuelle, de renforcer l'effet vers **+100 d'environ 2 à 3 %**. Cette piste
est distincte de l'ajustement de vibrance : elle ne concerne pas les valeurs
négatives ni le zéro. Le sens exact du pourcentage reste à préciser avant
conversion en coefficient, et le gamut/écrêtage sera à contrôler.

Ce sont des estimations visuelles et des pistes de réglage, pas des mesures
de ΔC, des points de curseur supplémentaires ou une validation numérique.
La référence exacte de ces pourcentages (intensité du traitement ou réponse
colorimétrique observée) reste à préciser avant conversion en coefficients.
Un renforcement global de vibrance n'est pas un renforcement sélectif des
jaunes/rouges : vérifier aussi les autres couleurs, carnations, gradients
et écrêtages. Aucune scène particulière n'a été indiquée pour ce retour.

**Retour consigné uniquement : aucun nouvel ajustement de production appliqué.**
La validation photographique du correctif reste en cours ; les JSON v001/v002
et leurs conclusions quantitatives ne sont pas modifiés par cette appréciation.

## Vibrance et saturation — reprise complète du 13 octobre 2026

Mémoire canonique : [Couleur v002, 09:20:25 UTC / 11:20:25 Paris](../raw/mesure-couleur-v002-20261013T092025490768Z.json).
Elle référence explicitement la campagne v001 dans `supersedes_campaign_id` ;
les deux JSON restent immuables. Cette reprise complète le corpus et corrige
les associations, **sans modification du code de rendu**.

### Nettoyage autorisé et génération

Sur demande explicite de l'utilisateur, 224 fichiers ont été supprimés :
217 variantes IPR courantes du groupe Couleur et sept neutres Couleur,
soit 57 851 612 345 octets (environ 53,88 Gio). Le neutre 9890 sans déclaration
était inclus dans ce remplacement. La suppression est définitive sur disque,
pas un passage par la corbeille ; les fichiers ont ensuite été régénérés
depuis les sept RAW conservés. Aucun fichier du mélangeur, TIFF Lightroom,
ancien dossier de campagne, RAW ou JSON de mesure n'a été supprimé.

Commande : `./scripts/generate-test-images.ps1 couleur`. Le lanceur utilise
le générateur courant recompilé dans `build-windows-msvc/Release` ; son
SHA-256 embarqué est
`368d8a938c8c9a06c12a9bc2a1fde74960cc62a1c70e859ae50bee414e25d266`.
Génération terminée sans erreur : **217 variantes et sept neutres TIFF**.
Le groupe complet a été remplacé parce que le générateur n'accepte pas une
sélection de curseurs et refuse l'écrasement des fichiers existants.
Température/nuance/niveaux de gris sont régénérés, mais pas réanalysés ici.

### Corpus complet et audit

Sept scènes : 0007, 0037, 0043, 0327, 9421, **9890** et 9950, préfixe 6V1A.
Chaque commande dispose, dans chaque moteur, de −100, −50, −25, 0, +25, +50
et +100 : **196 états requis mesurés, aucune exclusion, 98 paires**.
Le zéro IPR 9890 est maintenant déclaré et compatible avec ses variantes ;
0037 +50 est présent, 0327 +25 n'est plus ambigu et 9950 utilise Camera Neutral
avec les autres paramètres constants.

Cinq fichiers LR supplémentaires restent diagnostics dans le dossier vibrance :
0007−8, 0037−8, 0043−8, 9421−8 et 0327−9 déclarent Saturation −100.
Ils ne sont pas des états requis manquants : l'inventaire total contient donc
**201 associations : 196 mesurées, cinq diagnostics, zéro exclue**.
Ils sont conservés, sans choisir un autre fichier sur la seule base du suffixe.

Les **98 paires** conservent la réserve `different_optical_development` :
géométrie/vignettage actifs côté IPR, correction objectif désactivée côté LR.
La normalisation IPR est désactivée. Les références restent propres à chaque
moteur : neutres IPR déclarés sous `base/neutre`, zéro LR audité dans chaque série.
Leurs balances et rendus de base ne sont pas supposés identiques. L'agrégat de
développement déclaré compatible reste vide. Ni MAE ni recalage inter-moteurs.
Méthode identique à v001 : TIFF 16 bits sRGB, mesures en RGB linéaire,
grille native de pas huit, empreintes complètes vérifiées à la clôture.

### Résultats et comparaison avec v001

- Les **14 zéros IPR** sont identiques à leur neutre indépendant sur les points
  échantillonnés. Le zéro LR utilisé comme référence n'est pas une preuve
  indépendante d'identité.
- Les **14 progressions IPR** de chroma faible passent le contrôle de moyenne
  croissante, tolérance `1e-4` ; les sept progressions saturation LR passent aussi.
  Le JSON laisse cinq progressions vibrance LR indéterminées à cause des fichiers
  diagnostics supplémentaires ; les deux séries sans ces extras passent.
  Ce verdict conservateur ne signifie pas qu'un des sept états requis manque.
- À saturation −100, l'écart entre canaux linéaires IPR est nul sur les points
  mesurés des sept scènes ; le résidu LR est de 2,25 à 3,43 × 10⁻⁵.
- Les **84 états IPR communs mesurés** aux deux campagnes ont des métriques
  exactement identiques. L'évolution des agrégats vient du corpus complété
  et des corrections LR, pas d'un correctif de rendu IPR.

Médianes des moyennes de ΔC par scène, relativement au zéro propre :
faible chroma `0,02 ≤ C < 0,25`, forte chroma `C ≥ 0,5` au neutre.
Chaque ligne porte sept scènes ; les masques sont propres au moteur et à l'état.

| Commande | Valeur | ΔC faible IPR | ΔC faible LR | ΔC forte IPR | ΔC forte LR |
| --- | ---: | ---: | ---: | ---: | ---: |
| Saturation | +50 | +0,07522 | +0,07608 | +0,16791 | +0,15025 |
| Vibrance | −100 | −0,13661 | −0,16480 | −0,14888 | −0,55708 |
| Vibrance | +50 | +0,06159 | +0,03666 | +0,06865 | +0,07229 |
| Vibrance | +100 | +0,11914 | +0,12675 | +0,12975 | +0,16409 |

**Saturation : fonctionnement cohérent confirmé sur le corpus élargi.**
Les réponses de faibles chromas restent proches ; l'action IPR sur les forts
chromas est un peu plus forte aux positifs intermédiaires. Cela n'établit pas
une conformité perceptuelle Lightroom ou une validation universelle du gamut.

**Vibrance : l'écart négatif est confirmé, pas résolu par les nouveaux exports.**
À −100, la médiane des moyennes de variation relative `ΔC/C` vaut
**−21,51 % IPR contre −86,19 % LR** pour les forts chromas, et −81,40 % contre
−97,43 % pour les faibles. À +50, la protection relative IPR est effective :
+37,15 % sur les faibles chromas contre +9,89 % sur les forts ; LR donne
+24,06 % et +10,82 %. Ne pas comparer les seuls ΔC absolus pour juger cette
protection. La formule décrite dans v001 protège actuellement les couleurs
déjà saturées aussi bien en réduction qu'en augmentation.

La vibrance +100 conserve la luminance moyenne IPR à la précision des mesures.
La saturation +100 monte jusqu'à **+4,95 %** côté IPR. Les réponses LR à
vibrance +100 de 0007 et 9950 présentent respectivement **−27,93 % et −34,28 %**
de luminance moyenne ; elles sont auditées dans leur série, mais ces cas restent
à examiner visuellement et ne doivent pas être masqués par une médiane.
Pour 9890, saturation +100 donne +4,04 % IPR contre +9,18 % LR ; vibrance +100
donne une moyenne IPR quasi inchangée contre +4,54 % LR.

### Conclusion actuelle

Le corpus vibrance/saturation est désormais complet et exploitable pour les
**réponses internes**, avec toutes les réserves conservées. Aucun nouveau
réexport n'est requis pour combler les états de cette campagne. La parité LR
reste non validée, notamment à cause des contextes optiques différents.

Priorité proposée : décider d'un correctif ciblé de **vibrance négative**,
séparant sa pondération de celle des positifs, puis le vérifier sur ce corpus
et sur les carnations/gradients. Il ne faut pas ajouter des poids commerciaux
par profil pour forcer ces seules médianes. La saturation ne justifie pas
un changement immédiat sur la base de ces moyennes seules ; garder les
contrôles de gamut et de luminance dans une prochaine recette.

Tests ciblés réussis : deux CTest générateur/export, cinq tests du comparateur
(dont le lien entre versions), dix WB, 14 sélection Windows ; suite groupe :
26 exécutés dont un ignoré. Les nouveaux TIFF sont conservés après analyse.
Pas de modification de production, de suppression après mesure, de publication
Git ni de nouvelle validation visuelle déclarée pendant cette reprise.

## Vibrance et saturation — campagne native du 12 octobre 2026

Mémoire canonique : [mesure Couleur v001, UTC 21:26:46](../raw/mesure-couleur-v001-20261012T212646781304Z.json).
Les données et exclusions font autorité dans ce JSON immuable ; cette section
est une interprétation, pas une validation de fidélité Lightroom.

### Corpus et contrôles

Le groupe Couleur a été généré dans les dossiers partagés existants :
217 variantes et sept neutres prévus, sans écrasement ni suppression.
La présente analyse porte seulement sur vibrance et saturation, aux valeurs
−100, −50, −25, 0, +25, +50 et +100. Six scènes sont comparables :
6V1A0007, 0037, 0043, 0327, 9421 et 9950. Les quatorze variantes IPR de
9890 sont exclues : son neutre déclaré n'est plus présent lors de l'analyse,
et aucune référence n'est substituée.

Sur **189 associations** (98 IPR, 91 LR), **166 sont mesurées, neuf sont
diagnostics et quatorze exclues**. Les comparaisons de réponses propres aux
moteurs portent sur **82 paires** : 40 vibrance et 42 saturation.
Les neuf diagnostics sont conservés :

- Six fichiers LR placés dans vibrance déclarent en réalité Saturation −100
  et Vibrance 0 : suffixe −8 des scènes 0007, 0037, 0043, 9421 et 9950,
  et suffixe −9 de 0327. Ils ne deviennent pas des références zéro.
- `vibrance/6V1A0327-5.tif` et `-6.tif` déclarent tous deux +25 : les deux
  sont écartés de l'agrégat apparié, sans choix arbitraire. Les n°7 et 8
  déclarent déjà +50 et +100.
- `vibrance/6V1A9950-7.tif` déclare bien +100, mais Adobe Standard,
  netteté 40, bruit couleur 25, corrections objectif et aberrations actives,
  au lieu du développement de sa référence : ce n'est pas une variation
  isolée de vibrance. Son réexport doit conserver les paramètres du n°4.

IPR utilise Camera Neutral, sans normalisation RAW, avec géométrie et
vignettage LCP actifs. Les séries LR admissibles utilisent Camera Neutral,
5200 K / +5 (Custom), sans correction objectif. Les références IPR viennent
de `base/neutre` ; le zéro LR est le fichier unique propre de chaque série,
audité explicitement. Le neutre LR As Shot n'est pas substitué au support
Custom : même température affichée ne suffit pas à prouver le même développement.

**Les 82 paires ont des développements optiques différents** : l'agrégat
« compatible_declared_development » contient zéro scène. Les dimensions
diffèrent également d'un pixel. Aucun recalage, redimensionnement, MAE ou
superposition inter-moteurs n'est réalisé. Mesures sur TIFF 16 bits sRGB,
RGB linéarisé et grille native espacée de huit pixels ; empreintes complètes
et métadonnées sont conservées avant/après mesure.

### Résultats

Les douze zéros IPR (six scènes × deux commandes) sont identiques à leur
neutre indépendant **sur les points échantillonnés**. Les douze séries IPR
ont une réponse moyenne de chroma faible croissante sur les sept états,
à tolérance `1e-4`. Pour LR, les six séries saturation passent ce contrôle ;
les diagnostics empêchent un verdict global de progression vibrance.
Le zéro LR choisi comme référence n'est pas un test indépendant d'identité.

Le tableau donne la médiane des moyennes par scène de ΔC, relativement au
zéro de chaque moteur, pour les points de chroma élevé (C ≥ 0,5 au neutre).
C = (max RGB − min RGB) / max RGB en RGB linéaire ; il ne s'agit pas d'une
mesure perceptuelle ni d'une différence pixel à pixel. Les masques sont
propres aux moteurs et les points écrêtés sont filtrés selon la méthode du JSON.

| Commande | Valeur | Scènes appariées | ΔC IPR | ΔC LR |
| --- | ---: | ---: | ---: | ---: |
| Saturation | −100 | 6 | −0,70149 | −0,69688 |
| Saturation | +25 | 6 | +0,10588 | +0,08478 |
| Saturation | +50 | 6 | +0,18354 | +0,15218 |
| Saturation | +100 | 6 | +0,25323 | +0,23795 |
| Vibrance | −100 | 6 | −0,16128 | −0,55909 |
| Vibrance | +25 | 5 | +0,03582 | +0,03655 |
| Vibrance | +50 | 6 | +0,06894 | +0,07352 |
| Vibrance | +100 | 5 | +0,12975 | +0,16409 |

**Saturation : fonctionnement cohérent**, zéro conservé, direction correcte
et niveaux de gris à −100 (écart entre canaux linéaires IPR nul sur les points
mesurés ; résidu LR de l'ordre de 2,3 à 3,4 × 10⁻⁵). Les réponses des faibles
chromas sont relativement proches : à +50, ΔC médian 0,07572 IPR contre
0,07468 LR. IPR agit davantage sur les forts chromas aux positifs intermédiaires.
Ce constat ne constitue pas une nouvelle validation visuelle ou une parité LR.

**Vibrance : divergence importante aux valeurs négatives.** À −100, IPR
conserve nettement plus les couleurs déjà saturées que Lightroom. À +25/+50,
son action sur les faibles chromas est plus forte (à +50 : 0,06184 contre
0,03399), tandis que la réponse des forts chromas est plus proche.

Le code actuel applique, avant les contraintes de sortie :

```text
S = 1 + saturation / 100
V = 1 + (vibrance / 100) × (1 − C)
RGB' = Y + (RGB − Y) × max(0, S × V)
```

À saturation −100, le facteur est nul ; à vibrance −100 seule, il vaut C.
La même protection des couleurs déjà saturées agit donc aux valeurs positives
et négatives. C'est une piste structurelle pour expliquer l'écart négatif,
pas la démonstration du fonctionnement interne Lightroom ni une attribution
exclusive de toutes les différences à cette formule.

La luminance théorique est conservée avant bornage, mais pas nécessairement
après les contraintes de gamut et l'encodage : saturation +100 donne jusqu'à
**+4,95 % de luminance moyenne** IPR sur ce corpus. Vibrance +100 conserve
la moyenne IPR à la précision des mesures. LR présente notamment **−27,93 %**
pour vibrance +100 de 0007 : ce cas reste à examiner visuellement, sans
le dissimuler par une médiane ni conclure automatiquement à un défaut IPR.

### Conclusion et suite

La saturation et la progressivité IPR passent les contrôles fonctionnels
observés. La vibrance négative nécessite une décision de rendu : conserver
la protection actuelle, ou chercher une réduction plus proche de Lightroom.
Avant ajustement de coefficients, homogénéiser les options optiques et
résoudre les deux associations LR vibrance mentionnées ci-dessus ; compléter
9890 seulement si une référence LR et un neutre IPR de même développement
sont disponibles. Les neutres source ne sont pas à remplacer arbitrairement.

**Aucun code de production modifié pendant cette campagne, aucune image
supprimée.** L'outil spécialisé réutilise le moteur canonique Couleur et
conserve ses sources/empreintes ; le commit de base `7297197` ne contient pas
encore ce nouvel outil local. Vérifications ciblées : cinq tests du comparateur,
14 de sélection Windows, dix WB ; suite groupe : 26 exécutés dont un ignoré ;
deux CTest générateur/export réussis. Pas de nouvelle validation photographique
déclarée, ni de publication Git déclenchée par cette analyse.

## Correctif du 11 octobre 2026 — retouche WB dans l'espace capteur

**Correctif de production livré depuis `patch_color` dans `dev`**, isolé
des autres travaux locaux, par fusion `--no-ff` et tag `v0.1.17-alpha`.
La [CI 38166258163](https://github.com/KimntexX/IPR/actions/runs/38166258163)
est réussie sur Windows, Ubuntu, macOS Apple Silicon et macOS Intel :
18 CTest et 148 tests Python par plateforme. Le code final est identique
au commit testé `74f4310f590312ac4f5462a00e68ebd869ab308f` ; seuls les
documents et preuves de livraison complètent la fusion. `main` reste inchangée.
L'entrée utilisateur « 6V1A6421 » correspond dans le
corpus à **6V1A9421**. Audit XMP : `nuance/6V1A9421-5.tif` = 5200 K / 0,
les indices 6, 7, 8, 9 = +25, +50, +100, +150, profil Camera Neutral.
Il ne manque donc plus le support manuel 0 de cette scène.

La température/nuance d'un RAW DCP est maintenant appliquée au RGB capteur
**avant** matrices, tables et courbe du profil, puis les autres réglages.
Le moteur conserve un contexte immuable, réutilisé pour les curseurs et
le changement de profil ; aucun nouveau décodage à chaque état. Les matrices
du DCP et le blanc effectivement utilisé par LibRaw déterminent les gains.
La norme vert=1 et la distance de nuance en CIE 1960 remplacent, pour ces RAW,
l'adaptation RGB de sortie. Les TIFF/JPEG ont toujours leur propre adaptation
RGB, sans prétendre disposer d'une caractérisation de capteur.
Voir la [description technique et les équations](../../camera-profile-engine.md#retouche-températurenuance-avant-dcp--11-octobre-2026).

### Vérifications réalisées

- Compilation Windows Release de l'application et de ses tests réussie.
- **17/17 CTest** réussis, dont Lumières, DCP, RAW, export/générateur,
  mémoire et IHM. **47 tests Python ciblés** réussis : comparaison WB (10),
  analyseurs de groupe (22), sélection Windows (15).
- Tests synthétiques : identité au neutre avec température source fractionnaire,
  gains avant la courbe non linéaire, absence d'accumulation des retouches,
  options d'exposition DCP conservées, températures et nuances individuelles,
  erreur explicite si la caractérisation ColorMatrix manque.
- Le cache LRU inclut le tampon capteur et ne double pas la charge d'une
  allocation partagée. Le proxy porte un tampon capteur à sa propre géométrie.
- Sonde fraîche de **six RAW × quinze états = 90 états**, avec Camera Neutral,
  WB boîtier, normalisation et optique désactivées. Le neutre interne source
  est conservé **exactement sur les six scènes**, sans changer son décodage
  ni lui attribuer artificiellement 5200/+5. Cela vérifie l'absence de régression
  de la retouche neutre ; ce n'est pas une identité de fichiers avec les anciens
  TIFF partagés, dont l'optique et la provenance complète restent distinctes.

Mémoire canonique : [sonde RAW/DCP v003](../raw/comparaison-wb-20261011/sonde-wb-capteur-v003-20261011T133656397Z.json).
Elle conserve valeurs, mesures, gains, blanc capteur, options, empreintes RAW,
DCP, code et exécutable. Il s'agit de points natifs espacés de huit, encodés
par le moteur de sortie 16 bits, sans redimensionnement ; aucun nouvel export
TIFF n'a été généré par cette sonde. Au réglage **2000 K**, le pourcentage de
points écrêtés est compris entre **0 et 0,0652 %** : quatre scènes à zéro,
0037 à 0,0289 % et 0043 à 0,0652 %. Ces chiffres ne doivent pas être soustraits
sans précaution aux anciens exports IPR de provenance différente.

### Statut de validation

La compilation et les tests structurels ne certifient pas la parité Lightroom.
La [comparaison fraîche v003](../raw/comparaison-wb-20261011/comparaison-wb-capteur-v003-20261011T134140543650Z.json)
est terminée : **91 états LR mesurés**, aucun exclu/diagnostique ; les deux
exports nuance 0 de 0037 restent visibles, mais cette scène est exclue de
l'agrégat strict à 0 (cinq scènes), sans sélectionner arbitrairement un doublon.
Les autres agrégats portent six scènes.

| État | Axe IPR médian | Axe LR médian | Médiane des écarts appariés IPR−LR |
| --- | ---: | ---: | ---: |
| Température 2000 K, Δlog(R/B) | −2,8123 | −2,9200 | −0,1259 |
| Température 3500 K, Δlog(R/B) | −0,9901 | −1,0978 | +0,0594 |
| Température 9000 K, Δlog(R/B) | +0,9398 | +0,9718 | −0,0536 |
| Nuance −150, Δlog(√RB/G) | −1,2595 | −1,1957 | −0,0629 |
| Nuance +150, Δlog(√RB/G) | +1,2122 | +1,2611 | −0,0240 |

Les douze séries IPR (six scènes × deux commandes) ont des réponses moyennes
monotones sur les états échantillonnés, à tolérance `1e-4`. Cela ne garantit
pas la monotonie de chaque pixel : le masque de points non écrêtés peut varier.
Les réponses de nuance et de température sont maintenant bien plus cohérentes
avec les coordonnées du profil qu'une multiplication après sa courbe.

**Ne pas masquer les divergences par la médiane signée** : à 2000 K, les
écarts appariés vont de −0,7004 à +0,6027 selon la scène, et la médiane des
valeurs absolues est **0,5017**. Les trois plus fortes divergences sont 0327,
0007 et 0037. Les masques de points sont indépendants entre moteurs, les
blancs source et les rendus de base diffèrent ; ce n'est ni une MAE couleur
ni une preuve de précision physique. L'influence des tables/courbes DCP et
du gamut subsiste. Il n'y a pas lieu d'ajouter un poids commercial arbitraire
par profil pour annuler cet agrégat.

Conclusion : intégration et non-régression neutre vérifiées. **Rendu accepté
visuellement par l'utilisateur le 11 octobre 2026** : légèrement plus violet
à 2000 K et plus orangé à 50000 K que Lightroom, écarts jugés acceptables.
Cette validation n'établit ni une parité numérique LR, ni une calibration
physique, ni une couverture de tous les boîtiers et profils. Aucun poids
supplémentaire ou correction plus forte n'est ajouté ; leur éventuelle
évolution attend les retours de tiers et d'autres boîtiers/objectifs.
La condition de CI multiplateforme avant fusion est satisfaite. Les paquets
de livraison sont contrôlés par un workflow distinct ; cette CI native ne
certifie pas à elle seule les archives ni les fonctions non couvertes visuellement.
Pour reproduire le contrôle, ouvrir les six RAW dans le nouvel
`build-windows-msvc/Release/appIPR.exe`, Camera Neutral, normalisation et auto WB
désactivées ; essayer surtout 2000/3500/9000 K et nuances ±150, puis revenir
au blanc source. Contrôler continuité, absence de zones saturées artificielles,
aller-retour au neutre, export et restauration de la recette. Les neutres
partagés restent dans `base/neutre`. Aucun nouveau fichier LR n'est requis
pour ce premier contrôle visuel ; on ne demande pas de refaire tout le corpus.
Le standalone précédemment distribué n'est pas mis à jour par ce seul build.
Aucun RAW, neutre ou TIFF n'a été supprimé.
Les preuves précédentes restent immuables et les résultats ci-dessous
décrivent les versions antérieures, pas le nouveau moteur.

## Reprise du 11 octobre 2026 — neutres partagés et TIFF LR corrigés

### Références et interprétation de la balance des blancs

Les neutres communs **LR et IPR sont dans `tests/image/base/neutre`**.
Lightroom rapporte sur ce corpus **5200 K / nuance +5 en As Shot** :
ce n'est pas une erreur de saisie de l'utilisateur. Ces coordonnées sont
propres à l'interprétation du blanc par LR et au boîtier/profil concerné ;
elles ne justifient pas une constante +5 pour tous les boîtiers dans IPR.

Ne pas confondre trois références : le neutre As Shot partagé, le support
manuel 5200/+5 de la campagne température du 10 octobre, et le support
manuel 5200/0 de la campagne nuance. Un support manuel mesure la réponse
d'un curseur ; il ne remplace pas l'audit du rendu à l'ouverture.
Le générateur simplifié du 11 octobre utilise désormais nuance 0 comme
support de température : ses futurs rendus ne sont pas interchangeables
sans audit avec les exports à +5 du 10 octobre.

L'audit retrouve la référence LR de 0037 sous **`6V1A0037.tif`**,
grâce à son `RawFileName` embarqué, sans déduire le RAW du numéro d'export.
Les deux candidats IPR de chaque scène sont conservés dans l'inventaire,
avec leur hash : le nom `-IPR-CameraNeutral-AsShot` guide la sélection,
mais ne prouve pas à lui seul les réglages ou l'exécutable de génération.

**Confirmation de l'utilisateur du 11 octobre** : les fichiers à retenir
comme références du rendu souhaité sont **`-IPR-CameraNeutral-AsShot.tiff`**,
et la **normalisation RAW n'était pas cochée** lors de leur génération.
Il s'agit d'une confirmation opérateur, pas d'une métadonnée extraite du TIFF.
Cette déclaration est conservée dans une
[mémoire JSON de contexte opérateur](../raw/comparaison-wb-20261011/contexte-operateur-wb-v002-20261011T093003Z.json),
liée par empreinte à l'audit des fichiers, sans modifier celui-ci.
Ne pas substituer les fichiers génériques `-IPR.tiff`, ni expliquer le rendu
retenu par une normalisation activée. Les réglages de génération du jeu
générique restent non établis.

Le neutre 9421 a été remplacé pendant cette reprise. Le fichier principal
`6V1A9421-LR-CameraNeutral-AsShot.tif` indique maintenant optique désactivée,
5200/+5 et **WhiteBalance=Custom** ; la copie `_optique` indique As Shot
avec correction d'objectif et aberration chromatique activées. La copie
optique n'est pas choisie comme référence commune. `Custom` à ces mêmes
coordonnées n'est pas une erreur de valeur, mais ne doit pas être présenté
comme un export As Shot confirmé par son XMP.

Source de vérité de cet audit :
[neutres et métadonnées WB, mémoire v002](../raw/comparaison-wb-20261011/audit-neutres-wb-v002-20261011T091439198763Z.json).
Les fichiers ont été relus et leurs empreintes revérifiées avant publication.
Le premier essai a été interrompu par la disparition d'un nom de fichier
pendant son remplacement : aucune mémoire partielle n'a été publiée.

Constat important : les deux neutres IPR ont des empreintes différentes sur
cinq scènes sur six ; **0037 est la seule paire strictement identique**.
Moyennes Y linéaires relatives à LR, sur pixels natifs échantillonnés à pas 8
sans recalage inter-moteurs :

| Scène | `IPR-CameraNeutral-AsShot` / LR | `IPR.tiff` / LR |
| --- | ---: | ---: |
| 0007 | 1,00363 | 0,77008 |
| 0037 | 0,87732 | 0,87732 |
| 0043 | 0,99393 | 0,61586 |
| 0327 | 1,04723 | 0,67858 |
| 9421 | 1,04728 | 0,71967 |
| 9950 | 1,37872 | 0,98532 |

Ce tableau est un **diagnostic photométrique**, pas une MAE, une mesure de
justesse des couleurs ni une preuve de réglages identiques. Les réglages
et l'exécutable des neutres IPR ne sont pas attestés par leurs noms ; la
confirmation opérateur ci-dessus établit le jeu et l'absence de normalisation,
mais pas l'ensemble des réglages ni la version de l'exécutable. Ne pas
mélanger les candidats pour ajuster la température ou la nuance ; vérifier
le profil, la normalisation RAW et l'état restauré à leur génération.
Pour 0007, le candidat nommé CameraNeutral/AsShot est proche du neutre LR
en Y, alors que le générique est sensiblement plus sombre : les candidats
ne sont donc pas interchangeables. Cela ne démontre pas la cause de l'écart.

### Corrections reçues et couverture restante

- 0007 : le TIFF nuance +150 est désormais présent et identifié à +150.
- 9421 : les variantes température/nuance indiquent désormais les deux
  corrections optiques désactivées. Le point **nuance 0 manque encore** :
  conserver les huit valeurs dans l'inventaire, sans inventer le neuvième
  ni utiliser le neutre +5 comme support zéro.
- 0037 : deux fichiers sont encore identifiés à nuance 0 (`-4` et `-6`).
  Ils restent traçables ; cette valeur dupliquée ne participe pas à une
  médiane stricte et ne permet pas de certifier la progression complète.

### Correctifs de l'analyseur et état de la production

La recette WB sait auditer les vrais neutres du dossier commun, conserve
les métadonnées As Shot et distingue la copie optique de la référence
principale. Ses médianes strictes excluent désormais les paires diagnostiques
et les associations dupliquées dans l'un ou l'autre moteur.

La réévaluation des anciens pixels IPR est explicite : leurs mémoires JSON
immuables servent à retrouver les réglages et empreintes, revérifiées sur
les fichiers présents. Ce n'est **pas une nouvelle génération IPR**.
L'ancien argument de manifeste `--generation` de cette recette a été retiré,
sans supprimer les preuves historiques ni les images.

**Aucune correction température/nuance du moteur de production n'est
encore livrée dans cette reprise.** Le déplacement de la WB avant le DCP,
son ancrage au blanc caméra et la suppression éventuelle de la compensation
froide empirique demandent une ablation contrôlée, sans altérer le neutre
As Shot approuvé. La réception de TIFF LR corrigés ne constitue pas,
à elle seule, une preuve permettant de choisir de nouveaux coefficients.

La sonde d'ablation projette la formule actuelle, sa version sans diagonale
froide et un Bradford complet sur les **mêmes neutres sélectionnés**.
Ces projections restent diagnostiques : une image déjà rendue et encodée
ne permet pas de reconstituer le RGB caméra avant DCP, sa dynamique perdue
ou ses tables non linéaires. Ne pas intégrer le modèle donnant seulement
la meilleure moyenne comme correctif de production.

Contrôle mathématique synthétique à 2000 K et référence de décodage
4924,211 K, avant compression de gamut : un gris RGB linéaire `(0,18; 0,18;
0,18)` devient environ `(0,00724; 0,19369; 1,07302)` avec la formule actuelle,
`(0,02069; 0,16277; 1,27740)` sans diagonale froide, et
`(−0,05674; 0,22963; 1,27312)` avec le Bradford complet. Ce ne sont pas des
mesures des photographies : ces valeurs illustrent qu'une suppression
aveugle peut augmenter le bleu hors gamut, et qu'une adaptation complète
appliquée après le profil n'est pas automatiquement une WB caméra correcte.
Le calcul conserve des valeurs hors `[0,1]` jusqu'au gamut de sortie ; ne
pas les écrêter dans la matrice pour dissimuler le problème.

### Réévaluation des séries de curseurs avec les TIFF LR corrigés

Source de vérité :
[comparaison native v002, 172 états](../raw/comparaison-wb-20261011/comparaison-temperature-nuance-v002-20261011T093638765086Z.json).
90 états IPR historiques réutilisés et 82 états LR mesurés, 172 mesures,
aucun diagnostic ou fichier exclu parmi les états associés. Les huit
variantes LR nuance de 9421 restent **dans l'inventaire sans réponse mesurée**,
car leur support manuel 0 manque ; ne pas interpréter `excluded=0` comme
une couverture complète du protocole. Le doublon 0037 à zéro reste visible.

Les empreintes des quatre sources de production contrôlées sont identiques
à celles conservées dans les mémoires originales IPR. La différence des
résumés ci-dessous n'est donc **pas une amélioration du code** : les TIFF
LR ont été corrigés et les effectifs/règles d'agrégation ont changé.
Les supports de réponse sont toujours ceux de la campagne originale
(température : 5200/+5 ; nuance : 5200/0), distincts des neutres partagés.

| Contrôle | États strictement appariés | Axe médian IPR | Axe médian LR | Médiane des écarts IPR−LR |
| --- | ---: | ---: | ---: | ---: |
| Température 2000 K | 6 scènes | −4,7030 | −2,9200 | −1,6836 |
| Température 3500 K | 6 scènes | −1,3305 | −1,0978 | −0,2675 |
| Température 6500 K | 6 scènes | +0,4436 | +0,4523 | −0,0356 |
| Température 9000 K | 6 scènes | +0,9258 | +0,9718 | −0,0986 |
| Nuance −150 | 5 scènes | −1,0397 | −1,1430 | +0,1033 |
| Nuance +150 | 5 scènes | +1,0397 | +1,2151 | −0,1754 |

Température : axe `Δlog(R/B)` ; nuance : axe `Δlog(√(RB)/G)`.
À nuance 0, quatre paires strictes seulement (doublon 0037 exclu de
l'agrégat, absence de support 9421). À 2000 K, le clipping médian des
exports réels de cette série vaut environ **5,339 % IPR / 0,049 % LR** ;
0007 demeure à **69,268 % / 0,003 %**. Ce sont les anciens exports IPR
contre les nouveaux LR, pas des exports générés après correctif.

Le décalage froid extrême et la différence de modèle de nuance sont
confirmés malgré la correction des références LR. La conformité n'est
pas validée. Il n'y a pas de preuve de la nécessité d'un poids empirique
spécifique au nom du profil : il faut investiguer la caractérisation du
blanc caméra et l'ordre WB/DCP.

### Ablation sur les neutres retenus — diagnostic, pas rendu de production

Source de vérité :
[18 projections température et variantes LR](../raw/comparaison-wb-20261011/investigation-adaptation-wb-v002-20261011T093106401927Z.json).
Six scènes, trois températures (2000, 3500, 9000 K), trois hypothèses par
état, mêmes pixels d'entrée IPR. L'axe LR est calculé contre son neutre
commun, à nuance +5 ; aucune variation de nuance supplémentaire n'est
appliquée aux neutres IPR As Shot. Les masques restent indépendants entre
les moteurs et états. La CCT de référence provient du décodage RAW
antérieurement mesuré, et non d'une mesure du TIFF sélectionné.

À **2000 K**, médianes des six scènes :

| Projection diagnostique | Écart absolu d'axe `Δlog(R/B)` à LR | Pixels écrêtés |
| --- | ---: | ---: |
| Formule actuelle projetée | 1,6164 | 15,842 % |
| Sans diagonale froide | 0,8255 | 19,817 % |
| Bradford complet sans diagonale | 0,7884 | 19,738 % |

Le clipping LR médian de ces états est environ **0,049 %**. Ce tableau
ne donne pas le clipping d'un nouvel export IPR réel : les trois lignes
sont des projections post-profil sur des TIFF déjà encodés. Les images
hors gamut et valeurs négatives perdues à l'export ne sont pas retrouvées.

**Verdict de l'ablation** : enlever la diagonale froide rapproche l'axe
rouge/bleu mais augmente le clipping dans **les six scènes**. Le Bradford
complet augmente également le clipping partout et dégrade l'écart d'axe
sur certaines scènes, notamment 9950. Aucun de ces remplacements simples
ne satisfait le double objectif chromatique et qualité des hautes lumières.
L'inverse d'une adaptation physique ne suffit pas à caractériser une
balance caméra dans une image déjà transformée en sRGB par un profil.

La prochaine correction doit agir sur les gains du RGB caméra **avant**
les tables et la courbe non linéaire DCP, avec un blanc caméra dérivé de
la caractérisation du profil et des multiplicateurs As Shot. Le profil
intervient par ses matrices, pas par un poids arbitraire choisi selon son
nom. La source `RawDecoder::DcpSource::cameraRgb` déjà retenue pour changer
rapidement de profil est un point d'intégration à examiner, afin d'éviter
un nouveau décodage LibRaw à chaque mouvement de curseur.
Conserver l'identité exacte du rendu au zéro As Shot, contrôler mémoire,
proxy/pleine résolution, cache, export et restauration du sidecar avant
de considérer ce chemin comme intégré ou validé.

Cette orientation est cohérente avec la caractérisation caméra et les
conversions de blanc du
[DNG SDK, `dng_color_spec.cpp`](https://android.googlesource.com/platform/external/dng_sdk/+/master/source/dng_color_spec.cpp).
Elle ne prouve pas le détail du moteur Lightroom ni l'identité des couleurs
entre profils créatifs. **L'intégration pré-DCP reste à réaliser** ; aucune
correction empirique de substitution n'est livrée dans cette passe.

Tests ciblés de cette reprise : **10 tests de comparaison WB, 22 tests
d'analyse de groupe, 3 tests mathématiques d'ablation et 15 tests de sélection
des tests réussis**. Aucune suppression d'image, aucun commit,
fusion ou publication ; les campagnes ci-dessous restent historiques.

## Campagne native du 10 octobre 2026 — intégration des analyseurs

Source de vérité : [40 états natifs](../raw/modernisation-analyseurs/mesure-couleur-v001-20261010T190505433356Z.json),
mesurés le 10 octobre à 19:05:05 UTC. Ce JSON, avec hashes du code, des TIFF,
réglages/provenance, unités, effectifs, audits et progressions, est immuable.
[Vérification des outils](../raw/modernisation-analyseurs/verification-outils-v001-20261010T190542Z.json).

### Entrées et conditions

Manifeste [génération Couleur](../raw/generation-photographique-20261010T120912Z/couleur.json),
RAW 6V1A0043 et 6V1A0327. Camera Neutral requis et appliqué selon le manifeste,
normalisation d'environnement désactivée ; neutres propres à chaque source.
TIFF sRGB 16 bits, dimensions natives, états isolés ; IPR uniquement, sans
nouvelles variantes Lightroom. Les ressources DCP/LCP sont identifiées par
nom mais leurs hashes ne sont pas fournis par ce manifeste : limite explicite.

Lecture memmap native sans conversion 8 bits ni interpolation. Global à pas 8
(pixels réels, pas exhaustif), sRGB linéarisé et Y Rec.709. Les variantes et
neutres correspondent à leurs empreintes déclarées ; hashes des sources de
l'analyseur relus et conformes aux fichiers au terme de cette passe.

### Couverture et observations

| Contrôle | États mesurés, deux scènes | Observation |
| --- | ---: | --- |
| Température | 6 | Deux progressions observées croissantes de l'axe log(R/B) |
| Nuance | 10 | Deux progressions observées croissantes de l'axe vert/magenta |
| Vibrance | 10 | Deux progressions observées croissantes de chroma faible |
| Saturation | 10 | Deux progressions observées croissantes de chroma faible |
| Niveaux de gris | 4 | Deux états activés : écart maximal linéaire entre canaux = 0 sur les pixels mesurés |

40 états `measured`, 0 `diagnostic`, 0 `excluded`. Huit progressions
observées monotones, **sans ajouter d'ancre zéro fictive**. Ce constat ne
garantit ni monotonie pixel par pixel, ni progressivité complète sur toutes
les valeurs, ni protection des couleurs saturées validée visuellement.
Les valeurs détaillées par scène, contrôles et masques sont dans le JSON.

### Décision

Intégration de l'outil Couleur réussie : lecture 16 bits, associations,
audits, provenance et publication JSON fonctionnent sur ce corpus réel.
Fonctionnement photographique complet, qualité visuelle, fidélité physique
et parité Lightroom restent **non évalués dans cette campagne**. Aucune
nouvelle validation des curseurs ni modification du moteur de production.
Aucune image supprimée : `cleanup_eligible=false`.

### Étapes suivantes

Auditer les références LR Camera Neutral/As Shot avec mappings explicites,
étendre gris/carnations et couleurs hors gamut ; contrôler faibles/fortes
chromas, clipping et rendu visuel. Définir les tolérances avant la campagne.
Recette IHM/reset/restauration et éventuelle charte restent nécessaires.
Maintenir les prochaines conclusions ici, les JSON successifs séparément.

## Campagne température/nuance Camera Neutral — 10 octobre 2026

### Conditions et audit des entrées

Six RAW : 6V1A0007, 6V1A0037, 6V1A0043, 6V1A0327, 6V1A9421,
6V1A9950. Les fichiers LR sont dans `tests/image/base/couleur/temperature`
et `nuance`, pas dans les anciens dossiers homonymes à la racine de `base`.
Les valeurs sont lues dans le XMP, jamais déduites du suffixe.

- Température LR : 2000, 3500, 5200, 6500, 9000, 50000 K, nuance constante +5.
- Nuance LR : −150, −100, −50, −25, 0, +25, +50, +100, +150,
  température constante 5200 K. Le fichier `nuance/6V1A0007-9.tif`
  contient encore +100 : +150 manque pour cette scène. 0037 a deux états 0.
- Profil LR : Camera Neutral, WB Custom, réglages Lumières/couleur/détail
  audités à zéro. Ces supports manuels ne sont **pas** des neutres As Shot.
- Exception optique : les quinze fichiers LR de 6V1A9421 ont
  `AutoLateralCA=1` et `LensProfileEnable=1`, contrairement aux cinq autres
  scènes. Conserver cette scène comme diagnostic, pas comme paire stricte
  du seul contrôle température/nuance.

La génération IPR utilise le moteur de production inchangé, Camera Neutral
requis, sidecars ignorés, normalisation d'environnement désactivée. Elle
reproduit les valeurs affichées et conserve un support interne par scène :
5200/+5 pour température et 5200/0 pour nuance. 102 TIFF natifs 16 bits sRGB
sont prévus dans `tests/image/base/couleur/comparaison-wb-20261010-v1`.
Les options de support ont été ajoutées uniquement au générateur et testées.
Un nombre de nuance identique n'est pas un blanc physique identique : IPR
définit son zéro par la WB caméra décodée, LR possède sa propre conversion.

### Investigation du pipeline et hypothèse des profils

Constats dans le code actuel :

1. `RawDecoder` estime la température source à partir de `cam_mul` et
   `cam_xyz` LibRaw, pas des matrices du DCP sélectionné. Elle vaut environ
   4924,21 K sur les scènes déjà générées de ce corpus.
2. `CameraProfileEngine::apply` interpole déjà les matrices et les éventuelles
   HueSatMaps selon l'illuminant. Mais la température passée est celle du
   **décodage source**, non celle du curseur de retouche.
3. Le DCP est rendu avant `renderWorkingImage`. Le curseur température agit
   ensuite sur le RGB linéaire sRGB, après la LookTable et la courbe DCP.
   Changer le curseur ne recalcule donc pas cette interpolation DCP.
4. La transformation Bradford utilisée laisse le gain du cône M à 1 ; ce
   n'est pas une adaptation Bradford complète. La branche froide ajoute
   `diag(1−0,65s, 1+0,19s, 1−0,16s)`, avec
   `s=clamp(log(Tref/T)/log(Tref/2000),0,1)`. À 2000 K, cette diagonale
   vaut (0,35 ; 1,19 ; 0,84). C'est une calibration de l'ancien corpus,
   pas une propriété physique des profils.
5. La nuance est une diagonale RGB normalisée sur la luminance d'un gris :
   elle n'est pas une conversion température/nuance vers un blanc caméra.

Sa signature théorique, avant écrêtage et à température fixe, est
`Δlog(sqrt(RB)/G) = tint×ln(2)/100`. La normalisation de luminance disparaît
dans ce rapport. Les bornes ±150 correspondent donc à ±1,03972 sur cet axe,
quelle que soit la scène non écrêtée. Cette loi universelle ne consulte
aucune matrice de profil ; vérifier son accord avec LR est distinct de
vérifier que la direction vert/magenta fonctionne.

Le DCP local Camera Neutral R6 Mark III possède deux ColorMatrix, deux
ForwardMatrix, illuminants A et D65, LookTable 90×16×16, courbe de 128 points,
offset d'exposition −0,5 EV ; pas de HueSatMaps dans ce fichier. Son inventaire
et son hash devront être conservés dans la campagne. Le nom du manifeste
n'établit pas, à lui seul, une identité cryptographique avec le profil LR.

L'[audit des trois DCP locaux](../raw/comparaison-wb-20261010/audit-profils-wb-v001-20261010T193929806024Z.json)
confirme que Camera Neutral et Camera Standard ont des ColorMatrix et
ForwardMatrix identiques. Adobe Standard partage leurs ColorMatrix mais
diffère sur les ForwardMatrix (écart absolu maximal de coefficient :
0,2983 et 0,2933). Ces écarts de coefficients ne sont pas des ΔE ni des
erreurs perceptuelles. Les tables/courbes diffèrent également : changer de
profil ne se résume pas à changer un gain bleu.

Dans le SDK Adobe, le blanc xy détermine les matrices interpolées en 1/T,
les coordonnées du blanc caméra puis les gains et la transformation vers D50.
Cela soutient l'idée d'une **adaptation dépendante de la caractérisation DCP**,
plutôt qu'un poids arbitraire par profil. Ce code public ne prouve pas les
choix exacts du moteur Lightroom actuel. Source :
[Adobe DNG SDK, dng_color_spec.cpp (miroir AOSP)](https://android.googlesource.com/platform/external/dng_sdk/+/master/source/dng_color_spec.cpp).
Cela ne démontre notamment pas que LR remplace sa courbe tonale à 2000 K :
un changement de blanc caméra en amont d'une courbe non linéaire suffit à
modifier la réponse visible, même si les points de cette courbe restent fixes.

### Direction recommandée, à confirmer par les mesures

Replacer la WB manuelle dans l'espace caméra, avant LookTable/courbe et
limitation de gamut ; convertir température/nuance en blanc xy et dériver
les gains avec la caractérisation du profil. Réévaluer les matrices au blanc
cible et préserver un intermédiaire flottant pour éviter un nouveau décodage
RAW. Vérifier ensuite l'identité As Shot, les gris/carnations, les deux profils
et les extrêmes avant de retirer la calibration empirique historique.

L'objectif est une balance des blancs cohérente et prévisible, **pas le même
bleu forcé dans tous les profils** : leur rendu créatif peut légitimement
changer teinte et saturation. Ce corpus contient Camera Neutral seulement ;
il ne démontre pas une différence causée par un changement de profil.
Une série Adobe Standard strictement identique sur une ou deux scènes
permettra cette comparaison ultérieure, pas une calibration automatique à
partir du nom du profil.

Schéma mathématique de la piste (pas un correctif implémenté) : blanc cible
`Wxy(T,t)`, neutral caméra `n=CMp(T)×Wxy`, gains diagonaux proportionnels à
`1/n`, normalisation d'exposition, puis transformation DCP vers XYZ D50
et rendu du profil. Les calibrations individuelles/AnalogBalance doivent
être prises en compte lorsqu'elles existent. L'interpolation utilise les
bornes d'illuminants du profil ; elle ne doit pas extrapoler aveuglément
jusqu'à 2000 ou 50000 K. Température, nuance et blanc As Shot doivent être
convertis de façon cohérente, sans appliquer deux fois les gains source.

Si `Fp` désigne le rendu non linéaire du profil et `AT` la correction de WB,
en général `AT(Fp(x)) ≠ Fp(AT(x))`. Une simple diagonale placée après la
LookTable/courbe ne corrige donc pas forcément cet ordre des opérations.
Cette architecture pourra utiliser un buffer caméra conservé et un rerendu
du profil ; elle n'impose pas de réouvrir/décoder le RAW à chaque geste.
Elle devra cependant conserver l'identité du neutre actuel approuvé et être
validée sur les autres réglages : changer l'ordre WB/profil peut modifier
les supports des masques tonals, les couleurs saturées et le clipping.
Ne pas livrer ce déplacement comme une simple retouche de coefficients.

### Résultats température — comparaison native

[Mémoire JSON température](../raw/comparaison-wb-20261010/comparaison-temperature-nuance-v001-20261010T193900537890Z.json) :
72 états, 66 mesurés, 6 diagnostics LR pour l'optique 9421, aucun exclu.
Les valeurs ci-dessous sont les médianes des **cinq scènes comparables**,
excluant 9421 ; les agrégats généraux du JSON portent les six scènes.
Les données par scène, dénominateurs, hashes et réglages font autorité.

| Température | Axe Δlog(R/B) IPR | Axe LR | Médiane des écarts IPR−LR | Clipping IPR | Clipping LR |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 2000 K | −4,793 | −2,829 | −1,918 | 5,136 % | 0,072 % |
| 3500 K | −1,280 | −0,984 | −0,296 | 0,153 % | 0 % |
| 5200 K | 0 | 0 | 0 | 0 % | 0 % |
| 6500 K | +0,412 | +0,448 | −0,035 | 0 % | 0 % |
| 9000 K | +0,863 | +0,962 | −0,098 | 0,0002 % | 0 % |
| 50000 K | +1,786 | +1,877 | −0,095 | 0,0004 % | 0,0028 % |

Chaque axe est une variation par rapport au support 5200 K propre au moteur.
Le zéro à 5200 est donc une identité interne, **pas une preuve d'égalité des
rendus IPR/LR**. Médiane des différences ≠ différence des médianes.
Les axes emploient des masques non nuls/non saturés indépendants entre
moteurs/états : à 2000 K la population IPR est fortement réduite. Il ne faut
pas interpréter leur écart comme un gain exact applicable à chaque pixel.
Le clipping est calculé séparément sur tous les pixels natifs échantillonnés
au pas 8 : au moins un canal encodé ≥ 65534/65535. Ce n'est pas un taux de
pixels blancs, ni un taux exhaustif pleine image.

Cas marqués à 2000 K : 0007 atteint 69,268 % de clipping IPR contre
0,003 % LR ; 9950 atteint 32,447 % contre 0,653 % ; 0327 atteint
5,136 % contre 0 %. 0007 conserve seulement 22 093 pixels pour l'axe WB
IPR, contre 504 427 LR. Cela documente une forte réduction du support
colorimétrique utilisable, pas uniquement un bleu esthétiquement différent.
Les valeurs détaillées sont conservées dans le JSON.

Les six progressions IPR observées sont monotones sur cet axe moyen.
Le fonctionnement directionnel est donc présent, mais la fidélité au rendu
LR du froid extrême est insuffisante sur ce corpus. Une monotonie moyenne
n'efface pas le clipping. Les états chauds sont plus proches sur l'axe étudié,
sans constituer une validation visuelle ou une mesure de fidélité physique.

Le soupçon de départ est pertinent comme **piste d'architecture**, mais ces
mesures ne répartissent pas causalement l'erreur entre la diagonale froide,
le modèle de nuance, l'ancre As Shot et l'ordre WB/DCP. Un test d'ablation
contrôlé reste nécessaire avant de choisir des coefficients ou de déplacer
la WB en production. Aucun correctif du moteur de retouche n'est appliqué.

Attention : la compensation froide réduit déjà le bleu (×0,84 à 2000 K).
La supprimer globalement peut augmenter l'écrêtage bleu, même si elle
réduit la sur-atténuation du rouge. La prochaine ablation devra distinguer
compensation diagonale, modèle d'adaptation et ordre des opérations ; ne
pas remplacer un réglage empirique par un autre à partir de la seule moyenne.

### Résultats nuance — comparaison native

[Mémoire JSON nuance](../raw/comparaison-wb-20261010/comparaison-temperature-nuance-v001-20261010T194534501239Z.json) :
109 états (54 IPR et 55 LR), 100 mesurés, 9 diagnostics optiques 9421,
aucun exclu. Les duplications LR +100 de 0007 et zéro de 0037 sont
conservées, sans inventer le +150 de 0007. La progression LR automatique
est indéterminée pour les séries à doublons et pour 9421 diagnostique ;
ce n'est pas une non-monotonie démontrée. Les six progressions IPR sont
monotones sur les moyennes observées.

Médianes sur les paires `measured` des deux moteurs, hors 9421 :

| Nuance | Scènes | Axe Δlog(√(RB)/G) IPR | Axe LR | Médiane des écarts IPR−LR |
| ---: | ---: | ---: | ---: | ---: |
| −150 | 5 | −1,040 | −1,143 | +0,103 |
| −100 | 5 | −0,693 | −0,801 | +0,108 |
| −50 | 5 | −0,347 | −0,386 | +0,040 |
| −25 | 5 | −0,173 | −0,192 | +0,019 |
| 0 | 5 | 0 | 0 | 0 |
| +25 | 5 | +0,173 | +0,190 | −0,017 |
| +50 | 5 | +0,347 | +0,383 | −0,037 |
| +100 | 5 | +0,693 | +0,788 | −0,095 |
| +150 | 4 | +1,040 | +1,261 | −0,221 |

La loi IPR théorique est retrouvée à la quantification près ; LR présente
une réponse plus ample et moins symétrique. À +150, sur quatre scènes,
le clipping médian est 1,015 % IPR contre 0,096 % LR, et le rapport de
luminance moyen à son support vaut environ 0,998 contre 1,215. Ce ne sont
pas des exigences d'égalité de luminance : les normalisations et le rendu
des deux moteurs diffèrent.

Le couplage chromatique confirme une différence de modèle : à +150,
`Δlog(R/B)` IPR reste proche de zéro (quelques millionièmes), tandis que
LR vaut −0,209 pour 0037, −0,107 pour 0043, −0,272 pour 0327 et −0,039
pour 9950. Un simple coefficient multipliant la diagonale de nuance IPR
ne reproduira pas ce couplage rouge/bleu. Les masques sont indépendants,
les différences de profil/blanc source restent des réserves d'attribution.

### Conclusion et suite

L'observation visuelle à 2000 K est confirmée : le problème n'est pas
réductible à un choix esthétique de profil, car le froid extrême introduit
un écrêtage important dans les exports IPR. La direction des curseurs
fonctionne ; leur équivalence LR et leur qualité aux extrêmes ne sont pas
validées. La nuance présente une divergence moins spectaculaire mais réelle,
à la fois en amplitude, luminance et couplage rouge/bleu.

Priorité recommandée : ablation contrôlée de la branche froide, puis modèle
température/nuance → blanc caméra cohérent avec les matrices DCP, avant le
rendu non linéaire du profil et le gamut de sortie. Vérifier le neutre As Shot
approuvé et les Lumières figées avant intégration. Pas de poids arbitraires
par nom de profil, ni de garantie de bleu identique à travers les rendus créatifs.

Les images actuelles suffisent pour commencer ces investigations. Pour
compléter la conformité, réexporter seulement `nuance/6V1A0007-9.tif` à
+150 et les séries 9421 avec les deux corrections optiques désactivées,
ou appliquer/auditer des corrections strictement identiques dans les moteurs.
Une comparaison causale entre profils demandera ensuite, par exemple sur
0007 et 0037, des états 2000/5200/9000 sous Adobe Standard et Camera Neutral,
avec nuance +5 constante et tous les autres contrôles identiques.
Il ne faut pas encore préparer de nouveaux RAW pour cette étape.

102 TIFF IPR générés, 181 états analysés au total, mémoires JSON publiées
sans écrasement. Tests ciblés : 86 tests Python et les deux tests CTest
générateur/export réussis. La vérification documentaire globale relève
quatre liens historiques vers des images supprimées dans le rapport du
29 septembre ; aucun ne concerne cette recette.

Aucun correctif de production n'est livré : seuls les outils de génération
et d'analyse ont changé, sur `patch_couleur` créée depuis `dev`, sans commit,
fusion ou publication automatique. Les TIFF restent conservés à cette passe :
conformité incomplète (+150 manquant, optique non homogène), attribution causale
non isolée et `cleanup_eligible=false`. RAW et neutres communs sont intacts.

## Campagne vibrance/saturation sur 49 scènes — 21 octobre 2026

Première mesure du modèle A1/A2 sur le master Rec.2020 (aller-retour sRGB linéaire,
fusion de `dev` dans `patch_color`, `bcb5d57`). Références LR des 49 RAW de la base
(R6 III, R7, EOS 60D), préréglages autonomes du groupe « IPR Couleur »
(`scripts/make_lr_presets.py`), valeurs −100, −50, −25, +25, +50, +100 ; neutres
communs de `base/neutre`, états IPR rendus en mémoire. Campagnes
[vibrance](../raw/mesure-couleur-v004-20261021T135305383058Z.json) et
[saturation](../raw/mesure-couleur-v004-20261021T141819844745Z.json) : 637 états
mesurés chacune, aucune exclusion.

Médianes sur les scènes de la variation relative de chroma, pixels peu et très
saturés (LR / IPR), et écart absolu médian par scène (IPR − LR) :

| Réglage | Valeur | Peu saturés | Très saturés | Y | Écart peu / très |
| --- | ---: | --- | --- | --- | --- |
| Vibrance | −100 | −0,97 / −0,96 | −0,79 / −0,92 | 0,88 / 1,00 | 0,01 / 0,13 |
| Vibrance | −50 | −0,53 / −0,48 | −0,40 / −0,37 | 0,95 / 1,00 | 0,05 / 0,04 |
| Vibrance | +25 | +0,11 / +0,21 | +0,05 / +0,10 | 1,00 / 1,00 | 0,10 / 0,03 |
| Vibrance | +50 | +0,26 / +0,42 | +0,11 / +0,18 | 1,00 / 1,00 | 0,14 / 0,06 |
| Vibrance | +100 | +0,90 / +0,86 | +0,22 / +0,31 | 0,98 / 1,00 | 0,31 / 0,09 |
| Saturation | −100 | −1,00 / −1,00 | −1,00 / −1,00 | 0,99 / 1,00 | 0,00 / 0,00 |
| Saturation | +50 | +0,45 / +0,44 | +0,22 / +0,23 | 1,01 / 1,00 | 0,03 / 0,02 |
| Saturation | +100 | +0,86 / +0,85 | +0,32 / +0,35 | 1,02 / 1,00 | 0,05 / 0,03 |

Décision de l'utilisateur : **saturation conservée telle quelle** (écart ≤ 0,05).
**Vibrance à corriger** : réponse positive trop forte au départ (LR progressive,
accélérée vers +100), protection insuffisante des couleurs déjà saturées (−100 et
positif), baisse de Y de LR au négatif non reproduite, forte dispersion par scène
à +100 à expliquer avant tout ajustement.

## Historique et compatibilité

Un premier [smoke-test de cinq saturations 0327](../raw/modernisation-analyseurs/mesure-couleur-v001-20261010T185910569266Z.json)
a précédé le renforcement de l'audit des neutres. Il reste une preuve
d'intégration datée, pas un résultat à agréger avec les 40 états suivants.
Les deux identifiants portent `v001` car ce sont deux campagnes/scopes
distincts, chacune horodatée ; leurs hashes de code sont conservés.

Les anciennes analyses [température](analyse-temperature-2026-10-02.md),
[nuance](analyse-nuance-2026-10-02.md), [vibrance](analyse-vibrance-2026-10-02.md)
et [saturation](analyse-saturation-2026-10-02.md) restent historiques.
Leurs diagnostics anciens ne sont pas numériquement comparables à cette
méthode native lorsque lecture, filtre, profil ou neutre diffèrent.
