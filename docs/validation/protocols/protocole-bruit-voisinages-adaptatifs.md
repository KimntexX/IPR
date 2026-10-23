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
