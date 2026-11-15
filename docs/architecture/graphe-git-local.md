# Réseau Git vertical local

Le script `scripts/git_network.py` produit une page HTML autonome et un SVG,
sans Graphviz, bibliothèque Python supplémentaire ni service externe.

Depuis le worktree contenant le script :

```powershell
python scripts/git_network.py --open
```

Le résultat est écrit dans `build-git-network/network.html` et
`build-git-network/network.svg`. Ces deux fichiers sont régénérables ; aucune
modification des branches, aucun commit et aucun fetch ne sont effectués.
Si `python` n’est pas disponible sous Windows, utiliser `py -3`.

Par défaut, le graphe conserve les tags, les fusions, les bifurcations, les premiers commits et
les extrémités des branches locales et `origin`. Les lignes reliant deux nœuds
suivent les liens de parenté réels, avec regroupement des commits masqués.
La vue est topologique : ancien en haut, récent en bas, **sans échelle de temps**.
Les pistes sont attribuées aux chemins de premier parent, avec priorité à
`main`, puis `dev`. Git ne stocke pas le nom d’origine d’une branche pour chaque
commit : cette attribution est une présentation, pas une preuve d’appartenance
historique. Les pistes « other history » représentent les chemins qui ne sont
plus directement portés par une branche nommée.

Survoler un nœud donne le SHA complet, la date, le message et les références.
Le curseur de zoom réduit ou agrandit le dessin ; les barres de défilement
permettent d’explorer les branches. Les couleurs ne constituent pas un état CI.
Les noms `origin/...` distinguent explicitement les références distantes de
leurs équivalents locaux, y compris lorsqu’elles divergent.

Pour afficher tous les commits :

```powershell
python scripts/git_network.py --full --output build-git-network-full --open
```

Le graphe est un instantané : relancer le script après de nouveaux commits.
Pour actualiser les références distantes, effectuer séparément un `git fetch
origin` lorsque souhaité. Les changements locaux non commités, les stashs et
les résultats de pipelines ne sont pas représentés. Le graphe n’est pas publié
sur GitHub par ce script.
