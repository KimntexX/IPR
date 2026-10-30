# Séparation du développement RAW — rapport évolutif

## Bilan actuel — 30 octobre 2026

Dernier jalon (§31) : repli global préparé au-dessus du candidat(c1), après
les abstentions signalées par Claude sur la campagne du plancher sombre.
77 contrôles natifs, sept suites et test optique passent. **Qualification
synthétique seulement, pas de promotion.** Les22 scènes signalées restent à
mesurer avec ce candidat ; la campagne réelle de(c1) est menée par Claude.
Forces et bornes DCP inchangées ; coût du repli encore à mesurer.

Socle X1/X2 préparé sur `patch_raw_bench` depuis `dev` (`6e614ca`). Le
comparateur et les captures après `unpack()` et via l'observateur C2 existent ;
trente-sept tests Python passent via quatre groupes CTest. F1 et son garde sont
intégré localement après clôture des mesures C3/X4 figées.
Le banc ne modifie pas le moteur ; la parité C3 du pipeline complet est
validée sur le corpus figé, pas une amélioration de bruit ou un gain de vitesse.

Dernière campagne §4 :30 RAW,450 comparaisons de zones sur cinq frontières
communes C2, écarts strictement nuls. Les deux noirs calibrés rejoignent
également les anciens buffers de bruit avant RCD/après médiane (§5).
La représentation flottante finale et le rendu DCP sont qualifiés par C3 (§9).

Campagne C3 terminée (§9) : 60/60 cas passent, images complètes avec/sans
DCP, métadonnées et budget par ouverture. Aucun C4 fusionné dans ses sources.
Le premier X4 (§§7–8) portait sur le C4 à bornes internes. Claude a corrigé
ces bornes dans `ee8f26d` ; le nouveau X4 est terminé (§§10–11), avec capture
après médiane avant borne finale et invariance A sur les douze fichiers.
Cette campagne C4 est historique : F1 a depuis supprimé la borne de sortie B.
Qualification F1 terminée (§§14–16) ; B n'est pas activé par défaut.
Le branchement expérimental du débruitage avant LCP/DCP est maintenant
raccordé (§§17–23), désactivé par défaut. Les bornes DCP restent inchangées.

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

## 6. Campagne production `dev` / C3 lancée — 28 octobre 2026

Le jalon C3 `32c6853` est intégré par fusion locale dans `patch_raw_bench`.
La référence est une archive Git du `dev` figé `6e614ca`, sous le build du
banc ; aucun autre worktree ou cache CMake n'est réutilisé. Les deux moteurs
sont construits séparément avec le même programme de mesure public, depuis
`tests/raw_bench/production`. Aucun fichier moteur édité ou bascule de sonde
ajoutée à la production. Les travaux C4 de Claude restent exclus.

Périmètre :30 RAW (28 base +noirs1600/51200), un rendu Camera Neutral et un
sans profil par source, WB boîtier, optiques désactivées, manifeste explicite
commun. DCP absolus et empreintes figés avant calcul, absence de profil
obtenue via un dossier de recherche explicitement vide. Le R7 figé est inclus.
[Gel de campagne](../raw/gel-banc-raw-production-v001-20261028T153440Z.json).

Contrôle : empreinte des lignes de toute l'image Rec.2020 RGBA, type/forme,
métadonnées appliquées et trois ROI binaires. Une différence hors des ROI
échoue même si leurs pixels sont identiques. Les sorties non finies sont
refusées. Les premières campagnes de module n'anticipent pas ce verdict.

Ressources : timer de `RawDecoder::decode` avant hash/export ; budget20,8s,
maximum8 workers RCD, OpenCV1, OMP8. Pic working set du processus relevé
avant sérialisation. Processus frais, ordre référence/candidat alterné,
cache disque OS non purgé : mesure exploratoire, pas intervalle de confiance
d'un gain de vitesse. Le pic mémoire ne prouve pas à lui seul le respect
du plafond système80 % ou les bornes du cache.

État persistant sous `build-patch_raw_bench/production/20261028T153440Z/status.json`,
logs de configure/build par moteur et sortie partielle en cas d'échec.
Session de lancement67817. Les quatre nouveaux tests de contrat vérifient
que forme, type et empreinte entière doivent correspondre ; ils s'ajoutent
aux huit tests précédents. Aucun verdict de C3 tant que la campagne n'est
pas terminée. Replis balance auto/noir spatial/autres formats, optiques,
annulation et performance répétée restent à qualifier séparément.

## 7. Revue C4 et pilote X4 lancé — 28 octobre 2026

Le jalon `42f99b6` propose une entrée CFA signée et supprime la grille16
dans le développement B. A reste la politique du décodeur de production.
Une archive Git de ce commit est compilée séparément sous
`build-patch_raw_bench/signed-c4` : aucune source de la campagne C3 en cours
n'est modifiée et aucun fichier moteur appartenant à Claude n'est édité.

**Réserve constatée avant calcul :** `Demosaic.cpp` applique encore une
borne `max(0, estimation)` au vert interpolé, aux R/B diagonaux et aux R/B
sur sites verts. `flatChromaMedian` borne également R/B à zéro. B préserve
donc les négatifs à l'entrée CFA, mais pas toutes les composantes signées
jusqu'à la sortie de médiane. La réserve et la demande de correction sont
transmises au journal commun. Aucune correction de ces fichiers moteur par
le banc. L'observateur post-médiane est appelé après borne/grille finale :
une statistique de médiane avant borne ne peut pas être inventée à partir
de cette capture. Le stage post-RCD, lui, précède effectivement la médiane.

Pilote lancé sur six paires déjà étudiées, toutes accessibles : ISO1600
et51200, chacun avec noir bouchon, proche noir et scène éclairée.
Trois ROI fixes256 par image, quatre phases CFA séparées et RGB caméra
par canal ; niveaux noirs/blancs repris des captures historiques avec
SHA256 par fichier, paramètres identiques entre A et B sur chaque prise.
Le jeu réservé reste fermé. Les statistiques distinguent moyenne, variance
spatiale, zéros, négatifs, covariance et différence de paires divisée par
racine de2. La variance centrée et le second moment brut sont tous deux
conservés pour ne pas masquer une dérive moyenne entre prises.

[Gel X4](../raw/gel-banc-raw-signe-v001-20261028T154441Z.json).
Statut sous `build-patch_raw_bench/signed/20261028T154441Z/status.json`,
logs par capture et arrêt au premier défaut de contrat, valeur non finie
ou source modifiée. Outils : `signed_campaign.py`, `signed_measurement.py`.
Vingt tests Python passent, dont huit nouveaux tests des calculs X4.
Pas de TIFF mesuré, pas de nouveau coefficient ajusté, pas de verdict
visuel ni de gain de débruitage à partir d'une variance plus basse.

Limites : le noir moyen est une mesure relative au noir appliqué, pas une
référence zéro indépendante ; une différence de paires élimine aussi le
motif fixe commun et le contenu stable. Les scènes ne sont pas recalées
ni corrigées d'un changement d'éclairage. Les unités DN ne sont jamais
mélangées aux valeurs normalisées et les deux verts ne sont pas fusionnés.
La fraction supérieure ou égale à1 n'indique le blanc que pour les stages
normalisés, pas pour les DN. Aucun profil DCP ou rendu d'affichage dans
ce premier pilote de module. Une revue visuelle suivra le diagnostic,
elle ne pourra pas valider des frontières signées qui restent bornées.

Chevauchement C3/X4 à partir d'environ15:44 UTC : la parité de pixels reste
exigée exactement ; les mesures C3 de temps/mémoire ne sont pas un benchmark
isolé et ne doivent pas servir à annoncer un gain de vitesse causal.

## 8. Résultats du premier X4 et décision — 28 octobre 2026

Les six paires et24 captures sont terminées, sans contrat invalide ni
valeur non finie. [JSON canonique X4](../raw/banc-raw-signe-v001-20261028T154536Z.json).
Verdict `measured_not_validated` : un calcul correct n'est pas une validation
de la politique B. Les données détaillées par phase, canal et ROI restent
dans ce JSON, avec les empreintes des captures binaires de diagnostic.

Sur les noirs bouchon, rapport **variance temporelle B/A**, sans racine :

| ISO | CFA avant RCD, étendue phases/ROI | Caméra après médiane et borne, étendue canaux/ROI |
| --- | --- | --- |
| 1600 | 2,61–2,75 | 0,957–1,102 |
| 51200 | 2,70–2,92 | 1,047–1,092 |

Le débornage de l'entrée CFA révèle bien une variance sous le noir que A
tronquait. Cela n'est ni du nouveau bruit ajouté par le capteur ni une
preuve de gain de qualité : les deux versions partent des mêmes comptes.
Après les bornes internes et la borne finale, l'écart de variance devient
nettement plus petit ; il ne prouve donc pas une propagation fidèle de
la distribution signée à travers tout RCD/médiane.

La moyenne des noirs en sortie baisse par rapport à A : environ15–26 %
selon canal à1600,16–18 % à51200 (moyennes des deux prises et trois ROI).
C'est une baisse du plancher positif relativement au noir appliqué,
pas une mesure absolue de justesse du noir. Simultanément, la fraction à
zéro augmente en sortie : sur la première prise, moyenne des trois ROI,
environ41/54/41 % en R/G/B à1600 contre14/37/14 % en A ; à51200,
37/52/38 % contre14/32/14 %. La masse à zéro confirme qu'il serait
trompeur de déduire une amélioration de débruitage du seul niveau de variance.

Les proches noirs changent aussi : rapport de variance caméra B/A entre
1,032 et1,138 à1600 et entre1,078 et1,126 à51200. Sur les zones éclairées
du même pilote, il reste entre1,002 et1,007 à1600 et entre1,000 et1,0014
à51200. Les niveaux moyens y changent peu : environ+0,14 à+0,17 % à1600,
moins de+0,005 % à51200. Ce résultat limité à ces zones ne garantit pas
les couleurs, textures et hautes lumières de tout le corpus.

**Décision :** conserver A en production ; demander à Claude de lever ou
justifier explicitement les bornes internes de B, avec invariance A et
une capture post-médiane pré-borne. Ne pas générer une série utilisateur
présentée comme validation d'un mode entièrement signé tant que ce point
n'est pas réglé. Les mesures C4 livrées sont conservées pour quantifier
l'effet de la correction future, sans élargir de tolérance ou régler des
coefficients sur ces six paires. Pas de relance identique de ce succès.

## 9. Résultat final C3 — 28 octobre 2026

[Résultat canonique de production](../raw/banc-raw-production-v001-20261028T154842Z.json).
Les 60 cas passent : 30 RAW × deux modes (Camera Neutral et sans profil),
60 empreintes d'image entière, métadonnées et ROI exactes, aucun défaut de
valeurs finies ni dépassement du budget 20,8 s. Boîtiers effectivement couverts :
EOS 60D, EOS R6 Mark III et EOS R7. Le candidat est C3, pas B.

Temps de décodage médian : référence 2394 ms, candidat 2541 ms ; maximum
7316 / 7436 ms. Pas de gain de vitesse démontré : une seule exécution par
cas, cache disque non purgé et fin de campagne chevauchant X4. Pic mémoire
processus médian 1871 / 1624 Mio ; maximum 2258 / 1823 Mio. Ce pic mesuré
ne valide pas à lui seul le plafond mémoire système de 80 % ni le cache.

Verdict `pass_scoped_C3` : égalité complète sur ce corpus et ces réglages.
Les replis balance auto/noir spatial/autres marques, les corrections optiques,
l'annulation et les variantes de compression non identifiées restent hors
de cette validation. Aucun déploiement, fusion dans `dev` ou tag implicite.

## 10. X4 corrigé et frontière avant sortie — 28 octobre 2026

Claude livre `ee8f26d` : `Demosaic::Range::Signed` lève les bornes basses
des estimations RCD et de la médiane pour B. A reste `NonNegative`. Nouvelle
capture I2 `pre_output_camera_rgb`, après médiane avant borne et grille de
sortie, dans les deux politiques : sept captures I2, plus la caméra finale
du banc. Les stages A antérieurs gardent leur contenu ; leur invariance
est contrôlée contre les captures du premier X4, sur les mêmes sources.

Une nouvelle archive immuable est compilée sous
`build-patch_raw_bench/signed-c4-corrected`. Aucun fichier moteur édité,
aucune modification des preuves C3 ou du premier X4. La première tentative
du lanceur a été interrompue après les captures du noir 1600 : une variable
locale de mon script masquait le JSON de l'étude précédente (`KeyError:
records`). Gel `gel-banc-raw-signe-v001-20261028T155917Z.json`, statut et
partiel d'échec conservés sous le build, avec une
[mémoire canonique de l'échec](../raw/echec-banc-raw-signe-v001-20261028T155917Z.json).
Ce n'est pas un échec moteur B ;
aucun résultat global n'a été publié pour cet essai. La sélection des entrées
est maintenant une fonction isolée testée, sans modifier le JSON précédent.

[Gel du rejeu corrigé](../raw/gel-banc-raw-signe-v001-20261028T160000Z.json).
Six mêmes paires, mêmes noirs/blancs/WB, mêmes ROI ; aucun nouvel ajustement
de coefficient. Les sept étapes I2 sont obligatoires sur chaque ROI ; leur
absence commune aux deux politiques échoue aussi, au lieu de passer sous
silence. La comparaison historique de A porte sur les sept frontières
observables auparavant, pas sur une capture pré-borne historique inexistante.

Les statistiques incluent désormais le décalage moyen ajouté par la borne,
la fraction de négatifs avant borne et le contrôle exact en B :
`post_median_camera_rgb = clip(pre_output_camera_rgb, 0, 1)`. Cela sépare
le comportement de RCD/médiane de celui de la sortie bornée ; aucun bénéfice
visuel n'est déduit automatiquement d'une baisse de variance. Vingt-six tests
Python passent après correction du lanceur. Statut persistant sous
`build-patch_raw_bench/signed/20261028T160000Z/status.json`, arrêt au premier
défaut. Attendre son résultat avant tout verdict B et les rendus utilisateur.

## 11. X4 corrigé terminé — 28 octobre 2026

[Résultat canonique X4 corrigé](../raw/banc-raw-signe-v001-20261028T160057Z.json).
Six paires, 24 développements : sept captures I2 et caméra finale, toutes
présentes et finies. A est strictement identique à ses captures antérieures
sur les douze fichiers : sept frontières communes × trois ROI × douze,
soit 252 comparaisons exactement nulles. La nouvelle frontière pré-sortie
est effectivement observée, pas reconstruite. Sur les 36 ROI RGB finales
de B, la sortie est exactement le bornage `[0,1]` de cette frontière.

La réserve de §7 est corrigée : dans les noirs, B conserve encore environ
49–52 % de valeurs négatives par canal juste avant sortie, après RCD et
médiane. Pour les noirs bouchon, rapport **variance de paires B/A** :

| ISO | Après médiane, avant sortie | Après borne finale |
| --- | --- | --- |
| 1600 | 2,703–2,963 | 1,031–1,102 |
| 51200 | 2,854–3,198 | 1,084–1,161 |

Étendues par canal et ROI. Avant sortie, les moyennes signées des noirs B
se rapprochent fortement du zéro appliqué : à 1600, R/G/B valent environ
−8,0 / +11,3 / −6,2 ×10⁻⁶ contre +336 / +209 / +341 ×10⁻⁶ en A ; à 51200,
+653 / +294 / +65 ×10⁻⁶ contre +9941 / +6470 / +9661 ×10⁻⁶ en A.
Ces moyennes sont celles des deux prises et trois ROI. Ce n'est pas une
vérité indépendante du noir idéal : les niveaux appliqués restent les mêmes.

La borne finale recrée un plancher positif. Moyennes après sortie en B :
R/G/B ≈227 /177 /229 ×10⁻⁶ à 1600 et 7458 /5333 /7165 ×10⁻⁶ à 51200.
Le biais avant borne est donc bien distinguable du biais ajouté en sortie.
Préserver le signe dans RCD/médiane est utile à l'étude du bruit, mais
**ne suffit pas à transmettre une distribution signée au débruitage aval**
si l'on borne ensuite avant ce débruitage. Le mode B mesuré n'est pas une
réduction de bruit : sa variance finale peut être supérieure à celle de A.

Dans les proches noirs, variance finale B/A entre 1,096 et 1,168 à 1600
et entre 1,122 et 1,170 à 51200. Sur les seules zones éclairées du pilote,
écart de variance finale entre +0,16 et +0,80 % à 1600 et +0,003 à +0,177 %
à 51200. L'absence d'écart important sur ces zones ne certifie pas tous
les détails, saturations et rendus DCP ; aucune hausse/baisse de variance
n'est assimilée automatiquement à une amélioration de rendu.

**Décision X4 :** correction des bornes internes confirmée sur ce pilote,
invariance A et capture pré-borne fonctionnelles. Pas de nouveau coefficient,
pas d'ouverture du jeu réservé ni de déploiement B. Les prochains rendus
devront comparer A/B avec mêmes noirs, blanc, balance, DCP, exposition et
sortie sRGB ; pas de simple gamma appliqué aux RGB caméra. Pour reprendre
le bruit physique, garder une frontière signée accessible avant la borne,
ou traiter avant celle-ci : ce choix moteur reste à échanger avec Claude,
pas une modification implicite du décodeur de production.

Après clôture des campagnes, `ee8f26d` a été fusionné **localement seulement**
dans `patch_raw_bench` pour aligner l'interface du banc sur celle du moteur.
Le candidat effectivement mesuré demeure l'archive figée de `ee8f26d`,
pas un moteur modifié pendant l'exécution. Aucun fichier moteur édité par
Codex ; pas de push, de fusion vers `dev` ni de tag. Les anciennes preuves
et captures restent intactes. Vingt-six tests Python / trois groupes CTest
passent ; l'essai interrompu du lanceur reste explicitement séparé.

## 12. Direction validée : sortie flottante signée — 28 octobre 2026

L'utilisateur confirme l'option **(a)** : sortir le développement RAW en
flottants signés, plutôt que conserver une sortie entière qui détruit les
valeurs sous le noir avant leur exploitation. Cette validation porte sur
la direction d'architecture, pas sur une activation de B déjà livrée ou
un bénéfice visuel de débruitage démontré. Claude prend en charge le moteur ;
le banc qualifiera les frontières, la non-régression et les ressources.

Chemin cible : RAW → développement flottant signé → débruitage dans un
domaine linéaire adapté → rendu DCP → master Rec.2020 linéaire → affichage
ou export. Conserver l'accès aux frontières CFA et RGB caméra : selon la
méthode, le débruitage pourra intervenir avant ou après RCD. Le choix (a)
n'impose pas de placer tous les algorithmes dans le décodeur comme en (b).
Les valeurs négatives doivent rester disponibles jusqu'au débruitage,
avant les traitements non linéaires du DCP ; supprimer aussi les allers-retours
entier16/flottant devenus inutiles, sans ajouter un chemin legacy permanent.

Lecture du code au moment de la décision : `CameraSignal::validate` accepte
déjà des RGB caméra `CV_32FC3` négatifs et finis. Le chemin DCP purement
matriciel conserve la plage étendue. En revanche, `applyHueSatMap` borne
son entrée à `[0,1]`, `applyCurve` borne son abscisse, et le mode de courbe
préservant la teinte utilise aussi une entrée bornée. Une entrée flottante
ne garantit donc pas une conservation des négatifs dans tout le DCP.
Auditer et expliciter ces frontières : il ne s'agit ni de faire passer
aveuglément un signal signé dans une table HSV, ni de lever toutes les
bornes sans définir le comportement mathématique attendu.

Avant activation : vérifier conservation des négatifs et absence de
quantification aux frontières linéaires, valeurs finies et comportement
près de zéro ; contrôler séparément matrices, tables et courbes du DCP,
chemin sans profil, orientation et corrections optiques ; rejouer les
témoins R6 III, R7 et 60D. Distinguer l'invariance des étapes inchangées
des écarts intentionnels dus au retrait du bornage. Mesurer temps et pic
mémoire réels, notamment les copies et buffers retenus par le cache.
Un RGB24 Mpx passe théoriquement de 144 à 288 Mo en passant de trois
entiers16 à trois flottants32, soit +144 Mo pour ce tampon isolé ; le coût
net du pipeline dépend des conversions et allocations effectivement retirées.
Pas de nouveaux TIFF Lightroom nécessaires pour cette qualification technique.

**Chantier ultérieur demandé par l'utilisateur : harmonisation des domaines
couleur entre modules.** Recenser les fonctions utilisant sRGB, Oklab,
ProPhoto et les autres domaines, puis définir des contrats et conversions
cohérents autour du master Rec.2020 existant. Distinguer espace de travail,
espace de calcul auxiliaire et rendu d'affichage : imposer un unique domaine
numérique à toutes les méthodes n'est pas décidé ici. Les domaines internes
nécessaires à certaines méthodes ou aux tables DCP devront être justifiés
et leurs conversions explicites. Ce travail reste **différé**, sans changement
de modèle colorimétrique ni recalage des réglages pendant la transition RAW.

Décision et points de vigilance transmis dans le journal commun
`PLAN-SEPARATION-LIBRAW.md`. Aucun changement moteur, nouveau calcul,
push, fusion vers `dev` ou tag dans cette mise à jour documentaire.

## 13. Préparation F1, placement bruit et relecture F2 — 28 octobre 2026

L'utilisateur demande à Claude et Codex de lancer ensemble la transition
flottante et annonce son absence jusqu'à 23 h. Répartition maintenue : F1
moteur et inventaire F2 chez Claude ; qualification, captures et proposition
de raccordement du bruit chez Codex. Pas de publication ou fusion vers `dev`.

### Placement du débruitage proposé

Pour un premier raccordement, traiter le **RGB caméra linéaire signé après
RCD et la médiane actuels, avant LCP puis avant DCP et retouches**. LCP
rééchantillonne le signal et peut amplifier le bruit par vignettage ; placer
le débruitage avant évite d'avoir à retrouver sa loi après ces opérations.
Garder aussi une frontière CFA accessible pour les futurs algorithmes,
sans déplacer la médiane ni changer RCD en même temps que F1.

Le modèle doit tenir compte des gains WB déjà appliqués et des corrélations
introduites par RCD/médiane. Un coefficient de variance CFA ne devient pas
une variance RGB indépendante par simple copie. Conserver une source caméra
signée, non débruitée et immuable ; chaque modification du curseur repart
de cette source, force zéro = identité exacte. Le changement de DCP et les
retouches aval ne doivent pas déclencher un nouveau décodage. Les besoins
de cache borné, annulation, prévisualisation et export à pleine définition
seront spécifiés au raccordement, derrière un module, pas calculés dans
`ImageEditor`. Le débruitage actuel après les tons/couleurs dans le cadenceur
n'est pas annoncé déplacé : cela reste un chantier distinct de F1.

La remarque utilisateur transmise par Claude est intégrée : l'utilisateur
peut régler le bruit tôt, même si son panneau apparaît actuellement en bas.
L'ordre des gestes n'est pas l'ordre du calcul ; le débruitage doit rester
en tête de développement quel que soit le moment où le curseur est modifié.
Remonter le panneau est une amélioration IHM séparée, sans intervention
sur `patch_ui` ou les réglages photographiques pendant cette préparation.

### Banc F1 préparé et sonde d'entrée DCP vérifiée

`iprProductionRawCapture --capture-camera` lit le tampon réellement retenu
dans `image.dcpContext->cameraRgb`, pas une reconstruction depuis Rec.2020.
Il capture trois ROI RGB32 et, sur tout le tampon, hash, forme, finitude,
moyennes, minima/maxima et fractions de négatifs/zéros. Temps de décodage
et pic mémoire sont relevés avant ces diagnostics. Sans DCP, ce contexte
n'existe pas : la sonde refuse de prétendre capturer une entrée absente.
La sortie Rec.2020 sans DCP reste contrôlable séparément.

[Gel de contrôle de la sonde](../raw/gel-banc-raw-entree-dcp-reference-v001-20261028T162257Z.json)
et [résultat canonique](../raw/banc-raw-entree-dcp-reference-v001-20261028T162354Z.json) :
cinq témoins A de la référence figée `ee8f26d` passent (0043 R6 III,
7Y6A7599 R7, IMG_3135 60D, noirs 0003/9950). Images entières et métadonnées
rejoignent les témoins C3 exactement ; les entrées caméra sont finies et
non négatives en A. **C'est une validation de la sonde de référence, pas
du candidat F1 ni de sa politique B.** Le binaire de référence a ensuite
été reconstruit pour ajouter la déclaration de capacité F1 et son refus
explicite de B ; les captures/empreintes du contrôle restent historiques.

Le lanceur de production accepte maintenant `--capture-camera` pour exiger
l'égalité exacte plein cadre et ROI de l'entrée DCP, en plus du master et
des métadonnées. F1 A sera comparé à `ee8f26d` sur 30 RAW avec/sans DCP.
La nouvelle API annoncée par Claude est `Options::signedDevelopment` ;
le moteur reste en A par défaut. La sonde l'active seulement si compilée
explicitement avec cette capacité contre un moteur F1. Le binaire oracle
antérieur refuse `--development signed`, sans sortir une image A à sa place ;
ce refus a été vérifié après compilation. Ce drapeau ne conserve pas un
ancien moteur dans la production : il distingue les capacités des deux
exécutables de comparaison, provenant de commits différents.

`float_campaign.py` prépare le contrôle B sur les six paires existantes,
sans nouveau TIFF : entrée DCP effectivement signée sur les noirs témoins,
finitude caméra/master, sources et paramètres tracés, temps et pic mémoire.
Des négatifs ne sont pas exigés sur toute photo éclairée. Leur présence ne
prouve pas seule l'absence de toute quantification intermédiaire : compléter
par l'audit du chemin F1 et les captures de module avant/après développement.
La représentation A annoncée est `float(k) * float(1/65535)`, identique au
produit historiquement reçu par le DCP ; ne pas masquer un changement de
représentation de l'étape I2 par un élargissement des tolérances.

Trente-deux tests Python / quatre groupes CTest passent. Une collision
avec les macros Windows `min/max` dans mon nouveau code de statistiques
a été corrigée avant le contrôle de la sonde. Aucun fichier moteur de
Claude modifié. À ce stade aucun commit F1 figé n'est livré au journal :
pas de qualification F1 revendiquée, ni d'attente active d'un commit absent.

### Relecture de l'inventaire F2 de Claude

L'inventaire est cohérent avec le code : matrices DCP étendues ; bornes
dans HueSatMap/LookTable et courbes ; chemin sans DCP distinct ; pas de borne
explicite repérée dans LCP. L'existence de négatifs créés par la matrice
en A signifie qu'une extension de ces fonctions changerait déjà certains
rendus actuels. **Aucun changement de rendu F2 pendant F1.**

La proposition de résidu transmis est un candidat colorimétrique à tester,
pas une loi de bruit ni une transformation automatiquement neutre. Pour
une courbe scalaire, prolonger sous zéro par `f(0) + f'(0) * x` utilise
la pente du premier segment ; gérer aussi `f(0)` non nul. Pour HSV/LUT,
les canaux sont couplés : ajouter un résidu signé composante par composante
ne garantit pas la continuité de la dérivée. Vérifier continuité, frontières
entre canaux/secteurs, neutralité et variations de teinte ; ne pas généraliser
sans contrôle la règle scalaire à une LUT multidimensionnelle.

Le traitement des valeurs supérieures à un constitue un problème séparé,
non résolu par un résidu uniquement négatif. Après débruitage, des négatifs
peuvent aussi représenter du bruit résiduel ou une incertitude du noir, pas
uniquement une couleur hors gamut. Enfin, une interpolation LCP sans borne
peut néanmoins changer variance/covariance et comportement aux bords :
la qualifier séparément, avec et sans optiques, pas la déduire des mesures
optiques désactivées. Toute règle de rendu proposée par F2 reste à valider
avec l'utilisateur, sans harmonisation des espaces couleur à ce stade.

## 14. F1 : contrat flottant et campagnes figées — 28 octobre 2026

Claude livre F1 `7474aad` : RGB32 de bout en bout dans le décodeur, plus
d'aller-retour entier16 sur le chemin IPR. Le repli LibRaw convertit sa
sortie entière une fois ; il n'est pas devenu un développement signé.
`Options::signedDevelopment` sélectionne B sur le chemin Bayer Canon IPR,
mais reste faux par défaut. B ne borne plus ni sous zéro ni au-dessus de un
en sortie de `develop()` : la sortie est exactement `PreOutputCameraRgb`.
En A, la grille de sortie est représentée par `float(k)*float(1/65535)`,
le produit historiquement reçu par le DCP, et non par la division float.
Pré-RCD reste sur sa représentation par division, inchangée.

Le banc est adapté à ce contrat courant : l'ancien test de borne B est
remplacé par une identité exacte pré-sortie/sortie, avec témoins négatifs
et supérieurs à un. Les tests ne conservent pas un ancien comportement
comme mode de compatibilité. Les anciennes preuves JSON restent intactes.
La comparaison A avec C4 est exacte sur les six frontières avant sortie ;
ses représentations finales f32 ayant changé volontairement, la vérité
de non-régression est vérifiée par l'entrée DCP et le master entiers dans
la campagne de production, sans élargir la tolérance.

Toutes les campagnes utilisent des archives immuables. Un premier lancement
a échoué à la configuration du banc, avant décodage, car un chemin relatif
était transmis à CMake : [mémoire de l'échec](../raw/echec-banc-raw-production-v001-20261028T163124Z.json).
Les arguments de chemin sont désormais résolus avant configuration, avec
un test dédié. Cette erreur d'outillage n'est pas un échec du moteur F1.

Gels et résultats :

- A production : [gel](../raw/gel-banc-raw-production-v001-20261028T163154Z.json),
  [résultat](../raw/banc-raw-production-v001-20261028T164747Z.json).
- B module, six paires : [gel](../raw/gel-banc-raw-signe-v001-20261028T163415Z.json),
  [résultat](../raw/banc-raw-signe-v001-20261028T163515Z.json).
- B décodeur complet : [gel](../raw/gel-banc-raw-flottant-signe-v001-20261028T163906Z.json),
  [résultat](../raw/banc-raw-flottant-signe-v001-20261028T164123Z.json).

## 15. Qualification F1 terminée et garde de finitude — 28 octobre 2026

**A : 60/60 cas passent** sur 30 RAW (28 base et deux noirs), Camera Neutral
et sans DCP, R6 Mark III/R7/60D. Images entières Rec.2020, métadonnées
auditées et ROI exactement identiques à `ee8f26d`. Pour les 30 cas avec
DCP, l'entrée caméra effectivement retenue est aussi comparée par hash
plein cadre et ROI : 30/30 identiques. Aucune tolérance non nulle ajoutée.

| Mesure sur les 60 cas A | Référence ee8f26d | F1 7474aad |
| --- | ---: | ---: |
| Décodage médian | 2681 ms | 2632 ms |
| Décodage maximal | 7986 ms | 7582 ms |
| Pic mémoire processus médian | 1624 Mio | 1440 Mio |
| Pic mémoire processus maximal | 1824 Mio | 1824 Mio |

Les budgets 20,8 s, huit workers RCD et OpenCV1 sont respectés. Une mesure
par cas, caches OS non purgés, chevauchement de contrôles de module/B et
de compilations : pas de gain de vitesse causal revendiqué. Le pic mémoire
reste un pic processus, pas une preuve du plafond système de 80 %. Un
instantané système pendant la campagne indiquait 55 % utilisés et 7,09 Gio
disponibles sur 15,77 Gio ; il ne prouve pas une borne pendant toute la campagne.

**B module :** les six paires / 24 développements passent les contrôles de
contrat. Sortie B bit-identique à la pré-sortie, négatifs transmis sans borne.
Les douze invariances A pré-sortie passent ; les tableaux de variance restent
des mesures exploratoires, pas un verdict de meilleur débruitage.

**B via le décodeur : 12/12 fichiers passent.** Caméra avant DCP et master
finis, budgets respectés. Dans les quatre noirs, environ 50–52 % de valeurs
par canal restent négatives sur le tampon caméra plein cadre. Les quatre
prises noires ne sont pas tournées : leurs trois ROI sont comparées
bit pour bit entre `PreOutputCameraRgb` du module et entrée DCP effective,
soit 12 ROI RGB exactement identiques. Ce contrôle est plus fort que la
seule présence de négatifs pour déceler une quantification ou borne
réintroduite à cette frontière. Les huit autres prises sont tournées :
les ROI capteur/affichage étant géométriquement différentes, elles ne sont
pas prétendues identiques. Pic processus B médian/maximal environ 1810 Mio,
temps médian/maximal 5675/7190 ms ; mêmes réserves de mesure concurrente.

### Défaut de frontière publique détecté puis corrigé par Claude

Le test natif `iprRawFloatContract` sur `7474aad` confirme qu'une entrée
flottante valide peut être signée et supérieure à un, mais que NaN/±Inf
étaient acceptés par `fromLinearSrgb`, produisant un master non fini :
[preuve d'échec initial](../raw/contrat-entree-flottante-v001-20261028T164212Z.json).
Claude ajoute le garde dans `0adefe7`, sans borner le signal fini. Le même
test sur une archive distincte de ce commit passe 4/4 :
[preuve corrigée](../raw/contrat-entree-flottante-v001-20261028T164640Z.json).
Les deux résultats sont conservés ; les essais sur RAW réels ne remplacent
pas ce contrôle d'entrée malformée. Aucun fichier moteur édité par Codex.

Pour ne pas réexécuter inutilement les 60 succès de F1, le garde est aussi
contrôlé sur trois témoins ciblés : 9153 R6 III, 7Y6A7599 R7 et IMG_3135 60D,
en A/B avec/sans DCP, soit 12 décodages.
[Gel](../raw/gel-banc-raw-flottant-temoins-v001-20261028T164745Z.json) et
[résultat](../raw/banc-raw-flottant-temoins-v001-20261028T164912Z.json).
Tous passent : les six A rejoignent exactement les images entières et
métadonnées C3 ; les six B sont finis. Le dépassement supérieur à un de
9153 est conservé dans l'entrée DCP. Le test de contrat positif confirme
également que le garde ne borne pas arbitrairement le signal étendu.

**Portée du verdict :** transition F1 techniquement qualifiée sur les
témoins cités, avec le garde de finitude validé séparément et par régression
ciblée. Pas de promotion de B par défaut, de validation visuelle du bruit,
de règle DCP F2 appliquée ni de généralisation à tous les formats/boîtiers.
Replis LibRaw, optiques actives, annulation, fichiers corrompus et variantes
de compression non identifiées restent hors de cette campagne. Rien à
faire générer dans Lightroom pour clôturer cette qualification technique.

## 16. Suite bruit proposée et fin de campagne — 28 octobre 2026

Le point d'insertion proposé reste le RGB caméra signé après RCD/médiane,
avant LCP puis DCP/retouches, quelle que soit la position du panneau ou
l'ordre des gestes. Une source pré-LCP immuable est nécessaire pour rejouer
le curseur depuis l'original, sans rouvrir ni débruiter cumulativement.
Le contexte DCP actuel retient une source après LCP : il ne suffit pas, à
lui seul, à placer le bruit avant les optiques quand celles-ci sont actives.
Prévoir un étage de préparation/rendu caméra avec identité de source et
cache borné, distinct du décodage et du rendu créatif du profil.

Il ne faut pas simplement déplacer l'appel existant à `DetailProcessing` :
il utilise la luminance du master, borne certaines luminances/RGB à zéro
et se trouve actuellement après tons/couleurs. Ces hypothèses ne sont pas
automatiquement valides en RGB caméra signé. Le raccordement devra définir
les coordonnées de traitement, le modèle de bruit et son transport par les
gains WB/RCD/médiane, les corrélations, les bornes justifiées et les clés de
cache. Les corrélations d'un RCD non linéaire ne sont pas garanties par une
simple propagation matricielle constante. Force zéro doit être une identité
exacte ; les traitements doivent vivre dans un module, pas dans le cadenceur.

Ce déplacement et l'adaptation du débruitage ne sont **pas implémentés par
F1**. La prochaine étape utile est un premier raccordement expérimental
pré-DCP avec ses tests de signal et de structure, puis un contrôle visuel
ciblé ; pas une nouvelle campagne de réglage des poids d'un estimateur
après avoir perdu le signe. L'harmonisation des domaines couleur et les
prolongements de tables/courbes F2 restent différés à validation utilisateur.

37 tests Python / quatre groupes CTest passent ; les quatre cas du contrat
natif passent sur le garde. Les résultats immuables distinguent bien les
commits mesurés (`7474aad` / `0adefe7`) du futur commit du banc. Les fusions
réalisées sont locales à `patch_raw_bench` uniquement ; aucune publication,
fusion dans `dev` ou création de tag. Sources des autres agents préservées.
Après sauvegarde/commit final, la fin de mes calculs sera signalée dans le
journal commun ; Claude a confirmé sa fin de travail et prend en charge
la mise en veille demandée par l'utilisateur, après mon signal explicite.

## 17. 29 octobre — premier module expérimental avant profil (N2)

L'utilisateur autorise un branchement **désactivé par défaut**, commandé par
une case du panneau bruit existant. N1 (Claude) porte la source caméra,
le raccordement et l'IHM ; N2 (Codex) porte `CameraDenoise` et ses tests.
Les règles de bornage DCP ne sont pas modifiées (D0 reporté).

Le premier module travaille sur le RGB caméra flottant, après RCD/médiane,
sans borne basse ou haute. Après compensation des gains WB, trois échelles
d'ondelettes à trous B3-spline séparent l'intensité caméra et deux axes
opposants. Cette intensité n'est pas la luminance photométrique Rec.2020.
Le bruit et les corrélations sont estimés sur les coefficients des régions
les plus calmes, puis un retrait Wiener local protège les structures communes.
Une région fortement périodique n'est pas un témoin sûr : abstention plutôt
que calibration sur ce motif. La correction conserve les moyennes globales.

Il s'agit d'un **estimateur empirique post-dématriçage**, pas du modèle physique
capteur `a·signal+b` qualifié par boîtier. ISO n'impose aucune force inventée.
Les champs CFA/diviseur initialement envisagés dans I4 ont été retirés car
non consommés par ce modèle ; l'interface transmise à N1 conserve gains,
ISO et échelle du proxy. Les structures faibles restent une limite à mesurer.

Les forces séparées valent 0 à 100. Deux forces nulles conservent exactement
le tampon ; erreur ou annulation laisse l'entrée intacte ; la source partagée
n'est jamais modifiée. Au plus huit threads, tous rejoints ; seul le thread
appelant invoque l'annulation. Le traitement par tuiles possède un halo pris
dans les voisins réels et ne change pas les réglages globaux OpenCV.

Le premier jalon `d52d153` passe 47 contrôles synthétiques ; après raccordement
de N1 `dd2ef6d`, le jalon local `816e512` en passe 50, dont gains WB,
annulation atomique, négatifs/>1, axes indépendants, déterminisme et témoins
propres. Les cinq suites CTest du banc passent. La preuve initiale immuable est
[camera-denoise-n2-v001-20261029T123023Z.json](../raw/camera-denoise-n2-v001-20261029T123023Z.json).

Sur cette machine, le module seul, sur champ synthétique à force 100/100 :

| Définition | Temps | Pic mémoire du processus |
| --- | ---: | ---: |
| 2048 × 1365 | 188,809 ms | 124 583 936 octets |
| 6000 × 4000 | 1 318,21 ms | 621 584 384 octets |

La qualification finale N2 inclut aussi la compilation complète de `iprEngine`
et le succès de `cameraProfileEngineTests`, sans retouche des fichiers N1.
La preuve actualisée est
[camera-denoise-n2-v001-20261029T123736Z.json](../raw/camera-denoise-n2-v001-20261029T123736Z.json) :
50 contrôles natifs, cinq suites du banc et test moteur passent ; second relevé
181,675 ms / 127 369 216 octets à 2,8 Mpx et 1 234,65 ms / 621 015 040 octets
à 24 Mpx. Ces deux relevés sont des observations, pas une étude de dispersion.
Le lanceur `tests/raw_bench/qualify_camera_denoise.py` conserve commandes,
sorties, sources et empreintes des exécutables dans un nouveau JSON immuable.

Ces nombres n'incluent ni décodage, ni DCP, ni caches de l'IHM ; ils ne
qualifient pas le plafond global de RAM. La réduction de variance d'un champ
synthétique n'est pas une preuve de conservation des détails naturels ou de
similarité Lightroom. Aucun nouveau rendu n'est déclaré validé visuellement.

**N3 reste ouvert.** N1 livré retient encore une source après LCP lorsque
l'optique est active, et le sélectionneur B signé n'est pas raccordé hors
du décodeur dans ce jalon. Ces deux frontières sont signalées au journal
pour N1b, domaine de Claude. Il faudra séparer : case décochée = A inchangé ;
B sans filtrage = témoin de développement ; B filtré = effet du module.
L'identité à force zéro est relative au signal fourni, pas une preuve A=B.
Proxy et pleine définition doivent être qualifiés séparément, leur filtrage
ne commute pas exactement avec la réduction d'image.

## 18. 29 octobre — raccordement signé pré-LCP et témoins réels (N3 partiel)

Claude livre `a9434b5` et `67be313` : la case demande désormais B signé et
la rétention du signal pré-LCP ; l'identité du cache distingue A/B. Les
corrections optiques sont rejouées après le filtre. Ces commits sont fusionnés
localement dans `67dfbf7`, puis moteur et RenderServer recompilés.
Les réserves d'architecture du §17 sont donc traitées dans le code candidat,
sans que cela transforme les premiers tests N2 en qualification de ce jalon.

Un contrôle en mémoire produit 18 rendus : A, B sans filtre, B50, B100,
rejeu B100 et retour A sur `6V1A9153.CR3` (R6 Mark III), `IMG_3135.CR2`
(60D) et `7Y6A7599.CR3` (R7). Les trois témoins passent : sorties disponibles,
filtre effectif, B100 déterministe et retour A identique à son premier rendu.
Cela contrôle l'absence de contamination cumulative dans cette séquence,
**pas l'invariance A contre une ancienne version**, ni la qualité du débruitage.
Preuve canonique :
[camera-denoise-n3-v001-20261029T124016Z.json](../raw/camera-denoise-n3-v001-20261029T124016Z.json).
Le lanceur `camera_denoise_witnesses.py` ferme son serveur par scène et utilise
un watchdog de 180 s ; aucun TIFF n'est écrit. Calibration active, Camera
Neutral propre à chaque boîtier, corrections optiques désactivées selon les
déclarations : ce contrôle réel ne valide donc pas la réapplication LCP.

Différences B100 moins B0 dans les échantillons sRGB 16 bits finaux :

| Source | Écart moyen R / G / B | Écart absolu moyen R / G / B |
| --- | ---: | ---: |
| 9153 | +73 / +71 / +62 | 368 / 389 / 360 |
| 3135 | +67 / +81 / +43 | 488 / 545 / 468 |
| 7599 | −67 / −33 / −749 | 2169 / 2055 / 2712 |

Ce ne sont **pas** des mesures de bruit capteur. La conservation DC du module
caméra n'implique pas celle du rendu après DCP non linéaire et bornes : le R7
montre notamment un déplacement moyen du bleu à contrôler visuellement.
Les temps de requête B50/B100 sont environ 4,3 à 8,7 s ici (rendu/export
en mémoire compris), distincts du benchmark du module seul au §17.

**Test de raccordement optique non résolu :** `cameraProfileEngineTests`
passe sur N1 initial, mais échoue après N1b au témoin
« camera denoise before the lens corrections applies them again, source untouched ».
Ce test attend encore le rendu sans filtre à force40, hypothèse du bouchon
identité. La référence doit explicitement composer filtre, LCP puis DCP.
Signalement transmis à Claude ; son fichier de test n'est pas retouché ici.
N3 complet reste ouvert : correction de ce test, cas optiques, proxy/100 %,
ressources de l'application et revue visuelle. Aucun changement DCP D0.

## 19. 29 octobre — audit de la prétendue fuite et échelles chroma élargies

Après fusion de N2 dans la branche moteur, Claude livre `7ef1e59` : source
transmise sans copie préalable inutile, test optique composé avec le vrai
algorithme. Ce correctif est intégré localement (`4884215`). Le test moteur
qui échouait au §18 passe désormais ; ne pas conserver cette réserve comme
un défaut courant. La revue N3 relève ensuite des taches de couleur persistantes
et une hausse de 17,2 à 19,0 d'un indicateur nommé initialement « bruit lum. ».
Les deux pistes sont examinées séparément, sans modifier les bornes DCP.

### 19.1. Intensité caméra, luminance linéaire et moyenne RGB : trois mesures différentes

L'axe conservé par le filtre couleur est
`I = 0,25·R/gR + 0,5·G/gG + 0,25·B/gB`. Préserver I n'impose pas de préserver
Y après matrice, tables et courbe du profil. Sur neuf recadrages caméra fixes
(trois positions diagonales par scène, 768 px traités, centre 420 px mesuré),
la variation RMS de I est de l'ordre de 10⁻⁸ ou moins, mais celle de Y après
DCP atteint 0,0059 sur le R7. Ce constat établit la non-équivalence des axes,
pas une dégradation perceptuelle. Ces recadrages ne sont pas ceux de Claude
et l'estimation locale diffère du filtrage de l'image entière.
Preuve : [étude trois échelles](../raw/camera-denoise-leakage-v001-20261029T125644Z.json).

Une projection expérimentale du delta couleur dans le plan tangent de Y du
profil est évaluée dans le banc uniquement : gradient par différences finies,
correction le long de l'axe I (direction des gains, pas un gris scène caractérisé),
amplitude limitée et abstention
lorsque le gradient est inexploitable. Elle réduit la variation Y linéaire,
mais ne la supprime pas à travers les non-linéarités et ne garantit pas la
luma sRGB. Elle **n'est pas intégrée au module** ; aucun accès aux profils
ni nouveau calcul DCP n'est ajouté dans `CameraDenoise`.

Claude précise ensuite ses coordonnées exactes et son indicateur : moyenne
arithmétique des RGB sRGB 8 bits, non luminance pondérée. Le rejeu sur l'image
entière puis recadrage reproduit strictement son constat R7 :

| R7, ROI (2940,2940), 420 × 420 | B0 | B couleur 50, trois échelles |
| --- | ---: | ---: |
| Écart-type passe-haut σ2 de la moyenne RGB | 17,1883 | 18,9956 |
| Écart-type passe-haut σ2 de la luma Rec.709 encodée | 17,3704 | 17,1449 |
| Moyenne des écarts-types passe-haut R−G et B−G | 20,4166 | 15,7404 |
| Moyenne des écarts-types passe-bas R−G et B−G | 33,6610 | 31,9632 |

La luma pondérée baisse ici de 1,30 %, au lieu d'augmenter de 10,5 % comme la
moyenne RGB. Cette dernière est sensible à la chroma ; son augmentation ne
suffit donc pas à conclure « bruit de luminance accru ». La luma encodée
n'est pas non plus Y linéaire ni une vérité physique du bruit. Claude a
confirmé au journal le renommage de l'indicateur et retiré cette conclusion.
Preuve : [rejeu exact du prototype initial](../raw/camera-denoise-exact-roi-v001-20261029T130540Z.json).
L'exécutable initial est identifié par son empreinte déjà conservée au §18,
pas présenté comme une compilation des nouvelles sources cinq échelles.

### 19.2. Cinq échelles chroma, trois échelles d'intensité

Le prototype ajoute les pas 8 et 16 aux pas 1,2,4 pour les seuls axes couleur.
L'intensité reste traitée aux trois échelles initiales. Sans force couleur,
les deux échelles supplémentaires ne sont pas calculées. Le halo suit le
support total ; `pixelScale` adapte toujours les pas pour le proxy. Aucun
renforcement arbitraire du curseur ni nouvelle option de compatibilité.
Les campagnes ont été lancées avant le commit de ce prototype : la révision
Git enregistrée est la base `4884215`, complétée par les sources et empreintes
embarquées dans les preuves. Ne pas identifier la variante par cette seule
révision de base. Les comparaisons exactes partagent les mêmes RAW, ROI et
empreintes de pixels B0 sur les trois scènes.

Sur les neuf recadrages d'étude, la dispersion chroma lissée diminue jusqu'à
8,2 % face aux trois échelles, avec variation du passe-haut de luma inférieure
à 0,12 %. Sur le rejeu exact plein cadre, les gains sont plus modestes :

| ROI exacte | Chroma fine, 3 → 5 échelles | Chroma lissée, 3 → 5 échelles | Luma pondérée HP, 3 → 5 |
| --- | ---: | ---: | ---: |
| 7599 | 15,7404 → 15,5090 | 31,9632 → 31,3917 | 17,1449 → 17,1794 |
| 7958 | 9,1470 → 9,1469 | 30,1434 → 30,1393 | 14,9689 → 14,9690 |
| 9539 | 4,3346 → 4,3305 | 18,3383 → 18,3114 | 4,3733 → 4,3730 |

Le R7 gagne environ 1,47 % sur l'indicateur chroma fine et 1,79 % sur la chroma
lissée ; l'effet est presque nul sur les deux autres ROI. Ces statistiques
incluent structures et gradients de scène : elles ne mesurent ni une vérité
bruit ni le détail perdu. Les taches de couleur ne sont pas déclarées résolues.
Preuves : [recadrages cinq échelles](../raw/camera-denoise-leakage-v001-20261029T125945Z.json)
et [rejeu exact cinq échelles](../raw/camera-denoise-exact-roi-v001-20261029T130751Z.json).

Les 53 contrôles natifs passent, dont un champ chroma corrélé à basse fréquence
sur vérité constante connue ; le test moteur LCP corrigé et les cinq suites du
banc passent. Sur champ synthétique 24 Mpx à force100/100 : 3,83 s et pic
processus 673 423 360 octets, contre 1,23 s / 621 015 040 octets au premier
relevé trois échelles. Deux relevés cinq échelles donnent 3,27 et 3,83 s :
ce n'est pas une étude de dispersion ni un budget IHM. Le coût supplémentaire
doit être considéré face au faible gain constaté sur les ROI exactes.
Preuves : [essai intermédiaire](../raw/camera-denoise-n2-v001-20261029T130222Z.json)
et [contrat final 53 contrôles](../raw/camera-denoise-n2-v001-20261029T130408Z.json).

**Décision de prototype :** cinq échelles chroma disponibles dans le seul
chemin expérimental, toujours désactivé par défaut ; pas de projection profil,
pas de changement DCP. Cette étape n'est pas une déclaration de qualité stable.
Avant d'augmenter la force automatiquement pour compenser l'écart A/B, il faut
tenir compte de la différence de signal de départ, vérifier les textures et
présenter les forces disponibles à l'utilisateur. Ne pas compenser un biais
d'indicateur par un poids ajusté à une seule scène.

## 20. 29 octobre — N2b(a), couverture du niveau de signal avant modification du filtre

Le retour utilisateur reconnaît une nette amélioration sur 7599, mais signale
points rouges/verts à luminance100, pixels roses à couleur100, grain dans les
aplats clairs et perte de détails. La campagne N2b distingue estimation du
bruit, impulsions, grandes taches et protection des détails. Claude prend
l'interaction avec la netteté et la page visuelle v2 ; Codex commence par
mesurer la couverture de l'estimateur. Le filtre n'est pas modifié à ce jalon.

Le dernier essai D-gris négatif a été abandonné par Claude. Les essais de
bornage sont conservés dans `essais-bornes-dcp-v001-20261029T133401Z.json` :
masques variables et essai D1+D2 combiné déclarés ; ils ne qualifient ni D2
isolé ni des comparaisons à population constante. Aucun correctif de bornage
n'est introduit pour répondre aux défauts visuels du bruit.

### 20.1. Dispositif et séparation des observations

Pour 7599 (R7), 7958 (60D), 9539 et 9613 (R6 III, cette dernière dans
`base-hors-protocole`), la sonde charge le signal caméra signé, balance boîtier,
Camera Neutral et calibration actuels, sans correction optique. Aucun TIFF
n'est écrit. Une grille9×9 mesure81 régions64px : les25 positions de la grille
initiale5×5 sont réservées à l'ajustement ; les56 autres positions servent au
contrôle spatial. Ce n'est pas une séparation de scènes ou de boîtiers.

La mesure indépendante utilise les coefficients B3 à trous aux cinq échelles
dans les axes I/Co/Cg, après compensation WB, et le MAD centré/0,67448975.
Elle conserve coordonnées, intensité médiane, dérive entre quadrants,
autocorrélations horizontales/verticales et rejets de motifs cohérents.
Il s'agit de **dispersion de coefficients**, susceptible d'inclure des textures,
pas d'une vérité de bruit propre. La porte « candidat plat » (dérive des
quadrants ≤3σI fin) reste une heuristique ; elle ne certifie pas les textures
faibles. La sonde ne prétend pas reproduire bit à bit les calculs du module.
Preuve : [cartographie initiale](../raw/camera-denoise-signal-survey-v001-20261029T165726Z.json).

### 20.2. Biais de couverture observé

Le quart global des témoins les plus calmes favorise les signaux bas.
Sur9613, les intensités des régions retenues à l'échelle fine vont d'environ
−0,00019 à0,02658, tandis que le groupe clair d'ajustement couvre0,119–0,163.
Son σI conditionnel médian vaut0,01089 contre0,00275 globalement, soit environ
quatre fois plus. Les autres axes présentent aussi une hausse ; cette
différence étaye la piste d'une sous-représentation des régions claires,
sans démontrer que toute leur dispersion est du bruit.

Trois groupes de signal, définis sur les seules positions d'ajustement,
fournissent des nœuds (signal médian des témoins retenus, variance par axe).
Un candidat interpole les **variances**, puis prend leur racine. Il s'abstient
hors de l'intervalle des nœuds et n'invente aucune extrapolation par ISO.
Les positions de contrôle ne servent pas à ajuster ces nœuds. Le rejeu utilise
le JSON initial, sans redécoder les RAW ni recalculer les observations.
Preuve : [interpolation et contrôle spatial](../raw/camera-denoise-signal-survey-v001-20261029T170256Z.json).

À l'échelle fine, écart relatif médian de σI prédit aux MAD observés sur
les mêmes candidats plats de contrôle couverts par l'interpolation :

| Source | Régions comparées / candidates | Global | Conditionnel |
| --- | ---: | ---: | ---: |
| 7599 | 28 / 55 | 30,8 % | 1,5 % |
| 7958 | 32 / 34 | 3,0 % | 2,8 % |
| 9539 | 39 / 48 | 33,8 % | 2,0 % |
| 9613 | 37 / 54 | 60,8 % | 2,9 % |

Les abstentions sont explicites :27,2,9 et17 régions à cette échelle.
Les deux prédictions sont comparées sur la même population ; les régions
non couvertes ne sont pas présentées comme corrigées. Les gains ne sont
pas universels : sur7958, certaines échelles plus grossières se dégradent
dans ce contrôle. Il ne faut pas promouvoir automatiquement ce candidat.

### 20.3. État du chantier

Six tests du réducteur de données vérifient la séparation ajustement/contrôle,
les bandes, l'interpolation de variance et l'abstention. Ces résultats valident
une direction pour N2b(a), **pas un nouveau débruitage en production**. La
covariance conditionnelle, le raccord spatial et la protection des structures
restent à qualifier avant substitution du modèle global. Une covariance
spatiale par axe n'est pas une covariance entre I/Co/Cg : ne pas les confondre.

N2b(b) reste à traiter séparément : un point isolé peut être un reflet ou un
détail réel ; pas de médiane aveugle ni de correction implicite d'un axe dont
le curseur est nul. Les contrôles de détails commencent dès cette étape,
sans attendre une surcorrection. Pas de nouvelle extension des grandes
échelles avant mesure de sa nécessité, pas de changement DCP ou d'activation
par défaut. Les nouvelles ROI et masques fixes de Claude serviront au rejeu
du futur filtre, pas à réinterpréter rétroactivement cette cartographie.

## 21. 29 octobre — correction d'interprétation : le grain des murs est réel

L'utilisateur précise que les murs d'escalade comportent du sable, de la colle
et une peinture granuleuse. Les régions appelées « aplats clairs » de7599 et
9613 mêlent donc texture réelle et bruit. Leur passe-haut plus élevé dans B
peut traduire en partie du détail conservé, et non une insuffisance du filtre.
La conclusion « B ne fait presque rien sur9613 » doit être limitée aux
mesures et ne vaut pas diagnostic de bruit sur ces régions.

**Conséquence : le réglage de N2b(a) et son raccord au filtre sont suspendus
sur cette base.** Le §20 et ses JSON gardent leurs valeurs numériques : ils
mesurent des dispersions de coefficients, pas une vérité de bruit. Une meilleure
prédiction du MAD sur les positions de contrôle ne justifie pas de supprimer
ce qui fait précisément la texture de la matière. Aucun coefficient ni
renforcement de force n'est intégré pour rendre le mur plus lisse.

Une photographie rapprochée décrite par l'utilisateur indique un grain fin,
serré, peu contrasté et approximativement achromatique. Cela fournit un
indice local en faveur d'un traitement du résidu coloré, sans garantir une
chroma fine nulle à chaque pixel : éclairage, reflets, contours et dématriçage
peuvent aussi produire des différences colorées. Pas de règle universelle
« toute variation de couleur du mur est du bruit ».

Claude prépare les références Lightroom « IPR Bruit » de7599 et9613 : onze
états, quart central à pleine résolution, recette suivie sur
`patch_validation_presets` (`186693b`). Ce sont des **comparaisons de rendu**,
pas une référence propre indépendante. Auditer profil, WB, corrections,
netteté et cadrage ; sélectionner les ROI dans le quart central commun après
orientation, sans redimensionnement qui changerait le grain. Distinguer
peinture texturée, surface réellement lisse, contours et noirs, avec masques
fixes. Les comparaisons sont en attente de ces exports.

En attendant, les points isolés et l'examen du noir de9613 sont les cibles
suivantes, sans traitement médian aveugle ni confusion entre détail ponctuel
et impulsion. Cette clarification ne modifie ni le prototype disponible ni
les bornes DCP. Les gardes de détail restent obligatoires dès N2b(b).

## 22. 29 octobre — audit des anciennes références Lightroom à 100/100

Claude retrouve les neutres et les exports100/100 de9608 et9890 et observe
un lissage nettement plus fort dans Lightroom. Avant d'utiliser ces mesures
pour étalonner les curseurs, Codex audite les XMP des quatre TIFF, sans
décoder les pixels. Les noms, dimensions, empreintes, réglages et différences
sont conservés dans
[audit-noise-lr-settings-v001-20261029T171447Z.json](../raw/audit-noise-lr-settings-v001-20261029T171447Z.json).

Les deux paires présentent la même différence de réglages :

| Déclaration XMP | Neutre LR | LR100/100 |
| --- | ---: | ---: |
| LuminanceSmoothing | 0 | 100 |
| ColorNoiseReduction | 0 | 100 |
| Sharpness | 0 | 0 |
| LensProfileEnable | 0 | 1 |
| AutoLateralCA | 0 | 1 |

Camera Neutral, balance As Shot5200/+5 et identité du profil restent cohérents,
mais **l'optique n'est pas constante** entre les états LR. Le changement de
géométrie, le rééchantillonnage des canaux et le vignettage peuvent contribuer
aux mesures ; une translation par corrélation de phase ne corrige pas une
éventuelle déformation optique. Les déclarations ne quantifient toutefois pas
l'amplitude effective des corrections. Cet audit ne réfute pas le lissage
visible : il empêche d'attribuer tous les facteurs d'atténuation au seul bruit.

Ces références restent des repères visuels de rendu fort, **pas des paires
contrôlées permettant de calibrer un multiplicateur de force**. Privilégier
la campagne à réglages constants déjà demandée, après audit, sans nouvelle
demande redondante à l'utilisateur sur cette seule découverte.

Dans CameraDenoise, force100 applique déjà la totalité du delta calculé par
le retrait Wiener. Si σ est sous-estimé ou la protection structurelle élevée,
ce delta reste petit. Le multiplier au-delà de1 est une extrapolation pouvant
créer des artefacts, pas une garantie de traitement plus pertinent. Il faut
séparer estimation du bruit, protection des détails et loi du curseur ; ne
pas prendre une surface « plastique » comme vérité de détail. Aucun changement
du filtre n'est apporté sur la base de ces comparaisons confondues.

## 23. 29 octobre — références contrôlées et prototype conditionnel N2b(a)

La campagne de Claude `ff83666`, conservée dans
`../raw/loi-force-bruit-v001-20261029T185322Z.json`, remplace les références
optiquement confondues du §22 pour étudier les forces. Elle couvre28 scènes,
les neutres et dix états LR par scène, Camera Neutral/As Shot, optiques
désactivées. Les masques issus du neutre LR restent fixes entre états/moteurs.
Les ratios ne prouvent pas l'architecture interne de Lightroom : ils décrivent
seulement la réponse de ces rendus. Notamment la hausse médiane ×1,27 de
chroma HP dans les noirs sous B luminance100 est un défaut à examiner,
pas une raison de traiter la texture des murs comme du bruit.

### Modèle essayé, sans changement de la loi des curseurs

Le module estime désormais variance et covariance par niveau de signal et
par échelle. Sur25 positions candidates, les médianes de quatre quadrants
éliminent les dérives trop grandes relativement à l'incertitude fine ; le
rejet de motifs corrélés reste actif. Trois plages équipopulées sont utilisées
si au moins neuf témoins couvrent un signal suffisamment distinct ; sinon
une seule covariance est ajustée. Dans chaque plage, le quart le plus calme
fournit le modèle robuste. Ce choix reste une heuristique, pas une séparation
prouvée entre texture faible et bruit physique.

Les covariances sont interpolées dans une table128 positions ; l'application
interpole les énergies normalisées et leurs espérances. Aux extrémités, le
dernier modèle est constant jusqu'au dernier signal témoin, élargi de son
incertitude ; au-delà le filtre s'abstient. Les signaux non finis sont refusés.
Cette frontière est plus large que les seuls centres des nœuds de l'étude
numérique du §20 : ne pas leur attribuer la même couverture. Les trois
échelles intensité/cinq chroma et les protections de structure restent en place.
Ni gain ISO empirique, ni coefficient supplémentaire de curseur, ni nouvelle
borne DCP ne sont introduits. Les axes restent ici compensés de la BdB pour
isoler(a) ; leur remplacement est la prochaine comparaison distincte.

### Contrats et défaut effectivement corrigé

Preuve négative conservée : `../raw/camera-denoise-n2-v001-20261029T191250Z.json`.
Le test moteur LCP détecte qu'une abstention globale retournait le tampon
encore partagé ; la correction optique suivante pouvait modifier la source.
Le module détache désormais le tampon sur tout succès de force non nulle,
même sans bruit mesuré. Force0 reste sans allocation. Aucun test LCP n'a
été affaibli pour faire passer ce contrôle.

Après correction, `../raw/camera-denoise-n2-v001-20261029T191951Z.json`
consigne59 contrôles natifs réussis, le test moteur optique et six suites CTest
réussies. Trois bandes synthétiques à variances croissantes sont bien traitées ;
un escalier propre est conservé et la translation DC diffère de moins de
7,1e-7. Cela ne qualifie pas la conservation du sable réel. Mesures isolées :
0,654 s sur2,8 Mpx, 4,846 s sur24 Mpx, pic740237312 octets (processus de test,
pas RAM de l'application). Huit threads maximum, tous rejoints.

### Trois témoins réels : bilan mitigé, pas une validation

`../raw/camera-denoise-n2b-v001-20261029T191319Z.json` conserve15 états de
7599 (R7),9539 (R6 III),7958 (60D), sans TIFF intermédiaire. SHA sources,
géométrie commune, masques et métriques B0 rejoignent exactement la référence.
Les25 autres scènes sont réservées pour un candidat figé, réserve prospective
seulement puisque ce corpus avait déjà servi auparavant.

À luminance100, dans le masque clair de7599, la dispersion HP de luminance
passe de6,911 à3,967, mais la chroma monte de14,565 à16,169. À100/100 elle
passe de6,450 à3,105 et la chroma de11,500 à6,388. Dans le noir,100/100
est moins réducteur qu'avant (luma1,136→2,292 ; chroma1,358→2,958).
Sur9539, le masque moyen à luminance100 passe de3,931 à1,865 en luma,
mais de5,000 à6,737 en chroma. Le60D change peu. Toutes ces unités sont
des écarts-types HP sur RGB encodé échelonné0–255, pas des unités de bruit
capteur ; les masques contiennent aussi de la matière.

Les requêtes pleines images chaudes prennent environ8,4–9,1 s en luminance
seule et13,3–14,0 s en chroma sur les deux CR3 ; le60D4,7/7,4 s. Ce sont
des requêtes comprenant le rendu, pas le seul filtre ; les ouvertures froides
prennent32–35 s. Le serveur de cette étude précède la correction d'alias et
les derniers gardes numériques : optiques désactivées, leurs sources exactes
sont incluses. Le binaire final est qualifié séparément par les contrats ;
ne pas présenter ces preuves comme une seule identité d'exécutable.

**Décision :** garder ce jalon comme candidat expérimental, sans promotion.
L'amélioration des métriques de luminance n'autorise pas à lisser le mur.
Le diagnostic séparé de Claude (`ef43917`, journal commun) montre qu'un
delta neutre avant compensation BdB devient coloré après réapplication des
gains. La décomposition entièrement dans le RGB caméra déjà balancé est la
prochaine piste cohérente ; remplacer seulement le delta de luminance rend
les axes incohérents. Qualifier cette piste séparément avant toute hausse
de force ou traitement des points isolés.

## 24. 29 octobre — V1, correction des axes caméra déjà balancés

Le diagnostic `ef43917` de Claude distingue deux choses : l'entrée contient
déjà les gains de BdB, tandis que le module les annulait pour travailler dans
des unités proches du capteur. Une correction d'intensité commune dans ces
unités redevenait proportionnelle aux gains après reconstruction : elle n'était
donc plus neutre dans le RGB caméra fourni au DCP. Cela explique un mécanisme
possible des points colorés, sans prouver que toutes leurs causes sont levées.

Après accord au journal, V1 est isolé sur le modèle de `7e367b0` **sans le
candidat conditionnel(a)**. Celui-ci reste conservé dans `0941dc4` et ses JSON ;
pas de double implémentation ni d'option legacy. L'estimation et la correction
opèrent maintenant toutes deux sur le RGB caméra déjà balancé. Le delta de
luminance est identique dans R/G/B ; la couleur seule conserve l'intensité
caméra0,25R+0,5G+0,25B. Ce ne sont pas des garanties sur la luma/chroma sRGB
après DCP, courbes et bornes. L'ISO n'introduit aucune loi de force.

`Context::gains`, sa validation et le test d'équivariance à une compensation
BdB ont été supprimés. Un nouveau témoin applique des gains2/1/3 au champ
avant le filtre, exige une correction effective, puis contrôle deltaR=deltaG=
deltaB en intensité seule et deltaI=0 en couleur seule. L'écart admissible est
5e-7. L'abstention détache toujours la source pour les optiques en aval.

Preuve `../raw/camera-denoise-n2-v001-20261029T192417Z.json` : **56 contrôles
natifs et six suites réussis**. Mesure du module synthétique :465 ms sur2,8 Mpx,
3223 ms sur24 Mpx, pic685129728 octets du processus, huit threads maximum.
Ces différences ne prouvent pas un gain de vitesse de V1 : le modèle(a) est
aussi retiré et aucune campagne chronométrique répétée n'est faite.

**Raccordement en attente :** Claude retire le remplissage du champ supprimé
dans `imageeditor.cpp` et dans son test optique lors de l'intégration de ce
jalon. Ces fichiers moteur qui lui sont attribués ne sont pas retouchés ici.
Le test profil complet n'est donc pas annoncé réussi sur ce nouveau contrat ;
le lanceur utilise explicitement `--module-only`, refuse un ancien outil profil
et écrit `engine_integration=pending`. Aucun ancien binaire ne remplace cette
qualification manquante.

La campagne réelle V1 avec les masques LR figés est confiée à Claude après ce
raccordement. Son essai exploratoire donne une piste favorable, pas encore une
preuve sur ce code. Ensuite seulement rejouer(a)+V1 dans un jalon distinct,
sans confondre changement d'axes, loi des curseurs et adaptation au signal.
Les temps13–14 s du §23 concernaient la pleine définition, non l'aperçu.
Aucune nouvelle image ni action utilisateur nécessaire à ce stade.

## 25. 29 octobre — orientation utilisateur pour la commande globale (b)

Orientation transmise par Claude et retenue : une seule barre principale
**« Réduction du bruit »**, avec un sous-menu **« Avancé »** replié pour
affiner séparément luminance et couleur. La commande sera à **0 par défaut**.
Lightroom reste un repère de réponse
et de rendu, pas une cible à recopier. Cette décision ne modifie ni(a), ni V1,
ni les bornes DCP, et ne vaut pas implémentation de l'interface.

Le contrat `CameraDenoise::Settings{luminance, chrominance}` reste inchangé.
La future commande globale sera traduite en ces deux forces par une loi en
C++, pas par des calculs photographiques dans le QML. Aucune pondération,
proportion fixe ou nouvelle courbe de force n'est choisie ici ; les mesures
contrôlées et le contrôle visuel doivent guider ce choix après qualification
des axes balancés. Définir aussi explicitement comment les réglages avancés
interagissent avec un nouveau déplacement du curseur principal, afin d'éviter
des valeurs masquées ou des corrections appliquées deux fois.

Avant raccordement IHM : tests des bornes0–100, finitude, déterminisme,
identité lorsque les deux forces effectives valent0, et réponse progressive
sur des champs de bruit connus. La baisse d'un indicateur sur une photo
texturée ne suffit pas à valider cette loi. Le panneau et le sous-menu seront
pris en charge dans le chantier IHM dédié ; aucune modification moteur ou
QML supplémentaire n'est faite pour cette seule orientation.

## 26. 29 octobre — force souhaitée et comparaison signé/non signé à isoler

Retour utilisateur transmis par Claude : viser un lissage de luminance de
l'ordre du rendu LR70–75, sans rechercher le flou et la perte de texture
observés au-delà. Ce repère visuel ne fixe pas un coefficient numérique
IPR, ni une équivalence entre positions des curseurs. La conservation des
contours et de la matière fait partie de l'objectif. Aucun nouveau poids
de la future commande globale n'est fixé avant comparaison.

Autre observation : sur7599 le débruitage paraît meilleur avec la BdB
automatique ; des points rouges/verts restent avec la BdB boîtier et le
développement signé. L'inspection de `RawDecoder::developedByIpr` confirme
que la BdB automatique exclut le développement IPR : elle passe par
`dcraw_process`, sa sortie bitmap16 bits, puis sa conversion en flottants.
Elle modifie aussi les gains, et peut modifier la résolution de la calibration
dont le contexte contient `cameraWhiteBalance`. Cette observation n'isole
donc pas le seul rôle des valeurs négatives. Le magenta/vert après rendu
reste une hypothèse, pas une causalité établie pour tous les points.

Claude prend en charge l'essai A non signé + CameraDenoise contre B signé,
avec **la même BdB boîtier**, sur7599/9539, après sa campagne V1. Préserver
neutres A0/B0 et états traités, paramètres, calibration réellement résolue,
masques fixes, identité de l'exécutable et des sources. Attention : A/B
changent aussi la grille numérique, les bornes avant RCD et son traitement
signé ; une différence A/B seule ne prouvera pas que le DCP est la cause.

Pour isoler spécifiquement la frontière après débruitage, une sonde distincte
peut reprendre **le même tampon B traité** et comparer son rendu direct à un
rendu où seuls les négatifs ont été remplacés par0, juste avant les mêmes
étapes optiques/profil. Consigner aussi les valeurs négatives par canal et
niveau de signal avant/après filtre, et les pixels concernés après rendu.
Cette sonde introduit un biais de noir : elle n'est ni une correction approuvée,
ni une justification pour borner en amont du débruitage. Ne pas changer le
filtre ou la force au milieu de cette comparaison. Aucun nouvel essai n'est
lancé ici en parallèle de la campagne confiée à Claude.

## 27. 29 octobre — V1 confirmé, sondes BdB et combinaison(a)+V1

### Référence V1 et relecture des sondes de chemins

Claude livre `75d8701` :
`../raw/loi-force-bruit-b-v1-v001-20261029T200937Z.json` reprend les28 scènes
et masques de la campagne initiale, un seul exécutable haché et un témoin0/0
inchangé. La hausse médiane de chroma HP à luminance100 passe de×1,27 à×0,99
dans les noirs ; la couleur seule agit davantage, sans bénéfice majeur dans
les aplats clairs. Cela soutient les axes balancés, pas encore le modèle(a).

Les sondes `8fd363f`,
`../raw/essai-debruitage-chemins-bdb-v001-20261029T201816Z.json`, comparent7599
et9539. Le chemin non signé réduit certains points noirs de7599 mais relève
la luma moyenne du masque sombre de12,6 à18,5 à100/100 ; ce n'est pas une
amélioration sans contrepartie. La BdB automatique livre une image plus sombre
et froide et désactive la calibration concernée : les observations ne démontrent
pas un meilleur débruitage. L'identité de l'exécutable autoWB est déclarée
« not recorded » ; ce volet reste une sonde de diagnostic, pas une qualification
reproductible du chemin. Aucun abandon du signal signé ni nouvelle borne DCP.

### Candidat conditionnel dans les axes balancés

Le raccordement moteur de Claude `dec3c6f` est intégré localement au banc.
Le modèle conditionnel du §23 est ensuite réintroduit **sans annulation de BdB** :
mêmes témoins admissibles, trois plages si support suffisant, covariance
interpolée et abstention hors domaine. La physique capteur n'est pas recalibrée.
`Context::gains` reste supprimé ; la luminance seule ajoute un delta commun
R/G/B. Le garde d'alias et l'intégrité de la source sont conservés.
Ce lot ne change ni les forces, ni les taches basse fréquence(c), ni la
future loi maître(b). L'ordre convenu est(a)+V1, puis(c), puis(b).

Première preuve native :
`../raw/camera-denoise-n2-v001-20261029T202017Z.json`, 61 contrôles natifs,
test moteur optique et six suites réussis. Trois nouveaux tests Python
vérifient en plus le rattachement de la référence V1 à la bonne campagne
figée ; ils sont désormais déclarés dans une septième suite CTest.
Mesures initiales du module :718 ms sur2,8 Mpx et5340 ms sur24 Mpx,
pic733028352 octets sur le champ synthétique. Pas une mesure de l'aperçu
interactif, ni une campagne répétée de performance.

La comparaison réelle emploie les mêmes7599/9539/7958 de développement et
réutilise **B-V1**, pas l'ancien B aux axes compensés. Le lanceur exige le
SHA de la référence commune, ceux des sources et des masques, puis les mêmes
métriques B0 ; un écart arrête la campagne. Les25 autres scènes restent une
réserve prospective pour un candidat figé, pas un corpus jamais observé.
### Résultats de la combinaison figée `15a4cc2`

`../raw/camera-denoise-n2b-v001-20261029T202044Z.json` conserve15 rendus en
mémoire. Les trois neutres B0 et les empreintes des masques concordent avec
B-V1. Le SHA du module et celui du serveur rejoignent le candidat final :
pas de changement d'algorithme entre mesures et jalon. Les sources et le
binaire sont identifiés dans la preuve ; aucun TIFF de mesure généré.

Comparaison à B-V1, mêmes masques et même état :

| Scène / masque / état lum-col | Luma HP V1 → (a)+V1 | Chroma HP V1 → (a)+V1 |
| --- | ---: | ---: |
| 7599 clair,100-0 | 6,699 →3,495 | 14,164 →12,198 |
| 7599 clair,100-100 | 6,534 →3,138 | 11,661 →6,288 |
| 7599 noir,100-100 | 1,237 →2,389 | 1,524 →3,307 |
| 9539 moyen,100-0 | 3,916 →1,812 | 4,462 →4,452 |
| 9539 moyen,100-100 | 3,882 →1,693 | 3,282 →1,718 |

Unités : écart-type haute fréquence, luma Rec.709 pondérée et différences
R−G/B−G, sur RGB encodé échelonné0–255. Ce ne sont ni des mesures de bruit
capteur seul ni une vérité propre. Le masque de contours9539 baisse aussi
de5,187 à4,057 en luma à100-0 : examiner visuellement la conservation des
détails avant de l'interpréter comme du bruit retiré. Le60D7958 change peu
(clair100-100 : luma1,188→1,223, chroma1,415→1,452).

La réduction plus forte du masque clair de7599 ne réintroduit pas la hausse
de chroma à luminance100 observée avant V1. En revanche, le noir7599 à100/100
est sensiblement moins réduit qu'avec V1 seul, et couleur100 seule y laisse
5,112 au lieu de4,224. Il n'y a donc **pas de domination uniforme** de V1
par(a)+V1. Ne pas augmenter arbitrairement la force pour masquer ces écarts.
Les taches basse fréquence ne sont pas qualifiées par le seul HP sigma2 ;
leur traitement(c) reste séparé.

Requêtes chaudes pleines images CR3 :8,2–9,2 s en luminance,12,6–14,1 s en
chroma, rendu compris ; ouvertures froides32–36,5 s. Sur60D :4,7/7,4 s.
Qualification finale `../raw/camera-denoise-n2-v001-20261029T202504Z.json` :
61 contrôles natifs, **sept suites CTest** et test moteur optique réussis.
Ce second enregistrement inclut la nouvelle suite de référence, sans changement
de module. Mesures synthétiques finales :730 ms sur2,8 Mpx,5402 ms sur24 Mpx,
pic731951104 octets ; elles n'établissent pas la latence réelle du proxy IHM.

**Décision :** jalon disponible pour intégration expérimentale et revue,
pas remplacement validé. Réserve25 scènes non rejouée sur cette combinaison ;
faire la qualification du candidat figé sans ajuster ses seuils au fil des
résultats. Contrôle visuel ciblé :7599 noir et mur clair,9539 contours et
matière ; conserver V1 comme référence de campagne historique, pas comme
double chemin legacy dans le code. Ni nouvelle image LR ni changement DCP
nécessaires pour cette comparaison. Aucun push/dev/tag.

## 28. 29 octobre — régression sombre sur28 scènes et audit du modèle réel

Claude livre `dd674b0`,
`../raw/loi-force-bruit-b-a-v1-v001-20261029T212226Z.json` : combinaison(a)+V1
mesurée sans réglage sur les28 scènes. À100/100,10 scènes sur25 sont moins
réduites de plus de3 % en luma dans le noir,11 en chroma. Les25 scènes
initialement déclarées réservées ont donc maintenant été consommées par cet
essai ; ne plus les décrire comme un jeu intact pour le prochain candidat.
La piste reste utile sur le mur, mais pas promue : points isolés colorés
visibles sur7599, et résidus basse fréquence distincts des points.

Avant repli, ajout d'un observateur optionnel du modèle réel dans Context :
appel seulement sur le thread appelant avant filtrage, aucune modification
des pixels quand il est absent.64 contrôles natifs passent, dont identité
binaire avec/sans observateur et annulation atomique sur exception du callback.
La sonde `camera_denoise_leakage` est réparée après suppression des gains V1 :
son analyse s'effectue maintenant dans le RGB caméra déjà balancé, pas dans
les anciens axes capteur. Les anciennes preuves gardent leurs axes historiques.
Preuve des contrats de cette instrumentation :
`../raw/camera-denoise-n2-v001-20261029T213232Z.json`,64 contrôles natifs,
sept suites et test optique réussis ; aucun repli encore appliqué.

Audit `../raw/camera-denoise-signal-survey-v001-20261029T212749Z.json` :
modèles effectivement estimés sur l'image entière et81 ROI par source,
7599/7958/9539/9613. Le filtre est exécuté pour observer les modèles ; sa
sortie est jetée, aucune image exportée. Sur7599,23 témoins plats sur25 ;
au premier niveau, σ intensité sombre0,01258 contre global0,01692 et σ Co
0,00300 contre0,00381. Le modèle global est centré vers le signal0,08191,
le sombre vers0,02991 : **la différence ne prouve pas une sous-estimation
physique**, car le signal diffère et le global mélange des niveaux.

Sur les12 ROI les plus sombres de la grille (49152 pixels sondés par
échelle), aucun guide n'est sous la borne basse, sur chacune des quatre
scènes ;18 pixels sont au-dessus de la borne haute sur7599. Ce n'est pas
un masque sombre plein cadre, mais cela ne soutient pas l'abstention sous
le noir comme explication principale de cette régression. Les20 modèles
de l'audit ont un support admissible. Les écarts sombre/global apparaissent
aussi aux échelles chroma plus larges, sans les confondre avec une preuve
sur les taches après rendu.

Prochain essai : plancher de variance au premier nœud, raccordé par
l'interpolation existante vers le nœud suivant. Garder les autres nœuds,
les forces, le domaine couvert et le DCP inchangés. Ce garde-fou est un
compromis empirique par rapport à V1, pas une extrapolation validée du bruit
capteur. Il devra améliorer le noir sans effacer la matière ni annuler les
gains clairs ; aucune promotion avant mesure et revue.

## 29. 29 octobre — essai du plancher de variance au nœud sombre

Un seul modèle de filtre reste compilé : le candidat conditionnel balancé
avec garde-fou sombre. Ce n'est pas un retour à un second filtre global ni
une option legacy. Le global est une estimation statistique de secours issue
des mêmes25 positions non rejetées comme cohérentes, avec le quart le plus
calme ; ses niveaux mélangés ne constituent pas une vérité de bruit physique.

Avec au moins deux nœuds conditionnels distincts, pour chaque axe c du
premier nœud : variance(c)=max(variance_sombre(c),variance_globale(c)). Les
covariances hors diagonale sont redimensionnées pour conserver la corrélation
des témoins sombres, sans importer la corrélation d'une autre population de
signal. Une variance initialement nulle reçoit un terme diagonal, sans
inventer de corrélation. La régularisation de covariance reste inchangée.
Le raccord vers le deuxième nœud utilise l'interpolation continue existante ;
les autres nœuds ne sont pas relevés. La table128 positions conserve son
approximation numérique, et le domaine couvert/les abstentions ne changent pas.

Ni σ=valeur fixe, ni seuil de noir en sRGB, ni amplification de curseur ne
sont ajoutés. Le seul nœud d'un modèle sans bandes distinctes reste inchangé.
Une absence de témoins admissibles ne provoque pas un repli global hors
domaine : elle reste une abstention. Le risque de lisser trop les vrais
détails sombres reste à examiner ; le plancher restaure un niveau empirique
V1, pas nécessairement le σ réel du capteur.

Preuve des contrats `../raw/camera-denoise-n2-v001-20261029T213441Z.json` :
65 contrôles natifs, sept suites CTest et test optique réussis. Le nouveau
témoin synthétique vérifie le plancher sur les cinq échelles tout en gardant
le contrôle de réduction du bruit aux trois luminosités et les escaliers
propres. L'observateur reste sans effet binaire sur la sortie, avec callback
sur le thread appelant uniquement et échec atomique sur exception.

La comparaison réelle réutilise les masques, sources et neutres de référence
sur7599/9539/7958, contre B-V1 et contre B_a_V1 de `dd674b0`, sans regénérer
les anciens états. `--prior-reference` vérifie le même SHA de campagne avant
d'utiliser cette seconde référence. Ces scènes ne sont plus des données
réservées ; la prochaine campagne devra être déclarée comme réévaluation,
pas comme validation sur un jeu intact.

### Résultat du candidat figé `fcd95a2`

Preuve `../raw/camera-denoise-n2b-v001-20261029T213552Z.json` :15 rendus,
trois scènes, SHA du module final et de l'exécutable, masques et neutres B0
concordants. Même définition HP/unités que le §27 ; données par scène, état
et masque préservées. Aucun seuil modifié après lecture de cette campagne.

| Masque / état lum-col | V1 seul luma/chroma | (a)+V1 avant plancher | Avec plancher |
| --- | ---: | ---: | ---: |
| 7599 noir100-100 | 1,237 /1,524 | 2,389 /3,307 | 1,010 /1,456 |
| 7599 noir0-100 | 10,710 /4,224 | 10,715 /5,112 | 10,715 /4,236 |
| 7599 clair100-100 | 6,534 /11,661 | 3,138 /6,288 | 3,074 /6,218 |
| 9539 noir100-100 | 1,428 /1,056 | 1,027 /0,859 | 0,721 /0,620 |

Le garde-fou corrige la régression de dispersion sombre du témoin7599,
sans annuler le gain dans le masque clair. Le60D reste presque inchangé.
La moyenne de luma dans le noir7599 à100/100 vaut12,866, contre12,631 en
V1 et12,500 avant plancher : ne pas assimiler ce résultat à une identité
de luminosité après DCP, même si le filtre conserve le DC caméra. Le masque
clair garde202,515 contre202,544 avant plancher. Les nœuds clairs ne sont
pas modifiés, mais l'interpolation des nœuds, les voisinages et le rendu
peuvent changer des pixels de ces masques ; pas d'identité régionale promise.

La suppression des points isolés ne découle pas de ces seules dispersions,
et les textures peuvent être lissées. **Décision : candidat d'essai transmis
à Claude, campagne28 scènes et contrôle visuel avant intégration validée.**
Ne pas qualifier cette campagne future de nouveau jeu réservé. Aucun
changement(c) des taches basse fréquence, des forces ou du DCP dans ce lot.
Les requêtes CR3 chaudes pleines définition prennent8,2–8,9 s en luminance
et12,6–13,9 s en couleur, rendu compris ; pas un budget de proxy interactif.

## 30. 29 octobre — candidat(c1), ne pas protéger du bruit couleur avec la texture I

### Cible et hypothèse distincte du plancher sombre

Claude lance la chaîne de nuit sur des exécutables copiés/hachés :28 scènes
puis76 RAW hors protocole, versions V1/(a)+V1/plancher. Ce travail reste
indépendant de(c1), préparé ici sur la même base `fcd95a2`. Aucun rendu RAW
concurrent ni benchmark24 Mpx ajouté par Codex pendant cette chaîne.

Le filtre utilisait le maximum entre protection individuelle et protection
jointe des trois axes I/Co/Cg pour chaque canal. Une texture forte en I pouvait
donc protéger un coefficient Co/Cg contenant seulement du bruit. L'information
« la structure est réelle » ne suffit pas à prouver qu'elle est colorée ; les
murs à relief réel mais teinte approximativement uniforme sont le cas visé.
Cette hypothèse n'est pas une affirmation que toute variation de chroma du
mur serait du bruit ni que les filaments visibles ont une cause unique.

### Contrôle négatif puis changement limité

Avant modification du filtre, un nouveau témoin définit une vérité connue :
texture I achromatique, avec du bruit Co/Cg additif fin et des champs colorés
simulés corrélés par flou gaussien sigma3/6 px. La scène contient des plages
stationnaires pour l'estimation et une région texturée pour l'évaluation.
Le plancher de base garde0,997698 de l'erreur quadratique chroma initiale dans
cette région. Le nouveau test exigeait moins de0,5 **avant** le changement :
il échoue comme prévu dans
`../raw/camera-denoise-n2-v001-20261029T214914Z.json` (69/70 contrôles).
Cette preuve négative est conservée, pas effacée ou renommée en succès.

Dans(c1), I garde exactement la formule de protection jointe3D. Pour Co/Cg,
la protection partagée utilise seulement leurs énergies normalisées et leurs
espérances locales déjà disponibles :

```text
E_chroma = E_Co + E_Cg
N_chroma = N_Co + N_Cg
protection_chroma = clamp(1 - N_chroma / max(E_chroma, N_chroma), 0, 1)
protection_finale_c = max(protection_individuelle_c, protection_chroma)
```

Si l'espérance est nulle, cette protection partagée vaut0 ; les autres gardes
de support restent actifs. L'espérance de la somme d'énergies est additive
même lorsque Co/Cg sont corrélés : c'est un contrôle de second moment, **pas
une probabilité chi-deux ni une hypothèse d'indépendance des canaux**. La
covariance3D estimée reste utilisée par la protection I ; le veto chroma
utilise les deux énergies marginales, sans nouvelle inversion de matrice.

Pas de nouveau flou, médiane, échelle, σ, plancher, seuil de noir ou force.
La loi des curseurs(b) ne change pas. L'intensité caméra est conservée en
couleur seule, pas nécessairement la luma sRGB après les non-linéarités DCP.
La version en cours de mesure par Claude n'est pas modifiée.

### Qualification disponible et limites

Preuve `../raw/camera-denoise-n2-v001-20261029T215306Z.json` :70 contrôles
natifs, sept suites et test moteur optique réussis. Dans le témoin :

- Erreur quadratique chroma restante :0,331894 de l'entrée, contre0,997698
  avant(c1) ; il s'agit d'une MSE, pas d'un écart-type ni d'une note visuelle.
- Partie basse fréquence après passe-bas sigma4 : MSE0,433354 de celle de
  l'entrée. Cette métrique n'était pas mesurée dans le contrôle négatif ; ne
  pas inventer de comparaison avant/après pour ce seul indicateur.
- Texture I de l'entrée conservée à moins de5e-7 en couleur seule.
- Contrôle distinct de vrais détails couleur : contraste du contour0,079467
  pour0,08 attendu ; ligne couleur d'un pixel0,049973 pour0,05 attendu.
  Cela couvre des détails forts, **pas les textures colorées faibles** proches
  du bruit, ni toutes les fréquences, teintes, expositions ou boîtiers.
- Annulation, finitude, alias de source, bornes de force, répétabilité, DC,
  modèle sombre et témoins synthétiques précédents restent contrôlés.

Lanceur utilisé avec `--skip-benchmarks` : aucune mesure de performance n'est
annoncée et aucun ancien temps n'est attribué à(c1). Compilation séquentielle
et petits tests seulement pendant la chaîne de Claude. Une relecture de la
[publication Guided Image Filtering](https://people.csail.mit.edu/kaiming/eccv10/index.html)
rappelle le rôle du guidage et du transfert de structure ;(c1) n'en implémente
ni ne copie l'algorithme, c'est une variante IPR du veto énergétique existant.

**Décision : candidat séparé pour la prochaine mesure de Claude**, sans
publication ni promotion. Vérifier masques fixes, métriques HP ET basses
fréquences, points isolés, vraies prises colorées, détails de faible contraste,
aperçu et pleine définition. Une amélioration synthétique ne prouve pas la
disparition des taches réelles. Ne pas mélanger(c1) et(b) avant ce bilan.

## 31. Repli global hors support conditionnel — 30 octobre 2026

### 31.1. Résultat réel déclencheur et contrôles négatifs

Claude a terminé la campagne du plancher sombre, preuves intégrées localement
depuis `d3279aa` :
[28 références](../raw/loi-force-bruit-b-plancher-v001-20261030T021115Z.json) et
[76 RAW hors protocole](../raw/debruitage-versions-hors-protocole-v001-20261030T021115Z.json).
Le plancher améliore notamment7599/9539 et préserve les témoins0/0 des104
sources. Il ne résout pas l'abstention de certaines bandes où V1 débruite :
neuf références et treize scènes supplémentaires. Le sondage limité de§28
ne permettait donc pas d'écarter cette cause sur tout le corpus.

Références signalées :6V1A0007,6V1A0043,6V1A0064,6V1A9141,6V1A9153,
IMG_3135,IMG_6248,_MG_7580,_MG_7840. Parmi les autres :_MG_7906 etIMG_8023.
La liste exhaustive et les mesures restent dans les preuves et le journal
commun ; ne pas conclure que tous les états de ces scènes sont inchangés.

Deux cas synthétiques reproduisent un défaut de support : rampe sans témoins
plats admissibles et petite plage sombre hors domaine entre les positions
d'estimation. Avant correction, leur MSE chroma restante vaut1,0 ;
[preuve négative](../raw/camera-denoise-n2-v001-20261030T021542Z.json).
Ces constructions isolent des mécanismes possibles, sans établir la cause
individuelle des22 scènes réelles. Bruit additif simulé, pas oracle CFA physique.

### 31.2. Politique du candidat

- Dans le support conditionnel : mêmes nœuds et même interpolation.
- Sans modèle conditionnel : covariance etσ globaux de V1, si disponibles.
- Hors domaine : transition extérieure du modèle de bord vers le global,
  sur l'incertitude d'intensité déjà estimée ; pas de seuil de signal ajouté.
- Sans témoins globaux admissibles : abstention conservée. Unσ nul valide
  n'est pas remplacé arbitrairement par un bruit inventé.

Le global utilise les témoins non cohérents et leur quart silencieux comme
V1, sans la sélection supplémentaire d'aplats conditionnels. Ce n'est ni une
nouvelle calibration physique ni un second filtre legacy. L'observateur
distingue `supported` et `globalFallbackAvailable`.

Les énergies locales conditionnelles et de repli sont calculées séparément
pour ne pas mélanger leurs normalisations dans le veto des pixels supportés.
Le rééquilibrage DC final reste global : **pas de promesse d'identité pixel
par pixel dans les régions déjà supportées**, ni de preuve d'absence de
raccord visible. Les tuiles mixtes nécessitent deux buffers supplémentaires
et un calcul local supplémentaire ; temps et mémoire restent à qualifier.

### 31.3. Qualification et limites

[Preuve finale](../raw/camera-denoise-n2-v001-20261030T022403Z.json) :77 contrôles
natifs, sept suites CTest et intégration optique réussis, benchmarks non lancés.
La MSE chroma restante passe de1 à0,0292 sur la rampe et0,0418 dans la plage
sombre hors support. Les contrôles(c1) de texture achromatique, bord coloré
et ligne fine forte restent satisfaits. Ces ratios ne mesurent pas un gain
visuel ni le comportement de textures colorées faibles.

Une [preuve intermédiaire](../raw/camera-denoise-n2-v001-20261030T022318Z.json)
conserve l'échec d'une assertion trop large : un damier cohérent ne rejette
pas nécessairement les témoins de toutes les échelles, les coefficients
grossiers nuls pouvant être admissibles. L'assertion a été corrigée par
échelle : aucun repli inventé là où tous les témoins sont rejetés. Le filtre
n'a pas été modifié pour contourner cette assertion.

**Décision : candidat séparé au-dessus de(c1), à mesurer par Claude.**
Comparer(c1) seul à(c1)+repli sur les22 scènes, puis contrôler7599, les murs,
les détails colorés, les transitions, l'aperçu et la pleine définition.
Mesurer ressources et latence avant promotion. Aucun rendu RAW concurrent
n'est lancé ici pendant sa campagne ; force, DCP, commande maître(b) et
paramètres de profil inchangés. Pas de push, fusion versdev ou tag.

## Conclusion actuelle et fiche de reprise

- Travail local dans `patch_raw_bench`, builds isolés ; pas de modification des
  fichiers moteur attribués à Claude, pas de push/dev/tag.
- Socle RAW clos sur son périmètre : X1/X2 exacts sur30 sources, C3 à60/60,
  F1 flottant et garde de finitude qualifiés (§§1–16). Les preuves X4 pré-F1
  restent historiques ; ne pas les interpréter comme limites du signal actuel.
- Le prototype signé pré-LCP/DCP reste expérimental et désactivé par défaut.
  V1 à axes déjà balancés est intégré par Claude et mesuré sur28 scènes (§27).
- Le premier(a)+V1 améliore certaines plages mais régresse dans les noirs :
  pas de promotion après la campagne28 scènes (§28). Cette campagne a consommé
  les25 scènes auparavant réservées ; aucun jeu intact n'est revendiqué ensuite.
- L'audit du modèle réel confirme des σ sombres inférieurs au global, sans
  démontrer une sous-estimation physique. L'abstention basse n'explique pas le
  sondage des quatre scènes. Le plancher sombre fcd95a2 (§29) restaure le noir
  sur les trois témoins et garde le gain clair. La campagne28+76 de Claude
  confirme des gains mais révèle des abstentions sur22 scènes (§31).
- Les murs ont une matière réelle ; ne pas confondre baisse de HP et bruit
  retiré. Employer les références LR contrôlées, pas les anciennes paires
  optiquement confondues (§§21–23). Pas de retour au non signé justifié par
  les sondes BdB automatique, qui changent aussi rendu et calibration (§27).
- Suite convenue : corriger/qualifier(a)+V1, puis(c) taches basse fréquence et
  points isolés, puis(b) commande globale à0 avec réglages avancés (§§25–29).
  LR70–75 est un repère de lissage, pas une cible d'imitation ni de flou.
- DCP et force inchangés ; harmonisation des domaines couleur différée.
  Mesures par scène et versions dans les JSON ; revue visuelle, ressources
  applicatives et validation multi-boîtiers restent des qualifications distinctes.
- Candidat(c1) prêt au niveau synthétique (§30) : la texture I ne bloque plus
  la correction de chroma par le veto partagé.70 contrôles/sept suites/optique
  passent, pas encore de rendu réel ; comparaison à confier à Claude après
  sa chaîne figée du plancher sombre. Pas de modification(b) ni de promotion.
- Repli global préparé au-dessus de(c1) (§31) :77 contrôles, sept suites et
  intégration optique passent. Qualification réelle des22 scènes et coût
  supplémentaire attendus ; aucune conclusion de correction réelle anticipée.
