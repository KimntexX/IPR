# Analyseurs photographiques par groupe

Contrat aligné le 12 octobre 2026, branche `patch_validation`.
Outil canonique : [analyse_groupes.py](../../../tests/analyse_groupes.py).
Les dix entrées dédiées partagent la même CLI et les mêmes méthodes natives.
Elles ne génèrent, modifient ni suppriment d'image : les états IPR sont rendus
en mémoire par `iprRenderServer` ([ADR-0019](../../decisions/ADR-0019-analyses-ipr-en-memoire.md)).
Mesurer ne signifie pas valider.

## Commandes courantes

Depuis la racine, avec Python 3, NumPy et Pillow, après compilation de
`iprRenderServer` (Release dans `build-windows-msvc`, ou chemin donné par
`--render-server` ou `IPR_RENDER_SERVER_BIN`) :

```bash
python tests/analyse_groupes.py --list
python tests/analyse_couleur.py --help
python tests/analyse_couleur.py --base tests/image/base --strict --data-version 1
python tests/analyse_lumiere.py --base tests/image/base --feature shadows --data-version 2
python tests/analyse_groupes.py --group all --base tests/image/base --output-dir docs/validation/raw
```

Dix groupes : `raw`, `lumiere`, `courbes`, `couleur`, `melangeur`,
`grading`, `effets`, `bruit`, `nettete`, `optique`. Chaque entrée est
`tests/analyse_<groupe>.py`. L'IHM/persistance/export relève de la recette
transversale et de CTest, pas d'un analyseur pixel.

`--feature` et `--scene` restreignent les états. `--render-workers N` fixe le
nombre de serveurs parallèles (une scène par serveur) ; par défaut il suit la
mémoire libre : environ 2,5 Gio par serveur pour 32 Mpx, 2 Gio de réserve,
au plus 4. Une campagne n'écrit que son JSON. `--strict` retourne 2 si
des états sont diagnostics/exclus, après conservation du JSON. Une erreur
d'association retourne 1 sans publier de campagne. `--output` impose un
chemin pour un seul groupe, sans écrasement ; sinon le nom inclut groupe,
version et UTC réelle. `--data-version` et `--supersedes` conservent le lien
vers une campagne remplacée sans modifier son JSON.

## Associations et provenance

`--base` lit les TIFF Lightroom des dossiers partagés, sans campagnes datées.
Les états IPR sont ceux des sources à la racine de base, listés et rendus par
`iprRenderServer` avec les recettes du [générateur](generateur-images-tests.md) :
le serveur renvoie les codes 16 bits sRGB de l'export TIFF et sa déclaration
(réglages, empreintes source/exécutable, profils, référence). Les TIFF IPR
présents dans base servent à la revue visuelle et ne sont pas analysés.
Référence et variante doivent partager leur contexte de développement ; à la
clôture, chaque source doit avoir encore l'empreinte décodée. Le JSON indique
`ipr_pixels.kind = in_memory_render`, le serveur et son SHA-256 ; l'empreinte
des pixels IPR porte sur les échantillons uint16, pas sur un fichier.

Pour Lightroom, la valeur vient du XMP, jamais du numéro de suffixe. Les
supports manuels température/nuance sont 5200 K / 0 ; le neutre As Shot
5200/+5 est distinct et n'est pas une erreur. Pour les autres contrôles,
chercher un neutre du même moteur sous `base/neutre`, avec le reste des
métadonnées inchangé. Une référence absente reste exclue ; plusieurs candidates
différentes bloquent l'association. Les scènes LR simples sont identifiées par
le nom avant le premier tiret ; pour une autre convention, utiliser un mapping.

Les groupes dont le contrôle XMP n'est pas connu (courbes/grading/optique,
notamment) demandent un mapping explicite et restent diagnostics si l'audit
n'est pas implémenté. Les anciens scripts de curseurs et le mode de génération
à manifeste ont été remplacés : aucun chemin `--legacy` n'est disponible.
Les sondes spécialisées WB/RAW restent séparées car elles testent une autre
hypothèse. Git et les JSON historiques sont conservés.

Les TIFF sont revérifiés en dimensions natives, uint16 RGB(A) non compressé,
ICC sRGB, orientation 1, alpha opaque et empreintes. Aucun recalage ni
redimensionnement inter-moteurs n'est automatique. Les régions de bruit et
détail sont adjacentes, stride 1 ; l'échantillonnage global est déclaré.
Le nom commercial DCP ne vaut pas empreinte du profil. Les déclarations TIFF
ne sont pas des signatures d'authenticité : conserver cette limite.

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
Les neutres ambigus sont refusés. Ajouter `--mapping CHEMIN` à la commande d'analyse
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
Sources TIFF, RAW rendus et JSON sont revérifiés avant publication ; changement
détecté → arrêt sans publication de la campagne concernée.

- 0 : mesure publiée ; **pas** validation.
- 1 : erreur d'entrée, exécution ou publication.
- 2 avec `--strict` : mémoire diagnostique publiée avec état(s) non exploitable(s).

`decision` reste `not_evaluated` et `cleanup_eligible=false`. La décision
humaine et la clôture du [socle commun](../protocols/protocole-commun.md)
restent nécessaires avant toute suppression. Dans une exécution multi-groupes,
les campagnes déjà publiées avant une erreur restent conservées.

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
générateur actuel. Les nouveaux exports déclarés s'analysent directement avec
`--base tests/image/base` ; les mémoires historiques ne sont jamais réécrites.

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

## Rendu d'ouverture — diagnostic sur le corpus racine

`rawOpeningProbe` est une cible native manuelle, exclue de la compilation
par défaut et de CTest. Elle lit tous les RAW directement dans `base`,
sans sidecar, sans normalisation et sans optique. Les JSON de scènes sont
immuables ; les PNG dans un nouveau dossier de build sont des aperçus, pas
des exports de conformité. `rawOpeningProbeTests` utilise de petits tableaux
synthétiques, sans la banque privée.

```powershell
cmake --build build-windows-msvc --config Release --target rawOpeningProbe rawOpeningProbeTests
./build-windows-msvc/Release/rawOpeningProbe.exe . docs/validation/raw build-NOUVELLE_INSPECTION
python tests/audit_opening_corpus.py
python tests/summarize_raw_opening.py CHEMIN_AUDIT_CORPUS.json
python -m unittest discover -s tests -p test_raw_opening_analysis.py
ctest --test-dir build-windows-msvc -C Release -R '^rawOpeningProbeTests$' --output-on-failure
```

Utiliser l'environnement Qt/OpenCV/LibRaw du build. Le comparateur refuse
un RAW absent, dupliqué, d'empreinte différente ou un rejeu DCP divergent.
Des campagnes de scènes multiples ne doivent pas être mélangées ; l'outil
refuse les doublons au lieu de choisir silencieusement une génération.
Les comparaisons LR sont des distributions indépendantes ; aucune MAE ou
équivalence de WB n'est annoncée. Voir le
[rapport unique, §30](../analyses/analyse-base-colorimetrique.md#30-corpus-lr-complet-et-méthodes-de-développement--14-octobre-2026).

En présence de plusieurs références éligibles, ajouter une sélection explicite,
par exemple `--lr-reference 6V1A0327=6V1A0327.tif`. Le JSON conserve le choix
et les candidats avec leurs empreintes ; une sélection absente ou ambiguë
est refusée. Sans sélection, une ambiguïté reste une référence manquante.
La réutilisation des sondes vérifie les empreintes des RAW, du code natif
et des DCP. Si ces sources changent, produire une nouvelle campagne native.
Les synthèses restent versionnées, horodatées et immuables.

Essai DCP indépendant du générateur photographique :

```powershell
cmake --build build-windows-msvc --config Release --target rawOpeningTrials rawOpeningSplineTests
ctest --test-dir build-windows-msvc -C Release -R '^rawOpeningSplineTests$' --output-on-failure
python -m unittest discover -s tests -p test_raw_opening_trials.py
python tests/run_raw_opening_trials.py CHEMIN_SYNTHESE_OUVERTURE_COMPLETE.json
```

La cible de corpus est exclue de la compilation par défaut ; le test spline
synthétique est compilé normalement, sans corpus privé. Le lanceur crée des
copies DCP jetables et une spécification dans un nouveau dossier de build,
pas dans les profils utilisateur. Il vérifie les entrées, exécute les variantes
et conserve le résultat complet sous `docs/validation/raw/`. Il ne faut pas
modifier les sources ou références pendant l'exécution. Voir la recette
[RAW/profils](../protocols/protocole-raw-profils.md#essais-isolés-de-courbe-dcp-et-exposition-caméra).

Audit radiométrique de tous les RAW racine :

```powershell
cmake --build build-windows-msvc --config Release --target tonalBaseProbe
python tests/audit_raw_opening_levels.py --summary CHEMIN_SYNTHESE_OUVERTURE_COMPLETE.json
python -m unittest discover -s tests -p test_audit_raw_opening_levels.py
```

Le convertisseur Adobe installé produit des DNG depuis des copies RAW sans
sidecars, puis `tonalBaseProbe --scalars-only` mesure les scalaires LibRaw
avant/après traitement sans exporter de pixels. Les copies sont locales,
non distribuables, jamais utilisées comme substituts aux références TIFF LR.
`--reuse-artifacts DOSSIER` permet de relire un dossier diagnostique complet
après contrôle du corpus, des copies et des empreintes ; ce mode ne relance
ni conversion ni décodage et sa provenance est explicite dans le JSON.
Les noirs CFA non uniformes sont conservés ; aucun gain scalaire unique
n'est déclaré dans ce cas, seulement des bornes descriptives.

Décodages radiométriques contrôlés sur toutes les scènes auditées :

```powershell
cmake --build build-windows-msvc --config Release --target tonalBaseProbe rawOpeningSplineTests
python tests/run_raw_radiometric_trials.py --audit CHEMIN_AUDIT_NIVEAUX.json --summary CHEMIN_SYNTHESE_OUVERTURE.json
python -m unittest discover -s tests -p test_raw_radiometric_trials.py
```

Le JSON canonique conserve les huit variantes par RAW, paramètres auditables,
traces LibRaw et bilans par boîtier. Les variantes de blanc nécessitent un
nouveau décodage, pas une multiplication après rendu. Ne pas modifier les
entrées pendant la campagne. Témoin, blanc seul, exposition seule, combinaison
et versions préservant la teinte sont distincts ; aucun mode de production
n'est activé automatiquement par ce lanceur.

Les TIFF manuels suffixés `_RAW_Audit` se comparent avec le même outil :

```powershell
python tests/compare_raw_radiometric_output.py CHEMIN_ESSAIS_RADIOMETRIQUES.json --ipr-audit-exports tests/image/base/neutre
```

L'analyseur refuse les doublons par scène sans sélection explicite et les références LR dont
l'empreinte a changé. Il conserve les métadonnées d'export et distingue
un canal à la borne de trois canaux simultanément à la borne, sur la grille
de pas 8. Il ne suppose pas que le nom d'un TIFF atteste ses réglages.
`tests/inspect_saved_raw_state.py` lit en lecture seule les `.ipr` courants
v6 ; `tests/check_raw_audit_optics.py` peut rejouer les recettes où les deux
options optiques sont actives, via la sonde manuelle de production avec
argument LCP. Un état `.ipr` actuel ne date pas la recette d'un ancien export.

Pour retenir explicitement un nouvel export sans effacer son prédécesseur :

```powershell
python tests/compare_raw_radiometric_output.py CHEMIN_ESSAIS_RADIOMETRIQUES.json --ipr-audit-exports tests/image/base/neutre --ipr-reference 6V1A0054=6V1A0054-IPR_RAW_Audit-2.tiff
```

L'option est répétable par scène ; le JSON conserve les candidats, leurs
empreintes et la sélection. Aucun choix automatique du fichier le plus récent.

L'étude ciblée des étapes HL se lance après compilation des sondes :

```powershell
cmake --build build-windows-msvc --config Release --target rawNormalizationCorpusProbe editorRawDevelopmentTests rawOpeningSplineTests
python tests/analyse_opening_highlights.py CHEMIN_ANALYSE_EXPORTS_RAW_AUDIT.json
```

Elle mesure 0043, 0081, 9940, 0524 et les trois R7, puis reproduit l'activation/désactivation
de 0054 sur une copie RAW temporaire. Les `.ipr` sources restent intacts.
Le candidat de gamut préservant Y est uniquement diagnostique : aucun
changement du moteur. Les paramètres sont figés pour cette étude, pas un
outil de génération de TIFF. Le JSON horodaté conserve les empreintes,
mesures et limites ; les artefacts intermédiaires restent dans un dossier build.
La sonde rejoue les matrices/exposition, puis les tables Hue/Sat, la LookTable
et la courbe, par masquage des seuls identifiants de tags dans des copies DCP
temporaires. Le rejeu complet doit être identique à la sortie native à `1e-6`
près. Les bornes sont mesurées en RGB caméra, en ProPhoto reconstitué et en
sRGB linéaire ; ne pas interpréter des composantes ProPhoto comme une luminance
sRGB. Le seuil linéaire `65534/65535` n'est pas le seuil d'écrêtage du TIFF encodé.

Le nouveau test numérique `rawHighlightHeadroomTests` prépare la séparation
signal étendu/coordonnées de table et une épaule scalaire indépendante :

```powershell
cmake --build build-windows-msvc --config Release --target rawHighlightHeadroomTests
ctest --test-dir build-windows-msvc -C Release -R '^rawHighlightHeadroomTests$' --output-on-failure
```

Il ne lit aucune image, n'utilise pas les tables Adobe et ne produit pas
d'exports. Il est intégré à CTest et à la sélection ciblée pour ses deux
fichiers sources. Invariants et limites sont dans le
[protocole RAW/profils](../protocols/protocole-raw-profils.md#préparation-expérimentale-de-la-conservation-de-réserve).

La sonde `rawNormalizationCorpusProbe` raccorde désormais le modèle aux
tables réelles par copies DCP temporaires isolant les étapes. Les aperçus
PNG sRGB 16 bits au pas 8 restent sous le dossier build diagnostique ; ce
ne sont pas des exports de validation en pleine résolution. Le témoin
rejoué doit respecter une erreur maximale de `2e-5` RGB flottant.

Pour qualifier les changements de couleur par rapport au témoin IPR :

```powershell
python -m pip install opencv-python-headless
python tests/qualify_headroom_candidates.py CHEMIN_INVESTIGATION_OUVERTURE_HL.json
python -m unittest discover -s tests -p test_headroom_candidates.py
```

OpenCV Python est requis seulement pour la lecture native des PNG dans la
campagne manuelle ; les tests numériques de qualification n'en dépendent pas.
La qualification vérifie les empreintes et la profondeur 16 bits, conserve
Delta E00, changement Y, chroma et teinte par masque défini sur le témoin.
Elle publie un JSON immuable distinct, lié à la campagne de rendu. Pas de
MAE/Delta E pixel à pixel LR à partir de grilles non alignées. La comparaison
au témoin IPR est possible car les variantes partagent exactement la grille.

### Comparatif contrôlé des espaces de travail

```powershell
cmake --build build-windows-msvc --config Release --target rawNormalizationCorpusProbe
python -m unittest discover -s tests -p test_working_spaces.py
python tests/compare_working_spaces.py CHEMIN_ANALYSE_EXPORTS_RAW_AUDIT.json
```

Le lanceur vérifie d'abord les sept références Camera Neutral/As Shot et leurs
empreintes avant tout décodage. La sonde, en mode diagnostique
`--working-space-source`, conserve une grille flottante **après DCP, avant
gamut sRGB**, et un témoin PNG 16 bits de la sortie actuelle. RAW, DCP et
recettes utilisateur ne sont pas modifiés.

Trois espaces flottants sont comparés : sRGB étendu, ProPhoto linéaire D50
avec adaptation Bradford, Rec.2020 linéaire D65. Six états par espace : neutre,
exposition ±1 EV, gain de chroma 1,25 à Y D65 commun, puissance signée 1,2
par canal, puis bornage isolé au cube unité. Les deux derniers sont des tests
diagnostiques dépendants des primaires, pas des curseurs de production.

Le JSON `comparatif-espaces-travail-vNNN-HORODATAGE.json` conserve matrices,
paramètres, données par scène/état, différences au même opérateur sRGB,
provenance, vérifications et limites. Les aperçus sRGB 16 bits neutres,
puissance et bornage restent dans le dossier de build de la campagne ; ceux
écrits par OpenCV n'incorporent pas d'ICC et leurs valeurs sont explicitement
encodées sRGB. Ils ne sont pas des exports pleine résolution.

Les trois répétitions chronométrées concernent NumPy (conversion/opérateur/
retour), hors rendu de sortie, mesures perceptuelles et I/O. Ce ne sont pas
des mesures de latence IPR ni de RSS ; le volume des quatre tampons est une
comptabilité logique RGB de la grille. La preuve ne doit pas servir à annoncer
un espace « plus rapide » en production. ACEScg n'est pas exécuté par ce lanceur.

### Rejeu des neutres après port du master Rec.2020

```powershell
cmake --build build-windows-msvc --config Release --target rawNormalizationCorpusProbe --parallel 4
python tests/verify_rec2020_opening.py docs/validation/raw/comparatif-espaces-travail-v001-20261015T100527950069Z.json
```

Ce contrôle local attend les RAW/DCP et les PNG natifs du comparatif cité,
vérifiés par SHA-256 avant de décoder. Il rejoue le moteur **actuel**, pas un
ancien moteur : les PNG conservés servent de témoins historiques. Le master
est Rec.2020 ; les métriques/échantillons sRGB des sondes sont des dérivées
d'analyse explicitement converties. Le script conserve une nouvelle mémoire
`integration-rec2020-neutres-vNNN-HORODATAGE.json` et retourne un échec si les
garde-fous de neutre ne passent pas. Il ne supprime ni source, ni référence,
ni sidecar. Les aperçus au pas 8 ne valident pas les détails pleine résolution
et les décodages uniques ne constituent pas un benchmark.

### Exports manuels neutres Rec.2020 face à Lightroom

```powershell
python tests/analyse_neutres_rec2020.py
```

Ce diagnostic spécialisé du groupe RAW/profils utilise les RAW à la racine de
`tests/image/base`, les sidecars courants et les TIFF dans `base/neutre`.
Il sélectionne un seul export IPR contenant `RAW_Audit` par scène (casse
indifférente, suffixe de numérotation accepté sans ambiguïté). Pour LR, il
reprend le chemin explicite de la mémoire des essais radiométriques ; s'il
est absent, il cherche seulement `<scène>.tif` dans `neutre`. Le chemin et
l'empreinte effectivement utilisés sont conservés dans la nouvelle preuve.

Avant toute comparaison LR/IPR, tous les TIFF doivent passer le contrôle
16 bits sRGB, structure, orientation, alpha opaque, profil Camera Neutral/
As Shot LR et réglages neutres. Les recettes `.ipr` sont vérifiées, puis les
19 exports IPR sont confrontés au rejeu natif du moteur Rec.2020 avec calibration
effectivement appliquée et optique désactivée. Un écart supérieur à quatre
codes 16 bits interrompt la procédure ; la recette courante seule ne prouve
pas celle qui a servi à exporter le TIFF.

L'analyse mesure ensuite les distributions de Y, l'écrêtage et des résumés
colorimétriques Lab, sans MAE/Delta E pixel à pixel sur des géométries non
alignées. Les masques de tons sont définis indépendamment sur chaque image.
Elle publie un JSON immuable `analyse-neutres-rec2020-vNNN-HORODATAGE.json`,
contenant audits, recettes, empreintes, versions, mesures et limites. Au premier
échec, elle conserve le motif d'arrêt, retourne un code non nul et ne poursuit
pas les images suivantes. Aucune photographie ou recette n'est modifiée.

### Résidu R7 : recalage, gain et exposition de base

```powershell
python tests/investigate_r7_gain.py
python tests/investigate_r7_baseline.py
```

Ces outils spécialisés consomment la campagne neutre Rec.2020 auditée, avec
contrôle de ses empreintes avant mesure. Le premier recale les trois R7 et
trois témoins R6 par translation puis affine ECC sur log-Y. Il ajuste un gain
de luminance sur des tuiles spatiales non écrêtées, en linéaire, et conserve
les résidus par tonalité/canal ainsi que la validation croisée par scène.
Les métriques appariées portent sur des images filtrées et recalées, pas sur
les pixels natifs sans interpolation ni sur une charte physique.

Le second vérifie séparément les expositions de base 0,30/0,32/0,33/0,34/0,35 EV
avec `rawNormalizationCorpusProbe --working-space-area`. Les manifestes
d'essai sont isolés dans `build-colorimetrie-inventory` : aucun changement du
manifeste de production, des profils, des sidecars ou des photos. Le mode de
sonde calcule la sortie complète de production en 16 bits, la décode en
linéaire, puis en fait la moyenne de surface ; moyenner avant le DCP ne
testerait pas le même opérateur. Le témoin 0,35 EV doit reproduire le TIFF
manuel avant de poursuivre. Une seule transformation et un seul masque par
scène servent à toutes les variantes. La grille ponctuelle au pas 8 est
conservée pour l'audit historique ; elle n'est pas utilisée comme substitut
de la moyenne de surface dans l'étude courante du gain.

Chaque exécution publie un JSON immuable `investigation-gain-r7-vNNN-…` ou
`investigation-baseline-r7-vNNN-…`. Un minimum de MAE n'autorise pas à déployer
automatiquement le coefficient : contrôler couleurs, ombres, HL, stabilité
entre scènes, portée ISO/profil et validation visuelle. Les trois R7 à hauts
ISO ne représentent pas toute la gamme d'usage du boîtier.

### Diagnostic ciblé d'un export 0081 après restauration

```powershell
python tests/investigate_0081_export.py
```

Compare les exports 0081 courant `-IPR_RAW_Audit-2.tiff`, précédent et LR
dans `base/neutre`, ainsi que le sidecar courant. Rejoue le moteur actuel
avec et sans calibration, même DCP et optique désactivée. Conserve les
empreintes, distributions et comparaisons exactes sur la grille au pas 8
dans `investigation-export-0081-vNNN-HORODATAGE.json`. Le mode de sonde
`--working-space-uncalibrated` désactive aussi la recherche du manifeste par
variable d'environnement ; le champ d'application dans la preuve est réel.
Ce diagnostic n'applique aucun correctif, ne modifie ni photo ni recette,
et ne compare pas les pixels LR/IPR de géométries différentes par MAE.
