*[Français](roadmap-beta.md) | [English](roadmap-beta_en.md)*

# Plan d'action bêta — panneaux de retouche et traitements neuronaux

Date de cadrage : 31 octobre 2026. Statut : **plan proposé, non implémenté**.
Périmètre demandé par l'utilisateur : géométrie, suppression d'objets,
calques/filtres, réseaux neuronaux pour bruit/netteté et pour formes/objets,
empilement, panoramas horizontaux/verticaux et fusion HDR.
« Formes » signifie formes géométriques dans l'image, confirmé par l'utilisateur ;
ce chantier ne concerne pas l'identification des formats de fichiers.

Ce document porte la feuille de route de ces lots. L'[inventaire des fonctions](roadmap-fonctions-lightroom.md)
reste l'inventaire de couverture, la [matrice](../validation/matrice-validation.md)
porte les validations et les rapports/JSON portent les preuves. Un lot planifié
n'est pas une fonctionnalité disponible ni une validation acquise.

## 1. Objectif et conditions d'entrée

Conserver un développement photo utilisable sans IA, puis ajouter des opérations
locales non destructives et des assistants neuronaux **locaux**. Lightroom reste
un repère visuel éventuel, pas une cible de copie ni une spécification algorithmique.

Avant le premier jalon bêta :

- clôturer la qualification des réglages déjà implémentés selon la règle du
  projet ; ne pas assimiler l'arrêt des recherches sur le bruit à cette clôture ;
- stabiliser le débruitage actuel sans intégrer les variantes de banc rejetées,
  conformément à la [décision du 31 octobre, §54](../validation/analyses/analyse-developpement-raw.md#54-stabilisation-et-orientation-neuronale-pour-la-bêta--31-octobre-2026) ;
- figer l'état de référence, les limites connues et les recettes de non-régression ;
- qualifier la sauvegarde/restauration et le comportement aperçu/export.

La première série bêta sera `v0.2.X-beta`. Les intégrations de nouveaux panneaux
feront évoluer la version mineure selon les [règles Git](gestion-branches-git.md).
Les lots techniques internes ou les commits ne sont pas automatiquement de
nouveaux panneaux ni des versions mineures. Les numéros de livraison seront
arrêtés au moment des validations ; ce plan ne crée aucun tag.

## 2. Ordre des travaux et dépendances

| Lot | Livrable | Dépendances | Point de validation |
| --- | --- | --- | --- |
| B0 | Contrats d'édition, recette et tâches longues | Référence alpha qualifiée | Annulation/restauration et rendu inchangé sans nouvelles opérations |
| B1 | Panneau Géométrie manuel | B0 | Placement exact, cadrage et export cohérents |
| B2 | Calques, masques et filtres non destructifs | B0, coordonnées de B1 | Composition et persistance reproductibles |
| B3 | Suppression d'objets manuelle | B2 | Retouche locale, bords propres et annulation |
| B4 | Restauration neuronale : bruit puis netteté | B0, contrat RAW et gestion des ressources | Bénéfice visuel/détail qualifié sur machine de référence |
| B5 | Détection de formes et segmentation d'objets | B0 ; raccordements B1/B2 | Sélections et propositions corrigibles, erreurs mesurées |
| B6 | Suppression assistée par détection | B3 + B5 | Sélection assistée sans modification automatique non confirmée |
| B7 | Empilement multi-images avec référence utilisateur | B0, contrat multi-source et tampon disque | Recalage, recadrage valide et bénéfice sans destruction de détail |
| B8 | Panorama horizontal/vertical, puis multirangée | Socle multi-source de B7, coordonnées de B1 | Géométrie, raccords, couverture et ressources qualifiés |
| B9 | Fusion HDR d'expositions, puis panorama HDR | Socle multi-source de B7 ; B8 pour le panorama HDR | Expositions, saturation, mouvements et rendu dynamique maîtrisés |

B4 n'a pas besoin d'attendre les calques pour une première étude indépendante,
mais l'intégration doit respecter le même système de tâches, d'historique et de
provenance. B5 peut être préparé après le socle ; il ne bloque pas les outils
manuels de B1/B3. Pas d'entraînement ou de chantier parallèle lancé par ce plan.

## 3. B0 — socle commun avant les panneaux

Définir les contrats moteur avant leurs raccordements QML :

- source immuable, recette non destructive, commandes annulables et restauration
  de session ; une opération annulée ou en échec ne publie pas un résultat partiel ;
- espaces de coordonnées explicites : source, image orientée, géométrie du
  document, aperçu et écran. Conserver les transformations inverses pour les
  pinceaux, poignées, masques et sélections après zoom/recadrage ;
- ordre de rendu et domaines couleur documentés. Conserver le master de travail
  Rec.2020 et des adaptateurs explicites aux domaines des modèles ; ne pas faire
  passer tous les modules arbitrairement dans l'espace d'un réseau ;
- historique/recette capables de référencer masques, opérations et résultats
  calculés volumineux. Prévoir des actifs associés au projet plutôt qu'enfler
  indéfiniment le sidecar avec des pixels sérialisés ; choix du stockage à fixer
  par ADR avant implémentation ;
- invalidation des caches par source, recette, géométrie, masque, modèle et
  paramètres ; un résultat asynchrone obsolète ne remplace pas l'image courante ;
- services de progression, annulation, file de tâches et réservation RAM/VRAM ;
  utiliser les contrôleurs existants plutôt que multiplier les budgets isolés ;
- recette multi-source et actifs sur disque pour B7/B8/B9 : référence, identités
  des entrées, alignements, exposition, masques de validité et recadrage ;
  prévoir le dossier tampon dès le socle, pas comme extension tardive ;
- protocole d'export : résolution complète, limites d'alpha, même ordre de
  composition qu'à l'aperçu et absence de résultat provisoire exporté par erreur.

Livrables : ADR des contrats, schéma de recette, tests moteur et interfaces
courtes. `ImageEditor` reste cadenceur ; pas de calcul photographique en QML.
Tests : identité sans opération, annuler/refaire, fermeture en cours de tâche,
recette invalide, actif manquant, panne disque et absence de fichier source modifié.

## 4. B1 — menu de paramétrage Géométrie

### Première intégration manuelle

Regrouper recadrage libre/ratios, rotation/redressement, miroir horizontal/vertical,
perspective verticale/horizontale, échelle, aspect et décalages. Prévoir une grille,
des poignées, une saisie numérique, la remise à zéro par action et pour le panneau.
Gérer explicitement les zones vides : conserver une transparence autorisée ou
contraindre/recadrer, sans remplissage inventé silencieusement.

Définir l'ordre orientation/optique/géométrie et éviter les rééchantillonnages
successifs inutiles. Qualifier la méthode d'interpolation et les limites de
transformations ; une homographie ne doit pas être présentée comme un correcteur
universel de distorsion optique. Les profils LCP restent dans leur domaine.

La version initiale est manuelle. Le redressement guidé par lignes peut suivre ;
les suggestions automatiques issues de B5 restent une étape distincte.

Validation : mires à grille et repères connus, rotations et miroirs, transformations
inverses, limites de cadrage, masque après transformation, sauvegarde/relecture,
export pleine définition. Fixer les tolérances avant campagne et mesurer netteté
des bords, aliasing et absence de décalage aperçu/export.

## 5. B2 — menu Calques et Filtres

Première tranche : groupes, calques de réglage, visibilité, ordre, opacité,
duplication, suppression et masques pinceau/dégradé/plage. Prévoir les calques de
retouche calculée nécessaires à B3 sans les confondre avec une copie du RAW.
Énumérer les filtres initiaux à partir des modules réellement présents ; ne pas
créer un second moteur photographique pour le panneau.

Commencer par un mode de fusion normal correctement défini. Tout autre mode
sera ajouté avec équation, domaine couleur, gestion de l'alpha et tests. Le choix
alpha prémultiplié/non prémultiplié doit être uniforme et documenté, notamment
aux frontières des masques et des opérations géométriques.

Les opérations caméra précoces, dont le débruitage RAW neuronal éventuel, ne
deviennent pas arbitrairement des filtres déplaçables après DCP. Le panneau doit
indiquer les opérations fixes et les filtres réordonnables ; formaliser leurs
contraintes dans le graphe de rendu.

Validation : cas synthétiques de composition, opacités0/100%, masque uniforme,
masques imbriqués, permutation de calques, annulation, réouverture et rendu
identique en export. Mesurer consommation mémoire avec plusieurs masques et
calques, pas seulement sur une image sans retouches.

## 6. B3 — menu Suppression d'objets

Séparer trois opérations : tampon avec source choisie, correcteur/remplissage
local et, seulement après qualification dédiée, reconstruction neuronale.
La suppression n'est pas un outil de restauration de la vérité : les pixels
remplacés sont reconstruits et doivent rester identifiables dans l'historique.

Première tranche utilisable sans réseau : pinceau/zone, taille, contour progressif,
visualisation du masque, choix de source pour le tampon, aperçu, appliquer/annuler
et réédition de la retouche dans un calque dédié. Ne jamais modifier l'original.
Définir la portée de l'échantillonnage : source seule, calque ou composition.

B6 ajoutera le clic sur objet avec B5, puis dilatation et correction manuelle
du masque avant traitement. La reconstruction générative est optionnelle et
hors du premier engagement ; elle exige un choix de modèle, des droits et des
critères propres, pas seulement un détecteur d'objet.

Validation : poussières, petits éléments, zones texturées, objets sur lignes,
contours fins, bord de photo et grandes zones insuffisamment documentées.
Conserver les cas où le système doit avertir ou s'abstenir plutôt que promettre
un remplacement crédible. Contrôler halos, répétitions, raccords et couleur.

## 7. B4 — réseaux neuronaux Bruit et Netteté

### Étape B4a : cadrage du modèle et des données

La [préparation B4 du 31 octobre](restauration-neuronale.md) précise la sélection
de modèles, le matériel observé, les frontières RGB/RAW et les portes du banc.
Étude autorisée en parallèle des tests alpha, sans inférence lourde concurrente ;
aucun modèle ni runtime n'est encore qualifié.

Distinguer débruitage, accentuation et restauration de flou. Aucun réseau unique
n'est imposé : qualifier d'abord le débruitage, puis la netteté, puis leur ordre
ou un modèle conjoint si un bénéfice est démontré. La netteté ne récupère pas
une information absente ; tester les faux détails autant que le lissage.

Choisir explicitement le domaine : CFA, RGB caméra signé ou image de travail.
Conserver les données disponibles en amont ; un modèle entraîné sur du RGB
encodé ne peut pas être utilisé directement sur un RAW signé sans validation.
Définir normalisation, balance, valeurs négatives/>1, sous-échantillonnage et
adaptateurs d'entrée/sortie. Aucun retour u16 ou écrêtage ajouté implicitement.

Séparer développement, réglage et qualification par scènes/séances/boîtiers ;
des crops d'une même rafale ne deviennent pas des jeux indépendants. Préserver
la réserve encore fermée et prévoir un jeu supplémentaire si elle ne couvre
pas le domaine choisi. Tracer droits des données, licences des poids et runtime,
empreintes et version du modèle ; ne pas supposer qu'« open weight » suffit.

### Étape B4b : prototype sur machine de référence

Comparer au moteur classique conservé, à rendu/cadrage identiques : bruit
luma/chroma, détails faibles, peau/cheveux/textiles, aplats, couleurs saturées,
noirs/hautes lumières et artefacts. Mesures conditionnées par signal et revue
visuelle ; pas de validation sur une seule moyenne ou la seule proximité LR.
Pour la netteté : halos, ringing, contours doubles, texture inventée et répétition
du traitement. Comparer bruit→netteté et leur interaction avec l'accentuation
classique pour éviter une double accentuation par défaut.

### Étape B4c : intégration utilisable et robuste

Traitement local déclenché explicitement, progression/annulation, comparaison
avant/après et retour au moteur classique. La commande et les réglages avancés
restent prévisibles ; éviter un nouveau calcul lourd à chaque mouvement de
curseur lorsque le résultat peut être réutilisé de manière qualifiée.
Mettre en cache résultat/provenance par modèle et recette, sans mélanger les
sorties de versions de poids différentes. Modèle absent ou incompatible :
message explicite, source et édition préservées, aucun téléchargement silencieux.

## 8. B5 — réseaux de détection des formes et des objets

Prévoir une même couche d'assistance, mais des tâches et sorties distinctes :

| Tâche | Sortie attendue | Utilisation |
| --- | --- | --- |
| Formes géométriques | Lignes, segments, contours ou primitives avec confiance | Repères, redressement et perspective proposés dans B1 |
| Détection d'objets | Classes éventuelles et boîtes | Recherche de candidats à sélectionner |
| Segmentation | Masque de pixels, frontières et confiance | Calques/retouches locales et suppression assistée B6 |

Une boîte de détection ne suffit pas à détourer un objet. Prévoir correction
manuelle, ajout/retrait au masque et ambiguïtés sujet/arrière-plan. Pour les
formes, conserver un témoin géométrique classique : ne pas imposer un réseau
si la méthode simple couvre le besoin à moindre coût. Les réseaux sont une
piste planifiée, pas une garantie de supériorité sur toutes les scènes.

Les coordonnées doivent rester exactes entre proxy d'analyse et source pleine
définition, y compris après géométrie. Une détection ne modifie pas le rendu
automatiquement ; l'utilisateur choisit et confirme la transformation/retouche.
Définir classes/formes couvertes avant entraînement, sans promesse de reconnaître
« tous les objets » ni de comprendre la scène.

Validation : précision/rappel, IoU ou métriques de bord selon tâche, erreur
géométrique des repères, objets fins/occlus, motifs répétitifs, bruit et faible
lumière. Séparer qualité de sélection et qualité de reconstruction B3. Définir
seuils de confiance et abstention avant les données finales de qualification.

## 9. Ressources hôte, confidentialité et déploiement

Première qualification sur la machine de l'utilisateur, conformément à la
décision du31octobre. Figer CPU/threads, RAM, GPU/VRAM si utilisés, OS, pilotes,
backend, modèle et taille d'image. La seule indication de20threads logiques
observée au banc ne suffit pas à qualifier cette machine pour l'inférence.

Prévoir détection des capacités, budget partagé CPU/RAM/VRAM, priorité à l'IHM,
tuilage/recouvrement qualifiés, temporaires disque bornés si nécessaires et
nettoyage des seuls actifs temporaires du traitement. Ne pas promettre qu'un
contrôle applicatif interdit tout dépassement de charge globale de la machine.
Les8threads actuels sont un plafond existant, pas le futur optimum neuronal.

Machines moins adaptées : mode avancé plus lent, limité ou indisponible avec
explication ; fonctions manuelles et moteur classique conservés. Pas de
dégradation silencieuse de résolution ou de substitution de rendu. Qualifier
les différences entre backends et précisions numériques avant de les proposer.
Les critères de mémoire/latence seront fixés avant mesure, pas inventés ici.

Par défaut, images et inférence restent locales. Aucun envoi réseau ni collecte
de photos pour entraînement n'est autorisé par ce plan. Tout téléchargement
de poids ou service externe futur requiert une décision explicite. Vérifier
droits de redistribution des modèles séparément des licences du code.

## 10. Livraison, branches et critères de sortie

Branches durables en anglais, depuis `dev` à jour : réutiliser celles existantes
par domaine ; propositions pour les nouveaux groupes `patch_geometry`,
`patch_layers`, `patch_object_removal`, `patch_detection`. Réutiliser le domaine
bruit/netteté approprié après audit des branches ; ne pas en créer un doublon.
Les raccordements IHM et moteur sont coordonnés par contrats et worktrees
isolés. Aucun branchement, push, fusion ou tag n'est exécuté par ce document.

Chaque lot suit : spécification/ADR si nécessaire → moteur et tests → IHM →
qualification sur références figées → validation utilisateur → livraison
autorisée avec fusion `--no-ff`. Préserver les branches après intégration.
Tests ciblés au développement, suite complète en grosse modification et CI.
Une modification de recette ou de graphe de rendu impose des tests transverses.

Pour chaque panneau livré : aide contextuelle/mémos prévus pour la bêta,
chapitre du livre utilisateur, raccourcis sans collision, traduction suivant
le chantier documentaire, protocole et rapport unique par groupe, JSON de
mesures canoniques. Mettre à jour le manuel technique au changement effectif
des contrats ; ne pas y présenter cette cible comme l'architecture déjà livrée.

Un lot n'est terminé qu'avec persistance/annulation, aperçu/export, erreurs et
ressources qualifiés, limites annoncées et validation utilisateur. La bêta
n'est pas `v1` : la stabilité complète reste conditionnée à la validation des
panneaux sur les boîtiers cibles possédés par l'utilisateur. Aucun calendrier
ferme ni sélection de modèle n'est engagé par ce cadrage.

## 11. Premier lot de préparation — B0, 31 octobre 2026

La préparation peut commencer en alpha ; elle ne constitue pas le passage en
bêta. Référence de travail : `dev` à `b19b4c3`. Aucun changement de moteur n'est
livré par cette section. Le nettoyage local est tracé dans le
[compte rendu dédié](../reports/storage/workspace-cleanup-2026-10-31/README.md).

### Existant vérifié et écarts à qualifier

- `EditRecipe::Settings` et `RawDevelopment` portent déjà les réglages et la
  recette RAW : les étendre plutôt que créer une seconde recette pour les panneaux.
- `Sidecar::write` utilise déjà `QSaveFile` et écrit le format 9 ; la lecture
  accepte les versions 1 à 9. **Décision utilisateur du 1er novembre 2026
  (audit C-13) : conserver cette lecture, la conversion vers la recette
  courante et les tests de migration.** Écrire un seul format courant, sans
  maintenir d'ancien moteur photographique. L'en-tête mentionne encore le
  format 6 : corriger cette déclaration et qualifier les erreurs de conversion
  dans le lot moteur, sans supprimer la prise en charge des fichiers utilisateur.
- Le basculement expérimental `denoiseBeforeProfile` est explicitement de session,
  non persisté. Définir si une future recette bêta doit le conserver avant de
  promettre une restauration identique ; le comportement alpha n'est pas changé ici.
- Des annulations existent déjà pour le préchargement et l'export par lot.
  Elles ne démontrent pas un contrat d'annulation commun à tous les traitements,
  ni un historique annuler/refaire complet : ces deux points restent à auditer.

### Séquence concrète et critères de clôture

| Étape | Travail préparatoire | Preuve attendue avant intégration |
| --- | --- | --- |
| B0.1 | Inventorier commandes, persistance, tâches et caches à partir du code ; préciser propriétaire et durée de vie | Tableau existant/manquant, tests existants repérés, aucune nouvelle implémentation concurrente |
| B0.2 | Rédiger l'ADR recette/historique et stockage des actifs ; décider les coordonnées et l'alpha | Contrat relu avec les chantiers moteur/IHM, schéma et erreurs définis |
| B0.3 | Définir les interfaces de tâche : état, progression, annulation, révision source/recette, publication atomique | Tests déterministes avec tâche obsolète, annulation et fermeture, sans attente infinie |
| B0.4 | Implémenter le socle par modules, puis raccorder l'IHM | Rendu sans nouvelle opération inchangé, sauvegarde/relecture et annuler/refaire qualifiés |
| B0.5 | Clore les validations alpha restantes et faire valider le jalon | Matrice et JSON référencés, limites connues, accord utilisateur avant tag bêta |

Commencer par B0.1, puis B0.2/B0.3 ; ne pas lancer en parallèle les cinq panneaux
ou l'entraînement d'un réseau. Pour le premier développement, créer la branche
de socle depuis `dev` à jour et isoler son worktree ; ne pas coder le moteur sur
`patch_documentation`. La branche exacte et les raccordements sont arrêtés au
démarrage de ce lot, sans dupliquer une branche existante.

## 12. B7/B8/B9 — empilement, panorama et HDR

Ajout demandé le 31 octobre 2026. Reprendre le
[plan d'empilement et panorama](empilement-et-panorama.md) comme référence
détaillée, sans créer un second moteur multi-images par fonction. Les trois
modes sont distincts : horizontal/vertical décrit un panorama, pas le HDR.
Un panorama HDR combine les deux opérations et vient après leur qualification séparée.

- **B7 — empilement :** commencer par l'antibruit à exposition constante,
  puis qualifier séparément l'empilement de netteté. Choix d'image de référence
  et d'ancre/région par l'utilisateur, recalage sous-pixel, confiance et exclusion
  des mouvements ; recadrage automatique proposé sur la couverture commune.
  Un point seul n'impose pas une rotation ou une perspective : compléter les
  correspondances et signaler les échecs. Ciel et sol peuvent nécessiter des
  alignements distincts, à qualifier dans une évolution dédiée.
- **B8 — panorama :** un moteur pour horizontal et vertical, une rangée d'abord,
  puis multirangée. Utiliser l'union des vues, non leur intersection ; projection,
  compensation des écarts d'exposition, raccords et zones transparentes explicites.
- **B9 — HDR :** aligner des prises d'une même scène à expositions différentes,
  gérer saturations et objets mobiles sans fantômes. Distinguer fusion visuelle
  d'expositions et reconstruction radiométrique HDR. Pour cette dernière,
  qualifier une entrée avant les courbes non linéaires du profil ; un buffer
  Rec.2020 à transfert linéaire n'est pas nécessairement scene-linear.
  Séparer résultat HDR flottant et rendu SDR à dynamique comprimée ; ne pas
  assimiler la fusion à l'export HDR écran/fichier, qui demande un contrat propre.

Tous utilisent un **dossier tampon disque dédié par tâche**, avec tuiles,
budgets partagés RAM/disque, progression, annulation et nettoyage des seuls
temporaires possédés. Garder une recette persistante indépendante du tampon,
les sources intactes et une publication atomique de la nouvelle image éditable.

Qualification : déplacements connus, détails fins, sujets mobiles, bords,
expositions extrêmes, couleurs, halos, raccords, disque plein, annulation et
relecture après suppression du tampon. Établir un protocole et un rapport
évolutif par famille avec preuves JSON ; les rafales existantes ne valident
pas à elles seules le HDR ou les panoramas. Aucun nouvel export utilisateur
n'est demandé ni corpus déclaré complet à ce stade.

L'ordre interne est socle multi-source → empilement simple → panorama et HDR
qualifiés séparément → panorama HDR. Les numéros B7/B8/B9 ne signifient pas
qu'il faut attendre la fin de tous les réseaux : ces lots dépendent du socle B0,
pas de l'entraînement neuronal. Branches proposées, à vérifier au lancement :
`patch_stacking`, `patch_panorama`, `patch_hdr`. Rien n'est implémenté par ce plan.
