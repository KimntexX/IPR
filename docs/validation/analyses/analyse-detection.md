# Analyse de la détection (B5)

Rapport unique du lot B5. Plan : [préparation de B5](../../architecture/detection-objets.md).
Suivi de la validation partielle : [issue 30](https://github.com/KimntexX/IPR/issues/30).

## Point de décision, 5 novembre 2026

**Le redressement d'après les lignes fonctionne sur les horizons, sans réseau ni OpenCV, en 30 ms.** Il refuse souvent, et c'est
voulu. **Il n'est pas raccordé à l'interface, et la perspective n'est pas validée sur de vraies photos.** La suite dépend de
photos de bâtiments et du jugement de l'utilisateur, car une mesure ne dit pas seule si une proposition est la bonne.

## D1 : lignes et proposition de redressement

**Méthode.** `src/detect/LineDetection` : les pixels de fort gradient forment des régions de même direction, et une région
longue, droite et dense devient un segment. Les morceaux alignés d'une même ligne se rejoignent, y compris derrière un obstacle
comme un mât. `src/detect/Straightening` lit ces segments à travers la géométrie et propose une rotation, puis les deux
perspectives si les lignes le permettent. Il refuse quand elles sont trop courtes, trop peu nombreuses ou en désaccord, et une
grande correction demande plus de preuves qu'une petite. Le détecteur est en C++ standard ; la proposition passe encore par
`Geometry`, donc par OpenCV, jusqu'à la migration de ce module.

**Ce que ces mesures prouvent et ne prouvent pas.** La vérité disponible est une rotation que nous appliquons nous-mêmes à une
photo : elle mesure la **répétabilité de la lecture**. Elle ne dit pas si la proposition est le bon niveau pour la scène. Celui-ci
n'a été jugé qu'à l'œil, par moi, sur des planches : ce n'est pas une validation de l'utilisateur.

### Scènes de synthèse

Tests automatiques `lineDetectionTests`, sur une grille photographiée à travers une géométrie connue, avec et sans bruit :

| Vérifié | Tolérance tenue |
| --- | --- |
| Rotation connue, de -8° à 10° | 0,1° |
| Perspective verticale connue, de -40 à 50 | 3 pas |
| Les deux perspectives ensemble | 4 pas |
| Image droite | Aucune correction |
| Bruit seul, cercle, arc lent, éventail de lignes, image plate | Aucune ligne longue ou refus |

### Photos réelles

Preuves : [base](../raw/line-straightening-base-v001-20261105.json),
[lac de Garde](../raw/line-straightening-italie-v001-20261105.json) et
[photos mixtes](../raw/line-straightening-mixtes-v001-20261105.json), révision `a518568`. Les deux derniers corpus sont des JPEG
de l'utilisateur, lus sur des copies privées, un sur deux et un sur neuf : ce sont des échantillons de dossiers, sans sélection.

| Corpus | Photos | Proposent | Refus | Erreur sur rotation connue : médiane, p95, pire | Mesures | Détection |
| --- | --- | --- | --- | --- | --- | --- |
| Base (rochers, escalade, surtout) | 29 | 2 | 27 | 0,010°, 0,082°, 0,082° | 16 | 29 ms |
| Lac de Garde | 24 | 4 | 20 | **2,58°, 3,14°, 3,14°** | 4 | 31 ms |
| Mixtes (côtes, phares, divers) | 61 | 11 | 50 | 0,008°, 0,219°, 0,231° | 39 | 28 ms |

La détection tourne sur une image de 2 048 pixels de côté, sur un poste chargé par d'autres calculs : elle est largement sous
la cible de 250 ms, sans cache ni tâche de fond.

**Vu sur les planches**, pour les propositions de la base et du lac de Garde et six de celles des photos mixtes :

- **Juste :** horizons marins redressés de -3,9° et 2,2° autour d'un phare, autres horizons de mer conservés presque tels quels.
  C'est le cas où l'outil rend service.
- **Faux :** `IMG_5974`, 3,6° d'après deux fils électriques devant une montagne. Ils sont longs, droits et d'accord entre eux :
  rien, dans les lignes seules, ne les distingue d'un horizon. **Une proposition doit donc rester visible et refusable, jamais
  automatique.**
- **Faux, mais faible :** `IMG_8435_DxO`, perspective verticale de 14 en mode complet sur une vue plongeante.
- **Instable :** `IMG_5932`, un bâtiment au bord de l'eau. Sa lecture s'écarte jusqu'à 3,1° selon la rotation, d'où l'erreur du lac
  de Garde : ces quatre mesures viennent de deux photos, et ce chiffre indique une fragilité sans donner de taux général.
- **Voilier (`6V1A0007`) :** avant le réglage du vote, les vagues l'emportaient sur l'horizon et penchaient la photo de 0,8°.
  Avec le seuil actuel, il refuse, faute d'une longueur d'horizon suffisante. C'est moins utile, mais plus sûr qu'une erreur.

**Trois réglages ont été faits sur ces mêmes photos**, après des échecs vus sur les planches : le seuil de preuve (9° d'après des
troncs d'arbres), la longueur de vote (vagues) et la jonction derrière un obstacle. **Ces corpus ne sont donc plus indépendants** :
les taux ci-dessus décrivent un comportement sans prédire celui d'un nouveau corpus. Un corpus neuf reste à mesurer.

### Ce qui reste à vérifier

1. **Perspective sur de vraies photos :** 114 photos, une seule proposition, et fausse. Il faut des photos de bâtiments, façades
   et intérieurs, prises en contre-plongée.
2. **Jugement de l'utilisateur** sur les propositions, dans l'application, une fois le panneau raccordé.
3. **Corpus neuf,** non vu pendant le réglage.
4. Le taux de refus : est-il acceptable à l'usage ? 97 photos sur 114 ne reçoivent aucune proposition, dont 84 faute de lignes
   assez longues. Ces corpus comptent toutefois peu de lignes droites : rochers, végétation, personnes.

## D0 : licences lues à la source

Lues le 5 novembre 2026 dans les dépôts, sans aucun téléchargement de poids :

| Candidat | Code | Poids |
| --- | --- | --- |
| [SAM 2](https://github.com/facebookresearch/sam2) | Apache-2.0 | **Apache-2.0, écrit dans le README** |
| [MobileSAM](https://github.com/ChaoningZhang/MobileSAM) | Apache-2.0 | **Non établi** : la licence ne parle pas des poids |
| [EfficientSAM](https://github.com/yformer/EfficientSAM) | Apache-2.0 | **Non établi** : la licence ne parle pas des poids |

Le corpus, l'annotation et les droits d'entraînement de ces modèles restent à lire avant D2. Ces lignes ne sont pas un avis
juridique.
