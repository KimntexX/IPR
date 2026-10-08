# Protocole — Base colorimétrique — v1

**Date :** 6 octobre 2026. **Statut :** proposition prête pour la phase A ;
mesures de fidélité conditionnées à une référence physique.
**Objectif :** vérifier le rendu à l'ouverture indépendamment des réglages,
puis distinguer fidélité colorimétrique, conformité du moteur DCP et préférence
esthétique. Il n'est pas nécessaire d'avoir validé tous les réglages.

## 1. Trois questions, trois niveaux de preuve

| Étape | Référence | Conclusion possible |
| --- | --- | --- |
| A — Photos existantes | IPR / Lightroom, paramètres et profils tracés | Écarts de rendu, dominantes suspectes, clipping et défauts de cohérence ; pas d'exactitude absolue. |
| B — Charte photographiée | Valeurs de référence de la charte, conditions connues | Fidélité de la base dans les conditions mesurées. |
| C — Prises identifiées | Fabricant, modèle, matière et code couleur ; idéalement mesure de la prise réelle | Vérification en situation et cohérence des familles de couleurs ; pas une charte de remplacement. |

Un rendu Adobe peut être intentionnellement contrasté ou saturé : Lightroom
n'est pas la vérité physique. Ses profils sont une base de rendu, et non
une absence de traitement [S1]. Un écart faible avec Lightroom et une bonne
fidélité à une charte sont deux résultats distincts.

## 2. Reconnaissance de la banque existante

Quinze TIFF Lightroom du dossier `tests/image/base/neutre` ont été examinés
en lecture seule : métadonnées et planche de reconnaissance 8 bits, sans
mesure de fidélité. Les RAW correspondants existent directement dans `base`.
Les sujets ci-dessous viennent de cette inspection visuelle ; ISO et réglages
proviennent des TIFF Lightroom, pas d'un nouvel audit des RAW.

| Priorité | RAW | ISO | Intérêt / précaution |
| --- | --- | ---: | --- |
| A | `6V1A0037.CR3` | 100 | Végétation, verts de plusieurs intensités, ombres et lumière extérieure ; couleurs naturelles sans valeur Lab connue. |
| A | `6V1A0043.CR3` | 100 | Fleur violette/magenta et feuillage : transitions, violets et teintes très saturées. |
| A | `6V1A0327.CR3` | 100 | Crashpads cyan, vert et rouge, roche orangée et peau ; les crashpads ne sont pas des prises d'escalade. |
| A | `6V1A0007.CR3` | 100 | Voilier, eau et ciel gris : faible chroma et dominantes ; voile/nuages ne sont pas des gris certifiés. |
| A | `6V1A9950.CR3` | 100 | Ciel bleu, nuages, feuillage, terrain et ombres : référence extérieure complémentaire. |
| A / C | `6V1A9421.CR3` | 3200 | Nombreuses prises colorées sur mur jaune, textile et peau ; meilleure candidate observée pour les prises, avec bruit et éclairage de salle comme facteurs de confusion. |
| Complément | `6V1A0767.CR3` | 2500 | Vue large d'un mur avec de nombreuses prises ; lumière chaude/non uniforme, faible surface par prise. |
| Complément | `6V1A9377.CR3` | 1250 | Prises sous dévers jaune, zones sombres et éclairage de salle ; comparaison de matières et d'ombres. |
| Stress | `6V1A9734.CR3` | 8000 | Peau, cheveux, textile, prises et fenêtres ; lumière mixte et bruit : pas une scène d'étalonnage primaire. |
| Complément | `6V1A9119.CR3` | 100 | Grimpeurs, peau, textiles et roche : contrôle visuel de carnations en extérieur. |
| Stress | `6V1A9944.CR3`, `6V1A9947.CR3` | 100 | Portraits à contre-jour : coloration des ombres et séparation couleur/tonalité, pas une référence de peau connue. |
| Complément | `6V1A0106.CR3` | 100 | Ciel bleu, eau, pont et hautes lumières ; alternative à 9950. |
| Complément | `6V1A0624.CR3` | 400 | Textile sombre, rocher et lichen jaune/orangé ; détails et faibles luminances. |
| Stress | `6V1A8988.CR3` | 400 | Intérieur sombre et fenêtre : balance des blancs locale/mixte, pas une scène de calibration globale. |

**Première livraison recommandée : six RAW déjà présents → douze TIFF frais**,
un IPR et un Lightroom par scène : **0037, 0043, 0327, 0007, 9950, 9421**.
Ne pas exporter toutes les scènes ni des variantes de curseurs à ce stade.

### Pourquoi des neutres frais ?

L'[inventaire daté](../raw/inventaire-base-colorimetrie-2026-10-06.json)
constate, sur les quinze références examinées :

- profil Lightroom **Adobe Standard**, espace embarqué **sRGB**, EV 0 ;
- corrections de profil d'objectif activées (`LensProfileEnable=1`) ;
- température 5200 K et nuance +5 dans les métadonnées de ces exports ;
- sur **0037, 0007, 0767, 9119, 9944 et 9947** : netteté 40 et réduction
  de bruit couleur 25 ; sur les neuf autres : ces deux valeurs sont à zéro.

Ce relevé ne certifie pas que tous les autres réglages sont neutres et
ne constitue pas une conclusion sur les anciennes campagnes. Pour cette
nouvelle campagne, repartir d'un état explicite. Ne pas confondre les anciens
suffixes `-3` avec le nouveau protocole. Ne rien écraser ni archiver ici.

La bibliothèque locale `profiles/dcp` ne contient actuellement que `.gitkeep`
et `profiles/index.json` est vide. Cela **ne prouve pas l'absence de profil
utilisé** : IPR peut chercher dans les autres racines, dont l'installation
Adobe. Relever le DCP effectivement choisi avant d'attribuer un défaut au profil.

## 3. Phase A — Procédure d'export immédiate

### Préparer la campagne

**Alternative automatisée pour les exports IPR :** utiliser le
[générateur de tests](../tools/generateur-images-tests.md), avec `neutral`
et les six RAW d'origine. Il ignore les sidecars sans les écrire ; les copies
ci-dessous deviennent inutiles pour ce chemin hors IHM. Choisir un nouveau
dossier de sortie et conserver son manifeste (profils, WB, paramètres).
La préparation Lightroom et les contrôles du présent protocole restent
nécessaires ; ne pas mélanger les numéros de cette campagne aux anciennes.

1. Créer un dossier distinct, par exemple
   `tests/image/base/base-colorimetrie/v1/`, avec `sources/`, `neutres/`
   et `metadata/`.
2. Pour préserver les anciens `.ipr`, utiliser des **copies des six RAW
   seuls**, sans leurs sidecars `.ipr` ou `.xmp`, dans `sources/`. Le reset
   d'IPR peut sauvegarder automatiquement un sidecar : ne pas le faire sur
   l'original si ses réglages doivent être conservés. Ne pas importer un TIFF
   Lightroom dans IPR à la place du RAW.
3. Noter boîtier/objectif, versions des deux logiciels, version de processus
   Lightroom, options d'importation et date. Côté IPR, relever le DCP/LCP
   indiqué dans les informations de l'image ; pour la passe instrumentée,
   noter aussi chemins et SHA-256 des profils et de l'exécutable.

### Neutraliser les réglages, pas la référence physique

Dans **IPR** :

- ouvrir le RAW, désactiver la WB automatique avant l'état final et attendre
  la fin du chargement/redécodage ;
- réinitialiser les réglages : EV, tonalité, couleur, texture, clarté, voile,
  grain, flou, bruit et gain de netteté à zéro ; courbes utilisateur identités,
  huit bandes du mélangeur neutres, saturations de grading à zéro, gris désactivé ;
- garder la température de référence restaurée par IPR et la nuance relative
  à zéro : la température ne doit **pas** être mise à 0 K ; un rayon de
  netteté non nul n'a pas d'effet lorsque le gain est zéro ;
- vérifier qu'aucun ancien réglage n'a été restauré, puis attendre le rendu.

Dans **Lightroom** :

- choisir explicitement **Adobe Standard** pour cette première passe,
  puisqu'il est présent dans les références historiques ; ne pas mélanger
  Adobe Color, Adaptive ou des profils créatifs dans la même série ;
- choisir **Telle quelle / As Shot** pour la WB du RAW, sans automatisme ni
  copie d'une température unique à toutes les scènes ; garder sa nuance
  « telle quelle », qui n'est pas nécessairement zéro dans Lightroom ;
- mettre à zéro les réglages utilisateur de lumière, couleur, mélangeur,
  grading, effets, netteté (gain), bruit luminance et couleur ; courbe
  utilisateur linéaire ; supprimer/désactiver les masques locaux ;
- désactiver les traitements IA/adaptatifs, HDR, Denoise/Enhance, correction
  automatique, recadrage/redressement et netteté de sortie. Ne pas substituer
  un DNG amélioré au RAW source ; déclarer toute amélioration déjà appliquée.

**Attention WB :** 5200 K/+5 dans Lightroom et 5200 K/+5 dans IPR ne désignent
pas nécessairement les mêmes gains. IPR travaille avec une estimation de
température et une nuance relative au décodage. Cette première passe teste
les bases « As Shot » de chaque moteur ; elle n'isole pas encore le DCP de la WB.
Les courbes ou looks intégrés au profil ne sont pas des courbes utilisateur.

### Corrections optiques : ne pas inventer un interrupteur IPR

Pour une comparaison des bases réelles, déclarer les corrections optiques de
chaque moteur. Si IPR applique un LCP, utiliser les corrections de profil LR
pour la passe initiale et noter les différences. Si IPR ne trouve aucun LCP,
désactiver la correction de profil LR lorsque c'est possible. Noter toute
correction intégrée obligatoire et l'aberration chromatique automatique.

IPR ne fournit pas encore de sélecteur DCP/LCP ni d'interrupteur LCP dans
l'IHM. L'isolement rigoureux « même DCP, LCP désactivé dans les deux moteurs »
appartient à la passe technique instrumentée décrite plus loin. Ne pas modifier
l'installation Adobe ou supprimer des profils pour simuler ce réglage.

### Exporter

Un seul export par moteur/scène, **TIFF SDR sRGB 16 bits**, ICC embarqué,
taille native, sans redimensionnement, recadrage ni netteté de sortie.
Utiliser de préférence un TIFF non compressé pour le lecteur natif actuel.

```text
neutres/
  6V1A0037-IPR-base-v1.tiff
  6V1A0037-LR-AS-base-v1.tif
  ... même paire pour 0043, 0327, 0007, 9950, 9421
```

Conserver à côté les XMP Lightroom exportés/sauvegardés après ces réglages,
ainsi qu'un relevé des réglages IPR (et le `.ipr` de la copie de travail).
L'actuel TIFF IPR ne certifie pas à lui seul ses curseurs ou son build.
Si un nom existe déjà, ouvrir une nouvelle passe `v2` : pas d'écrasement
silencieux, pas de suffixe automatique interprété comme état de test.

## 4. Analyse prévue de la phase A

1. Auditer fichier source, ICC, profondeur, dimensions, orientation, profils,
   version de processus, neutralité déclarée et champs XMP disponibles.
2. Définir manuellement des régions homologues à l'intérieur des objets :
   prises propres sans magnésie, textiles, feuillage, fleur, peau diffuse et
   surfaces de faible chroma. Exclure bords, reflets, ombres très profondes et
   canaux écrêtés. Une surface blanche/grise supposée n'est pas une charte.
3. Vérifier l'alignement avant superposition : orientation EXIF, dimensions,
   cadrage d'un pixel et éventuelles déformations LCP. Ne pas redimensionner
   simplement un moteur sur l'autre. Si les corrections ne permettent pas
   une correspondance fiable, mesurer des régions homologues séparées et
   renoncer à une MAE globale pixel à pixel.
4. Séparer luminance et couleur. Sur sRGB linéarisé, comparer Y, chroma et
   teinte, puis les écarts Lab. Publier les résultats bruts **et**, si une
   région neutre fiable existe, une version diagnostique après un unique
   gain d'exposition global documenté. Ne pas ajuster un gain par couleur
   ni masquer des différences de tonalité par une normalisation locale.
5. Classer : écart de WB, courbe/contraste de profil, exposition de base,
   comportement du gamut, bruit ou correction optique. Un écart mesuré
   avec LR est un écart entre deux rendus, pas une erreur physique démontrée.

Les mesures restent en pixels natifs 16 bits, pas sur la planche de
reconnaissance, une capture d'écran ou l'aperçu IPR limité à 2048 pixels.
Le lecteur TIFF existant est réutilisable ; une analyse dédiée de charte et
une implémentation vérifiée de CIEDE2000 restent à préparer avant la phase B.
Le comparateur HSL existant ne certifie pas cette nouvelle campagne.

## 5. Phase technique — isoler le moteur DCP

Après la première livraison, préparer une sonde contrôlée (pas encore
implémentée pour cette campagne) avec trois configurations explicites :

- conversion de repli LibRaw sans DCP/LCP ;
- même RAW avec un DCP précis du boîtier, sans LCP ;
- ce DCP dans Lightroom, sans correction optique désactivable ni look additionnel.

Tracer gains WB réels lorsque disponibles, illuminants, matrices, courbe,
HueSatMap, LookTable, compensation d'exposition et étapes effectivement
appliquées. Garder le même développement de départ dans les deux variantes
IPR autant que possible. L'ajout d'Adobe Neutral dans LR, sur deux scènes,
peut diagnostiquer un effet esthétique mais **ne remplace pas** une passe
au même profil exact. Une désignation commerciale identique ne prouve pas
l'identité du DCP, du look XMP ou du pipeline.

Une erreur de niveaux noir/blanc, d'estimation WB ou de conversion doit être
traitée séparément : ne pas fabriquer une LUT qui compense accidentellement
ce défaut. Le moteur DCP actuel est partiel ; la conformité Adobe n'est
pas supposée [contrat du moteur](../../camera-profile-engine.md).

## 6. Phase B — prises de vue de référence physique

Cette étape est nécessaire pour revendiquer une fidélité mesurée. Utiliser
une charte photographique physique documentée (24 plages ou équivalent),
plus une plage grise neutre. Les références doivent correspondre au modèle
et à la génération de la charte ; ne pas utiliser les pixels d'une image
de charte trouvée en ligne. Le fabricant décrit l'usage d'une charte sous
l'éclairage de prise de vue pour établir un profil [S2].

### Acquisition minimale : six RAW

1. Même boîtier/objectif, ISO de base (100 si approprié), trépied si possible,
   mise au point précise, pas de filtre coloré, pas de traitements RAW IA.
2. Photographier la charte sous **deux éclairages séparés**, par exemple
   lumière du jour diffuse stable et une source artificielle stable bien
   documentée. Pas de soleil couchant ni de mélange fenêtre/LED pour l'étalon.
   Un flash peut être une alternative si c'est le matériel disponible.
3. Pour chaque éclairage, réaliser trois répétitions avec exposition,
   ouverture et WB fixes. Attendre que l'éclairage soit stable ; vérifier
   les variations et bandes des LED plutôt que supposer leur absence.
4. Charte plane, uniformément éclairée, sans reflet ; image frontale si
   possible, plages assez grandes pour des régions centrales d'au moins
   64×64 pixels. Garder une marge autour de la charte et ne pas saturer
   les blancs/couleurs. Noter source lumineuse, heure et conditions.
5. Exporter les six RAW selon la phase A, puis mesurer contre les données
   de référence. Pour une calibration nouvelle, garder des prises distinctes
   de celles utilisées pour l'ajustement du profil : ne pas évaluer seulement
   ses images d'apprentissage.

La température nominale ou un CRI élevé ne décrit pas complètement le spectre
d'une LED. Des couleurs peuvent réagir différemment selon l'éclairage [S3].
Avec uniquement une référence Lab nominale et un éclairage non caractérisé,
la conclusion reste conditionnelle, pas une certification spectrale.

### Mesures et seuils proposés

Décoder sRGB, convertir vers XYZ puis Lab avec un **même blanc et un même
observateur** que la référence. sRGB est D65 ; si la référence est Lab D50,
appliquer/documenter l'adaptation chromatique ou employer un transformateur
ICC approprié. Ne pas comparer directement Lab D65 et Lab D50 [S4].

Sur les centres de plages non écrêtées, calculer médianes RGB linéaires,
Lab, ΔL*, ΔC*, écart de teinte et **ΔE00**. Valider l'implémentation de ΔE00
avec les jeux de test de Sharma, Wu et Dalal avant de tirer une conclusion [S5].
Sur les vrais gris, analyser séparément exposition et neutralité.

**Cibles initiales de projet, à confirmer selon l'incertitude des références,
et non des normes universelles :**

- médiane ΔE00 ≤3 et percentile 95 ≤6 sur les plages admissibles ;
- chroma C*ab ≤2 sur les gris moyens effectivement neutres ;
- médiane ΔE00 entre répétitions ≤1 sous le même éclairage ;
- aucune amélioration moyenne cachant une dégradation forte des gris ou
  d'une famille de couleurs ; publier aussi maxima, valeurs par plage et
  nombre de plages exclues.

Publier la mesure native puis, à part, celle après WB/exposition globale
déterminées uniquement sur les plages neutres. Si ces corrections sont
nécessaires, la base d'ouverture non corrigée n'est pas déclarée validée.
Les plages hors gamut sRGB ou écrêtées sont signalées ; leur exclusion ne
permet pas de revendiquer une validation de toutes les couleurs.

## 7. Phase C — prises d'escalade et nuanciers

L'utilisateur indique **Sup'r, 360HOLDS, EP Holds et probablement XCULT**.
Ces marques ne sont pas attribuées automatiquement aux prises photographiées.
Pour chaque région, fournir marque/modèle, matière (PU, PE, volume peint…),
couleur commandée/code exact et source fabricant datée. Si l'identification
est incertaine, la région reste « référence non confirmée ».

### Sources retrouvées

- **XCULT** publie un nuancier explicite : jaune RAL 1018, orange RAL 2004,
  rouge RAL 3020, violet RAL 4008, bleu RAL 5015, vert RAL 6037 et bleu clair
  Pantone 3115 C, entre autres. Le fabricant indique aussi des écarts de teinte
  possibles entre prises et volumes selon matériau/fabrication [S6].
- **EP** publie dans son catalogue 2023 des références comme bleu RAL 5015,
  rouge RAL 3020, violet RAL 4008, **vert RAL 6002**, jaune Pantone 116 C et
  des couleurs fluorescentes [S7]. Les références indexées du catalogue ont
  été retrouvées ; la récupération intégrale du PDF a expiré. Confirmer
  gamme/matière/année sur la référence exacte avant une association à une ROI.
- **360HOLDS** : page officielle Downloads/orderlists retrouvée, avec
  formulaire de téléchargement. Aucun nuancier chiffré vérifié pour cette
  campagne ; ne pas attribuer les codes d'un distributeur par défaut [S8].
- **Sup'r** : catalogue et formulaire de commande reliés depuis le site
  officiel, mais téléchargement Google Drive inaccessible lors de la recherche.
  Nuancier chiffré non vérifié ; utiliser le bon de commande ou un PDF fourni [S9].

Le vert EP RAL 6002 n'est donc pas interchangeable avec le vert XCULT
RAL 6037. Un nom générique (« vert », « jaune ») et une ressemblance visuelle
ne suffisent pas à identifier la référence.

### Ce que l'on peut mesurer

1. Sur 9421, puis 0767/9377, annoter des prises réellement identifiées, en
   évitant magnésie, usure, reflets, vis, doubles textures et ombres marquées.
2. Avec un simple nuancier web, contrôler la famille de teinte et la cohérence
   visuelle uniquement : **ne pas mesurer un ΔE de fidélité contre un HEX
   prélevé à la pipette sur un site**. L'image produit est elle-même un rendu.
3. Si des données Lab/spectrales fabricant existent, relever éclairage,
   observateur, géométrie et matière. Un code RAL seul n'est pas une mesure
   de la prise usagée sous l'éclairage de la salle. Une référence physique
   mesurée de la prise réelle permet une conclusion plus forte.
4. Pour une prochaine séance en salle, photographier une charte et la prise
   sous le **même éclairage**, aussi près que possible et sans lumière mixte.
   Cela aide à séparer dominante de lumière et réponse du profil.
5. Séparer les couleurs fluorescentes : émission dépendante de l'éclairage/UV,
   éventuel hors gamut et comportement non représenté par un simple RGB web.
   Elles restent des tests de stress, pas les premières cibles de calibration [S10].

## 8. Artefacts, décision et suites

Pour chaque passe : manifeste des RAW/TIFF et profils avec empreintes,
réglages déclarés, métadonnées auditées, régions annotées en coordonnées
natives, exclusions, méthode d'alignement, mesures brutes JSON/CSV et extraits
visuels. Conserver les originaux et les preuves historiques.

- Données brutes : `docs/validation/raw/mesure-base-colorimetrie-v1-*`.
- Rapport évolutif : [analyse de la base colorimétrique](../analyses/analyse-base-colorimetrique.md).
- Décision finale séparée : base cohérente / fidélité mesurée sous conditions /
  conformité DCP partielle / préférence esthétique. Aucun de ces statuts n'est
  encore acquis par l'inventaire ci-dessus.

Après correction éventuelle de la base, vérifier les réglages déjà validés
sur leur corpus propre, sans recommencer d'emblée tous les exports. Aucun
algorithme, profil, photo ou sidecar utilisateur n'est modifié dans cette
préparation ; aucune compilation requise.

## Bibliographie — consultation du 6 octobre 2026

- **[S1] Adobe**, [Profils et réglages de Lightroom Classic](https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/image-tone-color.html) : profils RAW et intention de rendu.
- **[S2] Calibrite**, [PROFILER — Custom Camera Profiles](https://calibrite.com/profiler/) : photographie d'une charte sous l'éclairage de travail et création de profils.
- **[S3] X-Rite**, [LED Controlled Lighting Solutions](https://www.xrite.com/blog/how-xrite-can-support-your-led-controlled-lighting-needs) : effets de l'éclairage et métamérisme ; résumé indexé consulté, accès direct indisponible pendant cette recherche.
- **[S4] International Color Consortium**, [Registre sRGB](https://registry.color.org/rgb-registry/srgb) : encodage, blanc D65 et accès aux transformations vers le PCS.
- **[S5] Sharma, G., Wu, W., Dalal, E. N. (2005)**, [CIEDE2000 : article et jeux de test](https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/), *Color Research & Application*, 30(1), 21–30, DOI [10.1002/col.20070](https://doi.org/10.1002/col.20070).
- **[S6] XCULT**, [Technical info for holds](https://www.xcultclimbing.com/en/holds_technical_info) : nuancier RAL/Pantone, matières et différences prises/volumes.
- **[S7] EP Climbing**, [Catalogue prises/volumes 2023](https://entre-prises.eu/userfiles/downloads/Holds%20%26%20volumes%20catalogue%202023-WEB_LQ___.pdf) et [présentation officielle](https://epclimbing.com/na/en/products/holds) : gammes PU/PE et références couleur ; accès PDF complet à confirmer.
- **[S8] 360HOLDS**, [Downloads](https://www.360holds.com/en/Downloads) : listes de commande officielles ; nuancier non vérifié.
- **[S9] Sup'r**, [Site officiel et liens catalogue/commande](https://suprclimbing.com/) : gammes et documents d'identification ; nuancier non vérifié.
- **[S10] X-Rite**, [Instruments de mesure spectrale](https://www.xrite.com/page/color-spectrophotometer) : réflectance et mesure de fluorescence/UV ; résumé indexé consulté.
- **Complément ArtLine**, [Downloads et table d'équivalence couleur](https://www.artline-holds.com/en/downloads/) : exemple explicite de variation des teintes selon les matériaux ; cette marque n'est pas attribuée au corpus.
