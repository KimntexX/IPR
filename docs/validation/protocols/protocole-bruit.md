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
   des ISO3200, 6400, 16000 et 51200** pour le banc B4 actuel.
   Les ISO1600, 12800 et 25600 restent des extensions à qualifier séparément. Ajuster seulement le temps de pose
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

## Étude exploratoire — cinq médianes directionnelles

Complément du 20 octobre 2026, séparé des verdicts sur photographies :
`tests/study_directional_noise.py` examine un plan de luminance linéaire
synthétique, ses huit voisins et cinq fenêtres 3×3 dans une empreinte 5×5.
Comparer une version directe à une version conditionnelle (résidu supérieur
à 2,5 σ et six voisins concordants), à une médiane 3×3 et à un filtre gaussien.
Lire les définitions et limites dans le [rapport unique Bruit](../analyses/analyse-bruit.md).

```powershell
python -m unittest discover -s tests -p test_study_directional_noise.py
python tests/study_directional_noise.py --output docs/validation/raw/etude-bruit-directionnel-v002-HORODATAGE.json
```

Remplacer `HORODATAGE` par l’heure UTC d’exécution ; le programme refuse un
fichier de résultat déjà existant. Cette entrée n’est pas un générateur de TIFF
et ne lit/modifie aucune photo. Les mesures JSON conservent scènes, graines,
modèle de bruit, seuils, versions, empreinte de l’analyseur, erreurs et comptes
de détection. Contrôler aplats, rampes, contours, lignes fines et texture ;
rejeter une variante qui dégrade les vrais détails même si elle réduit le bruit
des aplats. Aucun classement absolu LR/IPR ne découle de ce banc.

## Étude luminance — neuf voisinages et fusion d’estimateurs

Extension exécutée le 20 octobre 2026 : `tests/study_luminance_ensemble.py`.
Luminance seule ; cinq supports 3×3 sans le centre et quatre demi-lignes
cardinales de trois pixels. Comparer médiane, précision heuristique, Huber,
candidat directionnel et conservation de l’entrée, puis moyenne simple et
fusion convexe optimisée par les erreurs **sur calibration seulement**.

```powershell
python -m unittest discover -s tests -p test_study_luminance_ensemble.py
python tests/study_luminance_ensemble.py --output docs/validation/raw/etude-luminance-ensemble-v001-HORODATAGE.json
```

Le JSON conserve les poids, la matrice des seconds moments des erreurs,
les scènes/graines distinctes de calibration et de validation, les métriques
par cas et les constantes du modèle. Inclure du bruit corrélé et dépendant
du signal, des lignes et textures tournées. Juger les poids sur validation,
selon le même critère que leur optimisation, sans cacher les régressions
locales par une seule moyenne. Une fusion pondérée n’est pas une probabilité
ni une garantie de meilleur rendu. Le premier essai est **non validé pour
production** ; voir le [rapport unique](../analyses/analyse-bruit.md).

## Étude luminance — fusion locale et abstention

`tests/study_local_luminance.py` ajoute les quatre demi-lignes diagonales aux
neuf supports existants et compare, avec les mêmes six candidats, des poids
globaux, des poids par régime observable et une abstention sur le régime incertain.
Les classes utilisent uniquement le signal bruité (tenseur de structure et
soutien par paires opposées). Les poids sont réglés sur calibration uniquement.

```powershell
python -m unittest discover -s tests -p test_study_local_luminance.py
python tests/study_local_luminance.py --output docs/validation/raw/etude-luminance-locale-v001-HORODATAGE.json
```

Distinguer calibration, régressions déjà connues et nouveau holdout. Le jeu
de graines 401/419 a maintenant été observé ; ne pas le réutiliser comme test
aveugle après réglage. Mesurer MSE par bruit et par structure, amplitude et nombre
de régressions et taux d’abstention. Exiger que le gain sur les impulsions ne
masque pas une dégradation du bruit diffus. Préserver les motifs isolés sans
supposer que toute abstention améliore le résultat. Le
[rapport unique](../analyses/analyse-bruit.md) consigne le verdict négatif pour
la fusion locale et l’abstention de cette campagne, et le gain du candidat
directionnel sur les lignes diagonales historiques. Pas d’intégration de production.

## Étude luminance — détecteur et décision fondée sur le risque

`tests/study_noise_risk.py` utilise les régimes locaux et trois tranches de
résidu pour définir 12 cellules. Comparer l’entrée, une correction diffuse
faible et deux corrections robustes, puis des sélecteurs par risque moyen,
score conservateur par scènes et garde séparée du bruit diffus. Le score
« moyenne + 2 SE » est un diagnostic, pas une borne certifiée. Regrouper les
graines et variantes d’une même scène pour la marge ; conserver les comptes.

```powershell
python -m unittest discover -s tests -p test_study_noise_risk.py
python tests/study_noise_risk.py --output docs/validation/raw/etude-bruit-risque-v001-HORODATAGE.json
```

Inclure explicitement des contrôles sans bruit ajouté et du bruit gaussien
avec impulsions, pas seulement des impulsions sur une image propre. Évaluer
les cinq états séparément : propre, gaussien, corrélé, dépendant du signal,
mélange gaussien/impulsions. Les étiquettes du détecteur désignent uniquement
la contamination impulsionnelle supplémentaire. Conserver tous les compteurs
de confusion aux seuils prévus et les métriques par scène/structure.

Le holdout 601/617 a été observé et devient une régression. Le
[rapport unique](../analyses/analyse-bruit.md) relève un progrès moyen sur le
bruit diffus, mais encore huit cas dégradés et un compromis insuffisant entre
impulsions et détails réels. La table de fréquences n’est pas un posterior
calibré ; aucun seuil ni politique ne sont validés pour production.

## Ajustement exploratoire signal–variance depuis l’audit

```powershell
python -m unittest discover -s tests -p test_study_noise_ptc.py
$noiseStamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
python tests/study_noise_ptc.py --audit docs/validation/raw/audit-paires-bruit-v005-20261022T173747Z.json --output "docs/validation/raw/etude-ptc-exploratoire-v003-$noiseStamp.json"
```

Cet outil lit uniquement le JSON d’audit, pas les images. Il utilise les paires
sans fichier partagé, les zones acceptées et un noir empirique au même ISO,
puis compare ajustement non négatif et libre de `v=a×signal+b`. Conserver
les exclusions, plages, valeurs à la borne et résultats du retrait d’une paire
entière. Pondérer par paire, pas par nombre arbitraire de zones.

Le premier résultat est prometteur pour la pente mais ne qualifie pas le pied
de courbe. Un `b=0` imposé par la contrainte ne prouve pas une absence de bruit
de lecture. Compléter les faibles niveaux éclairés et les paires noires ;
ne pas convertir en électrons ou paramètres DNG sans vérifier les unités du
décodeur. La validation croisée sur les mêmes prises n’est pas un test indépendant
sur un autre corpus. Voir le [rapport consolidé](../analyses/analyse-bruit.md).

## Audit des paires et prises complémentaires pour la caractérisation

### Rejouer l’audit sans modifier les images

```powershell
python -m unittest discover -s tests -p test_audit_noise_pairs.py
$noiseStamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
python tests/audit_noise_pairs.py --corpus "CHEMIN/tests/image/colorimétrie" --output "docs/validation/raw/audit-paires-bruit-v005-$noiseStamp.json"
```

`--previous CHEMIN/ancien-audit.json` est optionnel : il trace les changements
de chemin par SHA-256, sans effectuer de déplacement. L’outil lui-même ne range
ni ne supprime de fichiers. Examiner les champs ISO/exposition, géométrie,
proximité du blanc, dérive, structure de la différence et identité des prises.
Les alternatives de paires ne sont pas nécessairement indépendantes. Ne pas
confondre « sombre d’après les pixels » et « prise avec bouchon confirmée ».

### Matériel et réglages

Utiliser le boîtier visé, un trépied, un déclencheur/retardateur et une surface
mate uniforme éclairée de façon stable, idéalement panneau diffusant ou source
continue à faible scintillement. Éviter un écran, les reflets et les LED à PWM
non contrôlées. Défocaliser légèrement la surface ; garder cadre, focale et
diaphragme fixes. Une mire colorimétrique n’est pas nécessaire pour ce modèle
de variance ; une surface colorée peut compléter les niveaux par canal.

Mode manuel, ISO fixe, mise au point manuelle, RAW pleine définition ; préférer
RAW non C-RAW pour établir une première caractérisation, puis caractériser
séparément les autres modes si utilisés. Garder le même mode d’obturateur,
la même profondeur et les mêmes traitements boîtier. Désactiver réduction de
bruit pose longue/hauts ISO et fonctions automatiques lorsqu’elles sont
désactivables ; noter toute option imposée. Ne pas appliquer LR, DCP, WB,
netteté ou correction optique aux données utilisées pour l’ajustement RAW.

### Série à réaliser par ISO et par boîtier

1. Laisser la température se stabiliser et noter les conditions. Fixer l’ISO
   et l’ouverture. Viser **8 à 12 niveaux d’exposition**, des faibles niveaux
   au proche blanc sans saturation d’aucun canal. Faire varier uniquement la pose.
2. À chaque niveau, déclencher **deux RAW consécutifs strictement identiques**
   (A/B). Ne rien modifier entre eux. Préférer quatre prises pour former deux
   paires indépendantes : une pour l’ajustement, une pour le contrôle.
3. Garder assez de surface uniforme pour plusieurs zones. Vérifier chaque
   canal dans le RAW : un histogramme JPEG non écrêté ne garantit pas l’absence
   de saturation du capteur. Si la source scintille, utiliser un éclairage plus
   stable et des poses adaptées ; ne pas raccourcir arbitrairement jusqu’à
   introduire des variations entre A et B.
4. Poser un bouchon opaque et prendre **au moins deux noirs au même ISO et à
   la même pose**, au début/à la fin de la série. Pour le premier raccord au
   corpus actuel, deux noirs à **1/200 s** donnent une répétition comparable.
   Pour couvrir les poses longues, ajouter des paires noires aux poses
   correspondantes ; ne pas supposer le bruit thermique constant.
5. Conserver les CR3 originaux et toutes leurs métadonnées dans les dossiers
   ISO existants. Fournir un court mémo des réglages, ordre des niveaux,
   identité des noirs, mode RAW/obturateur, éclairage et température approximative.
   Ne pas renommer les anciens fichiers ni les écraser. Aucune exportation TIFF
   ou traitement Lightroom n’est nécessaire pour cette étape.

Les paires enlèvent idéalement le motif fixe de la mesure de variance temporelle ;
elles ne caractérisent pas à elles seules tous les défauts fixes ou les corrélations.
Pour évaluer ces composantes, prévoir des répétitions supplémentaires, puis
examiner le spectre, les lignes/colonnes et la covariance des différences.

### Compléments identifiés sur le corpus du 20 octobre 2026

- **ISO 100** : reprendre prioritairement les trois niveaux des paires
  9652/9653, 9654/9655 et 9656/9657, instables. Les trois niveaux bas suivants
  passent le diagnostic ; viser néanmoins 8–12 niveaux valides au total.
- **ISO 1600, 3200, 6400** : six niveaux éclairés complets candidats par ISO.
  Ajouter au minimum une nouvelle paire noire à 1/200 s par ISO, puis deux
  niveaux éclairés non saturés pour atteindre huit niveaux si l’uniformité
  des images actuelles est confirmée.
- **ISO 16000** : cinq niveaux éclairés candidats ; ajouter une paire noire
  à 1/200 s et au moins trois niveaux pour atteindre huit.
- **ISO 51200** : seulement deux paires passent sur les douze zones/plans,
  deux autres restent partielles. Refaire les niveaux proches du blanc et
  compléter jusqu’à huit niveaux complets ; ne pas supprimer les parties
  utilisables. Une paire sombre candidate existe déjà à 1/200 s, à confirmer.
- Confirmer si **9626/9727** (ISO 100) et **9617/9738** (ISO 51200) ont bien
  été réalisées avec bouchon. Sinon refaire ces paires noires aussi.

Ces quantités servent la caractérisation robuste, pas un seuil réglementaire
universel. Les noirs nouveaux ne résoudront pas à eux seuls l’incohérence des
métadonnées noir/blanc du lecteur : ce raccordement doit être investigué côté
analyse avant de convertir les mesures en coefficients normalisés ou électrons.
Le [rapport unique](../analyses/analyse-bruit.md) conserve les résultats et limites.

## Analyse par plans, zones et résidus

Le banc `tests/study_noise_planes.py` compare une fusion médiane Haar/Laplacien/
DCT globale à des cartes sur fenêtres chevauchantes 32 et 64 pixels. La PCA
reste un diagnostic. Les trois composantes synthétiques ne prétendent pas
reproduire le développement couleur du boîtier. Contrôler séparément niveaux,
désaccord, admissibilité et résidus, sur les motifs propres et chaque bruit.
Ni l’accord entre méthodes ni un score de résidu ne constitue une probabilité
qu’un pixel soit bruité ; mesurer fausses alertes et impulsions manquées.

```powershell
python -m unittest discover -s tests -p test_study_noise_planes.py
$noiseStamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
python tests/study_noise_planes.py --corpus "CHEMIN/tests/image/colorimétrie" --output "docs/validation/raw/etude-bruit-plans-v002-$noiseStamp.json"
```

Lire les RAW réels sans écrire de TIFF ni altérer les mesures PTC existantes.
Inventorier les CR3, puis sonder le premier fichier par dossier ISO sur quatre
plans CFA centraux ; ne pas présenter ce sondage comme une analyse exhaustive
des 130 fichiers ou comme un passage dans le moteur IPR. Copier les données
utiles avant fermeture du décodeur, consigner versions, empreintes et géométrie.
Avant tout ajustement physique, vérifier la cohérence entre données, noir et
blanc, la validité des paires et le bruit déjà appliqué. Des résultats historiques
présents sur disque ne valent pas audit de leurs unités ni validation.

La campagne courante est détaillée dans le [rapport consolidé](../analyses/analyse-bruit.md).
Elle ne valide pas l’intégration du détecteur local. Le DCP couleur et le modèle
de bruit doivent rester distincts : un éventuel `NoiseProfile` du DNG peut servir
de point de départ, sans être supposé présent dans les fichiers Canon.

## Estimation du niveau de bruit — sonde native et comparatif isolé

Comparer la fonction C++ actuelle aux prototypes, sans changer la production :

```powershell
cmake -S tests/noise-estimator-probe -B build-patch_noise-probe -DOpenCV_DIR=C:/opencv/build/x64/vc16/lib
cmake --build build-patch_noise-probe --config Release --parallel 4
python -m unittest discover -s tests -p test_study_noise_level.py
python tests/study_noise_level.py --probe build-patch_noise-probe/Release/noiseEstimatorProbe.exe --opencv-bin C:/opencv/build/x64/vc16/bin --output docs/validation/raw/etude-niveau-bruit-v003-HORODATAGE.json
```

Adapter les chemins OpenCV et l’horodatage UTC à l’environnement ; utiliser un
nouveau numéro de version lors d’une correction, en actualisant également
les champs de version/provenance du script. Le script refuse d’écraser un
JSON existant. La sonde inclut le fichier de production pour appeler sa fonction
privée ; elle n’ajoute aucune API à l’application. L’échange temporaire porte
sur des tableaux numériques synthétiques, pas sur des exports photographiques.

Inclure les aplats, contours, lignes fines, textures tournées/périodiques et
aléatoires, valeurs négatives/HDR et une bordure fixe non bruitée. Croiser avec
le contrôle propre, trois amplitudes blanches, le bruit corrélé, dépendant du
signal et contaminé par impulsions. Conserver tous les résultats par scène,
les graines, effectifs/supports, paramètres et empreintes de l’exécutable,
des sources et de la DLL OpenCV. Vérifier le port float32, la médiane supérieure,
l’exclusion exacte et le pas de sondage contre le C++ natif, y compris sur un
plan assez grand pour déclencher le sous-échantillonnage.

Pour les bordures, distinguer cible sur le domaine actif et RMS sur tout le
plan. Pour les impulsions, distinguer sigma diffus et dispersion totale. Pour
le bruit corrélé, mesurer la réponse du bruit injecté au filtre Haar en plus
de son amplitude pixel. Les contrôles de quantification gardent comme cible
le sigma **préquantification**, qui n’est pas une loi gaussienne garantie après
arrondi. Ne pas assimiler le proxy haute fréquence à un sigma universel.

DCT et PCA sont ici des prototypes distincts des publications : documenter
leurs réglages, calibrer le facteur PCA uniquement sur les plans blancs
indépendants, et conserver les échecs plutôt que choisir une méthode sur sa
seule moyenne. La campagne v003 et ses limites figurent dans le
[rapport unique](../analyses/analyse-bruit.md). Aucun estimateur de remplacement
n’est validé pour production ; les scènes/graines observées deviennent des
régressions pour les futures études.

## Complément de conception : détection et fusion probabiliste

Avant une nouvelle campagne, séparer la détection d’anomalies de l’estimation
de la luminance propre. Définir les hypothèses de structure, le bruit attendu
et les fausses alarmes acceptables. Les cinq fenêtres se recouvrent : calibrer
leur biais et leur covariance sur des données distinctes de la validation,
avec exclusion du centre testé et examen de la corrélation résiduelle.
Comparer moyenne/médiane/GLS/robuste/posterior à détecteur constant, puis les
détecteurs à fusion constante. Ne pas interpréter une p-valeur comme une
probabilité de bruit, ni ajuster une gaussienne sur seulement cinq valeurs
en les supposant indépendantes. Les travaux de Choqueuse sur la détection
sous bruit corrélé et les détecteurs de synchronisation radio alimentent
la conception, sans constituer une validation en imagerie.
Modèles, équations et références dans le complément du
[rapport unique](../analyses/analyse-bruit.md). Étape non exécutée à ce stade.

## Références de la méthode consolidée

Références conservées du protocole initial (pas une nouvelle revue documentaire) :

- Donoho et Johnstone, *Ideal spatial adaptation by wavelet shrinkage*, 1994,
  [DOI](https://doi.org/10.1093/biomet/81.3.425). Le proxy Haar ne réimplémente
  pas à lui seul cet algorithme complet.
- [Adobe — débruitage manuel et IA](https://blog.adobe.com/en/publish/2023/04/20/lowdown-using-high-iso-settings-in-photography),
  pour la distinction des approches ; les possibilités exactes du logiciel
  utilisé doivent être auditées dans la campagne, pas déduites d'un article ancien.

## Reprise du 22 octobre 2026 — noirs partagés et affectation ISO

L'audit courant lit aussi `protocole/blacks` et affecte les prises aux six
groupes par **ISO des métadonnées CR3**, sans déplacement physique. Le booléen
`storage_folder_iso_matches` conserve l'anomalie de rangement éventuelle.
Dix RAW stockés sous `iso-3200` sont à ISO 1600/6400 ; le
[rapport Bruit](../analyses/analyse-bruit.md#bruit-20261022-reprise)
donne leurs noms et les paires défectueuses.

Deux noirs déclarés par l'utilisateur sont présents et stables à chaque ISO.
L'ajustement utilise leur moyenne empirique et conserve leur variance comme
contrôle, sans forcer cette variance à devenir l'interception b. Les alternatives
partageant un fichier ou son empreinte ne sont pas des répétitions indépendantes.
Les plans sans paire noire stable sont exclus explicitement.

L'[audit v005](../raw/audit-paires-bruit-v005-20261022T173747Z.json) et la
[PTC v003](../raw/etude-ptc-exploratoire-v003-20261022T173823Z.json) remplacent
les anciennes références pour cette reprise. La v004 et la PTC v002 gardent
la première passe avant réaffectation des ISO. Ne pas écraser ces preuves.

La liste de prises à compléter du 20 octobre est **historique** : les nouvelles
paires noires sont reçues, et sept des huit nouvelles paires éclairées ISO 51200
passent les douze contrôles. Ne pas demander de refaire ces sept paires.
9803/9804 reste partielle ; certaines reprises d'autres ISO sont instables.
Reprendre seulement les niveaux réellement nécessaires après revue des zones.

Le seuil de dérive actuel porte sur les DN avec piédestal ; une future
qualification des très faibles signaux devra aussi considérer le signal utile.
Le décalage entre noir déclaré par le décodeur et noir empirique reste une limite
du banc. Aucune conversion en électrons, normalisation DNG ou calibration de
production n'est validée par ces nouvelles prises.

## Approfondissement du 22 octobre 2026 — modèles et transfert

`tests/study_noise_transfer.py` prend `--ptc`, `--audit`, `--output` et
éventuellement `--previous`. Il vérifie l'empreinte de l'audit source de la PTC,
refuse l'écrasement et ne lit/écrit aucune photographie. Ses entrées courantes
sont la PTC v003 et l'audit v005 ci-dessus. Il conserve les points retenus,
ajuste cinq modèles expérimentaux et valide par retrait d'une paire entière,
d'un temps de pose et d'une journée de prises. Les noirs restent communs :
ne pas qualifier cette validation d'indépendante vis-à-vis des noirs.

Comparer affine absolu, affine à noir fixé, affine relatif, relatif robuste
et quadratique non négatif. Chaque paire a le même poids total, réparti entre
ses zones ; la métrique publiée est une erreur relative de **variance**,
pas un écart de pixel ni une qualité de débruitage. Conserver les prédictions,
plages d'entraînement et drapeaux d'extrapolation, ainsi que les sensibilités
aux seuils de dérive RAW et rapportée au signal utile.

La [mémoire transfert v002](../raw/etude-transfert-bruit-v002-20261022T183811Z.json)
et le [rapport unique](../analyses/analyse-bruit.md#bruit-20261022-transfert)
documentent les résultats et limites. `tests/plot_noise_transfer.py --input
CHEMIN_JSON --output-dir DOSSIER` régénère les deux SVG dérivés ; Matplotlib
est requis pour les figures, pas pour l'ajustement.

Cette campagne ne valide ni la détection d'un pixel bruité, ni une conversion
directe des coefficients RAW vers le proxy Haar/MAD de production. Les nouvelles
métriques spatiales robustes nécessitent de relire les RAW conservés.

## Prototype du 23 octobre 2026 — historique

Prototype non intégré au moteur ; ses scripts et sondes sont retirés de `patch_noise` le 12 novembre 2026 (historique Git et archive hors dépôt). Texte d'origine conservé :

Sur le build `patch_noise`, ouvrir une scène existante, cliquer sur
**Estimer le bruit**, puis comparer **Réduction globale** à 0/25/50/75/100.
Le bouton seul ne doit changer ni l'image ni les réglages. Les deux contrôles
avancés restent indépendants ; une restauration/copie de valeurs différentes
ne doit pas les remplacer par le minimum affiché du global.

Examiner une ombre uniforme, une texture fine, une frontière de couleur et
des valeurs HDR si disponibles. Utiliser `Ctrl+1` pour le pixel de l'aperçu,
Espace pour l'origine temporaire lorsque la vue a le focus, `Ctrl+Maj+Y`
pour le comparatif. Le zoom de l'aperçu ne remplace pas l'examen d'un export
pleine résolution : ce dernier est estimé à sa propre résolution.

Vérifier changement d'image pendant une analyse, retouche amont, sauvegarde
`Ctrl+S`, fermeture/réouverture et réinitialisation à zéro. Une ancienne
estimation ne doit pas être publiée sur la nouvelle cible. Le build ne modifie
aucun fichier source ; les retouches utilisent la sauvegarde normale `.ipr`.
La [campagne d'intégration](../analyses/analyse-bruit.md#bruit-20261023-prototype)
ne vaut pas validation de ces scènes ni calibration du capteur.

## Banc naturel CFA B4 — qualification avant apprentissage long (8 novembre 2026)

Maintenir les preuves dans le rapport unique de
[restauration neuronale](../analyses/analyse-restauration-neuronale.md), avec
JSON canoniques horodatés. Une qualification partielle renvoie à
[l’issue #14](https://github.com/KimntexX/IPR/issues/14) et ne vaut pas
validation de production.

1. **Provenance et données.** Vérifier les SHA-256 originaux, les métadonnées,
   le boîtier et les modes de capture ; grouper par scène et séance avant
   recadrage. Un fichier ou patch différent ne prouve pas une scène indépendante.
   Séparer apprentissage, développement connu et test final. Ne pas présenter
   une photo déjà inspectée comme une nouvelle réserve.
2. **Contrat numérique.** Travailler sur CFA signé avant gains, dématriçage,
   DCP et LCP. Vérifier origine visible, disposition physique, noir, blanc,
   diviseur et correspondance avec le décodeur natif. Les valeurs metadata de
   rawpy ne suffisent pas : conserver un rapprochement pixel à pixel avec
   l’entrée IPR sur un périmètre déclaré. Les verts physiques restent traçables.
   Conserver les négatifs et valeurs >1 ; le blanc de normalisation n’est
   pas à lui seul le seuil de saturation physique.
3. **Références.** Qualifier netteté et saturation et estimer le bruit résiduel
   des images bas ISO à partir des données disponibles. ISO100 est un proxy
   possible, pas une vérité parfaite obligatoire. Déclarer la part d’incertitude,
   les extrapolations et la limite des paires face au bruit commun. Une scène
   mobile peut servir de contenu à bruit ajouté sur une même image ; elle
   ne devient pas pour autant une rafale propre moyennable sans recalage.
4. **Bruit.** Séparer le banc contrôlé à bruit ajouté du banc physique. Pour
   le physique, vérifier lois par plan/ISO/mode, niveaux noirs, composantes
   temporelles et fixes, lignes/colonnes, quantification et compression.
   Ne pas additionner deux fois le bruit de lecture. L’entrée sigma doit
   être disponible de la même manière à l’inférence ; pas de carte spatiale
   calculée depuis la cible propre, sauf expérience oracle explicitement isolée.
5. **Comparateurs.** Figer identité, références CFA et paramètres sur le
   développement avant ouverture du test final. BM3D reste externe et non
   livré, avec version/licence/provenance ; son installation ne qualifie pas
   automatiquement un étalon signé ou VST. Vérifier finitude, répétabilité,
   biais et comportement aux frontières. Comparer le classique IPR dans un
   domaine RGB commun, pas directement à une PSNR CFA.
6. **Décision.** Réserver la cible de gain médian apparié par scène ≥0,5 dB
   aux comparaisons CFA qualifiées dans chaque bande 3200–6400 et 16000–51200.
   Fixer auparavant les tolérances noirs et spectre résiduel ; conserver les
   planches pour validation visuelle de l’utilisateur. Distinguer progrès,
   régression et résultat non concluant. Une mesure de débit n’est pas une
   mesure de qualité. Le budget 20,8 s / 8 threads vise l’ouverture par
   défaut, pas le traitement neuronal optionnel.

Les scripts `audit_natural_bench.py`, `audit_native_cfa_bridge.py` et
`prepare_native_normalized_cfa.py` réalisent les contrôles numériques actuels
sur les sources déclarées. Ils ne qualifient pas seuls toute cette liste.
L’audit n’utilise pas les scores neuronaux pour réassigner les images.


### Compléments physiques prioritaires — 8 novembre 2026

Cette acquisition complète les fichiers existants ; elle ne demande pas de
refaire les séries noires 3200/6400/51200 déjà exploitables. La qualification
initiale vise le **R6 Mark III, mode Canon RAW**, aux ISO explicitement testés.
Elle ne vaut ni pour C-RAW, ni pour les autres boîtiers, ni pour tous les
modes d’obturateur/températures. Les critères modèle +0,5 dB restent distincts
de la qualification de son banc de mesure.

**Lot reçu le8 novembre :** 52 RAW àISO100 et55 àISO16000, tous au1/200s.
Les splits32/20 et32/23 passent la répétabilité ; aucune nouvelle série
à ces deux ISO n’est demandée maintenant. Pour un futur lot de calibration,
viser 32 RAW bouchon réellement fermé à ISO100 et
32 à ISO16000, à **1/200 s**, pour compléter les deux prises actuelles de
chaque ISO. Garder la configuration de lecture, le mode d’obturateur et le
mode d’entraînement de la scène, sans basculer en rafale rapide qui pourrait
modifier la lecture du capteur. Si la référence ISO100 utilise 0,8 s, ajouter
16 noirs ISO100 à 0,8 s pour contrôler l’effet du temps de pose. Ne pas
substituer une scène sombre. Laisser la température se stabiliser, conserver
les EXIF, et noter tout changement d’obturateur ou de réglage depuis les
anciennes séries. Conserver tous les RAW, sans écraser un fichier existant.
Le dossier courant est `tests/image/colorimétrie/protocole/blacks`.

**Références éclairées :** commencer par une courte scène pilote (par exemple
5 ISO100, 5 ISO51200, puis 5 ISO100 de contrôle) avant de multiplier les prises.
La scène associe aplat mat, ombre, couleurs et détails fins immobiles selon
la rubrique « Une scène pilote suffisante ». Vérifier le placement avec le
banc avant les séries complètes de 32/16 prises décrites plus haut.
Le pilote inspecté est de la calibration, pas un futur test final inédit.

Faire le point une seule fois puis passer **en mise au point manuelle** ;
ne plus modifier focale, distance ou ouverture. Stabiliser le trépied et
utiliser télécommande/intervallomètre/contrôle distant pour éviter de déplacer
le cadrage en changeant l’ISO. Ne pas moyenner deux cadrages comme une vérité
propre. Une moyenne ISO100 plus tardive est un contrôle annoncé, jamais
un remplacement choisi pour faire passer l’ancien résultat.

Garder l’éclairage stable, sans branches/rideaux mobiles devant la fenêtre,
reflets changeants ou scintillement/PWM. Désactiver les automatismes qui
modifient exposition et débruitage boîtier. Conserver l’ouverture et une marge
sur les hautes lumières. À titre d’exemple, si ISO100 / 0,8 s donne une
exposition adaptée, les temps indicatifs sont : ISO3200 / 1/40 s,
ISO6400 / 1/80 s, ISO16000 / 1/200 s et ISO51200 / 1/640 s.
Ce calcul n’est pas une garantie de radiométrie identique ; les RAW seront
contrôlés avant admission. La météo ou les nombres ISO/temps ne suffisent
pas à certifier la stabilité.

Les données seront séparées par scène/séance et par rôle (calibration,
apprentissage, développement, réserve) avant apprentissage. Une qualification
physique complète exige aussi bruit fixe/chaud et lignes/colonnes sur le
périmètre déclaré, loi dépendant du signal, correspondance avec l’entrée IPR,
incertitude de référence et comparateurs qualifiés. Les nouveaux noirs ne
garantissent donc pas à eux seuls le passage de tous ces contrôles.


### Paires éclairées manquantes pour clore le banc — 8 novembre 2026

Bilan de reprise (Claude, `0071e8b`) : variance et corrélations du simulateur
équivalentes, mais 15 rapports spectraux sur 20 non concluants faute de
précision, avec 1 à 4 paires éclairées de contrôle par ISO. La demi-largeur
d'intervalle (5 à 9 % pour une borne de 9,5 %) décroît en 1/√paires : il faut
environ **16 paires par ISO** (estimation). ISO 12800 et 25600 n'ont aucune
pente éclairée. Les noirs existants suffisent ; aucun noir n'est demandé.

| ISO | Paires éclairées à prendre | But |
| --- | ---: | --- |
| 3200 | 16 | spectre, ISO d'entraînement |
| 6400 | 16 | spectre, ISO d'entraînement |
| 12800 | 16 | première pente éclairée |
| 16000 | 16 | spectre (1 à 2 paires aujourd'hui) |
| 25600 | 16 | première pente éclairée |
| 51200 | 16 | spectre et écart de variance de +2,6 à +5,0 % |
| 100 | 8 (facultatif) | spectre du domaine des cibles |

Soit 96 paires, 192 RAW (208 avec ISO 100).

**Scène :** un aplat mat uniforme (carton gris ou blanc mat, mur blanc), sans
texture visible : mise au point manuelle au plus près pour flouter le grain du
support, cadrage rempli par l'aplat. Lumière continue stable et sans
scintillement : jour couvert stable ou lampe halogène/LED sans PWM ; pas de
néon. Trépied, télécommande ou retardateur 2 s, stabilisation désactivée.

**Boîtier :** R6 Mark III, **Canon RAW (pas C-RAW)**, mode M, ouverture fixe
(f/5,6 comme les séries existantes), même mode d'obturateur que les séries
précédentes, vue par vue (pas de rafale rapide). Désactiver réduction du bruit
haute sensibilité, réduction du bruit en pose longue, priorité hautes lumières
et correcteur automatique de luminosité. Laisser le boîtier se stabiliser.

**Niveaux :** pour chaque ISO, régler d'abord la vitesse T0 qui place l'aplat à
environ deux tiers de l'histogramme RAW, sans alerte de hautes lumières. Prendre
ensuite **deux paires** (quatre vues identiques) à chacun des huit niveaux
T0, T0/2, T0/4 … T0/128. Une paire = deux vues consécutives aux mêmes réglages.
Si la vitesse demandée dépasse les limites du boîtier ou passe sous 1/4000 s,
**baisser la lumière** (éloigner ou atténuer la source, filtre ND) plutôt que
changer l'ouverture. Ne jamais modifier cadrage, mise au point ou ouverture
pendant une série.

**Rangement :** une série par ISO dans `tests/image/colorimétrie/protocole/iso-<ISO>`
(créer `iso-12800` et `iso-25600`), sans écraser de fichier existant, EXIF
conservés. Noter la source de lumière et tout changement de réglage.

**Ce que ces prises permettront :** rejouer `qualify_physical_spectrum.py` sans
changer ses bornes, ajuster les pentes à 12800/25600 et étendre l'apprentissage
à ces ISO. Les premières paires de chaque ISO (chronologiquement) iront à
l'ajustement, les suivantes au contrôle, avant toute lecture des résultats.

### Contrôle spectral physique — correctif du 8 novembre 2026

Ne pas décider de la conformité d’un modèle sur l’erreur absolue d’un seul
périodogramme sans contrôle nul. `qualify_physical_spectrum.py` utilise des
effets signés, des poids égaux par paire et une incertitude simultanée issue
de Monte-Carlo. Les64 répétitions de contrôle de l’instrument sont séparées
des64 de calibration. Les seuils, graines, coefficients, sources et limites
figurent dans le manifeste avant calcul. Les contrôles historiques restent
connus : ce correctif de méthode ne crée pas de nouvelle réserve.

Exiger la détection des erreurs injectées et contrôler les faux rejets.
Un intervalle qui recouvre à la fois la zone acceptable et son extérieur
donne «non concluant». Augmenter le nombre de simulations ne remplace pas
l’information physique manquante dans les prises. Pour agrandir lesROI,
conserver leurs centres, vérifier à nouveau stabilité/uniformité/saturation
et enregistrer les exclusions sans les choisir sur la PSD. La qualification
marginale reste distincte des structures fixes et du bruit conjoint.
