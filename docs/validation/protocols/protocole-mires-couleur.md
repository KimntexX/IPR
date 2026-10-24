# Protocole spécialisé — acquisition de mires et caractérisation couleur

Version 1 — **20 octobre 2026**. Statut : **préparation, campagne non exécutée**.
Spécialisation du groupe [RAW / profils](protocole-raw-profils.md), pas un second
protocole de curseur Couleur. Résultats à consigner dans le rapport évolutif
[base colorimétrique](../analyses/analyse-base-colorimetrique.md), mémoires JSON
canoniques et immuables sous `docs/validation/raw/`.

Voir l’[étude d’autonomie et le compte rendu des échanges](../../architecture/references/etude-autonomie-profils-camera.md)
pour les banques, licences, hypothèses et raccordement MCP. Appliquer les règles
de provenance/conservation du [socle commun](protocole-commun.md), mais distinguer
ici l’acquisition physique et le signal pré-rendu des exports de comparaison LR.

## 1. Objectifs et niveaux de preuve

1. **Qualifier les outils** : retrouver la mire, extraire correctement ses plages,
   convertir les domaines et rejouer les calculs sur des données publiques.
2. **Construire une caractérisation** : ajuster la transformation caméra→XYZ
   à partir de RAW et de références connues, sans courbe esthétique cachée.
3. **Valider le rendu** : vérifier les photos indépendantes et les réglages de
   retouche, sans faire de Lightroom la vérité physique.

| Niveau | Matériel / référence | Résultat légitime |
| --- | --- | --- |
| A — pratique | Mire physique connue, référence nominale correspondante, éclairage homogène non mesuré | Profil pratique avec hypothèses et incertitude ; pas de fidélité absolue certifiée |
| B — caractérisation mesurée | Réflectances de la mire réelle et spectre d’éclairage mesurés, instruments adaptés | Référence physique sous ces conditions ; quantifier l’incertitude |
| C — caractérisation spectrale du capteur | Banc spectral et sensibilité mesurée du boîtier | Simulation sous plusieurs éclairages ; confrontation au réel toujours nécessaire |

**Le niveau A suffit pour démarrer.** Un spectroradiomètre n’est pas un achat
obligatoire initial ; emprunt ou prestation de laboratoire conviennent au niveau B.
Une matrice validée sur 24 plages ne garantit pas toutes les matières ni tous les
éclairages : réserver des scènes et si possible une autre mire à la validation.

## 2. Matériel nécessaire

### 2.1 Kit de départ

- Boîtier capable d’enregistrer le RAW natif et batterie/alimentation fiable.
  Priorité proposée : 60D si encore accessible, puis R6 Mark III et R7.
  L’utilisateur possède des CR2 annoncés 60D ; sa possession actuelle du boîtier
  n’est pas confirmée. Un 60D ne devient pas un substitut de calibration du R7.
- Objectif propre, pare-soleil, focale fixe pendant la série ; éviter les
  très grands angles et les bords fortement vignettés pour le premier lot.
- Trépied, support rigide et plan pour la mire, retardateur ou déclencheur.
- **Mire photographique physique** avec identité et références accessibles :
  par exemple une charte Classic 24 plages. Une SG plus riche est un complément,
  pas une condition de départ. Un simple carton gris ne calibre pas les couleurs.
- Éclairage continu stable ou flash reproductible, diffusion et possibilité
  d’empêcher les éclairages parasites. Deux éclairages de natures distinctes
  seront utilisés dans des séances séparées, pas mélangés pendant l’ajustement.
- Une grande surface neutre mate pour contrôler l’uniformité de la lumière,
  mètre et fiche de séance. Un écran calibré aide la revue visuelle, mais ne
  fournit aucune mesure de référence du RAW.

Ne pas imprimer une mire téléchargée ni photographier son affichage à l’écran
comme substitut d’une mire caractérisée. Une impression peut servir de sujet,
mais il faudrait mesurer ses plages pour en faire une référence.

### 2.2 Pour le niveau B

- Spectrophotomètre adapté aux mesures de **réflectance des plages**, avec
  géométrie, étalonnage, mode de mesure et répétabilité documentés.
- Spectroradiomètre/spectromètre étalonné, avec accessoire et géométrie adaptés
  à la **mesure de l’éclairage au plan de la mire**. Une mesure de réflexion
  n’est pas automatiquement une mesure d’irradiance.
- Luxmètre pour l’uniformité et la dérive d’éclairement, en complément. Un
  luxmètre ou un thermocolorimètre ne remplace pas le spectre complet.

Vérifier les fonctions de l’instrument disponible avant de choisir la méthode :
une sonde d’écran n’est généralement pas un lecteur de mire photographique.
Un appareil polyvalent peut convenir à plusieurs mesures si ses accessoires,
unités et étalonnages sont adaptés. Pas de prescription de marque ou budget ici.

## 3. Identifier les références avant la prise

Noter fabricant, modèle, numéro de lot/série si connu, date d’achat et état de
la mire. Conserver la version du fichier de référence, sa provenance/licence,
son empreinte et l’ordre des plages. Ne pas confondre Classic, Passport, SG ou
mire vidéo : disposition, références et nombre de plages diffèrent.

La formulation des ColorChecker a évolué : BabelColor distingue notamment
les références avant/après novembre 2014 [R2]. Une moyenne de plusieurs
chartes n’est pas une mesure individuelle de notre exemplaire. Une référence
nominale peut servir au niveau A, avec cette limite explicitement déclarée.

Pour des valeurs Lab/XYZ, relever illuminant, observateur, blanc et normalisation.
Pour des spectres : longueurs d’onde, unités, pas, domaine, méthode de mesure.
Ne pas déduire une réflectance depuis un JPEG de la mire. Ne pas mélanger les
ordres de plages fournis par deux outils sans une table d’association vérifiée.

## 4. Installer la scène

1. Fixer la mire verticalement, plane et sans vitre. Ne pas toucher les plages ;
   éviter salissures, décoloration et lumière prolongée entre les séances.
2. Placer l’appareil face à la mire, axes approximativement perpendiculaires ;
   vérifier que les quatre coins restent nets, visibles et non masqués.
3. Pour un montage studio, commencer avec les sources hors de l’axe de visée,
   vers 45° de part et d’autre comme **géométrie pratique de départ**. Ajuster
   distance/angles pour éviter les reflets ; ce chiffre n’est pas une garantie
   d’absence de brillance ni une norme imposée.
4. Même type et même réglage spectral pour les deux sources d’une séance.
   Couper les autres lampes ; empêcher les retours de murs, vêtements ou objets
   colorés. Pas de lumière de fenêtre ajoutée à une lampe tungstène.
5. Stabiliser les sources selon leurs instructions et surveiller leur dérive.
   Pour le flash : puissance constante, recyclage complet, pas de TTL variable.
   Pour LED : éviter PWM/bandes, tester la répétabilité ; CCT ou CRI élevé seuls
   ne suffisent pas à garantir un spectre approprié.
6. Viser environ 40–70 % de la largeur de l’image pour la mire, en gardant les
   bords. C’est un choix IPR destiné à disposer de plages assez grandes, pas une
   exigence constructeur universelle. Viser au moins 64 pixels natifs de côté
   utile dans les petites plages pour cette première acquisition.

Un ciel couvert homogène peut servir à la séance pratique « lumière du jour »,
à condition d’éviter ombres et éclairages mixtes et de noter son évolution.
Ne pas l’étiqueter D65 mesuré par simple supposition. Une lampe « 6500 K » n’est
pas automatiquement un simulateur D65. Les spectres réels comptent [R3].

### Contrôler l’uniformité

Photographier une grande surface neutre mate au plan de la mire, avec les mêmes
sources. Comparer les régions homogènes après contrôle du noir, dans le même
signal linéaire : `écart EV = log2(max(signal)/min(signal))`.

**Cible de montage IPR proposée :** écart ≤ 0,10 EV entre centre et régions
de contrôle ; dérive d’une région fixe ≤ 0,03 EV entre répétitions. Ces nombres
sont des objectifs de préparation, pas des normes de conformité ni des preuves
d’une précision DeltaE donnée. Les contributions de vignettage, texture du
support et lumière restent à séparer. Si la cible échoue, améliorer le montage
ou consigner la limite ; ne pas corriger arbitrairement chaque couleur.

La rotation de la mire à 180° aide à distinguer une variation spatiale d’une
différence de plage ; elle complète le contrôle, sans fournir à elle seule une
mesure spectrale ou un flat-field parfait [R1].

## 5. Régler le boîtier

| Paramètre | Série initiale proposée | Motif / précaution |
| --- | --- | --- |
| Format | RAW natif pleine résolution, compression non destructive si proposée | Garder C-RAW, mRAW/sRAW et autres modes pour une campagne distincte |
| Exposition | Mode manuel ; temps seul modifié dans le bracket | Éviter les automatismes entre images |
| ISO | ISO de base natif documenté ; généralement 100 pour les Canon ciblés, à vérifier | Pas d’ISO étendu ou Auto ISO |
| Ouverture | Valeur intermédiaire adaptée à l’objectif, point de départ f/5,6–f/8 | Netteté et profondeur sans supposer un optimum universel |
| Focale / distance | Fixes, relevées | Reproductibilité et optique |
| Mise au point | Faire le point précisément puis verrouiller | Éviter les variations et le flou |
| WB | Réglage fixe, pas Auto ; garder les multiplicateurs et métadonnées | La vraie correction sera explicitée lors du traitement |
| Fonctions automatiques | Éclairage auto, priorité HL, HDR et modes scène désactivés si disponibles | Ne pas mélanger acquisition et rendu ; vérifier l’effet réel sur le RAW |
| Déclenchement | Mode reproductible, retardateur ; choix d’obturateur relevé | Éviter vibration, scintillement et changement de mode de lecture |

Les styles d’image et le choix sRGB/Adobe RGB du boîtier influencent notamment
l’aperçu/JPEG ; ils ne rendent pas les composantes du RAW égales à cet espace.
Les noter, mais ne pas utiliser l’histogramme JPEG comme preuve d’absence de
saturation de la mosaïque. Ne pas imposer la même température **numérique** dans
plusieurs logiciels comme preuve qu’ils appliquent les mêmes gains.

## 6. Régler l’exposition et capturer

1. Faire une image de contrôle et vérifier netteté, reflets et géométrie.
2. Auditer le RAW avec un outil capable de lire les niveaux natifs : noir par
   canal/CFA, blanc déclaré, distribution des plages et pixels saturés. Ce
   diagnostic spécialisé devra être raccordé à l’analyseur ; l’IHM seule ne
   le certifie pas aujourd’hui.
3. Diminuer l’exposition si une plage présente de la saturation dans un canal.
   Une marge pratique de 0,5 à 1 EV sous le premier écrêtage observé peut être
   visée pour l’acquisition, à confirmer sur le signal du boîtier.
4. Garder les plages sombres suffisamment au-dessus du bruit et du noir.
   Si ce n’est pas possible, revoir l’éclairage ou l’exposition : ne pas fusionner
   une image HDR pour fabriquer le témoin initial.
5. Conserver trois captures consécutives identiques. Ajouter une capture −1 EV
   et une +1 EV en ne changeant que le temps ; le bracket sert au contrôle de
   linéarité/saturation, pas à moyenner des plages brûlées. Exclure l’état +1
   des plages devenues invalides et conserver le motif d’exclusion.
6. Tourner la mire à 180°, sans déplacer appareil et éclairage, puis prendre
   une capture à l’exposition nominale. Remettre ensuite la mire en place.

**Ne pas viser 235 dans les codes du RAW.** Les conseils RGB 8 bits d’un
logiciel commercial concernent son rendu/encodage ; ce ne sont pas des niveaux
natifs du capteur et ils ne deviennent pas un seuil IPR [R1].

## 7. Plan de prise de vue initial

Proposition par boîtier ; ce plan n’a pas encore été exécuté :

| Lot | Captures | Usage |
| --- | ---: | --- |
| Éclairage A, par exemple jour stable | 3 nominales + −1 EV + +1 EV + rotation = 6 | Ajustement / répétabilité |
| Éclairage B, par exemple tungstène stable | Même série = 6 | Second illuminant ; appareil/objectif/ISO constants |
| Éclairage C distinct, non employé à l’ajustement | 3 nominales = 3 | Validation des interpolations/hypothèses |
| Scènes naturelles indépendantes | Au moins 5 RAW | Peau, oranges/rouges, feuillage, bleus, gris/HL |
| Contrôle uniforme | Au moins un par installation et contrôles répétés si dérive | Éclairement / vignettage, pas couleur absolue |

Cela représente **15 RAW de mire**, plus les scènes naturelles et contrôles,
par boîtier. Sans troisième lumière disponible, séparer une nouvelle séance de
validation sous conditions indépendantes et déclarer la portée plus limitée.

Les deux premiers éclairages doivent être réellement différents ; ne pas
annoncer un profil double illuminant à partir de deux prises sous la même
lampe avec deux réglages WB. La validation de plusieurs exemplaires d’un même
modèle et l’extension ISO/C-RAW seront des lots ultérieurs. Ne pas ajuster
une loi ISO à partir de ce seul lot bas ISO.

### Cas des fichiers publics et du 60D

Avant acquisition, contrôler licence et accès à l’archive. Utiliser les RAW
originaux plutôt qu’un PNG déjà sous-échantillonné pour tester notre décodeur.
Relire le README : ordre des plages, niveaux noirs déjà soustraits ou non,
références RGB caméra ou XYZ, éclairages mesurés ou reconstruits.

Beyond White fournit des références caméra établies suivant sa méthode :
elles permettent de tester les corrections, pas de calculer directement un
DeltaE00 comme si ces triplets étaient des XYZ [R4]. L’exemple de Jiang
associe un 60D et des données mesurées sous sa licence NC-SA [R5]. Les anciens
CR2 personnels du 60D sont utiles comme validation externe ; sans mire,
ils ne permettent pas de recalculer leur propre vérité terrain.

## 8. Fichiers et informations à me fournir

### Indispensables

- RAW natifs non retouchés, sans conversion Adobe préalable ; préserver
  intégralement les originaux et leurs métadonnées.
- Photo d’ensemble du montage, photo de l’identité/référence de la mire si
  disponible, identification des lumières et de leurs réglages.
- Fiche de séance : date/heure/fuseau, opérateur, boîtier/modèle exact,
  firmware, objectif, focale, ouverture, temps, ISO, mode RAW, WB, sources,
  diffusion, distances/angles approximatifs, conditions ambiantes et incidents.
- Table associant nom du fichier, éclairage, répétition, bracket, orientation
  de mire et rôle « ajustement » ou « validation ». Employer des identifiants
  uniques ; ne pas écraser les noms d’origine sans conserver l’association.
- Fichier de référence de la mire avec provenance, licence, version, ordre
  des plages, illuminant/observateur et unités ; sinon déclarer qu’il manque.

Pour le niveau B : ajouter les mesures spectrales et les métadonnées des
instruments. Distinguer lumière du jour observée, CCT instrumentale, spectre
mesuré et éclairage standard supposé. Documenter toute interpolation spectrale.

Exemple de fiche à remplir — **pas des valeurs réellement mesurées** :

```text
camera_model: à relever dans EXIF
camera_instance_id: identifiant local anonymisé
chart_id / reference_file / reference_license: à renseigner
light_id / light_type / measured_spd_file: à renseigner ou « non mesuré »
source_filename / exposure_variant / repetition / chart_rotation: à renseigner
iso / aperture / shutter / wb_mode / capture_mode: à relever
purpose: adjustment | validation | uniformity
notes: reflets, instabilité, saturation, autre
```

Stocker les acquisitions dans un emplacement dédié, par exemple
`tests/image/corpus/mires/`, **pas automatiquement à la racine de `base`** :
le générateur courant y traite toutes les images. Ce chemin est une proposition,
pas un répertoire créé par le présent protocole. Les références/mesures sont
des données d’acquisition/analyse, pas des manifestes ajoutés à `iprGenerateTests`.

Les exports LR sont facultatifs pour la fidélité physique. Pour comparer les
rendus, même RAW, état explicite, aucune retouche/automatisme, profil réellement
identifié, optique/CA cohérentes, TIFF SDR sRGB 16 bits ICC à dimensions natives,
sans redimensionnement ni netteté de sortie. Ils restent des témoins esthétiques.

## 9. Traitement et analyse — contrat à raccorder

La chaîne ci-dessous est une **procédure cible**, pas une commande disponible :

1. Auditer fichiers, empreintes, modèle/mode et compatibilité du décodeur.
2. Décoder une source caméra linéaire au contrat connu : niveaux noirs/blancs,
   WB déjà appliquée ou non, échelle, orientation et cadrage explicites.
3. Détecter les quatre coins de la mire, vérifier manuellement la grille,
   associer toutes les plages sans inversion après rotation. Éroder les ROI
   pour exclure bords et séparateurs ; commencer au centre de chaque plage.
4. Extraire statistiques par canal et plage : effectif, moyenne/médiane,
   dispersion, saturation et exclusions. Conserver les coordonnées dans le
   domaine natif et tout changement de repère.
5. Ajuster WB et matrice avec les seules données d’ajustement, selon un modèle
   et une normalisation déclarés ; éviter de compter deux fois un gain.
6. Évaluer sur données réservées. Comparer matrice seule et corrections
   résiduelles éventuelles ; préserver neutres, exposition et stabilité.
7. Appliquer séparément l’apparence IPR et le gamut d’affichage. Les mesures
   de caractérisation se font **avant courbe esthétique et gamut écran**, pas
   seulement sur le TIFF final borné. Évaluer aussi le rendu final, séparément.

La passerelle MCP doit appeler le moteur de production et exposer le stade
adéquat ou un résultat équivalent audité. Le générateur actuel de TIFF sRGB
ne fournit pas, par son seul export, une observation pré-DCP. Ne pas retirer
au hasard le profil d’un export déjà rendu pour prétendre retrouver le RAW.
Si ce point d’observation manque, le résultat est « bloqué par contrat moteur »,
pas une raison d’inventer une seconde chaîne photographique dans l’analyseur.

Pour une référence spectrale au niveau B, les XYZ attendus s’obtiennent par
intégration des réflectances et de l’éclairage avec les fonctions colorimétriques
déclarées, puis normalisation explicite. Mettre référence et mesure sous le
même blanc/observateur, avec adaptation chromatique seulement si justifiée,
avant calcul Lab/DeltaE00. Une MAE RGB brute entre domaines différents n’est
pas une erreur colorimétrique valide.

## 10. Critères d’arrêt, validation et mémoire

### Rejet de l’acquisition ou exclusion motivée

- Modèle, mire, référence ou mode non identifiés ; données de droits insuffisantes.
- Plages floues, reflets, ombres, saturation, niveau noir incorrect ou ROI mal
  associées. Un résultat peut exclure quelques plages si la méthode le permet,
  mais déclarer lesquelles et ne pas masquer un mauvais montage global.
- Lumière variable/mixte non prévue, bandes de scintillement ou dérive excessive.
- Références de mauvaises unités, ordre, génération de mire ou domaine caméra.

### Critères numériques à figer avant ajustement

- Répétabilité par plage/canal et de l’exposition ; comparer l’effet du profil
  à ce bruit de mesure avant de promettre une amélioration.
- Erreur WB angulaire dans le bon espace caméra ; DeltaE00 seulement lorsque
  la référence XYZ/Lab et le traitement le permettent.
- Moyenne, médiane, P95, maximum et résultats par plage, éclairage et boîtier.
- Pas de nouvelle saturation non justifiée, de dominante sur les neutres ni
  de cassure visible dans les gradients ; évaluer aussi les scènes naturelles.
- Les seuils finaux DeltaE/EV sont **à valider avant la campagne** selon le niveau
  A/B et la répétabilité ; aucun seuil universel de fidélité n’est déclaré ici.

Enregistrer dans un JSON horodaté/versionné : sources, recettes, versions et
SHA du moteur/passerelle/analyseurs, droits, références, métadonnées de capture,
mesures détaillées, incertitudes, exclusions et verdicts séparés. Conserver
l’état demandé et réellement appliqué de calibration, optique et profil.
Un seul rapport RAW évolutif ; le présent fichier reste le protocole, pas une
seconde mémoire de résultats. Images supprimées manuellement uniquement ;
préserver les RAW et références propres nécessaires à une nouvelle mesure.

## 11. Checklist de départ pour l’utilisateur

- [ ] Confirmer si le boîtier 60D est encore accessible et lire l’identité EXIF des CR2.
- [ ] Lister les boîtiers disponibles, objectifs, mire et éclairages déjà possédés.
- [ ] Choisir le niveau A pour démarrer ou organiser un accès aux mesures niveau B.
- [ ] Identifier la référence de mire et ses droits avant de photographier.
- [ ] Faire une seule série pilote sous une lumière et en contrôler la qualité.
- [ ] Après acceptation du pilote, réaliser les deux séries et la validation indépendante.
- [ ] Transmettre RAW, fiche de séance, références et photo du montage ; pas seulement les TIFF.

## 12. Références

Consultées le 20 octobre 2026. Les valeurs de montage explicitement nommées
« proposées IPR » sont des choix de protocole, pas une citation de norme.

- **R1. Calibrite.** [Capture et profils DNG simple/double illuminant](https://calibrite.com/us/learning-centre/calibrite-profiler-camera-dng/?noredirect=en-US).
  Uniformité, réglages fixes et contrôle d’une mire tournée ; les conseils RGB
  d’affichage ne sont pas des codes capteur.
- **R2. BabelColor.** [Références et variations des ColorChecker](https://babelcolor.com/colorchecker-2.htm).
  Références nominales/moyennées et distinction des générations ; vérifier les droits des données.
- **R3. Anders Torger.** [DCamProf — acquisition et caractérisation](https://rawtherapee.com/mirror/dcamprof/dcamprof.html).
  Réflectances, éclairage, reflets et diagnostics de profils.
- **R4. Cheng et al.** [Beyond White](https://yorkucvil.github.io/projects/public_html/beyond/beyond.html),
  [README des données](https://yorkucvil.github.io/projects/public_html/beyond/readme.txt).
- **R5. Jiang et al.** [Démonstration 60D et mesures spectrales](https://www.gujinwei.org/research/camspec/db.html).
  Conditions NC-SA à examiner avant adoption.
