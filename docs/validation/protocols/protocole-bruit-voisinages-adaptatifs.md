# Étude d’un débruitage adaptatif multiscale pour IPR

Statut : protocole expérimental proposé le 23 octobre 2026, sans validation ni intégration en production implicite.

Ce document peut être confié à une personne ou à un modèle IA. Il complète le
[protocole Bruit](protocole-bruit.md) pour une hypothèse spécialisée ; il ne le
remplace pas. Appliquer les [conditions communes](protocole-commun.md).
Consigner les résultats dans le [rapport Bruit unique](../analyses/analyse-bruit.md).

## 1. Objectif

Évaluer si un débruitage classique combinant voisinages adaptatifs, comparaison
de blocs et fusion statistique peut améliorer le prototype IPR.

La priorité est de réduire le bruit, notamment chromatique, sans supprimer les
textures ni créer de halos. Lightroom constitue une référence visuelle, pas une
vérité terrain ni une cible à reproduire exactement.

Aucune intégration en production avant comparaison mesurée et validation visuelle.

## 2. État de départ

Le prototype IPR utilise une estimation du bruit et un traitement multiscale
séparant luminance et composantes chromatiques.

Deux scènes ont déjà été comparées :

- `6V1A9608`, ISO 16000 ;
- `6V1A9890`, ISO 25600.

Les résultats montrent un bruit chromatique résiduel important après traitement,
particulièrement sur `9890`. Les derniers exports Lightroom utilisent la
réduction **manuelle** luminance/couleur à 100 et la netteté à 0. Ils ne constituent
pas une référence de débruitage IA.

Lire le rapport et les JSON associés avant de commencer. Vérifier le code et les
réglages actuels : cet état de départ décrit la comparaison du 23 octobre 2026,
pas une garantie sur les versions futures.

## 3. Hypothèses à tester

Tester séparément les hypothèses suivantes :

1. Agrandir le voisinage améliore l’estimation dans les zones homogènes.
2. Une sélection des voisins par similarité de blocs protège mieux les textures
   qu’une sélection par distance seule.
3. Une correction multiscale traite mieux les différentes tailles de bruit.
4. Une fusion pondérée par confiance est plus robuste qu’une moyenne fixe.
5. Un traitement chromatique spécifique réduit les taches colorées sans dégrader
   les contours de couleur.

Ne pas modifier plusieurs mécanismes simultanément sans pouvoir mesurer leur
contribution respective.

## 4. Corpus et références

Utiliser les RAW disponibles, en couvrant autant que possible :

| Type de zone | Ce qu’elle permet de vérifier |
|---|---|
| Surface uniforme claire et sombre | Bruit résiduel, taches, bandes |
| Cheveux, tissu, feuillage | Conservation des textures fines |
| Texte et contours contrastés | Netteté, halos, dédoublements |
| Couleurs saturées | Bavures et désaturation |
| Ombres profondes | Bruit chromatique et faux détails |

Pour chaque scène, comparer :

- rendu neutre IPR ;
- prototype IPR actuel ;
- méthodes expérimentales ;
- référence Lightroom manuelle ;
- référence IA, uniquement si elle est fournie et identifiée séparément.

Les réglages optiques, le profil, la balance des blancs et les autres retouches
doivent être identiques entre les états d’un même logiciel. Consigner les
différences entre logiciels. Ne pas confondre un changement du rendu d’ouverture
avec une amélioration du débruitage.

Les neutres existants avec optique désactivée et les exports traités avec optique
activée ne permettent pas de mesurer isolément l’efficacité du débruitage.
Ils restent exploitables dans des comparaisons distinctes et explicitement bornées.

## 5. Conditions expérimentales

Conserver le même signal d’entrée pour tous les candidats IPR. Documenter :

- commit, état local du code, configuration et paramètres ;
- modèle du boîtier, ISO et dimensions ;
- espace colorimétrique et étape du pipeline ;
- matériel, nombre de threads et éventuel GPU ;
- temps d’analyse, temps de correction et mémoire maximale.

Ne pas appliquer directement des coefficients de bruit capteur à une image déjà
dématricée et transformée sans justification : ces opérations modifient la
distribution et les corrélations du bruit.

Mesurer les rendus IPR en mémoire avec les interfaces disponibles. Le générateur
TIFF est réservé à la revue visuelle demandée ; ne pas générer de TIFF IPR pour
les seules mesures. Ne supprimer ni écraser aucune source ou référence.

## 6. Méthode proposée

Les mécanismes ci-dessous sont des candidats à expérimenter, pas des fonctions
annoncées comme déjà disponibles dans IPR.

### A. Estimation du bruit

Estimer séparément le bruit de luminance et de chrominance, en fonction du niveau
de signal lorsque les données le permettent. Utiliser des estimateurs robustes
et distinguer les surfaces homogènes des textures. Produire une estimation de
confiance.

Un résidu élevé ne suffit pas à déclarer un pixel bruité : il peut représenter
un détail réel.

### B. Voisinages adaptatifs

Tester plusieurs tailles, par exemple `3×3`, `5×5`, `9×9` et `17×17`. Ces tailles
sont des candidats expérimentaux, pas des constantes validées.

Limiter ou réorienter le voisinage près des contours. Les lectures horizontales,
verticales et diagonales peuvent fournir des indices de structure, mais ne sont
pas des observations indépendantes.

### C. Similarité de blocs

Comparer les petits blocs entourant les pixels dans une fenêtre de recherche
plus grande. Une pondération candidate est :

\[
w_{ij} =
\exp\left(-\frac{d(P_i,P_j)}{h_i^2+\varepsilon}\right)
\exp\left(-\frac{\|i-j\|^2}{2r^2}\right).
\]

Ici :

- \(P_i,P_j\) sont les blocs comparés ;
- \(d\) mesure leur différence, idéalement corrigée ou normalisée selon le bruit
  attendu ;
- \(h_i\) dépend du bruit estimé et doit avoir des unités compatibles avec \(d\) ;
- \(r>0\) règle la préférence pour les voisins proches ;
- \(\varepsilon>0\) protège le dénominateur.

Normaliser les poids et prévoir un repli lorsque trop peu de voisins fiables
existent. Spécifier le traitement des bords et des valeurs non finies.

### D. Traitement multiscale

Décomposer le signal en couches de différentes échelles. Appliquer une correction
adaptée à chaque couche et à chaque composante. Vérifier que les basses fréquences,
la luminosité moyenne et les contours ne dérivent pas.

### E. Fusion statistique

Comparer un petit nombre d’estimateurs complémentaires : médiane robuste,
moyenne pondérée par similarité et estimation multiscale.

Une fusion candidate est :

\[
\hat{x}_i=\sum_k\alpha_{ik}\hat{x}_{ik},
\qquad \alpha_{ik}\geq0,\quad\sum_k\alpha_{ik}=1.
\]

Les poids doivent dépendre de la confiance locale. Ne pas considérer les
estimateurs comme indépendants s’ils utilisent les mêmes pixels. Prévoir un
mécanisme conservateur lorsque les modèles sont en désaccord.

## 7. Plan de comparaison

Procéder par étapes :

| Variante | Modification étudiée |
|---|---|
| Référence | Prototype actuel |
| A | Voisinages adaptatifs seuls |
| B | Similarité de blocs seule |
| C | Combinaison A + B |
| D | C avec fusion par confiance |

Conserver le multiscale actuel pour isoler d’abord ces contributions. Étudier
ensuite sa modification si nécessaire. Comparer également D à C avec une fusion
à poids fixes pour tester réellement l’hypothèse de confiance adaptative.

Choisir les paramètres sur une partie du corpus, puis les vérifier sur des scènes
non utilisées pour le réglage. Les variantes expérimentales ne sont pas des modes
de compatibilité à conserver en production : seule la méthode retenue sera intégrée.

## 8. Mesures et revue visuelle

Mesurer sur les mêmes zones :

- indicateurs robustes de bruit de luminance et de chrominance ;
- bruit à plusieurs fréquences ;
- conservation des textures et des contours ;
- dérive de luminosité et de couleur ;
- halos, taches, bandes et aspect « plastique » ;
- temps de calcul et mémoire.

Sur des images synthétiques disposant d’une référence propre, ajouter MAE, PSNR
et éventuellement SSIM. Ne pas interpréter ces mesures comme une fidélité réelle
sur des photos sans référence propre. Un bruit synthétique gaussien seul ne valide
pas le comportement sur un RAW dématricé avec bruit corrélé et dépendant du signal.

Examiner les résultats à 100 % et à taille d’affichage normale, si possible en
comparaison aveugle. Consigner les coordonnées des zones, l’alignement et les
exclusions. Ne pas réduire le bruit mesuré artificiellement par redimensionnement
ou interpolation d’alignement.

## 9. Budget de calcul et ergonomie

Cible exploratoire : environ dix secondes, sur une résolution et une machine
explicitement définies. Ce n’est pas une garantie universelle.

Séparer l’analyse réutilisable, l’application de la force choisie, l’aperçu
interactif et le rendu final. Prévoir progression, annulation, mémoire bornée et
libération des ressources. Le traitement ne doit pas bloquer l’IHM.

Le GPU est une option à évaluer par mesure, pas un prérequis. Compter les
transferts mémoire et vérifier la cohérence numérique avec le chemin de référence.

## 10. Livrables et décision

Fournir :

- un prototype expérimental distinct du chemin de production ;
- les paramètres et commandes reproductibles, en signalant les outils encore
  à développer plutôt qu’en inventant des commandes ;
- les mesures dans un JSON versionné et horodaté sous `docs/validation/raw/`,
  mémoire canonique des résultats ;
- des crops comparatifs ;
- une mise à jour chronologique du rapport Bruit unique ;
- une conclusion précisant les gains, régressions, limites et coût matériel.

Retenir une variante uniquement si elle améliore le compromis bruit / détails /
artefacts / latence sur plusieurs scènes. Une image simplement plus lisse ne
constitue pas une validation. Définir les seuils d’acceptation avant la comparaison
finale et demander une validation visuelle avant toute intégration en production.

## 11. Reprise après changement de dématriçage — 24 octobre 2026

Après livraison validée du nouveau dématriçage, fusionner `dev` dans
`patch_noise`, reconstruire les sondes et conserver les anciennes campagnes
comme historiques. Ne pas réutiliser sans mesure une signature spectrale
caractérisée avec un autre dématriçage.

La sonde `noiseNeighbourhoodProbe` et son lanceur
`tests/study_noise_neighbourhoods.py` mesurent les masters en mémoire.
Pour la caractérisation des paires :

```powershell
python tests/study_noise_neighbourhoods.py --data-version N `
  --pairs-from docs/validation/raw/AUDIT.json `
  --pairs-root CHEMIN_COLORIMETRIE --jobs 1
```

Remplacer `N`, `AUDIT.json` et le chemin par les entrées vérifiées. Choisir le
parallélisme selon la mémoire disponible ; une paire retient deux masters et
plusieurs plans. La mesure est une différence de prises, pas une image propre.

Comparer les campagnes de paires avec :

```powershell
python tests/summarise_noise_rebaseline.py `
  --data-version N `
  --before ANCIEN_JSON --after NOUVEAU_JSON `
  --audit AUDIT_JSON --output NOUVELLE_SYNTHESE_JSON
```

La synthèse apparie les empreintes des sources et les classes de luminance,
sépare Y/Co/Cg, groupe par ISO des métadonnées et exclut, dans sa lecture
principale, les paires à dérive moyenne d'exposition supérieure à 2 %. Une
sensibilité sans cette exclusion accompagne les résultats. Ce seuil est une
précaution analytique, pas une certification des autres paires. Les tuiles calmes
retenues peuvent changer avec le dématriçage ; les classes ne sont pas des
observations indépendantes.

Pour une première reprise photographique ciblée :

```powershell
python tests/study_noise_neighbourhoods.py --data-version N `
  --base CHEMIN_BASE --base CHEMIN_BASE_COMPLEMENTAIRE `
  --candidate-e-only --scene 6V1A9608 --scene 6V1A9890
```

Le mode `--candidate-e-only` compare la référence de production aux trois réglages
E courants et mesure E sur l'image entière à huit threads. Il ne s'applique pas
à `--pairs-from`. Tracer le binaire et les paramètres utilisés ; garder le
candidat inchangé pour la première comparaison, avant de recalibrer son profil.
Une revue visuelle reste obligatoire : un ratio de bande plus faible ne prouve
pas une meilleure préservation du détail.

## 12. Extension du corpus et étude des impulsions

Avec l'autorisation de l'utilisateur, `tests/inventory_noise_sources.py` lit les
métadonnées CR2/CR3 dans les racines indiquées par `--root`, sans dématriçage,
suppression ni écriture sur ces racines. `--output` désigne un nouveau JSON
horodaté et `--data-version` sa version. Les chemins identiques sont dédupliqués,
pas les contenus : plusieurs copies d'une photo peuvent rester dans l'inventaire.
Les erreurs de lecture sont consignées et exclues de la sélection.

`tests/preview_noise_sources.py --inventory INVENTAIRE --scene NOM ... --output
PLANCHE.png` utilise les aperçus embarqués pour sélectionner les sujets. La
planche est un dérivé 8 bits, jamais une source de mesures. Les mesures natives
peuvent lire directement les fichiers dans leurs dossiers : aucune copie dans
base n'est nécessaire. Les originaux doivent rester disponibles pendant le calcul.

La variante expérimentale F précède E d'une correction de luminance : médiane
des huit voisins hors centre, seuil de 4/6/8 sigma de la bande pixel Y,
au moins six voisins proches de la médiane et protection lorsqu'une paire
opposée soutient la valeur du centre. Le centre et les voisins ne sont pas
réinjectés dans une boucle récursive. Un sigma absent ne doit pas être traité
comme la certitude d'un bruit nul. Ce filtre est dans `tests/`, pas en production.

Commandes de contrôle :

```powershell
ctest --test-dir build-patch_noise -C Release -R noiseImpulseTests --output-on-failure
python tests/study_noise_impulses.py --data-version N --output NOUVEAU_JSON
python tests/study_noise_neighbourhoods.py --data-version N `
  --impulse-study --base DOSSIER_RAW --scene NOM
```

Les contrôles synthétiques utilisent un sigma connu et des scènes propres
connues (aplat, bord, ligne fine, point d'un pixel, étoile étalée, texture,
rampe HDR), avec bruit gaussien et impulsions ajoutées. Ils ne simulent pas
fidèlement le bruit corrélé et non gaussien après RCD. Mesurer MSE, détection
des impulsions ajoutées et changements hors injection séparément : un changement
hors injection peut retirer du bruit gaussien, il n'est pas automatiquement
un faux positif sémantique.

Sur les photos réelles, comparer E et F sur la même entrée native. Pour les
images globalement très sombres, ajouter des zones centrées sur des éléments
lumineux : la sélection standard peut ne retenir que du fond noir. Ces zones
`bright-feature` ne sont ni des étoiles certifiées ni une vérité propre.
Tracer le choix des zones, leur signal et la disponibilité de sigma ; ne pas
assimiler une zone non mesurée à une réussite.

Ne pas valider F sur sa seule moyenne MSE. La suppression d'un détail réel
ponctuel est une limitation bloquante pour une intégration automatique non
conditionnée. Sans information supplémentaire, un détail d'un pixel et une
impulsion peuvent produire exactement les mêmes observations locales.
Les temps de F sur ROI ne constituent pas une latence pleine image validée.

## 13. Soutien du profil et abstention — étude G

```powershell
python tests/study_noise_neighbourhoods.py --data-version N `
  --profile-study --base DOSSIER_RAW --scene NOM
```

Ce périmètre est exclusif des modes E seul, F et paires. Il compare les mêmes
zones de la référence, des trois réglages E et de deux variantes G appliquées
au réglage E-l1-c1-f2. Les éléments lumineux des scènes sombres sont inclus.
G est un outil expérimental dans `tests/`, jamais une option de production.

Le profil E n'est pas réestimé : l'expérience isole l'effet d'un garde-fou.
Pour chaque classe de luminosité et chaque plan Y/Co/Cg, conserver dans le JSON
le nombre de tuiles 32 × 32, le nombre admissible, les quartiles HH et le verdict
de soutien. Les seuils suivants sont des hypothèses exploratoires, non une
probabilité calibrée ni une preuve de séparation bruit/texture :

- tuiles : gradient moyen de Y lissé G2 inférieur ou égal au HH Y ; au plus
  1 % des valeurs Y hors de l'intervalle strict (0, 1) ; HH Y supérieur à 10⁻⁷ ;
- classe/plan : au moins 16 tuiles admissibles, q25 HH > 10⁻⁷ et q75 ≤ 2 q25 ;
- G-support : interpoler les verdicts 0/1 des classes ; une classe manquante
  contribue zéro, sans remplissage par la classe valide la plus proche ;
- G-structure : multiplier ce poids par `1 / (1 + (gradient G2 / HH Y)⁴)`.

Le résultat est `entrée + poids × (E − entrée)`. Un poids nul doit restituer
exactement l'entrée. La modulation peut néanmoins rompre la conservation de
moyenne d'E ou créer des transitions de force : mesurer ces défauts, sans
ajouter une compensation destinée à masquer un échec. Tracer les poids moyens
par plan sur les ROI rembourrées, les dérives sur le cœur et les ratios de
bandes. Ne pas appeler ces poids « confiance statistique ».

Les contrôles joints à la campagne comprennent aplat, bord, point d'un pixel,
étoile étalée, texture et rampe, deux graines et bruit gaussien indépendant
σ = 0 ou 0,01 dans Y/Co/Cg. Leur vérité propre permet MSE et changement maximal.
Ils ne reproduisent pas le bruit RCD corrélé, les faibles signaux écrêtés ou
la distribution du bruit d'un capteur. Ils ne suffisent pas à valider G.

Examiner en particulier texture lunaire, étoiles, pierre/textiles et ombres.
Une meilleure conservation obtenue en ne corrigeant rien n'est pas une réussite
du débruitage : lire ensemble conservation, abstention et bruit résiduel.
Les timings G par ROI ne portent que sur le garde-fou ; ajouter le coût d'E
et du profil de soutien. Seul E est chronométré en pleine image : ne pas
annoncer un temps complet G ni une validation de latence IHM.

## 14. Diagnostic spectral et directionnel, avant un nouveau filtre

`tests/study_noise_spectral.py` utilise le même probe natif avec les entrées
`--spectral-pairs` et `--spectral-scenes`. Il ne débruite pas et n'écrit aucune
image de mesure. Il produit un JSON canonique versionné/horodaté.

```powershell
python tests/study_noise_spectral.py --data-version N `
  --audit docs/validation/raw/AUDIT_PAIRES.json `
  --rebaseline docs/validation/raw/REPRISE_RCD.json `
  --pairs-root DOSSIER_COLORIMETRIE `
  --base DOSSIER_RAW --scene NOM
```

Les deux documents d'entrée sont des preuves existantes, non des fichiers à
fabriquer. Dans les paires admissibles de la reprise RCD, choisir deux paires
aux ISO 100, 1600, 6400 et 51200, par nombre décroissant de classes mesurables,
puis nom. Vérifier les empreintes et ne réutiliser aucun RAW entre paires.
La première paire de chaque ISO sert à construire la référence, la seconde
reste hors ajustement. Les ISO viennent de l'audit des métadonnées, pas du
nom du dossier. La reprise et l'audit imposent des conditions compatibles ;
arrêter si une paire manque ou si une empreinte change.

Pour chaque paire développée A/B, former `D = (A − B) / √2` et `M = (A+B)/2`.
Refuser une dérive d'exposition moyenne supérieure à 2 %. Parmi les tuiles
32 × 32 dont le gradient Y de G2(M) est inférieur à 3 HH(DY), sélectionner
au plus 32 tuiles régulièrement réparties dans chaque classe Y. Conserver
leurs coordonnées, le nombre de candidats et les signatures de D et A.
Les tuiles ne sont pas indépendantes ; le seuil ne garantit pas l'absence
de structure ou de mouvement dans D. D donne une référence différentielle,
**pas la distribution marginale complète d'une image après RCD non linéaire**.

Dans chaque tuile et plan, mesurer huit descripteurs sans unité :

- logarithmes des rapports d'énergie Haar LH/HH et HL/HH ;
- corrélations de HH à un décalage d'un coefficient, selon x et y ;
- logarithmes des énergies `x−G1` et `G1−G4`, rapportées à HH ;
- platitude spectrale : moyenne géométrique/arithmétique des puissances DFT ;
- part des huit puissances DFT les plus élevées dans l'énergie retenue.

Pour la DFT, centrer la tuile, appliquer une fenêtre de Hann 2D et exclure
le disque de rayon deux cases fréquentielles autour du continu. Le plancher
relatif des puissances est 10⁻¹² ; celui des énergies Haar est 10⁻¹² fois
l'énergie centrée. Un plan constant/négligeable est « non mesurable », pas
« bruit nul certain ». Les entrées non finies ou de géométrie/type incorrects
sont refusées. Les tests vérifient aussi l'invariance approximative à l'échelle.

Comparer quatre groupes : échelles seules, spectre seul, directions seules,
et combinaison. Construire une enveloppe par plan et classe Y, en regroupant
les ISO, seulement avec au moins 64 tuiles D de deux paires d'ajustement.
Pour chaque caractéristique, utiliser médiane et `max(1,4826 MAD, 0,02)`.
Le score est le maximum des écarts absolus standardisés ; son seuil est le
95e percentile des seuls scores d'ajustement. Ne pas réajuster ces paramètres
sur les paires réservées, les contrôles ou les photos. Une classe sans soutien
entraîne une abstention.

Ce score n'est **ni une probabilité de bruit, ni une décision de pixel bruité**.
Un détail peut être compatible avec le bruit, et un bruit d'un autre domaine
peut être rejeté. Mesurer sur les paires réservées la part à l'intérieur de
l'enveloppe, par ISO, classe, plan et groupe ; tracer les dénominateurs et
abstentions. Les signatures A des mêmes tuiles sont des diagnostics appariés,
pas de nouveaux exemples de bruit de vérité.

Les contrôles comprennent aplat, textures fine/grossière, damier, bord, point,
étoile et rampe, deux graines et trois corruptions : aucune, gaussienne blanche,
gaussienne lissée G0,7 puis remise à σ = 0,01. Le même bruit est réutilisé par
graine pour comparer les scènes : ces répétitions ne sont pas indépendantes.
Il ne s'agit pas d'une simulation du bruit RCD. Comparer chaque contrôle aux
classes Y disponibles séparément, sans choisir après coup la plus favorable.

Sur les photos, retenir 16 tuiles 32 × 32 par ROI native (pas de 64 px) et
les plans Y/Co/Cg. Conserver aussi les classes sans modèle. Les diagnostics
R7/60D ne valident pas un transfert de la référence R6 Mark III. Une signature
spectrale rassurante n'autorise pas une intégration automatique : il restera
à vérifier l'estimateur de bruit, le filtrage, la moyenne, le détail et le coût.

Contrôle complémentaire plus proche du domaine développé :

```powershell
python tests/study_noise_spectral.py --data-version N `
  --injection-from docs/validation/raw/ETUDE_SPECTRALE.json
```

Le complément réutilise **sans ajustement** les enveloppes du JSON parent et
relit seulement ses paires réservées, avec vérification des empreintes.
Pour chaque classe/plan, prendre la première et la tuile médiane des tuiles
échantillonnées de D. Ajouter à ce résidu, sans remise à l'échelle du bruit,
des motifs propres connus (textures, damier, bord, point, étoile, rampe), dont
le pic absolu vaut 1, 3 ou 10 écarts-types mesurés du résidu. Un aplat sert de
contrôle « bruit seul » une seule fois. Tracer coordonnées, moyenne/écart-type
du résidu, amplitude du motif, signature et diagnostic par famille.

Les 22 états par résidu ne sont pas indépendants : ils partagent le même
bruit. Chaque case de comparaison utilise les mêmes résidus, et les classes
sans modèle restent des abstentions. Une compatibilité d'un motif faible
avec le bruit n'est pas automatiquement une erreur ; elle indique une limite
de détectabilité. D est un bruit différentiel après développement : ce montage
ne simule pas une acquisition RAW, les interactions motif/dématriçage, ni le
biais marginal d'une image unique. Aucun gain MSE de débruitage ne s'en déduit.

## 15. Profil de bruit à partir des tuiles spectralement compatibles

Étape demandée par l'utilisateur le 24 octobre 2026 : fiabiliser l'estimation du
bruit d'E avant de renforcer le débruitage. `tests/study_noise_profile.py`
utilise le probe natif avec l'entrée `--profile-pairs`, qui rend chaque paire
en mémoire (RCD/FlatOnly, master Rec.2020 linéaire) et décrit **toutes** les
tuiles 32 × 32 de la grille, sans débruiter ni écrire d'image.

```powershell
python tests/study_noise_profile.py --data-version N `
  --parent docs/validation/raw/etude-spectrale-bruit-v001-20261024T195453Z.json
```

Règles fixées avant lecture des paires réservées :

- **Entrées** : les huit paires R6 Mark III de la campagne spectrale parente,
  avec leur partage ajustement/réserve ; empreintes vérifiées.
- **Vérité** : par plan Y/Co/Cg et classe Y de l'image moyenne M, médiane du
  sigma de D = (A − B)/√2 sur les tuiles calmes (gradient G2 de M < 3 sigmas HH
  de D(Y)), au moins 8 tuiles ; en HH de Haar et en MAD de la bande G1 − G4.
- **E** (référence) : quartile 25 % des sigmas HH de toutes les tuiles de la classe
  de A, divisé par son biais ; classe vide recopiée depuis la plus proche ;
  bande moyenne déduite de HH par le rapport spectral d'E.
- **S** : tuiles de A dans l'enveloppe combinée gelée de la classe (parent) ;
  médiane de leur sigma, au moins 16 tuiles, sinon **abstention** ; un facteur
  par plan et par échelle, médiane vérité/estimation sur les paires
  d'ajustement seulement.
- **P** : comme S, avec une enveloppe combinée par plan, mise en commun sur
  toutes les tuiles D d'ajustement de toutes les classes (médiane, 1,4826 MAD,
  plancher 0,02, seuil au 95e percentile des scores d'ajustement).
- **Score** : log2(estimation / vérité) sur les paires réservées, par plan,
  classe, échelle et ISO. Aucun seuil n'est réajusté après lecture.

Limites : un seul boîtier ; D est un résidu différentiel après développement,
qui efface ce que les deux vues ont en commun (dont un motif fixe du capteur) ;
tuiles corrélées ; les constantes d'E viennent de synthèses antérieures qui
peuvent inclure ces paires.
