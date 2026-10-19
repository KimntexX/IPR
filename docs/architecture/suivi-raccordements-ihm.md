# IHM — suivi des raccordements au moteur

## Périmètre du chantier

Initialisé le **18 octobre 2026**, à la demande de l’utilisateur pour isoler
les agents qui travaillent simultanément.

- Branche : `patch_ui`, durable et dédiée à l’IHM.
- Worktree : `.worktrees/patch_ui`, indépendant des fichiers modifiés du dépôt principal.
- Base : `dev`, alignée sur `origin/dev` après actualisation ; commit
  `8efb6a4` (`v0.1.23-alpha`, selon l’intitulé du commit d’intégration).
- Aucune correction du moteur/master de traitement et aucune modification
  des branches `dev` ou `main` dans ce chantier.
- Pas de reprise des modifications locales des autres agents. Les bases Git
  et références sont partagées entre worktrees ; les fichiers et builds ne le sont pas.

Les composants QML utilisent les propriétés, signaux et commandes existants.
Une évolution du moteur nécessaire à l’IHM est une **demande de raccordement**,
pas une implémentation à effectuer discrètement sur cette branche.
Les règles applicables sont également consignées dans l’[AGENTS.md](../../AGENTS.md).

## Circuit de suivi

1. Décrire la correction IHM et le comportement attendu.
2. Vérifier si l’interface C++/QML actuelle suffit.
3. Si elle ne suffit pas, inscrire ici une demande `IHM-CONN-NNN` avec les
   composants concernés, les données/commandes attendues et les critères de test.
4. Confier l’implémentation au chantier moteur approprié, sans toucher à ses
   fichiers depuis ce worktree. Indiquer la branche et le commit livrés quand connus.
5. Après intégration validée dans `dev`, synchroniser `patch_ui` conformément
   au [workflow Git](gestion-branches-git.md), puis vérifier le raccordement réel.

États : **à spécifier**, **en attente moteur**, **moteur disponible**,
**raccordé côté IHM**, **validé**. Une maquette visuelle ou un test avec un
objet simulé ne vaut pas validation du raccordement de production.

## Demandes ouvertes

### IHM-CONN-001 — valeur par défaut globale de calibration RAW

- Date : 18 octobre 2026 ; état : **en attente moteur**, branche à attribuer.
- Le menu Préférences utilise actuellement `rawNormalizationEnabled` et
  `setRawNormalizationEnabled(bool)` : ce contrat porte sur **l’image ouverte**.
  Son accès avancé utilise `RawCalibrationModule.qml`, uniquement dans les
  préférences. Le manifeste utilise le contrat existant.
- Une préférence globale de calibration demanderait une propriété distincte,
  persistée dans la configuration application, et sa politique d’application
  aux nouveaux fichiers. Ne pas transformer implicitement le réglage de la photo
  courante en valeur par défaut ni écraser les choix explicites des sidecars.
- Critères de raccordement : redémarrage de l’application, RAW sans sidecar,
  RAW avec choix explicite activé/désactivé, boîtier non caractérisé et fichiers
  non RAW. Définir explicitement la priorité sidecar/préférence/défaut moteur.
- Aucun faux contrôle global n’est proposé dans l’IHM. L’explication de portée
  est affichée dans les préférences ; ce raccordement ne bloque pas les options
  générales d’affichage déjà opérationnelles.

Pour chaque demande, renseigner :

- identifiant, date et besoin utilisateur ;
- composants/fichiers IHM concernés ;
- contrat existant et évolution demandée (propriété, commande ou signal) ;
- formats, unités, valeurs limites et états de chargement/erreur attendus ;
- responsable/branche moteur, état et dépendances ;
- critères de vérification, preuve et commit de raccordement une fois connus.

## Améliorations IHM à prévoir

### Mémos explicatifs au survol prolongé

- Demande du 18 octobre 2026 ; état : **reporté à la phase bêta**, sur décision
  utilisateur. Ne pas intégrer ces mémos pendant la phase alpha actuelle.
- Ajouter une aide contextuelle après un survol prolongé des fonctions : rôle
  du réglage, effet des valeurs positives/négatives si pertinent, unité,
  valeur de réinitialisation et raccourcis disponibles.
- Employer un délai homogène, à ajuster après essai, pour ne pas afficher
  systématiquement les mémos lors d’un simple passage de la souris.
- Présenter les mémos en superposition, sans modifier la taille des groupes
  ni déplacer les commandes ; les masquer à la sortie du pointeur ou au début
  d’une manipulation. Respecter la palette et proposer aussi un accès clavier.
- Prévoir un composant commun et des textes propres à chaque fonction.
  Décrire le comportement réel d’IPR, sans promettre une équivalence Lightroom.
- Évolution de présentation QML a priori ; consigner un raccordement moteur
  uniquement si l’aide nécessite une information dynamique non exposée.

### Reprise prévue le 19 octobre 2026

- Reprendre les améliorations et la validation visuelle de l’IHM, avec les
  raccordements moteur encore ouverts comme points de suivi distincts.
- Commencer la création d’un **livre utilisateur** : prise en main, parcours
  ouverture/retouche/comparaison/export, préférences et raccourcis, avec
  illustrations du comportement réellement disponible. Le distinguer de la
  documentation technique et des cours Scholar ; sa structure reste à définir
  lors de la reprise. Aucun manuel n’est généré dans cette clôture de session.
- L’implémentation des mémos de survol reste réservée à la bêta ; la reprise
  du manuel ne vaut pas autorisation de les développer dès maintenant.

## Journal

### Livre utilisateur — paragraphes non coupés entre deux pages

Retour du 19 octobre 2026 : le bloc « Gestes et clavier » du mélangeur
commençait en bas de page et reprenait au milieu de sa phrase sur la suivante.
Le livre conserve désormais ses courts paragraphes sur une seule page ;
ils passent entièrement à la suivante si nécessaire, sans réduction du corps
ni étirement vertical pour remplir la page. Les tableaux multipages restent
autorisés. Les consignes de rédaction sous `docs/AGENTS.md` reprennent cette
règle pour les prochaines générations et reprises, sans recompilation globale
des autres livres. Les passages futurs dépassant une page devront être
restructurés plutôt que forcés dans un bloc insécable.

### 19 octobre 2026 — livre utilisateur par familles

Création du [livre utilisateur](../user/README.md), indépendant des cours et
du manuel technique : 19 chapitres et deux annexes, PDF local de 44 pages.
Chaque famille de réglages a son chapitre ; recadrage/géométrie, masques et IA
reprennent les familles réservées du manuel technique et restent vides.
Les raccourcis réellement déclarés et les gestes sont expliqués dans les
parcours puis regroupés en aide-mémoire. Les valeurs de référence non nulles,
la portée par image de la calibration, les sidecars, les limites de l’aperçu,
la sélection multiple et les formats d’export sont explicités.

Compilation LaTeX en trois passes, contrôle des liens et des références,
inspection visuelle du schéma de disposition, des tableaux, de l’aide-mémoire
et d’un chapitre réservé. Mise en page 12 pt, interligne 1,5, chapitres sur
une nouvelle page. Sources et lanceur versionnés, PDF sous le `build/` local
ignoré ; aucune traduction anglaise produite par ce chantier.

Les demandes de raccordement moteur restent ouvertes : aucun indicateur de
fin de rendu exact, badge complet de toutes les vignettes ou état exhaustif
de recette n’est simulé en QML. Les mémos restent reportés à la bêta.
Cette reprise est documentaire, sans modification du moteur ni nouvelle
commande IHM ; aucun rebuild de l’application n’est nécessaire pour lire le livre.

### Stabilité des réglages pendant les traitements

Retour utilisateur : le remplacement temporaire de la description RAW / Profil
par « Traitement en cours » changeait le nombre de lignes et déplaçait les curseurs
situés en dessous. Ce remplacement est supprimé. La section conserve la description
du profil effectivement utilisé ; seul le badge sur la zone image et le pied de
fenêtre portent les informations transitoires. Les nouvelles descriptions et
avertissements de profil restent affichés lorsque le développement change réellement.
Test ciblé : texte, hauteur et position du groupe Lumière avant, pendant et après
un rendu, à trois largeurs. Aucun changement du moteur.

### Vérification transversale des sept étapes

Les sept améliorations sont isolées en commits successifs sur `patch_ui`.
Les raccordements différés `IHM-CONN-003` à `006` restent documentés, sans
modification C++ du moteur. Les tests couvrent aussi le glissement réel d’une
poignée, les deux états de sélection, la palette du récapitulatif d’export,
l’annulation et l’identité périmée d’une photo. Les captures sont synthétiques.

Le test de double-clic réel a révélé un conflit entre le reset et le traitement
du relâchement par Slider : celui-ci réappliquait la position précédente.
Le reset du curseur est maintenant différé à la fin de cette livraison d’événement
(`Qt.callLater`), après le second clic, sans modifier le calcul photographique.
Le reset sur le nom reste immédiat. Le test ne se limite donc pas à invoquer
directement la fonction de réinitialisation.

Vérification finale : six tests CTest IHM réussis ; seize tests Python de
sélection réussis ; contrôle documentaire sans erreur ni lien manquant.
Préférences Basic et Windows vérifiées hors écran à 100 %, 125 % et 150 %.
Les captures Windows hors écran chargent explicitement Segoe UI dans le test
(aucun changement de police imposé à l’application). Le dialogue d’export
utilise la largeur disponible de l’overlay, pas celle du panneau Réglages.
Build `build-patch_ui/Release/appIPR.exe` reconstruit ; démarrage isolé
`--smoke-test` réussi, code 0 et stderr vide, sans chemins de développement
dans le PATH. INI personnel préservé par des préférences temporaires de test.
Validation visuelle utilisateur encore attendue, aucune publication ni fusion.

### Ergonomie — étape 7 : récapitulatif d’export

Après choix natif du fichier, un dialogue aux couleurs de l’interface récapitule
format, dimensions pleine résolution, encodage et destination demandée.
JPEG : sRGB 8 bits et qualité ; PNG/TIFF : sRGB 16 bits ; DNG : rendu linéaire
16 bits de primaires sRGB, explicitement distinct du RAW mosaïqué d’origine.
L’écriture ne démarre qu’au bouton Exporter. Annuler n’écrit rien. Les protections
existantes d’extension et de numérotation restent assurées par l’exporteur.
Confirmation désactivée pendant un traitement ou si la photo a changé.

**IHM-CONN-006 — en attente moteur** : fournir un descripteur d’export
canonique (formats/capacités réelles, profondeur, profil ICC, dimensions,
recette et identifiant d’image/révision) ainsi qu’un signal typé de résultat
avec le chemin effectivement publié. Les libellés actuels décrivent l’exporteur
audité ; toute évolution de ses espaces/profondeurs devra les actualiser jusqu’au
raccordement. L’IHM ne prédit pas le nom final en cas de collision concurrente.

### Ergonomie — étape 6 : lisibilité et adaptation

Les cinq palettes sont testées numériquement : contraste minimal 4,5:1 des
textes principaux et secondaires sur panneaux et champs. Focus clavier renforcé
par un contour de 2 px sur les contrôles communs ; poignées, origine et remplissage
des sliders suivent la palette au lieu de gris fixes. Les bandes photographiques
gardent leurs couleurs de signification.
La barre de zoom/comparaison passe sur plusieurs rangées lorsque la zone image
rétrécit ; le badge de traitement et les légendes sont décalés sous elle.
Tests des commandes dans des vues de 280, 350, 500 et 800 px. Ces vérifications
automatiques ne constituent pas une certification d’accessibilité complète ni
un contrôle visuel sur toutes les configurations de moniteurs.

### Ergonomie — étape 5 : ruban et états distincts

L’image effectivement ouverte porte un contour et un nom gras ; les cases et
le fond teinté désignent séparément la sélection multiple, avec compteur.
Un clic ne marque plus une photo comme ouverte avant la notification du moteur.
Les vignettes s’adaptent à la hauteur du ruban. Infobulles avec nom complet et
état ; badge Ouverte avec note, drapeau et libellé connus de la photo courante.

**IHM-CONN-005 — en attente moteur** : `ImageListModel` expose seulement nom
et URL. Ajouter des rôles légers pour note, libellé couleur, drapeau et présence
de retouches, avec `dataChanged` ciblé après édition/restauration. Ne pas décoder
les photos pour les lire. Sans ces rôles, aucun badge inventé sur les autres
images ni faux « non retouché » ; pas de lecture de sidecars en QML. Tester
navigation, sélection indépendante, changement de dossier et grands corpus.

### Ergonomie — étape 4 : panneaux redimensionnables

Poignées entre les trois panneaux et au-dessus du ruban : glisser à la souris,
flèches au clavier (10 px), double-clic pour restaurer la taille usuelle.
Les limites existantes restent 210–300 px pour Informations, 260–360 pour
Réglages et 100–260 pour le ruban. Les clés INI existantes et leur écriture
différée sont réutilisées ; rien dans le moteur ni dans les recettes photo.
Le bouton du ruban reste au-dessus, séparé de la poignée horizontale.
Tests : bornes et restauration depuis une nouvelle instance des préférences.

### Ergonomie — étape 3 : original temporaire

Maintenir Espace lorsque la vue image a le focus, ou le bouton Origine,
affiche la référence d’ouverture existante en plein cadre. Relâcher restaure
le comparatif éventuel, son séparateur, le zoom et le déplacement, sans rendu
ni modification de recette. Ce n’est pas le RAW brut : la référence conserve
le développement de base. La commande reste locale à la vue pour ne pas
intercepter les espaces dans les champs et dialogues ; perte de focus, masquage,
changement de photo ou désactivation de la fenêtre annulent le maintien.
Tests : pixels synthétiques avant/après, appui/relâchement réel et transformée
conservée. Aucun raccordement moteur nécessaire.

### Ergonomie — étape 2 : manipulation des réglages

Double-clic sur le nom ou le curseur pour revenir à la valeur de réinitialisation
(température de référence, rayon 1 et qualité 92 conservés). Focus clavier visible,
flèches au pas déclaré, nom accessible ; le bouton de réinitialisation reste présent.
Le nom devient gras et la section porte un point si un de ses contrôles numériques
diffère de sa référence, y compris après restauration d’un fichier.
L’indication ne prétend pas couvrir les courbes, profils, cases ou recettes entières.

**IHM-CONN-004 — en attente moteur** : exposer une comparaison canonique des
recettes par groupe, avec référence neutre/restaurée explicitement définie,
pour inclure courbes, optique, balance automatique et profils dans un futur
indicateur exhaustif. Tester restauration, réinitialisation, modification puis
retour au neutre et changement de photo. Aucun faux état « fichier enregistré ».

### Ergonomie — étape 1 : retour sur les traitements

Badge de traitement après 180 ms pour éviter les clignotements, mention immédiate
de l’aperçu boîtier, détails au survol et maintien de la texture précédente lors
du chargement asynchrone d’un nouvel aperçu (`retainWhileLoading`, Qt 6.8+).
Le badge utilise `busy` : aucune progression fictive ni promesse de succès à
l’arrêt. Les erreurs détaillées restent dans le pied de fenêtre.
Le moteur écarte déjà les rendus dont la révision ne correspond plus à la
demande courante ; ce mécanisme n’est pas réimplémenté dans l’IHM.

**IHM-CONN-003 — en attente moteur** : exposer un état typé distinct pour
ouverture, développement, aperçu, attente mémoire et export, ainsi que les
révisions demandée/publiée et la dernière erreur. `busy` agrège actuellement
ces opérations : l’IHM affiche donc « Traitement en cours », pas un diagnostic
déduit des messages traduits. Vérifier réglages rapides, échec, export concurrent,
changement d’image et disponibilité effective de la texture avant tout futur
indicateur « aperçu à jour ». Aucune modification du moteur dans cette étape.

### Histogramme analytique amont des courbes — IHM-CONN-002

État : **à spécifier**, évolution moteur éventuelle, non nécessaire au fond
illustratif demandé. Les courbes opèrent sur le master linéaire Rec.2020,
alors que le seul histogramme actuellement exposé à l’IHM décrit l’aperçu sRGB
après traitement. Un diagnostic exact de leur signal d’entrée demanderait un
histogramme amont distinct, avec espace, domaine, bornes, nombre de classes,
gestion des valeurs négatives/supérieures à 1 et notifications définis.
Ne pas déduire ce signal à partir de l’aperçu. Aucune nouvelle propriété moteur
n’est implémentée sur `patch_ui` ; le fond ci-dessous est explicitement indicatif.

### Navigation et fond de courbe — retours visuels

Navigation : boutons précédent/suivant carrés de 28 px, espacement 6 px et
marges de 12 px dans le coin inférieur droit de la zone image. Le composant
`ChevronIcon.qml` dessine les flèches au centre géométrique, sans dépendance
aux glyphes ou métriques d’une police. Il équipe aussi le bouton du ruban.
Tests : position des contrôles, centrage des icônes, palette et navigation réelle.

Courbes : une seule silhouette de luminance, gris atténué, derrière la grille
et les poignées ; les canaux RGB ne sont pas superposés, même lorsqu’une courbe
R/G/B est sélectionnée. Les effectifs sont normalisés au maximum de luminance.
Hauteur maximale 72 % du graphe et opacité 18 % : choix purement visuels pour
préserver la lisibilité de la courbe, sans influence sur les pixels.
L’infobulle précise la provenance sRGB après rendu et la limite de ce repère.
Le fond se rafraîchit avec l’histogramme courant et disparaît sans données.
Tests : indépendance aux pics RGB, données vides, liaison à l’histogramme de
l’éditeur et rafraîchissement lors du changement de photo. La capture de test
`tone-curve-histogram.png` utilise un histogramme synthétique, pas une mesure.

### Corrections IHM — demande du 18 octobre 2026

Validation visuelle utilisateur à réaliser sur le build isolé `build-patch_ui`.
Les vérifications automatisées sont consignées ci-dessous à chaque étape.

1. **Optique** : composant `OpticsModule.qml`, section indépendante repliable,
   état mémorisé dans `sections/optics`. Contrats C++ existants conservés.
   Test ciblé : `settingsQmlTests` (emplacement exclusif des commandes, largeur,
   repliement et conservation des réglages).
2. **Ruban** : repliement mémorisé, onglet de réouverture, flèches au bas droit
   de la zone image et nom de la cible au survol. Navigation sans chargement
   de vignettes hors écran, bornée aux extrémités et suspendue pendant le chargement.
   `uiPreferencesQmlTests` vérifie deux fichiers réels, aller/retour, cibles,
   repliement et réouverture. Contrôle visuel souhaité : position et infobulles.
3. **Sliders** : remplissage depuis zéro pour les paramètres bipolaires,
   repère central discret et alignement du trait sur le centre du curseur.
   Les paramètres uniquement positifs gardent une origine au minimum.
   `settingsQmlTests` vérifie la géométrie à −100, −50, 0, +50 et +100.
   Aucun changement de domaine numérique ou de calcul photographique.
4. **Bandes colorées** : température bleu/orange, nuance vert/magenta,
   vibrance/saturation gris vers couleurs ; mélangeur suivant la bande choisie.
   Repères indicatifs uniquement, sans modifier le rendu ni prétendre reproduire
   le calcul propriétaire Lightroom. `settingsQmlTests` vérifie leur présence
   et leur absence sur les curseurs de lumière. Contrôle visuel souhaité : lisibilité.
5. **Préférences** : menu dédié et raccourci Ctrl+virgule, affichage des panneaux,
   couleur du fond et largeur du panneau persistés dans le fichier INI existant.
   Calibration et manifeste accessibles pour l’image RAW ouverte, via un composant
   unique réservé aux préférences avancées. Portée par image indiquée
   explicitement ; évolution globale suivie sous `IHM-CONN-001`.
   Tests : `settingsQmlTests`, `uiPreferencesQmlTests` (ouverture du dialogue,
   thème réellement modifié, état de calibration synchronisé et indisponibilité
   sans RAW). Contrôle visuel souhaité : organisation et lisibilité du dialogue.
6. **Histogramme fixe** : titre, histogramme avec informations de prise de vue
   et réinitialisation placés hors du défilement. Seuls les groupes défilent.
   `settingsQmlTests` déplace réellement le contenu défilant et contrôle que
   l’histogramme conserve sa position, aux trois largeurs de panneau testées.
   L’option Affichage/Histogramme reste fonctionnelle.

### Vérifications du build IHM isolé

Retours utilisateur — seconde passe du 18 octobre : calibration, manifeste
et réinitialisation profil/calibration retirés des réglages ordinaires et
conservés exclusivement dans les options avancées des préférences. Le test
des réglages en vérifie l’absence ; le test des préférences garde le contrôle
de synchronisation avec l’état réel de l’image.

Autres ajustements de cette passe :

- Un seul bouton centré au-dessus du ruban commande son repliement ; il
  revient au bas de la vue pour la réouverture quand le ruban est masqué.
  Aucun chevauchement avec les vignettes ou les informations de la photo.
- Étoiles : glyphes de 17 à 18 px (arrondi d’environ +5 %), zone cliquable
  légèrement agrandie ; notation et suppression de la note vérifiées.
- Texte des informations réglable de **10 à 20 px**, valeur initiale 12 px,
  clé INI `layout/imageInfoFontSize`. La validation, la sauvegarde et la
  restauration sont gérées par `UiPreferences`, sans modification du moteur.
- Cinq palettes d’interface : bordeaux, blanc cassé, noir mat, bleu marine,
  gris actuel. Les clés INI existantes `theme/background`, `theme/panel`,
  `theme/text`, `theme/secondaryText` font foi ; aucune copie parallèle de thème.
  La couleur `theme/canvas` de la zone photo reste indépendante. Les textes
  et surfaces des contrôles héritent de la palette ; tests des cinq choix et
  de leur persistance, captures de contrôle pour les variantes claire et marine.
- Corrections optiques déplacées immédiatement avant Export. Le test des
  réglages vérifie l’ordre réel des composants, pas seulement leur existence.

État de livraison de cette seconde passe : les cinq tests ciblés et le contrôle
documentaire réussissent. Après fermeture de l’instance utilisateur qui bloquait
la liaison (`LNK1104`), la cible `appIPR` est reconstruite avec succès.
**L’exécutable intègre maintenant cette seconde passe**. Son démarrage isolé
hors du dossier de l’application, avec uniquement les dossiers système Windows
dans le PATH, réussit (code 0, aucun message d’erreur QML).
Le fichier INI personnel est préservé : les nouveaux défauts embarqués
sont complétés automatiquement au lancement par `UiPreferences`.

### Préférences — harmonisation après retour visuel

Trois sous-menus (onglets) remplacent la liste unique : **Interface**
(panneaux, dimensions, taille des informations), **Apparence** (palettes et
fond photo indépendant), **Avancé · RAW** (calibration de l’image ouverte).
Chaque page possède son propre composant QML et défilement vertical discret
de 6 px ; aucun défilement horizontal. Les pages restent instanciées pour
préserver les valeurs quand on change d’onglet.

Le dialogue est rattaché à l’overlay Qt : sa palette est désormais liée
explicitement aux couleurs INI. Les contrôles `PreferenceButton`,
`PreferenceCheckBox`, `PreferenceSpinBox`, `PreferenceComboBox` et
`PreferenceScrollView` utilisent Qt Quick Controls Basic avec une présentation
explicite. Cela évite les contrôles blancs natifs Windows vus dans la capture
utilisateur. Fond, texte, contours, liste déroulante et bouton Fermer suivent
la palette ; les cinq thèmes restent applicables immédiatement au dialogue.
Tous les nouveaux fichiers sont déclarés dans CMake. Aucune évolution moteur.

Vérifications : navigation dans les trois pages, largeur contenue, barre fine,
thème des contrôles pour chaque palette, modification au clavier et saisie
validée avec Entrée dans les champs numériques, persistance INI existante.
`uiPreferencesWindowsStyleTests` exécute les mêmes vérifications sous le style
Windows en plus de `uiPreferencesQmlTests` sous Basic. Ces tests restent hors
écran : ils ne remplacent pas la validation visuelle utilisateur. Les nouveaux
composants sont associés au test ciblé des préférences dans les règles locales ;
le test spécifique Windows est inclus dans les suites complètes de cet OS.

Build `build-patch_ui/Release/appIPR.exe` reconstruit et démarrage isolé réussi
(code 0, aucun message d’erreur QML). Six tests CTest ciblés réussis, dont les
deux variantes Basic/Windows des préférences ; seize tests Python de sélection
des tests réussis. Contrôle documentaire : zéro erreur et zéro lien manquant.
Le fichier INI utilisateur reste inchangé par les opérations de build.

### Fonds de la zone image — bleu et rouge

Ajout des choix **Bleu marine — dégradé** et **Rouge bordeaux — dégradé**
dans Préférences / Apparence. Les deux dégradés verticaux reprennent les
teintes panneau vers arrière-plan des palettes correspondantes :
`#1b2d45 → #111c2b` et `#42252e → #291a20`. Les quatre fonds neutres et les
couleurs personnalisées restent unis. Le catalogue `ImageBackgroundOptions.qml`
est partagé entre le sélecteur et le fond `ImageCanvasBackground.qml`.

Le choix est conservé dans la clé INI existante `theme/canvas` ; changer la
palette d’interface ne le remplace pas. Aucun effet sur les pixels, les profils
ou les exports. Un fond gris neutre reste préférable pour une appréciation
colorimétrique non influencée par un entourage coloré. Tests Basic et Windows :
deux extrémités du dégradé, indépendance des panneaux, persistance INI et retour
à un gris uni. Les fichiers sont déclarés dans CMake et les tests ciblés.
Build Windows reconstruit, six tests IHM ciblés réussis et démarrage isolé
vérifié (code 0, sans erreur QML). Fichier INI utilisateur préservé.

### Histogramme — présentation et repères tonals

La présentation utilise des courbes RGB plus lisibles et un remplissage de
luminance discret ; la hauteur est désormais proportionnelle aux effectifs
(normalisation au maximum commun), et non logarithmique. Les données du moteur
ne changent pas : 256 classes sur l’aperçu sRGB après rendu, luminance linéaire
calculée par le moteur puis réencodée en sRGB pour son abscisse. Ce n’est pas un
histogramme du RAW ni une reproduction du calcul interne de Lightroom.

Le tableau utilisateur comporte **cinq plages**, dont l’exposition centrale :
Noirs 0–10 %, Ombres 10–33 %, Exposition 33–66 %, Hautes lumières 66–90 %,
Blancs 90–100 %. Le survol montre le nom, les intervalles approximatifs en
luminance linéaire et en EV sous le blanc. Ces limites sont des repères de
lecture, pas la définition des masques ou des plages d’action des curseurs.
Les triangles indiquent la présence de valeurs RGB à 0/255 dans l’aperçu,
sans prétendre mesurer la saturation du capteur ou les réserves du master.
L’exposition affichée suit le curseur EV. Histogramme et métadonnées restent
fixes en haut du panneau. Tests : proportionnalité des effectifs, limites des
cinq plages et état des indicateurs d’extrémité ; aucune modification moteur.

### Commandes et textes — harmonisation des palettes

La balance des blancs automatique est proposée une seule fois, dans
Développement RAW / Profil. Le doublon et son rappel de synchronisation sont
retirés du groupe Couleur ; température et nuance manuelles restent présentes.

Les boutons d’ouverture, d’export, de réinitialisation (globale, paramètres,
courbes), de navigation, de zoom/comparaison et de sélection du ruban utilisent
une présentation liée à la palette. Les menus de l’application et les contrôles
de choix s’appuient sur Basic pour éviter les substitutions natives Windows
blanches. Texte, fond, contours, survol et état désactivé suivent le thème.
Les canaux RGB, roues, couleurs de libellés et graphiques conservent leurs
couleurs de signification ; leurs bordures et textes s’adaptent. Les étoiles
gardent une teinte dorée, assombrie sur une palette claire pour rester lisibles.
Les dialogues natifs du système (ouverture/enregistrement) restent gérés par l’OS.

Les six tests IHM réussissent, y compris vérification des boutons ouvrir,
réinitialiser et exporter sur les cinq palettes et sous les styles Basic et
Windows. Tests des réglages : balance automatique unique dans le groupe RAW.
La régression zoom/comparaison et la largeur des groupes restent couvertes.

Revue des captures : correction de la hauteur utile des champs de valeur,
du chevauchement entre le bandeau d’information et les flèches de navigation,
et harmonisation du titre/pied du dialogue Préférences. Régressions couvertes
dans les tests ciblés ; l’appréciation ergonomique finale reste utilisateur.

- Exécutable : `.worktrees/patch_ui/build-patch_ui/Release/appIPR.exe`.
  Bibliothèques Qt, OpenCV, LibRaw/TIFF et dépendances déployées localement,
  configuration INI/RAW dans `Release/config` et répertoires de profils prêts.
  Aucun profil propriétaire n’est embarqué depuis un autre worktree ;
  la découverte Adobe locale du moteur reste disponible.
- Six tests ciblés réussis : `settingsQmlTests`, `uiPreferencesQmlTests`,
  `uiPreferencesWindowsStyleTests`, `colorMixerQmlTests`, `imageViewportQmlTests`,
  `uiPreferencesTests`.
- Démarrage `appIPR.exe --smoke-test` réussi hors du dossier de l’application,
  sans chemins Qt/OpenCV/vcpkg dans le PATH ; aucun message d’erreur QML.
  Vérification hors écran, pas certification GPU ni machine vierge.
- Vérification documentaire : zéro erreur et zéro avertissement de lien.
- Captures optionnelles des tests sous `build-patch_ui/ui-review` ; variable
  `IPR_UI_CAPTURE_DIRECTORY`. Elles utilisent des fichiers synthétiques,
  pas un corpus de validation photographique.
- Validation visuelle utilisateur **en attente** : emplacement Optique,
  repliement/réouverture et noms au survol, traits signés et bandes colorées,
  préférences, histogramme fixe avec tous les groupes ouverts.
- Aucun changement moteur, de calcul, de cache, de profil ou de contrat C++ du
  moteur ; seule la configuration IHM `UiPreferences` reçoit la taille du texte.
  aucune fusion, publication ou création de tag.

### 18 octobre 2026 — initialisation

Worktree et branche locale créés depuis `dev` à jour. Seules les consignes
et ce suivi documentaire sont ajoutés. Aucun fichier moteur, profil,
configuration de rendu ou image utilisateur n’est modifié ; aucune branche
n’est fusionnée ou publiée. Aucun build IHM ni test d’écran n’est annoncé
à ce stade : les corrections concrètes restent à définir.
