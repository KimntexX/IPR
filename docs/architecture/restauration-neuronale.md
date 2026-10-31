*[Français](restauration-neuronale.md) | [English](restauration-neuronale_en.md)*

# B4 — étude de préparation de la restauration neuronale locale

## État du 31 octobre 2026

**Premier banc synthétique passé (§10), pas intégration ni résultat photo.**
L'utilisateur autorise les premiers tests et l'installation de PyTorch dans un
environnement isolé. Le moteur classique reste inchangé ; les essais alpha
gardent la priorité. Les sections 1 à 9 retracent le cadrage préalable, avant
installation et calcul neuronal.

Audit suivant (§11) : réglages des 115 CR3 lunaires vérifiés, trois aperçus
mesurés et mouvement historique réutilisé. Aucune référence propre constituée.

Orientation utilisateur précisée ensuite (§9) : architecture éprouvée,
**poids entraînés pour IPR et signal caméra signé** en priorité ; les modèles
RGB existants restent des comparateurs, pas une cible imposée.

Cette référence précise le [lot B4 du plan bêta](roadmap-beta.md). Les faits
locaux et limites sont conservés dans le
[JSON de préparation v001](../validation/raw/preparation-b4-v001-20261031T132231Z.json),
pas dans un nouveau rapport de campagne concurrent. Le
[rapport RAW évolutif](../validation/analyses/analyse-developpement-raw.md)
reste le suivi des résultats existants. Au cadrage initial : documentation sur
`patch_documentation`, branche locale de prototype `patch_neural_restoration` depuis `dev`
`b19b4c3`, sans worktree, implémentation ni publication à cette étape.

## 1. Conclusion de cadrage

Deux voies à ne pas confondre :

1. **Pilote RGB développé** pour comparer vite des poids disponibles : NAFNet
   SIDD largeur 32, DRUNet couleur et Restormer Real Denoising. Ce pilote évalue
   leur intérêt photographique ; il ne valide pas un remplacement du débruitage RAW.
2. **Cible RAW** : restaurateur conditionné par le bruit sur CFA ou RGB caméra,
   adapté à nos boîtiers et au signal signé. Aucun poids de la sélection ne
   justifie un branchement direct à cette frontière. Décider ensuite adaptation
   ou entraînement, selon le bénéfice du pilote et les données nécessaires.

Recommandation : commencer par NAFNet largeur 32 et DRUNet, puis Restormer si le
premier couple ne donne pas le compromis attendu. C'est un ordre d'essai proposé,
pas un classement mesuré ni un recensement exhaustif de l'état de l'art.
Pas de LLM, de diffusion générative ou de réseau
unique bruit/netteté imposé : le premier objectif est la restauration fidèle.

## 2. Machine de référence et conséquences

| Observation locale | Valeur | Portée |
| --- | --- | --- |
| CPU | Intel Core i7-12700K, 20 threads logiques annoncés | Affinité effective et budget partagé à qualifier |
| RAM physique | 16 933 507 072 octets, environ 15,77 Gio | Environ 5,65 Gio disponibles à la requête ; état variable, non réservé |
| GPU | NVIDIA RTX 3060, 12 288 Mio annoncés | VRAM libre, runtime CUDA et compatibilité ONNX non testés |
| Pilote GPU | 610.88 | Observé par `nvidia-smi`, pas une validation de dépendances |
| OS | Windows 11 AMD64, version 10.0.26300 | Pas de qualification Linux/macOS par cette mesure |

Le GPU rend un pilote local plausible ; il ne prouve ni capacité pleine image
ni délai de 10 s. La RAM système impose de borner également les copies CPU,
pas uniquement les activations GPU. À 32 Mpx, un seul RGBA float32 représente
512 Mo décimaux, avant source caméra, caches, tenseurs et temporaires.

Proposition initiale : une seule inférence à la fois, tuiles 256 puis 512 pixels,
batch 1, FP32 d'abord. Essayer 1024 seulement après mesure des pics. Ces tailles
ne sont pas encore qualifiées et comprennent des halos à définir. Réserver
les ressources via le contrôleur existant ; pas de règle « utiliser toute la
VRAM » ni de multiplication des pools CPU. Le plafond actuel de 8 threads du
filtre classique n'est pas une mesure du meilleur budget neuronal.

## 3. Modèles examinés et droits à lever

| Candidat | Utilité dans le pilote | Limite décisive |
| --- | --- | --- |
| NAFNet-SIDD-width32 | CNN RGB, premier témoin bruit réel | Domaine smartphone SIDD ; pas RGB caméra signé ; attention au contexte spatial des tuiles |
| DRUNet couleur | CNN RGB avec carte de niveau de bruit, utile pour étudier la force | Bruit gaussien de référence différent du bruit Canon dématricé |
| Restormer Real Denoising | Comparateur RGB fondé sur l'attention | Coût et contexte spatial à mesurer ; pas supérieur par définition |
| Learning to See in the Dark (SID) | Référence d'architecture RAW | Poids Sony/Fuji non interchangeables avec Canon ; pas retenu comme pilote prêt à l'emploi |

Les dépôts [NAFNet](https://github.com/megvii-research/NAFNet),
[DPIR/DRUNet](https://github.com/cszn/DPIR) et
[Restormer](https://github.com/swz30/Restormer) fournissent architectures et
références de poids. Les variantes débruitage et défloutage sont distinctes :
ne pas charger un poids GoPro à la place de SIDD. Aucun poids ni nombre exact
de paramètres n'a été vérifié localement. La taille des poids n'est pas le pic
mémoire d'inférence ; pas de chiffre de latence déduit d'un classement PSNR.

Les licences des sources consultées déclarent MIT pour
[NAFNet](https://raw.githubusercontent.com/megvii-research/NAFNet/main/LICENSE)
(avec notices Apache-2.0 de BasicSR),
[DPIR](https://raw.githubusercontent.com/cszn/DPIR/master/LICENSE) et
[Restormer](https://raw.githubusercontent.com/swz30/Restormer/main/LICENSE.md).
Cela ne clôt pas la qualification juridique d'un paquet : avant redistribution,
identifier l'artefact exact, ses droits, notices et données d'entraînement.
Conserver commit source, URL officielle, SHA-256, architecture/configuration,
version d'export et licence des poids. Aucun simple miroir ou tag « open weight »
ne suffit. Aucun tiers n'a été contacté ou contenu utilisateur envoyé.

Le [site SIDD](https://abdokamel.github.io/sidd/) décrit des données smartphone
et des versions RAW/sRGB ; leur disponibilité ne démontre pas la généralisation
aux Canon. Le [README SID](https://github.com/cchen156/Learning-to-See-in-the-Dark)
avertit lui-même de la dépendance des poids au capteur. Aucun corpus externe
n'est téléchargé pour ce cadrage.

## 4. Contrats d'entrée : protéger le chemin actuel

Le code de référence livre deux frontières différentes :

- `CameraDenoise.h` : `CV_32FC3`, RGB caméra **déjà balancé**, blanc 1, valeurs
  finies négatives et supérieures à 1 acceptées ; après RCD/médiane, avant LCP/DCP
  sur le chemin pré-optique retenu. Pas une luminance photométrique universelle.
- `ColorPipeline::WorkingImage` : `CV_32FC4`, Rec.2020 D65 à transfert linéaire,
  alpha droit, plage étendue. Le DCP peut avoir appliqué sa courbe ; ce tampon
  n'est donc pas nécessairement radiométrique scene-linear.

Pour le **pilote RGB**, fabriquer uniquement une copie de rendu neutre dans le
domaine RGB encodé attendu par les poids, en flottants si l'adaptateur le permet.
Comparer le modèle et les témoins sur **la même copie/adaptation**. Tracer chaque
borne et les pixels hors domaine ; ne pas faire passer les pertes de gamut ou
les changements de transfert de l'adaptateur pour un gain neuronal. Ce chemin
de banc SDR n'est ni inversible en général ni une proposition de nouveau master.
Pas d'aller-retour via TIFF IPR pour les mesures : utiliser les buffers en mémoire.

Pour la **cible RAW**, définir explicitement CFA/RGB, ordre des plans Bayer,
noir/blanc par canal, gains, ISO connu/inconnu, bruit dépendant du signal,
orientation et stade du dématriçage. Le réseau doit être entraîné/qualifié sur
ce domaine. Ajouter simplement un décalage aux négatifs ou borner à 0 ne crée
pas une compatibilité valide. Conserver source immuable, sortie flottante et
valeurs étendues selon le contrat ; alpha et métadonnées ne sont pas prédits.

Force 0 = saut exact du réseau. Pour un modèle sans condition de force,
`sortie = entrée + t × (prédiction − entrée)` est un témoin ergonomique à tester,
pas une preuve de débruitage monotone ou d'équivalence LR. La carte de bruit de
DRUNet n'est pas directement notre barre 0..100. Une correction de netteté future
aura ses propres poids, données et tests de faux détails.

## 5. Runtime et tuilage

Référence de calcul : environnement auteur isolé et figé, jamais installation
dans l'environnement des campagnes alpha. Proposition de déploiement :
[ONNX Runtime](https://onnxruntime.ai/docs/execution-providers/), API C++, CPU
pour contrôle et CUDA pour la première machine. La
[licence du runtime](https://github.com/microsoft/onnxruntime/blob/main/LICENSE)
est distincte des modèles et bibliothèques GPU. Export et opérateurs doivent
passer un contrôle de parité avant toute conclusion de qualité.

[ncnn](https://github.com/Tencent/ncnn), C++ CPU/Vulkan, reste un comparateur
de déploiement si conversions et taille du paquet le justifient ; ses
[licences et composants](https://raw.githubusercontent.com/Tencent/ncnn/master/LICENSE.txt)
doivent aussi être audités. Ne pas multiplier les backends au premier pilote.
CoreML/macOS et les autres GPU sont des qualifications ultérieures, pas couverts
par le matériel présent. La documentation
[DirectML](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html)
signale sa maintenance et l'orientation des nouveaux développements vers WinML :
ne pas choisir automatiquement cet ancien chemin pour tout Windows.

Qualifier pooling global de NAFNet et réductions spatiales de Restormer : un halo
fini n'assure pas l'identité pleine image. `NAFNetLocal` est une variante de
configuration à identifier, pas une correction invisible du checkpoint.
Comparer deux grilles décalées, bords, coins et image entière de petite taille ;
rechercher des différences de rendu, pas seulement une couture visible.
Conserver padding, recouvrement, fusion des tuiles et précision dans la recette.

CPU : limiter explicitement pools interne et externe ; mesurer 1/4/8 threads
après libération des tests alpha. La
[documentation ONNX sur les threads](https://onnxruntime.ai/docs/performance/tune-performance/threading.html)
explique notamment la consommation de l'attente active ; désactiver le spinning
pour le premier témoin afin de ne pas brûler du CPU entre deux commandes.
Une annulation empêche la publication et termine les travaux au prochain point
sûr ; une tuile GPU n'est pas nécessairement interruptible instantanément.

## 6. Données disponibles et prévention des biais

Inventaire par noms/extensions, **sans nouveau décodage** : 28 RAW de base
(14 CR3, 14 CR2), 76 RAW hors protocole, 42 CR3 en rafale et 241 CR3 dans le
protocole colorimétrique, dont 77 dans `blacks`. Les groupes se recoupent ;
ni leur somme ni le nombre de fichiers ne donne le nombre de scènes indépendantes.
Les milliers de TIFF ne sont pas déclarés neutres LR valides par cet inventaire.

L'inventaire CFA historique conserve 14/14/14 images de la rafale en
développement/validation/réserve fermée. Les anciennes scènes de contrôle et
les 76 hors protocole ont déjà été examinées : **pas de nouvelle réserve** créée
par renommage ou recadrage. Vérifier provenance et historique avant de figer
le jeu final ; ne pas sélectionner ses exemples selon le résultat d'un réseau.

Pour le premier pilote, aucune nouvelle photo n'est nécessaire pour vérifier
contrats, ressources et bénéfice visuel exploratoire. Pour entraîner ou conclure
sur la fidélité, il faudra des références propres qualifiées : rafales statiques
alignées avec exposition/BdB stables, saturation contrôlée, détails fins, peau,
cheveux, tissus et aplats. Une moyenne de rafale conserve le bruit fixe commun ;
un noir bouchon fermé n'est pas une vérité propre pour les textures éclairées.
Séparer séances/scènes/boîtiers, pas seulement les crops. Auditer le mouvement
de la rafale actuelle avant d'utiliser sa moyenne comme vérité. Les références
LR servent à une comparaison de comportement, pas à une vérité radiométrique
ni à des données d'entraînement implicitement autorisées.

## 7. Banc à préparer puis exécuter après les tests alpha

| Porte | Travail | Sortie attendue |
| --- | --- | --- |
| Q0 | Figer sources, poids/droits, adaptateurs, jeux et masques avant essais | Manifeste interne du banc et empreintes ; pas d'export IPR de mesure |
| Q1 | Chargement local et contrats sur petites images synthétiques | Dimensions/canaux, RGB/BGR, finitude, identité0, source inchangée, erreurs explicites |
| Q2 | Auteur FP32 contre export ONNX FP32 sur mêmes buffers | Erreurs moyenne/max et par canal ; tolérances arrêtées avant campagne |
| Q3 | Tuiles 256/512, deux origines, bords et masque alpha | Raccords, variation contextuelle et pic mémoire ; pas simple mesure sur image réduite |
| Q4 | Trois à six scènes de développement, témoin neutre/classique/neural | Détail et couleur à 100 %, forces et masques identiques, comparaison aveugle si possible |
| Q5 | Qualification gelée, scènes indépendantes, pleine définition | Accord utilisateur, échecs/limites et budget matériel avant intégration |

Ne pas noter une image « meilleure » sur la seule baisse du passe-haut.
Mesurer luma/chroma par signal, contraste des détails, déplacement des couleurs,
noirs/HL et faux contours. PSNR/SSIM uniquement avec référence alignée et
indépendante recevable ; sans elle, annoncer des indicateurs de résidu et une
revue perceptuelle, pas une erreur à la vérité. Éviter les métriques fondées sur
des masques recalculés séparément pour favoriser un candidat.

Ressources : chargement froid séparé, échauffement puis au moins 3 répétitions,
médiane/pire valeur, transfert CPU/GPU, préparation et reconstruction inclus
dans le temps total ; synchronisation GPU pour les chronos. Mesurer pics RAM et
VRAM, pas seulement les poids. Objectif exploratoire utilisateur d'environ 10 s,
pas engagement avant mesures pleine définition. Ne pas mesurer en concurrence
avec LR ou les tests alpha ; consigner toute interférence.

Défauts bloquants : changement de couleur manifeste, détails inventés, couture,
entrée non prise en charge acceptée silencieusement, source altérée, NaN/Inf,
annulation publiant un résultat obsolète, dépassement non maîtrisé ou crash.
Modèle absent/empreinte fausse : message explicite, mode classique disponible,
aucun téléchargement ou changement de backend silencieux. Ne pas exécuter de
scripts tiers ou charger un pickle non vérifié ; prévoir un chargement de poids
limité aux tenseurs et des artefacts de provenance contrôlée.

## 8. Prochaine décision

Finir l'audit des artefacts et geler Q0, puis lancer Q1/Q2 seulement après la
fin des campagnes alpha et libération du budget machine. Choisir le premier
checkpoint et autoriser son acquisition avant téléchargement ; aucun choix
de redistribution n'est acquis. Le présent travail livre les contrats proposés,
l'inventaire et le protocole du banc, **pas un modèle performant démontré**.

## 9. Orientation retenue et besoins d'images — 31 octobre 2026

L'utilisateur retient une **architecture éprouvée avec nos propres poids** et
un contrat de couleur maîtrisé. La cible prioritaire devient le RGB caméra
signé pré-LCP/DCP ; les poids RGB existants restent des comparateurs facultatifs,
pas un passage obligé avant notre apprentissage. Le détail factuel de cet audit
est conservé dans le [JSON de besoins v001](../validation/raw/besoins-corpus-b4-v001-20261031T134511Z.json).

### Ce qui manque réellement

- Plusieurs **scènes différentes avec bruit et référence propre comparable**,
  pas simplement davantage de photos isolées. L'inventaire historique vérifié
  couvre une rafale de 42 images à ISO 4000 et des noirs/prises en pièce sombre,
  tous du R6 Mark III ; il ne constitue pas un jeu supervisé diversifié.
- Des textures faibles : tissus fins, cheveux immobiles ou perruque, papier
  granuleux, bois ; et des couleurs franches : orange/rouge, bleu, verts et gris.
  Sur les panneaux d'escalade, conserver le vrai grain de sable en luminance.
- Des scènes réparties sur plusieurs séances et éclairages stables, avec des
  scènes indépendantes réservées avant réglage. Les noirs existants sont utiles ;
  **ne pas les refaire tous** avant d'avoir audité les séries non classées.
- La qualification du 60D viendra ensuite. Ne pas transférer automatiquement
  les niveaux ISO, le bruit, les poids ou les conclusions du R6 Mark III.

### Première acquisition demandée : trois scènes pilotes

Commencer seulement par trois installations fixes :

1. Tissu fin + mèches immobiles + objet à bord net, éclairage doux.
2. Objets orange/rouge, bleu et vert + plage grise, sans canaux saturés.
3. Aplat gris mat + détail peu contrasté + objet sombre, avec ombres lisibles.

Pour **chaque scène**, R6 Mark III sur trépied, RAW pleine définition non C-RAW
pour le premier lot, mode d'obturation constant, ouverture et mise au point
manuelles fixes. BdB fixe, pas d'Auto ISO ni de correction automatique d'exposition.
Désactiver la réduction de bruit longue pose ; enregistrer les autres réglages
de bruit boîtier. Ne pas déplacer caméra, sujet ou éclairage entre séries.
Éviter écran, LED à scintillement, feuillage, personnes et tissus qui bougent.

- **Référence : 16 vues à ISO 100**, ou 400 si cela permet une séquence plus
  stable ; allonger le temps de pose pour exposer correctement sans écrêtage.
- **Bruit : 16 vues à chacun des ISO 3200, 12800 et 51200**. Raccourcir le temps
  de pose quand l'ISO augmente pour garder une luminosité comparable ; ne pas
  conserver simultanément tous les paramètres d'exposition inchangés.
- Même cadrage, ouverture et éclairage. Contrôler les noirs, gris et couleurs,
  sans chercher à exposer toute la scène à droite. Noter les temps de pose et
  vérifier la stabilité entre la référence et les prises bruitées.

Cela fait **64 RAW par scène, 192 RAW au total**. Les 16 vues de référence ne
sont pas les entrées bruitées d'entraînement. Après alignement et contrôle,
leur agrégation réduit le bruit temporel sans supprimer automatiquement FPN,
pixels chauds ou variations d'éclairage. Référence basse sensibilité et vues à
haute sensibilité nécessitent un raccord radiométrique mesuré ; elles ne sont
pas égales pixel à pixel par simple changement d'ISO. Ne produire aucun TIFF LR.

Faire auditer ce lot avant d'en prendre davantage. Si recevable, proposition
de départ : **12 scènes au total, réparties 8 développement / 2 validation /
2 test fermé**, soit 768 RAW avec cette recette. Les trois pilotes restent en
développement ; toutes les sensibilités et tous les crops d'une scène restent
dans le même groupe. Ce nombre prépare une première étude, pas une garantie de
modèle généraliste : l'entraînement depuis zéro peut demander bien plus de diversité.

### Rafales lunaires sur la carte D:

L'utilisateur confirme une prise sur trépied. Lecture seule : 115 CR3 dans
`D:/DCIM/101EOSR6`, de `6V1A0016.CR3` à `6V1A0130.CR3`, regroupés par dates
d'écriture le 30 octobre vers 22 h 44, 22 h 46 et 22 h 47. Ces groupes ne sont
pas encore des limites de rafales prouvées par EXIF. Le premier aperçu embarqué
montre la Lune ; les autres fichiers et paramètres restent à auditer.

Usage proposé : contrôle exploratoire des cratères, halos, ciel sombre et faux
détails, hors apprentissage dans un premier temps. Trépied ne supprime ni dérive
lunaire, ni turbulence, ni vibrations. Pas de moyenne déclarée « propre » sans
recalage/contrôle ; ne pas sélectionner uniquement les vues qui favorisent le réseau.
La série déjà prévisualisée n'est pas présentée comme un test aveugle scellé.

### Banques publiques : compléments, pas substitution des Canon

| Source | Apport possible | Décision de préparation |
| --- | --- | --- |
| [SIDD](https://abdokamel.github.io/sidd/) | Paires bruit/référence, données RAW et sRGB ; licence MIT déclarée | Premier candidat public, commencer par un sous-ensemble ; smartphone et RAW normalisé ne qualifient pas nos négatifs Canon |
| [raw.pixls.us](https://raw.pixls.us/) | Diversité de scènes et de formats | Seulement fichiers dont CC0 est vérifié individuellement ; pas de vérité propre appariée, bruit synthétique à déclarer |
| [RENOIR](https://ani.stat.fsu.edu/~abarbu/Renoir.html) | Données incluant Canon T3i et S90 | Intérêt à étudier, droits de réutilisation des images non établis ici : pas retenu pour entraînement à ce stade |
| [ELD](https://github.com/Vandermode/ELD) | Modélisation du bruit et comparaison basse lumière | Code MIT, mais jeu annoncé pour évaluation : ne pas l'assimiler à un corpus d'entraînement libre |
| [FiveK](https://data.csail.mit.edu/graphics/fivek/) | Diversité RAW, retouches expertes | Licences de recherche limitant les usages commerciaux ; pas retenu comme base sans restriction |
| [HDR+](https://hdrplusdata.org/dataset.html) | Rafales smartphone et résultats fusionnés, CC-BY-SA déclaré | Étude ultérieure possible pour B7 ; pas de téléchargement complet, ni de droits sur nos poids supposés automatiquement |

La [licence FiveK](https://data.csail.mit.edu/graphics/fivek/legal/LicenseAdobeMIT.txt)
est une licence de recherche malgré le nom MIT dans le jeu ; ce n'est pas la
licence logicielle MIT. Chaque acquisition future doit tracer les droits des
images séparément de ceux du code et des poids. Aucun téléchargement effectué.

### SSD de 500 Go : organisation proposée, aucune opération disque

À la médiane de 38 026 452 octets des 14 CR3 de base, 192 nouveaux RAW occupent
environ **7,3 Go**, 768 environ **29,2 Go** ; tailles futures variables, hors
copies et caches. Les 115 candidats lunaires représentent environ 2,62 Go.
Un SSD annoncé 500 Go est une aide substantielle, pas une raison de matérialiser
tous les tenseurs pleine image : un RGB float32 de 32 Mpx occupe déjà 384 Mo.

Après montage par l'utilisateur, garder un corpus d'apprentissage distinct de
`base` et de ses exports jetables, avec identité boîtier/séance/scène/série et
empreinte des sources. Les noms Canon se répètent : ne jamais copier les vues
lunaires dans `base` en écrasant un homonyme. Sources immuables sauvegardées,
crops calculés à la demande, cache régénérable borné ; budget initial proposé
de 100 Go pour ce cache avec réévaluation après mesure, et marge disque libre.
Le SSD de calcul ne remplace pas une sauvegarde. Ne pas appliquer la purge des
TIFF d'analyse aux RAW d'entraînement nécessaires à la reproductibilité.

Le formatage, le choix du volume et le déplacement de corpus restent à décider
avec l'utilisateur. Aucune action de formatage, copie, suppression ou partition
n'est exécutée par cette préparation.

## 10. Premier banc synthétique et cours — 31 octobre 2026

Sur `patch_neural_restoration`, worktree dédié et environnement `.venv-neural`,
PyTorch **2.9.1+cpu** et NumPy **2.3.5** installés ; Python 3.14.3. Paquets acquis
depuis les index officiels PyTorch/PyPI, pas de poids préentraînés. Le Python
global et les dépendances alpha n'ont pas été modifiés. Aucun calcul GPU lancé.

Le commit local `b6ad93d` livre un petit U-Net résiduel à deux réductions,
largeur 8, **29 995 paramètres**, et un banc CPU. Entrées RGB caméra/sigma par
canal en NCHW float32 ; corps aléatoire et tête nulle au départ. C'est un témoin
de contrat, pas une architecture ou des poids sélectionnés pour la production.

[Preuve JSON](../validation/raw/neural-contracts-v001-20261031T190924Z.json) :
**14 tests réussis**, zéro échec/erreur/skip, environ 1,36 s pour la suite interne,
deux threads intra-op et un inter-op. Contrôles des tailles paires/impaires,
plage étendue à l'identité, force 0 sans appel réseau, source inchangée,
entrées/sorties invalides, annulation avant/après et gradients. Un test fait
16 petits pas Adam sur un seul patch artificiel : vérification du câblage,
**pas apprentissage photographique ni généralisation**. Aucun poids sauvegardé.

Ce temps n'est pas la latence d'une photo. Pas de CUDA, ONNX, tuilage pleine
image, pic RAM/VRAM, bruit Canon ou qualité photo qualifiés. La prochaine porte
est le gel de l'adaptateur/dataset et un essai sur références recevables, pas
une intégration ou un nouvel entraînement massif automatique.

Le [cours 13](../scholar/cours-13-reseaux-neuronaux-de-zero.pdf), avec
[source LaTeX](../scholar/cours-13-reseaux-neuronaux-de-zero.tex) et
[TP exécutable](../scholar/references/reseaux-exemples.py), explique les bases,
gradients à la main, CNN/U-Net, données et qualification. Le TP reste séparé
du banc : démonstration pédagogique, pas nouvelle campagne de validation.

## 11. Audit des rafales disponibles — 31 octobre 2026

L'utilisateur autorise l'audit pendant la préparation de nouvelles scènes.
Outil dédié sur `patch_neural_restoration`, commit `1aec51c`, sept tests de
lecteur synthétique réussis. Mesure conservée dans le
[JSON lunaire v001](../validation/raw/moon-audit-v001-20261031T194552Z.json).
Lecture bornée des CMT1/CMT2, pas interprétation complète des MakerNotes/CTMD.
Traitement séquentiel, un thread OpenCV ; aucune modification de production.

### Ce qui est confirmé pour la Lune

Les 115 fichiers de `D:/DCIM/101EOSR6` déclarent Canon EOS R6 Mark III,
200 mm, objectif `70-200mm F2.8 DG OS HSM | Sports 018`, orientation1.
Le trépied reste une information fournie par l'utilisateur, pas une mesure EXIF.

| Fichiers | Nombre | ISO | Temps de pose | Ouverture |
| --- | --- | --- | --- | --- |
| `6V1A0016.CR3` à `6V1A0049.CR3` | 34 | 1600 | 1/200 s | f/5,6 |
| `6V1A0050.CR3` à `6V1A0130.CR3` | 81 | 1000 | 1/200 s | f/9 |

Les horodatages de prise confirment le 30 octobre vers22h44–22h47.
La seconde plage comprend deux groupes horaires ; l'égalité des paramètres
ne prouve pas la continuité d'une rafale. Ne pas moyenner les 115 images en bloc :
gain et ouverture diffèrent, et le cadre s'est déplacé entre les aperçus.

Les JPEG embarqués de0016/0073/0130 font6960×4640 pixels. La zone lumineuse
détectée mesure environ300 pixels de côté : intérêt local sur la Lune, pas
115 scènes ni des exemples variés de textures et couleurs. Les niveaux médians
encodés de cette zone sont159/75/78. Le premier aperçu contient environ1,17%
de canaux à255 dans cette zone ; les deux autres0%. Ce n'est **pas un diagnostic
d'écrêtage RAW** : courbe, balance et traitement JPEG peuvent saturer un canal.

Le proxy Laplacien varie entre les vues, mais n'est pas utilisé pour classer
leur netteté : exposition et traitement changent aussi sa valeur. La boîte de
sélection est en coordonnées JPEG, pas un ROI capteur certifié. Les empreintes
RAW complètes sont conservées pour ces trois vues ; les autres ont seulement
les empreintes des blocs de métadonnées et leur taille/date, pas un hash complet.

**Décision :** série candidate aux tests exploratoires de détails et halos,
hors apprentissage pour l'instant. À qualifier ensuite en RAW : saturation,
alignement par sous-série, couverture, turbulence et stabilité des textures.
Ni moyenne « propre » ni référence supervisée créée à partir des JPEG.

### Réutilisation de la rafale historique de 42 vues

Le banc ne relit aucun pixel des groupes développement/validation/réserve.
Il réutilise le JSON de mouvement du27 octobre, dont le chemin et l'empreinte
sont conservés dans la nouvelle preuve. L'audit ancien portait seulement sur
les14 premières vues de développement, avec aperçu CFA de800 pixels de large.
Il rapportait jusqu'à5,19 pixels de translation globale,0,427° de rotation
absolue et une échelle entre1 et1,00458. Ne pas convertir ces valeurs en
déplacements RAW natifs sans reprendre les conventions exactes de l'ancien banc.

Ces résultats interdisent une moyenne naïve comme vérité de texture. Ils ne
réfutent pas une référence après recalage : affine globale, parallaxe et mouvement
local doivent encore être contrôlés sur les pixels autorisés. La réserve reste
fermée. Les anciennes images examinées ne deviennent pas un nouveau test vierge.

### Suite retenue

Auditer le premier lot de nouvelles prises avant extension, en conservant
scène/séance/réglages et groupes indépendants. Pour les sources existantes,
geler d'abord le domaine RAW et le traitement d'alignement avant toute cible
de moyenne ; pas de reprise d'un entraînement ni de modification des poids
sur la base de métriques d'aperçu. Aucun RAW copié, exporté, supprimé ou modifié.
