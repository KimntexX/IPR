# Restauration neuronale — qualification du corpus

## 1er novembre 2026 — scène pilote 6V1A0133

**Conclusion : composition retenue pour poursuivre les acquisitions ; pas
encore une référence propre ni une scène d'entraînement qualifiée.**

Source : `D:/DCIM/101EOSR6/6V1A0133.CR3`, R6 Mark III, ISO 100,
1/15 s, f/5,6, 32 mm. Source laissée intacte. Lecture des métadonnées
CMT1/CMT2, mosaïque décompressée par rawpy/LibRaw 0.22.1 et inspection du
JPEG intégré, dont huit recadrages à taille native. Aucun entraînement,
développement photographique IPR ou changement en production.

### Intérêt des matières

| Élément fourni par l'utilisateur | Utilisation envisagée | Limite |
| --- | --- | --- |
| Feuille blanche | Surface peu texturée claire, gradients et bruit chromatique | Pas une mire de gris/couleur étalonnée ; éclairage non uniforme possible |
| Porte de réfrigérateur blanche et lisse, à droite (précision utilisateur) | Zone claire peu texturée supplémentaire pour observer le bruit et les dominantes | Sélectionner une zone intérieure sans bord ni reflet marqué ; un gradient d'éclairage n'est pas du bruit |
| Tôle d'aluminium | Surface relativement lisse, reflets et détails faibles | Reflets, rayures et salissures visibles : pas un aplat uniforme de calibration |
| Tapis de yoga | Stries régulières, teinte sombre, préservation des motifs | Son relief est du signal, pas du bruit |
| Peluche | Fibres fines blanches et noires, transitions contrastées | Les fibres noires peu visibles exigent une référence indépendante |
| Sac rouge | Texture textile, coutures, plis et couleur soutenue | Ne pas confondre trame et bruit chromatique |
| Pull orange | Maille, coutures et gradients sur les plis | Test de conservation de texture, pas vérité colorimétrique |
| Carton | Fibres et reliefs faibles beiges | Surface non uniforme, zone périphérique moins précise |
| Lino | Motifs imprimés et bords à faible contraste | L'utilisateur confirme que les veines sont imprimées ; ne pas les effacer comme du bruit |

### Netteté et exposition : conclusion prudente

La vue d'ensemble permet de distinguer les objets et leurs structures. Dans
les recadrages JPEG à taille native, les stries du tapis et les coutures sont
visibles ; des détails textiles, du carton et du sol sont plus doux. Cela ne
prouve ni un flou de bougé ni une netteté RAW optimale : le JPEG a subi les
traitements du boîtier. Ne pas qualifier cette prise seule comme référence
parfaitement nette. Une prise sur trépied avec mise au point vérifiée reste
nécessaire, en particulier à 1/15 s et sur cette scène en profondeur.

Sur les quatre plans CFA, 0,00036 % à 0,00453 % des échantillons atteignent
le blanc déclaré par LibRaw (16383). Aucun écrêtage massif à ce seuil n'est
mis en évidence. Ce seuil ne remplace pas le blanc effectif caractérisé du
boîtier, et les rares valeurs maximales ne sont pas identifiées ici comme
reflets, pixels isolés ou autre cause. Les noirs déclarés par cette version
de LibRaw (0/44/130/94) ne sont **pas validés** : aucune estimation du bruit
ni marge d'exposition en EV n'est déduite de ces noirs.

### Prochaine acquisition proposée

Conserver cette composition. Immobiliser le boîtier et tous les objets,
vérifier la mise au point, garder éclairage, cadrage et ouverture constants.
Une ouverture f/8 peut être essayée si les différents plans ne sont pas
assez nets ; refaire alors aussi la référence dans les mêmes conditions.

Prévoir une rafale de référence à bas ISO, puis des rafales à ISO plus élevés
en adaptant le temps de pose pour une luminosité comparable, sans saturer les
surfaces claires. Le nombre et les ISO de la campagne seront figés avant
entraînement. Une moyenne alignée de rafale peut réduire le bruit aléatoire,
pas garantir une vérité sans bruit fixe. Avec ce seul fichier, impossible
de mesurer le bruit indépendant, la stabilité temporelle ou le gain U-Net.

Conserver cette scène entière dans une seule partition du corpus. Les
recadrages et ISO de la même composition ne doivent pas être répartis entre
entraînement et test : cela créerait une fuite de contenu.

Les métriques et les coordonnées des recadrages sont conservées dans le
JSON canonique `neural-pilot-0133-v001-*` sous `../raw/`. Les planches JPEG
de diagnostic restent dans le build dédié, sans nouvelle copie du RAW.

## 1er novembre 2026 — réception des séries intérieure et végétale

**Premier audit terminé : compositions pertinentes et séries candidates,
pas encore des paires RAW d'entraînement qualifiées.** Les 170 CR3
`6V1A0131` à `6V1A0300` sont présents sur `D:/DCIM/101EOSR6`, dont les trois
prises pilotes déjà connues. Chaque fichier a été lu pour les métadonnées et
son aperçu JPEG ; aucun original n'a été déplacé ou modifié.

Mémoire canonique :
[neural-capture-series-v001-20261101T152651Z.json](../raw/neural-capture-series-v001-20261101T152651Z.json).
Les groupes suivent les changements consécutifs de métadonnées : une même
combinaison ISO/temps/focale ne prouve pas l'identité d'une scène.

| Composition observée | Fichiers | ISO | Temps | Nombre |
| --- | --- | ---: | ---: | ---: |
| Intérieur, pilotes à 32 mm | 0131–0133 | 100 | 1/15 s | 3 |
| Intérieur, série à 29 mm | 0134–0151 | 100 | 1/15 s | 18 |
| Intérieur | 0152–0180 | 3200 | 1/200 s | 29 |
| Intérieur | 0181–0194 | 12800 | 1/1250 s | 14 |
| Intérieur | 0195–0209 | 51200 | 1/3200 s | 15 |
| Intérieur, prise isolée | 0210 | 12800 | 1/1250 s | 1 |
| Herbe et feuilles, série à 24 mm | 0211–0243 | 100 | 1/25 s | 33 |
| Herbe et feuilles | 0244–0262 | 1600 | 1/500 s | 19 |
| Herbe et feuilles | 0263–0280 | 6400 | 1/1000 s | 18 |
| Herbe et feuilles | 0281–0300 | 12800 | 1/2500 s | 20 |

Ouverture constante déclarée : f/5,6. La végétation apporte les nervures,
brins fins et détails à faible contraste recherchés ; ne pas assimiler ses
motifs irréguliers à du bruit. Les pilotes à 32 mm ne remplacent pas la
référence intérieure à 29 mm.

### Contrôle préliminaire de stabilité

Recalage euclidien ECC de chaque aperçu contre le premier de son groupe,
largeur maximale 800 px, OpenCV limité à deux threads. Tous les recalages ont
convergé. Dans la série intérieure ISO 100 à 29 mm, le déplacement maximal
est 0,018 px d'aperçu. Dans les quatre séries végétales, il est inférieur à
0,094 px. Ces résultats sont favorables au cadrage global, **sans prouver
l'immobilité locale des feuilles ni la précision à l'échelle du RAW**.

La série intérieure ISO 51200 est moins stable : déplacement maximal
0,539 px d'aperçu et moyenne du gris JPEG comprise entre 0,593 et 0,627
(échelle codée 0..1). L'origine reste à établir : éclairage, traitement du
JPEG ou autre variation. Ce n'est pas une preuve d'instabilité RAW, ni une
raison suffisante pour jeter la série.

Les expositions diffèrent entre séries : le produit ISO × temps n'est pas
constant, et les aperçus changent effectivement de luminosité. Ne pas
soustraire directement une image ISO élevé de la référence ISO 100. Il faut
contrôler la radiométrie linéaire, le recalage inter-séries et les zones
saturées, sans convertir un écart d'exposition en faux bruit.

### Décision et suite

Conserver toutes ces acquisitions ; aucune nouvelle photo demandée à ce
stade. Priorité aux 18 références intérieures et aux 33 références végétales
à ISO 100. La prise 0210 seule ne constitue pas une rafale supplémentaire.
Avant toute moyenne ou entraînement : qualifier mouvement local, netteté,
exposition et saturation dans le RAW, préparer masques valides et références
indépendantes. Garder toutes les variantes ISO d'une même composition dans
une seule partition ; deux compositions ne suffisent pas à démontrer une
généralisation U-Net. Aucun entraînement ou rendu de débruitage n'est lancé
par ce premier audit.
