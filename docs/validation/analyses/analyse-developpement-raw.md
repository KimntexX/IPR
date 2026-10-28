# Séparation du développement RAW — rapport évolutif

## Bilan actuel — 28 octobre 2026

Socle X1/X2 préparé sur `patch_raw_bench` depuis `dev` (`6e614ca`). Le
comparateur et les captures après `unpack()` et via l'observateur C2 existent ;
huit tests Python passent via CTest. Le jalon C2 est intégré localement.
Le banc ne modifie pas le moteur ; aucune parité du pipeline complet,
performance ou amélioration de bruit déclarée.

Dernière campagne §4 :30 RAW,450 comparaisons de zones sur cinq frontières
communes C2, écarts strictement nuls. Les deux noirs calibrés rejoignent
également les anciens buffers de bruit avant RCD/après médiane (§5).
La représentation flottante finale avant DCP reste à qualifier avec C3.

Le plan commun local est `PLAN-SEPARATION-LIBRAW.md` dans le dépôt principal,
hors Git ; propositions I2/I3 et revue I1 y sont consignées pour Claude.

## 1. Contrat du banc et premier contrôle de lecture — 28 octobre 2026

Outils : [banc RAW](../../../tests/raw_bench/README.md). Captures binaires
little-endian avec manifeste versionné, SHA256, origine/forme/stride/unité,
CFA et repère d'orientation. Comparaison fail-closed : étape manquante,
contrat différent, pixels altérés ou non finis ne valident pas une parité.
Les tolérances entières restent exactement nulles ; aucune tolérance flottante
non nulle n'a été approuvée.

La sonde est construite dans `build-patch_raw_bench` sans le moteur d'un
autre worktree. Elle fait uniquement `open_file/unpack`, sans soustraction
du noir, normalisation ou dématriçage. Les niveaux LibRaw ne sont pas
présentés comme les niveaux corrigés par IPR. Les étapes aval sont absentes,
pas synthétisées.

Premier contrôle sur `6V1A0007.CR3` (EOS R6 Mark III) et `IMG_3135.CR2`
(EOS60D), trois carrés256 natifs chacun : zéro valeur différente par rapport
aux ROI visibles décodées par rawpy. Preuve :
[smoke initial](../raw/banc-raw-unpack-smoke-v001-20261028T143625Z.json).
Les deux lecteurs utilisent LibRaw : ce contrôle atteste la cohérence du
repère et de la sérialisation sur ces six ROI, pas un décodage indépendant,
une couverture C-RAW ou une équivalence du développement candidat.

La première compilation a signalé une construction ambiguë des options Qt ;
elle a été corrigée avec des `QCommandLineOption` explicites. La reconstruction
native réussit. Les sept tests couvrent égalité stricte, étape manquante,
écart entier et interdiction de tolérance entière, NaN, empreinte invalide,
mode signé non certifiable en parité, et orientation différente.

## 2. Contrôle de lecture étendu — 28 octobre 2026

Tous les28 RAW à la racine de `base`, plus les deux noirs ciblés
`6V1A0003` (ISO1600) et `6V1A9950` (ISO51200), ont été capturés après
`unpack()` puis relus par le comparateur de pixels rawpy : zéro valeur
différente sur les90 ROI256×256. Les sources et captures sont identifiées
dans le [JSON canonique](../raw/banc-raw-unpack-smoke-v001-20261028T143905Z.json),
avec versions et sources du banc. Cette campagne ne déclare pas la parité
du développement candidat ni la couverture de tous les modes compressés.

Le corpus lu couvre R6 Mark III, R7 et60D. Le type de compression n'est
pas encore audité : ne pas déduire « RAW et C-RAW couverts » de l'extension
CR3. Les champs signés, gains et captures RCD restent à raccorder à I2.

## 3. Raccordement C2 et premières parités de module — 28 octobre 2026

Le jalon Claude `bc45f4a` (avec `a132575`) a été intégré par fusion locale
`--no-ff` sur `patch_raw_bench`, commit `3cd4e01`. Aucune fusion sur `dev`,
aucune publication et aucun fichier moteur de Claude édité par le banc.
La sonde compile les sources de ce worktree dans son propre build.

`iprRawDevelopmentCapture --path candidate` raccorde `Options::observer`
et copie les trois ROI après chacun des six stages ; capture finale caméra
distincte. Le chemin de référence installe un callback LibRaw de banc pour
appeler le RCD IPR courant, puis capture la sortie entière finale. Même
`RawInput`, niveaux et paramètres appliqués aux deux chemins. Noir/blanc
natifs par défaut, ou paramètres explicites tracés pour les noirs prioritaires.
La sélection de calibration IPR et les profils DCP ne sont pas évalués ici.

Première tentative : erreur maximale2,98e−8 uniquement dans la représentation
flottante finale de la sonde de référence (`convertTo`, multiplication par
réciproque). Aucun compte16 récupéré différent ; pré-RCD/RCD/médiane exacts.
Preuve échouée conservée :
[parité initiale](../raw/banc-raw-parite-v001-20261028T151748Z.json).
La sonde a été corrigée en normalisant les entiers par division float
explicite, conformément à I2. Moteur inchangé, aucune tolérance élargie.
Attention : ce point de représentation reste à qualifier dans C3, où le
chemin de production actuel utilise `convertTo` avant le profil.

Rejeu sur0007.CR3 et3135.CR2 : cinq frontières communes ×trois ROI par
fichier strictement égales (max0), six stages candidat présents et finis.
[Preuve de module](../raw/banc-raw-parite-v001-20261028T151838Z.json).
Les stages candidat signés et normalisés n'ont pas de frontière indépendante
observable dans LibRaw : leur présence est contrôlée, pas présentée comme
une égalité à une capture LibRaw inexistante.

## 4. Campagne de module étendue terminée — 28 octobre 2026

Gel de30 sources :28 RAW de `base` et deux noirs prioritaires1600/51200.
[Gel préalable](../raw/gel-banc-raw-parite-v001-20261028T152028Z.json).
Pour les sources présentes dans les captures de production de l'étude bruit,
les niveaux noirs/blancs sont repris explicitement avec contrôle SHA256 ;
aucun coefficient réajusté. Les autres sources restent aux niveaux natifs
du décodeur, sans prétendre reproduire la politique de calibration entière.

Calcul terminé : [résultats canoniques](../raw/banc-raw-parite-v001-20261028T152231Z.json).
Les30 fichiers passent, cinq frontières ×trois ROI par fichier, soit450
comparaisons avec écarts exactement0, sans tolérance non nulle. Les six
captures de l'observateur candidat sont présentes et finies pour chaque
source. Données sous `build-patch_raw_bench/parity/20261028T152028Z`.
Empreintes du gel, des60 manifestes et des30 comparaisons vérifiées à la
reprise ; le chargement des captures contrôle les empreintes binaires.

Pour les noirs, niveaux explicites R/G/B/G2 :0003,2048/2048/2048/2048,
et9950,2042/2043/2041/2043 ; blanc14351 dans les deux cas. Entre46,9 et
47,2 % des pixels des ROI0003 sont sous le noir avant borne, et entre48,9
et49,9 % sur9950. Ces valeurs sont visibles dans la capture f32 signée ;
le mode A conserve volontairement la borne basse ensuite. Ce n'est pas
une validation du mode B non borné ni un gain de bruit démontré.

Verdict **parité de module C2 sur les ROI contrôlées**, pas du pipeline
complet : niveaux natifs sur28 sources, explicites sur les deux noirs,
orientation neutralisée, DCP absent. Ni le bon choix des calibrations IPR,
ni les modes de compression, replis, performances ou tous les pixels de
l'image ne sont certifiés. Les14 prises de réserve du bruit restent fermées.

## 5. Raccordement aux anciennes captures de bruit — 28 octobre 2026

`tests/raw_bench/compare_noise_baseline.py` relit les deux noirs calibrés
de §4 et les buffers de production conservés avant C2 dans l'étude bruit.
[Résultat canonique](../raw/banc-raw-reference-bruit-v001-20261028T152403Z.json).
Les sources et binaires historiques sont vérifiés par SHA256, géométries
et coordonnées identiques ; aucun nouveau décodage ou rendu.

Pré-RCD et post-médiane : écart strictement0 sur les six ROI de chaque
frontière. Cela complète le contrôle où les deux chemins utilisent le même
RCD actuel : on rejoint aussi ces témoins historiques sur les deux sources.
Caméra finale avant DCP : quelques ulps de représentation, maximum7,45e−9,
mais zéro compte16 récupéré différent. Ce point confirme la réserve de §3 ;
il n'est pas masqué par une tolérance élargie ou déclaré bit-identique avant
DCP. Il devra être tranché dans le raccordement C3 et le rendu complet.

## Conclusion actuelle et fiche de reprise

- Branche/worktree : `patch_raw_bench`, build isolé ; aucun fichier moteur
  ou fichier attribué à Claude modifié. Aucun push, fusion ou tag.
- X1/X2 en cours : observateur C2 raccordé, parité de module exacte sur30
  sources ; témoins bruit historiques rejoints aux frontières entières. Les profils, calibration automatique,
  orientation, replis et performance restent à qualifier après C3.
- I2 accepté par Claude au journal, unités confirmées et prises en compte.
  I3 implémenté avec portée de comparaison explicite et huit tests.
- Contrôle de lecture étendu terminé :30 fichiers,90 ROI, aucun écart.
- À faire : raccorder le jalon C3 annoncé par Claude pour la comparaison du
  pipeline complet, y compris représentation finale f32/DCP. X1/X2 non cochés à ce stade.
- L'étude de bruit est suspendue comme demandé ; ses preuves sur `patch_noise`
  ne sont pas promues en validation du développement flottant.
