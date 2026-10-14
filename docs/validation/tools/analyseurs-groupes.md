# Analyseurs photographiques par groupe

Contrat aligné le 12 octobre 2026, branche `patch_validation`.
Outil canonique : [analyse_groupes.py](../../../tests/analyse_groupes.py).
Les dix entrées dédiées partagent la même CLI et les mêmes méthodes natives.
Elles ne génèrent, modifient ni suppriment d'image. Mesurer ne signifie pas valider.

## Commandes courantes

Depuis la racine, avec Python 3, NumPy et Pillow :

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

`--feature` et `--scene` restreignent les états. `--strict` retourne 2 si
des états sont diagnostics/exclus, après conservation du JSON. Une erreur
d'association retourne 1 sans publier de campagne. `--output` impose un
chemin pour un seul groupe, sans écrasement ; sinon le nom inclut groupe,
version et UTC réelle. `--data-version` et `--supersedes` conservent le lien
vers une campagne remplacée sans modifier son JSON.

## Associations et provenance

`--base` lit les dossiers du [générateur](generateur-images-tests.md),
sans campagnes datées. Pour IPR, les réglages, empreintes source/exécutable,
référence et développement proviennent du JSON embarqué dans le tag TIFF 270.
Un TIFF IPR non déclaré exige une régénération ou un mapping explicite ;
le nom seul ne prouve pas son état. Les références doivent rester sous base,
appartenir à la même scène et conserver leur contexte de développement.

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
Sources TIFF et JSON sont revérifiées avant publication ; changement détecté
→ arrêt sans publication de la campagne concernée.

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
[rapport unique, §29](../analyses/analyse-base-colorimetrique.md#29-reprise-exhaustive-du-corpus-douverture--14-octobre-2026).
