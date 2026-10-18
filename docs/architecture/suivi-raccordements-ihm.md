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
  Son accès avancé réutilise `RawCalibrationModule.qml`, également disponible
  comme raccourci dans Développement RAW. Le manifeste utilise le contrat existant.
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

## Journal

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
   unique réutilisé dans les réglages et les préférences. Portée par image indiquée
   explicitement ; évolution globale suivie sous `IHM-CONN-001`.
   Tests : `settingsQmlTests`, `uiPreferencesQmlTests` (ouverture du dialogue,
   thème réellement modifié, état de calibration synchronisé et indisponibilité
   sans RAW). Contrôle visuel souhaité : organisation et lisibilité du dialogue.

### 18 octobre 2026 — initialisation

Worktree et branche locale créés depuis `dev` à jour. Seules les consignes
et ce suivi documentaire sont ajoutés. Aucun fichier moteur, profil,
configuration de rendu ou image utilisateur n’est modifié ; aucune branche
n’est fusionnée ou publiée. Aucun build IHM ni test d’écran n’est annoncé
à ce stade : les corrections concrètes restent à définir.
