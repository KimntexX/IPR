# Pilote U-Net bruité — lancement et reprise

Préparation du 4 novembre 2026 sur `patch_neural_restoration`, dans le
worktree `.worktrees/patch_neural_restoration`. Le
[rapport unique](../analyses/analyse-restauration-neuronale.md) porte les
résultats scientifiques ; ce document décrit l'exploitation du banc.
Les JSON horodatés restent la mémoire canonique.

## Lancement de la campagne courante

Essai courant du 5 novembre : contrôle aléatoire frais contre couverture
stratifiée, avec branche de corrélation apprise dans les deux bras.
Préparation déjà réalisée et vérifiée (ne pas écraser ses sorties) :

```powershell
& .venv-neural/Scripts/python.exe tests/neural/prepare_coverage_continuation.py --output build-patch_neural_restoration/paired-coverage-plan-v001
& .venv-neural/Scripts/python.exe tests/neural/run_paired_continuation.py --prepared build-patch_neural_restoration/paired-coverage-plan-v001 --output build-patch_neural_restoration/paired-coverage-campaign-v001 --prefix coverage --candidate stratified
```

Ces noms désignent la campagne figée. Les 822 textures par bras portent
une identité de contenu distincte de l'exposition ; ne pas remplacer cette
identité par le numéro de pas. Les confirmations excluent les contenus
des deux bras et de leur ascendance. Les marges, critères et protections
mémoire décrits ci-dessous restent applicables.

### Campagne précédente à entrées de corrélation

Le lancement courant est autorisé par la demande de poursuivre en autonomie.
Un horaire seul ne vaut pas autorisation. Le plan schéma4
exige un prétest réussi du calcul courant avant l'apprentissage long.
Les anciens plans du correcteur local sont des preuves historiques.

Depuis le worktree, la préparation exige le prétest courant :

```powershell
& .venv-neural/Scripts/python.exe tests/neural/prepare_warm_continuation.py --output build-patch_neural_restoration/paired-correlation-plan-v002 --engineering build-patch_neural_restoration/correlation-engineering-v001/result.json
& .venv-neural/Scripts/python.exe tests/neural/run_paired_continuation.py --prepared build-patch_neural_restoration/paired-correlation-plan-v002 --output build-patch_neural_restoration/paired-correlation-campaign-v001 --prefix input-cues --candidate correlation
```

Le plan indiqué est déjà préparé et la campagne a terminé le 5 novembre :
ne relancer aucune des deux commandes dans leurs dossiers existants.
Pour une nouvelle préparation, choisir de nouveaux
noms de plan, sortie et préfixe. Le pilote apparié entraîne le contrôle et
le candidat avant leurs évaluations ; sa fenêtre de départ est de deux heures.
Le dossier de sortie doit être nouveau. En lancement direct d'une campagne,
`run_noisy_campaign.py --until` interdit **un nouveau
départ d'essai** après l'heure indiquée ; un essai déjà actif termine normalement,
avec une limite technique de30minutes par processus. Il ne s'agit pas
d'une extinction ou mise en veille du PC. Si le départ est décalé à une
autre journée, modifier explicitement la date et le fuseau de cet argument.

## Séquence préparée

1. Vérifier les empreintes du plan, des parents et des outils. Attendre
   deux relevés de RAM globale inférieure à90 %, espacés de15secondes,
   avant de charger PyTorch. Cette marge d'admission conserve l'arrêt
   autorisé à92 %, les plafonds2Gio engagés et1,25Gio privés/résidents.
2. Reprendre les poids à 2 048 étapes, puis effectuer 512 mises à jour
   par graine (71/73) et par bras : contrôle à projection statistique
   figée nulle, candidat à projection apprise. Adam est neuf à 0,00025,
   les exemples et la supervision simple sont identiques. Les nouveaux
   paramètres ne reçoivent que 512 étapes ; les anciens en cumulent 2 560.
3. Terminer les deux bras et graines avant de les évaluer. Retenir leurs
   derniers poids, sans sélection selon un score.
4. Rejouer le développement puis les textures fines. Comparer bruit,
   couleur et structures par famille ; garder les échecs et les biais.
5. Ne poursuivre vers les cohortes réservées que si tous les critères
   précédents passent. Sinon diagnostiquer sans modifier les seuils.
   Les empreintes exclues couvrent toute l'ascendance, pas seulement
   les exemples du préfixe rejoué. Aucun rejeu connu n'est un jeu réservé.

Le lanceur automatise l'exécution et les reprises des points1à3. L'analyse
scientifique des points4et5 reste une étape de l'agent, à consigner dans
le rapport ; le lanceur ne modifie pas les modèles ni leurs critères.

## Sauvegarde et reprise technique

- Un fichier `training-latest.pt` est enregistré au départ, puis toutes
  les64 étapes d'Adam complètes et au terme du stage512. Un arrêt peut imposer
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
& .venv-neural/Scripts/python.exe tests/neural/run_noisy_moment_pilot.py --plan build-patch_neural_restoration/paired-correlation-plan-v002/control/plan.json --seed 71 --resume build-patch_neural_restoration/input-cues-control-campaign-v001/seed71-attempt01/training-latest.pt --output build-patch_neural_restoration/input-cues-control-manual-resume-v001
```

Ce chemin est un exemple : lire le checkpoint réel dans
`training_progress.checkpoint` du JSON de la tentative. La reprise n'est
pas une option de sélection photographique ou un ancien moteur à conserver.
Les lecteurs utilisent `campaign_artifacts.campaign_models` pour résoudre
la dernière tentative complète avec continuité des empreintes et du
checkpoint, jamais un chemin `attempt01` fixé pour toute la campagne.

Le correcteur utilise désormais un contexte U-Net à deux réductions.
Les fenêtres d'apprentissage sont256pixels, mais la perte garde le même
centre32qu'avant (bord112). Les tirages d'indices restent calculés pour
la taille source128 ; les anciens centres RGB/CFA/sigma sont copiés
exactement dans un halo analytique à bruit indépendant. La qualification
classique mesure ces centres128après calcul sur320 ; les contrôles fins
gardent leurs centres192après calcul sur384. La marge minimale vérifiée
est112pixels (128 est aussi exigé dans l'audit ; 96 est trop court pour
les statistiques ajoutées). Ne pas utiliser les anciennes petites fenêtres comme si
elles apportaient tout ce contexte.

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

Pour rejouer une cohorte connue, préparer son plan avec
`tests/neural/prepare_neural_regression.py --source <preuve courante> --historical <ancien result.json> --kind classic|fine --output <nouveau dossier>`.
L'outil conserve les entrées et critères, remplace les poids/contrats par
ceux du candidat courant et marque le rôle `regression`. Exécuter le
plan avec `confirm_noisy_model.py` ou `confirm_fine_noise.py` selon son type.
Les verdicts négatifs historiques ne sont jamais exclus de ce rejeu.

La confirmation fine se prépare par
`confirm_fine_noise.py --prepare --proof <preuve courante> --cohort <indice> --prior <anciens plans fins> --output <nouveau dossier>`.
Le bloc de cohorte change les fréquences, angles, phase et réalisations
de bruit. Les anciens contenus et ceux de l'apprentissage sont exclus
par empreinte. Les critères de transfert de chaque canal et d'erreur CFA/RGB
sont figés avant l'inférence. `--reuse <result.json>` reprend seulement
les observations complètes d'un arrêt mémoire du même plan, sans filtrer
les résultats défavorables. Ne pas reprendre l'apprentissage sur une
cohorte encore présentée comme réservée.
