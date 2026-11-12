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

Rejeu photographique (point 3) : `tests/study_noise_neighbourhoods.py
--estimate-study` ajoute la variante E-P-l1-c1-f2, même force qu'E-l1-c1-f2,
profil P calculé dans le probe (enveloppes et facteurs de etude-profil-bruit-v002),
sigma nul dans une classe de moins de 16 tuiles compatibles. Le profil Q (quartile
25 % des tuiles compatibles) est un contrôle de `tests/study_noise_profile.py`.

## 16. Contamination contrôlée du profil P

```powershell
python tests/study_noise_contamination.py --data-version N `
  --profile docs/validation/raw/etude-profil-bruit-v002-20261024T224800Z.json
```

Conserver strictement les enveloppes et facteurs du JSON de profil fourni ;
le lanceur les compare aux valeurs réellement compilées. N'utiliser que ses
quatre paires réservées, empreintes vérifiées et disjointes de l'ajustement.
Rendre en mémoire A/B, former D, figer les classes Y de A et M, la sélection
calme et la référence D : l'injection ne modifie aucune de ces références.

Échantillonner au plus 256 tuiles régulièrement réparties par classe Y de A.
Ce profil échantillonné n'est pas celui de l'image entière de v012 : comparer
d'abord chaque mélange à son propre état sans injection. Les références
par classe sont les médianes D des tuiles échantillonnées calmes, au moins huit.
Le profil P exige seize tuiles compatibles, sans remplissage d'une autre classe.

Injecter séparément dans chaque plan une texture fine (fréquences 0,9 et 0,7
radian/pixel) ou grossière (0,3 et 0,25), fenêtrée par Hann, à moyenne nulle
et RMS égale à l'écart-type du résidu D local. **RMS à 1 σ n'est pas pic à 1 σ**.
Deux phases évitent une unique réalisation spatiale. Par classe et graine
(733/947), classer déterministement les coordonnées et contaminer les premiers
0, 10, 25, 50 ou 100 % des échantillons ; les ensembles sont emboîtés.
Tracer la fraction réelle (arrondie à l'entier inférieur), pas seulement demandée.

Recalculer compatibilité et HH des tuiles touchées et la bande moyenne avec
le contexte des injections voisines (marge 16 pour G4). Les mesures sur les
autres tuiles échantillonnées conservent ce contexte : ne pas simuler chaque
tuile avec une frontière réfléchie indépendante. Vérifier par contrôle
numérique l'équivalence de la convolution locale et pleine image.

Conserver coordonnées, écart-type D, comptes injectés/compatibles, P HH/milieu,
référence et erreurs log2, rapport à l'état sans injection, abstentions et
moyenne de la perturbation. Les médianes à effectif pair moyennent les deux
valeurs centrales, comme NumPy lors de la calibration. Les graines et motifs
réutilisent les mêmes photos : ces essais ne sont pas indépendants. À 100 %,
les deux ordres modifient le même ensemble ; ne pas les compter comme deux
réalisations indépendantes.

Cette injection après développement ne simule pas les interactions texture/CFA/
RCD ni la distribution marginale d'une vue unique. Le résidu D est une référence
différentielle, pas une vérité absolue du bruit. Aucune réduction d'image ni
amélioration MSE n'est mesurée. L'abstention du profil n'est pas automatiquement
l'identité d'un filtre qui contient médiane et rééchantillonnage : vérifier ce
contrat avant tout nouveau rejeu photographique. Aucun TIFF n'est écrit.

## 17. Enveloppe conjointe des signatures — diagnostic hors ligne

```powershell
python tests/study_noise_joint.py --data-version N `
  --profile docs/validation/raw/etude-profil-bruit-v002-20261024T224800Z.json `
  --injection docs/validation/raw/etude-injection-spectrale-bruit-v001-20261024T195952Z.json
```

Réutiliser uniquement les signatures enregistrées ; aucun RAW n'est ouvert et
aucun filtre n'est exécuté. Vérifier les empreintes du parent et l'absence
de fichiers d'ajustement dans l'injection. L'état P de comparaison est
l'enveloppe commune par plan du profil fourni, pas l'ancienne enveloppe par
classe Y. Les deux méthodes doivent partager exactement médianes/MAD d'entrée.

Règle pilote fixée avant évaluation : standardiser les huit descripteurs par
les médianes/MAD de P ; borner ces coordonnées à ±4 pour calculer une covariance
d'ajustement ; régulariser `C = 0,9 covariance + 0,1 I`. Le score conjoint est
`sqrt(zᵀ C⁻¹ z)`, calculé avec z **non borné** à l'évaluation. Son seuil est le
95e percentile des scores des seuls résidus D d'ajustement. Le coefficient
0,1 n'est pas choisi sur les tuiles réservées. Ce choix exploratoire unique
n'autorise pas une recherche de paramètres jusqu'à obtenir un résultat favorable.

Mesurer les compatibilités P/conjoint sur les mêmes tuiles D réservées, les
injections connues et les signatures photographiques disponibles. Les scores
sont normalisés par leur propre seuil, avec compatibilité ≤1. Les signatures
dégénérées restent non mesurables. Tracer les indices vers les JSON sources,
les deux scores et verdicts par tuile, sans recopier toutes les signatures.

L'injection est celle au **pic** 1/3/10 écarts-types D du §14, et non la
contamination par fraction/RMS du §16. Les taux obtenus ne démontrent donc
pas une amélioration du profil médian ni une diminution de sa contamination.
La prochaine vérification éventuelle doit calibrer le profil conjoint sur les
seules paires d'ajustement, puis rejouer le banc de contamination à paramètres
figés. Pas de modification de force d'E ni d'intégration en production.

## 18. Contrat d'abstention d'E et E-P

```powershell
python tests/study_noise_abstention.py --data-version N
ctest --test-dir build-patch_noise -C Release -R noiseProfileSupportTests --output-on-failure
```

Le prototype conserve une disponibilité distincte du sigma : absente, mesurée
ou transférée (ce dernier statut ne concerne que le comparateur E). P ne
transfère aucune classe. Interpoler seulement si tous les termes de poids
non nul sont disponibles. Refuser les niveaux non finis, hors [10⁻⁴,1[ et
les sigmas non finis/négatifs. Une valeur zéro mesurée est disponible mais
n'entraîne pas de correction si toutes les bandes de traitement sont nulles.
Un plancher numérique de division ne crée jamais de disponibilité.

Le soutien d'un pixel exige les niveaux Y brut et Y lissé G4 disponibles.
Co/Cg exigent aussi le profil de leur guide Y. Les normalisations de blocs
et du filtre guidé utilisent encore la moyenne du champ traité ; si cette
moyenne n'est pas couverte, le canal chromatique s'abstient entièrement.
C'est conservateur et doit être compté comme tel, même si d'autres niveaux
du champ sont disponibles. Tracer les fractions de pixels effectivement
soutenus par canal lors du rejeu, ainsi que le maximum de changement hors
soutien, qui doit être exactement nul dans les plans Y/Co/Cg.

Le raccord utilise un poids `smoothstep(clamp((distance−1)/16,0,1))`, calculé
uniquement à l'intérieur du domaine soutenu. La largeur 16 pixels est un
choix expérimental cohérent avec l'échelle G16, pas une garantie mathématique
de conservation de moyenne. À poids zéro : recopier l'entrée exactement ;
à poids un : recopier l'estimation, sans aller-retour arithmétique superflu.
Ne pas ajouter une correction globale pour faire passer un test de dérive.

Contrôles natifs : absence totale malgré sigmas positifs, zéro réellement
mesuré, soutien complet, trou de classes, disponibilités différentes par
canal, guide absent et moyenne représentative dans un trou. Deux graines,
bruit synthétique 0/0,003, 256 × 256. Ils testent le contrat, pas la justesse
de l'estimateur. Vérifier également poids de transition progressifs,
entrées invalides, distinction absent/zéro et identité à soutien complet
du mécanisme de raccord (ne pas exiger que le débruitage lui-même soit nul).

Le rejeu photographique `--estimate-study` utilise le même contrat pour les
comparateurs E et E-P. Ses résultats ne doivent donc pas être agrégés avec
v012 comme si seul le profil avait changé ; la médiane P paire est désormais
alignée sur NumPy. Les anciens JSON restent immuables. Aucune modification
du moteur de production n'est impliquée.

Pour un rejeu long, utiliser `scripts/background_job.py start --name NOM --
COMMANDE ARGUMENTS` depuis le worktree. Le dossier `build-patch_noise/jobs/NOM`
conserve spécification, état JSON et journaux ; un son Windows est demandé
à la fin (il peut être désactivé dans les paramètres système). Ce mécanisme
n'envoie pas de message dans le chat et ne réactive pas l'assistant. Un objet
de travail Windows borne la durée de vie des descendants ; en cas de délai
dépassé, le processus et ses descendants sont arrêtés. Ne pas surveiller
périodiquement ce dossier : reprendre sur notification de l'utilisateur.

## 19. Diagnostic du soutien effectif et des dérives de raccord

Le rejeu `--estimate-study` enregistre désormais `support_diagnostics` par
plan et par zone intérieure 256 × 256. Ne pas confondre les fractions de
réussite des critères indépendants (niveau brut propre, niveau G4 propre,
guide Y) avec des causes exclusives : un pixel peut échouer à plusieurs
critères. `representative_available` indique séparément si la normalisation
chromatique dispose de ses niveaux représentatifs. `processed` indique si
le filtre a été exécuté sur le champ, pas si tous ses pixels sont corrigés.

Mesurer la fraction soutenue avant transition, le poids moyen après
transition, les fractions de poids strictement positif et égal à un, et
la proportion de voisins horizontaux/verticaux de statuts différents.
Cette dernière mesure quantifie la fragmentation à un pixel, sans prétendre
compter les composantes connexes. Tous les poids et fractions sont sans unité.
Le champ est traité avec sa marge, puis les statistiques portent sur l'intérieur.

Un contrefactuel purement diagnostique calcule le soutien et le poids avec
G4 aux deux entrées de la règle. Il conserve les exigences de guide et de
moyenne représentative. **Ce masque ne produit aucun rendu** ; une meilleure
couverture ne validerait pas sa sécurité. Il faut une étude distincte avant
de remplacer le critère Y brut, notamment près des niveaux hors domaine.

Pour les canaux exécutés, noter la moyenne de correction avant restauration
G16, après restauration et avant mélange, puis la correction pondérée et
la correction finale réellement obtenue. Ces moyennes sont en unités de
plans linéaires Y/Co/Cg, pas en pourcentage ni en sigma. Pour la correction
restaurée d et le poids w, calculer
`cov(w,d) = moyenne(w*d) - moyenne(w)*moyenne(d)` sur le même intérieur.
Cela distingue la moyenne de la correction du biais introduit par sa
sélection spatiale, sans établir à soi seul une causalité de texture/bruit.
Les champs de correction ne sont pas présents si le canal s'abstient.

Campagne ciblée v014 : 0112 (couverture limitée), 9890 (bruit élevé), 0513
et 9273 (dérives chromatiques v013). Vérifier les SHA-256 contre v013 avant
lecture et les rectangles après mesure. Comparer les anciennes mesures
communes pour vérifier que l'instrumentation n'a pas modifié le rendu.
Conserver v013 et v014 séparés ; les nouvelles statistiques n'existaient
pas dans v013. Aucune force, calibration ou règle de traitement n'est ajustée.

## 20. Soutien stable et échanges pondérés : expérience factorielle

```powershell
python tests/study_noise_reconstruction.py --data-version N
python tests/study_noise_neighbourhoods.py --data-version N --estimate-study --base DOSSIER --scene NOM
```

Comparer quatre états à profil P et force 1/1/2 figés : E-P courant,
`E-P-stable` (soutien seul), `E-P-balanced` (raccord seul), et
`E-P-stable-balanced` (combinaison). Ce sont des comparateurs de recherche,
pas des options de compatibilité ou des chemins de production. Ne pas
confondre ce plan factoriel avec l'étude de sélection spectrale conjointe.

Le soutien stable utilise G4 aux deux entrées de `processingMask`. Il exige
toujours un profil propre disponible, le guide Y disponible, des bandes de
traitement non nulles et une moyenne représentative utilisable pour Co/Cg.
**Le contrat change : le niveau bruité brut ne définit plus la disponibilité
du profil**, mais les plans d'entrée doivent tous être finis. Les valeurs
finies brutes hors [10⁻⁴,1[ ne sont pas individuellement exclues si G4 est
couvert. Cela n'est pas une détection de saturation ni une protection des
petites formes ; l'impact sur les pics et limites doit être testé explicitement.
Les pixels hors du domaine G4 déclaré restent strictement identiques.

Pour le raccord pondéré, prendre la correction candidate avant restauration
G16, `d = estimation - entrée`, et le poids de soutien w. Appliquer :

`r = w * (G16(w) * d - G16(w*d))`, puis `sortie = entrée + r`.

Il ne s'agit pas d'une moyenne globale soustraite à l'image. Si K est le
noyau symétrique de G16 à bord réfléchi, chaque terme s'écrit
`r_i = somme_j K_ij w_i w_j (d_i-d_j)` : les échanges opposés s'annulent
sur le **champ entier traité**, aux arrondis près. Hors soutien ou à poids
nul, recopier l'entrée exactement. Aucune division par un soutien faible
ni plancher fabriquant une mesure. À soutien complet, on retrouve
`d-G16(d)`, principe de restauration du comparateur courant.

Ce mécanisme ne garantit ni moyenne nulle de chaque ROI intérieure, ni
absence de halo, ni préservation d'un détail. Les faibles supports réduisent
encore l'amplitude via le produit des poids. Les calculs d'échange sont en
double, la sortie reste en float. Les tests unitaires contrôlent identités,
absence de changement sur correction constante, masque irrégulier, impulsion
au bord et dérive moyenne de champ inférieure à 2 × 10⁻⁹ en unité de plan.

Contrôles natifs connus : six scènes 256 × 256 (uniforme, rampe, bord,
texture faible, point et étoile), profils complet/troué/absent, deux graines,
bruit gaussien indépendant 0/0,003 et quatre états factoriels : 288 cas.
Le profil positif 0,003 avec ratios spectraux R6 est volontairement conservé
sur les contrôles propres ; il n'est pas la calibration de ce bruit gaussien
synthétique. Noter MSE entrée/sortie face à la vérité, pic central, moyenne
de correction, soutien et maximum hors soutien. L'estimation du profil n'est
pas testée ici ; un échec de préservation interdit une qualification production.

Rejeu photographique ciblé v015 : mêmes quatre sources que v014, après
contrôle des empreintes. Enregistrer par nouvelle variante soutien, poids
moyen, identité hors soutien et moyenne de champ entier, en plus des mesures
intérieures ordinaires. Vérifier l'identité des résultats des comparateurs
communs avec v014. Les montages restent des dérivés, pas une vérité propre.

## 21. Protection significative des structures et effet du contexte

Le comparateur expérimental `E-P-structure` reprend soutien stable et échanges
équilibrés, avec un facteur supplémentaire de rétention de la correction.
Il ne change ni le profil P ni la force du débruitage. Pour chaque plan,
calculer `s = max(abs(x-G1)/sigmaFine, abs(G1-G4)/sigmaMid)` avec les sigmas
du profil évalués sur G4 **sans plancher numérique**. Le facteur vaut 1
jusqu'à s=3, puis `1-smoothstep((s-3)/3)` jusqu'à zéro à s=6. Un sigma nul
fait s'abstenir cette protection, sans créer de disponibilité de profil.
Ce sont deux seuils pilotes fixés avant mesure, pas des probabilités,
ni une recherche de coefficients sur les images réservées.

Pour Y utiliser sa rétention ; pour Co/Cg, prendre le minimum des rétentions
du plan propre et de Y. Ne pas modifier le tampon de rétention Y en calculant
les autres plans. Multiplier le poids de soutien par cette rétention et
l'utiliser des deux côtés des échanges symétriques. Ainsi un poids effectif
nul garde l'entrée exacte et la somme du champ reste conservée aux arrondis
près. Pas de dilatation spatiale ni de compensation moyenne ajoutée.
Le seuil peut laisser du bruit impulsionnel et manquer des textures faibles :
mesurer ces deux risques, ne pas annoncer une protection universelle.

Le banc synthétique courant étend le §20 : ajouter le profil P estimé sur
l'entrée (distinct du profil positif imposé), et la cinquième variante avec
protection, soit **480 cas**. Enregistrer la somme d'erreur dans une fenêtre
5 × 5 centrée, en plus des pics/MSE, pour ne pas qualifier une méthode sur
le seul maintien du pixel central. Une abstention de l'estimateur n'est pas
une détection correcte de texture ; un résultat synthétique gaussien ne
valide pas l'estimation du bruit RCD.

Diagnostic de contexte v016 : conserver les mêmes quatre RAW et zones que
v015. Pour chaque intérieur, réévaluer **la combinaison sans protection**
avec marges demandées 64 et 128, contre la marge 24 du résultat ordinaire.
Limiter le rectangle à l'image réelle, sans inventer les pixels au-delà ;
tracer le rectangle effectif et assurer une géométrie paire. Le profil P
reste celui de l'image entière. Figer aussi le niveau Y représentatif des
normalisations Co/Cg à la moyenne du champ original de marge 24, afin de ne
pas confondre changement de contexte et changement de normalisation.

Les mesures de ces champs élargis sont rangées dans `context_controls`, avec
marge demandée, rectangle réel, niveau représentatif figé, moyennes de champ
et identité hors soutien. Les statistiques portent sur le même intérieur.
Le diagnostic est exécuté sur toutes les zones des quatre scènes, pas seulement
sur celle qui dépassait le seuil ; ne pas régler une marge jusqu'à faire passer
9273. Une dépendance au contexte n'établit pas à elle seule que la dérive est
un artefact : distinguer bord de champ, transfert local de couleur et profil.
Ces temps de diagnostic ne sont pas inclus dans les timings des variantes.

## 22. Protection régionale et témoins chromatiques connus

```powershell
python tests/study_noise_reconstruction.py --regions --data-version N
```

Le pilote `E-P-region` étend la rétention ponctuelle du §21 sans modifier
ses seuils 3/6, le profil ou la force. Prendre le minimum des rétentions
sur 7 × 7 (rayon 3), puis le minimum avec son lissage gaussien sigma 1
pour prolonger la transition vers l'extérieur. Prendre enfin le minimum
avec la rétention originale, afin de ne jamais déprotéger un pixel-graine.
Ces dimensions sont fixées avant mesure ; elles ne sont pas optimisées
sur les vérités réservées. Ce premier pilote est une **extension spatiale**,
pas une détection sémantique de région cohérente.

Utiliser ces poids des deux côtés des échanges symétriques. Vérifier identité
à poids nul, somme de champ, absence d'augmentation des rétentions et coût
en bruit résiduel. La conservation de somme ne garantit toujours ni celle
de chaque petite forme, ni l'absence d'anneau au raccord de sa région.

Le banc connu comprend 324 cas, trois variantes à soutien stable et raccord
équilibré (sans protection, ponctuelle, régionale) :

- 252 cas de régions : uniforme, texture périodique, point, étoiles de
  largeur 0,7/1,5/3 pixels et impulsion de bruit ; amplitudes 1/3/10 fois
  0,003, graines 733/947, bruit gaussien 0/0,003. Le champ `width` n'est
  interprété comme largeur physique que pour les trois étoiles.
- 72 témoins chromatiques : uniforme, rampe, onde de période 128 pixels,
  marche de chroma, bosse au bord supérieur de largeur 8 pixels et marche
  de luminance ; graines 733/947 et bruit gaussien 0/0,0002. Les variations
  Co/Cg ont amplitudes 0,001/0,0005, Y de fond 0,025.

Le profil positif imposé utilise les ratios spectraux R6 déjà figés ; il
n'est pas une calibration du bruit gaussien ajouté. Aucune estimation P,
paire RCD ou vérité photographique propre n'est évaluée par ce banc.
Le point réel et l'impulsion de bruit ont des **observations identiques**,
mais des vérités différentes. La protection ne peut résoudre cette ambiguïté
en observant ces seuls pixels ; mesurer explicitement son coût sur l'impulsion.

Par plan, conserver MSE entrée/sortie, pic central, sommes d'erreur 5 × 5
et 9 × 9, erreur moyenne de l'anneau 5-vers-9 (56 pixels), erreur basse
fréquence `G16(sortie-vérité)`, poids moyen et identités. Les témoins mesurent
aussi la moyenne de correction sur deux fenêtres 64 × 64, l'une touchant
le bord supérieur, l'autre centrale. Ces résultats permettent d'exposer
des biais de région avec vérité connue ; ils ne prouvent pas que le même
mécanisme explique 9273 sans diagnostic complémentaire sur son signal réel.

## 23. Cohérence multiscalaire et audit des étapes chromatiques

Le pilote `E-P-coherent` remplace les graines ponctuelles, sans changer
la force ou le profil, puis conserve l'extension régionale du §22. Calculer
`a=G1-G2`, `b=G2-G4`. Une graine exige `a*b>0` et
`min(abs(a),abs(b)) >= 0,5*max(abs(a),abs(b))`. Son score est ensuite
`abs(G1-G4)/sigmaMid`, avec les mêmes seuils 3/6 et le même partage Y/Co/Cg.
Le facteur 0,5 est fixé avant campagne, pas ajusté sur les résultats.

Ce modèle favorise des formes persistantes entre échelles ; il peut rejeter
une vraie forme ponctuelle aussi bien qu'une impulsion. Le couple à observations
identiques reste donc dans le banc et ne doit pas être présenté comme résolu.
Un sigma nul entraîne l'abstention de protection comme aux étapes précédentes.

Le lanceur `tests/study_noise_reconstruction.py --regions --data-version N`
comprend désormais quatre variantes : **432 cas**, avec les mêmes sources
synthétiques que les 324 cas du §22. Préserver les champs communs et contrôler
leur identité. Ajouter l'énergie d'erreur moyenne de l'anneau 5-vers-9
(`ring_5_to_9_mse`) : une moyenne signée nulle n'établit pas un anneau propre.
Les erreurs sont toujours mesurées face à la vérité connue, pas à l'entrée.

L'audit photo v017 cible 9273 et 9890, empreintes contrôlées contre v016.
Dans la combinaison stable/équilibrée **sans protection**, enregistrer par plan
chroma et intérieur les états cumulés suivants, rapportés à la même entrée :
réduction de résolution réagrandie, médiane réagrandie, blocs réagrandis,
guidage, candidat avec résidu fin, sortie après échanges équilibrés.

Pour chacun, conserver moyenne de correction intérieure et du champ entier,
moyenne de G16(correction) intérieure, MSE intérieure de G16(correction) et
MSE de sa partie complémentaire. Ces MSE de correction ne sont **pas** des
erreurs face à une vérité photographique inconnue. La différence de moyennes
entre deux états aide à localiser un transfert, sans démontrer qu'une étape
isolée est fautive ou que le profil est correct.

Les enregistrements `chromatic_stages` sont absents pour un plan qui s'abstient,
pas des corrections mesurées égales à zéro. Les champs sont disponibles sur
toutes les zones traitées du sous-corpus, pas seulement la zone en échec.
La collecte ajoute du calcul au temps de la combinaison : ne pas comparer
ce timing instrumenté à un benchmark sans collecte. Aucun coefficient de
restauration, de profil ou de bord n'est réajusté pendant cet audit.

## 24. Ablation factorielle de médiane et blocs

```powershell
python tests/study_noise_reconstruction.py --ablation --data-version N
```

Comparer quatre états : médiane et blocs actifs, sans médiane, sans blocs,
sans les deux. Dans le banc photo, les trois états nouveaux sont
`E-P-no-median`, `E-P-no-blocks`, `E-P-no-median-no-blocks`. Le comparateur
complet est `E-P-stable-balanced`, sans protection de structure. Les options
de suppression sont exclusivement des facteurs d'étude, pas des options
de compatibilité à livrer dans l'IHM ou le moteur.

Retirer la médiane signifie recopier la moyenne à demi-résolution à sa place ;
retirer les blocs signifie recopier leur entrée à demi-résolution. Maintenir
réduction de résolution, guide Y, normalisations, guidage final, reconstruction
du résidu fin, profil, force, disponibilité et échanges équilibrés identiques.
Les quatre rendus Y doivent être strictement identiques : les facteurs
ne concernent que Co/Cg. Ne pas interpréter « sans les deux » comme une
absence de filtrage chromatique, puisque les autres opérations subsistent.

Le banc connu reprend les scènes de régions et témoins chromatiques, sans
protection, avec les quatre états. Ajouter aux observations gaussiennes
indépendantes un témoin corrélé pour les états avec bruit : convolution
séparable par `[0,25;0,5;0,25]`, puis multiplication par 8/3. Cette valeur
inverse la norme L2 3/8 du noyau 2D, et conserve le sigma nominal en champ
stationnaire infini ; les bords réfléchis changent la variance près du bord.
Ce témoin n'est **pas** une simulation validée du bruit RCD d'un capteur.

Cela représente 648 cas : 108 entrées de scènes/états existantes plus
54 entrées corrélées, chacune évaluée par les quatre variantes. Ne pas
dupliquer le contrôle sans bruit sous une étiquette corrélée. Les impulsions
connues restent présentes avec leur vérité différente des points réels.
Le profil positif demeure imposé et n'est pas recalibré sur les injections.

Conserver par plan les MSE, erreurs basse fréquence, pic et signal intégré,
énergie d'anneau, dérives et bruit résiduel. Vérifier identité à poids nul,
hors soutien et conservation de somme ; le probe vérifie aussi l'identité
de Y entre les quatre variantes. Retirer une étape n'est pas une amélioration
si cela ne fait que rendre davantage de bruit ou de structure.

Le travail indépendant `noise-ablation-v001-v018` exécute d'abord les
contrôles, puis le rejeu v018 sur 9273 et 9890 uniquement si les contrôles
se terminent sans erreur. Cette condition porte sur l'intégrité du banc,
pas sur une validation de qualité automatique. Revérifier les SHA-256 des
RAW contre v017 avant le rejeu. Les résultats antérieurs ne sont pas écrasés.
Au retour, vérifier les mesures communes avec v017, puis évaluer séparément
la dérive de 9273 et le coût en bruit sur les deux scènes et les vérités
synthétiques. Aucune paire RCD n'est rejouée dans ce travail.

## 25. Ablation sur paires RCD : cible bruitée et réinjection différentielle

```powershell
python tests/study_noise_pair_ablation.py --data-version N
```

Utiliser les quatre paires historiquement réservées du profil P v002
(ISO 100/1600/6400/51200), déjà consultées dans les études précédentes.
Vérifier l'empreinte du parent spectral et de chaque RAW, ainsi que l'absence
de recouvrement avec les sources d'ajustement. Une réutilisation de ces
réserves est une **étude pilote**, pas une nouvelle validation indépendante.
Le lecteur de JSON impose UTF-8, y compris pour les chemins accentués.

Développer A et B dans le chemin neutre RCD courant. Refuser une géométrie
différente ou une dérive Y globale supérieure à 2 %. Sur une grille sans
recouvrement de champs 304 × 304, sélectionner les niveaux moyens valides,
une dérive locale A/B d'au plus 2 % et un gradient G2 de M=(A+B)/2 inférieur
à 3 fois le sigma Haar de D=(A-B)/sqrt(2). Un sigma inférieur ou égal à
10⁻⁷ n'est pas retenu. Choisir **un champ de rang spatial médian par classe
logarithmique Y**, avant tout filtrage. Mesurer sur son intérieur 256 × 256.
Tracer candidats par classe et rejets de niveau/dérive/structure. Une paire
sans champ admissible reste dans le JSON comme exclusion, pas comme succès.

Deux expériences complémentaires, chacune avec les quatre états du §24 :

1. **Prédiction croisée** : filtrer A et mesurer l'écart à B ; puis filtrer
   B et mesurer l'écart à A. Le profil de chaque prédicteur est estimé sur
   sa propre image entière seulement, pas sur la cible ni sur D. La cible
   reste bruitée : `loss_to_target` n'est **pas une MSE face à une image
   propre**. Les différences de perte entre variantes sont prioritaires.
2. **Réinjection mesurée** : centrer D par plan sur le champ, puis l'ajouter
   à des signaux construits connus (aplats aux niveaux moyens de M, puis
   marche chromatique de six sigmas Haar D, sans marche Y). Utiliser le
   même profil de A pour toutes les variantes. Ici l'erreur face au signal
   construit est connue, mais l'expérience n'est pas une simulation RAW
   ni une estimation de l'erreur sur la scène photographique.

La variance de D est une référence conditionnelle de variance marginale
seulement si les bruits sont indépendants, de même variance et le signal
stable. Sous ces hypothèses, `loss_to_target - variance(D)` estime un risque
débruité ; conserver ses valeurs signées, même négatives. La sélection
des champs utilise A et B, peut biaiser ce score conditionnel et empêche
de revendiquer automatiquement une estimation sans biais. Ni M ni B n'est
déclaré comme vérité propre. Le centrage de D et ses variances sont tracés.

Le bruit différentiel ne conserve pas le motif fixe commun et ne reproduit
pas l'asymétrie marginale d'une seule prise ; mouvement, illumination ou
texture non annulée peuvent aussi contaminer D. Les comparaisons croisées
et les réinjections doivent donc être interprétées séparément. Les gradients
chromatiques injectés ne changent pas la luminance de référence ; ils restent
une perturbation après développement, non une caractérisation capteur.

Enregistrer identités source, ISO, rect, classe, direction, niveau, amplitude
injectée, moyenne D retirée, pertes entrée/sortie, différence au complet,
risque conditionnel, erreur basse fréquence, dérives et fractions de soutien.
Vérifier Y identique entre variantes, sortie finie, identité à poids nul et
somme de champ aux arrondis près. Ne pas confondre abstention et amélioration.
Les quatre paires sont traitées séquentiellement dans un processus à huit
threads OpenCV ; pas de TIFF ni de modification de production.

## 26. Ancrage local de la correction lente aux étapes robustes

```powershell
python tests/study_noise_reconstruction.py --anchors --data-version N
python tests/study_noise_pair_ablation.py --anchors --data-version N
```

Conserver médiane et blocs actifs et comparer quatre états : aucun ancrage,
ancrage après médiane seule, après blocs seuls, après les deux. Les nouveaux
comparateurs photo sont `E-P-anchor-median`, `E-P-anchor-blocks` et
`E-P-anchor-both`. Aucun facteur de retrait ni protection de structure n'est
activé simultanément ; profil, force, disponibilité, guidage et raccord final
restent ceux de la combinaison stable/équilibrée.

Pour une étape à demi-résolution, avant U et après V, utiliser
`V_ancré = V + G4(U-V)`, soit une correction `(I-G4)(V-U)`. Sigma 4 à
demi-résolution (environ 8 pixels de l'image) est un choix pilote fixé avant
mesure, en rapport avec l'échelle spatiale des blocs ; il n'est pas optimisé
sur 9273. Les calculs d'ancrage utilisent le double puis reviennent en float.
La gaussienne normalisée à bord réfléchi conserve la somme du champ aux
arrondis près, mais **pas la moyenne de chaque voisinage**, ni exactement
toute la composante basse fréquence. Cette opération limite la correction
lente ; elle peut conserver du bruit coloré lent ou déplacer des contours.

L'ancrage de médiane change l'entrée des blocs ; cela fait partie du facteur
testé, pas d'un guide Y modifié. L'ancrage de blocs compare leur sortie à
leur entrée effective. Le raccord équilibré final reste intact. Ne pas
soustraire une moyenne de crop ou un décalage global pour forcer un seuil.

Les contrôles synthétiques reprennent le plan de 648 cas indépendant/corrélé
du §24, mais avec les quatre facteurs d'ancrage et sans retrait. Les paires
reprennent strictement la sélection et les deux types de cible du §25.
Comparer les champs et comparateurs complets aux preuves d'ablation pour
vérifier leur identité. L'invariance de Y, l'abstention et la somme restent
contrôlées ; les tests unitaires vérifient aussi entrée inchangée, suppression
d'un décalage constant et conservation de somme d'un champ variable.

Le travail `noise-stage-anchoring-v001-v019` enchaîne les contrôles, les
quatre paires historiques puis le rejeu photo v019 sur 9273/9890, si chaque
phase se termine sans erreur. Cette condition valide le déroulement du banc,
pas la qualité du modèle. Conserver chaque JSON déjà produit si une phase
ultérieure échoue. Aucun coefficient n'est retouché entre les phases et
aucun résultat intermédiaire ne déclenche une livraison automatique.

## 27. Déplacement de couleur et répartition spatiale — diagnostic, pas seuil de visibilité

Avant un nouveau réglage du filtre, caractériser le comparateur complet
`E-P-stable-balanced`. Les trois ancrages rejetés servent seulement de
comparateurs photo. Aucun calcul de production, coefficient ni seuil n'est changé.

Reconstituer le RGB Rec.2020 linéaire depuis Y/Co/Cg **sans compression de
chroma ni écrêtage**, puis convertir en sRGB linéaire par la matrice du moteur
et en [Oklab D65 selon son auteur](https://bottosson.github.io/posts/oklab/).
Utiliser les fonctions existantes de `WorkingColorSpace.h`, sans en modifier
les coefficients. Multiplier les différences de coordonnées par 100 pour
la lecture ; ce n'est **ni ΔE00, ni un seuil JND**, ni une preuve de visibilité.
Auditer séparément les fractions de pixels hors [0,1] en sRGB linéaire pour
chaque image : la racine cubique signée rend le calcul défini hors gamut,
mais n'y garantit pas la validité perceptuelle. Aucun profil écran, adaptation
d'exposition ni transformation d'affichage n'est appliqué.

Pour la différence Oklab signée sortie moins référence, conserver :

- le vecteur moyen L/a/b et sa norme (déplacement moyen) ;
- les distances RMS, médiane, P95, P99 et maximum par pixel ;
- les RMS de clarté et de chroma, puis la RMS chromatique après retrait de
  la moyenne (variation spatiale distincte du biais uniforme) ;
- les mêmes statistiques après lissage **du champ de différences signé**
  par G4 et G16, en pixels natifs, bord REFLECT, sur le champ avec marge
  avant sélection du carré central. Ces échelles sont descriptives et ne
  constituent pas un modèle de vision humaine.

Sur les photos, la marge habituelle de 24 pixels n'isole pas le lissage
G16 de la frontière réfléchie ; cette composante est un diagnostic de champ
local, pas une statistique invariante au contexte ni une mesure plein cadre.
Interpréter les amas/halos avec les couronnes et les planches existantes.

Sur photo, nommer ces mesures `perceptual_change_not_error` : l'entrée
bruitée n'est pas une vérité propre. Une petite différence peut signifier
une faible correction, pas une bonne fidélité ; une grande peut être la
suppression de bruit. Sur les contrôles synthétiques connus, mesurer les
erreurs entrée/vérité et sortie/vérité du comparateur complet, en complément
des pics, flux, couronnes et MSE par plan déjà enregistrés. Ne pas déduire
une perte de texture ou un halo d'une seule distance agrégée.

Campagne bornée : contrôles `--regions` v003 et photo `--estimate-study`
v020 sur 9273 et 9890. Les paires RCD déjà mesurées ne sont pas recalculées
à cette étape. Préserver les JSON horodatés et l'empreinte du binaire commun.
Les tests de la métrique vérifient identité, symétrie, décalage constant,
changement localisé, atténuation spatiale, domaine négatif et rejet des NaN.

## 28. Petites formes sur résidus RCD et domaine de couleur commun

Objectif : qualifier le reconstructeur complet sans changer sa force ni
réestimer le profil après injection. Réutiliser les quatre paires réservées
R6 Mark III à ISO 100/1600/6400/51200, contrôlées par empreintes et séparation
avec l'apprentissage dans `study_noise_pair_ablation.selected_pairs`.
Ce sont des paires déjà consultées, pas une nouvelle validation indépendante.

La sélection reste celle du §25 : grille 304 pixels, dérives globale et
locale au plus 2 %, G2-gradient inférieur à 3 sigma HH de D(Y), une fenêtre
au rang spatial médian de chaque classe de Y éligible. Ne pas choisir la
fenêtre en fonction du résultat du débruitage.

Construire D=(A−B)/sqrt(2), retirer sa moyenne sur le champ ; le niveau de
fond de chaque plan est la moyenne de (A+B)/2 dans ce champ. Le profil P
estimé sur A complète avant injection reste figé ; le normaliseur scalaire
de Y représentatif est fixé au fond, mais les guides et masques locaux
peuvent répondre à la forme injectée. Force 1/1/2, support stable et raccord
équilibré, sans nouvelle protection ni ancrage.

Dans chaque plan Y, Co ou Cg séparément, injecter un point, puis trois
gaussiennes de sigma spatial 0,7 / 1,5 / 3 pixels, tronquées au carré de
rayon ceil(3 sigma). Pic positif à 3 / 6 / 10 fois le sigma HH de D du
plan, mesuré sur le carré intérieur 256 pixels. Ce facteur est une échelle
d'amplitude, **pas une probabilité ni une significativité sous bruit corrélé**.
Chaque classe donne 36 états, plus un filtrage du témoin uniforme avec le
même résidu. Une classe sans sigma suffisant est explicitement exclue.

Avec F le filtre, la réponse à la forme est
`R = F(fond + forme + D) − F(fond + D)`. Elle soustrait la réponse au même
bruit, mais ne linéarise pas F et n'est pas une mesure propre absolue.
Conserver, pour les trois plans :

- réponse au centre / amplitude injectée ;
- somme de R dans le support / flux injecté ;
- moyenne signée et RMS de R dans la couronne extérieure de 4 pixels,
  normalisées par l'amplitude ;
- MSE sortie/vérité construite, MSE d'entrée, MSE extérieure hors forme
  et couronne, MSE du témoin uniforme sur ce même extérieur ;
- disponibilité, poids moyen et invariance des pixels à poids nul.

Les rapports de réponse des plans non injectés mesurent une diaphonie,
pas la rétention d'un signal qui y aurait été injecté. Pour le point,
la même observation peut être une impulsion parasite si sa vérité est
le fond : garder ce témoin ambigu, sans refaire artificiellement un
deuxième filtrage. Une forte rétention n'est pas un succès pour ce témoin.

Pour Oklab, construire **un masque commun** d'appartenance au cube sRGB
linéaire [0,1] sur vérité, entrée, sortie et sortie du témoin. Appliquer
ce masque figé aux erreurs entrée/vérité et sortie/vérité, globalement
et sur forme+couronne ; enregistrer le nombre de pixels retenus. Un domaine
vide donne un statut explicite, jamais une erreur numérique nulle.
Ces métriques masquées restent natives : aucun lissage à travers des pixels
exclus. Elles n'établissent toujours pas un seuil de visibilité.

Le rejeu photo v021 utilise l'intersection entrée + complet + trois ancrages,
identique pour les quatre comparateurs de chaque zone, afin de compléter
v020 sans masquer les exclusions. Les métriques intégrales demeurent
disponibles. Les couvertures peuvent varier entre scènes ou états injectés ;
ne pas agréger ces distances comme si leurs domaines étaient identiques.

Commande de campagne :

```powershell
python tests/study_noise_perceptual.py --pair-forms --controls-version 1 --photo-version 21 --parent docs/validation/raw/etude-voisinages-bruit-v020-20261025T152345Z.json
```

Le lanceur détaché conserve journaux, timeout et état persistant. Échec d'une
phase : arrêter la suivante et conserver les preuves déjà écrites. D perd
le bruit fixe et une partie des statistiques marginales ; il ne simule pas
le RAW et peut conserver des dérives ou textures différentielles. Réestimer
le profil après injection sera une campagne distincte, pas un facteur caché.

## 29. Localisation de la perte chromatique par étape

Suite du §28, sans nouveau filtre : observer les sorties intermédiaires
de `candidateE` par un callback en lecture seule. Pour chaque forme,
soustraire l'image de l'étape obtenue sur le témoin sans forme mais avec
le même résidu. Ne pas comparer une étape à l'entrée d'une autre pour
attribuer directement une erreur, car le traitement reste non linéaire.

Conserver les quatre tailles et les trois plans injectés du §28, mais
seulement les amplitudes 6 et 10 sigma HH : 24 états par classe, soit
360 états attendus sur les 15 classes historiques si la sélection reste
identique. Les abstentions demeurent tracées et exclues des agrégats actifs.

Étapes observées pour chaque plan chromatique disponible :

- `area-up` : réduction AREA puis remontée LINEAR ;
- `median-up` : médiane demi-résolution, remontée LINEAR ;
- `blocks-up` : sortie des blocs remontée LINEAR ;
- `guided` : sortie guidée, sans détail fin ;
- `fine-residual` : composante native moins `area-up`, contribution séparée ;
- `fine-retained` : cette composante après Wiener, contribution séparée ;
- `candidate-with-fine` : somme guidage + détail retenu ;
- `balanced` : raccord final, identique à la sortie complète.

Les étapes à demi-résolution sont toutes remontées par la même interpolation
avant comparaison, afin de garder centre, support et couronne en pixels
natifs. Cela mesure la chaîne avec son rééchantillonnage, pas un transfert
pur à demi-résolution. Les deux contributions fines ne sont pas des étapes
successives du chemin grossier ; ne pas les soustraire comme si elles
étaient la totalité du signal.

Par étape, tracer pic/amplitude, flux/flux injecté et moyenne/RMS de la
couronne normalisées par l'amplitude, avec les indices du plan injecté et
du plan observé. Garder les métriques finales du §28 pour vérifier leur
identité sur les états communs. Aucun callback de plan indisponible : ne
pas transformer une absence de mesure en réponse nulle.

Contrôle natif `--small-form-stage-controls` : 24 états, 16 descripteurs
d'étape par état sur le témoin positif disponible (huit étapes, deux plans),
finitude, invariance à poids nul et égalité de réponse `balanced`/sortie.
Commande de campagne :

```powershell
python tests/study_noise_pair_ablation.py --small-form-stages --data-version 1
```

Ne pas rejouer la photo : aucun nouvel indicateur photo n'est ajouté à
cette étape. Selon le résultat, choisir le prochain essai borné, puis le
lancer dans le cadre de l'autorisation temporaire du 25 octobre, sans
modification implicite de production ni attente active.

## 30. Résidu fin et phase de la grille — audit sans nouveau filtre

Le résidu fin réellement filtré est `n − U(AREA(n))`, où U est la remontée
LINEAR. Le profil P fournit sa bande fine estimée selon `n − G1(n)`.
Ces deux opérateurs ne sont pas identiques : vérifier la correspondance
avant d'interpréter la suppression du détail comme un simple mauvais poids.

Même sélection des quatre paires, même profil figé et même résidu D que
le §29. Injecter uniquement Co ou Cg, aux quatre tailles du §28, amplitude
6 sigma HH, sur les quatre parités du centre (pair/pair, impair/pair,
pair/impair, impair/impair). Cela donne 32 états par classe, soit 480
attendus et 448 actifs si l'abstention historique reste identique.

Pour chaque fenêtre et plan, conserver : sigma MAD et RMS du résidu fin
de D, sigma MAD de `D−G1(D)`, et RMS du sigma fin prévu par P sur le
même intérieur. Les rapports entre les deux bandes de D décrivent les
opérateurs sur ce résidu ; comparer P à une seule bande mêle calibration,
sélection locale et différences d'opérateur. Aucun de ces rapports ne
devient automatiquement un coefficient de production.

À chaque centre injecté, tracer l'énergie locale 5×5 du résidu fin,
le sigma utilisé, le seuil de variance `2 sigma²` et le gain
`max(0,1−2 sigma²/max(énergie,10⁻¹²))`, ainsi que le gain sur le témoin
sans forme au même centre. Ce gain descriptif reproduit la règle du
filtre, pas une probabilité de signal. Garder les huit réponses d'étape
et les erreurs finales pour les états correspondants.

Le résidu de bruit reste fixe tandis que la forme est déplacée d'un pixel :
les sorties non linéaires dépendent donc aussi d'un voisinage de bruit
différent. Ne pas attribuer toute différence entre parités au seul
rééchantillonnage. La décomposition linéaire `area-up + fine-residual`
permet d'étudier plus directement la part géométrique. Les résultats ne
permettent pas de choisir la meilleure parité pour une photo utilisateur.

Contrôle natif : `--fine-phase-controls`, 32 états avec données finies,
audit de bruit/énergie présent, parités du centre correctes et égalité
de la réponse finale avec la sortie. Commande de campagne :

```powershell
python tests/study_noise_pair_ablation.py --fine-phase-audit --data-version 1
```

Pas de rejeu photo ni de correction candidate à cette étape. Conserver
empreintes et provenance ; comparer la phase pair/pair à la campagne §29
avant d'interpréter les nouveaux effets.

## 31. Pilote de gain du résidu fin — compromis détail/bruit

Suite de l'audit §30 : ne modifier ni P, ni le chemin grossier, ni Y,
ni le raccord. Comparer quatre règles appliquées au seul résidu fin,
avec E son énergie locale moyenne et N le sigma fin de P au carré :

| Variante | Fenêtre d'énergie | Gain du résidu |
| --- | --- | --- |
| Référence | 5×5 | max(0, 1−2N/max(E,10⁻¹²)) |
| Alpha 1 | 5×5 | max(0, 1−N/max(E,10⁻¹²)) |
| Alpha 1 local | 3×3 | max(0, 1−N/max(E,10⁻¹²)) |
| Gain doux pilote | 5×5 | E/(E+2N+10⁻¹²) |

Ces variantes sont des contrôles expérimentaux, pas une estimation de
probabilité de signal. La dernière conserve un gain positif sur le bruit
dès que E>0 ; son coût en bruit doit être mesuré, pas masqué par une bonne
rétention de pic. Les premières variantes isolent poids de variance et
dilution de l'énergie dans la fenêtre. Ne pas changer les coefficients
en cours de campagne pour faire passer une scène.

Reprendre les quatre paires, Co/Cg, quatre tailles, amplitude 6 sigma HH,
en ne gardant que les phases pair/pair et impair/impair. Cela représente
16 états par classe et par variante : 960 attendus, dont 64 abstentions
si la disponibilité demeure identique. Chaque variante filtre aussi son
témoin uniforme avec le même résidu, pour mesurer sa MSE de bruit finale.

Conserver réponse différentielle, flux/couronne, erreurs finales et
extérieures, MSE du témoin uniforme et traces d'étape. Comparer les
variantes sur les mêmes fenêtres actives, jamais en mélangeant les
abstentions avec de bons résultats. Le masque Oklab est commun aux images
d'un même essai, **pas entre toutes les variantes** : les critères linéaires
et leurs domaines identiques font foi pour comparer le compromis.

Le point réel reste un témoin impulsionnel à observation identique :
une récupération de détail conserve aussi l'impulsion. Aucune variante
ne peut être retenue sur le seul gain de pic ou une MSE globale dominée
par l'aplat. Comparer la référence aux états identiques du §30 avant
analyse et séparer le retour visuel futur de cette validation construite.

Contrôle natif : `--fine-gain-controls`, 64 états disponibles, réponses
finies et identité à poids nul, étape finale égale à la sortie. Commande :

```powershell
python tests/study_noise_pair_ablation.py --fine-gain-study --data-version 1
```

Le champ `local_energy` et `energy_window_width` explicite la taille de
fenêtre dans cette nouvelle preuve ; ne pas interpréter les valeurs 3×3
comme l'ancien champ `local_energy_5x5` des audits historiques.

## 32. Réinjection fine conditionnée par signal spatial — diagnostic oracle

Le relèvement global du gain du §31 augmente le bruit. Tester un
déclenchement spatial sélectif **avec bruit de calibration connu**, avant
d'envisager un détecteur aveugle. Ce pilote n'est pas une implémentation
de production et ne prouve pas qu'une calibration équivalente sera possible
sur une image isolée.

Pour le plan chromatique x, définir trois bandes
`b_s(x)=G_s(x)−G8(x)`, s=0,7 / 1,5 / 3 pixels. Avant toute injection,
estimer `sigma_s` par la MAD normalisée de `b_s(D)` sur le carré intérieur
en excluant la zone centrale 65×65. Le même résidu D sert aux essais :
il ne s'agit donc **pas d'une calibration indépendante**. Elle exploite
un bruit construit indisponible en production, même sans exploiter la forme
injectée. Les corrélations spatiales ne disparaissent pas par cette exclusion.

Sur l'image observée, calculer `z=max_s |b_s(x)|/sigma_s`, puis deux
pilotes `t=clamp((z−a)/2,0,1)`, `q=t²(3−2t)`, pour a=4 ou 6. Les sigma
nuls/insuffisants sont exclus ; sans bande disponible, q=0 (référence).
Ce maximum multiscalaire est un score heuristique : les nombres 4/6 ne
donnent pas un taux de fausses alarmes gaussien après maximisation.

Avec r le résidu fin et W(r) sa sortie de référence, remplacer seulement
la branche fine par `W(r)+q*(r−W(r))`. Y, chemin grossier, profil P et
raccord demeurent inchangés. Il s'agit d'une réinjection progressive,
pas d'un nouveau modèle probabiliste du pixel bruité. Les seuils sont
fixés avant mesure ; aucun ajustement sur une seule scène en cours de calcul.

Comparer référence et deux pilotes sur le même corpus borné du §31 :
Co/Cg, quatre tailles, amplitude 6 sigma HH, phases 0/3, soit 720 états
attendus et 672 actifs si les abstentions sont inchangées. Garder toutes
les métriques du §31, et ajouter les sigma de calibration, la fraction de
pixels du témoin uniforme où q>0 et sa moyenne. Ces fractions décrivent
le déclenchement sur le bruit connu, pas une probabilité universelle.

Conserver le coût de bruit du témoin uniforme, les flux/couronnes et le
témoin impulsionnel. Les bandes lissées peuvent aussi reconnaître un
bruit coloré lent comme signal : leur bénéfice éventuel doit être confirmé
sur textures et sur une calibration indépendante/aveugle avant toute adoption.
La branche fine seule ne peut pas rétablir tout le flux grossier perdu.

Contrôle natif `--spatial-fine-controls` : 48 états, invariants et cohérence
de sortie, abstention sans sigma et absence de déclenchement sur constante.
Commande de campagne :

```powershell
python tests/study_noise_pair_ablation.py --spatial-fine-study --data-version 1
```

Les métadonnées `spatial_rules` et `spatial_fine_study` définissent ce
périmètre ; la sélection commune des pilotes de gain est réutilisée, mais
ce ne sont pas les trois règles de gain global du §31.

## 33. Détection par filtre adapté avec/sans covariance — pilote oracle

Le pilote §32 ne retrouve presque aucun des petits signaux. Tester une
banque de filtres adaptés pour tenir compte de la corrélation du bruit,
plutôt que baisser les seuils jusqu'à déclencher sur les mêmes témoins.
Le principe `h∝C⁻¹g`, avec g un motif connu et C la covariance du bruit,
est décrit par [Vio, Vergès et Andreani, A&A 604 A115 (2017)](https://doi.org/10.1051/0004-6361/201629330).
Leurs limites sur l'interprétation des détections après recherche de pics
s'appliquent aussi à la prudence nécessaire ici : maximiser plusieurs
scores ne donne pas une probabilité de fausse alarme gaussienne simple.

Choix propres au pilote IPR : patches 9×9 ; motifs point et gaussiennes
0,7/1,5/3 pixels, chacun privé de sa moyenne. Calibrer sur D connu, avec
patches de pas 4 dans le carré intérieur, hors carré central 65×65 plus
garde de 4 pixels pour ne pas le recouvrir. Retenir au plus 2048 patches
uniformément espacés dans la liste spatiale. Retirer la moyenne de chaque
patch, puis centrer les colonnes pour former la covariance empirique C.
Ces patches se chevauchent et restent dépendants ; D est le même résidu
que celui de l'essai, pas une calibration indépendante ou aveugle.

Régulariser par `Cλ=C+0,01 trace(C)/81 I`, résoudre `Cλ h=g`, retirer
le résidu numérique de moyenne de h, puis normaliser `hᵀg=1`. Ne pas
inverser explicitement la matrice ; vérifier résolution et valeurs finies.
Le témoin sans blanchiment utilise exactement la même banque, mais h∝g.
Pour chaque noyau, la MAD normalisée des projections des patches de
calibration fixe l'échelle du score. Conserver coefficients des noyaux,
normalisation, sigma et métadonnées de covariance dans la preuve.

Évaluer les corrélations locales centrées par `filter2D`, bord REFLECT,
puis leur maximum absolu normalisé. Comparer : référence, banque non
blanchie avec transition 4→6, banque blanchie 4→6 et banque blanchie 6→8.
La réinjection reste celle du §32, dans la seule branche fine. Conserver
les mêmes 960 états prévus que le §31, mêmes témoins de bruit et impulsions.
Le seuil et la régularisation ne sont pas adaptés en cours de campagne.

Ce pilote bénéficie d'une banque contenant la famille exacte des formes
injectées et d'une calibration oracle ; ne pas généraliser son résultat
à toutes les textures. Les fonds inconnus, la non-stationnarité et les
bruits non gaussiens ne satisfont pas nécessairement le modèle théorique.
Un éventuel gain devra ensuite être testé avec une calibration séparée,
puis une estimation aveugle, avant toute production.

Tests du module : entrée invalide, normalisation DC, constante, point fort
et abstention sans détecteur. Contrôle natif intégré :
`--matched-fine-controls`, 64 états avec les invariants du banc.

```powershell
python tests/study_noise_pair_ablation.py --matched-fine-study --data-version 1
```

Les métadonnées `matched_fine_study` et `matched_rules` précisent ce
périmètre ; ne pas interpréter les champs génériques hérités des pilotes
de gain global comme la description de cette banque de filtres.

## 34. Détectabilité selon taille/amplitude — détecteur seul, positions centrales réservées

Ne pas multiplier les réglages du filtre sur les seules injections à
6 sigma HH : cette unité ne prédit pas la marge du détecteur sous bruit
corrélé. Tracer les scores de détection, sans aucun nouveau débruitage,
pour les quatre tailles et les amplitudes 2/4/6/10/20/40 sigma HH.

Reprendre les mêmes quatre paires et fenêtres, et les banques adaptées
avec/sans covariance du §33. Les patches utilisés pour covariance et
échelle de score restent hors centre 65×65 avec garde de 4 pixels.
Tester 25 centres sur une grille centrale 5×5, pas 12 pixels, offsets
−24/−12/0/12/24. Le support de chaque corrélation 9×9 est strictement
contenu dans la région exclue des patches de calibration. Les ensembles
de pixels ne se recouvrent donc pas ; cela **ne rend pas indépendants**
les bruits d'un même champ corrélé. Les références demeurent oracle.

Utiliser la linéarité du filtre adapté, avant maximum/valeur absolue :
`score_j(bruit + A*g) = score_j(bruit) + A*(h_jᵀg)/sigma_j`.
Les motifs sont évalués sur le support 9×9 de chaque noyau. Conserver
les quatre scores signés du témoin à chaque centre et les réponses
théoriques au motif. Puis conserver le maximum absolu par centre pour
chaque amplitude/taille. Aucun filtrage complet de l'image injectée n'est
nécessaire pour cette mesure ponctuelle ; ne pas en déduire une couronne
ou une qualité de reconstruction qui n'a pas été calculée.

Mesurer les fractions dépassant les scores fixes 4 et 6, ainsi que les
quantiles 99 % et 99,9 % du maximum sur le champ de calibration hors
centre. Ces quantiles sont des comparateurs diagnostiques, pas des
seuils optimisés et retenus pour la production. Conserver la population
de calibration et les scores nuls aux 25 positions de test. Une fraction
de détection sur ces positions dépendantes n'est pas une puissance de
test validée ; 25 positions par fenêtre ne permettent pas d'estimer une
queue à 0,1 % avec précision. Ne pas confondre quantile de calibration
et taux de fausse alarme sur les positions réservées.

Attendus : 60 enregistrements fenêtre/plan/banque, 24 courbes par
enregistrement et 25 évaluations par courbe. La classe ISO 100 sans
disponibilité de profil doit rester identifiée, sans être comptée comme
une validation du reconstructeur. Le détecteur est calculé même dans
cette classe, mais l'agrégation liée au filtre peut l'exclure.

Tests : linéarité des réponses signées sur une injection connue, compte
des banques/courbes/positions dans `--detectability-controls`, invariants
du module adapté. Commande de campagne :

```powershell
python tests/study_noise_pair_ablation.py --detectability-study --data-version 1
```

Cette étude borne le domaine du mécanisme testé. Elle ne prouve ni une
impossibilité générale du débruitage, ni la capacité d'un modèle IA ou
d'un autre algorithme à reconstruire une vérité inconnue.

## 35. Réinjection fine ou complète — comparaison de mécanisme à signal plus fort

La détection d'une forme ne garantit pas sa reconstruction. Le pilote
fin ne peut rétablir le flux supprimé dans le chemin grossier. Tester
l'emplacement de la réinjection en conservant les mêmes détecteurs
blanchis du §33, sans nouvelle calibration de seuil.

Comparer quatre variantes : référence, réinjection fine 4→6, réinjection
complète 4→6, réinjection complète 6→8. Pour la variante complète, laisser
la branche fine à sa référence, former le candidat chromatique d, puis
`d' = d + q*(x−d)` avant les mêmes échanges équilibrés. Ne pas interpoler
directement la sortie après raccord : ses invariants de somme et de
support doivent rester ceux du banc. Y, profil P et normaliseur demeurent
figés comme aux campagnes précédentes.

Co/Cg, quatre tailles, amplitudes 6 et 20 HH, phase pair/pair uniquement :
16 états par classe et variante, soit 960 attendus et 896 actifs si les
abstentions historiques sont inchangées. L'amplitude 6 garde le cas faible ;
20 examine un régime plus détectable sans supprimer le premier du bilan.
La banque et la calibration oracle restent favorables et non déployables.

Mesurer pics, flux, couronnes, erreurs locales/globales, MSE du témoin
uniforme et coût extérieur. Garder le témoin impulsionnel à observation
identique et les couvertures colorimétriques : augmenter le signal peut
aussi faire sortir une partie du domaine sRGB des mesures masquées.
Les mesures linéaires ne sont ni écrêtées ni sélectionnées par ce masque.

Dans les traces d'étape, `candidate-with-fine` reste le candidat avant
réinjection complète ; ajouter `full-candidate-gated`, puis `balanced`.
Le champ de gain fin doit décrire le gain réellement appliqué à la
branche fine, pas le poids du déclenchement appliqué au candidat complet.

Il s'agit d'isoler l'emplacement de la réinjection, pas de sélectionner
le meilleur détecteur pour toutes les tailles. Les courbes du §34 montrent
que le blanchiment n'est pas supérieur à la banque brute dans tous les
régimes, notamment à seuil empirique comparable pour les formes larges.
Cette question de détecteur reste distincte de celle du flux restauré.

Contrôle natif `--matched-full-controls` : 64 états, 16 traces chromatiques
dans les variantes fines et 18 dans les complètes, sortie finale cohérente,
valeurs finies et identité aux poids nuls. Commande de campagne :

```powershell
python tests/study_noise_pair_ablation.py --matched-full-study --data-version 1
```

Comparer les états à 6 HH aux preuves précédentes avant interprétation ;
aucune adoption en production sur la seule récupération de flux construite.

## 36. Couverture du masque de réinjection complète — rayons fixes

La réinjection complète améliore certains pics mais laisse le flux partiel.
Isoler l'effet de couverture du masque avant de modifier encore le détecteur.
Référence, candidat complet 4→6 inchangé, puis deux dilatations maximales
du même poids q, sur un carré de rayon 2 ou 4 pixels natifs, bord REFLECT.
Ne pas changer seuil, covariance, profil, Y, branche fine ou raccord.

Une dilatation étend les déclenchements sur signal **et** sur bruit ; elle
ne détecte pas un nouveau signal et ne fait pas disparaître l'ambiguïté
des impulsions. Elle n'est pas ajustée à la taille connue de la forme.
Les rayons sont fixés avant mesure. Le poids étendu est appliqué à
`d'=d+q*(x−d)` avant les mêmes échanges équilibrés.

Corpus identique au §35 : Co/Cg, quatre tailles, 6/20 HH, phase pair/pair,
960 états attendus, dont 64 abstentions historiques. Vérifier l'identité
des états de référence et du masque non étendu avec la campagne précédente.
Mesurer pic/flux/couronne, MSE du bruit uniforme et extérieure, ainsi que
la fraction et le poids moyen de déclenchement du masque **étendu**.

La somme des poids réintroduits n'est pas une mesure de qualité : une
couverture accrue peut améliorer le flux et dégrader fortement le bruit.
Ne pas sélectionner le rayon sur le seul exemple le plus favorable.
La calibration reste oracle et l'étude ne rend pas le mécanisme déployable.

Contrôle natif `--matched-support-controls` : 64 états, invariants du
banc, et inclusion pixel par pixel q ≤ dilatation2(q) ≤ dilatation4(q).

```powershell
python tests/study_noise_pair_ablation.py --matched-support-study --data-version 1
```

Interpréter le périmètre via `matched_support_study` et
`matched_support_rules`, et non les descriptions génériques des familles
de sondes réutilisées.

## 37. Reconstruction locale paramétrique — ne plus recopier les pixels bruités

Le pilote §36 améliore le flux mais élargit trop les zones de bruit
conservé. Tester un mécanisme différent : reconstruire un motif estimé
dans une petite zone détectée, au lieu de réinjecter directement x−d.
Cette projection est un prototype propre au banc IPR ; elle ne constitue
pas un débruiteur universel ou une estimation optimale prouvée.

Utiliser la même banque blanchie 9×9, les mêmes sigma oracle et transitions
4→6 / 6→8. Pour chaque pixel, choisir le motif donnant le plus grand
score absolu ; ne retenir que les maxima locaux 3×3 dont le score dépasse
le bas de la transition. Les égalités sont conservées sans départage
aléatoire ; elles peuvent donner plusieurs centres. Le poids q conserve
la même transition smoothstep.

Au centre c, l'amplitude signée estimée est `A_c=hᵀx`, avec h normalisé
selon le §33. Le modèle du patch est `m_c(p)=mu_c+A_c*g_0(p−c)`, où
mu est la moyenne locale 9×9 de l'entrée et g_0 le motif privé de sa
moyenne sur 9×9. La moyenne du fond n'est pas estimée par un GLS complet.
Le modèle reproduit donc une forme de la banque, mais peut introduire
un biais sur les textures ou fonds non décrits par celle-ci.

Agréger les modèles chevauchants par les poids `q_c*T(p−c)`, avec une
tente séparable `T(i,j)=(5−|i|)(5−|j|)` sur −4..4. À poids total non nul,
la prédiction est leur moyenne pondérée. Le poids de mélange à chaque
pixel est le maximum des q_c sur le carré 9×9 environnant. Mélanger cette
prédiction au candidat de référence, **avant les mêmes échanges équilibrés**.
Sans centre retenu, conserver le candidat de référence. Aucun pixel
bruité original n'est recopié directement sous le masque.

Comparer référence, réinjection brute complète 4→6, projection 4→6 et
projection 6→8. Corpus inchangé : Co/Cg, tailles du §35, 6/20 HH, phase 0,
960 états attendus. Les étapes ajoutent `candidate-projected`, puis
`balanced`, sans modifier la branche fine de référence. Tracer nombre
de centres retenus et couverture des modèles sur le champ avec marge,
séparément pour témoin uniforme et image injectée. Le champ historique
de déclenchement brut du détecteur ne représente pas cette couverture.

Outre pics/flux/couronnes et MSE du bruit, contrôler les faux motifs :
un déclenchement sur bruit peut créer une structure paramétrique au lieu
de seulement laisser du bruit existant. La banque contient les familles
injectées et la calibration demeure oracle. Tout résultat favorable devra
ensuite être testé sur bords, rampes, textures non modélisées et calibration
indépendante/aveugle. Le support 9×9 ne couvre pas toute la forme gaussienne
sigma 3 tronquée à ±9 ; cette limite doit rester explicite.

Tests du module : constante sans modification, point fort restitué,
finitude, en plus des invariants du détecteur. Contrôle natif
`--matched-projection-controls` : 64 états avec traces et cohérence de sortie.

```powershell
python tests/study_noise_pair_ablation.py --matched-projection-study --data-version 1
```

Le périmètre est défini par `matched_projection_study` et
`matched_projection_rules` ; cette projection n'est pas la réinjection
brute décrite dans les métadonnées génériques des autres pilotes.

## 38. Premier pilote multiscale à contributions figées — grandes/petites zones

À la demande de l'utilisateur, changer de famille après les limites des
pilotes à motifs. Ce premier essai isole ordre des échelles et guidage
souple ; il ne prétend pas encore réunir plusieurs modèles spécialisés
ni implémenter BM3D. Aucune banque de points/gaussiennes n'est utilisée
pour reconnaître le signal.

Décomposer le plan original, sans réduction de résolution :
`b0=x−G1`, `b1=G1−G2`, `b2=G2−G4`, `b3=G4−G8`, base `G8`.
Les quatre gaussiennes sont toujours calculées depuis x original.
L'entrée et les bandes restent immuables ; une étape ne relit pas une
image déjà filtrée. Le fond G8 n'est pas débruité dans ce premier pilote,
ce qui peut limiter la suppression du bruit chromatique lent.

Avant injection, calibrer les sigma des quatre bandes sur le résidu D
connu par MAD, hors centre 65×65. Cette calibration demeure oracle et
non indépendante ; les supports gaussiens peuvent traverser la région
centrale exclue. Les nouvelles variantes partagent cette calibration,
mais pas la référence E-P : un éventuel gain face à E-P ne permettrait
donc pas d'attribuer tout le bénéfice à l'architecture seule.

Pour chaque bande k, énergie locale E sur 5/7/9/13 pixels de côté
respectivement, variance N du bruit, estimation `S=max(E−N,0)` et poids
propre `c=S/(S+N+10⁻²⁰)`. Ces poids ne sont pas des probabilités.
Conserver une mémoire M, maximum des poids propres des bandes déjà
visitées, initialement nulle. Le gain est `g=c+beta*M*(1−c)` ; puis
mettre à jour `M=max(M,c)`, **pas avec g**, pour éviter de recycler
le renforcement comme une nouvelle preuve.

La contribution `g*bk` est ensuite figée pour ce passage. Comparer :

- bandes indépendantes, beta=0 ;
- grandes vers petites, ordre b3/b2/b1/b0, beta=0,5 ;
- petites vers grandes, ordre b0/b1/b2/b3, beta=0,5.

Conserver G8 plus la somme des contributions et appliquer le même raccord
équilibré et la même disponibilité P que le comparateur. Les bandes
ne sont donc pas détruites successivement ; les décisions restent
traçables et les données originales permettent un autre passage. Cela
n'est ni un verrouillage définitif des pixels ni un mécanisme de retour
arrière automatique. À variance connue nulle, la bande est conservée ;
une bande vide ne doit pas renforcer les suivantes comme une preuve de signal.

Corpus inchangé Co/Cg, quatre formes, 6/20 HH, phase 0, référence plus
trois pilotes : 960 états attendus. Y reste inchangé. Les sigma par bande,
ordre, poids propres/gains moyens et fraction renforcée sont conservés
dans les audits. Tracer les mêmes pics/flux/couronnes, bruit uniforme,
impulsions et couvertures colorimétriques que précédemment.

Tests du module : entrée immuable, identité sans bruit aux arrondis près,
constante, finitude, ordre sans effet avec beta=0 aux arrondis près,
rejet d'entrée invalide. Contrôle intégré `--multiscale-controls` : 64 états,
étapes finales cohérentes et identité à poids nul. Commande :

```powershell
python tests/study_noise_pair_ablation.py --multiscale-study --data-version 1
```

Interpréter via `multiscale_study` et `multiscale_rules`. Les anciens champs
de résidu fin ne décrivent pas le calcul utilisé par ces trois variantes ;
leurs audits de bandes dans `model_reconstruction_audit` font foi.
Un guidage peut aussi préserver du bruit corrélé : ne pas retenir le modèle
sur la seule augmentation du pic ou sur une comparaison oracle favorable.

## 39. Plancher empirique d'énergie du bruit multiscale — 25 octobre 2026

Le premier pilote conserve trop de bruit uniforme. Tester seulement
l'hypothèse des faux indices locaux, sans changer les supports ni G8.
Pour chaque bande, calculer sur D la même énergie locale E_D que celle
utilisée pour les poids (fenêtres 5/7/9/13). Hors centre 65×65, prendre
le quantile empirique q95, rang inférieur `floor(0,95*(n−1))` des valeurs
triées. Le plancher F vaut `max(sigma²,q95(E_D))`.

Remplacer S par `max(E−F,0)` puis garder `c=S/(S+sigma²)` et le guidage
du §38. Conserver les cas sigma nul, les contributions figées, Y, P,
le fond G8 et le raccord équilibré. Comparer indépendant et les deux
ordres avec beta 0/0,5, plus la référence inchangée : 960 états.
Tracer F, q, sigma, les gains et les métriques précédentes. La règle
`multiscale_null_rules` précise cette modification du §38 dans le JSON.

Ce n'est pas un test indépendant à 5 % : D sert à calibrer et à évaluer,
les fenêtres se recouvrent et les supports gaussiens traversent la zone
exclue. Un dépassement d'énergie n'est pas une probabilité de signal.
Une réduction du bruit n'est utile que sans perte excessive de flux,
de pic ou déformation des couronnes. G8 inchangé reste une limite.

```powershell
python tests/study_noise_pair_ablation.py --multiscale-null-study --data-version 1
```

Contrôle intégré : `noiseNeighbourhoodProbe --multiscale-null-controls`.
Le module teste aussi planchers non décroissants par rapport à la variance,
finitude, invariance à l'ordre sans guidage et rejet du quantile invalide.

## 40. Reconstruction originale guidée par un pilote débruité — 25 octobre 2026

Le q95 réduit le bruit mais efface davantage les formes faibles. Tester
un guide en deux passes, sans boucler sur les pixels filtrés :

1. Calculer p(x) par l'indépendant q95 du §39, avant raccord équilibré.
2. Décomposer p(x) dans les mêmes bandes et mesurer leurs énergies E_p.
3. Sur D connu, calculer p(D), puis ses énergies par bande. Hors centre
   65×65, mesurer leur moyenne M et leur quantile q95 Q.
4. Deux candidats : S=`max(E_p−M,0)` ou `max(E_p−Q,0)` ;
   poids `g=S/(S+N_original)`. À variance originale nulle, conserver
   la bande comme aux étapes précédentes.
5. Sommer G8(x) original et `g*b_original`, puis appliquer le raccord
   équilibré une seule fois. Ne pas ajouter les sorties des deux passes.

Pas de mémoire de poids entre bandes (beta=0). Le pilote sert uniquement
aux poids ; une information originale perdue au premier passage reste
accessible, mais ne sera pas nécessairement reconnue dans le guide.
Les planchers M/Q sont des mesures d'énergie résiduelle, pas le MAD
du bruit d'origine. Leur calibration sur le même D est oracle, dépendante,
sans garantie de taux de fausse alarme. G8 original reste inchangé.

Comparer E-P, indépendant q95 et ces deux variantes, sur les mêmes
960 états : mesurer prioritairement les formes faibles et le bruit des
aplats, sans oublier flux et couronnes. Audits : `multiscale_pilot_rules`,
`pilot_noise_energy_mean`, `pilot_noise_energy_q95` et `pilot_guide`.

```powershell
python tests/study_noise_pair_ablation.py --multiscale-pilot-study --data-version 1
```

Contrôle intégré : `noiseNeighbourhoodProbe --multiscale-pilot-controls`.
Tests module : finitude des deux variantes, constante préservée, entrée
immuable, indépendance de l'ordre sans mémoire et rejet d'une calibration
pilote manquante. Aucun calcul de production modifié.

## 41. Guide propre connu : diagnostic de reconstruction — 25 octobre 2026

Les deux passes du §40 ne retrouvent pas les formes faibles. Séparer
la qualité du guide de la loi de gain, avec un diagnostic **oracle non
déployable**, sans nouvelle acquisition.

Fournir au reconstructeur la vérité synthétique (niveau uniforme plus
forme injectée), et le niveau uniforme seul pour son témoin bruité sans
forme. Décomposer ce guide propre en bandes, comme l'original bruité.
Deux variantes de `E_propre` : moyenne du carré des bandes sur les
fenêtres 5/7/9/13, ou carré au pixel (fenêtre 1). Appliquer
`g=E_propre/(E_propre+N_original)` aux bandes **bruitées originales**.
Sans bruit connu, préserver la bande. Le guide ne contient pas de bruit :
pas de soustraction d'un plancher dans son énergie. Garder G8(x) original,
Y, disponibilité P et le raccord équilibré. Beta=0.

Comparer E-P, indépendant q95, oracle local et oracle ponctuel sur les
960 états précédents. Ce n'est ni une borne de risque optimale ni une
estimation indépendante ; les variances restent calibrées sur D.
L'oracle peut aussi conserver un point parasite d'observation identique.
Tracer pics, flux, couronnes et MSE. Un échec avec guide propre met en
cause la reconstruction étudiée ; un succès ne résout pas la détection.

```powershell
python tests/study_noise_pair_ablation.py --multiscale-oracle-study --data-version 1
```

Métadonnées spécifiques : `multiscale_oracle_study`,
`multiscale_oracle_rules`, `pilot_guide=3/4` ; ces règles priment sur les
champs génériques des campagnes antérieures. Le guide propre reste
réservé au banc de tests. Contrôle intégré : `--multiscale-oracle-controls`.
Tester aussi la finitude, l'invariance à l'ordre et le refus d'un guide
manquant/incompatible ; ne jamais remplacer silencieusement le guide.

## 42. Seuillage des coefficients sans guide propre — 25 octobre 2026

L'oracle ponctuel du §41 améliore la réponse aux petites formes. Tester
une reconstruction observable sans guide propre, toujours avec calibration
oracle du bruit. À partir des mêmes bandes de D, hors centre 65×65,
mesurer les quantiles absolus L=q95 et H=q99,5, rang inférieur
`floor(q*(n−1))`. Ces quantiles portent sur les coefficients, pas sur
leur énergie moyennée ; ils ne garantissent pas un taux d'erreur indépendant.

Sur chaque coefficient b de l'image originale bruitée :

- doux : `sign(b)*max(|b|−L,0)` ;
- progressif : zéro si `|b|≤L`, b si `|b|≥H`, sinon
  `sign(b)*H*(|b|−L)/(H−L)`.

À L=0, préserver la bande ; à H=L>0, les conditions aux bornes évitent
la division par zéro (limite dégénérée discontinue). Pas de passage
du résultat d'une échelle comme entrée à la suivante, pas de guide de
vérité, pas de moyenne spatiale pour le gain. Conserver G8, Y, support P
et raccord final équilibré. Les seuils restent communs à l'état uniforme
et aux formes injectées. Le diagnostic d'impulsion identique reste requis.

E-P, indépendant q95 et ces deux variantes : 960 états, mêmes quatre
formes Co/Cg, amplitudes 6/20 HH, phase 0. Comparer MSE des aplats,
pics/flux/couronnes, pas seulement le gain sur le point fort. Calibration
sur le même D, coefficients corrélés, supports gaussiens traversant la
zone exclue : ce n'est pas encore une validation sur photos réelles.

```powershell
python tests/study_noise_pair_ablation.py --multiscale-coefficient-study --data-version 1
```

Contrôle intégré : `--multiscale-coefficient-controls`. Le JSON conserve
`multiscale_coefficient_rules`, `coefficient_abs_q95`,
`coefficient_abs_q995`, gains et fractions conservées par bande.
Tests : finitude, constante préservée, identité aux seuils nuls et
rejet d'une calibration manquante. Aucun calcul de production modifié.

## 43. Score conjoint de quatre bandes — 25 octobre 2026

Le seuillage indépendant conserve mieux les points, mais pas les petites
formes faibles plus larges. Tester la décision sur le vecteur
`v=(b0,b1,b2,b3)` au même pixel, par plan Co ou Cg, sans coupler ces plans.

Sur les mêmes coefficients de D hors centre 65×65, calculer moyenne mu,
covariance de population `C=E[vvᵀ]−mu*muᵀ` et régularisation
`lambda=0,01*trace(C)/4`. Inverser `C+lambda*I` par Cholesky ; arrêter
en cas d'échec. À covariance exactement nulle, conserver les bandes.
Le score appliqué aux images est `s=sqrt(max(0,vᵀ Creg⁻¹ v))` : ne pas
soustraire mu à l'image. Tracer mu pour rendre cette convention explicite.

Calibrer L=q95 et H=q99,5 sur les scores de D avec la même inverse.
Gain doux `max(1−L/s,0)` ou progressif zéro jusqu'à L, un à partir de H,
`H*(s−L)/(s*(H−L))` entre les deux. Reprendre les conventions de seuil
nul et dégénéré du §42. Appliquer le même gain aux quatre bandes de
l'image originale, sans rotation/reprojection de leurs coefficients.
Conserver G8, Y, P et le raccord équilibré. Pas de guide propre.

Ce partage peut aussi transmettre du bruit aux bandes sans signal.
Le score n'est ni une probabilité, ni un chi-deux validé, ni une preuve
d'indépendance : même D pour covariance, quantiles et évaluation,
recouvrement des supports gaussiens. La régularisation fixée avant le
calcul n'est pas optimisée pour chaque forme.

Comparer E-P, doux indépendant du §42, conjoint doux et conjoint progressif
sur 960 états. Examiner formes faibles/larges, bruit, flux/pics/couronnes,
pas seulement le pic maximal. Métadonnées : `multiscale_joint_rules`,
`joint_covariance`, `joint_noise_mean`, `joint_inverse_regularized`,
`joint_ridge`, `joint_score_q95/q995`, gains et couvertures.

```powershell
python tests/study_noise_pair_ablation.py --multiscale-joint-study --data-version 1
```

Contrôle intégré : `--multiscale-joint-controls`. Tests module : finitude,
constante préservée, identité aux seuils nuls, absence de modification
de l'entrée et rejet de calibration manquante. Production inchangée.

## 44. Diversifier les structures, mesurer localement — 25 octobre 2026

Après les essais de seuils, ne pas choisir une variante sur les seuls
points et bosses positives. Garder tous les paramètres des candidats :
E-P, indépendant q95, doux indépendant et doux conjoint. Même D par
fenêtre, calibration et niveau moyen fixes, Co/Cg à 6/20 HH, phase 0.

Quatre formes s, centrées au même pixel, dans un carré de rayon 15 :

- gaussienne sigma 1,5, tronquée au carré de rayon 5 (zéro ailleurs) ;
- bord vertical : +A pour x≥0, −A pour x<0 dans le carré ;
- rayures : `A*cos(2*pi*x/6)*exp(−(x²+y²)/50)` ;
- damier : `A*cos(2*pi*x/8)*cos(2*pi*y/8)*exp(−(x²+y²)/50)`.

Le bord comporte aussi les transitions externes du carré : le documenter
comme structure finie, pas comme bord infini. Ces formes ont toutes un
centre +A ; le signe négatif existe dans les autres lobes. Rester en
plans linéaires non bornés, sans écrêter artificiellement les injections.
Les métriques colorimétriques conservent leur masque de domaine commun.

Soit r=sortie(signal+D)−sortie(aplat+D). Sur le même carré 31×31 :
gain par projection `sum(r*s)/sum(s²)` ; distorsion de réponse
`sum((r−s)²)/sum(s²)` pour le plan injecté (r seul pour les autres).
En plus, MSE locale sortie/vérité et entrée/vérité : celles-ci gardent
le bruit, contrairement à la seule différence de réponses appariées.
Tracer aussi pics et couronnes. Le ratio de flux est `null` pour les
formes signées, dont la somme peut être faible malgré un contraste élevé.

Le JSON ajoute `shape_kind`, `signed_structure_study`,
`response_projection_gain`, `response_shape_error_energy_ratio`,
`local_output_error_mse`, `local_input_error_mse`. `gaussian_width` est
1,5 uniquement pour la gaussienne, `null` pour les autres. Ces métriques
locales sont aussi disponibles lors des prochains passages du banc
sur les anciennes formes, sans réécriture des JSON historiques.

960 états attendus. Vérifier l'aplat dédupliqué par modèle/fenêtre/plan,
et comparer les nouvelles formes entre candidats dans cette campagne ;
le support de mesure a changé, donc pas d'égalité exigée des anciens flux.
Pas de guide de vérité fourni aux reconstructions, calibration sur D
toujours oracle et dépendante. Aucun résultat ne vaut encore validation
sur une texture réelle ni sur une nouvelle paire indépendante.

```powershell
python tests/study_noise_pair_ablation.py --multiscale-structures-study --data-version 1
```

Contrôles : `--multiscale-structures-controls`, finitude des mesures
locales, support commun, largeur nulle au sens JSON pour les structures
non gaussiennes, flux signé non calculé et cohérence des sorties finales.

## 45. Potentiel de sélection entre modèles fixes — 25 octobre 2026

Réutiliser le JSON structures du §44, sans rendu ni RAW supplémentaire.
Former les comparaisons par ISO, empreintes de paire, niveau, plan,
structure et amplitude. Exiger les quatre modèles, mêmes amplitude,
sigma HH et rectangle ; refuser doublons et disponibilités discordantes.
Exclure une comparaison seulement si tous les modèles s'abstiennent.

Tracer pour chaque comparaison :

- meilleur candidat selon la MSE locale vraie : oracle diagnostique ;
- meilleur candidat selon la MSE vraie de l'aplat : autre oracle,
  représentatif d'un classement au seul bruit, pas d'un sélecteur aveugle ;
- modèles non dominés sur les trois axes minimisés : MSE uniforme,
  MSE locale et distorsion de réponse normalisée.

Conserver tous les axes et le gain projeté par modèle. En cas d'égalité
exacte, départager suivant l'ordre fixe E-P, indépendant q95, doux indépendant,
doux conjoint ; conserver les modèles égaux sur le front non dominé.
Résumer ratios de MSE locale à E-P par moyenne, médiane, maximum et nombre
de dégradations, avec mêmes poids par état. Ce n'est pas le rapport de
sommes d'énergies ni une statistique de mesures indépendantes. Refuser
les métriques non finies et le dénominateur de référence nul.

La sélection sur vérité fournit une enveloppe diagnostique pour **ces
candidats et ces états**, pas une borne universelle ni une performance
réalisable sur photo inconnue. Les données ont déjà servi à explorer les
modèles : ne pas les annoncer comme validation finale indépendante.
Ne pas déduire les risques de mélanges pondérés à partir des seules MSE
individuelles, sans covariance croisée des erreurs de sortie.

```powershell
python tests/study_noise_model_selection.py --data-version 1
```

JSON canonique dérivé `etude-selection-modeles-bruit-vNNN-*`, parent
empreinté, mesures/décisions par comparaison, exclusions explicites,
versions du script/Git et identité des sources du parent. Aucun écrasement
de campagne, aucun TIFF produit, aucun changement de production.

## 46. Sélecteur observable très simple, contrôle croisé par paire — 25 octobre 2026

Réutiliser la preuve structures du §44 et sa sélection du §45, dont
l'empreinte parent doit correspondre. Prédicteurs : les quatre `mean_gain`
des bandes du candidat indépendant q95, pour le plan considéré, mesurés
sur tout le champ 304×304 de l'image bruitée. Ne pas utiliser le gain
de l'aplat témoin, l'ISO, le nom de structure, l'amplitude ou une erreur
à la vérité dans les arguments de prédiction. Ces gains utilisent une
calibration de bruit oracle ; ne pas qualifier l'ensemble d'aveugle.

Pour chaque paire réservée, apprendre sur les trois autres paires :

- choisir le meilleur modèle constant selon la moyenne des ratios locaux
  de MSE à E-P ;
- essayer une seule coupure sur une des quatre caractéristiques, aux
  milieux des valeurs distinctes d'apprentissage ;
- exiger au moins 14 états de chaque côté, puis choisir le meilleur modèle
  constant de chaque feuille ;
- garder la coupure seulement si la somme des risques d'apprentissage
  s'améliore de plus de 1e-12 ; sinon garder le choix constant.

Les vérités servent à l'apprentissage et à l'évaluation, pas à la
prédiction sur la paire réservée. En cas d'égalité, garder le premier
choix dans l'ordre fixe des modèles et la première coupure améliorante.
Ne pas choisir une coupure depuis les valeurs de la paire évaluée.
Refuser toute empreinte de source partagée entre apprentissage et test.

Conserver modèle appris, seuil, bande, tailles des feuilles, paires
apprises/testées, caractéristiques et prédictions par état. Évaluer
risque normalisé moyen/médian/maximal et dégradations contre E-P, meilleur
constant appris et oracle local. Préserver les références de modèles fixes.
L'unité de séparation est la paire, pas la ligne : les états d'une même
fenêtre et les deux plans sont dépendants.

Ce contrôle comporte seulement quatre paires, une par ISO, toutes déjà
explorées lors du choix des modèles. Ce n'est pas une validation finale
indépendante. Les caractéristiques globales sur un patch centré ne
démontrent pas une détection localisée sur photo ni une fusion par pixel.

```powershell
python tests/study_noise_gain_selector.py --data-version 1
```

Preuve dérivée `etude-selecteur-gains-bruit-vNNN-*`, parents empreintés,
JSON exclusif sans écrasement. Aucun nouveau rendu ni changement de production.

## 47. Auditer les gains localisés sans changer les filtres — 25 octobre 2026

Le sélecteur global du §46 apporte peu. Avant d'augmenter sa complexité,
mesurer les gains sur la même fenêtre locale 31×31 que le support des
structures, fixée au centre du champ par le banc. Pour chaque bande :
`local_gain_mean_31`, `local_gain_active_fraction_31` et coordonnées
`local_feature_rect`. Aucun accès à la vérité dans ce calcul, aucune
soustraction du gain de l'aplat ; la calibration de bruit reste oracle D.

Le centre est connu dans cette expérience synthétique ; son utilisation
ne démontre pas qu'un détecteur saurait localiser la structure en photo.
Ces agrégats sont des caractéristiques, pas des probabilités de signal.
Le calcul des filtres, leurs seuils et le raccord restent inchangés.

Rejouer uniquement l'indépendant q95, quatre formes à 6/20 HH en Co/Cg :
240 états attendus, 224 actifs et 16 abstentions. Garder les risques des
quatre candidats dans la preuve structures initiale. Avant toute jointure,
vérifier empreintes sources, niveau, rectangle, amplitude, calibration
et identité des mesures de reconstruction q95 communes. Ne pas comparer
des caractéristiques nouvelles à des labels de rendu différents.

```powershell
python tests/study_noise_pair_ablation.py --multiscale-gain-features-study --data-version 1
```

La preuve `etude-gains-locaux-paires-bruit-vNNN-*` trace les audits et les
mesures de vérification ; `multiscale_gain_features_rules` décrit ce
sous-ensemble et prime sur la liste générique des modèles du §44.
Contrôle intégré `--multiscale-gain-features-controls` : 16 états,
contrats des structures, finitude et cohérence des sorties. Le module
vérifie présence/finitude des moyennes locales et fractions dans [0,1].

À la reprise, comparer le même sélecteur global/local avec la même
validation par paire ; ne pas choisir un modèle final depuis ces quatre
paires déjà utilisées pour explorer les hypothèses. Production inchangée.

## 48. Comparer le même sélecteur sur gains locaux et globaux — 25 octobre 2026

Après vérification du §47, joindre les audits locaux au tableau de risque
figé. L'outil refuse les différences de mesures des canaux, amplitude,
sigma HH, rectangle, centre, support, largeur ou calibration de bruit.
Vérifier également les gains globaux communs et la fenêtre locale attendue,
centrée en `(centre.x−15,centre.y−15)`, taille 31×31. Les références
et les nouveaux audits restent séparés, avec leurs empreintes propres.

Remplacer uniquement les quatre caractéristiques globales du §46 par
les quatre `local_gain_mean_31`. Ni ajout de profondeur, ni variation
du minimum de feuille, ni sélection d'hyperparamètre sur la paire testée.
Apprendre chaque coupure sur les trois autres paires. Préserver tous
les résultats réservés, et rejouer le contrôle global dans le même JSON.
Les prédictions de ce contrôle doivent retrouver le §46.

Comparer les risques normalisés locaux/global/constant, les dégradations
contre E-P et les résultats par paire, pas seulement le risque global
moyen. Les fractions de gain actif sont conservées dans l'audit mais
ne sont pas ajoutées aux prédicteurs de ce test. Pas d'utilisation du
nom de forme, de l'ISO, de l'amplitude ou de l'erreur à la vérité pour
prédire. La vérité sert aux labels d'apprentissage et à l'évaluation.

```powershell
python tests/study_noise_gain_selector.py --feature-scope local --feature-campaign docs/validation/raw/etude-gains-locaux-paires-bruit-v001-20261025T201510Z.json --data-version 1
```

Preuve `etude-selecteur-gains-locaux-bruit-vNNN-*` : parents des risques,
audits, sélection et contrôle global empreintés. L'extraction exige des
gains finis dans [0,1]. Tests de jointure : refus des rendus et supports
différents. Aucun nouveau rendu ni changement de production.

La fenêtre locale est centrée sur la forme par le banc ; ce test ne
valide pas une localisation automatique ni une fusion par pixel. Les
calibrations restent oracle, et les quatre paires ont déjà servi à
explorer les modèles. Conserver ces réserves même si le local réussit.

## 49. Trois intervalles sur une caractéristique locale — 25 octobre 2026

Le sélecteur local du §48 améliore la moyenne sur chaque paire mais
laisse surtout un défaut sur les contours faibles. Tester une extension
strictement bornée, sans modifier les reconstructeurs : choisir une seule
caractéristique parmi les quatre gains locaux, et au plus deux seuils
créant trois intervalles ordonnés. Chaque intervalle choisit un candidat
constant parmi E-P, q95, doux indépendant, doux conjoint.

Sur l'apprentissage uniquement, proposer les seuils aux milieux de
valeurs distinctes ; imposer 14 états au minimum dans chaque intervalle.
Minimiser la même moyenne de ratios de MSE locale à E-P, à poids égal
par état. Ne retenir les trois intervalles que s'ils améliorent de plus
de 1e-12 la somme de risques du meilleur constant/coupure unique appris.
Ne pas imposer E-P au milieu pour reproduire les résultats vus : laisser
les risques d'apprentissage déterminer les modèles. Les seuils exacts
du test ne doivent pas servir à choisir les coupures.

Aux seuils, utiliser la convention ≤ pour la zone inférieure. Préserver
les modèles, tailles d'intervalles et prédictions par pli ; conserver
les contrôles local à coupure unique et global dans le même JSON.
Comparer moyenne/médiane/maximal et dégradations, par paire et par forme,
notamment bord faible et gaussienne forte. Une moyenne améliorée ne
garantit pas la préservation des textures ni l'absence de halos.

```powershell
python tests/study_noise_gain_selector.py --feature-scope local --feature-campaign docs/validation/raw/etude-gains-locaux-paires-bruit-v001-20261025T201510Z.json --selector intervals --data-version 1
```

Preuve `etude-selecteur-gains-locaux-intervalles-bruit-vNNN-*`, parents
empreintés et aucun rendu supplémentaire. Tests de l'ajustement : trois
régimes connus, bornes de feuilles et prédictions par seules caractéristiques.
Ce choix d'extension découle des résultats du même corpus : validation
croisée exploratoire, pas validation finale indépendante. Les limites
de calibration oracle et de localisation centrale restent inchangées.

## 50. Transfert : paire et famille de structures réservées — 25 octobre 2026

Sans modifier le modèle du §49, construire seize plis : une paire et
une des quatre familles de formes à chaque pli. En apprentissage,
retirer toutes les lignes de cette paire **et** toutes les lignes de
cette famille, quelle que soit leur paire. Tester uniquement les lignes
à l'intersection de la paire et de la famille réservées. Chaque état
actif est ainsi prédit une fois, soit 224 prédictions attendues.

Les empreintes de sources doivent rester disjointes et la famille testée
absente de l'apprentissage. Le nom de famille n'est utilisé que pour
séparer les lignes, jamais comme caractéristique de prédiction. Les
deux amplitudes d'une paire restent dans le même lot ; elles ne sont
pas une réserve d'amplitude indépendante dans cette étude.

Garder au plus trois intervalles sur une seule des quatre caractéristiques
locales, seuils et modèles choisis uniquement dans l'apprentissage, minimum
14 états par intervalle. Ne pas compenser une famille absente par des
règles codées d'après son résultat oracle. Refuser les plis vides.

Conserver les contrôles à coupure unique locale et globale sous cette
même séparation ; rejouer aussi le contrôle ordinaire laissant seulement
la paire de côté, qui doit retrouver le §49. Mesurer risques moyens,
médians et maximaux, dégradations, choix de modèles et seuils, par famille
réservée et par paire. Garder les meilleurs constants appris comme repères.

```powershell
python tests/study_noise_gain_selector.py --feature-scope local --feature-campaign docs/validation/raw/etude-gains-locaux-paires-bruit-v001-20261025T201510Z.json --selector intervals --holdout-structure --data-version 1
```

Preuve `etude-selecteur-gains-locaux-intervalles-structures-reservees-bruit-vNNN-*`.
Tests : exclusion effective des deux dimensions, effectifs d'apprentissage
et couverture des prédictions. Aucun nouveau rendu ni changement de production.
Ce test utilise encore les quatre paires explorées et des injections
synthétiques : ne pas le présenter comme une validation finale sur une
nouvelle acquisition, une nouvelle texture réelle ou une calibration aveugle.

## 51. Auditer le support des caractéristiques après échec de transfert — 25 octobre 2026

Réutiliser la preuve du §50, sans changer le sélecteur. Reconstituer les
mêmes plis stricts : paire et famille exclues de l'apprentissage. Vérifier
unicité des états, absence d'empreintes partagées et couverture de tous les
états évalués. Dans chaque pli, normaliser chaque gain par son écart
interquartile d'apprentissage, plancher 1e-6 ; les gains sont dans [0,1].

Distance euclidienne sur ces quatre coordonnées normalisées. Pour chaque
état d'apprentissage, mesurer la distance à son plus proche voisin issu
d'une **autre paire** d'apprentissage ; le q95 de ces distances donne
un seuil indicatif de support. Ni caractéristiques ni vérités du lot
évalué n'interviennent dans les échelles ou le seuil. Les observations
d'apprentissage restent corrélées : aucune calibration de probabilité
ou de fausse alarme n'est revendiquée.

Pour chaque état réservé : conserver son voisin le plus proche dans
l'apprentissage, la distance, le seuil et le classement proche/éloigné.
En cas d'égalité exacte, départager par identité source/état. Tracer le
meilleur modèle selon les risques du voisin et le risque qu'il aurait sur
l'état réservé ; cela reste un transfert diagnostique, pas une promotion
d'un sélecteur au plus proche voisin. Comparer aux choix/erreurs du
sélecteur du §50 et à l'oracle local.

Résumer par famille et proximité : erreurs du sélecteur, désaccords des
modèles optimaux et risque du transfert. Des modèles optimaux différents
peuvent avoir des risques quasi égaux ; ne pas conclure depuis le seul
taux de désaccord. Une proximité métrique ne prouve pas que les gains
contiennent toute l'information, et une distance élevée ne prouve pas
que le cas est réellement hors-distribution. Ce sont des pistes de
diagnostic sur quatre paires explorées, pas une preuve d'impossibilité.

```powershell
python tests/study_noise_feature_support.py --data-version 1
```

Preuve `etude-support-caracteristiques-bruit-vNNN-*`, parent empreinté,
échelles/seuils par pli, distances et choix/risques par état. Aucun RAW
relu, aucun nouveau rendu, aucune modification du code de production.

## 52. Faisabilité MC-SURE, covariance connue — 25 octobre 2026

Étude numérique indépendante des filtres IPR. Références primaires :
[Ramani, Blu, Unser, MC-SURE (2008)](https://bigwww.epfl.ch/publications/ramani0803.html)
pour la divergence Monte-Carlo en bruit gaussien blanc ;
[Luisier, thèse, §2.4](https://bigwww.epfl.ch/publications/luisier1001.pdf)
pour l'expression du risque en bruit gaussien coloré. La restriction au
bruit gaussien doit rester explicite ; ne pas transposer directement au RAW.

Dans ce pilote, y=x+n, variance marginale connue 1. Deux générateurs :
blanc gaussien, ou blanc convolué périodiquement par le noyau séparable
[1,2,1]/4 et divisé par sa norme L2, 3/8. Taille 64×64, évaluation au
carré central 31×31. Les perturbations z sont indépendantes du bruit n,
de covariance identique à n, ou volontairement blanches pour le contrôle
mal spécifié en bruit corrélé. Aucun paramètre appris depuis la vérité.

Avec W le masque de mesure, utiliser :
`SURE_W = moyenne_W((f(y)−y)²) − 1 + 2*moyenne_z moyenne_W(z*J_f(y)z)`.
Approximer `J_f(y)z` par `(f(y+epsilon*z)−f(y−epsilon*z))/(2*epsilon)`.
Perturber l'image complète, pas seulement la ROI : ses dépendances
spatiales extérieures participent au calcul. Epsilon 0,01, 16 sondes,
comparer leurs préfixes 4/16 avec les mêmes sondes entre filtres. Garder
les estimations négatives ; ne pas les transformer en probabilités.

Filtres numériques : identité, zéro, gain 0,5, lissage périodique 3×3
par le noyau ci-dessus non renormalisé L2, seuil doux 1,5 et progressif
1,5→3. Signaux : aplat, gaussienne sigma1,5, bord fini, rayures période6,
damier période8, dans le support central. Amplitudes 2/6 fois l'écart-type
marginal, pas HH. 32 réalisations indépendantes par signal et type de bruit,
soit 576 observations. Graines de bruit/sondes séparées et conservées.

Comparer par filtre fixe biais moyen, dispersion et erreur Monte-Carlo
de SURE à la MSE vraie. Évaluer séparément le risque vrai du choix par
minimum SURE et son excès à l'oracle parmi les six filtres : l'absence
de biais pour un filtre fixe ne rend pas son minimum sans biais. Les
sondes sont partagées pour les comparaisons, les essais restent appariés.
Un epsilon unique n'établit pas la convergence des filtres non linéaires.

```powershell
python tests/study_noise_sure_feasibility.py --data-version 1
```

Preuve `etude-sure-faisabilite-bruit-vNNN-*`, mesures par observation,
graines, covariance génératrice, ROI, epsilon et versions Python/NumPy/script.
Tests préalables sur base orthogonale complète : trace locale exacte des
filtres linéaires sous covariance blanche et corrélée, epsilon 0,01/0,001.
Aucune hypothèse de covariance diagonale ne doit remplacer silencieusement
la covariance corrélée. Aucun RAW relu, aucun TIFF produit, aucun filtre
IPR validé par ce contrôle. Vérifier ultérieurement non-gaussianité,
non-stationnarité, calibration et coût avant tout raccordement photographique.

## 53. MC-SURE sur les reconstructeurs C++ expérimentaux — 25 octobre 2026

Après le contrôle mathématique du §52, tester sans réécriture les modules
C++ multiscales indépendant q95, doux indépendant et doux conjoint.
Identité et G8 constituent deux témoins supplémentaires. L'outil natif
`noiseSureFiltersTests --study sortie.json` est piloté par le lanceur
Python ci-dessous ; sans argument, il exécute ses contrôles unitaires.

Covariance connue : gaussienne blanche ou bruit blanc passé dans le noyau
binomial 3×3 périodique, divisé par 6 pour obtenir variance marginale 1.
Préparer les seuils/covariances des filtres sur une réalisation indépendante
128×128, fenêtre interne [8,8,112,112], zone centrale exclue comme dans
le module existant. Garder cette calibration fixe pendant tous les essais
et toutes les perturbations de ce type de bruit. Il ne s'agit pas d'une
estimation aveugle ; la covariance génératrice exacte de SURE est connue.

Neuf signaux du §52, huit réalisations chacun, deux bruits : 144 observations.
Taille 128×128, carré central31×31, amplitudes2/6 en sigma marginal, pas HH.
Huit sondes adaptées, préfixes4/8, epsilon0,01, même expression locale
de SURE et mêmes sondes pour tous les candidats. Générer chaque sonde
depuis la graine `observation_seed+1000000+k*100000`, k=0..7 ; les graines
de calibration sont distinctes de celles des observations et des sondes.
La vérité sert seulement à évaluer risques fixes et choix SURE.

Conserver termes directionnels individuels, résidu, SURE, MSE vraies et
excès du modèle choisi à l'oracle parmi les cinq candidats. Les minima
SURE peuvent être négatifs : ne pas tronquer. Comparer par forme et bruit,
pas seulement la moyenne globale. Vérifier ultérieurement la stabilité
en epsilon et le nombre de sondes avant toute optimisation photographique.

```powershell
python tests/study_noise_sure_filters.py --data-version 1
```

Preuve `etude-sure-filtres-bruit-vNNN-*`, versions OpenCV, code/binaire,
graines, ROI et calibration. Contrôles natifs : constante, finitude,
immutabilité et différences directionnelles des opérateurs linéaires.
Ce banc n'inclut pas ImageEditor/candidateE, Y, disponibilité P, raccord
équilibré ni décodage : aucun résultat n'est une validation du moteur
photographique ou du bruit post-RCD réel. Aucun TIFF produit.

## 54. Sensibilité de SURE à une variance mal spécifiée — 25 octobre 2026

Réutiliser la preuve du §53 et vérifier variance marginale vraie=1.
Pour chaque candidat, conserver résidu `R=mean_ROI((f(y)−y)²)` et termes
directionnels d_k enregistrés. Rejouer avec q le ratio d'écart-type
supposé/vrai : `SURE(q)=R−q²+2*q²*mean(d_k)`.
Les sondes restent celles de covariance unitaire adaptée ; seul le facteur
scalaire de variance de l'estimateur de risque est modifié.

Déclarer avant calcul q dans [0,75 ; 0,9 ; 1 ; 1,1 ; 1,25], avec
préfixes4/8. Exiger qu'à q=1 les scores reproduisent ceux du parent,
tolérance absolue1e-12 pour le rejeu double précision. Ne pas modifier
la calibration des reconstructeurs, leurs sorties, ni choisir un q
optimal depuis les MSE vraies. Les scores négatifs restent autorisés.

Conserver 1 440 décisions : mêmes 144 observations, cinq q, deux nombres
de sondes. Mesurer MSE vraie choisie, excès à l'oracle, choix et écarts
au meilleur fixe calculé descriptivement sur la même population. Ce
dernier n'est pas une politique prospective apprise sur un autre lot.
Les essais sont entièrement appariés entre niveaux q ; pas d'argument
de significativité basé sur 1 440 observations indépendantes.

```powershell
python tests/study_noise_sure_sensitivity.py --data-version 1
```

Preuve `etude-sure-sensibilite-bruit-vNNN-*`, parent empreinté,
scores/choix/risques par observation. Tests : carré du ratio sigma,
reproduction numérique simple, refus de variance non positive ou de
sondes manquantes. Aucun nouveau calcul de filtre ni RAW relu.
Ce test n'inclut pas l'effet d'une mauvaise estimation sur les paramètres
du filtre lui-même, l'erreur de forme de covariance, la non-stationnarité
ou la non-gaussianité du bruit réel. Aucune validation de production.

## 55. MAD des pixels ou des bandes : vérifier les domaines — 25 octobre 2026

Avant une estimation automatique pour SURE, distinguer l'écart-type des
pixels et celui d'une bande. Sur le générateur synthétique du §52,
comparer trois mesures : MAD gaussien des pixels, MAD gaussien HH,
MAD HH corrigé par le transfert **connu** du générateur. Le MAD est la
médiane des écarts absolus à la médiane divisée par 0,6744897501960817.

HH décimé sur blocs disjoints 2×2, coefficient
`(a−b−c+d)/2`. Supprimer seulement la dernière ligne/colonne si impaire.
En blanc, transfert sigmaHH/sigmaPixel=1. Pour le noyau périodique
binomial normalisé spécifique au banc, transfert=1/3, vérifié sur
base complète. Aucun facteur de ce type n'est universel pour un RAW.
Le MAD HH brut est une mesure de bruit de bande, pas une estimation
automatiquement marginale. La correction connue reste oracle.

Taille64/128, bruits blanc/corrélé, aplat et quatre formes à amplitudes2/6
sigma marginal, 32 réalisations : 1 152 observations indépendamment
générées. Mesurer chacune globalement et dans le carré central31 sans
masquer les structures : 2 304 lignes, trois estimations chacune.
Les deux domaines d'une même image sont dépendants. Changer de taille
modifie la proportion de texture et l'effectif, pas seulement la variance
de l'estimation. Le centre est connu dans le banc synthétique.

Conserver estimations par image/domaine, graines et tailles HH. Résumer
ratios à sigma marginal vrai1, dispersion, cas sous0,75, dans[0,9;1,1]
et au-dessus1,25, sans ériger ces bornes issues du §54 en critères de
validation photographique. Sur texture, la distribution observée peut
ne plus être un bruit gaussien centré ; les estimateurs peuvent être contaminés.

```powershell
python tests/study_noise_scale_calibration.py --data-version 1
```

Preuve `etude-calibration-echelles-bruit-vNNN-*`, versions du générateur,
script et NumPy, contexte et covariance connus. Aucun RAW relu ni filtre
recalculé. Ne pas appliquer les nouvelles estimations aux anciens risques
SURE sans observations correspondantes : les populations sont distinctes.
Cette étude ne modifie pas `NoiseEstimation` en production et ne prouve
aucun défaut de son MAD dans le domaine pour lequel il est conçu.

## 56. Orientations, phases et fréquences du MAD — 25 octobre 2026

Contrôle après le §55 : un bord vertical s'annule dans HH, sans garantir
la même réponse à un contour incliné. Fixer avant calcul un champ128,
un aplat et112 scènes : normales0..90° par15°, décalages de la coordonnée
normale0/0,5 pixel, amplitudes2/6 sigma marginal, bord ±amplitude ou
sinusoïde crête amplitude de période4/8/16 pixels selon la normale.
Structures sur tout le champ : banc de stress, pas prévalence photographique.

Deux bruits gaussiens, blanc ou corrélé par le noyau binomial du §55 ;
16 réalisations indépendantes par scène/covariance, graines distinctes.
Chaque image mesurée sur le champ entier et central31 :3 616 images,
7 232 lignes appariées entre supports. Le changement de support change
l'effectif et la phase du découpage HH, pas seulement la surface.

Comparer MAD pixels et MAD HH / transfert connu (1 ou1/3). Conserver
chaque estimation et résumer médiane, extrêmes, effectifs sous0,75,
dans[0,9;1,1], au-dessus1,25. Bornes descriptives uniquement ; pas de
sélection automatique du meilleur estimateur, pas d'ajustement du facteur.
La covariance est oracle ; aucune valeur générale déduite pour RCD.

```powershell
python tests/study_noise_scale_orientation.py --data-version 1
```

Preuve `etude-orientation-echelles-bruit-vNNN-*`, parent du §55 et
dépendances empreintés, versions et provenance Git. Tests unitaires :
113 scènes, annulation des bords axiaux dans HH, réponse au diagonal,
période sinusoïdale et refus d'une période non positive. Aucune lecture
RAW, aucun filtre/SURE rejoué et aucune modification de production.

## 57. Niveau mesuré et décision SURE sur les mêmes images — 25 octobre 2026

Relancer le banc natif du §53 sans changer graines, filtres, calibrations
ou sondes. Ajouter pour chacune des144 observations les MAD pixels,
HH brut et HH converti avec la covariance connue (transfert1 ou1/3),
sur entier128 et central31. HH brut utilisé comme sigma marginal est
un contrôle volontaire de mauvais domaine pour le bruit corrélé.
Ajouter le contrôle sigma1 ; chacun avec4/8 sondes :2 016 décisions
sur144 images, pas2 016 observations indépendantes.

Avant de décider, vérifier identité des scènes/graines et reproduction
des MSE vraies et scores nominaux du parent, tolérance absolue1e-10.
Refuser population incomplète ou entrée modifiée pendant le calcul.
Ne changer que sigma² dans la formule de risque, jamais les paramètres
du filtre. Ne pas choisir un estimateur depuis les risques vrais.
Conserver scores, choix et risques par observation, MAD et audit natif.
Comparer à indépendant-q95 fixe et à l'oracle descriptif du même jeu.

```powershell
python tests/study_noise_sure_measured_scale.py --data-version 1
```

Preuve `etude-sure-echelle-mesuree-bruit-vNNN-*`, parent, sonde et sources
empreintés. Tests : médiane paire/impaire, MAD constant/vide, population
et contrôle nominal, refus d'observation ou risque modifié.
Les formes restent celles du §53, pas les textures denses du §56.
La covariance des sondes et les paramètres des filtres restent oracle ;
pas d'estimateur aveugle complet. Une variance dépendant des données ne
garantit pas la propriété de non-biais du SURE à variance connue.
Aucun RAW ni changement de production ; aucune inférence perceptuelle.

## 58. SURE sur structures denses orientées — 25 octobre 2026

Après le §57, vérifier l'utilité du sélecteur sur une famille pour laquelle
le MAD est exposé à la texture. Conserver les cinq reconstructeurs natifs
et leurs calibrations indépendantes figées. Fixer49 scènes : aplat puis
normales0/30/45°, offsets0/0,5 pixel, amplitudes2/6 sigma marginal,
bord ±amplitude ou sinusoïde de période4/8/16 pixels. Tout le champ128
est structuré, ROI de mesure central31. Ce n'est pas une fréquence
d'occurrence naturelle. Deux covariances gaussiennes connues, huit
réalisations par scène, soit784 observations, nouvelles graines.

Quatre niveaux scalaires de décision : exact1, MAD pixels central31,
HH converti par covariance connue sur central31 et entier128.
Deux nombres de sondes4/8, communes aux méthodes, soit6 272 décisions
dépendantes par observation. Epsilon0,01, perturbation du champ complet,
scores négatifs conservés. Reproduire les scores nominaux depuis les
résidus et dérivées avant de comparer. Calibration des filtres et forme
de covariance des sondes demeurent oracle dans tous les cas.

```powershell
python tests/study_noise_sure_oriented.py --data-version 1
```

Conserver mesures natives, paramètres géométriques, graines, MAD,
scores/choix/risques et calibrations, binaire/sources empreintés.
Preuve `etude-sure-structures-orientees-bruit-vNNN-*`. Vérifier49 scènes,
784 lignes natives, modèles complets, absence de modification du binaire.
Tests : grille, sinusoïde, sélection par score et conservation de la
géométrie, refus d'un rejeu nominal incohérent.

Lire dans cet ordre : potentiel oracle versus meilleur fixe descriptif,
SURE à niveau exact, puis SURE à niveaux estimés. Regrouper par bruit,
amplitude, angle et période sans ajuster une règle sur les risques vrais.
L'oracle et le meilleur fixe sont évalués sur la même population ; aucun
gain prospectif ou perceptuel garanti. Aucun RAW ni code de production.

## 59. Moyennes non locales à forces fixes — 25 octobre 2026

Après §58, modèle complémentaire par similarité de patchs, sans nouveau
sélecteur. Base scientifique : [publication des auteurs IPOL2011](https://www.ipol.im/pub/art/2011/bcm_nlm/).
Implémentation originale scalaire, pixelwise, flottante sans écrêtage ;
pas de copie de code externe ni d'équivalence revendiquée à sa variante
patchwise. Audit licences/brevets séparé avant une éventuelle livraison.

Patch5×5 uniforme, recherche21×21, h=0,4/0,8 fixes pour sigma marginal1.
Distance moyenne quadratique d ; poids `exp(-max(d-2*(1-rho(dx,dy)),0)/h²)`.
Rho connu du bruit générateur, produit des termes1D1,2/3,1/6 aux retards
0/1/2 dans le cas corrélé ; blanc nul hors décalage0. La correction de
moyenne ne constitue pas un blanchiment des patchs. Auto-poids égal au
maximum des poids non centraux ; repli identité si sous-flux de tous les
poids. Calcul seulement sur central31, mais lecture des voisinages requis
dans le champ128. Aucune frontière artificielle dans le support utile.

Reprendre les784 scènes/graines de §58. Comparer identité, q95 et les
deux forces fixes ; vérifier risques identité/q95 contre le parent à1e-10.
Conserver MSE, erreur moyenne et projection <sortie,vérité>/<vérité,vérité>.
Sur aplat, projection0 conventionnelle, non mesurée. Pas de score SURE,
pas de sélection par vérité ; les métadonnées communes de sondes du
binaire décrivent ses autres modes, pas ce calcul non local.

```powershell
python tests/study_noise_nonlocal.py --data-version 1
```

Preuve `etude-moyennes-non-locales-bruit-vNNN-*`, sources/binaire/parent
empreintés, chaque état conservé. Tests : constantes, entrée immuable,
sortie finie, corrélation analytique, refus de référence ou population
incomplète. Ces textures répétitives ne valident ni les formes isolées,
ni le bruit réel hétéroscédastique/couleur post-RCD. Aucun code de production.

## 60. Sept estimations directionnelles et confiance calibrée — 25 octobre 2026

Piste proposée par l'utilisateur, améliorée sans récurrence de balayage.
Dans une empreinte5×5, quatre fenêtres de coin3×3, lignes horizontale et
verticale de longueur5, fenêtre centrale3×3. Exclure partout le pixel
central : effectifs8/8/8/8/4/4/8. Estimations m_i par médiane, dispersion
d_i par médiane des écarts absolus intra-support. Sept sorties corrélées
issues d'une source figée, pas sept observations indépendantes.

Calibration indépendante :1 024 champs9×9 de bruit blanc ou corrélé
normalisé à sigma marginal1, mêmes générateurs que les §55–59. Mesurer
au pixel central variances v_i, matrice de covariance et quantiles95 %
q_i des dispersions. Mesurer aussi le quantile95 % t du désaccord
`D=mediane(|m_i-mediane(m_i)|)`. Pour ce générateur invariant par rotation,
regrouper v_i et q_i entre quatre coins et entre deux axes. Conserver
la covariance empirique non regroupée pour audit, pas pour prétendre
à une fusion optimale. Aucun signal propre utilisé pour ajuster ces poids.

Comparer quatre variantes fixées avant calcul :

- Moyenne des sept estimations.
- Médiane des sept estimations.
- Fusion `sum(w_i*m_i)/sum(w_i)`, avec
  `w_i=1/(v_i+max(d_i²-q_i²,0))` : heuristique marginale, non optimale.
- Fusion prudente : gain `min(1,(t/max(D,1e-15))²)`, mélange du résultat
  pondéré avec le pixel original. D est un désaccord, pas un sigma de bruit.

Ajouter identité et médiane3×3 sur les mêmes observations. Champ64,
central31 ;49 scènes denses de normales0/30/45°, phases0/0,5 pixel,
amplitudes2/6, bords et sinusoïdes périodes4/8/16, plus8 formes localisées
du §52. Deux covariances et16 réalisations par scène :1 824 images,
10 944 mesures de méthode. Nouvelle population, pas appariée aux images
C++128 précédentes. Conserver paramètres, graines, mesures, calibration,
covariance, désaccord et gain. Mesurer MSE, biais et projection sur le signal
connu, nulle au sens non définie sur l'aplat (`null`). Aucune détection
probabiliste de pixels bruités ni garantie de préservation des détails.

```powershell
python tests/study_noise_seven_directions.py --data-version 1
```

Preuve `etude-sept-directions-bruit-vNNN-*`, script et générateurs empreintés.
Tests : supports excluant le centre, constantes, impulsion centrale,
équivariance rotation90° à paramètres symétriques, entrée immuable,
sorties finies et refus d'entrée/calibration invalide. Calibration oracle
stationnaire : aucune validation RAW ou de production, pas de conclusion
fondée sur la seule concordance de sept estimations pouvant partager un biais.

## 61. Contrôle du centre tenu hors prédiction — 25 octobre 2026

Après §60, tester le biais commun des sept prédictions. Reprendre les
1 824 mêmes observations64 et calibrations, aucune retouche des poids.
Régénérer chaque scène/bruit depuis paramètres et graines ; exiger
reproduction des quatre MSE directionnelles du parent à1e-12.
Conserver les métriques des références parent et ajouter deux variantes
sur ces mêmes pixels centraux31.

Innovation `r = original - prédiction pondérée`. Calibrer le quantile95 %
t_I de |r| sur1 024 champs9×9 de bruit seul par covariance, graines980001
et980002 distinctes de la première calibration. Calcul au centre ; même
prédicteur pondéré. Le centre est exclu du support de prédiction, mais
pas indépendant des voisins corrélés. Conserver les résidus de calibration,
pas seulement le quantile. Sigma marginal et covariance restent oracle.

Gain innovation `g_I=min(1,(t_I/max(|r|,1e-15))²)`. Comparer :

- Innovation seule : `original-g_I*r`.
- Innovation et directions : `original-min(g_I,g_D)*r`, où g_D est le
  gain original du désaccord entre sept estimations.

Pas de récurrence de balayage, changement de seuil sur la vérité ni nouvelle
estimation de sigma. Quantile de calibration, non probabilité de pixel
bruité. Les grands résidus peuvent refléter bruit ou signal ; un gain réduit
laisse l'un comme l'autre. Vérifier MSE et projection par forme/amplitude,
pas seulement les moyennes globales. Tests : symétrie clair/sombre du gain,
bornes, décroissance avec |r|, refus d'entrée invalide, reconstruction de scène.

```powershell
python tests/study_noise_seven_innovation.py --data-version 1
```

Preuve `etude-innovation-sept-directions-bruit-vNNN-*`, parent et sources
empreintés. Les anciennes métriques restent canoniques dans leur parent ;
les nouvelles observations sont appariées, pas une validation indépendante.
Aucun RAW, TIFF, code de production ni inférence perceptuelle directe.

## 62. Comparaison non local/directions sur pixels identiques — 25 octobre 2026

Après §61, fixer le sous-ensemble des réalisations0..3 de chacune des
57 scènes/covariances du parent :456 images, sans sélectionner par résultat.
Champ64, central31, deux bruits gaussiens connus. Régénérer les scènes et
bruits par graines ; reproduire les quatre anciennes MSE directionnelles
à1e-12. Reprendre les métriques du parent et sa calibration figée.

Ajouter deux NLM fixes h0,4/0,8, même formule du §59 : patch5×5 uniforme,
recherche21×21, correction de l'espérance du SSD par2*(1-rho), sigma1 connu,
auto-poids maximum des autres poids, repli identité si sous-flux. Calculer
les moyennes de patchs par sommes intégrales en double. Aucun clipping,
conversion8bits ou frontière artificielle sur le support mesuré.
Les lectures restent dans le champ source : les21×21 centres de recherche
et leurs patchs5×5 autour du centre31 tiennent dans64.

```powershell
python tests/study_noise_nonlocal_directional.py --data-version 1
```

Preuve `etude-nonlocal-directions-appariees-bruit-vNNN-*`, deux parents
et dépendances empreintés. Conserver état/scène/graines, toutes métriques
parent et nouvelles MSE/biais/projections. Tester somme intégrale contre
moyenne explicite, formule directe d'un pixel, constantes, entrée immuable
et entrées invalides. L'implémentation double n'est pas déclarée bit à bit
identique à la version OpenCV float du §59.

Lire résultats par covariance, famille, forme et amplitude, pas uniquement
en moyenne. Projection non définie sur l'aplat (`null`). Ni sélection
automatique entre modèles ni ajustement de force sur la vérité. Seulement
quatre réalisations par scène ; données déjà examinées, pas validation
indépendante. Les1 824 lignes antérieures ne sont pas relancées intégralement.
Calibration oracle et bruit synthétique : aucun gain photo ou RAW réel
déduit sans campagne adaptée. Aucune modification de production.

## 63. Hybride convexe non local/directionnel — 25 octobre 2026

Après §62, mêmes456 images64, mêmes références et calibrations initiales.
Reproduire à1e-12 les MSE des deux NLM et de la sortie directionnelle
combinée avant de mesurer l'hybride. Pas de changement des reconstructeurs.

Nouvelle calibration indépendante :16 champs64 par covariance, graines
18000000+trial en blanc,18010000+trial en corrélé. Pour chaque base NLM
h0,4/h0,8, mesurer la différence avec le candidat directionnel au centre31.
T = quantile95 % de l'écart absolu, regroupé sur ces16 champs ; conserver
par champ moyenne, variance, quantile et graine. Les pixels d'un champ
sont dépendants, pas15 376 observations de calibration indépendantes.
Le sigma marginal1 et la covariance du générateur demeurent connus.

Avec B sortie NLM et D sortie directionnelle, fixer :

```text
d = D-B
w = max(0,1-(T/max(abs(d),1e-15))²)
sortie = B+w*d
```

Cette fusion est convexe et n'augmente pas la différence au-delà de D,
mais ne garantit pas de s'approcher de la vérité. L'écart peut être du
bruit et deux prédictions peuvent effacer le même détail. Ne pas nommer
le poids probabilité de signal. Aucun seuil ajusté sur les images propres.
Deux variantes fixes, pas de sélection par MSE vraie ou apprentissage.

```powershell
python tests/study_noise_nonlocal_hybrid.py --data-version 1
```

Preuve `etude-hybride-nonlocal-directionnel-bruit-vNNN-*`, trois parents
et sources empreintés. Conserver toutes métriques parent, nouvelles
MSE/biais/projections, poids et proportion d'activation par état. Tester
poids, convexité, symétrie clair/sombre, égalité des prédictions, entrée
immuable et rejet d'entrées invalides. Comparer petites formes par amplitude
et coût sur aplats avant une conclusion globale. Aucune nouvelle scène
réelle, preuve RAW/couleur ou modification de production.

## 64. Décomposition du NLM et poids oracle — 25 octobre 2026

Après rejet de l'hybride §63, diagnostiquer la perte de signal dans le NLM,
sans nouvelle force ni seuil. Reprendre les456 observations du §62.
Factoriser seulement le générateur de poids ; reproduire les MSE des
deux forces NLM à1e-12 contre leur parent avant toute interprétation.

À poids fixés pour chaque réalisation, appliquer exactement les mêmes
poids à y=s+n et à s. Inclure auto-poids et normalisation identiques.
Définir S=W*s, N=W*y-S, D=S-s. Mesurer par état :

```text
MSE = mean(D²) + mean(N²) + 2*mean(D*N)
```

Exiger cette identité à1e-12, conserver chaque terme, moyenne de N,
projection de W*y et de S sur s ; `null` sur aplat. Les poids bruités
dépendent de n : D n'est pas un biais statistique fixe, N peut être non
centré, ne pas omettre le terme croisé.

Ajouter un oracle diagnostique : poids calculés depuis la vérité s,
distances propres sans soustraction de2*(1-rho). Conserver h0,4/h0,8,
patch5×5 et recherche21×21, auto-poids maximum, puis moyenner y bruité.
Ne pas substituer une sortie propre connue ni recalibrer h sur les risques.
Le risque oracle à h fixé n'est pas une borne optimale garantie.

```powershell
python tests/study_noise_nonlocal_decomposition.py --data-version 1
```

Preuve `etude-decomposition-nonlocal-bruit-vNNN-*`, parent et sources
empreintés. Tester reconstruction sortie=S+N, décomposition des carrés,
chemin original, oracle moyennant encore les pixels bruités et refus
d'entrées invalides. Comparer d'abord formes isolées par amplitude puis
coûts sur aplats et textures répétitives. Bruit synthétique à covariance
connue, pas une validation photographique, RAW ou de production.

## 65. Distance de patch à covariance des différences — 25 octobre 2026

Après §64, isoler l'effet de covariance sur la similarité. Prédéclarer
trial0..1 des57 scènes et deux bruits du parent :228 images appariées.
Les métriques euclidiennes sont reprises du JSON parent immuable.
Garder patch5, recherche21, h0,4/0,8, auto-poids et centre31.

Pour décalage delta et coordonnées a,b dans les25 pixels du patch :

```text
K_delta[a,b] = 2*C(a-b)-C(a-b-delta)-C(a-b+delta)
distance = (2/25)*d^T*inverse(K_delta)*d
```

C est la covariance connue du générateur de sigma marginal1. Même en
bruit blanc, les patchs chevauchants ont des différences dépendantes.
L'espérance de la distance bruit seul vaut2. Utiliser la même forme de
poids qu'avant, soustraire2 sur guide bruité et0 sur guide propre oracle.
Les pixels moyennés restent bruités ; décomposition signal/bruit/croisé
inchangée. Distance normalisée en espérance, pas en force perceptuelle.

Calculer K pour chaque décalage non central. Refuser lambda_min inférieur
ou égal à1e-10*lambda_max ; aucune pseudo-inversion ou régularisation
ajustée. Vérifier inverse*K à1e-9. Conserver le conditionnement de chaque
matrice. Cette covariance oracle n'est pas une estimation post-RCD.

```powershell
python tests/study_noise_nonlocal_covariance.py --data-version 1
```

Preuve `etude-distance-covariance-nonlocal-bruit-vNNN-*`, parent et sources
empreintés, environnement de threads consigné. BLAS/OMP/MKL un thread
pour le banc. Tests exacts sur base orthogonale complète16×16 : covariance
des différences blanche/corrélée et décalages1/0,3/2, espérance2 après
normalisation ; constantes et guide oracle moyennant encore les pixels
bruités, refus du décalage central. Pas d'argument de significativité sur
deux réalisations par scène ni d'inférence RAW/couleur/production.

## 66. Double similitude photométrique et covariance — 25 octobre 2026

Après §65, reprendre les228 mêmes images et les deux forces fixes.
Reproduire les MSE covariance seules à1e-12 sur guides bruité et propre,
avant de mesurer la variante. Aucun nouveau seuil ajusté sur la vérité.

Pour chaque décalage, calculer :

```text
e = max(distance_euclidienne-2*(1-rho_delta),0)
c = max(distance_covariance-2,0)
distance_double = max(e,c)
poids = exp(-distance_double/h²)
```

En guide propre oracle, remplacer les deux planchers de bruit par0.
Même patch5, recherche21, h0,4/0,8, auto-poids maximum des autres poids,
moyenne des pixels bruités et normalisation. Le guide propre reste un
diagnostic non déployable. Intersecter deux similitudes n'est pas une
probabilité de signal ni un test statistique de niveau connu. Le maximum
des distances bruitées peut augmenter le rejet et le bruit résiduel ;
ne pas interpréter poids brut plus petit comme garantie sur l'image finale.

```powershell
python tests/study_noise_nonlocal_dual_distance.py --data-version 1
```

Preuve `etude-double-distance-nonlocal-bruit-vNNN-*`, parent et sources
empreintés, mêmes décompositions signal/bruit/croisé et projections.
Tests directs en un pixel sur guides propre/bruité avec corrélation de
retard connue, constantes et entrée immuable. Un thread BLAS/OMP/MKL.
Vérifier taches/damiers par amplitude, puis aplats et motifs denses, sans
sélection d'un h ou d'un coefficient par vérité. Deux réalisations par
scène déjà examinées, aucune validation indépendante RAW ou production.

## 67. Guide linéaire préfiltré pour matching seul — 25 octobre 2026

Après §66, isoler le guide : mêmes228 images, mais distance euclidienne
sur guide préfiltré par noyau binomial séparable3×3 [1,2,1]/4. Aucune
modification de la reconstruction : elle relit les valeurs originales y.
Pas de récurrence directionnelle ni double lissage de la sortie.

Covariance source C connue. Pour le noyau k, calculer exactement
`C_G(delta)=sum_a,b k(a)*k(b)*C(delta+a-b)`.
Variance du guide `v_G=C_G(0)` ; plancher de distance au décalage delta
`2*(v_G-C_G(delta))`. Corriger le SSD moyen5×5 avec ce plancher,
pas avec le sigma source. Forces fixes h=0,4/0,8*sqrt(v_G). Garder
recherche21×21, auto-poids maximal, centre31 et repli identité si sous-flux.
Le guide et les pixels reconstruits partagent du bruit ; aucune hypothèse
d'indépendance. Noyau périodique du générateur, support utile sans wrap.

Oracle : guide binomial de s propre, plancher0, même h et reconstruction
de y bruité. Cet oracle n'est pas déployable et peut encore effacer des
formes. Reprendre les métriques classiques du parent immuable ; conserver
nouvelles MSE/déformation/bruit/croisé/projections. L'échelle h normalisée
au guide ne garantit pas une force perceptuelle identique à celle d'avant.

```powershell
python tests/study_noise_nonlocal_pilot.py --data-version 1
```

Preuve `etude-guide-pilote-nonlocal-bruit-vNNN-*`, parent et sources
empreintés, covariance du guide et planchers par décalage conservés.
Tests exacts sur base16×16 pour covariance et variances de différences,
constantes, entrée immuable, reconstruction d'une impulsion démontrant
la lecture des valeurs originales. Un thread de calcul. Pas de réglage
sur vérité, validation photo ou changement de production.

## 68. Première campagne réelle bornée post-RCD — 25 octobre 2026

Après le bilan §195 du rapport, quatre paires R6 III déjà réservées puis
historiquement examinées :9660/9661 ISO100,9676/9677 ISO1600,9702/9703
ISO6400,9815/9816 ISO51200. Vérifier existence unique et empreintes
canoniques malgré les anciens chemins. Pas de nouvelle prise demandée.
Cette phase teste les paires ; les photos naturelles restent à auditer
séparément pour la conservation visuelle des petites formes.

Extraction native en mémoire, recette neutre et ouverture RCD+FlatOnly
actuelle. Référence DetailProcessing::reduceNoise50/50 sur image entière.
Sélecteur de zones existant sur A : première sombre/plate/détaillée disponible.
Extraire un carré64 au centre de chaque zone256 et réutiliser les mêmes
coordonnées sur B. Mesure central31. Conserver échantillons linéaires et
référence dans JSON, réglages, déclaration, empreintes et temps ; aucun TIFF.

Deux autres candidats seulement : NLM scalaire par plan h0,4 patch5/recherche21,
et NLM à matching sur guide binomial avec h0,4*sigmaGuide. Reconstruction
depuis les pixels originaux dans les deux cas. Aucune nouvelle distance,
fusion ou sélection de force. Ces paramètres ne sont pas déclarés de force
perceptuelle égale au réglage production50/50.

Calibration diagnostique sur D=(A−B)/sqrt2 hors centre33 protégé : sigma
marginal par écart-type, planchers de distances mesurés par décalage, au
moins128 couples de pixels de calibration. Guide : même calcul sur D
préfiltré. Pas de substitution d'un sigma HH. Les données de calibration
partagent les mêmes prises ; corrélation, mouvement, non-linéarité RCD et
signal résiduel limitent leur interprétation. Exclure dérive globale Y>2 %,
noise non mesurable ou incohérence caméra/ISO/dimensions ; tracer les raisons.

Mesures symétriques A/B : écart-type de (F(A)−F(B))/sqrt2, perte croisée
0,5*moyenne((F(A)−B)²+(F(B)−A)²), dérive moyenne des plans, énergie haute
fréquence partagée (produit des deux images). La dernière utilise x−Gbinomial(x)
sur le centre31, puis exclut un pixel de bord pour éviter le wrap du filtre.
Elle n'est interprétée en ratio que si l'énergie croisée d'entrée est positive.
Ces mesures ne constituent pas une MSE propre, une probabilité de détail ou
une vérité de bruit marginal après un développement non linéaire.

Critères descriptifs fixés avant calcul, sans promotion automatique :

- Ratio de bruit différentiel≤0,8 sur les zones supposées calmes.
- Perte croisée≤1,05 fois celle de production50/50.
- Dérive moyenne absolue≤1 % de max(|Y moyen de zone|,1e-6) par plan.
- Ratio d'énergie haute fréquence croisée≥0,9 lorsque définissable.

Pas de validation perceptuelle par ces seuils ; examiner ensemble par ISO,
zone et plan. Les labels de zones ne garantissent pas un contenu naturel
riche. Les traitements scalaires n'incluent pas encore le raccordement RGB
ni les opérations de compression chromatique du chemin complet.

```powershell
python tests/study_noise_real_bounded.py --data-version 1
```

Preuve `etude-reelle-bornee-nonlocal-bruit-vNNN-*`, binaire/sources/RAW
empreintés, sélection parent canonique, temps complets et locaux séparés.
Un processus séquentiel,5 GiB disponibles minimum avant calcul, OpenCV4
threads et BLAS/OMP/MKL1. Timeout natif1500 s, job1800 s. Aucune comparaison
directe de latence image entière/mini-crop ni extrapolation publiée.
Tests : calibration marginale, supports, constantes, entrée immuable,
métriques identité et refus d'entrée invalide. Aucun code de production,
nettoyage d'images ou publication ; vérifier couverture avant une conclusion.

## 69. Audit de couverture photographique, sans nouveau candidat — 25 octobre 2026

Après §68, les paires confirment un lissage accru mais ne suffisent pas à
valider la fidélité. Fixer trois photos existantes R6 III :6V1A9426 ISO1000,
6V1A9890 ISO25600,6V1A0112 ISO64000. Reprendre les chemins et empreintes de
la campagne spectrale canonique ; lecture seule, pas de déplacement ou copie.
Refuser fichier absent, ambigu ou modifié. Pas de nouvelle prise demandée.

Mode natif `--bounded-real-scenes` : recette neutre actuelle RCD+FlatOnly,
référence production50/50 sur image entière, sélection existante de la première
zone sombre/plate/détaillée si disponible. Échantillons linéaires64 avec centre31,
aucun TIFF ni NLM/pilote appliqué. Conserver métadonnées, déclarations, empreintes,
positions et pixels dans JSON. Le label automatique ne certifie pas le contenu.

Mesurer moyenne, écart-type, énergie x−Gbinomial(x) et dérive moyenne par plan.
Calculer le filtrage sur64 puis mesurer au centre31, pas sur une frontière
artificielle. Sans vérité propre, signal/bruit ne sont pas séparés ; aucune
MSE de restauration ni fidélité perceptuelle déduite de ces seules valeurs.

Depuis les échantillons des paires déjà conservés, mesurer aussi :
`moyenne(hA*hB)/(0,5*moyenne(hA²+hB²))`. Garder les signes et cas non définis,
sans appeler ce ratio probabilité de signal ou pureté du bruit. Centre29 pour
harmoniser avec le précédent indicateur croisé. Aucun redécodage des paires.

```powershell
python tests/study_noise_photo_coverage.py --data-version 1
```

Preuve `audit-couverture-photos-bruit-vNNN-*`, sources/binaire/parents empreintés.
Tests : image inchangée, constante sans division artificielle, entrées invalides.
4 threads OpenCV, BLAS/OMP/MKL1,5 GiB libres minimum, extraction600 s/job900 s.
Audit de corpus uniquement ; aucune nouvelle force, sélection de candidat,
optimisation, validation RAW complète ou modification de production.

## 70. Barrière méthodologique avant une reprise — 26 octobre 2026

Après les audits photo et critique, ne pas lancer de nouvelle variante sans
la fiche suivante dans le rapport consolidé : précédent le plus proche,
hypothèse réellement nouvelle, paramètres figés, comparateur nommé, preuve
existante réutilisable, résultat manquant et condition d'arrêt. Les huit
campagnes tardives NumPy64 ne sont pas huit validations indépendantes.
La répétition d'une référence à des fins de reproductibilité est distincte
d'une preuve nouvelle de qualité. Aucun enchaînement automatique reconduit
depuis l'autorisation temporaire du25 octobre.

Contrôles obligatoires pour la prochaine sélection de zones :

- Qualifier le support effectivement mesuré, y compris son centre, pas seulement
  le parent256 d'un extrait64. Un centre31 constant peut être entouré d'un contour.
- Examiner Y,Co,Cg : une luminance constante peut porter un détail chromatique.
  Un centre constant dans les trois plans est inapte à mesurer la fidélité de détail.
- Distinguer étiquette de sélection, contenu vérifié, bruit et niveau de signal ;
  ne pas transmettre automatiquement la classe de luminance du parent.
- Conserver explicitement les classes utiles comme points lumineux, ou justifier
  leur exclusion. La sélection doit rester fixée avant traitement des candidats.
- Ne pas substituer un indicateur de haute fréquence à une vérité propre ;
  garder les cas non définis et une vérification de stabilité du dénominateur.

Figer aussi la référence (production50/50 ou E-P, pas un échange silencieux),
le niveau de bruit résiduel ou le compromis comparé, le pipeline d'ouverture
et le domaine des canaux. H de NLM et curseur de production ne sont pas une
force perceptuelle commune. Séparer calibration, choix de variante et confirmation.
Énoncer les restrictions de la calibration sur D partageant les prises et les
conditions de réutilisation des preuves. Jamais d'acceptation par seule MSE globale.

Outil disponible pour les preuves tardives déjà sauvegardées :

```powershell
python tests/audit_noise_research.py --data-version N
```

Il ne décode pas de RAW et ne relance pas les filtres. JSON
`audit-methodologique-bruit-vNNN-*`, alias de mesures strictement limités à la
même banque NumPy64, empreintes parent et identités de décomposition vérifiées.
Trois tests couvrent répétition/alias, divergence numérique, terme croisé et
centre constant distinct d'une variation chromatique. Une cohérence numérique
ne prouve pas l'absence de biais partagé. Les verdicts négatifs restent locaux
aux variantes testées, sans fermeture des modèles multicanaux, patchwise,
collaboratifs, mosaïque ou appris qui n'ont pas été comparés dans ce banc.

## 71. Contrat spatial des ROI sur pixels conservés — 26 octobre 2026

Avant de redécoder les sources, utiliser les neuf extraits originaux64×64
conservés dans l'audit de couverture photo. Aucun accès aux sorties de filtres
pour choisir les zones. Conserver le centre historique `(17,17,31,31)` pour
diagnostic, puis examiner toutes les positions compatibles avec le support.

Pour le NLM et son pilote actuellement décrits : centre31, recherche de rayon10,
patch de rayon2, pilote de rayon1, soit halo13. Dans64 pixels, les origines
admissibles sont13 à20 inclus par axe :64 fenêtres. Ne pas réduire le halo,
la résolution ou le seuil pour faire entrer une zone. Ce contrat spatial ne
garantit pas l'indépendance de la calibration du bruit.

Rejeter pour la fidélité de détail un centre exactement constant dans Y,Co,Cg.
Préserver une variation chromatique même si Y est constant. Qualifier le niveau
du centre indépendamment de son étiquette héritée ; non-constance et gradient
ne séparent pas bruit et signal. Proposer au maximum deux centres distincts,
classés séparément selon gradient Y et gradient chromatique après binomial3 fixe.
Ces propositions nécessitent une revue visuelle des originaux avant gel des ROI ;
le maximum de gradient n'est pas une sélection neutre ni une vérité propre.

```powershell
python -m unittest discover -s tests -p 'test_study_noise_roi_contract.py'
python tests/study_noise_roi_contract.py --data-version N
```

JSON exclusif `etude-contrat-roi-bruit-vNNN-*` : parent et pixels empreintés,
support déclaré, coordonnées relatives et absolues, motifs d'inadmissibilité,
étiquettes et propositions. Quatre tests : halo/géométrie, périphérie variable
avec tous les centres constants, variation chromatique sans mutation, entrées
invalides. Ne pas confondre ce diagnostic avec une correction du sélecteur natif.

Si aucun centre n'est admissible, élargir/rechoisir la ROI dans le RAW existant ;
aucune nouvelle photo ne découle automatiquement de ce constat. Vérifier ensuite
la couverture des aplats, détails et points lumineux sur les sources. Figer les
comparateurs et lots distincts avant tout résultat de reconstruction supplémentaire.

## 72. Reprise du seul parent manquant — 26 octobre 2026

`tests/study_noise_roi_parent.py --data-version N` récupère le parent256 du
détail9426 depuis le RAW déjà empreinté dans l'audit photo. Il compile la sonde,
exige5 Gio de mémoire disponible, puis utilise `--original-roi-parent` pour une
seule ouverture neutre RCD/FlatOnly, sans estimation ni débruitage de référence.
Le rectangle est déduit du support antérieur : origine64 moins96 sur chaque axe.
Conserver tous les pixels originaux256 dans le JSON, pas seulement les propositions.

Refuser tout changement de source/parent/binaire pendant l'étude et toute différence,
même faible, entre le centre64 récupéré et les pixels historiques. Ensuite seulement,
appliquer le classement du§71 avec son halo inchangé. Les propositions ne valident
ni détail propre, ni couverture plein cadre, ni points lumineux isolés.

Trois tests complémentaires couvrent le rectangle absolu, le recouvrement exact
sans mutation et les données invalides. Exécuter avec les quatre tests ROI :

```powershell
python -m unittest discover -s tests -p 'test_study_noise_roi_*.py'
```

Lancer via le gestionnaire de tâches indépendant, timeout1100 s ; compilation
limitée à480 s et extraction360 s. À l'arrêt mémoire, conserver la trace d'exécution
sans la présenter comme mesure image ; ne pas réduire le seuil ni tuer une
application utilisateur. Reprendre sous un nouveau nom de job après libération de
mémoire, sans redemander des prises et sans relancer les anciens débruiteurs.

## 73. Qualification descriptive du contenu conservé — 26 octobre 2026

`python tests/study_noise_roi_content.py --data-version N` réutilise uniquement
les neuf centres photo historiques et les deux propositions du parent9426.
Aucune nouvelle recherche, ouverture RAW ou sortie de filtre pour choisir les zones.
Binomial3 fixe, tenseur des gradients par plan : anisotropie
`sqrt((Jxx-Jyy)^2+4*Jxy^2)/(Jxx+Jyy)`, indéfinie si trace nulle.
Décrire aussi concentration de l'énergie dans les10 % de pixels aux gradients
les plus forts, moments des plans et énergie du résidu au lissage. Ces valeurs
ne constituent ni estimation de sigma, ni probabilité de bruit, ni fidélité au signal.

Conserver le recouvrement des deux propositions : ne pas compter deux validations.
Planche par plan original Y/Co/Cg, échelle linéaire commune entre centres, pas
d'égalisation par vignette. Ces diagnostics ne sont pas des rendus RGB.
JSON canonique `qualification-roi-bruit-vNNN-*`, parents et planches empreintés ;
planches dérivées sous `build-patch_noise/roi-content`. Qualification visuelle
à consigner séparément avant gel des ROI ; aucun classement propre/bruité automatique.
Trois tests complémentaires ; dix tests ROI au total. Job détaché, timeout180 s,
reprise à notification de fin, sans attente active.

## 74. Repérage sémantique sur aperçus intégrés — 26 octobre 2026

`python tests/study_noise_context.py --data-version N` reprend les douze scènes
naturelles de l'étude spectrale historique. Vérifier leurs empreintes et extraire
les aperçus intégrés avec l'outil existant `preview_noise_sources`, sans dématriçage
ni filtrage. Sources manquantes ou modifiées explicitement tracées, pas substituées.
Conserver chemins, dimensions de l'aperçu, empreintes des sources/vues et parent
dans `reperage-contexte-bruit-vNNN-*`. Vues dérivées sous `build-patch_noise/context`.

Utiliser planche et vues individuelles pour identifier des objets/structures,
pas mesurer un niveau de bruit. Le JPEG boîtier peut être corrigé, accentué et
débruité ; sa géométrie peut différer du master. Valider orientation, recadrage
et dimensions utiles avant toute conversion de coordonnées et extraction RAW.
Qualifier ensuite le contenu à pleine résolution avec son voisinage, puis geler
les ROI indépendamment des sorties de filtres. Les scènes historiques restent
diagnostiques ; définir séparément le lot réservé de confirmation.

Job détaché limité à240 s, aucun téléchargement, aucun TIFF de mesure ni écriture
dans les dossiers source. Ne pas multiplier les descripteurs sur des carrés sans
contenu identifié quand une vue de contexte apporte l'information manquante.

## 75. Supports sémantiques sur masters neutres — 26 octobre 2026

`python tests/study_noise_semantic_samples.py --data-version N` ouvre0513,9890,
9426 et0195 séquentiellement, sources empreintées comme au§74. Dix ancres
approximatives de contenu fixées avant rendu ; mode de sonde `--original-context`,
ni estimation ni réduction de bruit. Conserver les pixels Y/Co/Cg des carrés256,
leurs rectangles dans le master, dimensions, recettes et une vue de contexte
produite depuis ce même master. Les coordonnées ne proviennent pas d'une
transformation supposée exacte de l'aperçu JPEG.

Refuser orientation/rapport d'aspect incompatible (écart relatif>2 %), géométrie
hors image et pixels invalides. Un rapport d'aspect compatible n'est pas un
recalage : vérifier visuellement placement et contenu avant gel. Les ancres
ne garantissent pas la présence de cheveux fins, lettres ou étoiles dans le256.
Vue Y à transfert fixe commun, contexte diagnostique non référence couleur.

JSON `supports-semantiques-bruit-vNNN-*`, supports originaux conservés ; images
dérivées sous `build-patch_noise/semantic-supports`. Trois tests couvrent géométrie,
non-mutation et données invalides. Exiger5 Gio avant chaque décodage, quatre threads,
timeout240 s par source, compilation480 s, job1600 s ; aucune attente active.
Après revue seulement, geler les ROI et préparer une comparaison discriminante,
en distinguant ces scènes diagnostiques d'un véritable lot de confirmation.

## 76. Comparaison bornée sur signaux naturels de substitution — 26 octobre 2026

`python tests/study_noise_semantic_comparison.py --data-version N` compare les
deux NLM déjà étudiés, scalaire et guide binomial, sur quatre supports fixés :
0513 roche/tissu et9890 cheveux/lettres. Pas de nouveau RAW ou estimateur.
Original bruité comme signal de substitution, injection sigma0,01 par plan,
deux covariances connues (blanc et binomial3 corrélé), deux graines ;48 observations.
Cette substitution ne donne pas de vérité propre ni d'erreur de débruitage réelle.

Conserver recherche21, patch5, covariance et correction de distance historiques,
poids propre maximum voisin. Rejeu testé des deux opérateurs sur64/centre31 à1e-12,
puis même opérateur sur256/cœur230 avec halo13. Calibration des forces séparée :
aplat graine26012601, scan0,02/0,1/0,4/0,8/2, refus si non monotone ou sans plage
commune. Cible milieu géométrique des écarts-types résiduels accessibles aux deux ;
dix dichotomies log, tolérance2 %. Contrôle graine26012602 sans réajustement.
Un appariement sur un aplat ne garantit pas le même bruit résiduel sur les textures.

Conserver par plan/contenu/tirage MSE de substitution, distorsion du signal,
bruit injecté résiduel et terme croisé. Vérifier l'identité de décomposition ;
ne pas masquer une perte en Y par une médiane des trois plans. Mesures normalisées
par sigma injecté, pas en unités perceptuelles ni comme profil de bruit caméra.
Les hypothèses gaussiennes ne valident pas un comportement sur bruit RCD réel.

JSON `comparaison-supports-semantiques-bruit-vNNN-*`, forces, graines, parents et
code empreintés ; cache de sorties NPZ dérivé sous `build-patch_noise/semantic-comparison`.
Trois tests (rejeu scalaire/pilote, constantes/support, décomposition/erreurs).
Job détaché limité à2400 s, OMP/BLAS/MKL1 ; reprise à notification, pas d'attente active.
Ne pas annoncer une comparaison contre E-P, production ou LR, absents de ce banc.

## 77. Poids de correspondance partagés Y/Co/Cg — 26 octobre 2026

`python tests/study_noise_semantic_joint.py --data-version N` isole la distance
moyenne sur les trois plans normalisés et des poids partagés, sans pilote.
Précédent architectural : B natif `blocks`. Pas son rejeu exact : conserver ici
les réglages du§76 (recherche21, patch5, correction de covariance, poids propre
maximum voisin, pas de pondération spatiale), plutôt que mêler plusieurs différences.

Réutiliser supports et observations du§76, métriques scalaire/pilote copiées des
JSON parents, pas recalculées. Bruit injecté indépendant entre plans à même sigma :
hypothèse expérimentale, pas bruit couleur RCD. Même reconstruction de pixels
originaux, cœur230 et décomposition complète par plan.

Cible fixée au bruit résiduel précédent, calibration multicanale sur aplats
distincts des photos. Critère : RMS des écarts-types résiduels des trois plans.
Scan et dichotomie du§76 ; contrôle séparé par plan et agrégé. Cible inaccessible,
scan non monotone ou tolérance manquée : abstention explicite sans déplacer cible
ou plage de force. Ne pas en déduire un rejet des modèles multicanaux en général.

JSON `comparaison-multicanal-semantique-bruit-vNNN-*`, références, code et cibles
empreintés ; sorties NPZ dérivées sous `build-patch_noise/semantic-comparison`.
Quatre tests : identité au scalaire si plans identiques, permutation/non-mutation,
entrées invalides, abstention sans changement de cible. Job détaché2400 s,
BLAS/OMP/MKL1, aucun nouveau RAW ni traitement de production.

## 78. Instrumentation du soutien, avant cascade ou nouvelle variante — 26 octobre 2026

`python tests/study_noise_weight_support.py --data-version N` rejoue les48
observations scalaire/conjoint, mêmes forces et graines des§76–77, plus leurs
aplats de contrôle existants. Pas de recalibration, nouveau candidat ou RAW.
Mesurer le nombre effectif de poids `(sum w)^2/sum(w^2)`, la part du pixel
central, le déplacement RMS et les cas de repli identité. Inclure le poids
propre maximal dans les normalisations ; quantiles et bornes sur le cœur230.

L'instrumentation optionnelle est testée sans aucun changement de sortie.
Exiger reproduction des métriques parents à1e-10 avant interprétation.
Conserver la provenance comme rejeu diagnostique, pas nouvelle validation.
Ne pas interpréter `1/N_eff` comme variance indépendante quand les poids sont
adaptatifs ou le bruit corrélé ; cartes conjointes partagées, pas trois répétitions.

JSON `audit-soutien-poids-bruit-vNNN-*`, parents et outils empreintés, mesures
de soutien et références recopiées. Job détaché2400 s, reprise à notification.
Une cascade ou un réseau reste une étude distincte : définir les données
d'apprentissage/confirmation et le critère de dégradation avant d'ajouter une
porte « ignorer ». Une vérité propre de laboratoire ne sera pas disponible
sur la photo utilisateur ; aucune porte oracle ne doit être annoncée déployable.

Robustesse numérique du diagnostic : accumuler `sum((w/max(w))^2)` avec mise
à l'échelle au fil des décalages. Calculer `N_eff` avec la somme relative au même
maximum ; inclure le poids propre. Ne pas calculer directement les carrés de
poids minuscules ni ajouter un epsilon qui inventerait un soutien. Le sous-flux
des carrés n'est pas le repli identité du filtre. Tester l'invariance entre
échelles1 et1e-200, en plus de l'identité bit à bit des sorties instrumentées.
En cas d'arrêt du diagnostic, garder sa trace et ne pas le présenter comme un
échec photographique ; nouvelle relance avec nom de job distinct, preuves inchangées.

## 79. Agrégation uniforme des estimations de patchs recouvrants — 26 octobre 2026

`python tests/study_noise_patch_aggregation.py --data-version N` conserve distance
conjointe, patch5, recherche21 et poids propre max, mais normalise chaque estimation
de patch puis agrège les25 estimations couvrant un pixel. Ce n'est pas une
transformée collaborative BM3D ni une cascade entraînée. Précédent `blocks`
centre-seul identifié ; limiter le changement à la reconstruction et sa force
calibrée hors photos. Ne pas réimplémenter les précédents sous un nom nouveau.

Halo14, cœur228 : le centre-seul est recalculé sur ce même support, jamais
comparé aux anciennes métriques230. Cible d'aplat inchangée, scan/dichotomie et
contrôle distinct des§76–77. Si cible inaccessible, abstention sans changer h
ou inventer un résultat photo. Pixels d'origine toujours bruités, vérité de
substitution uniquement ; aucune validation RAW réelle déduite du banc gaussien.

Trois tests : limite rayon0 identique au centre-seul sur support commun,
constantes/contexte/non-mutation, permutation/erreurs. Cache temporaire d'environ
200 Mio pour les poids, libéré à chaque reconstruction. JSON
`comparaison-agregation-patchs-bruit-vNNN-*`, parents et code empreintés ; job
détaché2400 s. Mêmes supports/graines, pas de nouveau RAW, force ou réseau appris
sur les photos. Rapports par plan/contenu, décomposition complète, abstentions tracées.

## 80. Trois méthodes au même point de calibration — 26 octobre 2026

`python tests/study_noise_common_operating_point.py --data-version N` compare
scalaire, conjoint centre et agrégation sur le cœur228, quatre supports fixés.
Trois aplats de calibration26012601 identiques par méthode, critère RMS des
écarts-types spatiaux par plan, contrôle26012602 séparé et rapporté par plan.
Scan0,02/0,1/0,4/0,8/2 et dix dichotomies log, tolérance2 % ; pas de réglage photo.

Blanc : garder la cible historique0,094241. Corrélé : milieu géométrique de
l'intersection des plages accessibles aux trois méthodes, choisi hors photos.
Ce point est distinct de l'ancien objectif0,226892. Abstention si intersection
vide, scan non monotone ou appariement manqué. Ne pas confondre appariement sur
aplats et bruit résiduel sur structures. Réutiliser les scans d'agrégation seulement
si empreinte du script et contrat d'entrée/support inchangés ; tracer la réutilisation.

JSON `comparaison-point-commun-bruit-vNNN-*`, parents/code, forces, contrôles,
mesures par support/plan/tirage et décomposition conservés. Trois tests couvrent
cibles, abstentions et même support/constantes/non-mutation. Job détaché2400 s,
aucun nouveau RAW ni apprentissage. Ne pas accepter une moyenne globale qui
masquerait une perte chromatique ou remplacer les références propres par les
originaux bruités de substitution pour prétendre valider la production.

## 81. Formes connues sur résidus empiriques conservés — 26 octobre 2026

`python tests/study_noise_empirical_forms.py --data-version N` utilise une ROI64
historiquement « flat » de chacune des quatre paires RCD ISO100/1600/6400/51200
déjà sauvées. D=(A−B)/sqrt2, normalisation par plan sur l'extérieur du rectangle
12:52 ; conserver moyennes/sigma, dépendance interplans et contrôle des niveaux
centraux. Floors de distance par décalage sur couples extérieurs valides128 min,
individuels au scalaire et moyennés au conjoint.

Calibrer une seule force par méthode/ISO sans forme injectée sur l'intersection
accessible des scans usuels, cible milieu géométrique, dix dichotomies/tolérance2 %.
Le même résidu sert au filtrage : diagnostic conditionnel, pas calibration
indépendante ou méthode aveugle de production. Abstentions/exclusions explicites.
Pas de faux tirages indépendants par signe, rotation ou pavage.

Injecter une gaussienne sigma1,5, une marche signée, des stries période6 sous
gaussienne sigma5, dans un seul plan à la fois, amplitudes3/6 sigma **marginal**.
18 cas/ISO, cœur36 commun/halo14. Décomposition et projections par plan,
pas seulement MSE moyenne. Projection de signal filtré à poids fixés n'est ni
pic restauré ni critère de sécurité disponible sans vérité propre.

JSON `formes-residus-reels-bruit-vNNN-*`, parent et code empreintés. Trois tests
couvrent normalisation, formes exactes et support/entrées. Job détaché2400 s,
pas de nouveau RAW ou réseau. Différences de paires potentiellement contaminées,
non-stationnarité et absence d'indépendance à conserver dans tout verdict.
Ne pas confondre un gain relatif et la satisfaction du critère petites formes.

## 82. Contributions réelles au centre des formes connues — 26 octobre 2026

`python tests/study_noise_localized_weights.py --data-version N` reprend36 cas
du§81 :24 gaussiennes et12 marches à6 sigma, mêmes résidus, forces et modèles.
Ni nouvelle calibration ni oracle de poids propres, déjà étudié historiquement.
Au pixel32,32, tracer441 coefficients de pixels source. Agrégation : moyenne
des25 estimations de patch **normalisées séparément** ; conserver leurs effets,
pas une approximation par distance moyenne.

Reproduire sortie/signal centraux et MSE parent à1e-10, poids sommant à1.
Stocker positions, coefficients, pic de signal reconstruit, poids vers sources
<10 % du pic connu, signe opposé, rayon≤3 et fraction de couples sous le floor.
Distinguer pic/projection, fréquence de couples/masse de poids et diagnostic
avec vérité connue/porte réellement disponible sur photo inconnue.

JSON `audit-contributions-formes-bruit-vNNN-*`, parents et code empreintés.
Deux tests : reproduction des trois opérateurs ; poids uniformes/entrées.
Job détaché2400 s. Ce rejeu explique un mécanisme ; aucune qualité indépendante
ou optimalité de correctif déduite, aucune nouvelle photo/RAW ou couche apprise.

## 83. Correspondance par contraste centre/couronne — 26 octobre 2026

`python tests/study_noise_contrast_matching.py --data-version N` réutilise les
quatre résidus du§81. Descripteur : moyenne3×3 moins couronne7×7 hors3×3.
Échelles par plan et floors par décalage sur supports entièrement extérieurs
à la garde centrale ; au moins64 couples valides par décalage. Distance retenue :
max(distance patch corrigée positive, distance descripteur corrigée positive),
sans coefficient appris, réinjection centrale ni accès à la forme vraie.

Même agrégation normalisée par patch. Cœur34×34/halo15 pour les trois méthodes,
scalaire et conjoint recadrés ; recalibrer toutes les forces au même RMS global
sur bruit seul, dix dichotomies, tolérance2 %. Tracer aussi bruit par plan,
quantile absolu99,9 % et maximum : RMS global égal ne garantit ni même bruit par
plan ni absence d'accidents. Calibration conditionnelle sur le même champ,
pas contrôle indépendant ni borne de faux positifs.

72 formes précédentes plus24 gaussiennes prédéfinies à6 sigma, largeurs1/2,5,
centre(32,5;31,5). Mesures par cas et plan, décomposition MSE et projections ;
les extensions ne sont pas des réalisations indépendantes. Ne pas comparer
directement aux anciens résultats sur cœur36/forces différentes.

JSON `correspondance-contraste-bruit-vNNN-*`, parents et code empreintés.
Trois tests couvrent plans affines, modèles/supports/constantes/non-mutation,
entrées invalides. Lancer en tâche détachée puis rendre la main, sans attente
active. Refuser une protection des formes qui conserve aussi les accidents ;
aucune adoption de production sur une seule moyenne globale.

## 84. Détectabilité du descripteur, seuils séparés par paire — 26 octobre 2026

`python tests/study_noise_contrast_detectability.py --data-version N` reprend
les quatre résidus du§81, sans filtrage. Réserver chaque paire successivement ;
calibrer échelle par plan et seuils sur les trois autres,243 positions.
Comparer pixel brut et contraste3/couronne7 : absolu sur plan fixé, ou maximum
absolu sur trois plans. Seuils95/99 % empiriques `higher`, dépassement strict.

81 centres espacés de7 sur chaque champ réservé. Injecter séparément aux mêmes
centres les gaussiennes largeur1/1,5/2,5, phase0/0,5 sur x et y ensemble,
amplitudes3/6 sigma marginal et signes± :288 cas, pas des tirages indépendants.
Calcul analytique de réponse linéaire testé face à une vraie injection.
Conserver scores, seuils, dépassements réservés sur bruit seul, détection et
nouveaux dépassements. Ne pas ajuster le seuil sur la paire réservée.

JSON `detectabilite-contraste-bruit-vNNN-*`, provenance/empreintes/coordonnées.
Trois tests : réponse analytique, supports/recherche de canal/non-mutation,
contrat d'entraînement/entrées invalides. Tâche détachée, timeout2400 s.
Les paires étaient déjà explorées et les ISO diffèrent : diagnostic de transfert,
pas nouvelle validation aveugle au même ISO. Supports non superposés ne veut pas
dire indépendants. Normalisation extérieure conditionnelle, scène/mouvement
possibles, centre connu, pas taux de fausse alarme global sur image.

## 85. Audit détection et transport des sources — 26 octobre 2026

`python tests/study_noise_detection_transport.py --data-version N` réutilise
les24 gaussiennes et coefficients centraux du§82. Même force/opérateur ; rejouer
sortie et signal centraux à1e-10 sans recalculer les images filtrées.
Reprendre seuils pixel95/99 % et échelles du§84, réservés par paire.

Détecteur plan fixé : seuil absolu et même signe que le centre. Détecteur max3 :
seuil du maximum, même canal dominant et signe que le centre. Tracer statut de
détection centrale et canal choisi ; si le centre n'est pas détecté, ne pas
présenter la compatibilité comme une porte déclenchée. Mesurer masse/compte de
sources compatibles, poids propre, part provenant du fond connu<10 % du pic et
contribution signal compatible. Vérité utilisée pour les étiquettes seulement.

Pas de poids restreints renormalisés, réinjection d'innovation ou nouveau filtre.
Ces familles ont déjà été explorées ; l'audit interroge leur précondition au lieu
de les refaire. Contrôle central bruit seul à modèles/forces inchangés :36 lignes
ne représentent que4 champs réutilisés. Pas de taux global de faux positifs.

JSON `audit-detection-transport-bruit-vNNN-*`, parents/code empreintés, décisions
et indices source conservés. Trois tests : canal/signe, masses, entrées invalides.
Lancer détaché avec timeout2400 s et rendre la main sans attente active.
Séparer détection, compatibilité et fidélité de reconstruction ; aucune qualité
nouvelle ou indépendance supposée à partir des mêmes réalisations.

## 86. Soutien spatial connexe des sources détectées — 26 octobre 2026

`python tests/study_noise_connected_support.py --data-version N` réutilise les
24 cas, poids et masques compatibles du§85. Vérifier réponse signal et masses
parentes, sans filtrage des formes. Composante contenant le centre, connexité4
puis8 ; centre absent implique composante vide. Pas de dilatation, remplissage,
seuil de taille optimisé, nouvelle calibration ou renormalisation des poids.

Conserver indices, masse restante, contribution de signal, contamination du
fond connu<10 % du pic et soutien effectif des poids. Vérité pour diagnostic
seulement. Analyser Co ISO100/51200 à6 sigma sans exclure les autres cas.
Contrôle bruit seul :144 fenêtres21×21 sur les quatre mêmes champs,36 centres
par champ sur grille17:52 au pas7. Fenêtres superposées, pas de nouveaux tirages
indépendants. Tailles/composantes sauvées sans en déduire un taux global validé.

JSON `audit-support-connexe-bruit-vNNN-*`, parents/code empreintés. Trois tests :
connexité/diagonales/îlots/centre absent, masse et soutien effectif/non-mutation,
entrées invalides. Job détaché2400 s ; pas d'attente active. Réduire le fond au
prix de ne garder que le pixel bruité n'est pas une solution démontrée ; aucun
gain de reconstruction n'est attribué à cet audit morphologique.

## 87. Reconstruction sur composante connexe, coût de bruit explicite — 26 octobre 2026

`python tests/study_noise_connected_reconstruction.py --data-version N` reprend
le conjoint agrégé du§81 à forces/floors inchangés, cœur36. Cartes441 coefficients
exactes ; rejouer la MSE des72 cas parents à1e-10. Détecteur pixel max3 seuil99 %
réservé par paire du§84 ; composante4-connexe du même canal dominant/signe dans21×21.
Centre non détecté ou soutien effectif<2 : abstention, sortie du comparateur.
2 est une heuristique structurelle fixée, pas une confiance statistique.

Aux pixels acceptés, C est la moyenne pondérée du groupe renormalisée et m sa
masse de poids d'origine : variantes C et(1−m)B+mC, B comparateur. Même sélection
observée et coefficients sur les trois plans et sur la composante signal connue
pour décomposer la MSE ; aucune vérité ne sélectionne les sources. m n'est pas
une probabilité, le mélange peut conserver des accidents. Pas d'ajustement de
taille, de seuil, de h ou de coefficient sur les résultats.

216 cas :72 parents plus144 gaussiennes largeur1/1,5/2,5, centres(x28,y30) ou
(x36,5,y34,5), amplitudes3/6, trois plans et quatre champs. Phase/position
partiellement confondues, mêmes réalisations déjà explorées. Conserver métriques
par plan/cas, projections, activations, masses et soutiens. Sur bruit seul :
MSE/écart-type par plan et extrêmes99,9 %/max sur cœur complet.
Forces initiales égales n'impliquent pas bruit final égal ; expliciter ce coût.

JSON `reconstruction-support-connexe-bruit-vNNN-*`, parents/code empreintés.
Trois tests : comparateur/abstention/non-mutation, constantes/décomposition,
contrats/54 cas par ISO. Job détaché2400 s, pas d'attente active ni changement
de production. Ne pas annoncer une validation indépendante ou photographique.

## 88. Étendue des composantes et attribution exacte de l'erreur — 26 octobre 2026

`python tests/study_noise_component_extent.py --data-version N` reprend les216
cas et quatre contrôles du§87, sans nouvelle sortie candidate. Rejouer activations
et MSE par plan des trois méthodes à1e-10 ; vérifier l'empreinte du reconstructeur.
Recalcul des masques nécessaire car géométrie par pixel absente du JSON parent.

Tracer aire, boîte, rayon, centroïde, masse/soutien, contact aux côtés de la
fenêtre21×21(une source à±10 sur x/y). Par activation et plan, conserver la
variation d'erreur quadratique des deux candidats existants. Sommes/1296 séparées
entre groupes touchants/non touchants : elles doivent reproduire les deltas MSE.
Vérité pour erreur seulement, pas pour géométrie ni porte de décision.

Pas de seuil de taille ajusté ou de simulation d'une porte choisie sur l'erreur.
Contact ne prouve pas structure étendue ; absence ne prouve pas signal.
Examiner séparément bruit seul, marches, stries et gaussiennes, y compris les
deux cas sentinelles. Même corpus, pas nouvelle validation indépendante.
JSON `audit-etendue-composantes-bruit-vNNN-*`, événements/parents/code conservés.
Trois tests : géométrie, budget, opérateur/activations/abstention. Tâche détachée
2400 s, sans attente active ni modification de production.

## 89. Contrôle synthétique du biais après sélection — 26 octobre 2026

`python tests/study_noise_selection_bias.py --data-version N` : sélection sur
A=S+nA seulement, estimation sur S+ρnA+sqrt(1−ρ²)nB, B indépendant de A avant
mélange, ρ0/0,5/1. Pas de guide propre ni sélection avec B. Poids comparateur,
groupe renormalisé et mélange déjà définis, h=0,8 fixé, centre32 uniquement.

Deux covariances connues(blanche/binomiale), trois plans indépendants, sigma1.
Seuil max3 théorique99 % : (2Φ(t)−1)³=0,99. Composante4-connexe même canal/signe,
refus de contact de bord ou soutien effectif<2.4096 plats et36 configurations
gaussiennes×16 par covariance,9344 tirages. Gaussiennes trois plans/largeurs
1/1,5/2,5/phases0/0,5/amplitudes3/6 ; graines/flux distincts et refus conservés.

Risque conditionnel exact par plan : (wS−Scentre+ρwnA)²+(1−ρ²)wᵀCw.
Conserver risque et décomposition, contrôle avec B fini, tailles/masses/poids.
Résultats conditionnels aux centres acceptés, pas MSE d'une image complète.
La répétition indépendante est une information oracle supplémentaire, non un
algorithme sur photo unique ; corrélationρ théorique, pas calibration RCD.

JSON `controle-biais-selection-bruit-vNNN-*`, code/dépendances empreintés.
Trois tests : covariance connue, risque/contrats, signal. Tâche détachée2400 s,
pas d'attente active ni production. Ne pas confondre ce contrôle bruité avec
les anciens oracles à guide propre ou une preuve de validité sur des RAW.

## 90. Faisabilité des scores de risque sur sélection discontinue — 26 octobre 2026

`python tests/study_noise_connected_sure.py --data-version N` :136 nouvelles
observations synthétiques, par covariance32 plats,12 gaussiennes×2 et six
marches×2. Bruit gaussien blanc/binomial, covariance et sigma1 connus, plans
indépendants. Au centre32 : conjoint agrégé h0,8, groupe seul et mélange,
seuil max3 théorique99 %, refus bord/soutien<2. Pas de sélection sur vérité.

Sondes adaptées à la covariance,4/8 imbriquées,ε0,03/0,15 fixés, mêmes sondes
entre méthodes. Recalculer poids ET masques à chaque perturbation. Score par
plan : résidu²−1+2 moyenne[zcentre(f(y+εz)−f(y−εz))/(2ε)]. Conserver négatifs,
termes directionnels, erreur vraie, activations et changements de statut.

Ce sont des scores de différences finies : pas d'absence de biais supposée pour
la connexité/seuil discontinu. Contrôle scalaire du seuillage dur : risque nul
2[tφ(t)+1−Φ(t)] et terme de bord manquant4tφ(t) pour la dérivée presque partout.
Références : [Tibshirani,2015](https://arxiv.org/abs/1402.1920),
[Ramani et al.,2008](https://bigwww.epfl.ch/publications/ramani0803.html).
Vérifier stabilitéε/sondes avant sélecteur. Pas de minimum de risque activé,
ni de nouveau sigma/gain appris.32 plats ne contrôlent pas les rares faux groupes.

JSON `faisabilite-risque-connexe-bruit-vNNN-*`, code/dépendances/références.
Trois tests : frontière scalaire, scores/négatifs, sorties/non-mutation.
Tâche détachée2400 s, pas d'attente active ni modification de production.

## 91. Falsification des scores sur les faux groupes corrélés connus — 26 octobre 2026

`python tests/study_noise_risk_challenge.py --data-version N` : les neuf groupes
plats acceptés du§89, pas nouvelle population de validation. Régénérer par
graines/flux parent et rejouer sorties quadratiques à1e-10. Même reconstructeur
et paramètres du§90, seuils/pas non ajustés après sélection des cas.

Quatre jeux indépendants de sondes par observation, covariance binomiale adaptée,
ε0,03/0,15 et4/8 sondes imbriquées, recalcul complet des poids/masques.
Conserver scores/négatifs/termes directionnels, erreurs et changements d'activation.
Examiner signe du coût face au comparateur sur plan choisi et moyenne3, stabilité
entre jeux de sondes. Pas de sélecteur automatique, de gain ou de correction appris.

Neuf cas×quatre sondages ne sont pas36 images indépendantes ; une falsification
présélectionnée ne mesure ni biais global ni taux de faux positifs. Réussite
éventuelle non assimilée à une validation. JSON `epreuve-risque-faux-groupes-bruit-vNNN-*`,
parents/code/graines conservés. Trois tests : bruit reproductible, sondes distinctes,
garde de rejeu. Tâche détachée2400 s, sans attente active ni production.

## 92. Shrinkage continu du contraste local à covariance connue — 26 octobre 2026

`python tests/study_noise_local_shrinkage.py --data-version N` : patch7×7,
covariance C connue, moyenne GLS μ=(1ᵀC⁻¹y)/(1ᵀC⁻¹1), énergie de contraste
q=(y−μ1)ᵀC⁻¹(y−μ1). Gain positif g=max(0,1−(d−2)/q), g0 àq0 ; sortie
μ+g(ycentre−μ). Par plan d48 ou commun trois plans d144. Pas de masque dur,
sélection de sources, seuil ajusté ou sélecteur de risque.

Référence conceptuelle : [James et Stein,1961](https://www.stat.yale.edu/~hz68/619/Stein-1961.pdf).
Ne pas transférer une propriété de risque vectoriel blanchi au risque central,
au rendu couleur ou à une image de patches recouvrants. Gain continu mais à
coude de dérivée ; poids GLS signés, artefacts possibles. Conditionnement et
poids conservés, pas de régularisation cachée.

Comparateurs identité/GLS/conjoint agrégé h0,8, cœur36.432 formes(54×quatre
tirages×deux covariances),64 nouveaux plats et neuf contre-exemples connus à
part. Covariance blanche/binomiale gaussienne, trois plans indépendants,
sigma1 : pas calibration RCD. MSE/décomposition par plan, projections, bruit
et extrêmes conservés ; gains observés utilisés aussi pour transporter le signal.
Forces fixées ne signifient pas bruit final égal. Aucun réglage après lecture.

JSON `shrinkage-local-continu-bruit-vNNN-*`, code/graines/provenance et modèles.
Trois tests : rang/covariance, gain continu, constantes/décomposition/non-mutation.
Tâche détachée2400 s, pas d'attente active ni modification de production.

## 93. Prototype de reconstruction collaborative de faible rang — 26 octobre 2026

`python tests/study_noise_collaborative_rank.py --data-version N` : matrice75×17
de patches5×5×trois plans, référence incluse. Groupe similaire :16 voisins au
SSD brut joint dans21×21, tri stable ; contrôle fixe : grille4×4 des décalages
−6/−2/+2/+6 plus référence. Pas de vérité pour sélectionner.

Conserver le patch moyen de groupe, blanchir le résidu par covariance spatiale
connue, SVD et soft-threshold du spectre, inverse et ajout de la moyenne.
Seuil : quantile95 % `higher` de32 normes spectrales de groupes extraits aux
mêmes positions dans32 champs indépendants ; mêmes centrage/blanchiment,
recouvrement spatial inclus. Ne pas supposer colonnes iid ou calibration
conditionnelle à la sélection sur bruit. Pas de réglage taille/seuil après lecture.

Transport du signal par projection/gains observés pour décomposer l'erreur.
Mesure au pixel central seulement, comparateurs identité/conjoint h0,8.
216 formes,64 plats, neuf épreuves connues séparées, graines conservées.
Spectres/indices/rangs/calibrations et erreurs par plan sauvegardés.
Pas MSE d'une image complète ni bruit final apparié. Bruit gaussien connu,
plans indépendants, pas calibration RCD.

Direction issue de [BM3D](https://doi.org/10.1109/TIP.2007.901238) et
[WNNM](https://openaccess.thecvf.com/content_cvpr_2014/html/Gu_Weighted_Nuclear_Norm_2014_CVPR_paper.html),
sans reproduction de ces algorithmes ou revendication de leur performance.
JSON `prototype-rang-collaboratif-bruit-vNNN-*`, code/provenance/graines.
Trois tests : blanchiment, seuil0/identité/décomposition, constantes/groupes.
Tâche détachée2400 s, pas d'attente active ni production.

## 94. Reconstruction collaborative Wiener à deux étapes — 26 octobre 2026

`python tests/study_noise_collaborative_wiener.py --data-version N` : pilote
conjoint agrégé h0,8 issu de la même observation, support valide14:50. Patches
5×5,17 voisins au SSD joint dans9×9, groupes choisis sur brut ou pilote pour
ablation. DCT orthonormale spatiale et selon les17 membres, par plan ; gain
de Wiener `pilot_coeff²/(pilot_coeff²+variance_coeff_brut)` sur **tous** les
coefficients, moyenne comprise. Pas de moyenne de groupe laissée intacte.

Variance à géométrie fixée calculée depuis la covariance stationnaire connue,
avec recouvrement des patches et ordre du groupe. Cela n'élimine pas le biais
du regroupement choisi sur le bruit. Puissance du pilote non corrigée de son
bruit résiduel ; pilote non indépendant. Aucune vérité dans les groupes/gains.

Reconstruire tous les17 patches de chaque groupe, agréger à poids égaux sur
81 références(grille20:43 au pas3 plus43). Mesure commune20:44,24×24, couverture
strictement positive. Comparateurs identité/conjoint h0,8 sur ce même cœur.
Mesures aux pics **échantillonnés** des gaussiennes, coordonnées explicites,
pas point32 fixe. Pas de revendication BM3D complet ou de risque sans biais.

108 formes(un tirage par configuration/covariance),48 aplats à niveaux0/5/20,
huit tirages par niveau/covariance, neuf contre-exemples connus à part. MSE et
décomposition par plan, projections/pics, erreurs extrêmes et biais moyens.
Recherche9 et cœur24 diffèrent des anciens contrats : comparer aux sorties
courantes, pas directement aux anciens scores. Pas d'appariement final du bruit.

JSON `collaboratif-wiener-deux-etapes-bruit-vNNN-*`, code/graines/provenance.
Trois tests : variance transformée face au calcul direct incluant recouvrement,
identité à variance nulle/transport/non-mutation, couverture/champ nul/pic décalé.
Tâche détachée2400 s, pas d'attente active ni production, RAW ou entraînement.

## 95. Audit apparié des offsets du Wiener collaboratif — 26 octobre 2026

`python tests/study_noise_wiener_offsets.py --data-version N` applique le
reconstructeur inchangé du§94 à59 champs(32 plats,12 gaussiennes,six marches,
neuf épreuves connues) aux offsets0/5/20. Même bruit et même forme par triplet,
offset ajouté au signal vrai et à l'observation.177 états liés, pas177 réalisations.
Même cœur24 et métriques d'erreur. Pas de modification de gain ou de nouveau filtre.

Conserver indices ordonnés des groupes, variation des gains nonDC, gains DC,
translation du pilote. Si groupes invariants, prédiction de F(y+a)−a−F(y)
par agrégation de `[g_a(rawDC+a sqrt425)−g_0 rawDC]/sqrt425−a`.
Rejouer cette attribution à1e-7 si variations nonDC<1e-9 ; tracer comme non
valide si groupes changent. Pas de vérité pour produire cette prédiction.

Ne pas assimiler ajout constant et exposition/bruit capteur dépendant du signal.
L'audit localise une dépendance, il ne valide pas de correction DC choisie sur
ces erreurs. JSON `audit-offsets-wiener-apparies-bruit-vNNN-*`, provenance et
graines. Trois tests : formule, invariance sur champ translaté/non-mutation,
instantanés identiques. Tâche détachée2400 s, pas d'attente active ni production.

## 96. Séparation expérimentale du coefficient constant vers le pilote — 26 octobre 2026

`python tests/study_noise_wiener_dc_separation.py --data-version N` remplace
seulement Z00 du§94 par P00(coefficient constant du pilote débruité). Ailleurs,
Zpk=gpkYpk inchangé. Pas de modification groupes/gains/agrégation/h0,8.
Implémentation par correction exacte `(P00−g00Y00)/sqrt425` du parent immuable.
Le signal de la correction vient du pilote transporté à poids observés,
jamais d'un signal propre utilisé comme source DC.

149 champs :108 formes neuves,32 plats neufs et neuf épreuves connues, chacun
aux offsets0/5/20,447 états liés. Cœur24 et pics échantillonnés aux bonnes
positions. Vérifier invariance de translation à1e-7 pour chaque champ avant
interprétation de qualité. Mesures par plan, biais, extrêmes, projections du
signal total distinctes de celles du contraste(avec retrait du niveau ajouté).
Comparateurs actuels identité/conjoint et sorties parentes sur mêmes bruits.

Pas de gain/seuil ajusté. Le pilote peut atténuer le niveau lent et reste
corrélé au bruit ; contrat de translation non assimilé à une qualité validée.
JSON `separation-dc-pilote-wiener-bruit-vNNN-*`, code/parents/graines conservés.
Trois tests : correction DC contre remplacement explicite, translation par plan
et transport/non-mutation, entrées invalides. Job détaché2400 s, pas d'attente
active ni changement de production, RAW ou recette d'ouverture.

## 97. Localisation spatiale du bruit et de l'erreur sur transitions — 26 octobre 2026

`python tests/study_noise_wiener_edge_profiles.py --data-version N` reprend
12 marches/12 stries/32 plats du§96, mêmes graines àoffset0. Rejouer les six
méthodes et leurs quatre termes d'erreur par plan à1e-10. Aucun nouveau filtre.

Frontière de marche x31,5, cœur20:44 : contact distance<2,5(96 pixels),
voisinage2,5≤distance<6,5(192), plus loin≥6,5(288). Profils24 colonnes par plan
et bande : MSE, distorsion signal, bruit, terme croisé et moyennes signées.
Contributions pondérées par aire doivent sommer au budget entier.
Bandes identiques sur plats/stries comme contrôles, pas frontières présumées.

Plus loin n'est pas indépendant du contexte pilote ou de l'agrégation. Vérité
pour diagnostic uniquement ; pas de porte ni gain choisi sur l'erreur.
JSON `profils-spatiaux-transitions-wiener-bruit-vNNN-*`, code/parents/graines
et coordonnées conservés. Trois tests : partition, budgets/rejeu, entrées.
Tâche détachée2400 s, pas d'attente active, nouveau corpus ou production.

## 98. Budget exact du bruit par famille de coefficients — 26 octobre 2026

`python tests/study_noise_wiener_coefficient_budget.py --data-version N`
reprend les24 formes/32 plats du§97, mêmes graines/filtre/offset0. Partition
425coefficients en DC, moyenne spatiale variable, fréquences orientées comme
le contour(uy0,ux>0) partagées/variables, autres fréquences(uy>0) partagées/variables.
Effectifs1/16/4/64/20/320. Orientation pour analyse, pas nouveau filtre.

Pour chaque groupe : indices/référence/patches traversants, énergies signal
et bruit observés, gains et fraction>0,9 descriptive. Prédiction nonDC g²variance
à géométrie fixée ; pas de prédiction DC pilote inventée ou d'indépendance de
sélection supposée. Écart observé/modèle ne démontre pas seul une covariance fausse.

Inverser et agréger les six composantes séparément ; somme doit reproduire le
résidu à1e-10. Matrice Qij=moyenne(n_i n_j) par bande/plan : somme totale=énergie
de bruit, lignes=attributions signées. Pas simple somme d'énergies propres,
ni covariance centrée/probabilités. Contrôle du budget contre parent spatial.

JSON `budget-coefficients-contours-wiener-bruit-vNNN-*`, détails et provenance.
Trois tests : partition, compensations croisées, rejeu nul/masque invalide.
Job détaché2400 s, pas d'attente active ou modification de production.

## 99. Ablation du contexte de recherche collaborative — 26 octobre 2026

`python tests/study_noise_wiener_search_context.py --data-version N --trials 4`
compare9×9/21×21 à groupe17, DC pilote séparé et mêmes81 références sur cœur24.
Champs complets96, pilote valide14:82, cœur36:60, covariance exacte du recouvrement
pour chaque géométrie sélectionnée. Même observation/pilote pour les variantes.
Pas de padding du pilote ou de collage indépendant du bruit corrélé ; nouvelles
graines explicitement distinctes des anciennes réalisations64.

Plats/marches/stries/gaussiennes, amplitudes3/6 et trois plans, quatre répétitions,
deux covariances :152 champs. Positions sélectionnées sauvegardées, budgets
par bande du§97, pics gaussiens mesurés au vrai maximum échantillonné.
Le nombre de contributions d'agrégation change aussi avec la recherche ;
une amélioration ne prouve pas seule un effet causal de phase.

Tests : covariance contre parent, petite recherche identique sur champ central,
translation grande recherche, entrées invalides. JSON immuable
`contexte-recherche-wiener-bruit-vNNN-*`. Attente autorisée pour cette campagne
autonome par l'utilisateur ; aucun changement de production ou publication.
## 100. Transfert du contexte collaboratif — 26 octobre 2026

`python tests/study_noise_wiener_search_context.py --data-version N --trials 8 --suite transfer`
conserve toutes les méthodes du§99. Nouvelles graines2610261200000+indice,
marches horizontales/diagonales et gaussiennes aux centres(44,46)/(52,5,50,5),
largeurs1/2,5, amplitudes3/6, trois plans actifs et plats, deux covariances.
Huit réalisations par configuration :592 champs, pas592 photos indépendantes.
Pics gaussiens au vrai maximum échantillonné dans le cœur commun ; pas au centre
fixe de la fenêtre. Un quatrième test vérifie orientation/pic.

Pas de nouvelle porte de décision, apprentissage, optimisation ou production.

## 101. Diagnostic pilote / regroupement par contrôles oracles — 26 octobre 2026

`python tests/study_noise_wiener_pilot_oracle.py --data-version N` :152 champs
frais, quatre répétitions, gaussiennes1/1,5/2,5 et amplitudes3/6 sur trois plans,
plats, deux covariances. Recherche21/groupe17/DC pilote observé constants.
Contrôles : aucun oracle, groupes vrais, puissances vraies, les deux.
Puissance vraie = carré des coefficients du signal dans le groupe considéré ;
pas une variance conditionnelle estimée ni un risque optimal garanti.

Sortie observée doit rester identique si seul le signal de diagnostic change.
Oracles clairement étiquetés, jamais proposés en production. Conserver MSE,
distorsion, bruit, croisé, pics au vrai maximum et positions de groupes.
JSON `diagnostic-pilote-oracle-wiener-bruit-vNNN-*`, sources et empreintes.
Attente autorisée dans cette campagne ; aucune retouche de production.

## 102. Pilote collaboratif par seuillage — 26 octobre 2026

`python tests/study_noise_wiener_hard_pilot.py --data-version N` :248 champs
frais, gaussiennes1/1,5/2,5, marches/stries3/6 sur trois plans, plats,
quatre répétitions et deux covariances. Groupe17/SSD brut21/DCT fixes.
Pilote14:82 obtenu par576 références valides sur champ96, aucun padding.
Seuil bilatéral gaussien1 % par coefficient à géométrie fixée, DC gardé,
agrégation uniforme. Ce taux n'est ni familial ni garanti après sélection.

Second étage Wiener : référence NLM, puissance seuillée seule, groupes+
puissances seuillés avec DC NLM, puis DC seuillé également. Premier étage seul
mesuré. Budgets/pics/contrôles plats, sources et graines conservés dans
`pilote-seuillage-collaboratif-bruit-vNNN-*`. Trois tests de pilote + cinq tests
de reconstructeur. Aucun oracle dans ces sorties, pas BM3D complet ou production.

## 103. Correction du plancher de distance brute — 26 octobre 2026

`python tests/study_noise_wiener_matching_floor.py --data-version N` :152 nouveaux
champs, gaussiennes1,5/marches/stries3/6/trois plans, plats, quatre répétitions,
deux covariances. Même seuillage du§102 ; seule la distance change :
SSD brut contre SSD−2(1−rho(d)), variance marginale connue1, sans max(.,0).
Le tri est conservé identique en blanc (constante commune hors self), vérifié
par test et chaque réalisation. En corrélé, pas de promesse de dé-biais après
sélection ni de décorrélation réelle des patches.

Audits : fraction de voisins de rayon Chebyshev≤2 et variance DC de moyenne
à géométrie fixée. Mesurer premier étage, complet et DC NLM conservé.
JSON `distance-debiaisee-pilote-collaboratif-bruit-vNNN-*`, sources/graines.
Ne pas modifier le seuil pour compenser une régression ; pas de production.

## 104. Regroupement guidé du pilote seuillé — 26 octobre 2026

`python tests/study_noise_wiener_guided_pilot.py --data-version N` :248 champs
complets128, NLM14:114 ; pilote seuillé30:98 et576 références communes.
Groupes choisis sur brut, distance brute corrigée ou NLM observé. Coefficients
filtrés restent bruts, seuil/covariance identiques. Pas de soustraction du
plancher brut aux distances du NLM dont la covariance n'est pas celle du brut.

Final Wiener sur cœur52:76 et81 références, pilotes indexés explicitement
par leur origine14 ou30. Mesurer premier étage, complet et DC NLM conservé.
Pics et budgets identiques aux§102–103, graines nouvelles. Ne pas comparer
les chiffres àl'ancien96 sans reprise appariée. Le guide n'est pas indépendant.
JSON `regroupement-guide-pilote-collaboratif-bruit-vNNN-*`, sources/graines.
Tests de contexte128, non-fuite de vérité, invalidité et transport, en plus
des invariants précédents. Aucun réglage de seuil ou changement de production.

## 105. Énergie groupée à covariance complète entre membres — 26 octobre 2026

`python tests/study_noise_wiener_group_energy.py --data-version N` :248 champs
frais96, quatre répétitions/configuration. Groupes NLM observés, recherche21,
17 membres et cœur24 identiques aux comparateurs contemporains.
Covariance17×17 de chaque fréquence spatiale, valeurs propres positives
au-delà max(1e-12,1e-10 lambda_max). q=Σx²/lambda, rangr, gain max(0,1−r/q).
Comparateur Wiener pilote dans cette même base propre.

Àfréquence spatialeDC, projeter hors constante globale avec les16 lignes
non constantes de DCT groupe, puis réinsérer le DC NLM ; autres fréquences
entièrement régularisées. Pas moyenne spatiale locale brute non régularisée
du modèle§92. q après sélection n'est pas une chi² garantie ; pas théorème
de risque ni probabilité de signal. Covariance inter-fréquences non traitée.
Gains pilote individuels dépendent de la base choisie dans un espace propre
dégénéré ; statistique d'énergie groupée invariante àces rotations.

Trois tests : isotrope/DC, translation/non-fuite/transport, invalidité.
JSON `energie-groupe-covariance-wiener-bruit-vNNN-*`, sources et graines,
aucun ajustement empirique ou changement de production.

## 106. Transfert photographique borné du collaboratif — 26 octobre 2026

`python tests/study_noise_wiener_photo_transfer.py --data-version N` reprend
le JSON des10 supports sauvegardés256, quatre RAW. Fenêtres128 d'origine
(32,32),(96,32),(32,96),(96,96),(64,64), cœur24 au décalage52 dans chaque
fenêtre. Deux bruits/deux réalisations sur256 complet puis recadrage :40 champs,
200 recadrages liés. Sigma ajouté0,01, pas estimation ISO réelle.

Comparateurs figés : NLM, Wiener21/DC NLM, seuillé brut complet et DC NLM.
Pas ajustement de force/bruit. Budgets en sigma², biais/std en unités
YCoCg d'origine, projection de gradient sur source. Source déjà bruitée,
pas vérité propre : retrait de grain peut être pénalisé. Pas netteté
perceptuelle garantie, étoiles confirmées ou comparaison LR. Bandes du§97
géométriques seulement sur ces contenus naturels.

JSON `transfert-photo-wiener-collaboratif-bruit-vNNN-*`, parent vérifié,
sources/coordonnées/graines conservées. Deux tests : recadrage figé sans
mutation, identité/gradient constant. Aucun nouveau RAW ou changement de production.

## 107. Appariement du bruit plat avant comparaison de détail — 26 octobre 2026

`python tests/study_noise_wiener_matched_noise.py --data-version N` :calibrer
uniquement sur quatre plats frais par covariance. Cible MSE du Wiener21/DC NLM,
variante seuillée àDC NLM conservé. Force multiplie sigma supposé au seuil
et Wiener final, sans modifier le NLM/DC. Grille1/1,5/2/3/4/8, premier
encadrement puis au plus10 bissections géométriques ; tolérance2 % moyenne.
Ne pas supposer une monotonie globale d'une sélection discontinue. Abstention
si la cible n'est pas atteinte, aucun choix sur les formes ou photos.

Force figée,16 plats indépendants et120 formes par covariance. Graines
distinctes de calibration. Consigner si la MSE moyenne plate se transfère
à10 % près, sans recalibrer : sinon pas affirmation d'appariement validé.
Une égalité plate n'est pas une égalité de bruit texturé ou perceptuelle.
JSON `point-bruit-apparie-wiener-bruit-vNNN-*`, courbes/graines/sources,
pas estimation ISO ou paramètres de production. Aucun effet rétroactif
sur les preuves historiques àforce1.

## 108. Transfert photo du point plat figé — 26 octobre 2026

`python tests/study_noise_wiener_photo_transfer.py --data-version N --operating-point CHEMIN_JSON`
requiert un JSON du§107 avec deux calibrations réussies et contrôle plat
indépendant10 % satisfait. Vérifier l'empreinte du JSON au début et àla fin,
consigner forces et parent ; ne pas recalibrer sur les photos.
Même40 champs/200 recadrages que§106, mêmes graines, donc reprise appariée,
pas nouveau corpus indépendant. Comparateurs NLM, Wiener/DC, seuillé/DC NLM
àforce figée. Même mémoire `transfert-photo-wiener-collaboratif-bruit-vNNN-*`,
nouvelle version, jamais écrasement de la force1. Limites de vérité/bruit
d'origine et sigma ajouté inchangées. Aucun changement de production.

## 109. Robustesse au sigma estimé — 26 octobre 2026

`python tests/study_noise_wiener_sigma_robustness.py --data-version N --operating-point CHEMIN_JSON`
utilise des forces figées validées du§107.224 champs frais,672 états liés,
16 plats/96 formes par covariance. Sigma vrai1, estimé0,9/1/1,1 ; mêmes pixels
pour ces trois états. Normaliser observations/transport par sigma estimé,
puis multiplier sorties/signal/résidu pour comparer dans les unités originales.
Pas d'appariement annoncé hors estimation exacte, ni recalibration.
Pas erreur spectrale ou bruit RAW hétéroscédastique. Test constante/unités
et invalidité ; JSON `sensibilite-sigma-wiener-collaboratif-bruit-vNNN-*`.

## 110. Archivage d'une source historique absente — 26 octobre 2026

`python tests/archive_noise_wiener_context_sources.py` peut retrouver les
deux premières versions du contexte depuis une source ultérieure déjà
archivée. Chaque retrait d'édition doit être unique et **le SHA256 des octets
finaux doit égaler celui de la preuve originale**, sinon aucune archive écrite.
Conserver le parent, les sources et empreintes dans un JSON immuable
`archive-sources-contexte-wiener-vNNN-*`. Ne jamais modifier les anciennes
mesures, reconstruire un résultat numérique par supposition ou appeler
équivalent un code dont les octets n'ont pas été vérifiés.

## 111. Audit des hypothèses contre les calibrations existantes — 26 octobre 2026

`python tests/audit_noise_wiener_model_scope.py` réutilise les quatre
calibrations RCD conservées, sans nouveaux pixels. Rapporter spectres
marginaux via proxy1−D/(2variance) et matrices inter-plans déjà enregistrées,
comparaison aux hypothèses blanc/binomial indépendants. Pas covariance PSD
garantie ni nouvelle validation indépendante. JSON
`audit-portee-modeles-wiener-bruit-vNNN-*`, deux tests de modèle/source.

## 112. Préparation des spectres croisés par réserve spatiale — 26 octobre 2026

`python tests/study_noise_empirical_cross_spectrum.py` :quatre différences
RCD64 sauvegardées. Moitiés64×24 en x0:24 et40:64, écart16 ; échanger les rôles,
pas huit nouvelles paires. Covariance biaisée, normalisation de préparation,
lags tronqués8, FFT128, projection hermitienne PSD puis marges unitaires.
Auditer masse spectrale retirée, variance avant normalisation et symétrie
conjuguée ; spectre positif ne prouve ni gaussianité ni stationnarité caméra.

Comparer lags±4 de la réserve au binomial commun, marges seules et spectre
croisé. Ne pas choisir les hyperparamètres sur cette réserve. Conserver
préparation aux lags±24 et sources dans `preparation-spectres-croises-bruit-vNNN-*`.
Deux tests PSD/réalité/couplage et invalidité. Aucun nouveau RAW ou débruitage natif.

## 113. Transfert spectral réservé au reconstructeur — 26 octobre 2026

`python tests/study_noise_profile_covariance_wiener.py` :256 champs gaussiens128
informés par les huit échanges du§112. Générer depuis le spectre préparé
de la moitié réservée ; filtrer avec la moitié de préparation opposée.
Deux réalisations par configuration gaussienne/marche3/6/trois plans,
huit plats par échange. Ne pas assimiler ces champs àdes captures indépendantes.

Comparer binomial commun, marges par plan, puis blanchiment symétrique àlag0,
marges transformées et inverse exacte. Même recherche21/groupe17/h0,8/DC pilote,
pas nouveau seuillage ou force. Marges génératrices unitaires pour isoler
la forme, pas valider sigma. Couplage hors lag0 non intégralement blanchi.
Budgets et pics dans les coordonnées d'origine. Trois tests : rejeu modèle
binomial, inverse inter-plans, génération blanche. Mémoire
`transfert-profils-covariance-wiener-bruit-vNNN-*`, aucune modification de production.

## 114. Invariant de direction couleur après décorrélation — 26 octobre 2026

`python tests/study_noise_profile_color_invariance.py` :224 nouveaux champs,
réserves spatiales ISO1600/51200 contrastant le couplage, pas nouveau corpus
de boîtiers. Six directions unitaires Y/Co/Cg/Co+Cg/Co−Cg/Y+Co+Cg,
gaussienne1,5 et marche3/6, deux réalisations, huit plats/échange.

Comparer gains indépendants et communs aux trois modèles du§113. Gain
partagé=Σpuissance/(Σpuissance+Σvariance), mêmes groupes/pilotes/DC.
Pour vérité v*f, même opérateur spatial sur les plans commute avec le mélange
constant et son inverse ; vérifier RMS orthogonale relative≤1e-9.
Métrique sur signal transporté conditionnellement, pas teinte totale ou
garantie perceptuelle. Bruit et mélange spatial multicouleur restent possibles.
Conserver MSE trois plans, distorsion, bruit/croisé, projection/fuite ; ne
pas conclure sur le canal actif seul. Mémoire
`invariance-couleur-profils-wiener-bruit-vNNN-*`, deux tests métrique/invariant,
aucune modification de production ou ajustement sur la vérité.

## 115. Audit des preuves et sources directes — 26 octobre 2026

`python tests/audit_noise_wiener_campaign.py` vérifie les empreintes de scripts
archivés ou inchangés, dépendances directes déclarées, fermeture des budgets
MSE=distorsion+bruit+croisé et bandes pondérées. Les octets LF/CRLF sont
identifiés explicitement. Fichiers actuels utilisés en repli doivent correspondre
au hash original, puis leur source est archivée. Sinon échec, pas réinterprétation.
Mémoires `audit-integrite-campagne-wiener-bruit-vNNN-*`, nouvelles versions
lors d'ajout de preuves. Pas nouveau rendu, qualité photo certifiée ou fermeture
transitive complète de l'environnement. Deux tests empreinte/budget invalide.

## 116. Transfert photographique des profils préparés — 26 octobre 2026

`python tests/study_noise_profile_photo_transfer.py` :10 zones/quatre RAW,
fenêtres128 (32,32)/(96,96)/(64,64), quatre échanges de profils1600/51200,
120 bruits indépendamment tirés mais pas120 photographies. Générer depuis
la moitié opposée, marges sigma0,01, pas vrai bruit ISO de la photo.
Comparer cinq politiques figées du§114, aucune force réajustée.
Budgets/gradients/biais sur source déjà bruitée, pas vérité propre ou LR.
Mémoire `transfert-photo-profils-bruit-vNNN-*`, sources et coordonnées conservées.

## 117. Incertitude gaussienne de covariance corrélée — 26 octobre 2026

`python tests/study_noise_profile_covariance_uncertainty.py` :matrices K_ab
sur64×24 et retrait exact de moyenne J K_ab J, quatrièmes moments gaussiens
pour Cov(Sij,Skl)=Σ(Kik*Kjl+Kil*Kjk)/N², puis delta versrho. Pixels corrélés,
pas effectif1536 iid. Comparer différences de moitiés àun score diagnostique,
pas test calibré, intervalle natif ou gate automatique. Incertitude du fit
et non-gaussianité non incluses. Mémoire bornée/guard, pas images générées.
Deux contrôles analytiques iid indépendant/corrélé, mémoire
`incertitude-covariance-profils-bruit-vNNN-*`.

## 118. Résidus RCD réellement conservés, cœur12 valide — 26 octobre 2026

`python tests/study_noise_profile_real_residuals.py` :quatre champs différentiels
64 observés, normalisation extérieure déjà conservée, pas Gaussianisation ou
variance centrale forcée. Profils des côtés avant toute injection de signal.
Cœur26:38, pilote14:50/recherche21/groupe17/25 références, aucun padding.
Tous les comparateurs sur12, jamais anciennes MSE24/36. Six directions,
gaussiennes1/1,5/2,5 et marches3/6 ;49 états/profil,392 états liés àquatre
champs. Calibration hors cœur mais dans contexte de filtrage, pas indépendance
de capture ; nuisance différentielle potentiellement contaminée par scène.
Tests constante/contexte et budget/direction. Mémoire
`profils-residus-rcd-wiener-bruit-vNNN-*`, aucune production ou comparaison LR.

## 119. Captures distinctes et ancrage de l'ouverture — 26 octobre 2026

`python tests/extract_noise_profile_training_pairs.py` : paires d'apprentissage
9751/9752, 9674/9675, 9704/9705, 9813/9814, distinctes des quatre réservées.
Vérifier les empreintes RAW avant/après et les sources d'ouverture. Rejouer
9660/9661 comme ancrage, exiger pixels identiques puis retirer cet ancrage
des données d'apprentissage. Probe existante, délai fini, garde RAM, pas TIFF.
Persister l'intermédiaire dans le build pour éviter un redécodage en cas
d'échec d'assemblage. Mémoire `extraction-paires-profils-bruit-vNNN-*`.

## 120. Transfert inter-captures — 26 octobre 2026

`python tests/study_noise_profile_capture_transfer.py` : covariance apprise
sur capture distincte au même ISO/chemin ; contrôler réglages et identités.
Comparer 196 états liés à quatre champs sur cœur12. Signaler écarts de niveau
et couleur entre captures. Sigma cible encore oracle de paire. Mémoire
`transfert-captures-profils-bruit-vNNN-*`. Aucun correctif spécifique ISO.

## 121. Mélange fixe et variance adaptée — 26 octobre 2026

`python tests/study_noise_fixed_mixing.py` : covariances en unités physiques,
pondération par trace, blanchiment moyen et diagonalisation conjointe Jacobi.
Apprendre les axes sur les seules captures d'apprentissage, figer puis mesurer
les corrélations réservées. Contrôler inversion, rang et objectif monotone.
Mémoire `melange-fixe-covariances-bruit-vNNN-*` ; pas axes capteur prouvés.

`python tests/study_noise_fixed_axes_variance.py` : comparer sigma appris,
sigma cible des 3007 pixels extérieurs différentiels et noyau commun.
Restaurer les unités physiques avant matrice et normaliser ensuite chaque axe ;
inverse avant budget YCoCg. Gains communs, invariant de signal contrôlé.
Mémoire `axes-fixes-variance-adaptee-bruit-vNNN-*`. Sigma cible **oracle**,
pas estimateur sur image seule ; 196 états réutilisés, pas acquisitions nouvelles.

## 122. MAD sur une image et transfert figé — 26 octobre 2026

`python tests/study_noise_axes_single_frame_sigma.py` : HH Haar 2×2
non recouvrant, MAD centré. Facteur blanc gaussien, correction spectrale HH
et rapport empirique MAD/std appris uniquement sur les captures d'apprentissage.
Mesurer A/B séparément, jamais leur moyenne comme estimateur d'image seule.
Différence cible utilisée seulement pour évaluation. Pas retouche de facteur
après lecture des résultats. Mémoire `sigma-axes-image-seule-bruit-vNNN-*`.

`python tests/study_noise_single_frame_axes_transfer.py` : reprendre les
estimations A seulement depuis cette mémoire, vérifier empreintes des parents,
comparer commun/oracle/spectral/empirique dans le même cœur12 et les mêmes
196 états. Mémoire `transfert-sigma-image-seule-bruit-vNNN-*`.
Sigma estimé avant injection sur tuile sélectionnée plate : ne pas appeler
cette épreuve robustesse aux textures ou débruitage complet d'image seule.
La différence peut éliminer du motif fixe présent dans chaque image.
Conserver les budgets complets, l'invariant couleur et les pics des formes,
pas seulement une moyenne globale. Aucun changement de production.

## 123. Contamination directionnelle du MAD des axes — 26 octobre 2026

`python tests/study_noise_axes_mad_texture_bias.py` : repartir des quatre A
originales, jamais de leur différence pour mesurer le MAD. Injecter en unités
physiques via l'inverse des axes figés ; amplitudes 1/3/6 sigma de référence,
six directions, gaussiennes largeur1/2/4, rayures verticales et diagonales
période3/6/12, marche et rampe. Comparer avant/après à bruit identique.
792 états liés à quatre tuiles. Les annulations HH sur motifs séparables sont
des contrôles négatifs directionnels, pas une preuve de robustesse générale.

Mémoire `contamination-textures-mad-axes-bruit-vNNN-*`. Conserver rapport MAD
et sigma de chaque calibration ; leurs facteurs de contamination sont identiques,
ne pas les compter comme estimateurs indépendants. Pas de seuil réglé sur ces
cas ni de raccordement production. Contrôles unitaires : annulations HH,
réponse diagonale et finitude des signaux.

## 124. Décomposer le biais sans apprendre de correctif — 26 octobre 2026

`python tests/study_noise_axes_sigma_bias_decomposition.py` : reprendre les
estimations figées du §122 et les quatre différences conservées. Pour A/B
séparément, multiplier MAD image/MAD différence, MAD différence/(0,67449 std HH)
et 0,67449 std HH/(sigma référence × facteur appris). Vérifier égalité exacte
avec le rapport de sigma sauvegardé. Ne pas transformer ces facteurs en
corrections apprises sur les captures réservées.

Mémoire `decomposition-biais-sigma-axes-bruit-vNNN-*`. Facteurs diagnostiques
corrélés, pas effets causaux séparés. Le troisième inclut différence entre
ROI et approximation spectrale ; le premier n'isole pas le motif fixe.
Contrôler les compensations de biais, sans les vendre comme robustesse.
Deux tests : identité numérique et statistiques invalides. Aucun filtrage.

## 125. Enveloppe spectrale conditionnelle dans les axes fixes — 26 octobre 2026

`python tests/study_noise_axes_spectral_gate.py` : lire quatre paires
d'apprentissage distinctes des quatre réservées, matrice figée et textures
du§123. Six caractéristiques64 : LH/HH, HL/HH, platitude, huit pics, corrélations
HH x/y. Ce sous-modèle inspiré du profil P **n'est pas** le P natif à huit
caractéristiques/tuiles32. Fenêtre racine de Hann, retrait fréquences rayon≤4.

Par spectre préparé :256 tirages gaussiens pour centre/échelle,256 autres
pour seuil d'ordre supérieur à95 %,256 autres pour contrôle conditionnel.
Graines, caractéristiques et seuils conservés. Maximum par axe et conjoint
sur18 dimensions ; pas choix de force ni seuil après lecture des réserves.
Mesurer acceptation A/B/D et contamination des792 injections liées.
Abstention n'est pas correction ; ne pas recopier un autre niveau lumineux.

Mémoire `abstention-spectrale-axes-bruit-vNNN-*`. Garantie conditionnelle
gaussienne seulement ; publier aussi les faux rejets réels et les textures
acceptées, pas uniquement le bon rejet des motifs forts. Trois tests :
invariance échelle/décalage, rang du seuil, dimensions/échelle de l'enveloppe.

## 126. Stress du rejet et contrôle d'identifiabilité — 26 octobre 2026

`python tests/study_noise_axes_gate_texture_stress.py` : figer les seuils§125.
3840 injections liées aux quatre A (rayures localisées, phases/orientations
variées, deux bandes aléatoires, six directions, quatre amplitudes).
Rapporter aussi les tuiles d'origine rejetées, les contaminations acceptées
et les dénominateurs. Pas de seuil optimisé après résultat.

En parallèle :64 champs gaussiens de base et64 ajouts indépendants de même
PSD par ISO, quatre amplitudes partageant les mêmes champs. La loi normalisée
est identique, sigma multiplié par√(1+a²) ; l'acceptation élevée est attendue,
pas une capacité à reconnaître une texture.256 bases au total, pas1024 photos.
Mémoire `stress-textures-abstention-axes-bruit-vNNN-*`.

## 127. Contextes réels256 autour des ancres64 — 26 octobre 2026

`python tests/extract_noise_axes_gate_contexts.py` : quatre paires réservées
existantes, agrandir la région plate64 de96 pixels de chaque côté sans
clamp. Même rectangle sur A/B. Probe `--original-roi-parent` existante,
pas de preview, TIFF, sélection de zone ou correction photographique.
Vérifier garde5 Gio, empreintes avant/après, délai180s par RAW,
centre64 historique et médiane d'ouverture. Conserver pixels256 et coordonnées.
Mémoire `contextes-abstention-axes-bruit-vNNN-*` ; les régions voisines ne
sont pas automatiquement plates et ne sont pas de nouvelles captures.

## 128. Couverture locale32 avec données conservées — 26 octobre 2026

`python tests/study_noise_axes_local_spectral_gate.py` : quatre tuiles32
non recouvrantes par région64, spectres/axes d'apprentissage figés.
Recalibrer les six caractéristiques à cette résolution avec trois jeux
gaussiens disjoints256 chacun ; seuil par axe sans famille conjointe implicite.
Publier compte0..4, refus et médiane HH des compatibles, convertie avec
le facteur spectral HH déjà figé. Sélection sur A, jamais sur B/D.

Comparer les792 injections réutilisées et le biais par rapport à la référence
différentielle64. La présence d'au moins une tuile rend seulement un estimateur
exploratoire calculable : cela ne remplace pas les16 tuiles minimum du profil P.
Pas de copie de valeur si absence, ni seuil accommodé aux réserves.
Mémoire `voisinages-locaux-abstention-axes-bruit-vNNN-*`.

## 129. Couverture réelle256, classes de Y physique — 26 octobre 2026

`python tests/study_noise_axes_gate_context_coverage.py` :64 tuiles32 non
recouvrantes, classe de A(Y)=floor(4(log10(Y)+4)), Y∈[10⁻⁴,1[.
Sélection A seule avec enveloppe§128, médianeHH/facteur figé,16 compatibles
minimum par axe/classe ; jamais copier un axe/classe absent.
Référence a posteriori sur différence :8 tuiles minimum, gradient de l'image
moyenne lisséeσ2<3sigmaHH(DY), dérive locale de moyenne≤2 %.
Calculer aussi référence sur la population sélectionnée et luminosité médiane
pour auditer biais de sélection et variations dans la classe.

Mémoire `couverture-contextes-abstention-axes-bruit-vNNN-*` ; ancres centrales64
doivent reproduire caractéristiques et décisions§128. Les effectifs ne valent
pas indépendance spatiale et la différence calme/stable n'est pas vérité propre.

## 130. Estimation par second moment HH — 26 octobre 2026

`python tests/study_noise_axes_second_moment.py` : sélection§129 inchangée.
Covariance d'apprentissage figée ; calcul Var(HH)−Var(moyenne HH) sur16×16
coefficients décimés (32pixels), somme des covariances pondérées par effectifs
des déplacements. Contrôle iid :255/256. Ce calcul de second moment n'exige
pas une loi marginale gaussienne ; il exige le transfert de covariance.
Rapporter médiane d'écart-type et variance regroupée, références appariées.
Ne pas ajouter de correctif appris sur les réserves ou confondre sensibilité
aux valeurs extrêmes et réduction du biais de loi du MAD.
Mémoire `moment-ordre-deux-axes-bruit-vNNN-*`.

## 131. Paires supplémentaires et coordonnées fixées — 26 octobre 2026

`python tests/extract_noise_axes_additional_pairs.py` : choisir avant décodage
la première paire lexicographique par ISO étudié du catalogue développé61,
classes non vides et dérive historique≤2 %, exclure huit paires axes déjà utilisées.
Garder le même rectangle256 de l'ISO ; ne pas rechercher la zone donnant le
meilleur score. Garde5 Gio par RAW, délais finis, empreintes/source/binaire
contrôlés. Mémoire `paires-supplementaires-axes-bruit-vNNN-*`.
Captures supplémentaires de cette épreuve, pas test historiquement intact.

## 132. Domaine intermédiaire supplémentaire v002 — 26 octobre 2026

Après constat de plateaux dans v001, nouvelle campagne indépendante de domaine,
sans effacer v001 : sélectionner avant décodage la première paire lexicographique
par ISO dont la classe Y historique dominante est9..13, pondérée par effectif
de référence pixel Y ; exclure les12 paires déjà utilisées. Ne pas utiliser
l'erreur de sigma ni modifier le seuil spectral. Même rectangle256.
`extract_noise_axes_additional_pairs.py` décrit ce protocole courant ; la
recette et les résultats antérieurs restent dans les JSON v001, pas mode legacy.

`python tests/study_noise_axes_additional_pair_transfer.py` rejoue les facteurs,
seuils et fonctions du§130 sans réapprentissage ; vérifier égalité des facteurs.
Mémoire `transfert-paires-supplementaires-axes-bruit-vNNN-*`, v002 pour ce domaine.
Refuser les références localement instables même si l'ancien contrôle global
passait. Un axe constant est invalide, ses caractéristiques/scores sont `null`,
sans signature ni sigma copiés ; continuer les autres axes valides.

## 133. Composante HH commune aux prises — 26 octobre 2026

`python tests/study_noise_axes_common_component.py` :12 paires d'évaluation,
trois jeux de contextes et mesures figées. Pour chaque tuile32, axe et population
(candidates, calmes/stables, sélectionnées calmes/stables), calculer HH centrés,
Var(A),Var(B),Cov(A,B),Var((A−B)/√2). Vérifier identité exacte
Var(D)=(Var(A)+Var(B))/2−Cov(A,B). Ne pas tronquer les covariances négatives.

Mémoire `composante-commune-haar-axes-bruit-vNNN-*`. Fraction reproductible
Cov(A,B)/((Var(A)+Var(B))/2), pas fraction de bruit capteur démontrée.
Texture réelle, motif fixe, corrélation temporelle et dérive peuvent contribuer.
Les tuiles sont déjà sélectionnées ; pas confiance population ou a priori
appris des réserves. Deux tests d'identité/anticorrélation/cas constants.

## 134. Transfert de sigma de contexte vers le reconstructeur — 26 octobre 2026

`python tests/study_noise_axes_context_sigma_reconstruction.py` : quatre champs
différentiels historiques,196 états liés, même cœur12 et reconstructeur.
Comparer commun/oracle/MAD global A64/MAD compatible A256/variance compatible A256.
Choisir la classe selon Y physique de l'ancre originale avant injection,
minimum16 compatibles, pas autre classe si manque. Sigma des variantes aveugles
provient exclusivement de A ; D sert au diagnostic et repère commun de mesure.

Mémoire `reconstruction-sigma-contextes-axes-bruit-vNNN-*`. Pas adaptation
de force selon résultat. Les huit paires supplémentaires testent l'estimation,
pas ce rendu. Vérifier tous budgets, pics et invariant de signal couleur.

## 135. Décision sur les compromis, sans nouveau rendu — 26 octobre 2026

`python tests/summarize_noise_axes_context_tradeoffs.py` : lire§134, trois
comparaisons appariées ;588 lignes dérivées, pas588 nouveaux tests.
Vérifier ΔMSE=Δdistorsion+Δbruit+Δterme croisé. Classer domination seulement
sur les deux premiers axes, conserver le terme croisé et les pics séparément.
Ne pas appeler meilleure qualité une simple diminution du bruit d'aplat.
Mémoire `bilan-compromis-sigma-contextes-axes-bruit-vNNN-*`.

La suite devra figer un sigma défendable et le modèle de bruit, puis évaluer
un pilote conservant les petites structures à bruit résiduel apparié.
Ne pas sous-estimer volontairement sigma pour masquer une erreur du pilote,
ni relancer les anciens pilotes rejetés sous un nouveau nom. Aucun passage
en production sur la seule baisse d'erreur d'estimation du sigma.

## 136. Puissance du pilote et covariance de transport — 27 octobre 2026

`python tests/study_noise_pilot_cross_power.py` : opérateur local exact du
pilote NLM existant dans `noise_pilot_transport.py`, patch5/recherche21/h0,8,
self maximal/agrégation inchangés. Trois tests vérifient équivalence à `aggregate`,
matrices de bruit à opérateur identité et rejet des entrées invalides.
Covariances des coefficients DCT5 calculées à géométrie figée : raw/pilote.

Comparer pilote², pilote²−Var(pilote), raw×pilote−Cov(raw,pilote),
raw²−Var(raw), sans clip positif dans les mesures principales. Conserver
les estimands propres : puissance transportée, produit x×(Wx) ou puissance
du signal raw. Ne pas appeler la puissance croisée estimateur sans biais de x².

Poids aveugles vs poids d'une observation scout indépendante : ce contrôle
à deux observations n'est pas une implémentation sur image seule. Quatre lois
gaussiennes préparées,64 paires plates et4 structurées/modèle,32 formes
partagent chaque paire structurée.544 champs tirés et1536 états liés.
Synthèses sans DC, regroupées par tirage pour incertitude, pas par coefficient.
Pas groupes collaboratifs, seuils, force recalée, rendu final ou production.
Mémoire `puissance-croisee-pilote-bruit-vNNN-*`, sources et formules conservées.

## 137. Correction complète de la dérivée des poids — 27 octobre 2026

`python tests/study_noise_pilot_stein_trace.py` : pilote local NLM continu seul,
pas masque connexe ni sélection dure de17 voisins ; ne pas réactiver la garde
SURE écartée. Pour chaque coefficient non DC j, direction déterministe CBⱼᵀ,
tangente analytique complète du pilote puis projection sur gⱼ.
72 directions pour trois plans×24 coefficients ; pas sondes aléatoires ou
différences finies dans les mesures. Contrôle séparé de dérivée à poids figés.

Évaluer rⱼgⱼ−(CBⱼᵀ)ᵀ∇gⱼ sur128 champs gaussiens nuls nouveaux,32/modèle.
Conserver chaque coefficient et moyenne par tirage ; erreurs-types entre
tirages, pas indépendance fictive des coefficients. Pas de clip des puissances,
de réglage de force ou de sélection automatique. L'identité vise (Bx)E[g],
pas x² ; sa validité gaussienne ne valide pas les RAW développés non gaussiens.

Mémoire `derivee-exacte-pilote-bruit-vNNN-*`. Cinq tests : opérateur équivalent,
covariances identité, entrées invalides, dérivée contre différences centrales
et direction de covariance complète. La traversée de coudes à pas grossier
est distincte de l'erreur du calcul tangent exact.

## 138. Contrôle de covariance linéaire et réplication — 27 octobre 2026

`python tests/study_noise_pilot_gaussian_control.py` :1024 nouveaux champs
par modèle, quatre lois préparées, carré DCT raw comparé à Var(raw) analytique.
Pas pilote, dérivée, renormalisation des champs ou sélection des résultats.
Mémoire `controle-lineaire-gaussien-pilote-bruit-vNNN-*`.

`study_noise_pilot_stein_trace.py` courant exécute la réplication v002 :256
nouveaux champs/modèle, mêmes formules. V001 reste dans sa mémoire immuable,
pas option legacy ; ne pas fusionner les moyennes après lecture ni augmenter
encore le nombre de champs jusqu'à ce que le contrôle paraisse favorable.
Publier aussi les écarts persistants, valeurs négatives et coût borné.

Pour une future étude structurée, l'identité complète vise xE[g(y)], où g
est le pilote **observé**, pas xE[W(y)x]. Conserver séparément la contribution
transportée du signal et celle du bruit ; leur moyenne peut changer quand
les poids dépendent de l'observation.

## 139. Audit des tangentes et covariances croisées — 27 octobre 2026

`python tests/audit_noise_pilot_tangent.py` : comparer directions CBᵀ et
covariance spatiale du générateur,864 entrées/modèle (trois plans, quatre
décalages,24 coefficients). Contrôle déterministe, pas tirages jusqu'au succès.
Rejouer un contre-exemple connu et un champ neuf/modèle pour vérifier diagonales
archivées et convergence de trois directions aux cinq pas fixés. Conserver
tous les pas, ne pas choisir un epsilon dans le débruiteur (tangente analytique).
Mémoire `audit-tangente-pilote-bruit-vNNN-*`. Pas nouvel échantillon de qualité.

## 140. Poids propre1 : ablation du pilote — 27 octobre 2026

`python tests/study_noise_pilot_self_weight.py` : self maximal vs1, unique
implémentation partagée ; recherche21/h0,8/agrégation5 inchangés.196 formes
sur les quatre champs réels historiques, variances oracle communes aux méthodes,
mesure du pilote **cœur5**, noyaux effectifs et pics. Pas comparer cette MSE
aux anciens rendus12. Le B natif avait d'autres paramètres : pas son rejeu.
Mémoire `poids-propre-pilote-bruit-vNNN-*`. Ne pas conclure aux queues de bruit
à partir de quatre aplats seulement ni compter les états liés comme photos.

## 141. Self1 dans le reconstructeur partagé — 27 octobre 2026

`python tests/study_noise_pilot_self_reconstruction.py` : même reconstructeur,
groupes/gains/variance ; seul pilote remplacé par le self1. Contrôler équivalence
du pilote36 et de la sortie12 de référence avec fourniture explicite du pilote.
196 formes réelles liées et512 contrôles gaussiens plats nouveaux,128/modèle,
méthodes appariées. Budgets complets, pics et invariant couleur ; moyenne,
quantiles et contre-exemples de bruit, pas seulement amélioration moyenne.
Mémoire `reconstruction-poids-propre-pilote-bruit-vNNN-*`. Si bruit différent,
ne pas qualifier le point d'appariement sans étude dédiée. Aucune production.

## 142. Bruit réel et contre-exemples connus du poids propre — 27 octobre 2026

`tests/study_noise_pilot_self_real_nulls.py` : blocs64 non recouvrants dans
les12 contextes256 existants ; les quatre sous-tuiles32 doivent être calmes
et stables selon les références gelées, avant reconstruction. Enregistrer
toutes les exclusions, variance extérieure et variance centrale non forcée.
Mêmes sigma oracle et spectres appris pour self maximal et1. Moyennes,
queues et comptes par ISO/paires, sans traiter les blocs comme captures.
Mémoire `queues-reelles-poids-propre-pilote-bruit-vNNN-*`. La différence
A−B n'est pas une vérité propre de photo et peut annuler un bruit commun.

`tests/study_noise_pilot_self_counterexamples.py` : rejouer exactement les
neuf champs de bruit connus pour leurs faux groupes. Vérifier les anciennes
sorties centrales avant comparaison des deux pilotes actuels. Aucun retour
du masque discontinu, ni sélecteur SURE ; pas estimation d'un taux de défauts.
Mémoire `contre-exemples-poids-propre-pilote-bruit-vNNN-*`.

## 143. Transfert entre paires et phase sous-pixel — 27 octobre 2026

`tests/study_noise_pilot_self_capture_transfer.py` : premier bloc admissible
par paire dans l'ordre spatial, jamais celui au meilleur score. Huit paires
dans ce corpus ; seize gaussiennes chacune, directions Y/Co/Cg/mélange signé,
amplitudes3/6, largeurs1/2,5, phase0,5. Mêmes sigma extérieurs et apprentissage,
aucune adaptation des paramètres après lecture. Mémoire
`transfert-captures-poids-propre-pilote-bruit-vNNN-*`. Comparer MSE, bruit,
distorsion/croisement et pic, par paire avant moyenne. Rapporter régressions
et paires exclues ; l'épreuve reste conditionnelle, additive et non native.

## 144. Distance courte, agrégation fixe — 27 octobre 2026

`tests/study_noise_pilot_matching_window.py` : distance3×3 vs5×5 avec
agrégation5, self1, h0,8/recherche21 et reconstructeur partagé identiques.
Ne modifier ni sigma ni force pour obtenir un résultat favorable.132 états
sur quatre champs réels et512 nouveaux champs gaussiens nuls, quatre modèles,
méthodes appariées. Même espérance du plancher de distance ne signifie pas
même variance ni même distribution des poids. Mémoire
`fenetre-correspondance-pilote-bruit-vNNN-*`. Rapporter MSE/pics et bruit moyen,
quantiles, pire cas ; aucune garantie de point de fonctionnement identique.

## 145. Attribution oracle dans le reconstructeur courant — 27 octobre 2026

`tests/study_noise_pilot_power_dc_diagnostic.py` : huit paires fixes du
transfert précédent,200 états liés (aplats, gaussiennes, marches). Rejouer
les128 budgets aveugles des gaussiennes à1e−12. Même pilote observé self1,
mêmes groupes choisis sur celui-ci, variances et formule de gains partagés.
Intervenir seulement sur puissance transportée propre, puissance vraie,
DC vrai, ou puissance/DC vrais ensemble. Mémoire
`diagnostic-puissance-dc-axes-bruit-vNNN-*`. Ces interventions sont **oracle**,
inutilisables sur photo et non des bornes optimales. Distinguer cette
attribution de l'ancien diagnostic gaussien per-plan ; publier bruit,
distorsion et croisement, pas seulement erreur ou pic. Aucun modèle déployé.

## 146. Source aveugle de la moyenne de groupe — 27 octobre 2026

`tests/study_noise_pilot_raw_dc.py` : remplacer uniquement la moyenne DC
du pilote par celle des données d'entrée, groupes et gains inchangés.
Ne pas activer les interventions oracle.200 états réels du diagnostic
précédent, référence rejouée à1e−12, et512 nouveaux champs plats gaussiens,
128 par modèle. Vérifier indépendance de la sortie à la vérité de mesure,
translation constante et conservation de la direction de couleur conditionnelle.
Mémoire `moyenne-brute-groupes-axes-bruit-vNNN-*`. Si le bruit augmente,
aucune revendication de point de fonctionnement apparié ou de qualité
supérieure sur la seule conservation du pic ; pas de compensation par sigma.

## 147. Correction de moyenne calibrée sur bruit indépendant — 27 octobre 2026

`tests/study_noise_group_mean_gate.py` : δ=(DC_brut−DC_pilote)/√425,
moment3×3 non centré appris sur64 champs gaussiens par spectre, puis seuil
gelé sur128 autres champs : maximum des scores quadratiques de leurs25
groupes. Gain commun `max(0,1−T/max(q,T))` sur la seule différence DC ;
aucun changement du groupe, des puissances ou de la force. Les simulations
recalculent poids et sélection, au lieu de les supposer indépendants.

Contrôler sur128 nouveaux champs par modèle, les200 états réels précédents
avec rejeu à1e−12, et90 blocs plats réels admissibles. Archiver les innovations
des champs de moment et seuil, leurs graines disjointes, les gains par groupe
et les budgets. Compter faux déclenchements par champ. Calibration gaussienne
appariée n'est pas garantie sur RAW non gaussien, ni garantie de MSE. Ne pas
abaisser le seuil après lecture des détails ni compter les25 groupes comme
réplications indépendantes. Mémoire `correction-moyenne-calibree-axes-bruit-vNNN-*`.

## 148. Transfert de correction DC sans recalibration — 27 octobre 2026

`tests/study_noise_group_mean_gate_transfer.py` : lire les seuils/matrices
du premier banc et vérifier les empreintes des dépendances avant lancement.
32 nouveaux champs gaussiens, huit par modèle ;49 configurations par champ,
dont32 gaussiennes avec deux phases, huit marches diagonales, huit sinusoïdes
diagonales période8 et un aplat.1568 états liés,32 bruits indépendants.
Mesures dans les axes préparés unitaires : pas comparaison de MSE absolue
aux états réels YCoCg. Paire référence/candidat sur le même bruit et mêmes
paramètres. Ne pas refixer les seuils, choisir une famille favorable ou
traiter les1568 états comme réplications indépendantes. Publier activations,
pics, budgets et pires cas par famille. Mémoire
`transfert-structures-moyenne-calibree-bruit-vNNN-*`.

## 149. Sensibilité de la sécurité DC au sigma estimé — 27 octobre 2026

`tests/study_noise_group_mean_gate_sigma.py` : rapports sigma estimé /vrai
0,8 /0,9 /1 /1,1 /1,2 fixés avant calcul.512 champs plats réservés au contrôle
du premier banc, réutilisés sans sélection ;2560 états liés, pas nouveaux
bruits indépendants. Normaliser les données par le sigma estimé, recalculer
poids/groupes et remettre les sorties en unités vraies avant mesure. Le
rapport1 doit rejouer les512 budgets canoniques à1e−12. Aucun seuil refixé.
Compter activations par champ et rapport, MSE, quantiles et pires cas.
Cette épreuve nulle mesure la sécurité, pas la conservation des détails.
Erreur scalaire commune seulement, pas de défaut de spectre ou de canal.
Mémoire `sensibilite-sigma-moyenne-calibree-bruit-vNNN-*`.

## 150. Sécurité sur spectres de captures différents — 27 octobre 2026

`tests/study_noise_group_mean_gate_psd.py` : conserver modèle supposé et
calibration de la paire d'apprentissage du même ISO. Générer le bruit depuis
les spectres estimés des premiers blocs admissibles de huit **autres** paires,
sans mise à jour du filtre ou du contrôle avec ces spectres. Variances marginales
réelles/supposées1 par construction ; pas de renormalisation des tirages.
Auditer projection positive et générateur, comparer covariances aux lags±8.

128 nouveaux champs gaussiens par source,1024 au total. Rapporter activations,
budgets, quantiles et pires cas par source, pas seulement par ISO. Huit spectres
locaux ne sont pas1024 captures ou lois RAW natives ; incertitude d'ajustement,
dérives et composantes communes annulées restent des limites. Épreuve nulle
de sécurité, pas de conservation des détails. Mémoire
`transfert-spectres-moyenne-calibree-bruit-vNNN-*`.

## 151. Transfert structural sur spectres non appariés — 27 octobre 2026

`tests/study_noise_group_mean_gate_cross_shapes.py` : quatre premiers tirages
ordonnés de chacune des huit sources du test spectral, sans sélection par
score. Réutiliser les49 configurations du transfert apparié :1568 états liés
aux32 mêmes bruits, pas de nouvelles captures. Modèle/calibration supposés
fixes, spectre de génération différent, variances vraies unitaires. Rejouer
les32 aplats canoniques à1e−12 avant conclusion. Budgets, pics et régressions
par famille/source, mêmes unités d'axes préparés. Ce sous-ensemble ne mesure
pas précisément les queues et n'efface pas les contre-exemples du test nul.
Mémoire `structures-spectres-croises-moyenne-calibree-bruit-vNNN-*`.

## 152. Projection de correction sous budget d'aplat — 27 octobre 2026

`tests/study_noise_mean_risk_budget.py` : budget fixe1,1 de MSE totale trois
plans pour une vérité constante sur cœur12. Centrer la correction par plan,
limiter sa norme à(√1,1−1) fois celle de la référence centrée, coefficient
scalaire commun calculé sur sorties observées uniquement. Moyenne conservée,
borne déterministe par orthogonalité et inégalité triangulaire. Transporter
la même opération sur le signal pour les budgets conditionnels.

Rejouer les1568 états appariés, aucune nouvelle calibration ni choix du budget
sur les résultats. Mesurer MSE/pics, signal négatif et distorsion en couronne :
une borne de bruit avec perte du bénéfice ou halos n'est pas une réussite.
Les raccords/agrégations pleine image ne sont pas implémentés ; aucune garantie
par pixel, canal ou perception. Mémoire `budget-risque-moyenne-bruit-vNNN-*`.

## 153. Pilote positif équilibré et contrôle d'agrégation — 27 octobre 2026

`tests/study_noise_balanced_pilot.py` : graphe60×60 de centres5×5 valides
dans une entrée64, arêtes symétriques positives, sans padding/wrap. Recherche21,
self1/h0,8 et plancher de bruit fixés. Comparer pilote agrégé courant,
normalisation pixel par ligne et équilibrage symétrique D K D sur le même
graphe. Équilibrer jusqu'à résidu1e−8, maximum1000 itérations ; pas choix
du nombre d'itérations par score de qualité, échec si non-convergence.

Audit des sommes/masse sur domaine60 complet, pas une promesse sur cœur12.
Fournir la sortie36 au même reconstructeur partagé, sans réutiliser le
contrôle DC calibré pour un autre pilote. Huit champs gaussiens nouveaux,
200 états liés. Rapporter comparateurs, bruit/pics, convergence et temps
Python local (pas latence d'une photo). Mémoire `pilote-bistochastique-bruit-vNNN-*`.
Les références sur filtres symétriques motivent cette ablation ; ce n'est
pas une reproduction du modèle GMM/GSF publié ni de ses performances.

## 154. Isoler l'équilibrage à agrégation identique — 27 octobre 2026

`tests/study_noise_balanced_overlap.py` : agréger5×5 les poids des arêtes
correspondantes, pas les prédictions déjà déplacées. Contrôle par ligne
agrégé équivalent au pilote36 existant puis aux200 budgets enregistrés
à1e−10 ; contrôles pixel antérieurs aussi rejoués. Seulement l'équilibrage
change pour le candidat agrégé. Réutiliser les huit champs/200 cas, aucun
calibrage de force/itérations ou contrôle DC ancien. La masse du graphe60
ne garantit pas la masse du pilote agrégé tronqué. Mémoire
`pilote-bistochastique-agrege-bruit-vNNN-*`.

## 155. Cible DC débruitée, détection de référence inchangée — 27 octobre 2026

`tests/study_noise_balanced_dc_target.py` : pilote/groupes/puissances/gains et
innovation de déclenchement inchangés ; seule cible de correction DC remplacée
par le pilote équilibré agrégé. Pas nouveau seuil ou mélange choisi sur vérité.
200 cas enregistrés et31 états nuls **connus**, tous les actifs des contrôles
gaussiens/sigma/spectres précédents. Rejouer les références à1e−10, générateur
exact de chaque parent et sorties remises en unités physiques. Archiver un
échec de rejeu avant correction du script, sans relâcher sa tolérance.

Un succès sur ces épreuves de défauts connus n'est pas une validation de
queue indépendante ; prévoir de nouveaux tirages si la piste reste utile.
Mémoire `cible-moyenne-pilote-equilibre-bruit-vNNN-*`.

## 156. Cible débruitée sur résidus réels et calcul différé — 27 octobre 2026

`tests/study_noise_balanced_target_real.py` :200 états réels et90 blocs
admissibles déjà conservés. Même variance externe/axes, pas de substitution
gaussienne. Rejouer les290 références pilote/correction brute à1e−10.
Construire le graphe cible seulement si au moins un gain de référence est
non nul ; sinon résultat identique à la référence, sans calcul approché.
Tester indépendamment chemins fermé/ouvert. Mémoire
`cible-equilibree-residus-reels-bruit-vNNN-*`. Ce corpus réutilisé reste
exploratoire, pas un nouveau jeu de photos propres.

## 157. Validation indépendante à critères figés — 27 octobre 2026

`tests/study_noise_balanced_target_null_validation.py` :1024 nouveaux champs
sur12 spectres appariés/croisés, cinq rapports de sigma ;5120 états liés.
`tests/study_noise_balanced_target_structure_validation.py` :48 autres champs,
49 signaux chacun ;2352 états. Même candidat/matrices/seuils, convention
FFT(covariance) unique pour ces nouveaux tirages, aucune adaptation de qualité.

Écrans d'ingénierie prospectifs : nuls moyenne≤+1 % par source/rapport et
chaque état≤+10 % ; structures moyenne non aggravée par source/famille,
chaque état≤+10 %, gain gaussien global≥1 % sur validation fraîche et banc
réel exploratoire. Ce ne sont pas des tolérances de livraison approuvées.
Ne pas changer ces critères sur échec ou réétiqueter le banc en apprentissage.

`tests/summarize_noise_balanced_validation.py` ne s'exécute qu'après les deux
preuves complètes uniques ; contrôle graines distinctes, SHA et absence de
modification des sources pendant/après calcul. Conserver toutes les vérifications
et leurs échecs. Mémoires `validation-independante-cible-equilibree-*-bruit-vNNN-*`
et `bilan-validation-cible-equilibree-bruit-vNNN-*`. Un écran passé fige un
candidat **expérimental**, pas une version stable produit ou une autorisation
d'intégration : cœur12, validation visuelle/native et plein format restent ouverts.

## 158. Gel de sources du candidat expérimental — 27 octobre 2026

`tests/freeze_noise_balanced_candidate.py` exige un unique bilan prospectif
passé. Archiver SHA/textes des dépendances directes et des modules locaux
importés transitivement, vérifier qu'ils n'ont pas été touchés depuis le
début des validations. Conserver périmètre caméra/ISO, versions et limites.
Mémoire `candidat-bruit-cible-equilibree-vNNN-*` : ce n'est pas un tag Git,
une distribution autonome complète ou une version stable du produit.

Avant moteur/IHM, prévoir validation native plein format, raccords, bruit
image seule, coût et revue visuelle. Ne pas étendre les calibrations à un
appareil inconnu ni présenter les contrôles gaussiens comme vérité photographique.

## 159. Premier rendu photographique natif du candidat — 27 octobre 2026

`tests/render_noise_balanced_experiment.py` réutilise les dix supports
linéaires Rec.2020 de256 pixels archivés sur quatre RAW EOS R6 Mark III.
Il ne redécode pas les RAW depuis le nouvel exécutable et n'ajoute aucun
bruit synthétique. Le centre96×96 est rendu à sa résolution native.

Estimer le bruit uniquement depuis les supports de la même image : tuiles32,
axes figés, enveloppes spectrales existantes, médiane HH corrigée du facteur
spectral. Exiger16 tuiles acceptées par tranche de luminance et par axe ;
ne copier ni amplitude d'un autre ISO ni valeur d'une autre tranche.
Une tranche insuffisante reste inchangée. Le choix de la forme spectrale
par ISO logarithmiquement le plus proche est **une approximation expérimentale**,
pas un profil boîtier/ISO validé ; consigner ISO réel et ISO du modèle.

Chaque entrée64 physique fournit un cœur12 ; agréger par fenêtres de Hann
strictement positives avec un pas6, sans extension périodique ni padding.
Le transport de signal reçoit zéro seulement pour ses diagnostics internes :
les pixels de sortie, regroupements, gains et seuils n'utilisent aucune vérité
propre. Vérifier couverture, identité sans correction et abstention exacte.
Ce nouvel assemblage demande encore validation des raccords et des transitions
entre tranches traitées/non traitées ; il ne vaut pas validation plein format.

Exporter, pour revue visuelle demandée, original/référence algorithmique/candidat
en TIFF RGB16 sRGB avec ICC embarqué dans le dossier existant `base/bruit/proto`.
Nommer explicitement scène, zone, candidat/version et état. Aucun écrasement
ni nettoyage automatique. Les planches agrandies×4 dans le build sont des
aides de lecture ; les TIFF restent96 pixels natifs, pas des détails restaurés×4.
La référence algorithmique n'est ni Lightroom ni l'implémentation de production.

Conserver tous les comptes d'abstention, temps, changements moyens, écrêtages
d'affichage, empreintes de sources et exports dans
`rendu-experimental-cible-equilibree-bruit-vNNN-*.json`. Une image rendue n'est
pas une preuve de qualité : pas de MSE sans vérité propre, pas de gain garanti.
Comparer à100 % les TIFF, vérifier petits détails, couleur, aplats et raccords.

En cas d'interruption, archiver le job échoué et sa source avant correction.
Utiliser `--label` répété pour les seuls supports manquants, avec une nouvelle
version ; ce filtre porte sur le rendu, jamais sur les supports d'estimation
de la scène. Chaque zone terminée conserve une mémoire JSON immuable
`rendu-zone-cible-equilibree-bruit-vNNN-<zone>-<UTC>.json`, même si une zone
suivante échoue. Le statut final ne vaut réussite que pour la sélection
demandée. Les planches indiquent explicitement traitement, traitement partiel
ou abstention ; ne pas comparer une zone intacte comme si le filtre y avait agi.

## 160. Couverture par grille native hors zones visuelles — 27 octobre 2026

`tests/study_noise_photo_estimation_coverage.py` teste une seule hypothèse :
une distribution spatiale plus large peut-elle rendre l'estimation disponible
sans changer le seuil spectral ni le minimum16 tuiles/tranche/axe ?
Utiliser une grille5×5 de centres aux positions normalisées0,1/0,3/0,5/0,7/0,9,
prélèvements256 natifs sans redimensionnement. Exclure tout prélèvement qui
intersecte un des supports visuels256 ; vérifier absence de chevauchement
entre les supports d'estimation retenus. Ne pas combler une tranche/axe vide.

Employer exactement le binaire de sonde du premier rendu neutre, contrôlé
par SHA. Rejouer aussi les dix supports visuels pour contrôler géométrie,
contrat/profil/réglages d'ouverture, empreintes RAW et erreur maximale de
pixels≤2e−7. Seul `created_at_utc`, recréé à chaque ouverture, est exclu
de l'égalité de déclaration ; conserver les deux déclarations complètes.
Un échec interdit d'attribuer une différence à la seule couverture.
Décoder séquentiellement, avec≥5GiB libres et délai maximal300s par RAW ;
ne conserver dans les preuves que positions, signatures, MAD, acceptations,
estimations, provenance et rejeu, pas un nouveau jeu de photos à exporter.

Pour chaque bloc12 du rendu natif96 au pas6, identifier la tranche physique
et si ses trois amplitudes sont disponibles. Distinguer manque de population,
rejet spectral/axe non mesurable et luminance hors domaine. Comparer
l'estimation des seuls supports visuels à celle de la grille hors supports,
qui la **remplace** : les échantillons ne sont pas des ensembles emboîtés,
une diminution éventuelle de couverture doit aussi être conservée.

Mémoire immuable par scène `couverture-grille-photo-bruit-vNNN-<scène>-<UTC>.json`.
Plus de blocs couverts n'est pas une meilleure estimation du bruit : aucune
vérité propre sur ces photos. Une texture compatible peut être acceptée,
et un nouveau rendu exige encore une analyse des amplitudes et protections.
Pas de relâchement a posteriori, pas d'augmentation de force, pas d'utilisation
des TIFF IPR comme mesures fonctionnelles, pas de changement de production.

`tests/summarize_noise_photo_coverage.py` conserve aussi les variations de
médiane lors du retrait d'un contexte256 entier et la perte éventuelle des16
tuiles minimales. Ce contrôle spatial n'est pas un intervalle de confiance,
ni une référence de bruit vrai. Ne pas taire les diminutions de couverture.

## 161. Diagnostic des rejets et rendu sur estimations de grille — 27 octobre 2026

`tests/study_noise_photo_spectral_rejections.py` conserve les six signatures,
déviations normalisées et critères dépassés par tuile32/axe. Évaluer les quatre
enveloppes ISO existantes sans sélectionner celle qui accepte le plus, sans
élargir les seuils ni unir les critères. Un rejet spectral ne prouve ni
texture ni erreur de modèle : garder cette ambiguïté. Mémoire
`rejets-spectraux-photo-bruit-vNNN-<UTC>.json`.

Pour une revue exploratoire clairement identifiée, le lanceur de rendu accepte
`--coverage-evidence` répété, pointant les mémoires de grille terminées des
scènes sélectionnées. Contrôler identité RAW/modèle et rejeu d'ouverture avant
usage ; consigner les preuves comme parents et `estimation_support` dans
chaque ligne. Pas de nouvelle adaptation sur la zone à rendre. Même candidat
et force, mais nouveaux niveaux de bruit : ne pas attribuer leur différence
avec les anciens TIFF au seul correctif candidat. Aucun rendu ne remplace
une validation d'estimation sur des données à vérité connue.

## 162. Origine des rejets sur paires : image seule contre différence — 27 octobre 2026

`tests/study_noise_pair_rejection_origin.py` emploie les douze paires256
déjà conservées, sans nouvelle prise, décodage ni export. Calculer dans
les axes figés les signatures32 de A, B et D=(A−B)/√2. Le profil et les
seuils restent ceux de l'ISO correspondant, sans choix de la meilleure
enveloppe. Rejouer exactement toutes les décisions historiques A-only.

Conserver tous les résultats par tuile, mais distinguer les comptages dans
la population historiquement définie calme/stable : gradient moyen<3sigmaHH
de D et dérive moyenne≤2 %. Pour chaque paire/tranche/axe, distinguer :
A et D acceptés ; A rejeté/D accepté ; A accepté/D rejeté ; tous deux
rejetés ; axe non mesurable. Conserver les six critères dépassés, pas
uniquement un score agrégé. Aucun changement de seuil ou d'estimation.

A rejeté/D accepté est compatible avec une structure reproductible absente
de D ; D rejeté est compatible avec une différence entre modèle spectral
gaussien et résidu réellement développé. Aucune catégorie ne prouve seule
la cause : D peut aussi contenir dérive/mouvement et annuler une composante
reproductible. Les contrôles calme/stable sont conditionnels à D, et les
trois axes d'une tuile ne sont pas des observations indépendantes.

Mémoire `origine-rejets-spectraux-paires-bruit-vNNN-<UTC>.json`. Ces données
réutilisées donnent une piste de diagnostic, pas une nouvelle validation
des cheveux ou une autorisation de débruiter les zones rejetées.

## 163. Transport de spectres sur bruit gaussien sans signal — 27 octobre 2026

`tests/study_noise_gate_psd_transport.py` distingue les défauts du test spectral
des détails photographiques : appliquer les enveloppes32 figées à des champs
gaussiens sans signal, générés depuis les spectres de différence de paires.
Quatre spectres d'apprentissage contrôlent la cohérence de la génération ;
les48 quadrants128 des douze paires donnent les modèles de transport.
Utiliser256 tirages par contrôle et64 par quadrant, sans chercher une nouvelle
enveloppe ni ajuster la force. Conserver spectres/covariances, graines,
signatures, décisions et nombres de tuiles calmes/stables dans chaque quadrant.
Un axe de différence non mesurable impose une abstention de génération pour
ce quadrant ; ne pas emprunter un axe ou une covariance. Enregistrer nombre
de champs réellement générés et plages de graines disjointes. Conserver SHA
de covariance complète et bloc de lags8 ; les pixels de paires canoniques
restent nécessaires pour la régénérer exactement avec le code archivé.

Inclure aussi les quadrants insuffisamment stables, mais les identifier.
Une covariance ajustée sur D peut contenir dérive et structure résiduelle :
ce banc valide une propriété de transport du modèle mathématique, pas une
estimation correcte du bruit natif. Les spectres sont normalisés à variance
marginale unité ; toute différence d'acceptation porte donc sur la forme,
pas sur une simple amplitude ou une force de débruitage.

Mémoire `transport-psd-gaussien-filtre-spectral-bruit-vNNN-<UTC>.json`.
Comparer chaque quadrant aux contrôles de son ISO ; conserver tous les
résultats. Ne pas appeler les champs des photos indépendantes, ne pas déduire
une loi causale de luminance d'une variation simultanée de capture et niveau.

## 164. Plateaux natifs et transport du facteur Haar — 27 octobre 2026

`tests/study_noise_pair_plateaus.py` contrôle modes, niveaux répétés exacts
et dispersion de D sur les douze paires, dans les plans Y/Co/Cg et par
inversion vers RGB de travail. Ne pas identifier un écrêtage capteur depuis
ces seules mesures : un plateau peut provenir d'une autre étape d'ouverture.
Conserver les paires invalides dans le bilan avec leur diagnostic, sans les
utiliser comme profils de bruit. Mémoire `plateaux-paires-natives-bruit-vNNN-*`.

`tests/study_noise_haar_factor_transport.py` réutilise les covariances archivées
du transport gaussien, sans nouveau tirage. Calculer exactement hᵀCh pour
HH2×2 et le rapport de racines entre covariance transportée et covariance
de référence à même ISO. Pour une marginale gaussienne, ce rapport est
aussi celui du facteur MAD marginal théorique. Ce n'est **pas** le biais
réel du MAD fini d'une tuile corrélée/centrée/sélectionnée, ni un coefficient
correctif applicable à une image. Mémoire `transport-facteur-haar-spectral-bruit-vNNN-*`.

## 165. Prédiction de covariance par niveau, capture entière tenue à l'écart — 27 octobre 2026

`tests/study_noise_signal_covariance_prediction.py` teste une seule règle :
interpolation convexe de covariance **physique** selon la moyenne Y de A.
Pour deux ancres de même ISO, Y₀≤Y≤Y₁, poser
`w=(Y−Y₀)/(Y₁−Y₀)` et `C(Y)=(1−w)C₀+wC₁`. Normaliser ensuite seulement
par les variances marginales prédites pour comparer les corrélations.
Le mot physique désigne ici les unités du master développé, pas une preuve
de proportionnalité de Y au nombre de photons après toute la chaîne.

Les ancres proviennent de la paire d'apprentissage et des autres captures
de même ISO. Exclure **les deux fichiers de la capture évaluée de toutes
les ancres**. Au moins huit tuiles32 calmes/stables par quadrant128, et
au moins deux quadrants admissibles pour une ancre de capture ; conserver
les exclusions. Les ancres d'apprentissage64 ont une incertitude différente,
à expliciter. Vérifier boîtier, médiane d'ouverture, réglages déclarés,
profils, normalisation et corrections optiques identiques entre captures.

La moyenne Y d'entrée est calculée depuis A seulement ; le D de la capture
tenue à l'écart ne sert qu'à l'admissibilité de la référence et aux erreurs
d'évaluation. Aucune extrapolation hors des ancres disponibles : abstention,
pas de copie du profil le plus proche comme faux résultat d'interpolation.
Conserver ce profil voisin comme **comparateur**, avec le profil fixe actuel.

Mesures par quadrant puis par capture : erreur quadratique de covariance
normalisée sur les lags±8, erreur logarithmique absolue du facteur HH,
et erreur d'amplitude sigma (secondaire). Écran fixé dans le code avant
calcul : chaque capture testable doit améliorer covariance et facteur HH
face au profil fixe, et ne pas les aggraver face au voisin. Un échec ne
peut pas être effacé par une moyenne favorable des autres captures.
Cet écran d'étude n'est pas un critère de livraison du produit.

Mémoire `prediction-covariance-luminance-bruit-vNNN-<UTC>.json` : ancres,
poids, contrôles de contrat, erreurs, abstentions et verdict conservés.
Aucune nouvelle enveloppe, estimation sur TIFF, force de débruitage ou
modification de production. Les captures réutilisées ne constituent pas
une nouvelle validation indépendante d'un autre boîtier ou d'une autre session.

## 166. Découpler la corrélation de l'amplitude — 27 octobre 2026

`tests/study_noise_shape_only_prediction.py` effectue une seule ablation :
normaliser séparément chaque ancre de covariance avant le mélange, plutôt
que normaliser une covariance physique déjà mélangée. Garder les14 cibles,
quatre captures, poids issus de Y, ancres, refus d'extrapoler et écran§165.
Ne prédire aucun sigma et ne pas fournir le sigma vrai au calcul des poids.
Rejouer fixe/voisin/interpolation physique à1e−14 avant comparaison.
Mémoire `prediction-correlation-separee-bruit-vNNN-*`.

## 167. Capacité oracle : distinguer poids et famille de modèles — 27 octobre 2026

Après un échec du prédicteur, `tests/study_noise_covariance_oracle_capacity.py`
calcule les minima théoriques séparés de l'erreur de covariance normalisée
et de l'erreur HH, le long du **même** segment de corrélations. Le minimum
de covariance est une projection quadratique ; celui de HH est cherché
aux frontières et points stationnaires des morceaux de somme de logarithmes
d'affines positives. Aucun balayage de force du débruiteur.

Ces minima séparés peuvent demander des poids incompatibles : ils ne
prouvent pas un progrès simultané. `tests/study_noise_joint_capacity.py`
diagnostique alors la contrainte conjointe via une borne de Lagrange, avec
six pénalités fixes0/1/10/100/1000/10000 et minimisation scalaire aux frontières
et racines stationnaires quartiques. Les poids sont libres **par quadrant**,
donc plus favorables qu'un unique prédicteur issu de l'image.

`tests/verify_noise_joint_witnesses.py` vérifie arithmétiquement, dans tous les
essais déjà calculés, si un même vecteur de poids respecte simultanément
les limites covariance et HH de la capture. Conserver essais défavorables
et témoins ; ne pas recycler une pénalité ou un poids oracle comme paramètre
de production. Une absence d'obstruction ne prouve pas la faisabilité ;
un témoin vérifié donne seulement une existence sur ce banc numérique.

Mémoires `capacite-oracle-segment-correlation-bruit-vNNN-*`,
`capacite-jointe-segment-correlation-bruit-vNNN-*` et
`temoins-oracle-joints-correlation-bruit-vNNN-*`. Ces diagnostics utilisent
expressément D de la capture tenue à l'écart : **pas des estimateurs aveugles**,
pas une nouvelle validation indépendante ou une mesure de qualité visuelle.
Les bornes calculées en flottants ne sont pas des certificats formels
avec arithmétique d'intervalles ; conserver marges et tests numériques.

## 168. Poids depuis A seule par contraste robuste d'ondelettes — 27 octobre 2026

`tests/study_noise_single_image_weights.py` conserve les14 cibles et leurs
deux ancres de corrélation issues d'autres captures, ainsi que les34 abstentions
de plage/référence. Le poids est désormais inféré des pixels **A seuls**, pas
de la référence D, d'un poids oracle ou d'une nouvelle courbe de luminance.
La luminance de A continue seulement de déterminer les ancres disponibles.

Mesurer LH/HL/HH2×2, centrés par leur médiane, puis leurs MAD² divisés par
le facteur gaussien². Pour le modèle de corrélation mélangé, calculer les
variances théoriques de bande par hᵀCh. Éliminer l'amplitude par axe comme
paramètre de nuisance : `s²_c(w)=moyenne_b[E_cb/V_cb(w)]` et minimiser
`somme_c[3 log(s²_c(w))+somme_b log(V_cb(w))]`.

Ce critère remplace des moments par des MAD robustes : **contraste inspiré
d'un profilage gaussien, pas une vraisemblance exacte des MAD finis/corrélés**.
Optimisation du contraste d'entrée seulement : grille257 puis48 itérations
de raffinement local, frontières conservées. Aucune minimisation de l'erreur
de référence pour choisir le poids. Si un axe est non mesurable ou les ratios
de bande ne distinguent pas les ancres (seuil1e−4), s'abstenir.

Conserver les paramètres de nuisance estimés sans les appeler calibrés,
et quatre diagnostics sur sous-zones64 sans sélectionner le plus favorable.
Mesurer après inférence les erreurs de corrélation/HH face aux mêmes profils
fixe et voisin, puis les erreurs de sigma face à D pour le diagnostic uniquement.
La dispersion des sous-zones n'est pas un indice de confiance validé.

Épreuve de texture fixée avant calcul : quatre sinusoïdes faibles (période4,
amplitude0,5sigma de référence, axes0/1, angles0/45°) et deux petites
gaussiennes (largeur1,5, amplitude3sigma, axes0/1). La référence sert seulement
à définir l'amplitude des injections connues, jamais au calcul des poids.
Conserver tout déplacement du poids et des amplitudes estimées. Écran :
quatre captures passent covariance/HH sans abstention nouvelle ; sur les
sinusoïdes faibles, aucune amplitude estimée n'augmente de plus de10 %
par rapport à la même zone A originale. Ne pas ajuster ce seuil au résultat.

Mémoire `poids-image-seule-contraste-ondelettes-bruit-vNNN-*`. Il s'agit
d'un test hors ligne avec famille de modèles déjà caractérisée, pas d'un
débruiteur aveugle universel, d'une validation sur les cheveux ou d'une
intégration production. Aucun TIFF n'est généré pour cette étude.

## 169. Décomposer contamination des mesures et effet du poids — 27 octobre 2026

Si l'épreuve§168 échoue, `tests/study_noise_weight_amplitude_feedback.py`
rejoue les mêmes84 injections et leurs énergies, sans nouvel ajustement
de poids. Recalculer les amplitudes une fois avec le poids adaptatif enregistré,
une fois avec le poids de l'image A originale gelé. Vérifier le rejeu des
énergies et sigma à tolérance relative1e−12 avant interprétation. Conserver
les deux rapports et toutes les catégories face au seuil10 % déjà fixé.

Le poids original gelé n'est pas disponible comme référence propre dans
une vraie photo texturée : **ablation diagnostique, pas un nouveau garde-fou**.
Si la contamination demeure, ne pas l'attribuer au seul recalcul des poids.
La texture injectée à0,5sigma du plan peut être forte relativement au bruit
d'une sous-bande dont la variance est faible ; ne pas la qualifier de faible
à toutes les échelles sans vérification. Conserver aussi l'étendue des quatre
poids de sous-zones, sans inventer un indice de confiance ou un seuil de
sélection après le résultat. Mémoire
`decomposition-poids-amplitude-textures-bruit-vNNN-*`.

## 170. Poids par spectre détaillé robuste depuis A seule — 27 octobre 2026

`tests/study_noise_robust_spectral_weights.py` utilise l'estimateur
`tests/noise_robust_spectral_weights.py`. Conserver les14 cibles128, ancres
tenues à l'écart,34 abstentions et84 injections du§168, ainsi que tous les
critères de comparaison. Aucun changement des niveaux d'injection ou du
seuil de contamination10 % à la lecture des résultats.

Reconstituer les covariances complètes128 des ancres depuis les seules
paires de caractérisation autorisées pour chaque capture tenue à l'écart.
Contrôler leur bloc de lags±8 contre les ancres archivées (erreur relative
maximale≤1e−10) avant toute inférence. Normaliser leurs variances marginales.
Les pixels B de la capture évaluée ne sont jamais fournis à l'estimateur.

Sur A, retirer le plan affine par projection orthogonale, appliquer une fenêtre
de Hann périodique et calculer le périodogramme. Pour les modèles stationnaires
périodiques, calculer l'espérance du périodogramme **avec les mêmes projection
et fenêtre**, y compris les termes croisés du retrait des tendances. Vérifier
ce calcul sur une petite covariance blanche contre l'opérateur matriciel dense.
L'approximation restante concerne le modèle périodique du bruit photographique,
pas un oubli de l'effet de la fenêtre dans la réponse théorique.

Garder un demi-plan de fréquences complexes (sans compter les conjuguées
comme observations supplémentaires), rayon1/8 à1/2 cycle/pixel. Exiger un
support commun positif aux deux modèles et aux trois axes, au moins512 bins.
Le périodogramme observé est comparé au mélange des deux réponses modèles.
Par axe, éliminer l'amplitude avec la médiane des log-rapports ; minimiser
la somme des MAD des log-rapports centrés. Grille129 et raffinement32 étapes,
fondés uniquement sur le contraste d'entrée, jamais sur une erreur contre D.

L'amplitude nuisance est `sqrt(exp(médiane(log(I/P)))/ln(2))`, inspirée de
la médiane exponentielle d'un coefficient gaussien complexe. Fenêtre,
corrélations, retrait des tendances et bruit RCD natif limitent cette
approximation : **pas une vraisemblance exacte, ni une calibration garantie**.
Les pics spectraux rares peuvent perdre leur influence sans que des textures
larges ou compatibles avec le bruit deviennent identifiables.

Cinq tests préalables : espérance de fenêtre/projection, poids théorique et
invariance d'amplitude, pics rares, suppression d'une tendance affine,
abstention de modèles proportionnels. Ces tests ne remplacent pas l'épreuve
sur les captures et textures. Conserver délais par extrait128 sans les
annoncer comme performances de traitement d'une image entière.

Mémoire finale `poids-image-seule-spectre-robuste-bruit-vNNN-*`, sauvegardes
immuables par extrait `poids-spectre-zone-bruit-vNNN-*`. Journal de progression
par cible, contrôles mémoire avant lecture et traitement, délai fini.
Aucune application des paramètres au débruiteur ni export TIFF.

## 171. Diagnostic du contraste et de l'identifiabilité spectrale — 27 octobre 2026

Après §170, `tests/study_noise_spectral_identifiability.py` conserve les
14 extraits et leurs deux ancres externes, sans modifier l'estimateur.
Vérifier le rejeu des poids A à 1e−12 et des covariances d'ancres à 1e−10.
Contrôler explicitement les conventions : covariance périodique complète
centrée en indice0, bloc de lags±8 centré en indice8. Les deux normalisations
doivent produire le même bloc. Préserver les 34 abstentions dans le parent,
sans les convertir en estimations dans cette étude conditionnelle.

Comparer quatre diagnostics, qui ne sont pas quatre candidats de production :

- Poids depuis A et depuis D=(A−B)/sqrt(2), mêmes ancres et contraste gelés.
  D peut contenir mouvements/dérives ; il n'est pas une vérité propre garantie.
- Réponses spectrales moyennes idéales : segment construit au poids témoin
  oracle déjà archivé, puis modèle spectral estimé depuis D. Ajuster le même
  contraste au logarithme de ces réponses, sans appeler ce calcul l'espérance
  du contraste non linéaire sur un périodogramme aléatoire.
- Seize champs gaussiens réels par extrait issus du segment oracle exact.
  Conserver covariance inter-axes et corrélations spatiales par racine
  hermitienne de la PSD complète ; ne pas simuler trois axes indépendants.
- Seize autres champs depuis le modèle PSD de D, mêmes contraintes.
  Ce modèle est estimé sur D : expérience de diagnostic, pas validation
  indépendante de la PSD vraie ou de la gaussianité du bruit natif.

Graine fixe20261027405, 448 champs en tout, toutes les répétitions archivées.
Mesurer poids, dispersion et erreurs covariance/HH par répétition. Les témoins
oracles servent uniquement à construire le diagnostic simulé, jamais à régler
un poids utilisable en photo. Ne pas transformer leurs écarts en seuil de
confiance, ni relâcher les critères précédents. Seize répétitions ne calibrent
pas une garantie statistique. Aucun nouveau débruiteur ou balayage de force.

Quatre tests préalables : covariance jointe simulée, rejet PSD invalide,
réponse idéale de segment et cohérence des conventions de lags.
Mémoire `diagnostic-identifiabilite-spectrale-bruit-vNNN-*`, sauvegardes
`diagnostic-spectre-zone-bruit-vNNN-*`. Garde mémoire2GiB avant chargement,
1GiB par cible, progression/sauvegarde par extrait et délai fini30 minutes.
Ni TIFF ni changement de production.

## 172. Ablation des coins du spectre — 27 octobre 2026

Après §171, `tests/study_noise_spectral_identifiability.py --corners`
ne rejoue pas les simulations. Seule modification expérimentale : supprimer
la limite radiale supérieure0,5 cycle/pixel du support spectral. Garder
le rayon minimal0,125, le même demi-plan (ligne Nyquist toujours omise),
les profils, le retrait affine, la fenêtre, le contraste, l'amplitude,
la grille129 et le raffinement32. Inclure ainsi les coins hautes fréquences,
sans prétendre que cette extension soit une pondération optimale pour HH.

Rejouer d'abord les poids circulaires archivés sur A à1e−12. Comparer les
poids étendus sur les14 mêmes cibles, avec34 abstentions héritées. Conserver
les84 mêmes injections, y compris56 rayures. D sert uniquement de référence
et de diagnostic. Aucun poids oracle ni nouveau seuil dans l'estimation.
Critères inchangés : amélioration covariance/HH face au fixe et sans
régression face au voisin pour chaque capture ; contamination sous rayures
au plus+10 %, toutes les estimations disponibles. Pas de sélection a posteriori
entre masque circulaire et masque étendu pour une capture particulière.

Cinq tests préalables passent, dont extension effective du support sans
mutation des pixels. Garde mémoire2GiB avant préparation et1GiB par cible,
progression par extrait, délai30 minutes. Mémoire finale immuable
`ablation-coins-spectre-bruit-vNNN-*`, tous les états et sources conservés.
Étude séquentielle sur les mêmes quatre captures, pas validation indépendante
ni autorisation d'intégration. Aucune image ou modification de production.

## 173. Information inter-axes dans les spectres croisés — 27 octobre 2026

`tests/study_noise_cross_spectral_information.py` réutilise les14 cibles,
34 abstentions et ancres externes du§172. Conserver les contrôles de
reconstruction des ancres, les garde-mémoire et le support étendu du§172.
La référence est ici la PSD stationnaire estimée depuis D, pas le
périodogramme de A : **diagnostic idéal inaccessible en photo seule**.

Calculer, pour les trois paires d'axes, la cohérence complexe
`K_ij(f)=S_ij(f)/sqrt(S_ii(f)*S_jj(f))`. Elle conserve phase et module,
et ne change pas sous des gains réels strictement positifs par axe.
Sur les fréquences communes à puissance marginale>1e−10 pour tous les
modèles, comparer la cohérence du mélange convexe des deux spectres
normalisés en variance à celle de la référence. Minimiser la moyenne
des distances complexes au carré, grille129 puis32 raffinements.
S'abstenir si la distance quadratique moyenne des cohérences des deux
ancres est<1e−12 ; ce seuil numérique ne constitue pas une confiance.

Conserver les courbes complètes du contraste, les poids et les erreurs
covariance/HH. Contrôle parallèle : contraste marginal MAD des
log-puissances sur la même référence stationnaire et le même support,
sans fenêtre d'observation. Il diffère donc des estimations photo§170–172.
Comparer aux critères fixe/voisin inchangés, sans sélectionner le meilleur
critère par capture ni chercher un poids de mélange entre les contrastes.

Le contraste croisé et le contraste marginal utilisent des pertes
différentes : leur comparaison ne sépare pas strictement l'effet des
variables et celui de la perte. Une réussite ne prouverait ni robustesse
aux textures ni inférence A-only. Un échec ne condamne pas toute approche
conjointe. Le produit extérieur d'un seul coefficient fréquentiel est
de rang1 ; estimer une cohérence en photo demanderait un moyennage adapté.

Quatre tests : récupération d'un poids avec diagonales identiques et
termes croisés distincts, invariance aux gains, abstention sans information
croisée, rejet d'une PSD invalide. Mémoire immuable
`information-spectres-croises-bruit-vNNN-*`, sources/parents/poids/courbes
et exclusions conservés. Pas de simulation supplémentaire, d'export TIFF,
de changement de production ou d'ajustement des seuils après résultat.

## 174. Attribution fréquentielle des résidus HH — 27 octobre 2026

`tests/study_noise_hh_frequency_attribution.py` ne calcule aucun nouveau
poids. Réutiliser les cinq vecteurs archivés : A circulaire, A étendu,
cohérence idéale, marginal idéal et témoin oracle conjoint. Garder14 cibles
et34 abstentions, ancres reconstruites et référenceD. Le témoin oracle
reste un diagnostic inaccessible en photo seule, pas une cible à recopier.

Pour chaque axe, normaliser la covariance à variance marginale1. La
réponse en puissance du filtreHH2×2 orthonormal est
`W(fx,fy)=4 sin²(pi fx) sin²(pi fy)`. Vérifier
`variance_HH = moyenne(S(f)*W(f)) = hᵀCh` à1e−10 relatif.
Le signe de `moyenne((S_prédit−S_référence)*W)` doit être conservé.

Partition fixe, exhaustive et disjointe de la grille complète : rayon<0,125 ;
0,125≤rayon<0,25 dans le support conjugué ; 0,25≤rayon≤0,5 ; coins>0,5 ;
fréquences restantes de Nyquist omises par le demi-plan. Les quatre premières
régions hors basses fréquences utilisent le support et son conjugué ; isoler
la région omise plutôt que la compter deux fois. Aucun choix de frontières
en fonction d'une capture. La somme des contributions signées retrouve
la différence de varianceHH ; **l'erreur logarithmiqueHH n'est pas additive**.

Conserver aussi l'intégrale du résidu spectral absolu pondéréHH par région
pour révéler les compensations, les fractions de varianceHH de référence,
la dérivée analytique par rapport au poids et les résidus log-PSD régionaux.
Une médiane régionale n'est pas une contribution additive au contrasteMAD.
Décomposer la MSE de covariance sur les lags±8 en termes diagonaux et
inter-axes, avec le même dénominateur global : leur somme doit être la MSE.

Rejouer les métriques archivées des quatre modes non oracles à1e−12.
Trois tests : variance blanche/partition, équivalence Parseval/hᵀCh sur
covariance corrélée, linéarité des contributions signées. Aucun ajustement,
simulation, seuil ou rendu. Mémoire `attribution-frequentielle-hh-bruit-vNNN-*`,
avec poids figés, exclusions, détails par axe/région, sources et parents.
Garde mémoire et délai fini identiques aux diagnostics précédents.

## 175. Moment spectral robuste pondéré HH — 27 octobre 2026

`tests/study_noise_hh_robust_moment.py` utilise
`tests/noise_hh_robust_moment.py`. Même14 cibles,34 abstentions, ancres
externes, support étendu, fenêtre et retrait affine. Seul le contraste
de choix du poids change. Le critère covariance reste un contrôle externe
inchangé, pas une contrainte intégrée à l'optimisation.

Pour chaque poidsw, réponse modèleP_w après fenêtre/projection ; échelle
nuisance par axe `s=exp(médiane(log(I/P_w)))/ln(2)`. Limiter le rapport
`u=I/(s*P_w)` par `min(u,c)`, avec `c=−ln(0,05)` fixé avant calcul.
SousX exponentiel de moyenne1, `E[min(X,c)]=1−exp(−c)=0,95`.
AvecW réponseHH, minimiser la somme des carrés des trois moments
`m=sum(W*P_w*(min(u,c)/0,95−1))/sum(W*P_w)`.
Grille129 puis32 raffinements, frontières conservées.

La calibration est exacte pour ce modèle exponentiel, pas pour l'ensemble
des coefficients RCD natifs corrélés et fenêtrés. Le score pondère un
périodogramme fenêtré et ne reproduit pas exactement la varianceHH native.
L'échelle médiane et le moment borné restent couplés, avec biais fini possible.
Aucun poids/sigma/résiduD ne guide le fit sur A. Abstention de modèles
proportionnels au seuil numérique1e−4 inchangé, sans confiance calibrée.

Conserver les84 injections, critères fixe/voisin et contamination+10 %.
Ajouter quatre champs gaussiens conjoints par cible (56 au total), graine
20261027413, depuis le segment au témoin oracle archivé. Le témoin sert
uniquement à simuler le modèle connu, jamais à régler l'estimateur sur A.
Quatre tirages ne valident pas une confiance. Quatre tests préalables :
calibration analytique/numérique, récupération synthétique et invariance
d'échelle, abstention, rejet des entrées invalides.

Mémoire `moment-spectral-hh-robuste-bruit-vNNN-*`, sauvegardes
`moment-hh-zone-bruit-vNNN-*`, courbes, amplitudes, simulations et provenance
conservées. Garde mémoire et délai30 minutes. Étude séquentielle sur le
corpus existant, pas validation indépendante, génération TIFF ou production.

## 176. Isoler le couplage poids/amplitude — 27 octobre 2026

`tests/study_noise_weight_amplitude_coupling.py` reprend exactement les
56 champs gaussiens du§175, même graine et ordre, sans nouvelle réalisation.
Rejouer les poids archivés à1e−12 avant de comparer des variantes.
Les14 cibles et ancres restent celles du parent ; les34 abstentions sont
conservées dans celui-ci, pas converties en nouveaux cas mesurables.

Sur chaque champ, garder contrasteHH, plafond, support et optimisation
inchangés. Comparer trois traitements de l'amplitude : nuisance recalculée
pour chaque poids (résultat archivé), variance de population1 imposée,
et variance médiane estimée au poids injecté puis gelée pour tous les poids.
Ce troisième cas dépend lui aussi d'une vérité inaccessible en photo seule.
Stocker courbes et écarts appariés au poids injecté ; comparer par capture,
sans retenir seulement les réalisations favorables.

Vérifier analytiquement que le moment de population est nul au bon poids
et à la bonne échelle sous l'hypothèse exponentielle. Cela ne prouve ni
l'unicité du minimum, ni la calibration pour les coefficients corrélés
et fenêtrés. Trois tests préalables : moment théorique nul, invariance
aux gains avec échelle connue, rejet d'une variance invalide.

Mémoire `couplage-poids-amplitude-bruit-vNNN-*`, sources, parents et56
comparaisons conservés. Garde mémoire, progression par extrait et délai
fini30 minutes. Quatre réalisations par cible, pas un intervalle de confiance.
Un gain avec amplitude oracle ne devient pas un correctif utilisable en
production ; pas de nouvelles captures, TIFF ou modifications moteur.

## 177. Amplitude spatiale robuste figée avant le poids — 27 octobre 2026

`tests/study_noise_spatial_fixed_scale.py` emploie
`tests/noise_spatial_fixed_scale.py` et le lanceur d'épreuves partagé du§175.
Une seule ablation : remplacer l'amplitude recalculée à chaque poids par
une amplitude calculée sur les pixels avant optimisation. Retirer le plan
affine par projection orthogonale puis calculer, par axe,
`sigma=médiane(abs(résidu−médiane(résidu)))/0,6744897501960817`.
Ce facteur est la calibrationMAD d'une marginale gaussienne ; pas une
correction empirique propre à une capture. Aucune correction de projection
ou de corrélation dépendant du profil. Une échelle<1e−12 s'abstient.

Geler sigma² dans le contrasteHH borné du§175, sans changer support,
plafond, grille ou raffinement. Même14 cibles,34 abstentions,84 injections,
et56 réalisations de modèle connu avec la même graine/ordre. Les critères
covariance/HH et contamination+10 % restent inchangés. Aucune informationD,
poids vrai ou amplitude oracle dans l'estimation sur A.

Indépendance du **poids** ne veut pas dire indépendance statistique des
données : l'amplitude et le contraste utilisent la même zone. MAD spatial
peut mesurer scène+bruit, des textures larges subsistent après retrait du
plan et la projection peut atténuer du bruit basse fréquence. La bonne
réponse sur bruit blanc n'est pas une preuve de validité photographique.

Quatre tests préalables : échelle blanche connue, invariance affine et
aux gains, abstention d'un plan seul, rejet de non-fini. Mémoire
`amplitude-spatiale-figee-bruit-vNNN-*`, sauvegardes
`amplitude-spatiale-zone-bruit-vNNN-*`. Sources du lanceur partagé et de
l'estimateur archivées, garde mémoire, progression et délai30 minutes.
Pas de retouche des facteurs après résultat ni d'intégration moteur.

## 178. Budget du retrait affine et variabilité MAD — 27 octobre 2026

`tests/study_noise_affine_scale_bias.py` rejoue exactement les56 champs
du§177 (MAD archivées à1e−12). Aucun nouveau poids ni tirage. Calculer
le budget analytique du projecteur `Q=I−UUᵀ`, U portant les trois tendances
orthonormales : `diag(QCQ)` et `trace(QCQ)/N=C(0)−trace(UᵀCU)/N`.
Vérifier la cohérence moyenne/diagonale et garder leur dispersion spatiale.
Comparer MAD centrée avant/après, RMS avant/après et MAD divisée par
l'atténuationRMS théorique, exclusivement comme diagnostic oracle.
Une correctionRMS n'est pas une calibration exacte de la MAD non linéaire.

Trois tests : trace blanche1−3/N, opérateur dense corrélé, idempotence
et orthogonalité du projecteur. Conserver aussi les mesures nativesA/D
sans considérerD comme vérité propre. Mémoire
`biais-projection-amplitude-bruit-vNNN-*`, tous les56 résultats et sources.
Pas d'ajustement de coefficient ni de correction production.

## 179. Sensibilité du poids à de petites erreurs d'amplitude — 27 octobre 2026

`tests/study_noise_weight_scale_sensitivity.py` rejoue les56 mêmes champs
et contrôle le poids à amplitude exacte contre§176 à1e−12. Conserver
modèles, plafond, support, grille et raffinement. Pas de nouveau tirage.
Imposer dix scénarios fixés : erreur commune de sigma−2 %,−1 %,+1 %,+2 %,
puis−2 %/+2 % sur chacun des trois axes séparément. Convertir en variance
par `(1+erreur_sigma)²`, pas par `1+erreur_sigma`.

Conserver les616 ajustements (56 références et560 perturbations), courbes,
poids, déplacements appariés et erreurs au poids injecté. Aucune sélection
du signe favorable, aucun seuil de réussite inventé après calcul. Les
scénarios ne sont pas une loi probabiliste des erreurs d'une vraie photo.
Les frontières0/1 et changements de minimum empêchent d'assimiler les
différences finies à une dérivée locale garantie.

Trois tests : dix scénarios distincts/prédéfinis, conversion amplitude/variance,
rejet des erreurs invalides. Mémoire `sensibilite-poids-amplitude-bruit-vNNN-*`,
sauvegardes `sensibilite-amplitude-zone-bruit-vNNN-*`, garde mémoire et délai
fini30 minutes. Diagnostic oracle, pas nouvelle estimation d'amplitude,
amélioration photographique ou modification moteur.

## 180. Impact des poids archivés sur covariance et HH — 27 octobre 2026

`tests/study_noise_scale_profile_impact.py` lit les616 poids du§179 et
les ancres de covariance aux lags±8. Aucun nouveau champ ou ajustement.
La référence est le segment connu au poids injecté dans les simulations,
pas la covariance mesurée surD d'une photo native.

Séparer la covariance de forme normalisée à variance marginale1 et la
covariance physique `diag(1+erreur_sigma)*C_forme*diag(1+erreur_sigma)`.
Conserver MSE de forme, MSE physique, erreursHH et rapports
`sqrt(varHH_forme/varHH_vraie)` puis ce rapport multiplié par
`1+erreur_sigma`, axe par axe. Rapports orientés **prédit/vrai** ; ne pas
les confondre avec le facteur de transport inverse des anciens tableaux.
Comparer chaque perturbation à la référence du même champ à amplitude exacte.

Conserver tous les scénarios, répétitions et axes. Ne pas interpréter un
grand déplacement de poids comme une erreur visuelle ; ne pas ignorer
la covariance si une compensation amplitude/forme gardeHH proche.
Pas de seuil de validation nouvellement choisi sur les résultats.
Quatre tests : erreur nulle au modèle vrai, poids sans effet entre profils
identiques, effet amplitude séparé de la forme, rejet d'un poids invalide.
Mémoire `impact-poids-profils-bruit-vNNN-*`, sources/ancres/poids/métriques
et provenance conservés. Diagnostic de modèle, pas débruitage ou validation
photographique ; ni TIFF ni correction de production.

## 181. Contrôle conjoint des mesures et enveloppe échantillonnée — 27 octobre 2026

`tests/study_noise_joint_measurement_control.py` réutilise les616 profils
du§180, sans ajustement ou simulation. Comparer chacune des560 perturbations
à sa référence appariée, avec les erreurs au modèle connu : MSE covariance
et moyenne des valeurs absolues des logarithmes des rapportsHH. Distinguer
forme normalisée et profil total avec amplitude.

Classer les variations : meilleures ensemble, pires ensemble,HH meilleur
mais covariance pire, inversement, ou numériquement inchangées. Tolérance
de comparaison1e−12 uniquement numérique, jamais un seuil d'acceptabilité.
Conserver tous les deltas pour ne pas réduire les erreurs à un verdict.

Pour chaque champ, calculer aussi le maximum échantillonné de la distance
de covariance au profil de référence et du changementHH sur les dix
scénarios. Cette enveloppe n'est ni un supremum sur toutes les erreurs
d'amplitude possibles ni un intervalle de confiance. Stabilité relative
ne prouve pas la justesse absolue. Le classement des erreurs dépend de
la vérité des simulations : aucun garde-fou A-only n'est livré.

Quatre tests : invariance du logarithme absolu au rapport inverse,
classement conjoint/compromis, tolérance numérique, rejet d'un rapport
invalide. Mémoire `controle-conjoint-mesures-bruit-vNNN-*`, détails et
comptages par scénario conservés. Ni seuil choisi après calcul, nouveau
tirage, TIFF, correction moteur ou validation photographique.

## 182. Faisabilité d'indicateurs observables de fiabilité — 27 octobre 2026

`tests/study_noise_observable_reliability.py` réutilise les616 ajustements
du§179 et leurs erreurs de modèle§180, sans optimisation, simulation,
apprentissage ou choix de seuil. Extraire trois scores des seules courbes
de contraste : perte minimale, opposé de l'étendue des pertes sur la grille,
opposé de la courbure centrée au meilleur point de grille. Pour cette
dernière, minima de bord explicitement manquants, pas de valeur inventée.

Les signes des scores sont choisis avant calcul : valeur élevée supposée
indiquer moins de fiabilité. Comparer leurs rangs aux erreurs de covariance
normalisée, covariance physique et logarithme absoluHH total. Corrélation
de rangSpearman avec rangs moyens des égalités exactes ; si score/erreur
constant, résultat absent. Ne pas calculer de p-valeur indépendante sur
des perturbations du même champ. Présenter ensemble,56 références seules,
et chaque capture séparément, sans choisir après coup le sous-groupe favorable.

Les scores n'utilisent pas la vérité, mais les amplitudes des ajustements
proviennent ici de perturbations oracles : ce n'est pas une calibration
native A-only. Le résidu est calculé sur les données d'ajustement ; faible
résidu ne garantit pas profil juste. Étendue et courbure dépendent des
ancres et de la paramétrisation. Aucun score n'est une confiance calibrée.

Quatre tests : égalités de rang, direction/constance, absence de courbure
aux bords, courbure quadratique intérieure. Mémoire
`indicateurs-observables-fiabilite-bruit-vNNN-*`, scores, erreurs, exclusions,
dénombrements et provenance conservés. Pas de seuil d'abstention, nouvelle
estimation de bruit, TIFF ou modification de production.

## 183. Contrôle prédictif sur champs séparés — 27 octobre 2026

`tests/study_noise_held_predictive_control.py` ne réajuste aucun des616
profils. Générer56 champs de validation gaussiens conjoints depuis les
mêmes14 segments connus, graine séparée20261027429, distincte de celle
des données d'ajustement20261027413. Indépendance pseudo-aléatoire
conditionnelle au modèle, pas validation indépendante de boîtier/capture.

Sur chaque champ de validation128, covariance périodique brute aux lags±8
et varianceHH, sans projectionPSD ni centrage ajusté : la moyenne de
population est connue nulle dans cette simulation. Ce champ sans scène
n'est pas disponible tel quel sur une photo texturée. Comparer les profils
par MSE covariance physique et erreur logarithmiqueHH, avec le même champ
de validation pour la référence appariée et ses dix perturbations.

Vérifier l'identité de fluctuation de la différence des MSE : erreur
mesurée moins erreur vraie appariée égale
`−2*moyenne((C_candidat−C_référence)*(C_mesurée−C_vraie))`.
Conserver les différences, classes conjointes, erreurs vraies pour audit,
covariances mesurées et comptages par modèle. Pas de nouveau seuil.
La tolérance1e−12 des classes reste numérique, pas une signification
statistique ; les560 comparaisons partagent56 champs et ne sont pas
560 validations indépendantes.

Trois tests : HH empirique égal au filtre périodique, identité appariée,
score nul quand profil et mesure sont identiques. Mémoire
`controle-predictif-separe-bruit-vNNN-*`, sources, graines, parents et
mesures conservés. Garde mémoire et délai30 minutes. Ni nouveau
débruiteur, estimation native, TIFF ou modification moteur.

## 184. Incertitude appariée conditionnelle au modèle gaussien — 27 octobre 2026

`tests/study_noise_paired_uncertainty.py` réutilise les champs mesurés§183
et les profils figés. Aucun tirage ou ajustement. Adapter les anciens
budgets gaussiens à la **différence appariée** de MSE, pas à deux erreurs
absolues supposées indépendantes. Son terme fluctuant est une forme
quadratique de noyau circulant construit depuis la différence des profils.

Pour un champ gaussien réel, calcul exact de variance en Fourier :
`Var(q)=2*sum_f trace(A_f*S_f*A_f*S_f)/N²`, oùN est le nombre de pixels
etA inclut la normalisation de la MSE et le facteur−2 de la différence.
Vérifier la convention contre une matrice dense à petite taille.
Calculer aussi la variance des trois mesuresHH, en gardant la dépendance
spatiale du modèle. La PSD utilisée est ici la **vraie PSD du modèle**,
pas une covariance estimée sur la photo.

Fixer avant calculalpha=0,05 par comparaison. Chebyshev et union de quatre
événements donnent un multiplicateur`sqrt(4/alpha)`≈8,944 pour la
différence de covariance et les trois variancesHH. Propager les intervalles
de varianceHH dans la différence des erreurs absolues logarithmiques,
en utilisant la monotonie par axe et la limite quand la variance tend0.
Ne pas linéariser la valeur absolue à son coude.

Soutenir une amélioration conjointe seulement si les deux bornes supérieures
des différences sont strictement négatives, une dégradation seulement si
les deux bornes inférieures sont positives ; sinon s'abstenir.
La couverture théorique est conditionnelle au modèle connu et par
comparaison, pas familiale sur560 comparaisons. Ne pas appeler Chebyshev
un intervalle gaussien de typez ou une confiance native calibrée.

Trois tests : variance Fourier/dense, encadrement de la différenceHH,
abstention si0 reste dans un intervalle. Mémoire
`incertitude-pairee-controle-bruit-vNNN-*`, bornes, variances, décisions,
couvertures diagnostiques et sources conservées. Pas d'ajustementalpha
après observation, nouvelle image ou correction moteur.

## 185. Concentration gaussienne pour les différences signées — 27 octobre 2026

`tests/study_noise_gaussian_concentration.py` réutilise le lanceur§184,
les mêmes mesures et profils ; seule la borne change. Risquealpha0,05
par comparaison inchangé, huit queues (deux pour covariance et troisHH),
`x=ln(8/alpha)`. Pas de scorez ni de réglage rétrospectif du risque.

Référence pour le cas positif : Hsu, Kakade et Zhang,2012,
[A tail inequality for quadratic forms of subgaussian random vectors](https://www.cs.columbia.edu/~djhsu/papers/quadratic-ecp.pdf),
proposition1.1. Son énoncé positif ne doit pas être appliqué directement
à une différence signée. Pour celle-ci, dérivation gaussienne ci-dessous.

Après blanchiment théorique, forme centrée
`q−E(q)=sum(lambda_i*(z_i²−1))`, avecz indépendants normaux standard.
Sa log-fonction génératrice est
`sum(−t*lambda_i−0,5*log(1−2*t*lambda_i))`.
Pourt positif dans le domaine, majoration par
`t²*sum(lambda_i²)/(1−2*t*max(0,max(lambda)))` : les termes négatifs
sont majorés par leur terme quadratique, les positifs par la série
géométrique. Chernoff donne la déviation supérieure
`2*sqrt(sum(lambda_i²)*x)+2*max(0,max(lambda))*x`.
Pour la queue inférieure, appliquer le même raisonnement à−lambda.
L'union des huit queues garde le risque conditionnelalpha par comparaison.

Calculer les valeurs propres de `S_f^(1/2)*A_f*S_f^(1/2)/N` et contrôler
`2*sum(lambda²)` contre la variance analytique§184. Les fréquences
conjuguées comptent la multiplicité réelle de l'opérateur, pas de nouvelles
observations indépendantes. PourHH d'un axe, valeurs propres
`W(f)*S_aa(f)/N`, positives. Inverser correctement les queues asymétriques
pour encadrer l'espérance depuis la mesure : mesure moins déviation
supérieure, mesure plus déviation inférieure. PropagationHH et abstention
conjointe inchangées.

Quatre nouveaux tests : majoration de logMGF signée, échange des queues
sous changement de signe, cas nul/positif, identité variance valeurs
propres/trace ; trois tests§184 conservés. Mémoire
`concentration-pairee-controle-bruit-vNNN-*`, budgets spectraux et sources.
PSD vraie oracle, moyenne nulle et gaussianité stationnaire restent
nécessaires ; aucune couverture native ou familiale sur560 cas n'est validée.
Pas de nouveau tirage, ajustement, TIFF ou correction moteur.

## 186. Budget idéal de support de validation — 27 octobre 2026

`tests/study_noise_validation_support_budget.py` ne génère aucun champ.
Réutiliser profils et budgets de concentration§185. Hypothèse explicite :
moyenne deR blocs128 indépendants et de même covariance. Les valeurs propres
du problème moyenné sont celles d'un bloc divisées parR, répétéesR fois :
`sum(lambda²)` est divisée parR et les extrema parR. Conserveralpha0,05,
les huit queues et les formules de propagationHH.

ExaminerR fixé avant calcul dans{1,4,16,64,256}. Centrer les intervalles
sur les **espérances vraies** des mesures, pas sur une moyenne empirique
imaginaire. Classer la possibilité de décision à cette moyenne idéale.
Ce n'est ni une puissance statistique ni une garantie qu'une réalisation
déciderait. Stocker le premierR testé donnant une décision, sans le nommer
minimum exact ou budget validé. Les compromis vrais entre critères peuvent
rester en abstention même avec beaucoup de données.

Le nombreR×16384 est un nombre nominal de pixels de blocs indépendants,
pas l'équivalent garanti d'une région contiguë d'une photo. Homogénéité,
absence de scène et indépendance ne sont pas assurées par un simple
quadrillage. La PSD vraie reste une entrée oracle.

Trois tests : comparaison aux valeurs propres répétées/divisées, décroissance
des rayons sans changement du risque, rejet deR invalide. Mémoire
`budget-support-validation-bruit-vNNN-*`, cinq budgets par comparaison,
graines/parents historiques et sources conservés. Garde mémoire et délai
fini30 minutes ; pas de nouvelle image, simulation, ajustement ou production.

## 187. Campagne multi-supports réellement simulée — 27 octobre 2026

`tests/study_noise_pooled_validation.py` conserve les616 profils et le
risquealpha0,05 par comparaison. Générer56 pools de64 champs gaussiens
conjoints128 indépendants dans chaque pool, mêmes modèles connus, nouvelle
graine20261027439.3584 nouveaux champs, traités en flux ; aucun TIFF.
Conserver covariances spatiales et inter-axes. Réutiliser exactement les
mêmes champs pour comparer les scénarios suivants, fixés avant calcul :

- homogène :64 blocs du bruit cible ; relevés supplémentaires sur les
  préfixes1 et16 pour comparer les supports réellement mesurés ;
- niveau hétérogène : première moitié à sigma×1,05, seconde moitié inchangée ;
- rayures diagonales : premier quart, puis première moitié des supports
  contaminés, sinusoïde de période4 suivant la diagonale45°, amplitude
  crête0,5 sur l'axe0 ;
- texture large : premier quart contaminé sur l'axe0, texture déterministe
  issue d'un bruit blanc filtré gaussien de rayon sigma1,5, recentrée et
  normalisée àRMS0,5, graine20261027438.

Comparer le pool naïf64 au sous-ensemble propre **oracle**, indices de
contamination connus à l'avance, sans sélection sur les valeurs observées.
Ce contrôle garde32 ou48 supports selon le scénario ; il ne livre aucun
détecteur de texture ou sélecteur de zones. Le bruit cible reste celui de
la région locale initiale : changer la moyenne des niveaux du pool n'est
pas automatiquement une amélioration de sa justesse locale.

Accumuler les spectres croisés en flux puis moyenner avant retour aux
lags±8. Conserver scores covariance/HH, bornes§185 réduites selon le nombre
de supports, décisions, erreurs au modèle cible et moyennesHH. Les bornes
àPSD homogène restent intentionnellement appliquées aux pools naïfs
contaminés pour éprouver leur fragilité : **hypothèses invalides** pour
ces mélanges, ne pas annoncer une couverture théorique à5 % sur ceux-ci.
Les sous-ensembles propres ont une sélection fixée indépendamment du bruit.

Quatre tests : effectifs de contamination, accumulation spectrale égale
à la moyenne des covariances, texture reproductible àRMS fixé, rejet d'un
pool vide. Mesures par bloc/pool et scénarios conservées, empreintes des
textures, graines, sources et parents. Sauvegardes immuables après chaque
pool `validation-pool-zone-bruit-vNNN-*`, mémoire finale
`validation-multi-supports-bruit-vNNN-*`. Garde mémoire1GiB par pool,
progression tous les16 blocs et délai30 minutes. Pas de nouvelle photo,
retouche de profil, calibration native ou production.

## 188. Recentrage sur une rafale réelle et données réservées — 27 octobre 2026

Après le retour critique externe, ne pas ajouter une nouvelle variante aux
mêmes quatre captures comme si elle apportait une validation indépendante.
Conserver les preuves, les limites du modèle spectral et le reconstructeur
expérimental ; repartir d'un nouvel échantillon réel pour qualifier les mesures.

`tests/study_noise_burst_audit.py` vérifie les 42 CR3 de `colorimétrie/rafale`,
fige leurs SHA/métadonnées puis la partition chronologique 14 développement,
14 validation, 14 réserve finale. Les pixels des deux derniers groupes restent
fermés jusqu'au gel d'une méthode et de ses critères. Une telle partition ne
crée pas une nouvelle scène ni un autre boîtier. Les 14 premières prises seules
sont décodées ; neuf zones mosaïques 256×256 sont fixées aux fractions
0,2/0,5/0,8 de chaque axe, aux coordonnées CFA paires.

Conserver séparément les quatre phases R/G1/G2/B, moyennes par prise,
variance temporelle, variance spatiale de la moyenne, différence entre deux
demi-empilements et statistiques de saturation. La composante commune n'est
pas identifiée comme bruit fixe. La corrélation temporelle après centrage sur
14 prises possède un biais fini, à ne pas interpréter comme bruit physique.
Les niveaux noirs rawpy ne remplacent pas ceux du chemin d'ouverture IPR.

`tests/study_noise_burst_motion.py` estime un mouvement global sur des
prévisualisations de développement : moyenne CFA2×2, largeur800, 500 points
Shi–Tomasi au maximum, suivi LK et affine partielle RANSAC. Aucune interpolation
des pixels mesurés. Ne pas accepter une moyenne comme vérité propre si le
mouvement, le scintillement ou la parallaxe ne sont pas maîtrisés.

`tests/extract_noise_burst_native.py` utilise le probe existant pour extraire
neuf zones256 fixes après l'ouverture native Camera Neutral calibrée, optique
désactivée, Rec.2020 linéaire, RCD/médiane FlatOnly. Stocker les plans Y/Co/Cg
en mémoire scientifique et les déclarations, pas des TIFF pour analyse.
Les grilles RAW et native sont orientées différemment : ce ne sont pas des
paires de pixels pour mesurer une matrice de transport.

## 189. Diagnostic du mouvement et témoin de débruitage réel — 27 octobre 2026

`tests/study_noise_burst_residuals.py` compare la covariance temporelle brute
à celle des résidus projetés hors d'un sous-espace temporel fixe : constante,
translation X/Y, coefficients d'échelle/rotation de l'affine globale.
Normaliser les colonnes centrées, garder les valeurs singulières supérieures
à `1e-8` du maximum ; diviser l'énergie résiduelle par `N−rang`.
Ce correctif de degrés de liberté n'est exact que pour un design fixe et des
erreurs temporelles indépendantes homoscédastiques. Les paramètres de mouvement
proviennent ici des images : c'est un diagnostic, pas une vérité indépendante.

Mesurer covariance inter-canaux, lags spatiaux1/2/4 et variances des tuiles32.
Une forte baisse après projection révèle une mesure sensible au mouvement ;
elle ne prouve pas que tout ce qui reste est du bruit. Aucun ajustement de
`a,b`, seuil de production ou interprétation de motif fixe à ce stade.

Témoin suivant, figé avant ses scores : ouverture native puis débruiteur
**de production actuel** à0/50/100, luminance et couleur conjointes, sur les
prises de développement d'indices0/4/8/13. Ne pas changer sa force ou ses
poids pour optimiser cette rafale. L'outil isolé `tests/burst_tools` réutilise
la bibliothèque moteur déjà construite ; il n'édite ni le moteur ni le CMake
principal. Vérifier que ses pixels à0 correspondent à l'extraction native.
Le recalage éventuel d'une référence devra être tracé et ne servira pas à
qualifier un bruit capteur après interpolation. Les prises réservées ne sont
pas ouvertes tant qu'une méthode et un critère de validation ne sont pas gelés.

## 190. Validation préalable des rapports entre bandes — 28 octobre 2026

`tests/study_noise_burst_bands.py` mesure sur les résidus les énergies pixel,
HaarHH, fine `I−G0,8`, moyenne `G0,8−G1,8`, en excluant8 pixels de bord.
Unités : DN² ou plan linéaire². Une variance HH, un MAD et une variance pixel
ne sont pas interchangeables pour un bruit corrélé.

`tests/study_noise_burst_validation.py` écrit un gel immuable avant lecture des
pixels réservés : zones natives0/2/5 choisies par géométrie, trois rapports
par plan (HH/pixel, fin/HH, moyen/fin),27 écarts relatifs≤10 %. Ce critère
exploratoire de répétabilité n'est ni une confiance statistique ni une
tolérance produit. Le script refuse de refaire un gel déjà présent.

Extraire les14 prises9833–9846 avec le même probe/recette, contrôler identités
et mouvement RAW suivant le même opérateur, puis mesurer sans réajuster.
`extract_noise_burst_native.py --partition validation --freeze CHEMIN` exige
ce gel et son empreinte du script. La partition scellée n'est pas acceptée.
Les14 prises9847–9860 restent fermées. Une même rafale ne vaut pas un autre
boîtier, une autre scène ou une autre séance.

## 191. Références croisées et contrôle de sélectivité — 28 octobre 2026

Les sorties évaluées ne sont pas interpolées. Recaler seulement les références,
affine globale obtenue sur les prévisualisations du même master, unités
converties. La transformation locale va du pixel cible vers le pixel source ;
vérifier son sens par test de translation. Marge cible32, intersection valide
de toutes les références, minimum80 % de recouvrement par zone/bloc, sinon
exclusion tracée. Aucun remplissage ni sous-ensemble favorable par pixel.

Deux groupes de références entrelacés servent au score par plan
`(sortie−réfA)·(sortie−réfB)`. Il ne vaut un risque à une vérité propre que si
les références sont indépendantes, non biaisées et alignées sur le même signal,
hypothèses non certifiées ici. Conserver différences des demi-références,
scores sans recalage, changements moyens et intensité de correction.
Les flous sigma4/16 sont des témoins de sélectivité, pas des candidats.

Si les contextes256 sont trop étroits, conserver les exclusions puis extraire
des512 aux mêmes centres avec `iprBurstReference` et
`tests/extract_noise_burst_reference_context.py`. Comparer chaque centre256
à son extraction antérieure, tolérance maximale2e-7. Passer de coordonnées
source256 à source512 ajoute128 aux deux translations locales. Aucun changement
de filtre pendant cette correction géométrique ; pas une validation indépendante.

## 192. Faisabilité B3/Wiener puis chrominance partagée — 28 octobre 2026

`tests/study_noise_burst_band_wiener.py` fixe un essai, pas une grille : quatre
niveaux B3-spline non décimés, noyau `[1,4,6,4,1]/16` à espacement doublé,
fenêtre d'énergie21×21, gain `max(0,1−varianceBruit/énergieLocale)`.
Vérifier la reconstruction télescopique, l'identité à bruit nul et restaurer
les moyennes pour le diagnostic. Variance par zone/bande sur13 prises de
développement **hors prise évaluée** ; références de score seulement dans
les14 prises de validation, distinctes de la calibration.

Ce témoin est conditionné par une rafale, pas déployable sur une photo inconnue.
Le bruit estimé peut contenir du mouvement. La validation a déjà servi au
contrôle des rapports entre bandes : ce ne sont plus des données finales
vierges ; les14 prises finales restent fermées.

Après régression Y, variante unique `tests/study_noise_burst_shared_chroma.py` :
garder exactement Y de production100, covariance2×2 Co/Cg par bande sur les13
prises de calibration, même gain aux deux plans. Avec `z=(Co,Cg)` et covariance
`N`, énergie locale `E[zᵀN⁻¹z]`, gain `max(0,1−2/énergie)` ; le2 est la
dimension chromatique. Le plancher spectral relatif1e-8 sécurise l'inversion,
ce n'est pas un seuil de qualité. Partager la direction par bande ne garantit
pas la teinte finale après restauration DC/gamut. Mesurer les RGB négatifs,
sans les masquer dans les scores.

Geler sources/paramètres/limites avant scores. Comparer à production0/50/100
et au premier témoin, conserver couverture, covariances, gains et pixels.
Tests : Y identique, données finies, reconstruction, bruit nul, moyennes et
covariance singulière. Aucune livraison ou stabilité produit implicite.

## 193. Contrat de sortie et clôture du témoin conditionné — 28 octobre 2026

`tests/project_noise_burst_shared_chroma.py` contrôle la sortie du témoin figé,
sans réajuster ses poids. Si `min(R,G,B)=m<0` en RGB Rec.2020 de travail,
multiplier Co/Cg par `Y/(Y−m)` vers le gris de mêmeY ; sinon, facteur1.
Marge d'arrondi4 epsilon float32 sur les pixels comprimés. Conserver Y et
le HDR, ne pas écrêter à1. Ne pas restaurer ensuite DC, ce qui pourrait
recréer les valeurs négatives ; mesurer les changements moyens introduits.
Ce contrat de travail n'est pas une conversion complète vers le gamut écran.

Mesurer minimum RGB, fraction de composantes sous−1e−7, fraction de pixels
comprimés, changement de score après projection et Y identique à production100.
Le seuil−1e−7 est écrit explicitement dans la mémoire, indépendamment du nom
du champ. Une correction de métadonnées conserve les nombres et sa filiation,
sans se présenter comme une nouvelle validation.

Ne pas multiplier les variantes sur cette rafale après ces résultats : figer
le témoin conditionné et qualifier l'estimation mono-image, le domaine RAW
des noirs et la propagation des covariances avant une intégration. Les14
prises finales restent scellées. Une future validation doit inclure autre
scène/séance/boîtier et détails purement chromatiques à luminance proche.
La conservation de Y ne prouve pas celle de tous les détails colorés.

`tests/audit_noise_burst_campaign.py` relit les preuves JSON, vérifie les
fichiers explicitement désignés par chemin/SHA, les liens récents du rapport,
les partitions décodées0–27 et les tests ciblés. Il ne décode aucun nouveau
pixel, ne nettoie rien et n'écrit pas dans Git. Un audit d'intégrité réussi
n'est pas une validation physique ni une stabilité produit.

## 194. Audit d'estimation depuis une seule photo — 28 octobre 2026

`tests/audit_noise_single_photo.py` fixe la prise de développement9819, sans
ouvrir la réserve finale. L'outil isolé `iprNoiseSingleAudit` ne reçoit que
ce RAW : même ouverture native, estimateur de production sur toute l'image,
trois modèles Haar/MAD selon Y, puis neuf zones256 aux coordonnées existantes.
Conserver les modèles, leurs domaines, les sigmas prédits et les MAD locaux.
Le CMake principal et le moteur ne sont pas modifiés.

Sur les mêmes pixels mono-image, mesurer les covariances spatiales observées
Co/Cg des quatre bandes B3, après centrage, intérieur192. C'est un témoin naïf
de contenu total, pas automatiquement une covariance de bruit. Aucune sélection
de zones ni facteur calibré avec la rafale ne produit ces estimations.

Écrire la prédiction immuable avant de charger les autres pixels pour le score.
Ensuite seulement, ouvrir les archives des13 autres prises de développement,
en excluant9819, mesurer les covariances temporelles de bandes avec le même
sous-espace de nuisance et les degrés de liberté corrigés. Comparer le modèle
de production au sigma temporel Haar, pas au sigma pixel ; comparer séparément
les matrices de bandes chromatiques. Vérifier que9819 correspond à l'ouverture
précédemment ancrée.

Conserver ratios de sigma, matrices complètes et erreurs de Frobenius par zone
et bande. Aucun seuil de conformité ou multiplicateur ajusté sur les résultats.
Scène déjà inspectée, pas une validation vierge. Références temporelles encore
imparfaites face au mouvement, au bruit commun et aux hypothèses de projection.
Cet audit n'implique ni rendu débruité, ni raccordement moteur, ni ouverture
des14 prises finales. Tests : champ constant, covariance positive semi-définie,
référence identique et refus d'une entrée contenant plusieurs images.

## 195. Diagnostic de sélection sur supports distincts — 28 octobre 2026

`tests/study_noise_single_support_selection.py` n'essaie pas de réintroduire
un quartile de faible énergie chromatique : les anciens essais (§54–58,
§400–408 du rapport) ont déjà montré ses limites de biais et de contamination.
La nouvelle question porte sur le découplage entre le signal utilisé pour
sélectionner et le support utilisé pour mesurer, sur les pixels réels de9819.

Dans chaque extrait256, garder l'intérieur192 et quatre tuiles32 aux origines
relatives `(0,0),(0,160),(160,0),(160,160)`. Classer les quatre supports par
énergie du gradient de Y après Gauss sigma4 ; garder les deux plus faibles,
égalité résolue par indice. Aucun seuil ajusté aux résultats. Comparer :

- les quatre supports, sans sélection ;
- les deux supports sélectionnés, Co/Cg mesurés au même endroit ;
- les deux supports opposés, indice `3−i`, Co/Cg mesurés loin du sélecteur.

Si les deux supports retenus sont opposés entre eux, cette dernière permutation
redonne le même ensemble : les résultats sont identiques, pas deux mesures
indépendantes. Conserver les indices pour reconnaître ces cas.

Mesurer les quatre matrices de bandes B3 après centrage spatial dans chaque
tuile, puis moyenner les matrices, pas des écarts-types. Écrire les choix et
prédictions avant de consulter d'autres prises. Le sélecteur ne reçoit que Y,
pas l'énergie Co/Cg à estimer. Cette séparation ne garantit cependant pas
l'indépendance de Y et des canaux de couleur, ni l'homogénéité du signal.

Pour le score, utiliser les13 autres prises, hors9819, et le sous-espace
temporel déjà défini. Appliquer exactement le même filtrage, centrage et les
mêmes géométries sélectionnées à la référence ; diviser les énergies par les
degrés de liberté temporels. Ne pas comparer la variance centrée d'une tuile32
à l'ancienne variance d'un champ192 : l'opérateur de mesure a changé.

Témoin complémentaire : appliquer le même sélecteur aux13 résidus temporels
réels, sans ajouter de texture. Conserver les rapports sélection/tous dans
chaque résidu. Ces résidus sont liés par la projection et peuvent être
spatialement hétéroscédastiques ou contenir du mouvement ; une baisse seule
ne prouve donc pas un biais de sélection statistique. Pas d'intervalle de
confiance ni de faux-positifs certifiés à partir de ces13 états.

Les variances estimées décrivent leurs supports, pas automatiquement chaque
pixel de l'extrait ni une autre luminosité. Quatre supports ne qualifient pas
un modèle par boîtier. Aucun nouveau RAW décodé, réserve finale intacte,
aucune modification moteur. Tests : choix déterministe, indépendance du
sélecteur aux valeurs Co/Cg, covariance positive et refus d'une pile d'images.

## 196. Qualification du domaine RAW et de l'interception — 28 octobre 2026

`tests/audit_noise_raw_domain.py` utilise seulement l'audit des paires v005 et
la PTC exploratoire v003 du22 octobre. Vérifier leur filiation par SHA, puis
reconstruire chaque point conservé : signal = moyenne RAW auditée − médiane
empirique des noirs du même ISO, variance = variance différentielle auditée.
Aucun nouvel ajustement, RAW décodé ou coefficient de production généré.

Pour `variance = a·s+b`, si l'on soustrait `B+δ` plutôt que `B`, alors
`s'=s−δ` et `b'=b+aδ`. Vérifier les prédictions invariantes sans imposer à
nouveau `b'≥0`. Refaire une régression contrainte dans une autre origine
changerait le problème ; ce n'est pas une simple conversion d'unités.
Pour une normalisation par un intervalle positif W : `a_n=a/W`, `b_n=b/W²`.
Ne pas inventer W : il dépend du véritable contrat de décodage/calibration.

Comparer `b` à la variance des paires noires conservées et calculer le
déplacement de noir qui serait nécessaire pour les égaler, `(varianceNoir−b)/a`.
Pente nulle : valeur absente, pas un zéro inventé. Comparer ce déplacement
à la plage des moyennes de zones noires mesurées ; cette plage n'est pas
un intervalle de confiance certifié. Conserver les versions contraint/non
contraint historiques et la distance du plus bas signal éclairé à l'origine.

Relire les expositions des prises éclairées et des noirs. Une interception
extrapolée sur des durées différentes n'est pas automatiquement une variance
de lecture caractérisée ; aucun effet thermique ou courant noir n'est prouvé
par cette seule différence de métadonnées.

Recalculer le diagnostic de dérive avec et sans piédestal dans le dénominateur.
Le même seuil numérique1 % sur le signal utile sert seulement à repérer des
points à revoir : ne pas les déclarer conformes/non conformes rétroactivement,
ni assimiler un écart moyen à une augmentation démontrée de variance.
Conserver points, identités de paires et unités ; les phases CFA/ROI ne sont
pas autant de photos indépendantes. Vérifier séparément que le moteur mesure
ses noirs optiques : une anomalie d'outil exploratoire ne prouve pas celle
de l'ouverture de production.

Tests : translation d'origine, changement d'échelle, pente nulle, effet du
piédestal sur une dérive faible en DN. Cette qualification n'importe aucun
profil physique, ne corrige aucune image et n'ouvre pas la réserve finale.

## 197. Inventaire à exposition comparable — 28 octobre 2026

Lancer `tests/audit_noise_raw_coverage.py` depuis `patch_noise`. Entrées : audit
des paires v005, PTC v003 et audit de domaine §196 ; vérifier leur filiation SHA.
Aucun décodage RAW et aucune nouvelle régression. Conserver le JSON horodaté
immuable sous `docs/validation/raw/`.

Inventorier les paires éclairées non rejetées, exclure les rôles noirs/mixtes
et incohérences ISO signalées par l'audit. Pour chaque phase CFA/zone,
reprendre le noir empirique et calculer le signal utile. Le contrôle descriptif
est : ancien statut `candidate_stable`, signal positif, dérive relative au
signal utile ≤1 %. Ne pas changer les anciens statuts ou interpréter ce contrôle
comme une certification de stabilité physique.

Stratifier par durée d'exposition enregistrée, relever les ouvertures, et
identifier les durées des noirs avec une tolérance relative1e−8 et absolue1e−12s.
Avant résultats, fixer les classes de signal/sigmaNoir à0–3,3–10,10–30,>30.
Elles ne sont pas des seuils d'acceptation ni un SNR photonique. Conserver aussi
les mesures des paires candidates qui ne satisfont pas le contrôle, et
l'appartenance aux points historiques de la PTC.

Compter séparément mesures, paires candidates distinctes et une liste gloutonne
de paires sans SHA commun, triées par chemins. Cette dernière est une borne
inférieure, pas une preuve d'indépendance ni une recherche d'appariement maximal.
Ne pas additionner les phases/ROI comme des niveaux d'éclairage. Les niveaux
réels d'illumination ne sont pas enregistrés dans les métadonnées : conserver
les moyennes RAW et ne pas les inventer à partir des seuls paramètres EXIF.

Tests : bornes des classes, équivalence des durées, refus d'un SHA partagé,
comptage de plusieurs ROI comme une seule paire et traitement d'une liste vide.
Absence de couverture près du noir à durée constante : ne pas forcer `b` ni
réajuster les mêmes points pour remplacer une acquisition manquante. Prévoir
alors noirs et faibles niveaux éclairés à durée fixe, lumière stable et surface
uniforme, plusieurs répétitions et une série réservée avant tout ajustement.

## 198. Composantes des noirs et complément pilote — 28 octobre 2026

### 198.1. Analyse des données existantes

Lancer `tests/audit_noise_dark_components.py`. Reprendre les moments des
paires explicitement déclarées noires dans l'audit v005. Ne pas filtrer
silencieusement les mesures selon leur ancien verdict ; conserver ce verdict.
Vérifier mêmes coordonnées, taille, phase CFA, moyennes et paramètres
d'acquisition, ainsi que des SHA distincts.

Convertir les variances marginales `ddof=0` vers `ddof=1`, puis reconstruire
`C=(V_A+V_B)/2−Var((A−B)/√2)` et la variance de la moyenne de paire.
Conserver C signé, contrôler Cauchy, ne pas effacer les décalages de moyenne
du compte rendu. Le calcul porte sur des moments spatiaux centrés du RAW
visible ; ni électrons, ni bruit temporel physique certifié. Une seule paire
ne sépare pas structure fixe, corrélation temporelle et traitement commun.
Ne pas transformer les ROI en répétitions indépendantes ou annoncer un
intervalle de confiance iid sur des pixels corrélés.

### 198.2. Prises supplémentaires recommandées, sans achat imposé

Commencer sur le R6 Mark III avec deux ISO pilotes : **1600 et51200**.
Le premier teste une interception antérieure faible, le second le manque
de faibles signaux à la durée des noirs. Les autres ISO et boîtiers resteront
à qualifier ultérieurement.

1. Installer le boîtier sur support fixe, mode manuel, RAW, temps **1/200s**,
   ouverture fixe (f/5,6 si possible) et mise au point manuelle fixe.
   Garder le même mode d'obturateur et tous les traitements boîtier constants ;
   désactiver la réduction de bruit longue pose. Noter modèle, mode, heure,
   éclairage, ouverture et tout changement. Ne pas modifier les paramètres
   entre les répétitions d'un niveau.
2. Photographier une surface mate uniforme, sans motif, suffisamment défocalisée,
   sous une lumière continue stable, sans variation automatique ou scintillement
   manifeste. Ne pas cadrer une scène texturée. À51200 ISO, éviter absolument
   la saturation ; des aplats clairs à l'écran ne sont pas l'objectif.
3. Avant de faire les séries, fournir **deux RAW de noirs et deux RAW pour
   chacun de quelques niveaux très sombres**. Garder1/200s et l'ouverture :
   varier l'éclairage ou l'atténuation lumineuse, pas le temps de pose.
   On vérifiera le signal RAW utile et sa dérive avant de poursuivre.
   Le nombre de crans d'un curseur ou la luminosité de l'écran ne permettent
   pas de viser directement les classes de signal du protocole §197.
4. Après ce contrôle pilote, viser **cinq niveaux non nuls**, comprenant
   plusieurs faibles signaux et au moins un niveau plus élevé non saturé,
   avec **huit prises par niveau**. Prévoir **huit noirs avant et huit noirs
   après** (bouchon, même durée/ISO/mode). Cela représente56 RAW par ISO
   pour la série complète ; ne pas la réaliser avant validation des niveaux.
5. Prévoir avant l'analyse quatre répétitions de développement et quatre
   réservées par niveau ; ne pas les sélectionner après inspection des scores.
   Cette séparation permet un contrôle interne, pas une validation autre séance.
   Éviter un changement volontaire de température en cours de série ; noter
   les interruptions et durées, sans supposer que la température EXIF existe.

Utiliser les dossiers existants `tests/image/colorimétrie/protocole/iso-1600`,
`iso-51200` et `protocole/blacks`. Ne pas écraser les anciens RAW. Indiquer
les noms/ranges correspondant à chaque niveau et les conditions de prise ;
l'analyse stockera les identités, paramètres et rôles dans son JSON canonique.
Pas besoin d'exports Lightroom ou TIFF pour ce pilote physique.

Les cinq niveaux ne sont pas une garantie automatique d'identifiabilité :
le signal utile, l'uniformité, la dérive, l'absence de saturation et la
couverture près du noir doivent être contrôlés avant tout ajustement.

## 199. Qualification des nouvelles rafales sur carte — 28 octobre 2026

`tests/audit_noise_new_bursts.py` lit uniquement la plage explicitement
inventoriée `D:/DCIM/100EOSR6/6V1A9861.CR3` à `6V1A9932.CR3`, en lecture seule.
Vérifier la date de prise, les empreintes et les tailles avant toute lecture
pixel. Figer un JSON d'inventaire avant décodage ; vérifier à nouveau les
empreintes après lecture de chaque fichier. Ne pas déplacer les sources et
ne pas consulter les14 prises réservées de la rafale antérieure.

Regrouper par ordre des noms, métadonnées d'exposition identiques et écart
d'horodatage voisin compris entre0 et4s. Ce regroupement identifie des
**rafales candidates**, pas des niveaux lumineux certifiés : leurs rôles
noir/aplat/scène restent inconnus. Une interruption ou un changement de
réglage ouvre un nouveau groupe. N'ajuster aucun coefficient à cette étape.

Décoder séquentiellement et garder seulement trois carrés256×256 du CFA
visible, au centre vertical et à20/50/80 % de la largeur. Conserver toutes
les prises de chaque groupe pour une qualification descriptive, sans
revendiquer une validation réservée ou une vérité propre. Les statistiques
portent sur chaque phase128×128 ; aucune conversion vers RGB ou TIFF.

Pour chaque groupe d'au moins deux prises, calculer la moyenne par prise,
la variance temporelle brute pixel par pixel et la variance temporelle
après retrait de la moyenne spatiale propre à chaque prise. Cette dernière
est corrigée du facteur pixels/(pixels−1) pour la cohérence des identités
spatiales. Elle ne supprime pas les mouvements ou gradients d'éclairage.

Conserver la variance marginale moyenne, la variance spatiale de la moyenne
de rafale et l'estimation signée de structure commune :
`Var_spatiale(moyenne)−Variance_temporelle_centrée/nPrises`.
Vérifier l'identité avec `Variance_marginale−Variance_temporelle_centrée`.
Ne pas convertir cette décomposition algébrique en séparation physique
certifiée lorsque les prises sont corrélées ou la scène instable.

Conserver également toutes les statistiques de paires du groupe (dont les
paires se recouvrent), la saturation conservative à98 % du blanc déclaré
et le signal par rapport aux anciens noirs empiriques de même ISO.
Ce dernier reste **candidat**, car la séance des noirs est différente.
Aux ISO12800/25600 sans ancien noir empirique, laisser ce signal à `null`,
sans lui substituer automatiquement le noir déclaré du décodeur.

Le lanceur persistant borne le calcul à1800s. Tests : dérive additive,
équivalence à l'identité de covariance pour deux prises, pile invalide,
frontières de groupes selon temps/métadonnées. L'étude ne règle pas le
débruiteur et ne valide pas un modèle boîtier ; elle détermine les données
réellement disponibles avant de choisir l'expérience suivante.

## 200. Complément de noirs aux rafales de pièce sombre — 28 octobre 2026

L'utilisateur confirme qu'aucune prise9861–9932 n'est bouchon fermé.
Ne pas utiliser ces scènes pour mesurer directement le piédestal noir ou
la variance sans lumière, même si certaines moyennes sont proches du noir.

Complément recommandé : **huit RAW bouchon fermé par ISO1600,3200,6400,
12800,25600 et51200**, soit48 fichiers. Il n'est pas demandé de refaire les
scènes ni de produire des exports TIFF ou Lightroom.

- Même boîtier R6 Mark III et même mode d'obturateur que pour les rafales.
- Mode manuel, temps1/200s ; conserver f/5,6 et105mm pour la cohérence
  des réglages, même si l'ouverture n'agit plus sur la lumière bouchon fermé.
- Bouchon opaque bien placé, pas une photo d'un objet noir ni de la pièce sombre.
- Réduction de bruit longue pose désactivée ; noter les autres traitements
  et les conserver constants. Ne pas prétendre annuler les traitements internes
  du RAW simplement parce qu'une option de menu est désactivée.
- Même cadence autant que possible, éviter un échauffement volontaire important,
  noter heures, interruptions et mode ; la température du capteur n'est pas
  présumée connue ou identique à la séance précédente.
- Garder les huit prises, y compris les premières ; ne pas sélectionner après
  inspection du bruit. Les rôles des fichiers seront consignés dans le JSON.

Déposer les fichiers dans `tests/image/colorimétrie/protocole/blacks`, sans
écraser les anciens, ou indiquer leur plage de noms sur D:. Priorité en cas
de temps limité : ISO12800/25600 sans référence antérieure, puis1600/51200.
Cette série améliore la qualification des noirs ; elle ne certifie pas à elle
seule toute la loi signal/bruit, les autres boîtiers ou les variations thermiques.

## 201. Qualification des noirs répétés et réévaluation des scènes — 28 octobre 2026

`tests/audit_noise_repeated_blacks.py` utilise les nouveaux RAW déclarés
bouchon fermé du dossier `protocole/blacks` : `6V1A9950–9999`, puis
`6V1A0001–0015`. La remise à zéro du compteur ne doit pas inverser la
chronologie : trier d'abord l'horodatage boîtier, puis l'ordre numérique
circulaire à partir de9950 pour départager une même seconde.

Vérifier date, durée1/200s, métadonnées identiques à chaque ISO et SHA distincts.
Figer avant lecture pixel les indices de développement `0..floor(N/2)−1`
et de contrôle `floor(N/2)..N−1`. Requérir au moins huit prises par ISO.
Ce contrôle est interne à une même rafale, pas une validation autre séance.
Ne pas choisir les prises après avoir vu leurs variances.

Décoder séquentiellement les mêmes trois carrés CFA256×256 que pour les scènes
du §199. Vérifier intégrité, dimensions, phase et coordonnées de chaque zone.
Conserver les moments individuels du lecteur et les statistiques de rafale
sur chaque phase128×128. Reprendre l'identité de variance du §199 sur les
sous-ensembles développement, contrôle et sur la série entière.

Pour chaque phase/ROI, le noir de développement est la moyenne des moyennes
RAW des prises de développement. Conserver séparément le noir de contrôle,
leur écart en DN et le rapport des variances temporelles centrées
contrôle/développement. Aucun seuil de succès n'est choisi après mesure,
aucun intervalle iid pixel ni calibration physique ne sont annoncés.
Conserver les statistiques des paires chronologiques disjointes pour leur
structure spatiale ; ne pas les assimiler à une nouvelle vérité absolue.

Réévaluer seulement les moments déjà conservés des scènes de pièce sombre :
`signalCandidat = moyenneScène−noirDéveloppement` et
`rapportVariance = varianceTemporelleScène / varianceTemporelleNoirDéveloppement`.
Vérifier même modèle, durée, dimensions et CFA. Conserver les signaux et
différences de variance signés, sans écrêtage à zéro ; ils ne sont pas des
estimations de bruit photonique garanties. Conserver la valeur antérieure
ou son absence pour tracer le changement de référence. Aucun RAW de scène
redécodé et aucune pente/interception ajustée.

Les nouveaux noirs sont acquis après les scènes ; mode d'obturateur exact,
température et absence de fuite lumineuse ne sont pas certifiés par les
seuls EXIF. Les valeurs locales ne deviennent pas automatiquement un profil
universel ou un correctif d'ouverture. Le JSON final contient la filiation,
les versions de lecture, les sources, les mesures et les limites.

Tests : séparation figée et disjointe, ordre au passage9999→0001,
réévaluation d'une mesure auparavant sans référence, conservation d'un signal
ou d'un excès de variance négatif. Calcul borné à1800s par le lanceur persistant.

## 202. Incertitude des moyennes à faible signal — 28 octobre 2026

`tests/study_noise_mean_uncertainty.py` lit les JSON de §199/201, vérifie leur
filiation et ne décode aucun RAW. Conserver les noirs de développement
préalablement figés et rapporter leurs contrôles séparément.

Pour chaque phase/ROI, mesurer directement la variance échantillonnale des
moyennes de prise. Comparer à `varianceTemporellePixel/16384` comme témoin
iid, pas comme incertitude imposée. Le rapport ne sépare pas corrélation
spatiale, dérive globale, corrélation temporelle et faible effectif.

Pour une rafale de n prises, énumérer les étendues de tous les sous-ensembles
de n moyennes des noirs de développement. Comparer à l'étendue observée de
la scène, à nombre de prises égal. Les sous-ensembles recouvrants ne sont
pas des tirages indépendants ; leur rang ne fournit pas de p-valeur. Un
unique sous-ensemble ne qualifie évidemment pas une distribution d'extrêmes.

Présenter aussi la sensibilité en multipliant les étendues noires par
`sqrt(varianceScène/varianceNoir)`. Cette approximation suppose des lois de
corrélation inchangées et peut absorber la dérive recherchée dans la variance
de scène. Elle ne doit jamais devenir une règle d'acceptation automatique.

Conserver les résultats individuels, le seuil historique1 % seulement comme
repère, et distinguer étendue de rafale et différence de deux prises. Relever
séparément les cas à signal positif≤1 sigma noir comme description du
domaine, pas comme une règle de sélection de coefficients.

Ne pas confondre répétabilité de la variance par pixel et précision de la
variance des moyennes. Ne pas remplacer le seuil relatif par un maximum
empirique ou un score gaussien non calibré. Tests : variance de moyennes,
effectif comparable, invariance au décalage commun, sensibilité de l'échelle.
La suite porte sur la loi signal/variance à référence noire figée, sans
réoptimisation d'un seuil sur les mêmes données.

## 203. Loi signal/variance à noir figé — 28 octobre 2026

Lancer `tests/study_noise_dark_anchored_law.py` sur les moments immuables
des scènes §199 et des noirs répétés §201, après vérification de la filiation.
Ne relire aucun RAW, n'écarter aucun groupe au seuil relatif1 %.

Par ISO et phase, estimer une seule pente dans `v_i=b_i+a·s_i`, avec les
noirs moyens et les variances noires de développement figés par zone/phase.
Donner à chaque groupe le même poids total, réparti également entre ses zones.
Conserver la solution signée et sa projection non négative ; aucun ajustement
libre d'interception ou conversion implicite vers des électrons ou un profil.

Conserver une pente par groupe diagnostique et un rejeu retirant chaque groupe
entier à son tour. Exiger au moins trois groupes pour ce rejeu ; une phase
à groupe unique reste non contrôlable hors groupe. Évaluer les erreurs
individuelles relatives à la variance observée et leur moyenne équilibrée
par groupe, avec le témoin « variance noire seulement ».

Prédéclarer les sensibilités : référence noire de contrôle, moyenne minimale
et maximale entre prises de chaque scène. Garder les recalculs et les
prédictions à pente initiale figée, sans retenir le scénario le plus favorable.
Ne pas transformer ces perturbations en intervalle de confiance complet.

Le poids égal par groupe n'annule pas le levier des signaux élevés.
Les erreurs sur les abscisses, le mouvement et les corrélations ne sont pas
résolus par cette régression. Le rejeu après exploration des scènes ne
constitue pas une validation indépendante. Conserver ISO/phases/groupes,
points, prédictions, noirs locaux et unités dans le JSON canonique.

Tests : pente connue, poids de groupe invariant à une duplication de ses zones,
retrait de tout le groupe, insuffisance de groupes, pente négative diagnostique
et domaine de signal nul. Avant toute promotion, vérifier les pentes figées
sur une autre acquisition compatible, sans les réajuster à ses résultats.

## 204. Transfert de la loi figée sur la séance antérieure — 28 octobre 2026

`tests/study_noise_anchored_transfer.py` lit la loi de §203, les noirs de
§201, l'audit antérieur v005 et ses points conservés dans §196. Vérifier
les filiations SHA, puis la disjonction des empreintes sources de la cible
avec celles des scènes/noirs d'apprentissage. Les anciennes acquisitions
étaient déjà inspectées : ne pas parler de validation aveugle.

Exiger boîtier, dimensions, phase CFA, blanc déclaré et coordonnées de zones
identiques. Utiliser uniquement les ISO/phases munis d'une pente actuelle,
sans interpolation. Ne pas appliquer le nouveau diagnostic1 % comme filtre.
Le domaine cible reste celui des points historiquement retenus ; conserver
les raisons et effectifs des ISO absents du modèle.

Prédire sans ajustement `bDéveloppement+aFigé·(moyenneRAW−BDéveloppement)`.
Conserver erreur signée, rapport de variances, moyennes, métadonnées,
statuts historiques, unités et sortie du domaine de signal d'apprentissage.
Traiter d'abord1/200s, puis chaque autre durée séparément. Les différences
de durées et de signal étant couplées, ne pas en déduire un effet causal du temps.

Moyenner les erreurs absolues relatives à poids total égal par paire,
puis égal entre ses zones. Compter les paires distinctes séparément des
mesures CFA/ROI. Présenter en sensibilité prédéclarée le réancrage sur les
noirs locaux de la séance antérieure, pente toujours figée ; ce scénario
ne doit pas être confondu avec le transfert du modèle entier inchangé.

Tests : formule sans ajustement, changement cohérent d'origine noir/interception,
pondération par paire, absence de points et valeurs invalides. Aucun RAW
redécodé, aucun coefficient de production écrit, aucune réserve finale lue.
La suite doit qualifier les unités et la propagation du bruit dans l'ouverture,
pas réajuster la pente pour faire réussir le transfert.

## 205. Contrat de transport CFA → image de travail — 28 octobre 2026

`tests/study_noise_opening_transport.py` conserve les empreintes et points
d'ancrage des sources IPR et LibRaw0.22.2 installées, puis vérifie les lois
algébriques et une illustration gaussienne du bornage. Aucun RAW décodé et
aucune sonde d'exécution encore ajoutée par cette étude.

Séparer : décodage CFA, estimation du noir, soustraction/bornage LibRaw,
gains et normalisation entiers, RCD, médiane adaptative, sortie16 bits,
optique éventuelle, DCP et retouches avant le point d'analyse du bruit.
Noter les branches avec/sans DCP. Ne pas appeler le master « linéaire » pour
en déduire que sa relation au signal capteur est entièrement linéaire :
les primaires/transferts du master et les opérations de rendu sont deux notions.

À noir/gain fixes et sans bornage, pour `x=k·s`, vérifier `a_x=k·a`,
`b_x=k²·b`. Pour les matrices déterministes, conserver la covariance complète
`M·C·Mᵀ`, pas seulement sa diagonale. Pour un noir estimé aléatoire,
tenir compte de `Var(B_est)` et de sa covariance avec le signal.

Le gain réel vers RCD dépend de `scale_mul`, du maximum/noir ajusté et de
la division65535. Ne pas en inventer une valeur à partir du seul EXIF.
Les quatre phases CFA restent distinctes avant dématriçage. L'exposition DCP
est un gain de signal dont la variance évolue au carré, mais tables et courbes
nécessitent une Jacobienne locale ou une mesure dédiée à leur régime.

L'illustration `max(0,s+σN)` utilise les moments analytiques gaussiens et
rapporte biais de moyenne et rapport des variances. Elle ne reproduit pas
le capteur, les corrélations, la quantification ou les bornes hautes. Ne pas
présenter sa réduction de variance comme un débruitage ou un gain visuel.

Tests : loi sous gain, moments à signal nul, limite grand signal, covariance
hors diagonale et entrées invalides. La suite exige une sonde des paramètres
effectifs avant interpolation et un contrôle des étapes adaptatives. Un futur
chemin flottant après `unpack()` devra être étudié séparément, sans abandon
implicite du décodeur ni changement silencieux du rendu validé.

## 206. Sonde native des étapes d'ouverture — 28 octobre 2026

Construire uniquement `iprOpeningTransportProbe` via le CMake isolé de
`tests/burst_tools`, dans `build-patch_noise/burst-tools`. Le fichier de test
inclut le décodeur non modifié dans sa propre unité de compilation pour
réutiliser son estimation du noir et son processeur privés ; aucune nouvelle
API de production ni copie parallèle des calculs photographiques.
Allouer l'objet LibRaw de la sonde sur le tas : sa taille ne doit pas saturer
la pile Windows pendant l'ouverture du témoin de production.

La sonde charge d'abord le témoin via `TestRecipes::load` : Camera Neutral,
calibration, WB du boîtier, optique désactivée. Elle réutilise le blanc de
calibration effectivement appliqué et le contexte DCP du témoin. Les callbacks
enregistrent trois carrés256×256 avant RCD et après RCD/médiane. Conserver
`pre_mul`, noir restant et maximum aux étapes de traitement, le noir retenu
avant traitement et le profil exact. L'entrée est déjà soustraite/bornée et
équilibrée en blanc ; le callback ne récupère pas les valeurs sous le noir perdues.

Après traitement, comparer tout le buffer caméra au témoin de production,
puis chaque carré après application du même DCP au master témoin. Exiger un
écart absolu maximal≤1e−7 pour les deux ; arrêter sinon, ne pas présenter
une sonde divergente comme le moteur réel. Les tableaux flottants sont
des fichiers de mesure locaux, pas des TIFF de génération utilisateur.

`tests/measure_noise_opening_stages.py` fige avant mesure douze sources :
premiers deux noirs de développement à ISO1600/51200 et premières deux
prises des groupes de scènes0/20/5/16 du §199. Cela constitue six paires,
pas un nouveau corpus indépendant ou une comparaison visuelle de débruitage.
Vérifier les SHA et au moins7GiB de mémoire disponible ; lancer séquentiellement,
timeout240s par source et1800s pour le travail complet.

Conserver la fraction de zéros par phase CFA avant RCD, la variance de
différence de paire et la covariance RGB après chaque étape. Après DCP,
projeter la covariance sur Y Rec.2020, Co=R−B et Cg=G−(R+B)/2. Ne pas soustraire
naïvement les covariances des noirs et des scènes si leurs gains WB/profils
diffèrent. Les résidus peuvent contenir déplacement et variation d'éclairage.

Tests Python : suppression d'un décalage spatial uniforme à la précision
flottante, conservation des covariances croisées, invariance à l'ordre des
prises et entrées invalides. Le JSON final préserve les témoins, paramètres,
provenances et limites ; les tentatives échouées restent tracées dans les
journaux, sans être présentées comme des succès. Pas de régression de production,
de sonde contournant l'écrêtage ni de réseau neuronal ajouté à ce stade.

## 207. Audit du pilote terminé et témoin linéaire — 28 octobre 2026

`tests/summarize_noise_opening_stages.py` consulte le résultat §206 et les
moments des mêmes paires CFA de §199/201. Vérifier les empreintes des sorties
natives et fichiers de pixels, ainsi que les gardes d'égalité avec la production.
Ne pas relancer l'ouverture pour retrouver des résultats déjà conservés.

Par phase et zone avant RCD, comparer la variance de paire mesurée au témoin
sans borne `varianceRAW·moyenne(k_A²,k_B²)`, avec les gains effectifs
`k=pre_mul/maximum` du callback. Quand les gains sont égaux, la mise à l'échelle
linéaire est exacte sous ses hypothèses ; sinon, étiqueter le témoin comme
approximation, pas comme propagation exacte d'une covariance inconnue.
Tracer l'écart relatif de gains et la fraction de valeurs nulles de chaque prise.

Conserver les mesures individuelles et séparer noirs, scènes presque noires
et plus éclairées, par ISO. Les rapports sont des rapports de variance,
pas une proportion d'information ou de détails perdus. Les différences
incluent arrondi, bornage et variations des paramètres ; ne pas attribuer
un pourcentage causal exact au seul écrêtage.

Conserver les covariances RGB de chaque étape ; après DCP, Y utilise les
coefficients Rec.2020 et les différences chromatiques sont Co=R−B,
Cg=G−(R+B)/2. Ne pas appeler « gain du bruit » un simple rapport de canaux
entre deux bases de primaires différentes. Les étapes LibRaw après le callback
et avant le buffer final restent séparées. Tests : gain égal, gain inégal
étiqueté, invariance à l'ordre et entrées invalides.

## 208. Premier prédicteur local conditionnel — 28 octobre 2026

Construire `iprLocalNoiseTransport` dans le CMake isolé des sondes. Le calcul
réutilise `Demosaic::interpolate` et `CameraProfileEngine::applyDcp` de l'engine ;
il n'ajoute aucun traitement au moteur. Le test natif `--self-test` vérifie
le dénominateur de covariance pour deux images synthétiques et les termes croisés.

`tests/study_noise_local_transport.py` consulte les moments et pixels déjà
conservés du pilote, la loi RAW figée, les noirs et le manifeste de calibration.
Aucun RAW relu. Pour chacun des six cas de paire et trois zones, préparer
avant simulation18 cas utilisant uniquement la moyenne RAW de la première
prise comme entrée locale. Les références de bruit viennent toujours des
études antérieures, pas d'un ajustement sur la covariance de sortie.

Par phase, poser `v=b+a·max(moyenneRAW−noirDéveloppement,0)` avec pente et
variance noire figées. Conserver la différence de signal signée dans les
preuves, même si le terme photonique est borné à0. La moyenne est une
estimation bruitée ; ce pilote ne prétend pas retrouver le signal latent exact.

Simuler32 réalisations indépendantes d'une tuile512×512 uniforme par phase,
graines fixées par cas. Bruit **gaussien iid**, arrondi RAW14 bits, soustraction
des noirs optiques effectivement retenus pour la première prise, borne0,
gains effectifs et quantification16 bits. Cette génération est un contrôle
de modèle ; elle ne reproduit pas les corrélations réelles du CFA.

Appliquer RCD et la médiane d'ouverture de production ; mesurer le carré
central128×128, puis lui appliquer le même DCP, Kelvin et options de calibration.
Les opérations LibRaw restantes entre le callback post-RCD et le buffer caméra
final ne sont pas simulées. La tuile uniforme ne reproduit pas non plus
les gradients et le poids de médiane d'une vraie tuile texturée.

Estimer la covariance temporelle par pixel sur les32 sorties, en retirant
la moyenne spatiale de chaque réalisation et la moyenne d'ensemble propre
à chaque pixel. Dénominateur `(nRéalisations−1)·(nPixels−1)`, compatible avec
la covariance spatiale de différence de paire au cas de deux réalisations.
Comparer aussi les deux moitiés16/16 pour contrôler la dispersion de simulation.

Comparer aux différences des deux vraies prises du pilote sur leurs carrés
centraux128×128 : RGB caméra après RCD/médiane et Rec.2020 après DCP,
puis Y/Co/Cg. Conserver matrices, rapports de **sigmas** et erreurs de Frobenius.
Ne pas confondre rapport de variance et de sigma. Aucun seuil ou facteur
de correction n'est ajusté au vu des résultats. Les scènes et modèles ont
déjà été inspectés ; pas de revendication de validation indépendante.

Tests Python : loi de variance, signal négatif tracé avec variance plancher,
rapport de sigma, entrées invalides. Lancement indépendant, timeout1500s
pour le simulateur et1800s pour la campagne. Aucun TIFF, réseau neuronal,
modification du rendu ou lecture de la réserve finale.

## 209. Attribution du transport dans le profil — 28 octobre 2026

`tests/study_noise_profile_decomposition.py` exploite uniquement les buffers
conservés du pilote §206 et la covariance simulée figée de §208. Vérifier
empreintes des pixels, profil et parents. Refuser une paire dont profil,
température ou état de calibration diffèrent. Aucun RAW relu.

Construire `iprProfileNoiseTransport` dans le CMake isolé. Rejouer le DCP
de production sur chaque prise et imposer un écart maximal≤1e−7 avec la
sortie conservée. Tester préalablement le transport d'une covariance connue
par une application linéaire diagonale synthétique.

Sur les18 carrés128×128, poser `m=(A+B)/2`, `d=(A−B)/√2`.
Estimer J par différences centrales de pas1e−5, témoin demi-pas5e−6.
Comparer la covariance centrée du vrai résidu DCP à celle de `J·d` :
J au RGB moyen, J par pixel de m, puis moyenne de J sur le segment A–B.
Pour cette dernière, utiliser les positions `m±√(3/10)·d` et m,
avec poids5/18,4/9,5/18. Les trois nœuds constituent une quadrature,
pas une égalité exacte pour une transformation non linéaire arbitraire.

Transporter séparément la covariance caméra simulée figée par `J·C·Jᵀ`,
globalement et en moyenne spatiale. Conserver toutes les matrices et rapports
de sigma Y/Co/Cg, médianes et extrêmes ; ne pas sélectionner un seuil au vu
du meilleur score. Mesurer la sensibilité au demi-pas, sans optimiser ce pas.

Les champs moyens et résidus utilisent A **et** B : attribution oracle,
jamais prédicteur mono-image. L'accord du transport intégré contrôle la
réponse numérique du profil sur la même paire, pas une validation indépendante.
Une covariance constante ne représente ni toute la distribution du bruit
ni ses corrélations spatiales ; la moyenne des covariances transportées omet
leur contribution au retrait de la moyenne de zone. Les résidus réels peuvent
contenir mouvement ou fluctuation lumineuse. Aucun facteur correctif ajusté.

Lanceur indépendant, timeout600s pour la sonde et900s pour la campagne.
Quatre tests Python couvrent projection neutre, changement d'échelle,
cohérence du contexte de profil et entrées invalides. Aucun TIFF produit,
aucune modification du moteur ni accès à la réserve finale.

## 210. DCP complet à champ moyen contrôlé — 28 octobre 2026

`tests/study_noise_profile_mean_fields.py` reprend les18 cas et32 graines
de §208, sans ajuster la loi RAW. Les réalisations caméra n'avaient pas été
conservées, seulement leurs covariances : elles sont régénérées pour cette
nouvelle intervention contrôlée, pas comme une validation supplémentaire.
Imposer un écart maximal≤1e−12 des covariances caméra et DCP du témoin
original par rapport au résultat antérieur, sinon arrêter la campagne.

Conserver les32 images caméra en mémoire, retirer leur moyenne d'ensemble
en chaque pixel, puis réutiliser le même résidu dans deux branches :
RGB uniforme égal à la moyenne de `(A+B)/2`, ou champ spatial `(A+B)/2`.
Appliquer le DCP complet, avec les mêmes paramètres de production. Le
recentrage ne doit pas modifier la covariance temporelle caméra : contrôle
absolu≤1e−10. Aucun changement de force, pente ou variance, aucun écrêtage
supplémentaire avant DCP ; tracer les minima d'entrée des deux branches.

Comparer les covariances temporelles centrées, et les rapports de sigma
Y/Co/Cg aux mêmes paires réelles. Conserver aussi le rapport spatial/uniforme.
Le bruit généré conserve ses corrélations créées par RCD/médiane, mais sa
distribution RAW initiale reste gaussienne iid. Ne pas le qualifier de bruit
réel ni de modèle complet du capteur. Les moyennes réelles sont bruitées et
utilisent les deux prises ; c'est toujours une attribution oracle.

Le déplacement des résidus peut produire des RGB caméra négatifs ou rendre
leur distribution physiquement incompatible avec la moyenne choisie. Les
tracer sans les masquer : cette intervention isole une sensibilité du DCP,
elle ne reconstitue pas un autre RAW physiquement acquis. Les opérations
LibRaw restantes après RCD ne sont toujours pas simulées.

Quatre tests Python : moyenne, invariance à l'ordre, entrées invalides et
invariance de covariance temporelle au recentrage. Autotest natif de §208,
timeout1500s pour le calcul et1800s pour le lanceur indépendant. Aucun TIFF,
RAW relu, accès à la réserve finale ou modification de production.

## 211. Champ de signal CFA avant bornage et RCD — 28 octobre 2026

`tests/study_noise_raw_mean_fields.py` fige les18 cas précédents avant lecture
des pixels. Relire uniquement les12 RAW du pilote, dont les empreintes doivent
être inchangées, pour extraire des carrés512×512 entourant les carrés256
précédents. Vérifier origine paire, CFA RGGB, géométrie et moyennes par phase
du carré central256 identiques à celles du pilote à1e−6 DN près. Les14 RAW
de réserve finale restent exclus.

Quatre branches figées par cas : témoin original §208 ; moyenne uniforme
par phase de la paire centrale256 ; moyenne de paire lissée par Gaussienne
sigma8 ; même chose sigma16. Les sigmas sont en pixels de plan CFA, soit
respectivement16 et32 pas mosaïque. Lissage séparé des quatre plans, bord
réfléchi, troncature4 sigmas. Ne pas choisir une échelle au vu du score.

Pour les trois nouvelles branches, borner le **signal latent estimé** à0
par rapport au noir de développement mesuré. Pour les prises bouchon fermé,
imposer ce noir comme moyenne uniforme dans toutes les branches nouvelles,
sans apprendre un motif depuis les pixels noirs. Conserver les pentes et
variances noires : `v(x)=b+a·max(m(x)−B,0)` par phase, ISO et zone.
L'échantillon RAW bruité n'est pas tronqué au noir à cette étape : arrondi
14 bits, puis soustraction du noir optique, bornage, gains, quantification16
bits, RCD/médiane et DCP suivent les mêmes opérations que le témoin.

Utiliser32 tirages et la même graine par cas dans toutes les branches :
perturbations standardisées identiques, amplitude dépendante du signal local.
Aucune translation de résidus après RCD. Imposer le rejeu exact du témoin
original à1e−12 près sur les covariances caméra/DCP. Les72 simulations sont
des interventions sur18 cas, pas72 acquisitions indépendantes.

Tracer avant plancher la fraction de signal négatif, l'étendue et l'écart-type
du champ lissé et le RMS de la demi-différence des deux prises lissées.
Sous indépendance, cette demi-différence a la variance du bruit de la moyenne
de paire ; elle inclut ici mouvement/dérive et ignore la composante commune.
Ce n'est ni une référence propre ni un intervalle de confiance calibré.
Le plancher peut introduire un biais positif près du noir ; ne pas l'ignorer.

Conserver matrices et rapports de sigma caméra et Y/Co/Cg, contrôles16/16,
fractions nulles avant RCD et diagnostics par phase. Les paramètres ne sont
pas réajustés. La simulation iid gaussienne ne couvre pas les corrélations du
capteur, les étapes LibRaw restantes ou les poids RCD de toute l'image.

Quatre tests : indépendance des plans CFA, plancher tracé, invariance à
l'ordre et entrées invalides ; autotest natif de covariance. Lanceur indépendant
timeout2700s, sous-processus natif2400s. Aucun TIFF ou changement moteur.

## 212. Distributions et corrélations des résidus noirs — 28 octobre 2026

`tests/study_noise_residual_distributions.py` conserve les six cas noirs
du pilote : trois zones, ISO1600 et51200. Régénérer32 réalisations aux
graines originales uniquement pour capturer les pixels simulés, absents
des anciennes sorties de covariance. Imposer le rejeu du témoin à1e−12
près sur les covariances caméra/DCP. Sauvegarder les pixels centraux128
avant RCD, après RCD/médiane et après DCP ; garder empreintes et offsets.
Les vrais pixels viennent des buffers déjà conservés, sans décodage RAW.

Comparer une paire réelle à16 paires simulées disjointes (0,1), (2,3), etc.
Former `(A−B)/√2`, retirer sa moyenne spatiale par canal. Mesurer covariance
complète, sigmas, matrice de corrélation intercanal, asymétrie, excès de
kurtosis, quantiles standardisés0,001/0,01/0,1/0,5/0,9/0,99/0,999,
fraction au-delà de trois sigmas et fractions de zéros des prises individuelles.
La standardisation utilise le sigma échantillonnal ; les moments standardisés
sont descriptifs et non des estimateurs corrigés du biais de petit échantillon.

Mesurer les corrélations sur les recouvrements centrés séparément aux
décalages (0,1), (1,0), (1,1), (1,−1), (0,2), (2,0), (0,4), (4,0).
Avant RCD, empaqueter séparément R/G1/G2/B en64×64 : un pas correspond
à deux pixels mosaïque. Après RCD/DCP, utiliser le pas natif RGB. Ne pas
confondre corrélation de phases voisines et covariance de canaux coïncidents.

Conserver chaque paire simulée et chaque zone réelle, sans les présenter
comme acquisitions indépendantes. La paire supprime les composantes communes
aux deux images ; elle ne qualifie pas tout le bruit d'une photo unique.
Corrélations spatiales et quantiles n'ont ici ni test de significativité ni
intervalle de confiance. Une kurtosis élevée peut dépendre de rares valeurs :
ne pas identifier automatiquement des pixels chauds ou une loi du capteur.

Le buffer caméra réel final avant DCP reste séparé du callback post-RCD.
Les différences de contexte de tuile et opérations LibRaw manquantes restent
des limites. Aucun ajustement de distribution ou seuil au vu de ces scores.
Quatre tests : décalage constant, ordre des prises, empaquetage CFA, entrées
invalides. Autotest natif, timeout900s pour le calcul et1200s pour le lanceur.

## 213. Répétabilité sur les noirs déjà acquis — 28 octobre 2026

`tests/study_noise_black_distribution_repeatability.py` reprend la séparation
développement/contrôle figée en §199. Pour ISO1600 et51200, apparier les
indices consécutifs deux par deux **à l'intérieur** de chaque sous-ensemble,
sans recouvrement ni franchissement de frontière. Exclure explicitement les
dernières prises non appariées. Couverture attendue :10 paires,20 fichiers,
trois zones par paire ; pas30 acquisitions indépendantes.

Figer liste et empreintes avant pixels. Réutiliser les quatre buffers natifs
du pilote après vérification d'intégrité, décoder les16 autres séquentiellement
avec `iprOpeningTransportProbe`. Exiger au moins7 Gio disponibles avant chaque
nouveau décodage. Arrêter si la mémoire manque, sans abaisser automatiquement
le seuil ni arrêter les programmes de l'utilisateur. Timeout240s par RAW,
5400s pour le lanceur. Gardes d'égalité caméra/master avec la production≤1e−7.

Appliquer les mêmes métriques de §212 à toutes les paires, en préservant
leur rôle développement/contrôle, leurs paramètres effectifs et les contextes
de profil. La référence simulée reste celle de §212, sans nouveau tirage
ni ajustement. Si gains, noir ou balance changent, la comparaison reste
descriptive : ne pas annoncer une prédiction exacte dans un autre contexte.

Les noirs de contrôle ont déjà servi à qualifier des variances RAW : ce
n'est pas un jeu aveugle jamais consulté. Ne pas sélectionner ensuite les
paires sur leur kurtosis ou corrélation, ni réestimer une loi sur le contrôle.
Quatre tests vérifient appariement pair, impair, vide et indices invalides.

## 214. Contribution et localisation des valeurs extrêmes — 28 octobre 2026

`tests/study_noise_extreme_contributions.py` utilise les buffers conservés
de §213 et les32 simulations conservées de §212, sans décodage. Figer
avant calcul les fractions de rang0,01 %,0,1 %,1 %,5 % et16 coordonnées
par canal. Calculer les résidus de paire centrés, classer leur amplitude
absolue par canal et mesurer la part des sommes des carrés et puissances4
portée par chaque fraction. Arrondir le nombre de pixels au supérieur,
conserver fraction réellement atteinte et départager les ex æquo de façon
stable. Aucun retrait de pixel ni recalcul de rendu corrigé.

Conserver les rangs et les parts par zone, paire, canal, étape et rôle
développement/contrôle. Le quatrième moment est ici calculé avec moments
de population : ne pas le confondre exactement avec le moment standardisé
au sigma échantillonnal du §212. Comparer les mêmes métriques sur les
16 paires simulées par cas, sans les déclarer acquisitions du capteur.

Dans chaque zone réelle, suivre l'union des16 coordonnées les plus extrêmes
par canal après DCP : valeurs A/B et résidus centrés de tous les buffers,
coordonnées natives et voisinage CFA5×5 avant RCD. Même coordonnée ne
signifie pas correspondance causale un pixel CFA/un pixel RGB : RCD mélange
voisins et canaux. La taille du voisinage est descriptive, pas l'étendue
complète de dépendance du dématriçage.

Comparer les intersections des16 coordonnées après DCP entre toutes les
paires distinctes au même ISO, zone et canal, sans changer les allocations.
Les résidus suppriment le commun aux prises : une récurrence n'identifie
pas à elle seule un pixel chaud, son absence n'exclut pas un défaut commun.
La sélection par rang est assistée par les résultats, donc oracle d'attribution
et non détecteur mono-image déployable. Aucun seuil ou modèle ajusté.

Quatre tests : pixel dominant, changement d'échelle, comptes arrondis et
entrées invalides. Lanceur indépendant avec timeout600s, aucun TIFF ou RAW lu.

## 215. Sites CFA récurrents sélectionnés sur développement — 28 octobre 2026

`tests/study_noise_recurrent_sites.py` fixe d'abord sa politique, puis construit
les masques à partir des seuls rangs **avant RCD** des paires de développement
de §214. Par ISO, zone et phase, retenir les coordonnées présentes au moins
deux fois dans les16 plus grandes amplitudes centrées. Compter une occurrence
maximum par paire et site, garder aussi les masques vides. Écrire le gel des
masques avant consultation des pixels de contrôle. La politique est descriptive
et ne constitue pas un seuil de défaut à intégrer en production.

Mesurer les mêmes coordonnées sur chaque paire de développement et contrôle :
fraction de sites, parts des sommes des carrés et puissances4, présence dans
les16 plus grandes amplitudes et valeurs des résidus centrés sélectionnés.
Aucun pixel n'est exclu ou corrigé. Pour chaque sous-ensemble, retirer la
moyenne de chaque image par phase puis calculer la variance temporelle
par site sur les4 ou6 prises retenues. Rapporter à la médiane du plan entier
et à celle des huit voisins de même phase, hors sites sélectionnés. Un pas
de ce voisinage représente deux pixels mosaïque. Conserver valeurs et ordre
des sources ; ne pas cacher un voisinage vide ou une référence nulle.

Les scores de développement sont biaisés par la sélection ; juger séparément
le contrôle. Celui-ci a déjà été inspecté dans les études antérieures : un
gel prospectif des masques ne le rend pas vierge ou indépendant. Variances
de4/6 prises imprécises, aucun intervalle de confiance ou test de défaut
capteur. Les buffers sont déjà bornés et amplifiés, pas des électrons bruts.
Pas d'identification automatique de pixel chaud ni de généralisation ISO/boîtier.

Quatre tests : récurrence sans double comptage, sélection vide, variance
temporelle d'un site synthétique et entrées invalides. Aucun décodage ni
rejeu de simulation ; lanceur indépendant avec timeout600s.

## 216. Association spatiale des sites CFA aux résidus RGB — 28 octobre 2026

`tests/study_noise_site_neighborhoods.py` reprend exclusivement les masques
développement figés en §215. Réunir les phases en grille128×128 par
`(2y+dy,2x+dx)`. Construire les voisinages de Chebyshev aux rayons0/1/2/4
pixels natifs, bornés au carré, puis les couronnes disjointes entre rayons.
Ne pas sélectionner un rayon après consultation du contrôle.

Par région et paire, mesurer les parts des sommes des carrés et puissances4
des résidus RGB centrés après RCD, avant DCP et après DCP. Conserver surface
et concentration normalisée par cette surface. Garder les régions vides,
avec concentration non définie plutôt qu'une division par zéro.

Comparer à trois régions témoins de même surface, décalées circulairement
de (0,32), (32,0), (32,32) **après** construction du voisinage. Ces témoins
préservent la géométrie/aire mais peuvent recouvrir certains sites et comportent
un rebouclage artificiel. Ce ne sont pas des tirages d'une hypothèse nulle
calibrée ; aucun test de significativité ni preuve causale.

Mesurer les30 zones et distinguer développement/contrôle. Sélection et données
ont déjà été inspectées : pas de nouvelle validation aveugle. Les canaux CFA
sont réunis dans un masque spatial, pas projetés causalement vers un canal
RGB unique. Aucun pixel retiré, interpolation corrective ou DCP modifié.

Quatre tests : coordonnées des phases, couronnes disjointes, masque vide,
conservation d'aire par décalage. Buffers conservés seulement, aucun décodage
ni nouveau tirage de bruit ; lanceur indépendant avec timeout600s.

## 217. Sensibilité DCP aux perturbations multiplicatives caméra — 28 octobre 2026

`tests/study_noise_profile_gain_sensitivity.py` fixe par ISO/zone des plages
RGB à partir des sigmas caméra finaux des seules paires de développement
de §213, divisés par les sigmas simulés médians de §212. Inclure l'identité
dans chaque intervalle, sans consulter les écarts de sortie DCP pour les
définir. Ces rapports décrivent les écarts d'amplitude observés, **pas une
incertitude des vrais gains de balance des blancs ou un correctif physique**.

Neuf variantes positives : identité, bornes conjointes basse/haute, puis
les deux bornes séparées de chacun des trois canaux. Appliquer le multiplicateur
à l'image caméra entière, puis le DCP de production. Cela préserve le support
non négatif mais fait varier simultanément moyenne et bruit : ce n'est pas
une sensibilité à covariance seule. Ne pas choisir un facteur minimisant
l'écart après profil, ni assimiler ces neuf points à une enveloppe exhaustive
d'une transformation non linéaire.

Mesurer les30 zones réelles de §213, développement/contrôle séparés, et les
96 paires simulées déjà conservées de §212. Conserver les mêmes profils,
Kelvin et options ; refuser des contextes incompatibles. L'identité doit
rejouer les sorties natives à1e−7 près, pour chaque paire. Conserver toutes
les matrices et rapports de sigma Y/Co/Cg par rapport à l'identité de la
même paire. Aucun décodage, nouveau tirage ou ajustement des paramètres RAW.

Sonde `iprProfileGainSensitivity`, avec autotest de multiplication connue.
Quatre tests Python : bornes, identité incluse, neuf variantes et entrées
invalides. Timeout900s pour la sonde,1200s pour le lanceur. Les données de
contrôle ont déjà été inspectées : pas de nouvelle validation aveugle ou
amélioration du débruiteur annoncée sur ce diagnostic.
