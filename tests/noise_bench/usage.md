# Banc numérique partagé Bruit

Le banc fournit des fonctions NumPy, sans pilote de campagne ni débruiteur.
Depuis la racine du worktree :

```powershell
python -m unittest discover -s tests -p 'test_noise_bench*.py' -v
```

Les importations supposent `tests` dans `sys.path`, comme les outils d'analyse
existants. Aucun import vers un autre worktree n'est nécessaire.

## Contrats et ordre d'emploi

1. **Références et unités.** `captures.load_field` réutilise le lecteur
   `raw_bench.capture.load_capture` (contrôle d'empreinte et des descripteurs).
   Il reçoit le chemin de capture, l'étage, l'origine et l'unité attendue.
   Les outils actuels `tests/raw_bench/unpack_capture.cpp` et
   `development_capture.cpp` produisent ce contrat ; aucune lecture RAW Python
   n'est ajoutée. Les tableaux du banc sont CHW ; les rafales NCHW.
   `domain='cfa'` désigne quatre plans Bayer **positionnels** 00,01,10,11.
   Les couleurs dépendent du motif et de l'origine, conservés par l'adaptateur.
   Les dimensions impaires sont refusées, sans rognage silencieux.
   `domain='rendered'` désigne les canaux rendus explicitement fournis ; pour
   les captures RGB, l'adaptateur transpose HWC en CHW.
   Le pas spatial est de deux pixels capteur pour le CFA empaqueté.
   Ne pas mélanger unités normalisées, DN, RGB caméra, Rec.2020 ou sRGB.
   Les sorties de `analysis.render` restent des images rendues ; leurs valeurs
   sRGB ne deviennent pas du bruit capteur par simple changement de forme.

   `units.convert_noise_law` transforme une loi variance/signal avec origine du
   noir et diviseur explicites. Il ne calibre pas le capteur.
   `units.coverage` reçoit les ISO et bornes DN figés avant mesure ; il rend
   les cases vides, mesures hors couverture et nombres de captures distinctes.
   Une répétition de ROI ne devient pas une prise indépendante.

   `reference.crop_transform` compose les transformations locales sans
   rééchantillonner. Il ne recherche pas le mouvement. Pour un CFA, conserver
   la phase et suivre les contrôles des outils de préparation actuels ; une
   matrice affine valide ne suffit pas à autoriser une interpolation de CFA.
   `reference.audit_reference` décrit des prises déjà mises en correspondance :
   moyenne de chaque prise (illumination), dispersion temporelle et différence
   des demi-rafales. Il ne produit pas de référence ni de verdict d'acceptation.
   `reference.paired_risk` conserve les risques négatifs. Sa formule n'est
   sans biais que si les deux références correspondent à la même scène, ont
   des erreurs indépendantes de moyenne nulle, et si leurs erreurs n'ont pas
   servi à construire la sortie évaluée. La composante fixe commune échappe
   à une différence de paire ; une moyenne n'est pas une vérité parfaite.

2. **Noirs.** `blacks.black_repeatability` reçoit deux ensembles de prises
   bouchon fermé en DN, leurs identités source disjointes, un ISO commun et
   les quatre noirs explicites. Il sépare biais de piédestal, dispersion des
   moyennes de prises et variance temporelle après retrait du piédestal de
   chaque prise. Cette dernière exclut volontairement sa dérive globale.
   L'appelant doit vérifier boîtier, mode de capture, exposition, température
   et ISO de chaque prise avant regroupement. Ni une scène sombre ni deux
   crops d'une même prise ne constituent deux acquisitions de noir.

3. **Résidus.** `residuals.describe_residual` reçoit directement l'erreur
   **signée**, par exemple `estimate-reference`, avec domaine, unité, masque
   et décalages spatiaux explicites. Il rend moyenne, covariance échantillonnale,
   quantiles signés, fractions négatives/nulles, moments et corrélations par
   plan. Pour une différence de deux prises, la division par racine de deux
   reste une décision explicite, valable pour des variances égales et des
   erreurs indépendantes. La variance d'une référence imparfaite n'est pas
   soustraite automatiquement. Les moments standardisés des plans constants
   et les corrélations sans support/variance sont `None`, sérialisables en JSON.

4. **Zones.** `regions.region_support` vérifie le rectangle xywh et son halo
   explicite dans les coordonnées du tableau fourni. `describe_region` exige
   le masque valide sur tout ce contexte, puis décrit moyenne, étendue et
   énergie de gradients du cœur. Les pixels masqués ne sont pas des zéros.
   Un contexte troué est déclaré inéligible. Un cœur constant reste admissible
   pour une mesure de bruit ; un gradient contient aussi du bruit. Aucun score
   ne certifie une séparation texture/bruit ni une préservation de détails.

Les fonctions n'écrivent aucun fichier. Le pilote appelant conserve dans ses
JSON canoniques les unités, sources, identités des prises, réglages et résultats.
Aucune nouvelle campagne réelle ni verdict de qualification n'est livré ici.

## Réemploi et limites d'intégration

Les outils de `patch_noise_neural/tests/neural/` restent la référence pour
la préparation et l'évaluation : `real_noise_evaluation.py`,
`qualify_same_iso_burst.py`, `photo_pair_math.py`, `dark_bias_probe.py`,
`physical_reference_stability.py`, `sensor_noise_math.py` et
`residual_spectrum.py`. Le banc ne recopie ni recalage, ni référence
leave-one-out, ni comparateur BM3D, ni sonde CUDA, ni ajustement de loi.
Il ne réimplémente pas les bandes spectrales déjà disponibles.
Les dépendances de leurs pilotes ne sont pas celles du banc partagé.

Les fonctions ajoutées peuvent être appelées sur leurs tableaux préparés,
après harmonisation explicite de l'ordre des plans et des unités. Leurs
pilotes ne sont pas modifiés depuis cette branche ; ce raccordement reste à
faire dans la branche fille au moment d'une qualification définie.

Pour les mesures CFA d'entrée en DN, le contrat de capture courant suffit.
Pour qualifier une sortie du réseau au point exact de production, il manque
dans ce banc une capture **signée avant/après l'étage CFA**, sélectionnable sur
des rectangles explicites avec contexte complet : motif/origine, unités,
noir appliqué et son origine, diviseur, gains, écrêtages, réglages/modèle et
identités des sources. Ne pas déduire ces valeurs d'anciennes constantes de
campagne. Aucune interface moteur n'est modifiée ici. Aucun halo du U-Net
n'est inventé ; le pilote doit le déclarer d'après son contrat courant.
Mouvement, illumination et indépendance des acquisitions doivent encore être
qualifiés sur les données réelles ; ces diagnostics n'en apportent pas la preuve.

## Sources du portage

Fonctions extraites/adaptées des sources `wip/tests/` de l'archive du
12 novembre 2026 (archive inchangée) :

| Fichier courant | Points d'appui archivés |
| --- | --- |
| `units.py` | `audit_noise_raw_domain.py`, `audit_noise_raw_coverage.py` |
| `reference.py` | `study_noise_burst_reference.py`, `audit_noise_burst_reference.py` |
| `blacks.py` | `audit_noise_repeated_blacks.py`, `study_noise_black_distribution_repeatability.py` |
| `residuals.py` | `study_noise_residual_distributions.py`, `study_noise_profile_real_residuals.py` |
| `regions.py` | `study_noise_roi_contract.py`, `study_noise_roi_content.py` |

Les pilotes archivés et leurs dépendances transitives ne sont pas repris.
Métriques de détails et audit de filiation restent différés jusqu'à un besoin
de qualification précis.
