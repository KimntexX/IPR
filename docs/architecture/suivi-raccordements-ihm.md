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

Aucune demande fonctionnelle précise n’est encore spécifiée : cette action
prépare l’isolation du chantier, elle ne corrige aucun écran ni calcul moteur.

Pour chaque demande, renseigner :

- identifiant, date et besoin utilisateur ;
- composants/fichiers IHM concernés ;
- contrat existant et évolution demandée (propriété, commande ou signal) ;
- formats, unités, valeurs limites et états de chargement/erreur attendus ;
- responsable/branche moteur, état et dépendances ;
- critères de vérification, preuve et commit de raccordement une fois connus.

## Journal

### 18 octobre 2026 — initialisation

Worktree et branche locale créés depuis `dev` à jour. Seules les consignes
et ce suivi documentaire sont ajoutés. Aucun fichier moteur, profil,
configuration de rendu ou image utilisateur n’est modifié ; aucune branche
n’est fusionnée ou publiée. Aucun build IHM ni test d’écran n’est annoncé
à ce stade : les corrections concrètes restent à définir.
