# Pilote U-Net bruité — lancement et reprise

Préparation du3 novembre2026 sur `patch_neural_restoration`, dans le
worktree `.worktrees/patch_neural_restoration`. Le
[rapport unique](../analyses/analyse-restauration-neuronale.md) porte les
résultats scientifiques ; ce document décrit l'exploitation du banc.
Les JSON horodatés restent la mémoire canonique.

## Départ uniquement au signal de l'utilisateur

L'utilisateur sera absent de **13h30 à17h30, heure de Paris**, et donnera
le signal de reprise. La préparation, les tests courts et le gel du plan
ne lancent pas la campagne. Aucun horaire ne vaut autorisation implicite.

Après ce signal, depuis le worktree, commande préparée :

```powershell
& .venv-neural/Scripts/python.exe tests/neural/run_noisy_campaign.py --plan build-patch_neural_restoration/noisy-moment-plan-v003/plan.json --output build-patch_neural_restoration/noisy-afternoon-v001 --until "2026-11-03T17:30:00+01:00"
```

Le dossier de sortie doit être nouveau. `--until` interdit **un nouveau
départ d'essai** après17h30 ; un essai déjà actif termine normalement,
avec une limite technique de30minutes par processus. Il ne s'agit pas
d'une extinction ou mise en veille du PC. Si le départ est décalé à une
autre journée, modifier explicitement la date et le fuseau de cet argument.

## Séquence préparée

1. Vérifier les empreintes du plan, des parents et des outils. Attendre
   deux relevés de RAM globale inférieure à90 %, espacés de15secondes,
   avant de charger PyTorch. Cette marge d'admission conserve l'arrêt
   autorisé à92 %, les plafonds2Gio engagés et1,25Gio privés/résidents.
2. Entraîner la graine71 jusqu'aux2048 étapes finales fixées, puis évaluer
   les624 observations et les contrôles historiques. Aucun choix du
   meilleur checkpoint selon ses résultats.
3. Faire de même avec73, même si les critères photographiques de71
   échouent : les deux initialisations doivent être rapportées.
4. Comparer bruit/couleur/structures, famille par famille, avec le parent
   figé et la correction désactivée. Conserver les échecs, biais et limites.
5. Si les deux graines passent les critères, préparer une confirmation
   bruitée distincte avant toute revue photographique ou intégration.
   Sinon diagnostiquer le critère en défaut avant de proposer un nouveau
   candidat ; ne pas ajuster les seuils ou entraîner plus longtemps à
   l'aveugle sur les mêmes contrôles.

Le lanceur automatise l'exécution et les reprises des points1à3. L'analyse
scientifique des points4et5 reste une étape de l'agent, à consigner dans
le rapport ; le lanceur ne modifie pas les modèles ni leurs critères.

## Sauvegarde et reprise technique

- Un fichier `training-latest.pt` est enregistré au départ, puis toutes
  les64 étapes d'Adam complètes et au terme2048. Un arrêt peut imposer
  de recalculer jusqu'à64 étapes depuis le dernier fichier valide,
  notamment si l'écriture suivante échoue.
- Il contient poids, Adam, étape, pertes, compteurs d'exposition et état
  aléatoire Torch CPU. Les tirages de bruit et le calendrier sont dérivés
  de l'indice d'étape. Le cache de données est reconstructible et n'est
  pas une source de hasard supplémentaire.
- Écriture dans un fichier temporaire fermé puis remplacement atomique :
  une erreur d'écriture conserve la sauvegarde précédente. Un fichier
  de reprise d'un autre plan ou d'une autre graine est refusé.
- Après arrêt mémoire uniquement, le lanceur attend à nouveau la marge
  d'admission, puis reprend la dernière sauvegarde dont l'empreinte est
  vérifiée. Chaque tentative a un nouveau dossier et un nouveau JSON.
  Limite de8 tentatives par graine ; pas de boucle de relances illimitée.
- Une erreur de calcul, une sortie sans résultat ou un délai de processus
  dépassé arrête la campagne. Un checkpoint déjà écrit reste conservé.
  Les anciens essais interrompus avant l'ajout de cette fonction ne
  possèdent pas de sauvegarde récupérable : le prochain départ sera neuf.

Une reprise manuelle reste possible, avec le **même plan** et un nouveau
dossier de sortie :

```powershell
& .venv-neural/Scripts/python.exe tests/neural/run_noisy_moment_pilot.py --plan build-patch_neural_restoration/noisy-moment-plan-v003/plan.json --seed 71 --resume build-patch_neural_restoration/noisy-afternoon-v001/seed71-attempt01/training-latest.pt --output build-patch_neural_restoration/noisy-manual-resume-v001
```

Ce chemin est un exemple : lire le checkpoint réel dans
`training_progress.checkpoint` du JSON de la tentative. La reprise n'est
pas une option de sélection photographique ou un ancien moteur à conserver.

## Preuves et limites

Le lanceur produit un journal par tentative et un `campaign.json` dans
le build. Une mémoire horodatée de campagne est aussi déposée dans
`docs/validation/raw/`. Le pilote conserve ses propres JSON canoniques,
avec mesures par cas et état de la reprise. Les checkpoints et journaux
opérationnels ne remplacent pas ces preuves scientifiques.

Les tests ont vérifié une interruption à l'intérieur d'un lot du vrai
cadenceur d'apprentissage : reprise et calcul continu produisent les
mêmes poids, moments d'Adam, pertes et état aléatoire au bit près sur
le petit banc de test CPU. Cela ne qualifie pas le débruitage photo ou
une reprise GPU. Aucune architecture, donnée de validation, graine ou
tolérance photographique n'est changée pour ajouter la reprise.

Aucune publication, fusion, création de tag, modification de production
ou mise en veille n'est autorisée par ce lanceur. Les travaux des autres
agents et les applications de l'utilisateur restent hors de son périmètre.

## Confirmation et cohérence des tuiles

Après réussite des deux graines sur le même protocole, utiliser
`tests/neural/confirm_noisy_model.py --prepare`. Les arguments requis sont
`--data-plan`, `--eligibility`, `--kind plain|protected`, deux dossiers
`--models`, les anciens plans de cohortes `--prior` et un nouveau `--output`.
L'outil refuse une preuve incomplète, des poids différents ou une seule
graine réussie. Le plan préparé fige de nouveaux contenus et trois
réalisations de bruit, sans réentraînement.

Exécuter ensuite `--plan <plan.json> --output <nouveau dossier>`.
En cas d'arrêt mémoire seulement, `--reuse <result.json>` conserve les
groupes complets du même plan. Les groupes négatifs restent négatifs ;
ne pas les relancer pour chercher une meilleure réalisation. Ne modifier
aucun outil dont l'empreinte est figée pendant la campagne.

L'audit `tests/neural/audit_neural_tiles.py` accepte `--data-plan`, les
deux dossiers `--models`, `--captures <preuve mosaic-inlet/result.json>`,
`--kind plain|protected` et un nouveau `--output`. Il mesure les écarts
de centres identiques entre fenêtres et vue 384. Sa réussite concerne
la cohérence technique des tuiles, pas la qualité d'une photographie.
Les valeurs sigma constantes utilisées n'estiment pas le bruit du boîtier.

Ces outils conservent des JSON horodatés sous `docs/validation/raw/`.
Le verdict de qualité, les interruptions mémoire, les écarts par cas et
la provenance restent séparés ; aucun échec n'est effacé par une moyenne.
