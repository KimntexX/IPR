# Analyseurs photographiques par groupe

Version 1 — 10 octobre 2026. Outil canonique :
[`tests/analyse_groupes.py`](../../../tests/analyse_groupes.py).
Il mesure les fichiers explicitement associés, sans générer, modifier ni
supprimer d'image, sidecar, RAW ou profil. Il ne délivre pas une validation
visuelle automatique.

## Transition du générateur du 11 octobre 2026

Le [générateur courant](generateur-images-tests.md) écrit directement dans les
dossiers partagés, sans manifeste ni campagne datée. Les commandes à manifeste
ci-dessous décrivent les entrées d'analyse archivées, pas sa nouvelle sortie.
L'entrée directe des nouveaux noms explicites doit encore être adaptée ; ne pas
annoncer que ces TIFF sont automatiquement associés par les analyseurs actuels.
Leurs JSON de mesures versionnés et horodatés restent la mémoire canonique.

## Structure alignée sur les protocoles

| Protocole / groupe CLI | Entrée dédiée | Méthode principale |
| --- | --- | --- |
| RAW/profils — `raw` | `tests/analyse_raw.py` | Y, RGB, chroma, clipping ; pas de certification de charte |
| Lumières — `lumiere` | `tests/analyse_lumiere.py` | Zones tonales fixes, quantiles, loi EV et clips |
| Courbes — `courbes` | `tests/analyse_courbes.py` | Réponse dans dix classes de Y, points déclarés |
| Couleur — `couleur` | `tests/analyse_couleur.py` | Axes chaud/froid, vert/magenta, chroma faible/forte, gris |
| Mélangeur — `melangeur` | `tests/analyse_melangeur.py` | H/S/L dans huit masques diagnostiques, faibles chromas |
| Grading — `grading` | `tests/analyse_grading.py` | Réponses par zones tonales, teinte/chroma |
| Effets — `effets` | `tests/analyse_effets.py` | Contraste interpercentile et détail par ROI natives |
| Bruit — `bruit` | `tests/analyse_bruit.py` | Haar/MAD Y et chroma, ratios par ROI adjacentes |
| Netteté — `nettete` | `tests/analyse_nettete.py` | Gradients, résidus, bruit et détail par ROI |
| Optique — `optique` | `tests/analyse_optique.py` | Diagnostic photométrique ; lignes/franges/recalage restent une recette spécialisée |

L'IHM/persistance/export relève des tests et de la recette manuelle, pas d'un
analyseur de pixels. Géométrie changée dans une paire optique : exclusion,
pas redimensionnement automatique ni MAE trompeuse.

Le package `tests/analysis/` sépare catalogue, lecture native, entrées,
audit, métriques communes, méthodes de groupe, campagne et CLI. Les entrées
dédiées partagent exactement le même schéma JSON et les mêmes options ; elles
fixent seulement le groupe. L'alias `color-mixer` désigne `melangeur`.

## Dépendances et premiers usages

Python 3 avec NumPy/Pillow. Depuis la racine du projet, utiliser WSL si Python
Windows n'est pas installé ; les chemins Windows des manifestes sont convertis
vers `/mnt/<lecteur>/...` sous WSL. Aucun chemin personnel n'est codé dans
l'analyseur.

```bash
python3 tests/analyse_groupes.py --list
python3 tests/analyse_couleur.py --help
python3 -m unittest discover -s tests -p test_analyse_groupes.py

# Manifeste archivé contenant les arguments --output de la génération.
python3 tests/analyse_couleur.py \
  --manifest docs/validation/raw/generation-photographique-20261010T120912Z/couleur.json \
  --feature saturation --scene 6V1A0327 \
  --output-dir docs/validation/raw --data-version 1 --strict
```

Sans `--feature`/`--scene`, traiter les états du groupe présents dans les
entrées. Sans `--output`, le nom est généré avec groupe/version/horodatage UTC
réel (microsecondes). `--output CHEMIN` impose un nom pour **un seul groupe**,
sans écrasement. `--data-version 2 --supersedes ID-ANCIENNE-CAMPAGNE` référence
une correction d'un seul groupe ; l'ancienne mémoire reste immuable.

Plusieurs manifestes : répéter `--manifest`. `--group all` écrit un JSON par
groupe représenté, pas un mélange de toutes les métriques dans un fichier.
`--image-root NOUVELLE-RACINE` relocalise les exports d'un seul manifeste ;
sinon ses arguments `--output` sont utilisés, ou son dossier si cet argument
manque. Un fichier supprimé reste une exclusion : aucun export n'est recréé.

Le manifeste doit être `complete`, TIFF 16 bits, avec le nombre attendu
d'exports. Les associations sont `feature/value/file/source`, pas le suffixe.
Les empreintes TIFF sont comparées aux empreintes déclarées du générateur.
Les noms DCP/LCP restent des noms : les empreintes des profils absentes du
manifeste ne sont pas inventées.

## Ajouter Lightroom ou une association manuelle

Créer un JSON de mapping versionné, par exemple :

```json
{
  "schema_version": 1,
  "image_root": "../../../tests/image/base",
  "records": [
    {"scene": "6V1A0327", "engine": "LR", "feature": "neutral", "value": 0,
     "file": "neutre/6V1A0327-LR-CameraNeutral-AsShot.tif"},
    {"scene": "6V1A0327", "engine": "LR", "feature": "saturation", "value": 50,
     "file": "saturation/6V1A0327-LR-S050.tif"}
  ]
}
```

Les noms sont **des exemples**, pas une découverte automatique de fichiers.
`image_root` est relatif au JSON de mapping ; vérifier ce chemin selon son
emplacement. Une référence explicite peut être donnée dans chaque variante
avec `reference`; sinon le neutre unique du même `(scene,engine)` est associé.
Les neutres ambigus sont refusés. Ajouter `--mapping CHEMIN` au manifeste IPR
permet de mesurer chaque moteur par rapport à sa propre référence.

Pour la nuance LR, `"value_mode":"delta_from_reference"` exprime un
déplacement depuis sa nuance As Shot ; par défaut `value` est absolue.
Le XMP TIFF est audité, pas seulement la déclaration de l'opérateur. Un état
IPR manuel exige ses `settings`, `reference_settings` complets et ses
`expected_sha256`/`reference_sha256` pour être suffisamment tracé. Les champs
manquants donnent un diagnostic, jamais une conformité supposée.

`rois_declared` peut fixer des régions natives par état :

```json
[{"box": [100, 200, 256, 256], "kind": "flat"},
 {"box": [800, 900, 256, 256], "kind": "detail"}]
```

Ordre : haut, gauche, hauteur, largeur. Les boxes sont contrôlées et doivent
rester identiques dans une série. Pour une ancre conditionnelle non neutre
(grading/quantité de netteté), déclarer `reference_role:"support"` et tous
ses réglages ; le résultat ne la présente pas comme un zéro global.

## Lecture et calculs

- TIFF simple page non compressé, RGB/RGBA entier 16 bits, bandes contiguës,
  orientation 1, ICC identifié sRGB ; alpha entièrement opaque. Compression,
  absence d'ICC, autres espaces et mises en page non supportées sont exclus,
  sans conversion silencieuse. Pillow ne sert qu'aux métadonnées : les codes
  sont lus par memmap en endian natif, jamais via `convert("RGB")` 8 bits.
- Décodage analytique sRGB, Y Rec.709. Global : pas spatial `--step 8` par
  défaut, pixels réels sans interpolation ; `--step 1` est exhaustif mais
  plus coûteux en mémoire. Les comptes/dénominateurs sont conservés.
- Effets/Bruit/Netteté : ROI toujours adjacentes à pas 1, jusqu'à 256×256
  par défaut. Sans annotations, grille 4×4 de candidats, classement par
  gradient du neutre, jusqu'à six faibles gradients et trois détails sans
  duplication. Candidats à confirmer visuellement, pas segmentation sémantique.
- Haar : `HH=(p00-p01-p10+p11)/2`, sigma=MAD(HH)/0,6744897501960817.
  C'est un proxy du bruit de sortie corrélé, pas une mesure du capteur.
- Détail : gradient RMS, résidu au binomial 3×3 et résidu à quatre passages
  binomiaux 5×5. Cette méthode versionnée est **nouvelle** : ne pas comparer
  directement ses chiffres à ceux d'un filtre historique différent.
- Progressions : états observés seulement, pas d'ancre zéro inventée. Une
  moyenne monotone n'est ni preuve pixel-par-pixel ni validation esthétique.
  Teintes : différences et moyenne circulaires, exclusion des chromas nulles.

Les métriques inter-moteurs sont des réponses internes à chaque neutre, pas
une MAE LR/IPR ni ΔE absolu. Les déformations LCP demandent un recalage dédié.
Charte/ΔE00, profils de bord/halos annotés, continuité 359°/0°, optique et
couplages combinés restent des recettes complémentaires : les mesures
communes ne prétendent pas les remplacer.

## Audit, sorties et codes retour

Chaque état est `measured`, `diagnostic` (réglages/provenance insuffisants) ou
`excluded` (fichier manquant, incompatible, mauvais hash/géométrie...). Le JSON
contient versions, heure réelle, hashes du code/des entrées, métadonnées,
réglages déclarés, contrôles, ROI, valeurs sans arrondi, unités, effectifs,
exclusions et progressions. Un dénominateur nul donne `null`, jamais NaN/Inf.
Sources TIFF et JSON sont revérifiées avant publication ; changement détecté
→ arrêt sans publication de la campagne concernée.

- 0 : mesure publiée ; **pas** validation.
- 1 : erreur d'entrée, exécution ou publication.
- 2 avec `--strict` : mémoire diagnostique publiée avec état(s) non exploitable(s).

`decision` reste `not_evaluated` et `cleanup_eligible=false`. La décision
humaine et la clôture du [socle commun](../protocols/protocole-commun.md)
restent nécessaires avant toute suppression. Dans une exécution multi-groupes,
les campagnes déjà publiées avant une erreur restent conservées.

## Compatibilité historique

Les anciens fichiers Température/Nuance/Vibrance/Saturation/Texture/Clarté/
Voile redirigent par défaut vers l'analyseur du groupe. `--legacy` exécute
explicitement le vieux diagnostic texte avec sa quantification historique,
sans l'assimiler à un JSON canonique. Leurs fonctions Python importables
restent disponibles pour les sondes existantes.

`analyse_color_mixer.py`, `analyse_noise.py`, `analyse_lumieres.py` et les
sondes spécialisées RAW conservent leur API historique pour reproduire les
campagnes et tests existants. Pour une **nouvelle** campagne, employer les
entrées de groupe ci-dessus. Aucun ancien JSON n'est réécrit ni migré en
inventant des métadonnées.

## Recette spécialisée de comparaison WB

`tests/compare_temperature_tint.py` utilise le même analyseur de groupe
Couleur sur les états IPR d'une mémoire JSON immuable et les valeurs XMP LR.
Cette réutilisation explicite de preuves n'est pas une nouvelle génération :
les pixels et supports IPR sont revérifiés contre leurs empreintes originales.
L'ancien argument `--generation` n'est plus accepté par cette recette.
Elle ajoute les paires et une synthèse des réponses, sans recalage ni MAE
inter-moteurs.
`--feature temperature` ou `--feature tint` permet deux sous-campagnes
horodatées, consignées dans le rapport Couleur unique. Les agrégats stricts
retiennent seulement les deux états `measured` et une association LR unique.
Les diagnostics et doublons restent visibles, mais n'entrent pas dans leurs
médianes. Un support manuel manquant reste dans l'inventaire exclu, sans
inventer une valeur zéro.
Les TIFF aux réglages secondaires non nuls restent diagnostics, pas validés.

Les vrais neutres communs sont dans **`tests/image/base/neutre`**. Leur audit
est distinct de la réponse des curseurs contre un support manuel :

```bash
python tests/compare_temperature_tint.py --audit-neutrals \
  --output-dir docs/validation/raw
```

L'audit lit le nom du RAW dans le XMP Lightroom, conserve tous les candidats
IPR et choisit explicitement le candidat nommé `-IPR-CameraNeutral-AsShot`.
Ce nom seul ne prouve pas les réglages de production. Le candidat LR nommé
`-LR-CameraNeutral-AsShot` est préféré à une copie `_optique`, mais ses
réglages sont vérifiés dans le XMP, pas déduits du nom. En l'absence de nom
explicite, seule une référence LR unique est admissible.

**5200 K / nuance +5 en As Shot n'est pas une erreur de saisie** : conserver
ces métadonnées caméra/LR, sans forcer le neutre à nuance 0. Un export
manuellement réglé à 5200/+5 peut indiquer `Custom`; ne pas le présenter comme
un export As Shot confirmé. Les écarts d'optique et les points manquants sont
consignés. Ni suppression d'image ni validation automatique.

Pour réévaluer des pixels IPR historiques encore présents contre de nouveaux
TIFF LR, passer leurs mémoires (une par sous-campagne, sans les dupliquer) :

```bash
python tests/compare_temperature_tint.py \
  --ipr-memory CHEMIN_MEMOIRE_TEMPERATURE.json \
  --ipr-memory CHEMIN_MEMOIRE_NUANCE.json \
  --output-dir docs/validation/raw
```

Les supports de cette comparaison sont ceux de la campagne originale,
déclarés dans le JSON; ils ne remplacent pas les neutres As Shot. Le support
5200/+5 historique ne doit pas être confondu avec le support 5200/0 du
générateur actuel. L'entrée directe des nouveaux noms IPR sans manifeste
reste à intégrer à l'analyseur de groupe.

La recette actuelle est décrite dans le
[guide du générateur](generateur-images-tests.md#ancres-fixes-pour-température-et-nuance).
`tests/audit_wb_profiles.py` fournit en complément un inventaire en lecture
seule des matrices/tables DCP et de leurs hashes : il ne rend pas d'image,
ne modifie aucun profil et ne démontre pas les algorithmes privés de LR.

`tests/investigate_wb_adaptation.py` est une **sonde diagnostique d'ablation**,
pas un générateur ni un remplacement du rendu de production :

```bash
python tests/investigate_wb_adaptation.py \
  --audit-memory CHEMIN_AUDIT_NEUTRES.json \
  --temperature-memory CHEMIN_MEMOIRE_TEMPERATURE.json \
  --output-dir docs/validation/raw
```

Elle utilise les candidats CameraNeutral/AsShot explicitement sélectionnés
dans l'audit, vérifie leurs hashes et projette trois modèles sur les pixels
natifs échantillonnés : formule actuelle, suppression de sa diagonale froide,
Bradford complet sans cette diagonale. Les trois modèles partent des mêmes
pixels ; aucune correction supplémentaire de nuance n'est appliquée au
neutre As Shot. La CCT est reprise du décodage RAW antérieur et sa provenance
est indiquée, pas reconstruite depuis un nom de fichier.

Les neutres TIFF sont déjà encodés et rendus par le profil : cette sonde ne
reconstruit ni le RGB caméra pré-DCP ni les valeurs hors gamut perdues.
Une projection ne certifie donc pas le rendu de l'IHM ou un correctif avant
DCP. Ses résultats restent `diagnostic`, avec JSON immuable et unités ;
aucune production modifiée, image générée/supprimée ou coefficient ajusté.
