# Analyse — Empilement

Rapport évolutif de l'empilement antibruit (lot B7, paliers M0 et M1 du
[plan d'empilement](../../architecture/empilement-et-panorama.md)). Les JSON sous
`docs/validation/raw/` font autorité ; ce rapport les interprète. Suivi des validations
partielles : [issue #23](https://github.com/KimntexX/IPR/issues/23).

État au 3 novembre 2026 : **moteur seul**, sur `patch_stacking`, non fusionné. Il produit
un signal caméra en mémoire : pas encore d'onglet Assembler ni de fichier résultat.
Aucune validation visuelle de l'utilisateur à ce stade.

## Choix techniques

| Choix | Raison | Solution écartée |
| --- | --- | --- |
| Fusion en RGB caméra signé, après les corrections d'objectif, avant le profil | Conforme au plan : le bruit est encore linéaire, et les valeurs sous le noir restent, sans biais vers le haut | Après le profil, où sa courbe de tons déforme déjà le signal |
| Recalage écrit pour IPR : décalage global par corrélation de phase, blocs raffinés dans une pyramide, ajustement robuste | N'ajoute aucun module OpenCV à la CI, et va dans le sens de l'indépendance de la couche image | `features2d` et `calib3d` d'OpenCV |
| Homographie si les accords couvrent l'image, similitude sinon | Quatre points groupés sur la Lune ne déterminent pas une perspective | Homographie partout |
| Grille régulière, puis grille dense sur les blocs les plus détaillés si elle ne suffit pas | Une grille régulière répartit les accords ; elle touche à peine un petit sujet | Grille dense partout, où les accords se concentrent sur une seule zone |
| Champ local lisse après la transformation globale, mesuré sur un rééchantillonnage rapide | Une scène avec de la profondeur ne tient pas dans une seule transformation ; les pixels ne sont rééchantillonnés qu'une fois | Transformation seule, qui dédouble les objets proches |
| Un fichier par image dans un dossier appartenant à la tâche, fusion par bandes | Exigence de tampon disque ; la mémoire ne croît pas avec le nombre d'images | Toutes les images en RAM : 16 Go pour cette rafale |
| Décision sur une luminance lissée en 3 x 3, retrait progressif | Une décision par pixel en tout-ou-rien laissait un bord dentelé, observé sur la rafale | Médiane par pixel, rejet franc |
| Gain mesuré par canal contre la référence, appliqué à chaque image | Corrige le scintillement d'une lampe et une balance différente ; au-delà de 25 %, l'image est écartée | Faire confiance aux métadonnées |
| Image de référence choisie par l'appelant ; la mesure prend celle du milieu | Le choix appartient à l'utilisateur dans le plan, et l'image du milieu réduit l'écart aux autres | Choix automatique, qui obligerait à tout décoder avant de recaler |
| Recadrage au plus grand rectangle commun à toutes les images | Demande de l'utilisateur ; tous les pixels gardés ont alors le même nombre de contributions | Couverture partielle, reportée |

## Mesures

Boîtier R6 Mark III, sources lues seulement (empreintes et listes identiques avant et
après). Le bruit est mesuré sur les 10 % de tuiles les plus unies, contre la référence.

| | Rafale à main levée | Lune sur trépied |
| --- | --- | --- |
| Preuve | [`...225332Z`](../raw/b7-stack-audit-v001-20261102T225332Z.json) | [`...230314Z`](../raw/b7-stack-audit-v001-20261102T230314Z.json) |
| Images recalées | 42 sur 42 | 34 sur 34 (`6V1A0016` à `6V1A0049`, ISO 1600) |
| Modèle, accords | Homographie, 64 à 91 | Similitude, 132 à 141 |
| Déplacement global, au plus | 89 px | 34 px |
| Surface gardée | 94,1 % | 97,8 % |
| Bruit, rapport à une image (idéal 1/√N) | 0,156 à 0,160 (0,154) | 0,167 à 0,169 (0,171) |
| Samples écartés | 0,8 % | 0,9 % |
| Décodage, par image | 2,0 s | 1,7 s |
| Recalage et fusion, images décodées | 101 s | 67 s |
| Tampon disque | 16,3 Go | 13,2 Go |
| Pic mémoire | 2,3 Go | 2,3 Go |

Le rapport de bruit suit 1/√N à chaque étape : 0,70, 0,50, 0,36 et 0,26 pour 2, 4, 8 et
16 images sur la rafale. Mesuré autrement, par la différence de deux demi-piles sans
image commune, il reste près de l'idéal : 0,226 à 0,242 pour 0,221. Le gain n'est donc pas
un flou : il ne vient pas du rééchantillonnage seul.

### Effet du champ local, sur la rafale

Écart restant entre une demi-pile et la référence, sur 100 à 106 blocs des tuiles les
plus détaillées :

| | Transformation seule | Avec champ local |
| --- | --- | --- |
| Médiane | 0,57 px | 0,24 px |
| 95 % des blocs | 4,57 px | 2,55 px |
| Pire | 5,35 px | 5,22 px |
| Bruit, rapport | 0,159 | 0,159 |
| Surface gardée | 95,2 % | 94,1 % |

Le champ local corrige beaucoup sans changer le bruit. Il reste des blocs à plus de
2 px, aux bords d'objets proches, où le champ lisse ne peut pas suivre une rupture de
profondeur.

### Détail

Le transfert compare deux demi-piles à la référence par fréquence : 1 signifie aucune
perte. Sur la rafale, il vaut de 0,95 à 0,97 entre 0,125 et 0,275 cycle par pixel. Sur la
Lune, de 0,81 à 0,93 sur la même plage, ce qui traduit la turbulence. Au-delà, la
cohérence tombe sous 0,2 et la mesure ne dit plus rien.

Cette mesure surestime quand la pile est décalée par rapport à la référence : la
transformation seule y donne jusqu'à 3,6, valeur sans signification. Elle ne se lit donc
qu'avec le champ local, et ne remplace pas l'œil.

### Contrôle à l'œil, par l'agent

Rendus à 100 %, avec le profil de la référence. Sur la rafale, le texte « ALL FLOORS »
de l'aspirateur, illisible sur une vue, devient lisible, de même que le mouchetis du sol
et les veines du plancher ; les fils du plumeau restent fins. Sur la Lune, les cratères
du terminateur et les rayons ressortent. Ce contrôle ne remplace pas celui de
l'utilisateur.

## Limites

- **Pas de résultat utilisable dans l'application** : ni onglet, ni fichier, ni recette
  multi-source. Le contenant de la nouvelle image reste à choisir.
- Le bord de la porte garde une très légère rugosité, à vérifier à l'œil.
- Lune : le limbe reste doux. Seuls 10 blocs mesurent le champ local, qui ne corrige donc
  pas la turbulence ; le choix des meilleures vues n'existe pas encore.
- La moyenne reste sur la majorité : un sujet mobile présent dans la plupart des vues
  n'est pas ramené à la référence.
- Aucun sujet mobile réel ni disque plein réel n'ont été essayés : refus d'espace,
  annulation et nettoyage sont couverts par `stackTests`, l'échec d'écriture en cours
  de route ne l'est pas.
- Aucune vérification des réglages de prise : ISO et ouverture différents ne sont
  détectés que par l'écart de gain.
- Deux scènes d'un seul boîtier. Les 81 vues lunaires à ISO 1000, le 60D et l'empilement
  de netteté ne sont pas mesurés.
- La parité du rendu sans empilement n'est pas à refaire : aucun fichier du moteur de
  rendu n'est modifié.
