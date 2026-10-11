*[Français](README.md) | [English](README_en.md)*

# Mesures dérivées : CSV et courbes

**Règle depuis le 9 octobre :** les [JSON canoniques versionnés et horodatés](../data/README.md)
font autorité sur les données des nouvelles campagnes. Ce dossier conserve
les CSV, synthèses et graphiques dérivés. Les chaînes ci-dessous utilisent les
JSON sources ; leur présence ne garantit pas tous les champs du nouveau contrat.
Les synthèses et courbes doivent lire les JSON canoniques, directement ou via
leurs CSV exports traçables.

Actualisation documentaire du 8 octobre 2026. Les fichiers de ce dossier sont
reproductibles depuis les preuves de `../raw/` ; celles-ci ne sont jamais modifiées.

```sh
python3 scripts/export_documentation_metrics.py
python3 scripts/export_documentation_metrics.py --check
python3 -m unittest discover -s tests -p test_documentation_metrics.py
```

Le script utilise uniquement la bibliothèque standard Python. Les CSV sont en
UTF-8, séparateur virgule, décimales avec point ; importer explicitement ce format
dans un tableur français. Les grands entiers tels que `file_mtime_ns` doivent
rester du texte dans un tableur pour éviter sa limite de précision.
Le contrôle `--check` vérifie les empreintes de provenance, relit les valeurs
JSON pointées sans perte de précision des grands entiers et parse tous les SVG.

## Données et provenance

### Complément Lumières Camera Neutral — 9 octobre

Le sous-dossier [lumieres-camera-neutral-20261009/](lumieres-camera-neutral-20261009/)
contient la synthèse des 144 variantes et six courbes de réponse. Source :
[JSON audité](../raw/mesure-lumieres-camera-neutral-20261009.json),
[CSV par scène](../raw/mesure-lumieres-camera-neutral-20261009.csv).
Génération indépendante de l'export documentaire historique :

```bash
python3 scripts/summarize_lighting_validation.py docs/validation/raw/mesure-lumieres-camera-neutral-20261009.json --output docs/validation/metrics/lumieres-camera-neutral-20261009
```

Chaque scène a le même poids. Les ratios de synthèse sont des médianes de
ratios par scène ; les deltas et taux sont des moyennes. Les courbes montrent
trois états et non une progressivité mesurée, ni des temps CPU. Les quantiles
de zones sont propres au neutre de chaque moteur ; pas de correspondance
spatiale garantie. Les états avec avertissements sont exclus de la synthèse.
La nouvelle campagne n'a aucun avertissement XMP mais les réglages IPR
restent inférés des noms, sans manifeste. Voir le rapport Lumières pour les limites.

- `sources.csv` : fichiers d'origine, famille, taille et SHA-256.
- `rapports-tableaux.csv` : cellules numériques des tableaux publiés dans les
  rapports (y compris EV et noirs), avec ligne, section, en-têtes et cellule
  d'origine. Ce niveau de preuve est distinct des échantillons bruts.
- `base-raw-profils.csv`, `tonalite.csv`, `balance-blancs.csv`, `couleur-globale.csv`,
  `melangeur.csv`, `bruit.csv`, `detail.csv` : valeurs numériques, emplacement
  JSON Pointer ou ligne/token textuel, contexte et empreinte source.
- `temps-ouverture.csv` : les deux états du benchmark Windows du 1er octobre.
- `tonalite-revisions.csv` : réponses de luminance, moteur, région, révision et corpus.
- `reglages-reponses.csv` : mesures nommées des campagnes couleur/détail/WB.
- `melangeur-progressivite.csv` : huit bandes, trois composantes, LR/IPR et quatre
  valeurs intermédiaires sur les scènes disponibles. Chaque graphique retient
  une seule scène par bande, celle avec le plus de pixels dans le cœur de bande ;
  aucun mélange de sujets ou de neutres. Le CSV conserve toutes les scènes.
- `bruit-progressivite.csv` : ratios de résidu et de gradient des états valides du
  corpus v2 étendu. Les courbes illustrent 9950, pas une moyenne de sujets différents.

L'export numérique complet comprend aussi les paramètres et métadonnées :
**un nombre dans ce CSV n'est pas automatiquement un score de qualité**.
Les unités restent celles du schéma/source ; aucune unité inconnue n'est inventée.
Les lignes textuelles conservent leur contexte littéral ; leurs tokens peuvent
inclure seuils, tailles et identifiants. Utiliser les CSV spécialisés pour tracer.
Les chemins physiques de certaines anciennes mesures peuvent être archivés ;
les données de preuve demeurent disponibles dans leurs JSON.

## Courbes disponibles et lecture

Les graphiques SVG se trouvent dans [curves/](curves/).

| Partie | Graphiques | Ce qu'ils montrent / limite |
| --- | --- | --- |
| Ouverture RAW / profils / copie / totaux | [Temps d'ouverture](curves/temps-ouverture.svg) | Avant/après historique local ; pas de mesure du build actuel, pas de variance publiée. |
| Ombres, blancs, hautes lumières, contraste | `tonalite-*.svg` | Réponse IPR aux extrêmes par révision ; corpus explicite conservé, pas vitesse CPU ni score « plus bas = meilleur ». |
| Température, nuance, vibrance, saturation | `reponse-*.svg` | Réponse de la dernière campagne disponible, deux états documentés ; unités et conventions dans la source. |
| Texture, clarté, voile | `reponse-*.svg` | Même principe ; réponse du réglage, pas qualité absolue des détails. |
| Mélangeur huit bandes | `melangeur-<bande>-<composante>.svg` | Progressivité −50/−25/+25/+50, mesurée relativement au neutre propre à chaque moteur. |
| Bruit luminance / chrominance | `bruit-*.svg` | Résidu Haar/MAD sur 9950 selon le curseur ; moins de résidu peut aussi signifier perte de texture. |
| Base DCP | [Comparaison des profils](curves/base-profils.svg) | ΔE00 moyen des derniers exports par scène, pas évolution temporelle ni fidélité physique. |
| EV, noirs | CSV et rapports de campagne | Pas de série comparable extraite automatiquement des journaux disponibles pour un graphe dédié. |
| Courbes, grading, netteté, grain/flou | Recettes à faire | Pas de mesures photographiques suffisantes pour une courbe d'évolution. |
| Vignettes, cache, rendu, export, lot | Benchmarks à constituer | Aucun temps inventé ; préparer les répétitions à froid/chaud et les pics mémoire. |
| Masques, géométrie éditable, IA | Non commencés | Pas de graphique de performance. |

Une donnée absente reste une lacune : aucun point zéro ni résultat à l'état
neutre n'est ajouté sans mesure. Les révisions ne sont pas assimilées à des
dates et les corpus distincts ne sont pas moyennés. Les mesures historiques
peuvent correspondre à un autre pipeline ; consulter leur rapport de campagne.

## Prochaine campagne de vitesse

`benchmark-template.csv` prépare les colonnes et dix étapes à mesurer ; les
valeurs sont volontairement vides. Ce fichier n'est pas une campagne exécutée.

Sur une machine et une version de dépendances identifiées : mesurer séparément
ouverture/recherche de profils/décodage/LCP-DCP/proxy/rendu/vignettes/export,
avec RAW identifié par SHA-256, profil, paramètres, résolution et mode cache.
Conserver les mesures individuelles, effectuer au moins plusieurs répétitions,
puis publier médiane, dispersion et pic RSS. Ne pas juxtaposer des temps de
machines différentes comme s'ils prouvaient un gain de code.
