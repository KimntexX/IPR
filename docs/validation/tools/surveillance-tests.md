*[Français](surveillance-tests.md) | [English](surveillance-tests_en.md)*

# Fin de test et analyse automatique

État du 1er novembre 2026 : lanceur préparé sur `patch_raw_bench`, fichier
`scripts/run_test_then_analyse.py`. Python standard uniquement. Aucun raccordement
à l'IHM ou au moteur photographique, aucune tâche planifiée installée.

## Principe

Le lanceur exécute une commande de test au premier plan, attend sa sortie via
le système d'exploitation, puis écrit son statut et ses journaux. Il ne demande
rien au modèle pendant cette attente. Le test doit attendre ses propres enfants :
ne pas fournir une commande qui détache le vrai calcul puis quitte immédiatement.

Avec l'option explicite `--analyse`, il lance ensuite **une seule** commande
`codex exec`, avec sandbox `read-only`, sans demande d'approbation ni reprise de
conversation. La réponse est enregistrée dans `analysis.md`, pas injectée dans
le chat déjà ouvert. Sans cette option, aucun appel au modèle.

Le CLI doit être installé et authentifié. `--codex` peut fournir son chemin
absolu si sa découverte dans PATH échoue. Le lanceur n'installe rien et ne
collecte pas de clé API. Il ignore la configuration utilisateur Codex, mais
réutilise son mécanisme d'authentification ; aucun modèle particulier n'est imposé.
Les capacités et politiques de la version locale restent applicables.

## Utilisation

Depuis le worktree qui contient le script, exemple sans IA sur ses tests ciblés :

```powershell
python scripts/run_test_then_analyse.py --output-dir build-patch_raw_bench/jobs/controle-01 -- python -m unittest discover -s tests -p test_run_test_then_analyse.py
```

Pour autoriser une analyse réelle en fin de ce même type de test :

```powershell
python scripts/run_test_then_analyse.py --analyse --output-dir build-patch_raw_bench/jobs/controle-02 -- python -m unittest discover -s tests -p test_run_test_then_analyse.py
```

Remplacer ce dernier programme par la commande exacte de la prochaine campagne.
Ajouter `--result chemin/vers/statut-ou-resultat.json` **avant** `--` pour indiquer
le JSON qu'elle produira. Les chemins relatifs sont résolus dans `--workspace`
(par défaut le worktree du script). Résultat attendu et dossier de sortie doivent
rester dans ce workspace. Adapter l'interpréteur Python à l'installation locale.

Le dossier de sortie doit être **nouveau** à chaque lancement : sa réutilisation
est refusée, même après un arrêt. Cela évite un second appel involontaire ; aucune
relance automatique après échec réseau, quota ou délai dépassé. Ne pas mettre de
secrets dans les arguments ou les journaux de test.

## Fichiers et garde-fous

- `completion.json` : phase courante, code de retour, délai dépassé et durée.
- `test.stdout.log`, `test.stderr.log` : sorties du programme de test.
- `analysis-prompt.txt` : instructions et chemins fournis au modèle.
- Avec IA : `analysis.events.jsonl`, `analysis.stderr.log`, et `analysis.md`
  si la réponse finale a effectivement été produite.

Le succès du contrôleur n'est pas une validation photographique. Un test échoué
peut être expliqué par le modèle, sans masquer son code d'échec. Codes du lanceur :
0 = test réussi et analyse facultative finalisée ; 1 = test échoué ;
2 = délai du test dépassé ou erreur d'utilisation ; 3 = analyse non finalisée.
Des erreurs inattendues restent tracées et peuvent produire un autre code non nul.

Délais par défaut : 24 h pour le test, 15 min pour l'analyse ; options
`--test-timeout` et `--analysis-timeout` en secondes. Sur interruption ou timeout,
le lanceur arrête son enfant actif et tente de terminer ses descendants
(`/T` sous Windows, groupe de processus sous POSIX), sans rechercher les autres
agents par leur nom. Les commandes qui détachent leurs enfants ne sont pas prises
en charge. Une erreur de nettoyage est remontée, sans attente infinie implicite.

L'analyse reçoit une consigne de lecture des seuls journaux et preuves JSON,
sans images, réseau, nouvelle campagne, édition, Git, message externe ou veille.
Le script ne joint aucune photo. Avec `--analyse`, le contexte et les éléments
lus par Codex sont traités via son service : ce n'est pas une inférence locale.
La sandbox protège les commandes du modèle ; les consignes ne remplacent pas
une qualification de sécurité de toutes les intégrations d'un environnement.

**Aucune veille automatique dans ce lanceur.** Si un contrôleur de veille est
utilisé avec lui, attendre la fin du lanceur entier, donc de l'analyse aussi,
et non la seule fin du test. L'ancien watcher lié au PID 4008 n'est pas réutilisable
pour une nouvelle campagne.

## Qualification

Douze tests ciblés passent : succès, échec, délais test/analyse, sortie exclusive,
paramètres invalides, CLI factice invoqué une seule fois, échec de démarrage et
repli après timeout du nettoyage Windows. Un essai CLI du lanceur sans IA passe.
Aucun véritable appel modèle, quota ou accès au compte n'a été testé ; la première
utilisation réelle reste à qualifier. Windows testé, POSIX non qualifié.

Références : [mode non interactif Codex](https://learn.chatgpt.com/docs/non-interactive-mode)
et [options CLI](https://learn.chatgpt.com/docs/cli/reference).
