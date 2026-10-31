# Étude — autonomie des profils caméra et corpus de validation

Date de recherche : **20 octobre 2026**. Statut : **proposition et revue de
sources**, sans modification du moteur, téléchargement de corpus ni mesure nouvelle.
Ce document est à faire évoluer ; ne pas ouvrir un rapport parallèle par banque.

## 1. Synthèse et périmètre

L’objectif recommandé est de devenir indépendant des **données de profils
tiers**, pas de changer d’extension pour masquer leur origine. Un DCP créé
avec nos mesures peut être un profil IPR ; un binaire contenant les tables
d’un tiers reste dépendant de la provenance et des droits de ces tables.

Les banques consultées permettent de démarrer la validation technique,
l’estimation d’illuminant et certains essais de caractérisation. **Aucune source
consultée ne permet ici d’affirmer que nous disposons d’une caractérisation
physique complète, librement redistribuable et vérifiée des Canon R6 Mark III et R7.**
La présence éventuelle de photos d’un modèle ne démontre pas cette disponibilité.

Recommandation : corpus IPR actuel pour la non-régression ; petit sous-ensemble
de RAW CC0 de raw.pixls.us pour la diversité de décodage ; Cube++ pour les blancs ;
mires mesurées sur nos appareils pour construire les profils de production.

L’utilisateur indique qu’un serveur MCP et une passerelle entre production et
analyseurs sont **en cours de réalisation**. La section 8 propose un contrat
de raccordement à ce chantier, sans annoncer son implémentation ni inventer des
commandes déjà disponibles. MCP désigne ici Model Context Protocol.

## 2. Ce que contient un DCP

Le [protocole de prises de vue de mires](../../validation/protocols/protocole-mires-couleur.md)
détaille le matériel, les références, les séances et les contrôles à effectuer.
Il ne remplace pas le protocole de comparaison de rendu RAW existant.

Un DNG Camera Profile associe des données de caractérisation et de rendu au
signal couleur d’un appareil. Le conteneur suit des conventions TIFF/DNG :
en-tête, ordre des octets, répertoire de tags typés, valeurs ou offsets vers
des tableaux. Ce n’est ni une photographie, ni le décompresseur CR3, ni un ICC
d’écran. Voir les ressources Adobe et la structure du SDK [S1, S2].

| Famille | Informations possibles | Rôle |
| --- | --- | --- |
| Identité | Nom, modèle compatible, signature | Choix du profil |
| Éclairages | CalibrationIlluminant1/2 | Conditions de référence |
| Matrices | ColorMatrix, ForwardMatrix, ReductionMatrix | Transformations couleur |
| Tables | HueSatMap et LookTable, dimensions/encodages | Corrections et apparence |
| Tonalité | ToneCurve, BaselineExposureOffset | Rendu tonal et offset EV |
| Droits | Copyright, EmbedPolicy | Mentions et politique déclarée |

Tous les champs ne sont pas obligatoires. ColorMatrix décrit le sens
XYZ vers composantes caméra ; ForwardMatrix décrit caméra équilibrée vers
XYZ D50. Les matrices de calibration présentes dans une image ne se confondent
pas avec celles du profil. Une politique d’incorporation n’est pas une licence
universelle de redistribution. Les profils avancés/extensions DNG ne sont pas
présumés intégralement pris en charge par IPR [S2].

## 3. Droits : format, implémentation et données

Le droit d’auteur distingue l’expression originale des idées et des méthodes
mathématiques ; cela ne permet pas de qualifier automatiquement chaque
coefficient de protégé ou de libre. La provenance, les choix de rendu et les
conditions contractuelles restent à examiner [S3].

La licence de brevets DNG publiée par Adobe vise des implémentations conformes,
avec ses conditions propres. Elle ne constitue pas une permission générale de
redistribuer les fichiers fournis avec un logiciel Adobe [S1]. La licence du
lecteur, celle des profils et celle d’une banque d’images sont distinctes.

L’article L122-4 traite notamment de la reproduction et de la transformation
d’une œuvre sans autorisation [S4]. Changer de conteneur, compresser les tables
ou les incorporer dans l’exécutable ne suffit donc pas, en soi, à autoriser leur
distribution. Les exceptions et le statut de chaque élément demandent un examen
spécifique ; cette étude n’est pas un avis juridique.

**Politique proposée :** ne livrer que des données propres ou accompagnées
d’une autorisation vérifiée. Licence absente ou ambiguë : bloquer leur adoption,
pas déduire une autorisation de l’accès public. Les éléments issus d’une banque
NC ne sont pas intégrés par défaut à des profils distribuables. La portée sur
les paramètres ajustés reste à analyser ; une conversion ne nettoie pas l’origine.

## 4. Quelles vérités terrain rechercher ?

| Niveau | Données | Ce que l’on peut conclure |
| --- | --- | --- |
| Décodage | RAW original, identité et mode | Robustesse d’ouverture et cohérence technique |
| Balance des blancs | Signal caméra et blanc de référence | Erreur d’illuminant dans ce domaine caméra |
| Colorimétrie | RGB caméra de mires et références XYZ/spectres documentés | Erreur de caractérisation dans les conditions mesurées |
| Rendu | Photos naturelles et exports d’un autre moteur | Similarité esthétique, non vérité physique |
| Spectral | Sensibilités capteur, spectres d’éclairage et réflectances | Simulation contrôlée, à confronter au réel |

Une couleur de catalogue web, un JPEG de mire ou un TIFF Lightroom ne fournit
pas à lui seul une mesure spectrale de la scène. Une référence d’illuminant RGB
n’est pas non plus une référence XYZ pour toutes les couleurs de l’image.

## 5. Banques et ressources examinées

Les conditions ci-dessous reflètent les pages consultées, pas une validation
juridique ni un téléchargement réussi des archives. Vérifier chaque lot avant
son utilisation. Ne pas transférer une calibration d’un modèle voisin au R6/R7.

### 5.1 raw.pixls.us — priorité pour l’ouverture multi-formats

La base propose des RAW et métadonnées avec une licence indiquée par entrée.
Les contributions actuelles demandent une mise à disposition CC0 ; le site
distingue également des échantillons non-CC0. Sélectionner exclusivement les
entrées dont la licence est effectivement vérifiée [S5].

Usage IPR : formats, modes, compression, géométrie, identification des appareils
et erreurs d’ouverture. Le site ne recherche pas des photos de mires et les
exclut de ses contributions : ce n’est pas une banque de vérité colorimétrique.
Le relevé de catalogue Canon ci-dessous précise désormais les candidats
R6/R7/R5/6D II, sans vérification des pixels ni des modes par décodage.
Ne pas cloner son miroir complet.

#### Relevé Canon reflex/hybrides — 20 octobre 2026

Le [relevé JSON v001](../../validation/raw/inventaire-canon-rawpixls-v001-20261020.json)
conserve source, horodatage, comptages, liens, identifiants et SHA-256 déclarés
par le fournisseur. Il a été construit depuis le
[catalogue JSON public](https://raw.pixls.us/json/getrepository.php?set=all),
celui que charge la table du site. Aucun RAW n’a été téléchargé.

Filtre : fabricant `Canon`, nom commençant par `EOS`. Résultat : **227 entrées,
110 libellés de modèle et 221 entrées portant un lien CC0**. Les six autres
ne sont pas proposées pour adoption. Ces 110 libellés ne sont pas 110 boîtiers
distincts : les noms régionaux Rebel/Kiss créent notamment des doublons.
Le comptage inclut aussi certains DNG dérivés ; ce n’est pas le nombre de RAW
natifs Canon utilisables sans tri.

La sélection initiale proposée couvre **12 modèles et 27 fichiers annoncés
CR2/CR3, tous déclarés CC0, pour 668,16 MB selon le catalogue** (environ 0,67 Go,
hors futurs exports). Les familles ci-dessous classent les boîtiers, pas les
codecs : certains reflex Canon produisent du CR3.

| Famille | Modèle | Fichiers proposés | Identifiants raw.pixls.us | Objectif |
| --- | --- | ---: | --- | --- |
| Reflex plein format | 6D Mark II | 3 | 1623, 1624, 1625 | RAW, mRAW, sRAW |
| Reflex plein format | 5D Mark IV | 3 | 981, 982, 983 | RAW sans DPR, mRAW, sRAW |
| Reflex APS-C | 7D Mark II | 3 | 1064, 1065, 1066 | RAW, mRAW, sRAW |
| Reflex APS-C | 80D | 3 | 1294, 1295, 1296 | RAW, mRAW, sRAW |
| Reflex APS-C | 90D | 2 | 4649, 4650 | Deux CR3 ; compression à identifier |
| Reflex plein format | 1D X Mark III | 2 | 4654, 4655 | CRAW et RAW déclarés |
| Hybride EOS M | M5 | 1 | 2935 | Entrée EOS M en CR2 |
| Hybride EOS M | M6 Mark II | 2 | 3713, 3714 | RAW et C-RAW 3:2 |
| Hybride EOS R | R5 | 2 | 4694, 4698 | RAW et CRAW sans Dual Pixel déclarés |
| Hybride EOS R | R6 | 2 | 4660, 4662 | CRAW et RAW sans crop déclaré |
| Hybride EOS R | R7 | 2 | 5633, 5634 | RAW et cRaw déclarés |
| Hybride EOS R | R10 | 2 | 5871, 8185 | Deux CR3 ; un seul explicitement nommé RAW |

Ces lignes ne représentent pas nécessairement des paires d’une même scène.
Ni mRAW/sRAW ni CRAW ne doivent être supposés équivalents au RAW pleine mosaïque.
Les 90D/R10 restent des candidats à identifier, pas des paires RAW/CRAW certifiées.

Le catalogue complet annonce notamment **R6 : 4 fichiers**, **R7 : 4**,
**R5 : 8**, **R6 Mark II : 8**, **6D Mark II : 3**, tous CC0 pour ces modèles.
Des EOS R/RP, R3, R8, R50, R100, R5 Mark II et d’autres EOS M sont également
présents. Les variantes crop et les profils de compression additionnels
servent à un second lot ; ne pas les mêler à la calibration plein format.

Points de vigilance déjà visibles dans le catalogue :

- Les 11 entrées `EOS 5D Mark III` ne sont pas toutes des CR2 natifs :
  plusieurs sont des DNG issus de HDRMerge ou Magic Lantern. Les laisser dans
  un corpus DNG dérivés séparé, pas dans la caractérisation du boîtier.
- Les entrées R7 5635/5636 portent une remarque « Dual Pixel » du catalogue.
  Cette remarque seule ne prouve ni l’option de capture ni son interprétation
  par le décodeur ; elles sont exclues du premier lot et à auditer.
- Les noms générés pour plusieurs fichiers d’un même appareil sont identiques.
  Inclure l’identifiant du dépôt dans le nom local pour éviter toute collision.
- Les empreintes conservées sont celles **annoncées** par le site. Elles ne
  seront vérifiées qu’après acquisition des octets. Le volume est indicatif,
  calculé à partir des tailles arrondies affichées, pas des fichiers mesurés.

Ordre proposé : R6/R7/R5/6D II (nos priorités), puis extension aux autres modèles
du tableau. Acquisition ensuite sur autorisation, contrôle des empreintes,
audit des métadonnées et rejeu par la passerelle. Cette banque permet un corpus
de **compatibilité et non-régression**, pas une caractérisation spectrale ni
une preuve de fidélité colorimétrique absolue.

### 5.2 Cube++ — priorité pour l’estimation des blancs

Les auteurs annoncent 4 890 images, des CR2 originaux Canon 550D/600D et des
chromaticités d’illuminant déterminées avec un SpyderCube, sous **CC BY 4.0**.
La distribution complète représente environ 200 Go. SimpleCube++ est un
sous-ensemble réduit d’environ 2 Go, avec images redimensionnées [S6].

Usage IPR : évaluation de WB et détection des éclairages mixtes. Un SpyderCube
n’est pas une mire multicolore fournissant des XYZ pour chaque teinte. Les PNG
prétraités ne prouvent pas le fonctionnement du décodeur CR2 ; SimpleCube++
sert aux analyseurs, pas à une qualification de décodage pleine résolution.
La licence impose notamment l’attribution ; archiver sa version et les crédits.

### 5.3 NUS multi-camera — utile sous réserve des droits

Le projet des auteurs fournit les RAW natifs, des images linéaires dérivées,
des masques de mire et des blancs de référence. Il précise le traitement des
niveaux noirs et la différence entre représentations originales et converties
[S7]. Les huit appareils principaux sont anciens et ne comprennent pas R6/R7.

Usage : robustesse inter-boîtiers des blancs et des adaptateurs. Une licence
de redistribution/usage produit n’a pas été identifiée dans les pages consultées :
**adoption bloquée jusqu’à clarification**. Les masques de mire ne garantissent
pas à eux seuls une référence spectrale mesurée des plages. Ne pas mélanger
signal natif et PNG linéaire déjà corrigé en soustrayant deux fois le noir.

### 5.4 Gehler–Shi — référence historique, prudence méthodologique

La page SFU décrit un retraitement du jeu Gehler et ses références d’illuminant
[S8]. Usage potentiel : comparaison de méthodes de WB, avec version exacte de
prétraitement et de vérité terrain. Licence d’utilisation produit non confirmée
par la page consultée : pas d’import automatique. Ne pas mélanger des variantes
historiques de la base comme si leurs niveaux noirs et références étaient identiques.

### 5.5 INTEL-TAU — complément inter-capteurs, pas CR3 de référence

Le portail Tampere décrit 7 022 scènes provenant de Canon 5DSR, Nikon D810 et
Sony IMX135 ; les références sont des blancs au format R/G, B/G. Des variantes
redimensionnées et corrigées/non corrigées sont signalées [S9].

Usage : invariance inter-capteurs et WB. Le format exact de l’archive choisie
doit être audité ; des images dites « raw » au sens signal ne sont pas
nécessairement des fichiers natifs CR2/NEF. La notice d’accès liée n’a pas pu être
consultée par l’outil : **licence de données non confirmée dans cette étude**,
donc pas d’autorisation présumée depuis la licence de l’article.

### 5.6 Jiang et al. — données spectrales, contrainte NC explicite

La base des auteurs contient les sensibilités de 28 caméras, échantillonnées
de 400 à 720 nm par pas de 10 nm, chaque canal étant normalisé séparément.
Elle annonce **CC BY-NC-SA 4.0** [S10].

Usage potentiel : recherche spectrale sous conditions compatibles ; pas de
coefficients de production issus de ces données sans clarification/autorisation.
La normalisation par canal exige de réconcilier les gains relatifs. Ce n’est
pas une banque de photos ni une caractérisation confirmée de nos modèles récents.
Ne pas extrapoler hors bande sans déclarer l’hypothèse et son incertitude.

### 5.7 SPECTACLE — méthode de caractérisation à étudier

Les auteurs proposent une méthode et des outils pour mesurer biais, linéarité,
gain ISO, bruit, flat-field et réponse spectrale. Le dépôt renvoie vers les
données de calibration [S11, S12].

Usage : préparer notre protocole radiométrique et éviter de confondre profil
couleur et normalisation du capteur. La licence du code ne suffit pas à établir
les droits de tous les fichiers du dossier de données. Le lien de données est
identifié, mais contenu, disponibilité de nos boîtiers et licence par lot ne
sont pas vérifiés. Ressource méthodologique, pas dépendance ajoutée au moteur.

### 5.8 rawtoaces et DCamProf — outils de référence, pas banques interchangeables

rawtoaces distingue une conversion fondée sur la sensibilité spectrale d’une
conversion reposant sur les métadonnées. Son code est publié sous Apache-2.0 ;
cela ne dispense pas d’auditer la provenance de données spectrales externes
[S13]. Ne pas qualifier de caractérisation indépendante une sortie obtenue
par un mode utilisant des coefficients Adobe.

DCamProf décrit l’ajustement de profils à partir de mesures de mire ou de
sensibilités spectrales, et des diagnostics de qualité [S14]. Il peut servir
de comparateur hors production. Vérifier séparément licence du logiciel,
des mesures et des profils produits ; ne pas recopier un rendu tiers par défaut.

### 5.9 Choix proposé

| Source | Première utilisation | Adoption proposée |
| --- | --- | --- |
| Corpus IPR possédé | Non-régression R6/R7 et rendu accepté | Réutiliser, recettes figées |
| raw.pixls.us, entrées CC0 | Diversité RAW/modes | Lot réduit après audit individuel |
| Cube++ CC BY | WB/illuminants | Premier corpus externe conseillé |
| NUS / Gehler–Shi / INTEL-TAU | Benchmarks WB supplémentaires | Attendre clarification des droits |
| Jiang NC-SA | Recherche spectrale | Isoler ; ne pas embarquer en production |
| SPECTACLE / rawtoaces / DCamProf | Méthodes et contre-mesures | Étudier sans nouvelle dépendance de prod |
| Mires propres R6/R7 | Caractérisation indépendante | Nécessaire pour conclure sur ces appareils |

### 5.10 Complément — banques avec mire et références couleur

**Correction de périmètre :** l’absence de référence physique dans les scènes
raw.pixls.us ne signifie pas absence de banques Canon avec mire. NUS et
Gehler–Shi en contiennent déjà. Les sources suivantes permettent d’aller plus
loin que la seule ouverture de fichiers, sous réserve des droits et de l’audit
des archives. Leur existence ne valide pas automatiquement nos R6 Mark III/R7.

- **Beyond White, Cheng et al., ICCV 2015** : images de laboratoire, RAW,
  masques et fichiers de référence, notamment Canon EOS 600D et EOS-1Ds Mark III.
  Le README distingue les couleurs observées des 24 plages, les références
  `cc_color_ref` et les matrices de correction. Ces références sont établies
  suivant la méthode des auteurs dans le domaine caméra ; ne pas les présenter
  comme des mesures XYZ indépendantes de chaque plage. Candidat prioritaire
  pour qualifier nos analyseurs de correction couleur [S15].
- **Karaimer et Brown, CVPR 2018** : données et méthode de reproduction couleur,
  avec références obtenues par calibration X-Rite et images arrêtées avant le
  rendu esthétique. Référence de méthode et de comparaison, pas données
  indépendantes d’un autre outil de calibration. Licence et contenu exact du
  téléchargement restent à examiner [S16].
- **Exemple Canon EOS 60D de Jiang et al.** : un CR2 avec ColorChecker est
  annoncé avec sensibilités du boîtier et lumière du jour mesurées. Il s’agit
  d’un exemple expérimental, pas d’une banque multi-éclairage complète ;
  la licence NC-SA signalée au §5.6 reste applicable [S10].

### 5.11 Compte rendu des échanges — 20 octobre 2026

1. L’utilisateur souhaite rendre les profils indépendants des fichiers Adobe.
   La conversion DCP vers un binaire propre ne résout pas la provenance et les
   droits des données. Proposition : sources documentées, caractérisation
   séparée du rendu, cache compilé seulement si utile.
2. Un serveur MCP et une passerelle production/analyseurs sont annoncés en
   préparation. Ce rapport exprime les besoins de traçabilité ; il ne certifie
   pas la livraison ou la parité de ces outils.
3. Le catalogue Canon raw.pixls.us a été inventorié sans télécharger les photos :
   27 candidats sur 12 modèles, environ 668 MB déclarés. Il sert d’abord à la
   compatibilité/non-régression, pas de référence couleur absolue.
4. Les échanges ont clarifié qu’une image avec une mire est exploitable pour
   une calibration approximative ou une expérience contrôlée, mais que son
   niveau de preuve dépend des valeurs de référence, de l’éclairage et du domaine
   du signal. Il n’est pas nécessaire de connaître tout le spectre pour commencer
   un profil pratique ; il faut déclarer les hypothèses et l’incertitude.
5. L’utilisateur confirme que ses CR2 proviennent d’un **Canon EOS 60D**.
   Identité et modes restent à vérifier dans les métadonnées : aucun audit de
   ces fichiers n’est annoncé ici. Les photos sans mire seront une validation
   indépendante du comportement sur scènes naturelles, pas un étalonnage.
6. Le 60D devient un **candidat proposé** pour qualifier la méthode grâce à
   l’exemple public correspondant ; l’accès actuel de l’utilisateur au boîtier,
   à une mire et aux éclairages n’est pas confirmé. Ne pas transférer les
   coefficients du 60D aux R6 Mark III/R7 ni assimiler deux exemplaires du 60D.
7. Après vérification des droits, commencer par un petit lot Beyond White et/ou
   l’exemple 60D, puis préparer les prises propres selon le protocole. Aucune
   acquisition, intégration de coefficient, commande MCP ni décision juridique
   n’a été exécutée au titre de ces échanges.

Les formulations courtes « R6 » des échanges précédents ne doivent pas masquer
le modèle exact : le catalogue spécifique de `dev` examiné le 20 octobre vise
**EOS R6 Mark III** et **EOS R7**. Les références raw.pixls.us R6 original
constituent donc un autre modèle à qualifier.

### 5.12 Piste Canon Developer Programme et priorité produit

Mise à jour utilisateur du **20 octobre 2026** : la création de compte sur
le portail développeurs Canon n’est actuellement pas possible de son côté.
Blocage d’accès **signalé par l’utilisateur**, sans cause technique établie ;
ne pas le présenter comme une indisponibilité générale confirmée du service.
Aucun compte créé, aucune demande envoyée et aucun SDK téléchargé par l’assistant.

L’utilisateur retient cette piste comme prometteuse pour le travail sur les
profils **DCP Canon**. À la réouverture de l’accès, demander à Canon Europe
quelles données de caractérisation, matrices, sensibilités spectrales et
informations RAW sont accessibles pour les modèles ciblés, et quels droits
s’appliquent à leur utilisation et aux coefficients distribués. L’existence
d’un programme développeurs ne garantit ni l’accès à ces données ni leur
redistribution. Le SDK RAW peut être un témoin de rendu sans fournir les
paramètres internes d’un profil ; les Picture Styles PF2/PF3 ne sont pas des DCP.

- [Portail Canon Developer Programme Europe](https://developers.canon-europe.com/developers/s/).
- [Page d’inscription identifiée](https://developers.canon-europe.com/developers/s/login/SelfRegister),
  à réessayer ultérieurement ; aucun contournement de l’inscription n’est proposé.
- Les conditions d’accès, d’éventuelle confidentialité et de publication devront
  être examinées **avant** d’accepter des données ou de les intégrer au projet.

**Priorité convenue :** poursuivre l’étude et la validation sur Canon et les
boîtiers ciblés. L’élargissement à d’autres marques et à d’autres boîtiers est
reporté après l’atteinte d’une version stable. Cela n’annule pas les travaux
Canon déjà prévus ni les critères de stabilité définis par le projet ; ce n’est
pas une déclaration de compatibilité générale. L’architecture extensible reste
prévue, sans lancer de nouvelle campagne multi-marques à ce stade.

En attendant l’accès Canon, le travail sur corpus autorisés, les mires et la
passerelle d’analyse peut continuer indépendamment. Aucun choix de dépendance
SDK Canon en production ni remplacement du rendu approuvé n’est décidé ici.

## 6. Modèles physiques et mathématiques à tester

Pour une plage de réflectance rho, une approximation de la mesure d’un canal
est `c_k = gain * intégrale(E(lambda) * rho(lambda) * S_k(lambda)) d(lambda)`.
E désigne le spectre d’éclairage et S la sensibilité du canal. Les unités,
le domaine spectral et la normalisation doivent être documentés. Une CCT seule
ne suffit pas à décrire E ; plusieurs spectres peuvent partager une CCT.

Première expérience proposée : ajuster une matrice par éclairage sur des
couples RGB caméra/XYZ de mire, avec pénalisation de l’instabilité :

```text
M* = argmin_M somme_i w_i ||M c_i - x_i||² + lambda ||M - M0||²
```

Ce critère XYZ n’est pas un minimiseur direct de DeltaE00. Évaluer ensuite
les erreurs perceptuelles sous blanc/observateur déclarés. Garder les neutres,
surveiller le conditionnement, puis comparer une correction résiduelle lisse
à la matrice seule. Réserver une LUT plus flexible aux résidus démontrés.
Valider séparément l’interpolation des illuminants et les éclairages mixtes.

Sur une banque WB, mesurer l’erreur angulaire entre illuminant estimé et
référence dans le **même RGB caméra** :

```text
theta = acos(clamp(dot(e_estime, e_reference) / (norm(e_estime)*norm(e_reference)), -1, 1))
```

Convertir en degrés ; refuser les vecteurs nuls/non finis. Une bonne erreur WB
ne valide ni la matrice caméra→XYZ ni le rendu orange/rouge. Les captures d’un
même sujet et les dérivés d’une image doivent rester dans le même sous-ensemble
d’ajustement ou de validation pour éviter les fuites de données.

## 7. Roadmap et critères de passage

Cette proposition complète [ADR-0012](../../decisions/ADR-0012-formats-extensibles-et-caracterisations.md)
et la [roadmap RAW](../roadmap-ouverture-raw.md), **sans les remplacer**. Le
catalogue courant `config/raw-normalization.json` et les compensations liées
au SHA DCP restent inchangés. Une nouvelle organisation de production devra
faire l’objet d’une décision validée, pas d’un second catalogue ajouté discrètement.

| Lot | Action | Livrable / condition de passage |
| --- | --- | --- |
| P0 | Inventorier profils et coefficients actuels | Provenance, licence, modèles/modes et dépendances ; exclure les droits inconnus |
| P1 | Figer le témoin prod et le contrat de passerelle | SHA, recettes, domaines de signal et rejeu déterministe sur corpus existant |
| P2 | Préparer un corpus externe ciblé | Petits lots audités, licences et empreintes ; pas de téléchargement massif |
| P3 | Mesurer les boîtiers possédés | Mires et répétitions R6/R7, références et incertitudes documentées |
| P4 | Ajuster matrices puis résidus si nécessaires | Validation séparée, blancs/neutres et stabilité sans écrêtage nouveau |
| P5 | Définir le rendu IPR Neutral | Esthétique distincte de la caractérisation, validation visuelle utilisateur |
| P6 | Intégrer les données propres | Contrat profil/caches, tests multi-boîtiers ; aucun gain compté deux fois |
| P7 | Étendre R5 / 6D II puis autres marques | Caractérisation par modèle/mode, aucune promesse de support universel |

Les seuils numériques sont à fixer **avant l’ajustement**, après mesure de la
répétabilité et de la variabilité des données. Rapporter médiane, moyenne,
P95 et cas extrêmes par boîtier, éclairage et famille de couleurs, pas seulement
une moyenne globale. Examiner DeltaE00, neutralité, exposition EV, écrêtage par
canal, gradients et latences p50/p95. Les régressions importantes ne doivent
pas être masquées par une amélioration de moyenne. Un essai non exécuté reste
« non mesuré » ; une donnée invalide « exclue », jamais « succès ».

### Acquisition propre encore nécessaire

Pour chaque modèle : RAW pleine résolution, mode/firmware/ISO identifiés,
mire de référence fiable, éclairage homogène, charte non saturée, deux
illuminants caractérisés au minimum pour la première étude et répétitions.
Photographier des scènes indépendantes : peau, fleur orange/rouge, végétation,
bleus saturés, gris, HL et mélanges lumineux. Le corpus actuel reste utilisable
pour ces scènes, pas pour fabriquer rétroactivement une vérité spectrale.

Les prises d’escalade et leurs nuanciers web sont des sujets de validation
visuelle, **pas des mesures de réflectance** sans échantillon et éclairage maîtrisé.

## 8. Raccordement proposé au serveur MCP et aux analyseurs

### 8.1 Responsabilités — cible, non API existante

```text
Client d’analyse / agent
          │ demande structurée et autorisée
          ▼
Serveur MCP en cours
          │ orchestration de jobs, limites, état, annulation
          ▼
Passerelle de rendu → même moteur que la production, version identifiée
          │ résultats intermédiaires autorisés + export final
          ▼
Analyseurs versionnés → mesures JSON canoniques → rapport/graphes dérivés
```

MCP ne réalise pas lui-même la transformation couleur et ne constitue pas
une preuve de parité avec l’IHM. La passerelle doit appeler le vrai moteur,
sans seconde implémentation photographique Python. Un test de contrat doit
comparer le rendu passerelle et l’export IHM avec même source, recette et build.

Capacités souhaitées, à convenir avec le chantier en cours : découvrir la
version/capacités du moteur ; auditer une source sans mutation ; lancer un rendu
isolé ; suivre/annuler un job ; analyser ses sorties ; relire les mesures. Ces
descriptions ne sont ni des noms d’outils déjà disponibles ni des commandes à lancer.

### 8.2 Informations minimales d’une expérience

- Schéma, identifiant d’expérience, horodatage UTC ISO 8601 et statut explicite.
- SHA du code, état dirty et empreinte des modifications si le rendu utilise
  du code non commité ; build, plateforme, versions du décodeur et de l’analyseur.
- Source : identifiant, SHA-256, provenance/licence, modèle exact, mode et firmware
  lorsque disponibles ; signaler l’absence au lieu d’inventer une valeur.
- Recette complète résolue : profil et SHA, catalogue et SHA, état demandé **et
  appliqué** de calibration/optique, WB réelle, exposition, retouches et défauts.
- Sortie : dimensions, cadrage/orientation, profondeur, espace/primaires/blanc,
  fonction de transfert, ICC, clipping et étapes déjà appliquées.
- Référence/ROI : identifiants et empreintes, masque, repère des coordonnées,
  rééchantillonnage/alignement éventuel et exclusions motivées.
- Mesures avec unités, version des formules, effectifs, distribution et
  incertitudes ; temps à froid/chaud et configuration matérielle.

Un TIFF portant seulement un nom de réglage ne suffit pas pour qualifier un
profil. Une moyenne RGB sRGB ne s’emploie pas comme un RGB caméra linéaire.
Les tampons avant/après normalisation, matrice, rendu et conversion écran
seraient utiles **si** le chantier moteur expose ces étapes avec un contrat
strict ; ne pas annoncer qu’ils sont déjà accessibles.

### 8.3 Sécurité, ressources et reproductibilité

- Exécution locale par défaut ; authentification/autorisation si exposé au réseau.
  Aucun accès distant aux photos personnelles implicite.
- Actions typées et chemins autorisés ; pas de commande shell arbitraire ni
  de téléchargement/exécution automatique de scripts tirés d’une banque.
- Sources en lecture seule ; refus des écrasements, nettoyage explicite et
  extraction d’archives confinée avec limites de taille et contrôle des chemins.
- Jobs isolés, recettes explicites, aucune réutilisation d’un sidecar actif
  ou de la sélection IHM ; pas de mutation des préférences utilisateur.
- File de travaux bornée ; réservation RAM/disque, annulation et quota de sorties.
  Réutiliser la politique mémoire existante, pas un cache MCP non borné en plus.
- Conserver le registre externe et les preuves séparés des exports générés.
  Les JSON horodatés sous `docs/validation/raw/` restent la mémoire canonique ;
  CSV/courbes sont dérivés. Les erreurs et exclusions sont conservées également.
- Les règles de `iprGenerateTests` ne changent pas : entrée `all` ou groupe,
  sources à la racine de base, dossiers partagés, TIFF seuls, pas de manifeste
  de génération. Un éventuel registre de provenance est un artefact d’analyse
  distinct ; ne pas l’ajouter silencieusement à ce générateur.
- Aucun effacement automatique : les images sont supprimées manuellement.
  Une empreinte permet de vérifier un fichier, pas de le reconstruire après suppression.

### 8.4 Premier test de raccordement recommandé

1. Choisir un RAW R6 et un R7 du corpus possédé ; recettes explicites et mêmes profils.
2. Rejouer les neutres approuvés via production/passerelle, à froid puis à chaud.
3. Comparer dimensions et signaux de même domaine : identité attendue sur un même
   chemin déterministe ; tolérance motivée si backend différent. Auditer toute différence.
4. Tester profil absent, mauvaise caméra, recette invalide, sortie existante,
   manque d’espace et annulation ; aucune réussite partielle présentée comme complète.
5. Produire une mémoire JSON puis un résumé dans le rapport RAW existant.
6. Après parité, ajouter un petit lot Cube++ pour la mesure WB, sans entraîner
   un profil R6/R7 à partir des blancs Canon 550D/600D.

## 9. Stockage et volume de démarrage

Proposition initiale, pas inventaire acquis : 10–20 RAW CC0 couvrant quelques
formats/modes, 20–40 scènes Cube++ à illuminant simple et quelques cas mixtes,
plus nos témoins R6/R7. Le chiffre final dépend de la possibilité de récupérer
des fichiers individuels et du budget disque mesuré avant acquisition.

Les fichiers externes restent hors des paquets de distribution et hors Git,
avec un registre de provenance/attribution revu. Ne pas mélanger des fichiers
homonymes dans `base` ; l’identifiant de source doit éviter les collisions.
Si l’archive impose 200 Go, interrompre l’acquisition et choisir un lot léger
ou demander un arbitrage : l’accessibilité documentaire ne garantit pas un
téléchargement sélectif. Ne pas pré-générer tous les TIFF de toutes les banques.

## 10. Conclusion et limites

Nous pouvons préparer l’automatisation et commencer des tests externes sans
attendre une banque parfaite. En revanche, les banques WB et les photos CC0
ne suffisent pas à remplacer à elles seules les DCP R6/R7 par des profils mesurés.
La priorité est la parité production/passerelle, puis les données propres et
les références adaptées à chaque question. Un format JSON documenté suffit
comme source de travail ; un binaire préparé éventuel sert la performance,
pas l’affranchissement des droits.

Cette étude n’active aucune nouvelle calibration, ne déclare aucun profil
indépendant validé, ne modifie pas ADR-0012 et ne constate pas une API MCP livrée.
Les archives n’ont pas été téléchargées ; contrôles de licence et de métadonnées
sur fichiers réels, accès aux liens d’archives et couverture boîtier restent à faire.

## 11. Bibliographie et accès

Sources primaires consultées le **20 octobre 2026**. Les liens ci-dessous
pointent vers les auteurs, éditeurs ou dépôts des projets. Les restrictions
signalées concernent la source consultée ; réexaminer la version réellement acquise.

- **S1. Adobe.** [DNG : spécification, outils et licence de brevets](https://www.adobe.com/support/downloads/dng/dng_sdk.html).
- **S2. Adobe DNG SDK.** [Structure `dng_camera_profile`, miroir du code](https://android.googlesource.com/platform/external/dng_sdk/+/refs/heads/main/source/dng_camera_profile.h).
- **S3. OMPI.** [What Can I Protect with a Copyright?](https://www.wipo.int/en/web/copyright/protection).
- **S4. Légifrance.** [Code de la propriété intellectuelle, L122-4](https://www.legifrance.gouv.fr/codes/article_lc/LEGIARTI000006278911/2023-02-01).
  Référence juridique identifiée lors de l’étude précédente ; accès direct non obtenu lors de cette reprise, à vérifier avant décision juridique.
- **S5. PIXLS.US.** [Banque raw.pixls.us, conditions et licences par entrée](https://raw.pixls.us/).
- **S6. Ershov et al., Cube++.** [Dépôt des auteurs et description des archives](https://github.com/Visillect/CubePlusPlus) ;
  [notice Zenodo 4153431](https://zenodo.org/records/4153431). Le dépôt a été consulté ; l’archive Zenodo n’a pas été téléchargée.
- **S7. Cheng, Prasad et Brown, 2014.** [On Illuminant Detection / jeux multi-camera](https://yorkucvil.github.io/projects/public_html/illuminant/illuminant.html) ;
  [README des auteurs](https://yorkucvil.github.io/projects/public_html/illuminant/readme.txt).
- **S8. Shi et Funt.** [Reprocessing of Gehler’s Dataset, SFU](https://www2.cs.sfu.ca/~colour/data/shi_gehler/).
- **S9. Laakom et al., 2021.** [INTEL-TAU, notice Tampere](https://researchportal.tuni.fi/en/datasets/intel-tau/) ;
  [article des auteurs](https://arxiv.org/abs/1910.10404). La notice de dépôt liée n’a pas pu être consultée.
- **S10. Jiang, Liu, Gu et Süsstrunk, WACV 2013.** [Camera Spectral Sensitivity Database et licence NC-SA](https://www.gujinwei.org/research/camspec/db.html).
- **S11. Burggraaff et al., 2019.** [Standardized spectral and radiometric calibration of consumer cameras](https://arxiv.org/abs/1906.04155),
  Optics Express 27, 19075 ; DOI [10.1364/OE.27.019075](https://doi.org/10.1364/OE.27.019075).
- **S12. Auteurs SPECTACLE.** [Outils et liens de données](https://github.com/monocle-h2020/camera_calibration).
- **S13. Academy Software Foundation.** [rawtoaces : méthodes et licence du code](https://github.com/AcademySoftwareFoundation/rawtoaces).
- **S14. Anders Torger.** [DCamProf : création et diagnostics des profils](https://torger.se/anders/dcamprof.html).
- **S15. Cheng, Price, Cohen et Brown, ICCV 2015.** [Beyond White : données et article](https://yorkucvil.github.io/projects/public_html/beyond/beyond.html) ;
  [contrat des données](https://yorkucvil.github.io/projects/public_html/beyond/readme.txt).
- **S16. Karaimer et Brown, CVPR 2018.** [Improving Color Reproduction Accuracy on Cameras](https://karaimer.github.io/camera-color/) ;
  [article et méthode de création des références](https://openaccess.thecvf.com/content_cvpr_2018/papers/Karaimer_Improving_Color_Reproduction_CVPR_2018_paper.pdf).
