# Protocole — Réduction de bruit

Version 1 — 10 octobre 2026. Fusion du protocole initial et de la revalidation
v2. Statut : recette prête ; historique v2 fonctionnel, qualité à poursuivre.
Appliquer le [socle commun](protocole-commun.md).

## Corpus et conditions

0043/0327 pour détail faible ISO ; 0624 pour pelage/sangle ; 9890 pour aplats
bruités ; 0112 pour ciel sombre ; 9421/1020 ISO 3200, 9734 ISO 8000 pour peau,
textile et lumière mixte. Contrôler disponibilité et bruit du neutre à 100 %,
pas l'ISO seul. 9950/1020 sont les identifiants confirmés des anciens noms
annoncés 1050/9920. ISO 6400 exact n'est pas obligatoire.

Photo complémentaire utile : même scène fixe à ISO 1600/3200/6400/12800,
trépied, même ouverture/cadrage/lumière, temps ajusté sans bougé ; aplat,
ombre non noire, tissu/cheveux, bord net et plages colorées voisines. Pas de
débruitage longue pose qui changerait la capture. Ces prises ne donnent pas
automatiquement une vérité sans bruit.

## Prises appariées pour distinguer bruit et texture — complément du 2 novembre 2026

Ce complément sert à la caractérisation du bruit et aux cibles du banc
neuronal, pas à remplacer les séries de curseurs ci-dessous. Une image
unique ne permet pas de distinguer sûrement texture fine et bruit.
Les répétitions d'une scène immobile permettent d'observer ce qui varie
entre prises, sans considérer leur moyenne comme parfaitement propre.

### Une scène pilote suffisante pour commencer

Placer dans un même cadrage une feuille ou un carton mat uni, un tissu
beige immobile, des surfaces colorées mates, un texte imprimé ou des
coutures, et une zone d'ombre non totalement noire. Une porte de
réfrigérateur lisse convient si aucun reflet ne bouge. Le tissu est un
échantillon de texture, pas un aplat sans texture. L'aluminium brillant
est moins adapté à l'estimation sur aplat : reflets et gradients doivent
être distingués du bruit. Une couleur grise n'est pas obligatoire ;
plusieurs teintes aident à couvrir les canaux.

Fixer le boîtier sur trépied, ne plus toucher au cadrage ou à la focale.
Faire la mise au point puis la verrouiller ; ouverture, balance des blancs
et mode d'obturateur restent fixes. Désactiver les automatismes qui
changent l'exposition et les réductions de bruit boîtier, notamment longue
pose. Utiliser un déclenchement distant ou temporisé. Pour la stabilisation,
suivre le comportement recommandé pour ce boîtier/objectif sur trépied.

Utiliser une lumière stable : pas de branches devant une fenêtre, soleil
changeant, rideau mobile ou écran. Éviter les éclairages LED à variation
d'intensité/PWM et les lampes scintillantes. Photographier d'abord cinq
prises identiques et vérifier bandes ou fluctuations de luminosité ;
un éclairage secteur peut varier même sans scintillement perceptible.
Un temps plus long peut moyenner ce scintillement, mais n'en garantit
pas l'absence : vérifier les images.

### Quantités et réglages

1. Prendre **32 RAW à ISO100**, sans modifier les réglages dans la rafale,
   avec un histogramme laissant de la marge aux hautes lumières.
2. Sans bouger la scène ni modifier l'ouverture, prendre **16 RAW à chacun
   des ISO1600, 3200, 6400 et 12800**. Ajuster seulement le temps de pose
   entre les séries pour maintenir une luminosité proche. Ne pas changer
   le temps ou l'ISO à l'intérieur d'une série ; conserver les EXIF.
   L'équivalence des moyennes n'est pas garantie par le seul calcul ISO/temps.
3. Si possible, refaire **16 RAW ISO100 à la fin**, déclarés comme contrôle
   de dérive de lumière, pas comme remplacement sélectionné après résultat.
4. En complément de calibration seulement, faire **16 RAW bouchon réellement
   fermé** aux ISO testés, à température et temps de pose comparables à leur
   série. Une pièce sombre n'est pas un noir capteur. Éviter toute entrée de
   lumière ; garder ces prises séparées des scènes éclairées.

Le minimum utile initial est une rafale ISO100 et une rafale ISO3200 ou
6400 de la même scène. Une seule nouvelle composition peut commencer la
qualification ; plusieurs compositions indépendantes seront nécessaires
pour juger la généralisation. Garder une nouvelle scène entière hors de
l'apprentissage et ne pas la réutiliser pour choisir les paramètres.

### Ce que ces prises permettront et ne permettront pas

Conserver tous les RAW, y compris les prises douteuses : les exclusions
doivent être tracées. Le banc séparera par identité les prises sources et
celles qui composent leur référence. Une différence temporelle élimine
le signal de scène **seulement si l'alignement et la lumière sont stables**.
Elle ne révèle pas le bruit fixe commun ; les noirs aident à le caractériser
sans résoudre tous les effets dépendant du signal. Une moyenne réduit le
bruit aléatoire indépendant, mais garde certains motifs fixes et peut
flouter les détails si les prises bougent. Les ombres, reflets, flous,
changements d'éclairage et mouvements doivent donc être contrôlés localement.

## Séries de curseurs

Neutre : luminance 0, couleur 0, gain de netteté 0, IA inactive.
Luminance seule puis chrominance seule : **0/25/50/100** dans le générateur
courant ; 75 en complément manuel. Sur 0043/9890,
ajouter 1/5/10 pour l'activation. Paramètres LR secondaires fixes et enregistrés.
Tester ensuite séparément composantes combinées et couplage bruit/netteté.
Une série LR IA ou un DNG déjà amélioré est une autre approche, pas un témoin
de curseur classique ; absence de balise IA ne prouve pas l'absence d'IA.

Après correctif, exporter un nouveau neutre et vérifier ses différences avec
l'ancien avant d'attribuer un changement au débruitage. Vérifier aperçus,
exports pleine résolution, restauration et toutes les valeurs de support.

## Mesure native

Pixels adjacents 16 bits, pas décimation spatiale ni image redimensionnée.
L'analyseur unifié travaille sur des ROI natives adjacentes, jusqu'à 256×256.
Sans ROI déclarées, il prépare une grille 4×4 de candidats puis retient jusqu'à
six faibles gradients et trois détails distincts selon le neutre. Les coordonnées
sont fixes par référence, pas nécessairement homologues entre moteurs. Ces
choix ne sont pas ceux de l'ancien analyseur (80 candidats, six aplats/quatre
détails) : ne pas comparer leurs agrégats comme s'il s'agissait d'une même méthode.
Les aplats automatiques restent à confirmer visuellement.

Convertir sRGB linéaire ; Y Rec.709, `Co=R−B`, `Cg=G−(R+B)/2`.
Proxy haute fréquence :

```text
HH = (p00 − p01 − p10 + p11)/2
sigma = median(|HH − median(HH)|)/0,67448975
sigma_C = sqrt((sigma_Co² + sigma_Cg²)/2)
```

Le détail Haar est de norme quadratique unité ; le MAD normalisé suppose une
distribution gaussienne. Dématriçage et débruitage corrèlent le bruit : ce proxy
de sortie n'est pas une mesure exacte du capteur. Mesurer ratios au neutre
propre, dérive Y/Co/Cg, gradient et détail par ROI. Le gradient après deux
flous `[1,4,6,4,1]/16` sur masque fort du neutre (quantile 85 %) aide au diagnostic,
mais une diminution peut enlever bruit ou texture : vérifier à 100 %.

Critères : effet/progressivité du curseur, neutralité à zéro, réduction du
proxy sans perte visuelle inacceptable de texture, halos, saignement chromatique
ni désaturation excessive. Fixer les tolérances de qualité avant mesure.
Pas de PSNR/fidélité absolue sans référence propre ; MAE au neutre mesure le
changement, pas l'erreur. Les ROI automatiques par moteur ne sont pas homologues :
pas de MAE pixel-à-pixel LR/IPR ni de classement absolu sur leurs seuls ratios.

## Outils / mappings

```bash
python tests/analyse_bruit.py --base tests/image/base --strict --data-version 1
```

Les valeurs IPR proviennent des rendus en mémoire déclarés, les valeurs LR du
XMP. Chaque série doit avoir une référence propre au moteur. Références ambiguës,
géométrie différente ou état non auditable :
corriger l'entrée ou fournir un mapping explicite à l'analyseur.
Les ROI automatiques ne remplacent pas une sélection sémantique de zones
planes/texturées ; annoter les régions dans le mapping si nécessaire.
Les mémoires des campagnes précédentes restent des preuves, pas une API
de compatibilité à utiliser pour les nouveaux exports.
Rapport unique courant : [analyse Bruit](../analyses/analyse-bruit.md).
Clôture et nettoyage : socle commun. La [netteté](protocole-nettete.md)
est une recette distincte, puis un test de couplage.

## Références de la méthode consolidée

Références conservées du protocole initial (pas une nouvelle revue documentaire) :

- Donoho et Johnstone, *Ideal spatial adaptation by wavelet shrinkage*, 1994,
  [DOI](https://doi.org/10.1093/biomet/81.3.425). Le proxy Haar ne réimplémente
  pas à lui seul cet algorithme complet.
- [Adobe — débruitage manuel et IA](https://blog.adobe.com/en/publish/2023/04/20/lowdown-using-high-iso-settings-in-photography),
  pour la distinction des approches ; les possibilités exactes du logiciel
  utilisé doivent être auditées dans la campagne, pas déduites d'un article ancien.
