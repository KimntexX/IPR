# Nettoyage du worktree neuronal — 7 novembre 2026

Nettoyage demandé par l’utilisateur après l’inventaire de la branche
`patch_neural_restoration`. Périmètre exclusif : son dossier
`build-patch_neural_restoration`, vérifié par chemins absolus résolus.
Aucune intervention dans un autre worktree, sur les RAW ou sur les objets Git.

- [Plan préalable](plan.json) : liste des caches supprimables, tailles et
  SHA-256 ; groupes de checkpoints identiques et chemins à conserver.
- [Résultat de l’exécution](result.json) : opérations réellement terminées,
  erreurs éventuelles et espace libre avant/après.
- [Source exécutée](executed-script.txt) : trace du programme ponctuel,
  identifiée par son empreinte dans le plan ; ne pas réexécuter ce plan consommé.

Les 102 fichiers supprimables sont des sorties flottantes `.f32` de rendus
ou d’inférences, dans huit dossiers explicitement désignés. Leur suppression
ne conserve pas les pixels : les hashes ne sont pas une sauvegarde. Pour
reproduire exactement un ancien rendu, il faut les sources et la version
correspondante des outils, pas seulement la version actuelle du moteur.
Les aperçus visuels et les métadonnées restent disponibles.

Les doublons de checkpoints sont remplacés par des **liens physiques**
uniquement après égalité de taille et de SHA-256. Un lien temporaire est
créé et vérifié avant remplacement atomique du chemin ; une erreur de
création laisse donc la cible initiale intacte. Tous les noms restent
accessibles et tous les états distincts sont conservés. Le mécanisme a été
testé sur une petite fixture, y compris le refus d’un contenu différent et
d’un chemin hors périmètre.

Les checkpoints partagés doivent être traités comme des artefacts immuables.
Une future écriture doit utiliser un nouveau fichier et un remplacement
atomique, comme `dru_checkpoint.save`, plutôt qu’une modification en place
qui toucherait tous les liens du même contenu.

Conservés : JSON canoniques et métadonnées de build, sources/captures `.bin`,
captures CFA de `photo-comparison-v001`, cibles de
`sensor-learning-plan-v001/targets.f32`, tous les états distincts, code,
environnements CPU/CUDA et aperçus. Aucun entraînement ni push n’est effectué.
Les tailles logiques additionnent encore les liens physiques ; le gain
physique attendu et la variation de place libre doivent être distingués.

## Résultat vérifié

Nettoyage terminé : **102 caches supprimés**, **51 chemins de checkpoints
dédupliqués**, **287 chemins de checkpoints conservés**. Les contenus partagés
ont leurs empreintes vérifiées et leurs identités de fichiers confirmées.
Les cibles et les six captures CFA sources contrôlées sont présentes.
[Contrôle final](verification.json).

Gain constaté sur le volume : **39.75 Go**.
Le worktree représente désormais **41.06 Go logiques**, dont
**29.03 Go de fichiers distincts** après déduplication ; ces valeurs
ne sont pas une mesure des clusters NTFS ni des objets Git partagés.
