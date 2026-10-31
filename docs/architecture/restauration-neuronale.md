*[Français](restauration-neuronale.md) | [English](restauration-neuronale_en.md)*

# B4 — étude de préparation de la restauration neuronale locale

## État du 31 octobre 2026

**Préparation, pas intégration ni résultat de débruitage.** L'utilisateur autorise
l'étude pendant la qualification alpha. Le moteur classique reste inchangé ;
aucun entraînement, poids téléchargé ou inférence lancé. Les essais alpha ont
priorité et ne doivent pas subir la charge d'un second benchmark.

Cette référence précise le [lot B4 du plan bêta](roadmap-beta.md). Les faits
locaux et limites sont conservés dans le
[JSON de préparation v001](../validation/raw/preparation-b4-v001-20261031T132231Z.json),
pas dans un nouveau rapport de campagne concurrent. Le
[rapport RAW évolutif](../validation/analyses/analyse-developpement-raw.md)
reste le suivi des résultats existants. Documentation sur `patch_documentation` ;
branche locale de prototype `patch_neural_restoration` créée depuis `dev`
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
