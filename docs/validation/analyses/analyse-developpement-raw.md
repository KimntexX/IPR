# Séparation du développement RAW — rapport évolutif

## Bilan actuel — 31 octobre 2026

**Décision utilisateur (§54) : stabiliser le débruitage actuel et suspendre
les pistes algorithmiques incrémentales.** Préparer une voie neuronale locale
pour la bêta, à qualifier d'abord sur la machine de l'utilisateur. Le moteur
de base reste accessible aux machines moins adaptées ; le mode avancé pourra
y être limité ou indisponible. Aucun modèle neuronal ni profil matériel
n'est qualifié par cette décision, aucune nouvelle campagne lancée.

Préparation B4 engagée ensuite (§55) : inventaire léger et étude des modèles,
sans inférence ni changement du moteur ; les essais alpha gardent la priorité.
Précision suivante (§56) : propres poids sur architecture éprouvée, besoins de
prises appariées et rafales lunaires candidates ; pas d'entraînement lancé.
Premier banc technique (§57) :14 contrôles synthétiques passés sur petit U-Net
CPU, sans gain photo ni mise à jour du moteur revendiqués.
Audit suivant (§58) :115 métadonnées CR3 lunaires, deux groupes d'exposition,
trois JPEG embarqués et preuves de mouvement anciennes réutilisées ; pas de
référence propre créée, réserve fermée.
Préparation suivante (§59) :14 tests de données et un pilote8/4/4 synthétique,
sans bénéfice de débruitage utile démontré ; pas de photo utilisée pour apprendre.
Nouvelle demande ciblée (§60) : comparer le placement du débruitage couleur
signé/non signé du jalon de Claude. Banc compilé et références auditées,
premiers arrêts mémoire puis provenance Qt corrigés ; identité native à zéro
vérifiée sur 7599 et campagne relancée, sans conclusion photographique disponible.

Dernier jalon (§53) : ablation de routage sur les64entrées du§52,640parités
avec les anciennes sorties. Plafond diagonal local : détail préservé mais
aucun gain médian de MSE ; remplacement du seul secours : entrées bruitées
quasiment inchangées et stress propre encore en échec. Aucune variante promue.
La simple conservation des courbes conditionnelles ne suffit pas : prochaine
cible = budget réellement dépendant du signal local. Audit des threads :
adaptation matérielle déjà présente, plafond8 et réserve d'un thread logique ;
aucun changement de parallélisme de production pendant cette étude.

Jalon précédent (§52) : budget multi-noirs confronté au filtre réel sur64cas.
Erreur RGB médiane réduite de28% (spectral) ou34% (banque simple) sur le
rejeu de noirs réels avec signal simulé ; aucune régression MSE sur ces cas.
Cependant, échec du veto de détail sur le stress propre à budget non nul.
Ce stress n'est pas une perte de détail photo mesurée : la substitution
globale supprime aussi le routage conditionnel. Pas de promotion ; prochaine
cible = raccordement local conservant l'adaptation aux structures, pas
complexification spectrale ni nouvelle recherche de seuil après échec.

Jalon précédent (§§50–51) : biais du champ noir figé mis en évidence, puis
budget statistique multi-noirs passant les critères fixés dans deux domaines
séparés. Sur le rejeu fondé sur quatre noirs réels (terme de signal simulé),
erreur médiane de covariance d'environ4,1 % sur les trois premières bandes,
9,3 % sur les deux grandes ; rapportsσ0,823–1,205. Piste de budget attendu
retenue au banc, pas moteur de débruitage validé sur photos. Le guide spatial
seul échouait ; son apport avec le budget statistique reste modeste.
Six noirs de calibration/quatre de contrôle, même boîtier/séance, contrôles
déjà examinés : pas de validation physique indépendante ni de généralisation.
Aucun changement de production.
**Étude de banc seulement, aucun changement de production.**

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

## 32. Esquisse de la commande maître(b) — 30 octobre 2026

### 32.1. Retour réel sur(c1) et décision

Claude rapporte la campagne(c1), commit de preuve `a6b7a80`, au journal
commun : témoin0/0 identique sur104 sources, aucun cas pire de plus de5 %
que le plancher pour les mesures suivies, gain chroma limité sur les contours
et certains noirs. Le résidu noir de7599 à70/25 reste proche :11,09 contre
11,10 en chroma. Cela soutient de clore(c) avec(c1) comme amélioration limitée,
pas comme suppression des taches. Le rôle de la faible force couleur25
reste à traiter dans(b). Ce constat ne garantit pas la préservation de tous
les détails invisibles aux masques suivis.

Claude mesure maintenant le repli `0050858` dans un exécutable séparé
`b2e2b50`. Aucun rendu RAW concurrent lancé par Codex. La présente esquisse
ne modifie ni ce binaire ni le calcul d'`apply()`.

### 32.2. Loi candidate, contrat et interface envisagée

Fonction pure C++ `CameraDenoise::settingsFromAmount(int, Settings&)`, sans
changement du contrat `Settings{luminance, chrominance}`. Pour une barre M
entière entre0 et100, t=M/100 :

\[
L=M,\qquad C=\operatorname{arrondi}\{100[1-(1-t)^6]\}.
\]

| Barre maître M | Luminance interne | Couleur interne |
| ---: | ---: | ---: |
| 0 | 0 | 0 |
| 10 | 10 | 47 |
| 25 | 25 | 82 |
| 50 | 50 | 98 |
| 75 | 75 | 100 |
| 85 | 85 | 100 |
| 100 | 100 | 100 |

L'exposant6 est une **hypothèse ergonomique IPR**, non un coefficient déduit
d'un capteur ou ajusté à Lightroom. La chroma monte rapidement sans saut
dans la loi continue ; la quantification entière crée volontairement un
plateau à100 dès M=59. La partie haute de la barre affine alors surtout
la luminance. Monotonie des paramètres ne signifie pas monotonie de chaque
métrique rendue après DCP. La perception et la protection des détails restent
à contrôler, notamment sur des couleurs fines et peu contrastées.

Défaut M=0 : identité exacte. Entrées hors0..100 refusées sans modifier la
sortie. Aucun facteur supérieur à1 ajouté dans le filtre. Aucun accès à
l'image, allocation ou thread dans le remappage ; coût du filtrage inchangé
à couple(L,C) identique. Le coût applicatif d'une interaction reste à mesurer.

Raccordement futur proposé à Claude, **non implémenté ici** : une barre
« Réduction du bruit », défaut0, plus un sous-menu avancé affichant les
deux forces effectives. Une modification avancée passe directement ces
valeurs à `apply()` : ne pas appliquer le remappage une seconde fois.
Afficher un état personnalisé plutôt que déduire un M fictif d'un couple
arbitraire ; seuls les couples effectivement issus de la loi ont un M connu.
Le réglage reste de session tant que le contrat de persistance n'est pas changé.

### 32.3. Cible visuelle et qualification restante

Le repère utilisateur reste **le lissage luminance LR70–75 vers M80–90**, pas
une correspondance numérique L=LR. L=M est ici une référence simple, **pas
une preuve d'atteinte de cette cible**. Si L=100 reste insuffisant, un
remappage dans0..100 ne pourra pas inventer la force manquante : réexaminer
le filtre et sa protection avant de modifier la courbe ou dépasser son domaine.
Ne pas changer le modèle d'estimation pour obtenir une préférence de curseur.

[Preuve du module](../raw/camera-denoise-n2-v001-20261030T041640Z.json) :82
contrôles natifs et sept suites CTest passent. Les cinq nouveaux contrôles
vérifient les101 états contre une expression indépendante, bornes, monotonie,
points d'ancrage, identité et refus atomique des entiers invalides extrêmes.
Les précédents témoins du filtre gardent leurs résultats. Pas de nouveau
test d'intégration moteur annoncé : la preuve porte explicitement le statut
`engine_integration=pending`, benchmarks non exécutés. Aucun caller modifié.

Après qualification du repli, figer ce socle et comparer M=0,10,25,50,75,85,100
aux forces avancées correspondantes sur quelques scènes représentatives,
avec masques fixes, puis revue visuelle. Vérifier M=0 identique au témoin,
absence de double loi, plateau chroma et détails réels. Garder les références
LR75 de luminance comme repère, pas comme vérité propre ni cible de clonage.
Ne pas mélanger effet du repli et effet de la barre dans une seule comparaison.

## 33. Nouvelle étude algorithmique : domaine RAW et biais des noirs — 30 octobre 2026

### 33.1. Point de départ figé et absence de retour en arrière

L'utilisateur préfère visuellement `d4779e5` à V1, à zoom/cadrage identiques.
Claude a mesuré le repli (§31), preuve `45032c4`, et l'a intégré localement :
témoin0/0 identique à(c1) sur104 sources, gain sur plusieurs abstentions,
temps de campagne76 RAW augmenté d'environ7 % (rendu complet, pas module seul).
Les études(c1), plancher et repli ne sont pas relancées à l'identique ici.

Nouvelle demande : **étude et prototypes de banc seulement**, pas intégration.
Claude garde le raccordement de la barre maître et D0. Notre référence filtre
est celle de `47f406a` : même CameraDenoise que le repli intégré, plus fonction
de remappage non appelée par cette sonde ; le reste du moteur n'est pas déclaré
identique à la tête évolutive de Claude. Source, binaire, RAW, manifeste et DCP
sont identifiés dans les preuves. Ne pas attribuer un changement D0 à ce filtre.

Budget de sélection demandé : au plus huit threads adaptatifs,20,8 s à huit
threads, poste16 Go déjà chargé. Aucune nouvelle preuve de respect de ces
budgets n'est annoncée dans cette première étape.

### 33.2. Moyenne des noirs : première séparation des domaines

Défaut remonté par Claude : moyenne du masque sombre augmentée d'environ
3,4 niveaux sur7599,3,5 sur9153,2,7 surIMG_3135 à100/100. Ce sont des
mesures de rendu, non des DN capteur. Le module conserve le DC **global**
des axes caméra ; cela ne garantit ni le DC d'un masque local, ni celui du
rendu après tables, courbe, bornes et encodage. Le rééquilibrage DC peut
redistribuer spatialement les corrections : il n'est pas innocenté localement.

Une nouvelle option de la sonde, `--mean-audit`, filtre l'image entière à
100/100, puis mesure quatre rectangles fixes de7599 : trois positions
diagonales et le témoin2940/2940/420/420 déclaré auparavant. Aucun filtrage
indépendant par rectangle : modèle de bruit et rééquilibrage restent globaux.
Optiques désactivées, signal signé, DCP inchangé.

[Preuve réelle](../raw/camera-denoise-mean-audit-v001-20261030T115937Z.json) :
6984×4660, RGB caméra déjà balancé, profil d'empreinte49241c29… Les écarts
globaux après-avant sont R=−3,34×10⁻¹⁰, G=−1,05×10⁻¹⁰, B=−2,06×10⁻¹⁰.
La dérive de plusieurs niveaux ne vient donc pas d'un gain de DC global
caméra sur cette source et ce calcul.

| Rectangle caméra x/y/largeur/hauteur | Luma encodée avant | Après100/100 | Y Rec.2020 avant | Après |
| --- | ---: | ---: | ---: | ---: |
| 1641/1060/420/420 | 186,964 | 188,175 | 0,50964 | 0,51115 |
| 3282/2120/420/420 | 161,792 | 162,223 | 0,45720 | 0,45600 |
| 4923/3180/420/420 | 64,871 | 63,739 | 0,10282 | 0,09582 |
| 2940/2940/420/420 | 62,856 | 58,599 | 0,12874 | 0,12275 |

La luma encodée est pondérée Rec.709 sur sRGB borné/arrondi8 bits. **Ces
rectangles ne sont pas les masques noirs figés Lightroom** : pas de
contradiction démontrée avec les+3,4 de Claude, ni de reproduction de cette
valeur. Dans le dernier rectangle la moyenne caméra change aussi localement,
de sorte que le DCP seul ne peut être accusé. Étape suivante nécessaire :
reprendre exactement son masque, puis comparer mêmes pixels avant/après
filtre, après rendu linéaire et après encodage, et ablater le rééquilibrage
DC dans un prototype séparé. Ne pas soustraire arbitrairement3 niveaux.

Une [tentative préalable](../raw/camera-denoise-mean-audit-v001-20261030T115857Z.json)
échoue avec la racine locale `profiles/` vide de DCP ; conservée, sans résultat
numérique. La tentative réussie pointe vers le répertoire Camera réellement
installé. Aucun fichier de profil copié, modifié ou redistribué.

### 33.3. Contrôles de non-linéarité et prototype VST négatif

[Contrôles synthétiques](../raw/noise-domain-controls-v001-20261030T120039Z.json) :
65536 échantillons gaussiens antithétiques, graine fixée, oracle réduisant le
bruit autour d'une moyenne connue. Ce n'est pas un débruiteur d'image.

À moyenne0 etσ0,01, réduire le bruit résiduel à20 % diminue la moyenne après
borne basse de0,004010 à0,000802 et la moyenne encodée de9,81 à2,62 niveaux.
À moyenne0,02, le même contrôle conserve la moyenne signée mais fait monter
la moyenne encodée de36,76 à38,62. Borne basse et concavité peuvent donc
agir en sens opposés. Le signe observé dépend de la distribution et du rendu ;
ce contrôle n'identifie pas la cause réelle du défaut7599.

Prototype négatif original du banc : racine de type Anscombe généralisée,
a=0,001,b=10⁻⁵, puis **inverse algébrique naïf**, sans filtre. La racine
bornée rend identiques les observations sous−0,010375. Sur le témoin gaussien
centré deσ0,01 :15,04 % des échantillons perdent cette information et la
moyenne reconstruite monte à0,000790. Cet exemple ne simule pas un vrai
capteur Poisson-gaussien ; il démontre le danger d'une inverse naïve pour
des données signées. Six tests unitaires des formules et des limites passent.

Les [travaux de Mäkitalo/Foi](https://webpages.tuni.fi/foi/papers/ICASSP2012-Makitalo-Foi-GenAnscombe.pdf)
étudient précisément l'importance d'une inverse non biaisée de la VST en
régime Poisson-gaussien. La simple inversion de la racine n'est pas cette
inverse statistique. Toute piste VST devra mesurer biais et variance aux
faibles signaux et incertitude sura,b, pas seulement une baisse du grain.

### 33.4. Pistes retenues et critères pour ne pas refaire les mêmes études

1. **DC et masques exacts, priorité immédiate.** Rejouer7599/9153/IMG_3135 sur
   les masques figés, conserver moyennes caméra locales/globales, fractions
   négatives et moyennes de rendu. Les rectangles ci-dessus ne suffisent pas.
   Le modèle et les paramètres de force restent figés ; D0 séparé.
2. **CFA avant dématriçage.** Observer la mosaïque noire soustraite et
   normalisée parI2 ; traiter les quatre phases R/G1/G2/B avec leur origine
   et sans mélanger des couleurs adjacentes. Mesurera·signal+b par phase sur
   les rafales et les noirs, puis corrélations ligne/colonne et points aberrants.
   Le CFA peut conserver du bruit fixe, de ligne ou de compression : il n'est
   **pas garanti blanc/indépendant**. Comparer sans VST puis avec VST qualifiée,
   même dématriçage RCD/médiane derrière. Ce domaine n'avait pas été qualifié
   par nos variantes post-RCD ; ce n'est pas un nouveau seuil du même filtre.
3. **Auto-similarité contre Wiener local.** Comparer, sur ROI avec halo,
   une moyenne pondérée de patchs à la référence multi-échelle actuelle,
   sans recopier d'implémentation tierce. Rechercher sur des régions comparables
   en signal/couleur ; bruit corrélé et patchs recouvrants invalident une
   correction de distance supposant naïvement des échantillons indépendants.
   La redondance spatiale peut aider ; une texture unique ou du mouvement
   dans une rafale ne doit pas devenir un faux voisin.
4. **Faible bruit et résidus nommés.** IMG_7961/0032/9940/7423 : ne pas assimiler
   texture inchangée à échec ; chercher une référence de rafale indépendante
   ou une injection connue, pas viser un HP égal àLR. Garder3135/_MG_7840
   (noirs sous V1),5975/6062 (aplats),7599/9539 et détails colorés faibles
   comme sentinelles. Aucun de ces témoins n'est désormais un jeu réservé.

Les [travaux Unprocessing](https://people.csail.mit.edu/tfxue/papers/cvpr2019_unprocess.pdf)
appuient la nécessité de modéliser le bruit et le traitement photométrique
qui suit. Ils ne prouvent ni qu'un filtre CFA sera meilleur dansIPR, ni que
la covariance post-RCD se propage par une simple matrice : RCD est adaptatif.

Avant toute sélection, fixer jeux de développement et de contrôle par scène
ou séance, pas deux crops d'une même prise. Les104 RAW déjà utilisés ne
redeviennent pas réservés. Mesurer erreur face à référence si disponible,
chroma basse/haute fréquence, détails et contrastes, points isolés, DC,
temps/mémoire à définition identique ; aucun score unique ni nouveau seuil
adopté sur une seule scène. Le bénéfice visuel est requis pour une promotion.

### 33.5. Réutilisation : alerte licence, pas de validation juridique

La page [NLM IPOL](https://www.ipol.im/pub/art/2011/bcm_nlm/) signale des
fichiers potentiellement liés à des brevets et une licenceGPL pour le reste.
Le [logiciel officiel BM3D](https://webpages.tuni.fi/foi/GCF-BM3D/) est réservé
aux usages non lucratifs d'éducation et de recherche ; les
[paquets VST/inverse associés](https://webpages.tuni.fi/foi/invansc/) portent
également une licence limitée. **Publication et téléchargement ne valent pas
liberté d'intégration.** Aucun de ces codes n'a été téléchargé ou compilé ici.

Cette recherche n'est pas une vérification d'expiration des brevets par pays.
Une réimplémentation originale ne supprime pas à elle seule un éventuel droit
de brevet. Pistes bibliographiques retenues, mais aucune qualifiée de
« libre de droits » ; vérifier le périmètre juridique avant le prototype
correspondant si nécessaire et toujours avant intégration/distribution.

**Bilan de cette étape : deux sondes/prototypes de diagnostic, pas nouveau
débruiteur.** DC global confirmé, défaut local des noirs pas encore expliqué ;
VST naïve rejetée comme raccourci. Aucun moteur, profil, DCP ou IHM modifié.

## 34. Moyennes sur les masques noirs exacts — 30 octobre 2026

### 34.1. Contrat de comparaison et contrôles

Suite directe de§33, sans changer le moteur ni répéter une recherche de
paramètres. Le lanceur `audit_dark_mean.py` reprend les trois scènes ciblées
de la preuve `loi-force-bruit-v001-20261029T185322Z.json`. Empreintes RAW et
TIFF Lightroom neutre, compte de pixels et SHA256 du masque `packbits`
doivent correspondre exactement ; sinon la campagne s'arrête en conservant
son échec. Les masques sont recalculés dans le même crop Lightroom, puis
reportés par translation entière dans le repère caméra. Pas de redimensionnement.

| Scène | Pixels noirs vérifiés | Origine caméra du crop commun | Début SHA256 du masque |
| --- | ---: | --- | --- |
| 7Y6A7599 | 3303068 | 12/12 | ee5902f3 |
| 6V1A9153 | 670766 | 0/0 | 02ff739f |
| IMG_3135 | 23926 | 10/4 | 5a988309 |

Le filtre100/100 s'applique à l'image entière **avant** sélection du masque :
modèle, contexte spatial et rééquilibrage DC inchangés. Le rendu DCP de ce
socle étant pointwise et sans optiques, les pixels sélectionnés sont ensuite
regroupés en une ligne pour limiter la mémoire du diagnostic. Contrôle sur
une vraie région16×16 : erreur de rendu due à cette disposition **nulle**
sur les trois scènes. Cela ne qualifie pas un tel regroupement pour LCP ou
un opérateur spatial. Fichiers temporaires de masque dans le build, supprimés
à la sortie ; empreintes et recette conservées, aucun exportTIFF IPR.

[Preuve canonique](../raw/camera-denoise-fixed-dark-mean-v001-20261030T120856Z.json) :
trois scènes mesurées, mêmes pixels que la référence, source moteur0166857,
DCP inchangé dans ce worktree. Le moteur évolutif D0 de Claude n'est pas utilisé.
La sonde compile ; six tests du constructeur de masque passent (fixture figée,
translation, formats invalides, compte/hash erronés et débordements de crop).

### 34.2. Hausse reproduite et localisation dans la chaîne

Luma affichée = somme pondérée Rec.709 des canaux sRGB encodés,
bornés0..255 puis arrondis individuellement. Encodage signé = même transfert
sRGB sans borne finale, prolongé linéairement sous zéro. Unités linéaires
de travail : blanc nominal1 ; ce ne sont pas des DN capteur.

| Scène | Y Rec.2020 avant → après | Luma encodée signée avant → après | Luma affichée avant → après | Hausse affichée |
| --- | --- | --- | --- | ---: |
| 7599 | 0,004535 → 0,005350 | 9,294 → 12,934 | 9,465 → 12,935 | +3,470 |
| 9153 | 0,005925 → 0,007742 | 16,219 → 19,676 | 16,220 → 19,676 | +3,457 |
| IMG_3135 | 0,013196 → 0,015266 | 29,961 → 32,690 | 29,961 → 32,688 | +2,728 |

Les ordres de grandeur signalés par Claude sont donc reproduits. La hausse
existe avant l'arrondi et la borne d'affichage. Sur7599, la variation du
biais de borne finale vaut environ−0,185 niveau, et celle de l'arrondi+0,016 :
elles **atténuent globalement** la hausse, elles ne la créent pas. Ce constat
ne dit rien des bornes internes du DCP, qui précèdent les mesures rendues.

**Les trois moyennes RGB du masque augmentent déjà avant DCP :**

| Scène | ΔR caméra | ΔG caméra | ΔB caméra |
| --- | ---: | ---: | ---: |
| 7599 | +0,008142 | +0,007285 | +0,005802 |
| 9153 | +0,002083 | +0,001758 | +0,001626 |
| IMG_3135 | +0,002895 | +0,002043 | +0,001283 |

En revanche, sur l'image entière, le plus grand écart par canal est inférieur
à4×10⁻¹⁰,2×10⁻¹¹ et4×10⁻¹² respectivement. Le bilan DC global du module
est satisfait. Il ne garantit pas le bilan d'un sous-ensemble de pixels :
les corrections positives et négatives s'équilibrent ailleurs dans l'image.
Le filtre spatial, son adaptation et son rééquilibrage global peuvent y
contribuer ; aucune ablation de ces composants n'a été faite ici.

### 34.3. Témoin contrefactuel : imposer les moyennes locales ne suffit pas

Diagnostic seulement : aux pixels filtrés du masque, ajouter les trois
constantes ramenant leurs moyennes caméra aux valeurs initiales, puis rendre
le même DCP. Cela ne modifie ni le module ni une image de production ; ce
n'est pas une proposition de correction et ce n'est pas l'ablation du DC.

| Scène | Luma affichée avant | Après filtre | Après égalisation des moyennes caméra |
| --- | ---: | ---: | ---: |
| 7599 | 9,465 | 12,935 | 4,100 |
| 9153 | 16,220 | 19,676 | 16,303 |
| IMG_3135 | 29,961 | 32,688 | 29,934 |

Sur9153 et3135, ce témoin réduit fortement l'écart. Sur7599 il produit au
contraire un assombrissement important. Une égalité de moyennes caméra ne
conserve pas la distribution ni son rendu non linéaire. **Ne pas appliquer
de compensation locale ou de retrait constant fondé sur ce tableau.**

### 34.4. Le masque lui-même peut produire une hausse avec un oracle parfait

Le masque est figé, donc la comparaison porte bien sur les mêmes pixels.
Mais il a été choisi sur un rendu Lightroom du **même RAW**, pas sur une
vérité propre ou une capture indépendante. Le seuil de noir peut sélectionner
des excursions négatives partagées avec la source. Les retirer peut relever
la moyenne du masque, sans relever la moyenne de la scène véritable.

[Contrôle synthétique dédié](../raw/noise-dark-selection-v001-20261030T121147Z.json) :
262144 valeurs d'une plage uniforme de vérité0,01, bruit gaussienσ0,01.
Un oracle remplace chaque observation par sa vérité exacte, sans DCP, borne,
quantification, voisinage ni rééquilibrage DC. Masque = référence<0,01 ;
on varie seulement la corrélation du bruit entre référence et observation.

| Corrélation du bruit de référence | Moyenne observée dans le masque | Moyenne de l'oracle | Hausse |
| ---: | ---: | ---: | ---: |
| 0 | 0,010055 | 0,010000 | −0,000055 |
| 0,5 | 0,006035 | 0,010000 | +0,003965 |
| 1 | 0,002014 | 0,010000 | +0,007986 |

Avec un masque défini par la vérité (tous les pixels), la variation est
nulle à la précision numérique. Le cas indépendant est compatible avec
zéro à cinq erreurs-types marginales ; les cas corrélés montrent une hausse
sans aucun défaut de débruitage. Pas de simulation de Canon/Lightroom ici,
ni d'estimation de la part de ce phénomène dans les trois images réelles.

**Conclusion limitée :** les+3 niveaux sont réels pour ces masques, mais ne
prouvent pas à eux seuls une surexposition algorithmique. Les masques figés
restent utiles pour mesurer le bruit et comparer des variantes ; ils ne
constituent pas une vérité photométrique indépendante. Inversement, ce
contrôle n'innocente pas le filtre sur la photo réelle.

### 34.5. Décision et prochaine épreuve

Pas de changement du débruiteur, des forces, de la calibration ou de D0.
La question n'est plus seulement « où apparaît la hausse ? » : elle apparaît
déjà dans le signal caméra sélectionné. Il faut distinguer **sélection par
le bruit, redistribution spatiale et effet du rendu** avant de corriger.

Prochaine épreuve bornée, sans nouvelle photo requise au départ : définir des
zones géométriques fixes ou de grandes cellules à partir de la structure
basse fréquence, inclure tous leurs pixels (pas uniquement ceux sous25),
puis répéter les moments sur le même socle. Un masque lissé du même RAW
réduit le couplage au bruit fin mais n'est pas indépendant : conserver cette
limite. Pour une attribution photométrique, exploiter ensuite une rafale
avec référence multi-images excluant la prise testée et une validation
d'alignement ; l'écart-type temporel et les offsets fixes doivent rester séparés.
Les rafales disponibles sont à inventorier avant d'en demander de nouvelles.

En parallèle futur : ablation du rééquilibrage global dans un prototype
isolé du banc, jamais en changeant silencieusement le moteur de Claude.
La comparaison CFA/VST reste l'étape algorithmique suivante, mais aucune
VST n'est retenue sur la seule baisse des niveaux du masque sombre.

## 35. Cellules complètes et contrôle Lightroom — 30 octobre 2026

### 35.1. Épreuve fixée avant mesure

Même source moteur, même filtre pleine image100/100, mêmes trois RAW et
même référence de provenance qu'en§34. Nouvelle sélection : grille128×128
ancrée à l'origine du crop commun Lightroom. Une cellule complète est retenue
si sa **moyenne Lightroom neutre** est inférieure à25 niveaux. Tous ses
pixels sont alors inclus, même ceux individuellement au-dessus de25.
Pas de sélection sur les résultats IPR, pas de filtre de gradient, pas de
recherche de taille/seuil après lecture des résultats. Cellules partielles
du bord exclues ; une scène sans cellule ne déclenche pas de retouche du protocole.

Le masque fin historique est d'abord reconstruit et vérifié par empreinte,
mais il ne sert plus à la mesure. Dans cette preuve, son empreinte reste
le champ `mask_packed_sha256` de provenance ; les octets du **nouveau** masque
union des cellules ont leur propre `mask_bytes_sha256`, vérifié côté natif.
Le filtrage reste plein cadre, puis les moments et le rendu pointwise sont
mesurés sur l'union et sur chaque cellule. Disposition vérifiée avec erreur0.
Neuf tests de sélection/coordonnées passent ; sonde C++ compilée avec succès.

[Preuve IPR par cellule](../raw/camera-denoise-coarse-dark-cells-v001-20261030T121723Z.json) :
223 cellules pour7599, une pour9153, **aucune pourIMG_3135**. Cette dernière
n'est pas mesurée par cette sélection ; ne pas inventer un résultat nul.
Les coordonnées, moyennes Lightroom de sélection, moments caméra/rendu et
résultats de chaque cellule sont conservés, pas seulement la moyenne finale.

### 35.2. La hausse disparaît, mais la couverture change

| Scène | Cellules / pixels | Luma affichée IPR avant → après | Variation | Variation dans l'ancien masque fin |
| --- | --- | --- | ---: | ---: |
| 7599 | 223 / 3653632 | 20,012 → 11,633 | −8,378 | +3,470 |
| 9153 | 1 / 16384 | 23,875 → 24,093 | +0,218 | +3,457 |
| IMG_3135 | 0 / 0 | non mesurée | — | +2,728 |

Sur7599, les223 cellules s'assombrissent toutes : variations−9,853 à−5,260,
médiane−8,580. La moyenne agrégée n'annule donc pas des hausses opposées.
Sur9153, une seule cellule à6528/0 est admissible : couverture très faible,
aucune généralisation à l'image entière. Les cellules ne recouvrent pas
exactement les pixels du masque fin ; la différence des deux tableaux n'est
**pas** une quantification causale du seul biais de seuil.

Dans les cellules de7599, ΔRGB caméra vaut environ+0,0000864 /+0,0000096
/−0,0000580, contre+0,008142 /+0,007285 /+0,005802 dans le masque fin.
Le DC local caméra varie donc bien moins. Pourtant la moyenne Y Rec.2020
rendue passe de0,013174 à0,004570, et la luma encodée signée de19,602 à11,632.
La baisse existe déjà avant la borne finale et l'arrondi ; ce n'est pas
seulement un effet de gamma d'affichage. Les distributions changent sous un
rendu DCP non linéaire : leurs moyennes caméra ne suffisent pas à prédire
la moyenne rendue. Cela n'isole pas les tables, la courbe ou les bornes internes.

### 35.3. Même géométrie sur les exports Lightroom existants

Sans nouveau rendu IPR, `compare_coarse_lr.py` relit les TIFF neutres,75/0/0
et100/100/0 de la preuve historique, vérifie leurs empreintes puis mesure
les mêmes coordonnées translatées, sans redimensionnement. Toutes les
références demandées sont présentes et leurs empreintes correspondent.
[Preuve Lightroom](../raw/camera-denoise-coarse-lr-means-v001-20261030T122059Z.json).

| Scène | LR neutre | LR75/0/0 | LR100/100/0 | IPR neutre | IPR100/100 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 7599 | 19,330 | 15,307 | 9,371 | 20,012 | 11,633 |
| 9153 | 23,806 | 23,741 | 23,674 | 23,875 | 24,093 |

Unités0..255 : LR conserve les valeurs TIFF16bits/257, IPR est borné et
arrondi par canal en8bits. Cette différence est explicitée, pas assimilée
à une différence de signal linéaire. Les couples100/100 n'ont pas une force
équivalente garantie. L'objectif n'est pas de régler IPR pour copier ces
valeurs de moyenne ni d'effacer toutes les textures comme un export LR fort.

LR s'assombrit également sur ces mêmes cellules de7599 : environ−4,023
niveaux à75/0/0 et−9,958 à100/100/0. Ce comportement n'est donc pas propre
au filtre IPR. LR n'est toutefois ni un oracle propre ni une preuve que
l'assombrissement absolu d'IPR soit optimal. Sur9153, les écarts sont petits,
avec les limites de couverture d'une seule cellule.

### 35.4. Interprétation et décision pour éviter une fausse correction

L'hypothèse d'une **hausse systématique des noirs** ne résiste pas à ce
changement d'observation : elle dépend fortement des pixels retenus. Le
contrôle d'oracle de§34 expliquait un mécanisme possible de sélection ; la
mesure réelle ici est cohérente avec cette réserve, sans en attribuer
quantitativement toutes les causes. Les cellules restent choisies sur le
même RAW, donc pas statistiquement indépendantes ni une vérité photométrique.

**Ne pas corriger le DC sur la base des+3 niveaux du masque fin.** Une telle
compensation pourrait assombrir davantage des régions déjà assombries.
Ne pas non plus ajouter un gain pour annuler automatiquement les−8 niveaux
des cellules : LR présente un comportement comparable, et le signal propre
reste inconnu. Le rééquilibrage DC global n'a pas été ablaté ; aucune
conclusion de responsabilité ou d'innocence de ce composant seul.

Cette branche de diagnostic peut être mise en attente faute de référence
indépendante, sans multiplier les seuils/cellules pour obtenir un résultat
préféré. La prochaine cible algorithmique utile reste **la mesure CFA et
la comparaison avant/après dématriçage**, en commençant par inventorier les
rafales disponibles. Une référence multi-images doit exclure la prise testée,
contrôler mouvement/alignement et distinguer bruit temporel et motif fixe.
Pas besoin de nouvelles photos avant cet inventaire.

D0 est suivi par Claude : les preuves actuelles sont pré-D0 et ne doivent
pas servir à déclarer son futur rendu équivalent. Aucun changement moteur,
force, DCP, profil ou IHM, aucune intégration ni publication dans cette étape.

## 36. Pont vers le CFA : corpus et contrat signé — 30 octobre 2026

### 36.1. Relecture pour ne pas refaire l'ancien chantier

Avant de décoder un pixel, relecture du rapport `analyse-bruit.md` de la
branche `patch_noise`, notamment§439–471, et de ses JSON. Ce rapport reste
historique, pas un deuxième rapport courant à alimenter. Il existe déjà des
partitions de rafale, des PTC, des références noires répétées, des études
de transport CFA/RCD/DCP et d'injection de signal : ne pas relancer ces
travaux comme une étude nouvelle ou une validation indépendante.

Les anciens transports utilisaient un chemin d'ouverture différent, avec
les limites de bornage documentées. Le nouveau point à préparer est un
prototype dans le **chemin signé extrait**, pas une nouvelle recherche de
seuils sur l'ancien développement. Les quatre preuves historiques utilisées
sont identifiées par chemin et SHA256 dans le nouvel inventaire ; aucune
modification dans le worktree ou les fichiers de l'autre branche.

### 36.2. Disponibilité et partitions conservées

[Inventaire canonique](../raw/cfa-corpus-inventory-v001-20261030T122935Z.json) :
vérification du système de fichiers et des empreintes, sans décodage de pixels.
Métadonnées héritées des preuves authentifiées, pas réextraites ni inventées.

| Ensemble historique | Fichiers disponibles / attendus | Rôle conservé |
| --- | ---: | --- |
| Rafale9819–9860 | 42 / 42 | 14 développement,14 validation déjà utilisée historiquement,14 scellées |
| Bouchons fermés confirmés | 65 / 65 | 31 développement,34 contrôle déjà mesuré ; six ISO |
| Rafales pièce sombre surD: | 72 / 72 | 26 groupes éclairés déjà mesurés, **pas des noirs** |

Les179 empreintes correspondent. Les ISO des65 noirs sont1600,3200,6400,
12800,25600,51200 ; la rafale42 est àISO4000,1/200s, R6 Mark III.
Les14 dernières9847–9860 restent **scellées** : métadonnées historiques et
empreintes seulement, aucun pixel ouvert dans cette étape. Contrôle et
validation historiques ne sont pas renommés en jeux réservés neufs.

Douze autres fichiers du dossier `protocole/blacks` ne figurent pas dans les
parents sélectionnés :9785–9794 et9801–9802. Leur présence et leur taille
sont consignées ; rôle et qualification restent à rapprocher des anciennes
prises, pas déduits du nom du dossier. Aucun fichier déplacé, supprimé ou copié.

Une première exécution de l'inventaire a échoué lors de la lecture Git du
worktree tiers (`dubious ownership`), après les contrôles de fichiers mais
avant création du JSON. L'outil consulte maintenant la référence de branche
depuis notre propre dépôt ; aucune exception globale `safe.directory` ajoutée.
L'inventaire réussi ci-dessus est la première preuve canonique produite.

### 36.3. Pilote fixé avant pixels : six développements, pas une calibration

[Gel avant captures](../raw/cfa-signed-pilot-freeze-v001-20261030T123141Z.json) :
9819/9820 (éclairé,ISO4000),0003/0004 (noirs,ISO1600),9950/9951 (noirs,ISO51200).
Ces six sources sont dans les partitions de **développement**, non réservées.
Le sélecteur refuse d'y faire entrer une source manquante, divergente ou
appartenant à validation/contrôle/scellé. Cinq tests de cette sélection passent.

Outil existant `iprRawDevelopmentCapture` reconstruit depuis les sources de
notre worktree. PolitiqueSigned, noir expérimental fixé à2048DN dans les
quatre couleurs, blanc16383DN ; diviseur14335. Les gains appliqués sont
R=1,892578125, G=1, B=1,6923828125, G2=1 dans les six captures. Ce choix
explicite évite de confondre une sélection de noir avec le transport testé ;
**ce n'est pas la calibration de production**, ni une estimation nouvelle de
ces niveaux ou un audit DCP. Aucun réglage moteur n'est modifié.

Trois ROI256×256 fixes de l'outil existant, orientation capteur. Huit frontières
par source : CFA décompressé, noir soustrait, normalisé, avantRCD, RGB aprèsRCD,
avant sortie, après médiane/sortie, entrée caméra pré-DCP. Les quatre phases
R/G1/G2/B restent distinctes et suivent le motif et l'origine déclarés.

[Résultats](../raw/cfa-signed-pilot-v001-20261030T123141Z.json) : six captures,
trois paires,180 lignes de mesures. Manifestes, binaires, RAW, politiques,
géométries, unités et contextes de paire contrôlés. Sur chaque source et
chaque ROI, `PreOutputCameraRgb`, `PostMedianCameraRgb` et la capture finale
`pre_dcp_camera_rgb` sont **identiques au bit près**. Aucune borne ni grille
16bits réintroduite entre ces captures. Les pixels diagnostiques restent
dans le build du banc ; JSON, recette et empreintes font autorité sur les mesures.

### 36.4. Négatifs et corrélations observées

Étendues sur les deux vues, trois ROI et couleurs/phases :

| Paire | Négatifs CFA après noir | Négatifs RGB pré-DCP | Corrélation différentielle CFA x/y | Corrélation différentielle RGB x/y |
| --- | --- | --- | --- | --- |
| ÉclairéISO4000 | 0–2,03 % | 0–1,50 % | 0,084 à0,544 | 0,479 à0,793 |
| NoirISO1600 | 46,16–48,82 % | 50,09–52,89 % | −0,008 à0,033 | 0,320 à0,535 |
| NoirISO51200 | 49,44–51,93 % | 50,48–53,72 % | −0,010 à0,029 | 0,329 à0,549 |

La différence de paire est divisée par√2 ; moyenne, covariance, fractions
signées et corrélations sont conservées, pas seulement unσ agrégé. La fraction
CFA n'est pas exactement50 % : quantification, offset réel du noir et
distribution ne sont pas contraints à une gaussienne centrée par ce choix2048.
Pas de nouveau niveau de noir ajusté pour obtenir50 %.

Les pas de voisinage diffèrent : CFA séparé par phase = deux pixels capteur,
RGB = un pixel. On ne compare donc pas directement les mêmes distances
physiques. Les corrélations RGB des noirs sont cohérentes avec un transport
adaptatif créant des dépendances ; elles ne quantifient pas à elles seules
un effet causal de RCD ou de la médiane. Les noirs CFA de ce petit pilote
ne prouvent pas que tous les RAW/ISO sont blancs et indépendants.

### 36.5. La rafale éclairée ne fournit pas une vérité temporelle immédiate

Le fort résidu corrélé de9819/9820 ne doit pas être nommé automatiquement
« bruit CFA corrélé ». L'ancien audit de mouvement§440 de `patch_noise`
documente une transformation réelle de la rafale : rotation jusqu'à−0,427°,
échelle1,00458 et translation de modèle−5,18 pixels sur prévisualisation
largeur800. Ce ne sont pas des déplacements locaux garantis ; parallaxe et
résidu de recalage restent possibles. Le pilote actuel ne recale pas les vues.

Différencier deux mosaïques non recalées peut donc conserver de la texture
déplacée ou une variation d'éclairage. Une moyenne leave-one-out n'annule
pas ce problème, ni le motif fixe commun. Interpoler un recalage directement
sur une mosaïque mélangerait en plus les phases si le traitement n'est pas
défini dans leur repère. **Ne pas utiliser cette paire comme oracle de bruit
physique ou référence propre pour retenir une VST.** Le développement signé
n'a pas, à lui seul, résolu les limites de la référence.

### 36.6. Direction préparée, sans nouvelle demande de photos

Le corpus existe ; inutile de demander immédiatement de nouvelles prises.
Premier prototype recommandé : utiliser les résidus des **noirs de
développement** avec un signal mosaïque synthétique connu, motifs et phases
explicites, puis comparer traitement avantRCD à référence actuelle aprèsRCD.
La cible propre est alors connue et ne dépend pas d'un recalage de scène.
Conserver motif fixe, dérive et bruit temporel comme composantes distinctes.

Ce témoin au bruit de lecture ne suffit pas pour une scène éclairée : il
manquera la contribution photonique et sa dépendance au signal. Les lois
PTC historiques déjà figées pourront servir de candidats, avec leurs limites,
sans réajustement sur les résultats du filtre. Un traitement simple dans le
domaine CFA à variance constante précédera toute VST, pour isoler le bénéfice
du placement de celui d'une transformation statistique. Contrôles obligatoires :
biais du signal, détails chromatiques, faibles contrastes, bruit de ligne,
valeurs sous zéro, ressources ; pas seulementσ ou moyenne du masque sombre.

La validation sur vraie photo éclairée exigera ensuite des références de
rafale dont recalage et incertitude sont qualifiés, idéalement autre scène
ou séance ; les179 fichiers déjà examinés ne constituent pas un nouveau
jeu indépendant. Pour la réserve finale, toute ouverture devra rester tracée
et ne servira pas à choisir itérativement les poids. **Aucun débruiteur CFA,
VST, modèlea·signal+b nouveau ni coefficient de production livré ici.**

## 37. Vérification de la piste Jensen avec le DCP réel — 30 octobre 2026

### 37.1. Retour Claude et périmètre

Claude a raccordé la barre unique dans `e01fc59`, après fusion de l'esquisse
`47f406a`, et confirmé la décision D0 par la preuve `115beee`. D0 garde les
bornes actuelles ; il **ne crée pas un nouveau rendu**. Comparaison Git entre
notre tête et ce jalon : aucun écart dans `CameraProfileEngine.cpp/.h` ni
`WorkingColorSpace.h`. Les profils et manifestes sont vérifiés séparément
par empreinte dans l'épreuve ci-dessous ; cette concordance de trois fichiers
ne vaut pas équivalence de l'application entière ou de tous ses réglages.

Suggestion de Claude : une courbe concave peut assombrir la moyenne d'un
signal bruité ; réduire sa variance pourrait donc relever la moyenne rendue.
Le mécanisme est mathématiquement possible, et le contrôle scalaire§33 en
donnait déjà un exemple. Il faut toutefois l'évaluer dans le rendu réel,
qui compose matrices, tables, courbe, bornes et encodage et n'est pas garanti
concave sur tout le domaine des RGB signés.

### 37.2. Décomposition mesurée sans relancer le filtre

Pour chacune des cinq populations de§34–35, conserver les moyennes caméra
et rendues déjà mesurées. Décoder seulement pour retrouver le contexte du
profil, vérifier RAW/manifeste/DCP et l'empreinte de son implémentation, puis
évaluer le rendu sur un unique pixel portant la moyenne caméra. Aucun
débruitage, nouveau masque ou accès à la réserve finale.

Noter X le vecteur RGB caméra des pixels retenus, μ sa moyenne, F le rendu
pointwise fixé et G=E[F(X)]−F(μ) son écart de distribution. L'identité
comptable est :

\[
\Delta E[F(X)] = \underbrace{F(\mu_1)-F(\mu_0)}_{\text{déplacement de moyenne}}
 + \underbrace{G_1-G_0}_{\text{changement d'écart de distribution}}.
\]

F(μ) est évalué dans le moteur flottant32bits réel ; G est un **écart de
Jensen descriptif**, pas une estimation de courbure, une variance ou une
ablation causale des composants. L'effet de distribution mêle variance,
covariance, queues et autres changements de forme ; ce n'est pas seulement
½F″σ². Les deux termes peuvent se compenser : ne pas en tirer des pourcentages
d'explication causale.

[Preuve canonique](../raw/camera-denoise-jensen-v001-20261030T123902Z.json) :
trois sources, cinq populations, DCP/configurations/parents authentifiés.
Les décompositions sont conservées en Y Rec.2020 linéaire, encodage signé
et encodage borné avant arrondi. Sonde compilée, cinq tests de calcul passent
(déplacement seul, écarts positifs/négatifs, compensation et refus des NaN).

### 37.3. Résultats en luma encodée bornée, non arrondie

| Population | Variation observée | Terme déplacement de moyenne | Terme changement de distribution |
| --- | ---: | ---: | ---: |
| 7599, masque fin | +3,454 | +9,321 | −5,867 |
| 7599, cellules complètes | −8,389 | +0,086 | −8,475 |
| 9153, masque fin | +3,457 | +3,816 | −0,359 |
| 9153, cellule complète | +0,219 | +0,235 | −0,016 |
| IMG_3135, masque fin | +2,729 | +2,860 | −0,131 |

Les valeurs diffèrent très légèrement du tableau affiché8bits de§34–35
parce qu'ici l'arrondi final est retiré. Dans les **trois masques fins**, le
déplacement de moyenne caméra rendu par F porte la hausse ; le changement
de distribution du rendu complet la réduit, plutôt que de la créer.

Sur7599 fin, G passe de+6,938 à+1,071 niveaux. Sur ses cellules complètes,
de+9,403 à+0,928 : le rendu moyen bruité est ici **au-dessus** du rendu de
la moyenne, à l'inverse d'un modèle scalaire globalement concave. Cela ne
permet pas de déclarer la courbe du profil convexe : le F mesuré comprend
toute la chaîne et un signal multicanal, avec bornes. Une concavité de la
courbe peut intervenir tout en étant dominée par d'autres effets.

### 37.4. Avant encodage : effets déjà présents au DCP linéaire

En Y Rec.2020 sur7599 fin, ΔE[F]=+0,000815 se décompose en+0,002918 de
déplacement et−0,002103 de distribution. Sur ses cellules,−0,008604 vaut
environ+0,000029−0,008633. Ce phénomène ne vient donc pas uniquement du
transfert sRGB de sortie.

Pour9153 fin, la distribution augmente légèrement Y linéaire (+0,000103),
mais réduit la hausse après encodage (−0,359 niveau). Le signe peut changer
selon la frontière observée ; il faut nommer le domaine et les pixels, pas
simplement « luminance ». Aucun point intermédiaire entre les tables et la
courbe n'est mesuré ici.

### 37.5. Réponse à l'hypothèse et décision

**Accord sur le mécanisme général, attribution simple non retenue pour les
hausses observées.** Sur ce F complet et ces populations, la diminution
d'un écart négatif de courbe concave n'est pas le moteur de la hausse des
masques fins en luma encodée. Leur sélection et le déplacement local des
moyennes caméra restent déterminants ; la causalité entre filtrage spatial,
rééquilibrage global et sélection bruitée n'est pas décomposée par cette identité.

Cela n'implique pas que le débruiteur surexpose la scène : le contrôle
d'oracle et les cellules de§34–35 restent nécessaires pour l'interprétation.
Pas de correction de gain/DC ni de retouche du DCP justifiée par ce seul
bilan. Aucun désaccord avec D0, qui conserve précisément le rendu testé.

Reprendre la piste de prototype CFA au signal connu de§36. Ne pas relancer
les campagnes de force ou une grille de courbures sur les mêmes trois
images pour choisir un correctif. Une attribution composant par composant
demanderait une ablation séparée, seulement si un défaut photométrique est
confirmé par une référence indépendante. Aucun moteur, profil ou IHM modifié.

## 38. Premier prototype CFA à signal connu — 30 octobre 2026

### 38.1. Gel, entrées et ce que l'étude peut montrer

À la demande de l'utilisateur, premier prototype de filtrage **avant** RCD,
pas une nouvelle PTC ou un rejeu du transport historique. Pas d'intervention
de Claude nécessaire à cette étape : ses fichiers et le moteur restent
inchangés ; seules les interfaces existantes de RCD et CameraDenoise sont
appelées dans un exécutable de banc.

[Gel avant mesures](../raw/cfa-denoise-prototype-freeze-v001-20261030T125927Z.json),
[résultats canoniques](../raw/cfa-denoise-prototype-v001-20261030T125927Z.json).
Réutilisation des captures authentifiées§36, pas de nouvelle lecture RAW :
noirs de développement0003/0004 àISO1600 et9950/9951 àISO51200. ROI centrale
256×256 parmi les trois ROI déjà définies. Les14 vues scellées restent fermées.
Sources, recette, binaire, parents et empreintes sont conservés ; le gel
inclut le texte du prototype et les sources des modules appelés.

Signal synthétique RGB caméra équilibré connu, échantillonné selon le vrai
motif Bayer du crop ; G1/G2 restent séparés au filtrage. Résidu noir soustrait
2048DN, divisé par14335 et multiplié par les gains déclarés (§36), **ajouté
sans recentrage ni permutation**. Les offsets, valeurs extrêmes et structures
fixes du résidu sont donc conservés, pas masqués par une simulation iid.

Deux niveaux moyens0,005 et0,03, quatre motifs : aplat, texture achromatique
sinusoïdale faible, bord chromatique et ligne achromatique de deux pixels.
Deux ISO, deux directions de paire :32 cas. La variance de bruit est estimée
sur **l'autre prise noire de développement**, jamais sur la vérité synthétique.
Ce prior externe est privilégié par rapport à l'estimation aveugle du traitement
courant : comparer ces méthodes ne permet pas d'isoler le seul placement.

Limites importantes : bruit de lecture seulement, pas de contribution
photonique ni de VST ; offsets d'un noir expérimental, pas une calibration
physique nouvelle ; petits crops, une scène synthétique par motif, un boîtier,
pas de validation indépendante. Le vraiRGB synthétique reste la cible
mathématique de l'injection, pas une vérité radiométrique du boîtier.

### 38.2. Préfiltre original et branches comparées

Le prototype `cfa_denoise_prototype.cpp` implémente un Wiener scalaire local
par phase, fenêtre3×3 dans le plan de phase (pas de mélange R/G1/G2/B).
Moyenne locale m, variance locale échantillonnale v=9/8(E[x²]−m²), variance
du noir externeσ² :

\[
\widehat{x}=m+\max(0,1-\sigma^2/v)(x-m),
\]

avec gain nul si v≤σ², identité de la phase siσ²=0. La variance numérique
est ramenée au minimum0 ; **le signal n'est pas borné**. Calcul local en
double, sortie flottante32bits signée, bord réfléchi. Les corrélations du
noir réel limitent l'interprétation d'une variance locale supposée iid.

Référence mathématique de la famille locale :
[documentation officielle du filtre Wiener SciPy](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.wiener.html),
qui renvoie à Jae S. Lim,1990. Aucun code tiers repris, aucune dépendance SciPy
ajoutée, aucune affirmation de liberté de brevet ou d'identité avec cette
implémentation. Le prototype est original et limité au banc.

Quatre branches figées : RCD/médiane seuls ; préfiltre CFA puis RCD ;
RCD puis CameraDenoise100/100 ; préfiltre CFA puis RCD puis CameraDenoise100/100.
Toujours RCD signé et médiane actuelle. Chaque branche est aussi appliquée
au motif **sans résidu**, avec le même réglage et, pour le CFA, la même
variance externe. Ce dernier est un test de réponse structurelle sous prior
non nul, pas une prétention que le signal propre possède ce bruit.

Mesures sur96×96 centrés, marge80 pixels, pour éloigner les bords : MSE
contre RGB vrai, erreur contre RCD propre, biaisRGB, erreurs sur intensité
caméra/chroma et gain projeté sur le motif. L'intensité caméra n'est pas Y
Rec.2020. RCD propre permet de séparer les artefacts intrinsèques de
dématriçage ; les erreurs ne sont pas toutes du bruit supprimable.

### 38.3. Critère exploratoire fixé avant les résultats

Poursuivre le préfiltre **inchangé** seulement si les trois conditions sont
remplies : médiane des MSE pré/post<1 ; au moins la moitié des cas améliorés ;
gain de chaque motif propre structuré≥95 % du gain de RCD propre. Ce filtre
de décision conservateur est un critère de poursuite du prototype, pas un
seuil de conformité universel ou un réglage de force produit.

Cinq contrôles natifs passent : variance nulle=identité, constante signée,
phases non mélangées, translation du signal et refus d'une variance négative.
Le test CTest ajouté passe, ainsi que quatre tests Python de la décision
(bruit/détail, rejet de perte de détail, cas dégradé et gestion des aplats).
Les huit suites CTest locales du banc passent ensuite, sans lancer la
suite complète de l'application ni modifier ses exécutables.
Compilation initiale corrigée pour un includeQDir manquant et une déclaration
C++ ambiguë, avant toute mesure ; pas de tentative numérique déguisée en succès.

### 38.4. Résultats : le préfiltre simple ne bat pas le traitement courant

Médianes de MSE RGB contre la vérité, rapportées à RCD seul (plus bas=meilleur) :

| ISO | CFA Wiener seul | Post courant100/100 | CFA+post |
| ---: | ---: | ---: | ---: |
| 1600 | 0,434 | 0,354 | 0,295 |
| 51200 | 0,284 | 0,138 | 0,095 |

Les rapports appariés pré/post donnent des médianes1,272 et2,046 ; ce ne
sont pas des rapports des médianes du tableau. Le préfiltre n'est meilleur
que dans4/16 cas àISO1600, et0/16 àISO51200. À1600 il gagne notamment sur
la texture faible (rapport médian0,520), mais pas sur aplats, bords ou lignes.
Ce succès partiel ne suffit pas à le sélectionner.

Plus petit gain de motif propre, rapporté au RCD propre :

| ISO | CFA seul | Post courant | CFA+post |
| ---: | ---: | ---: | ---: |
| 1600 | 0,749 | 0,523 | 0,290 |
| 51200 | 0,322 | 0,523 | 0,322 |

Le préfiltre atténue la texture faible et, àISO51200, surtout la ligne fine.
Le bord chromatique reste bien mieux conservé (gain minimal0,982 àISO51200).
La combinaison abaisse davantage l'erreur totale mais ne franchit pas la
protection des motifs propres. **Écran de poursuite négatif aux deux ISO.**

Le traitement courant conserve les bords/lignes propres de ce banc, mais
réduit la projection de texture sinusoïdale à environ52,3 %. Sa variance
est estimée dans le contenu : le cas est compatible avec une confusion
texture/bruit. Ce n'est pas une nouvelle régression de production, puisque
le code n'a pas changé, ni une mesure de perte sur les murs réels. Le cas
propre reste une sentinelle explicite pour l'étude suivante.

### 38.5. Décision et coopération

**Ne pas intégrer ce Wiener CFA ni sa combinaison.** L'étude ne réfute pas
le domaine CFA, seulement cette première méthode locale à fenêtre/prior fixés.
Ne pas chercher a posteriori une fenêtre ou une force qui gagne sur ces32
motifs. Les motifs et prises désormais consultés sont du développement,
pas un nouveau jeu de validation.

La prochaine question utile est la protection des structures faibles :
distinguer signal spatial cohérent et bruit avant de décider la force de
retrait, avec témoins propres obligatoires. Une variante devra apporter un
mécanisme différent (guidage/prédiction structurelle qualifiée, par exemple),
pas simplement augmenterσ. Comparer ensuite à une baseline de prior comparable
pour ne pas confondre calibration externe et placement avant/aprèsRCD.

Pas de besoin de raccordement moteur pour ce premier jalon ; Claude reçoit
les résultats au journal. Si un point d'injection ou un observateur moteur
devient indispensable, demander un contrat séparé avant de modifier ses fichiers.
Pas de revue visuelle de photo ni budget20,8s/16Go qualifié : seuls des temps
de patches sont enregistrés, au plus huit threads via les modules existants,
OpenCV limité à un thread dans le banc. Aucun fichier de production, DCP,
profil ou IHM modifié ; pas de push, fusion versdev ou tag.

## 39. Gain tonal aval : faisabilité, coût et garde-fous — 30 octobre 2026

### 39.1. Demande et périmètre

Livraison signalée par Claude : `dev b19b4c3`, `v0.1.63-alpha`. La nouvelle
piste demandée par l'utilisateur est d'adapter la force locale au bruit rendu
plus visible après exposition, contraste et ombres. L'estimation physique
du bruit reste dans le signal caméra : éclaircir n'ajoute pas de bruit capteur
et multiplier signal et bruit ne restaure pas le rapport signal/bruit.

Inspection du chemin livré : le filtre caméra précède LCP et DCP ; avec DCP,
l'exposition intervient avant la courbe du profil, puis viennent contraste
et récupération des tons. Les statistiques tonales sont celles de la source
neutre partagée avec le master, pas des statistiques réestimées sur chaque
retouche. Les réglages tonals et le débruitage appartiennent actuellement à
la même première clé de cache : une retouche de cette étape peut **déjà**
relancer le filtre. Le coût existant doit être distingué du coût de l'adaptation.
Les modules de calcul utilisés ici sont identiques à ceux de `b19b4c3` ;
cela ne revendique pas l'alignement de toute l'application du banc sur dev.

### 39.2. Mesure : éclaircissement et amplification ne sont pas interchangeables

Preuve canonique :
[`camera-denoise-tonal-gain-v002-20261030T132023Z.json`](../raw/camera-denoise-tonal-gain-v002-20261030T132023Z.json).
La v001 est conservée ; la v002 ajoute l'écart-type effectivement tiré au
contrôle gaussien. L'écart apparent de 2,9 % de la v001 venait essentiellement
de l'échantillon aléatoire fini, pas d'un défaut de propagation démontré.

Sonde native : six recettes, 64 gris logarithmiques Rec.2020 linéaire,
modules actuels de contraste/ombres, statistiques typiques. Exposition
appliquée APRÈS profil à la manière d'un chemin raster, **sans DCP** : ces
facteurs ne sont donc pas les facteurs réels du chemin RAW. Dérivée par
différences centrées ; encodage sRGB et borne d'affichage, luma Rec.709.
Quatre niveaux contrôlés avec 2 048 échantillons gaussiens achromatiques
antithétiques, écart-type nominal de 0,5 % du signal.

Exemple au gris d'entrée Y=0,009876 :

| Recette | Y sortie / Y entrée | Dérivée linéaire | Amplification de σ affiché par rapport au neutre |
| --- | ---: | ---: | ---: |
| Neutre | 1,000 | 1,000 | 1,000 |
| Exposition +2 EV | 4,000 | 4,000 | 1,782 |
| Exposition +3 EV | 8,000 | 8,000 | 2,378 |
| Contraste +50 | 0,405 | 0,470 | 0,797 |
| Ombres +100 | 3,806 | 2,435 | 1,117 |
| +2 EV et ombres +100 | 8,824 | 4,812 | 1,351 |

Pour les contrôles non écrêtés, σ mesuré / (dérivée × σ réellement tiré)
reste entre 0,999596 et 1,000123. Ce résultat vérifie seulement une
approximation locale dans ce témoin à faible bruit. Les contrôles écrêtés
ont une dérivée nulle et un rapport indéfini : une variance faible par
saturation n'est pas un bon débruitage ni une information préservée.

La quantité utile est la sensibilité locale `σ_sortie ≈ |T′(Y)| σ_entrée`,
pas le rapport `T(Y)/Y`. Pour le vrai RGB caméra, il faudra le Jacobien du
rendu et sa covariance : `Σ_sortie ≈ J Σ_caméra Jᵀ`. Cette propagation ne
donne pas à elle seule une loi de force optimale. La chroma ne doit pas
recevoir aveuglément le même facteur que la luminance. Profil, BdB, tables,
bords de gamut et bornes nécessitent une qualification dans la vraie chaîne.

### 39.3. Réutiliser le travail du filtre : un témoin positif limité

Sur une image caméra synthétique 128×128, bruit gaussien, le résidu du filtre
à 100/100 est décomposé dans ses axes I/Co/Cg puis pondéré par les réglages
issus de la barre maître 1,25,50,85,100. Le résultat est comparé au filtre
réel exécuté séparément à chacune de ces forces. Écart absolu maximal :
**3,73×10⁻⁹** en RGB caméra. Il n'y a ni cache implémenté, ni bit-exactitude
revendiquée, ni essai photo, ni qualification de poids spatiaux.

Cette quasi-linéarité est plausible pour des forces globales positives qui
conservent les cinq échelles actives : les estimations et portes structurelles
ne dépendent pas de la force. Elle ne garantit rien pour le réglage avancé
couleur=0, qui peut changer les échelles actives. À zéro, garder une identité
stricte et ne pas calculer inutilement le résultat maximal.

**Attention au passage du global au local** : un résidu de moyenne globale
nulle ne garde pas nécessairement cette moyenne après pondération spatiale.
La sélection des poids sur un signal bruité peut ajouter un biais, modifier
les noirs ou produire des halos. Le témoin ne valide pas ce passage.

### 39.4. Architecture proposée, pas intégrée

- Conserver une source caméra immuable et son estimation de bruit ; ne pas
  réestimer σ à partir de l'image éclaircie ou déjà filtrée.
- Qualifier d'abord la réutilisation globale du résidu sur des photos, tailles,
  proxies et forces distincts. Une séparation de cache avant LCP/DCP pourrait
  ensuite éviter le filtre lourd à chaque déplacement tonal ; rendu aval et
  mélange O(N) restent nécessaires. Annuler les rendus périmés et temporiser
  les mises à jour, sans filtrage itératif de l'aperçu précédent.
- Identifier exactement source décodée/calibrée, dimensions, pixelScale,
  contexte et version du filtre/échelles. Les réglages qui changent cette
  source invalident le cache ; ne pas utiliser seulement le nom du fichier.
- Admettre ce cache dans le contrôleur mémoire borné existant, avec réservation
  du pic et éviction. Un résidu RGB f32 de 32 MP vaut environ 384 Mo, contre
  environ 34 Mo pour un aperçu 2048×1365. Pas de collection illimitée par force.
- Étudier un guide déterministe et lissé pour le gain tonal, sans boucle de
  rétroaction. L'exposition RAW avant courbe doit être incluse dans le vrai
  opérateur ; ne pas transposer les nombres de la sonde raster au RAW.
- Proposer initialement une adaptation explicite et désactivable, pas un
  renforcement caché. La barre maître reste le budget de force : zéro identité,
  maximum 100 et état avancé personnalisé conservés. À 100, il n'existe pas
  de marge de renforcement dans l'interpolation actuelle : il faudrait améliorer
  l'algorithme, pas dépasser la borne ou gonfler artificiellement σ.

### 39.5. Épreuve suivante et décision

Piste pertinente **à étudier**, pas prête pour intégration. Qualifier d'abord
le résidu réutilisable globalement puis la propagation dans le vrai DCP.
Comparer ensuite, à recette finale identique, force fixe et adaptation sur
scènes sombres éclaircies, témoins à signal connu et texture faible propre
du §38. Masques gelés indépendants, détail, halos, moyenne des noirs,
monotonie, latence et pic mémoire sont nécessaires ; aucune baisse de bruit
visible ne justifie seule de lisser les murs ou d'inventer une calibration.

Pas de nouveau RAW ni de réserve décodés, aucun TIFF généré. Cible native
`denoiseTonalGainProbe`, lanceur `study_tonal_noise_gain.py`, calcul bref
terminé. Aucun fichier moteur, IHM, profil ou réglage livré modifié ; aucune
fusion dev, publication ou création de tag. La protection structurelle reste
une cible complémentaire : le gain tonal ne résout pas la confusion texture/bruit.

## 40. Structures faibles : contamination des témoins d'estimation — 30 octobre 2026

### 40.1. Priorité et non-répétition

À la demande de l'utilisateur, reporter l'adaptation au gain tonal : ni
correction du décodage ni prérequis à la chaîne d'ouverture, mais optimisation
future de rendu et de réactivité. Priorité au détail faible. Avant tout
nouveau modèle, les anciens essais `patch_noise` sont relus : les§249–250
(rang collaboratif/SVD) et§273–274 (covariance de groupe/Wiener) ont déjà
montré un compromis défavorable entre petites formes et bruit/contours.
Ils ne sont pas relancés sous un nouveau nom.

Question ciblée : le filtre actuel attribue-t-il du bruit à des structures
dont on sait qu'elles sont propres ? Puis, la même structure locale reçoit-elle
le même traitement lorsque seuls les témoins distants changent ? Pas de
nouveau réglage, de filtre alternatif ou de seuil optimisé sur ces réponses.

### 40.2. Premier diagnostic figé

Preuves :
[`gel`](../raw/weak-structure-estimator-freeze-v001-20261030T132918Z.json),
[`mesures`](../raw/weak-structure-estimator-v001-20261030T132918Z.json).
Trente-deux cas : horizontal, vertical, diagonal, produit sinusoïdal ;
périodes16/32, phases0/0,73, amplitudes0,0015/0,003, moyenne0,03.
RGB caméra synthétique équilibré,256² ; ROI centrale128². États propres
barre0/25/100 et bruités0/100. Observer les résumés du vrai module : témoins,
support, σI global, nœuds conditionnels, chaque échelle.

Bruit ajouté : une même réalisation gaussienne achromatique, noyau binomial
séparable3, σ échantillonnal0,002. **Ce n'est pas le bruit physique du capteur
ou la covariance issue de RCD**, ni un essai indépendant par cas. Pas de CFA,
DCP, photo, RAW réservé ou référence LR relu. Le diagnostic propre ne dépend
pas de la validité de ce modèle de bruit synthétique.

| Motif | Cas | Cas propres avec σI estimé non nul | Gain du motif propre à100, min–max |
| --- | ---: | ---: | ---: |
| Horizontal | 8 | 8 | 0,662–0,675 |
| Vertical | 8 | 8 | 0,662–0,675 |
| Diagonal | 8 | 8 | 0,184–0,664 |
| Produit | 8 | 8 | 0,356–0,771 |

Sur les32 cas, à25 le gain propre va de0,796 à0,943 ; zéro conserve exactement
la source. Un aplat propre reste identique. Sur l'aplat bruité, l'erreurRGB
passe de3,919×10⁻⁶ à3,961×10⁻⁷ à100 : diminuer toute la force pour conserver
le détail sacrifierait une efficacité réelle, même dans ce témoin limité.
Une MSE basse sur les cas bruités ne suffit donc pas à valider le traitement.

Le σI estimé est ici contaminé par construction : il n'existe aucun bruit
dans les entrées propres. Cela ne prouve pas que l'étage d'estimation explique
seul toute l'atténuation, ni qu'une dispersion analogue est une texture sur
une photo. Le mode observateur n'injecte pas un modèle oracle dans le filtre.

### 40.3. Contrôle de contexte, sans changement du détail local

Après ce premier constat, un second protocole est figé avant calcul :
[`gel de contexte`](../raw/weak-structure-context-freeze-v001-20261030T133137Z.json),
[`mesures de contexte`](../raw/weak-structure-context-v001-20261030T133137Z.json).
Pour chaque motif, période16, phase0, amplitude0,0015 ; comparer sa version
256² à la même image entourée de256px propres constants, soit768².
ROI centrale96², avec80px de même signal de chaque côté, au-delà de la marge
maximale65 du filtre. Le code vérifie l'identité des pixels locaux.
Les régions distantes d'estimation, le découpage en tuiles et le bilanDC global
changent : ce n'est pas l'isolation d'une unique fonction interne.

| Motif propre | Gain original | Gain avec entourage uniforme propre |
| --- | ---: | ---: |
| Horizontal | 0,6743 | 1,0000 |
| Vertical | 0,6743 | 1,0000 |
| Diagonal | 0,1840 | 1,0000 |
| Produit | 0,3580 | 1,0000 |

Avec l'entourage uniforme, les σI globaux et les nœuds sont nuls à toutes
les échelles ; le détail est conservé. Sur les motifs pleins, plusieurs
échelles reçoivent des σI non nuls. Exemple horizontal :25/25 candidats aux
deux premières échelles, σI≈0,000117 et0,000395, alors que la source est propre.
Le noyau de lissage seul n'impose donc pas la perte constatée ; le modèle
estimé et son contexte font partie du problème. **Ne pas généraliser cette
réponse à une photo bruitée**, où un entourage uniforme contient aussi du bruit.

### 40.4. Lecture du mécanisme et prochaine intervention

Le rejet actuel de témoins cohérents demande une corrélation absolue>0,9
dans les DEUX axes, à un seul retard lié à l'échelle. Une structure périodique
propre peut manquer ce critère selon sa période et son orientation ; les
résumés ci-dessus confirment que de tels témoins sont effectivement admis.
Les deux orientations cardinales donnent des résultats proches ; le défaut
n'est pas une simple inversion x/y. Les différences diagonales ne doivent
pas être corrigées par des poids de teinte ou par un réglage de DCP.

Prochaine piste bornée : **séparer structure spatialement prévisible et
dispersion résiduelle avant d'accepter un témoin de bruit**. Évaluer la
prédiction hors des positions d'ajustement, dans plusieurs directions/retards
fixés avant mesure ; comparer aux corrélations attendues du bruit réel après
RCD, pas seulement à du bruit blanc. Le but n'est ni de garder toute
autocorrélation comme détail, ni de remplacer simplement le ET par un OU
au seuil choisi sur ces32 motifs. Risque inverse : sous-estimer le bruit,
s'abstenir partout et ne plus débruiter les vrais aplats.

Critères nécessaires avant promotion : témoins propres conservés, bruit
plat réellement diminué, contrôles de petits détails uniques/contours, noirs
et sources à bruit connu déjà authentifiées. Les références du §38 restent
des cas de développement ; ne pas les appeler validation intacte. Pas de
balayage opportuniste de seuils ni de reprise d'un modèle déjà rejeté sans
différence structurelle explicite.

Cette étape ferme un diagnostic, pas l'étude de protection. Aucun fichier
moteur/profil/IHM modifié, aucune publication, fusion dev ou création de tag.
Les calculs sont courts et terminés ; preuves JSON immuables et sources
de banc conservées. Aucun besoin de nouvelles photos ni d'intervention
de Claude pour ce jalon.

## 41. Prédiction spatiale hors ajustement et variance résiduelle — 30 octobre 2026

### 41.1. Question, données et gel

Suite du§40 : peut-on reconnaître la composante prévisible d'une structure
faible, et estimer le bruit dans le résidu plutôt que dans la texture ?
Il s'agit d'une étude de certification de témoins, **pas d'un nouveau filtre**.
Les anciens modèles de faible rang ne sont pas rejoués : ici un prédicteur
linéaire local est ajusté et évalué dans deux régions séparées, avec un contrôle
explicite de la covariance du bruit du pixel cible et de ses voisins.

Preuves canoniques :
[`gel initial`](../raw/structure-prediction-freeze-v001-20261030T134050Z.json),
[`prédiction`](../raw/structure-prediction-v001-20261030T134050Z.json),
[`gel à bruit égal`](../raw/structure-prediction-matched-freeze-v001-20261030T134222Z.json),
[`contrôle à bruit égal`](../raw/structure-prediction-matched-v001-20261030T134222Z.json).

Seules les captures signées déjà authentifiées des quatre noirs de
développement du§36 sont relues :0003/0004 à1600ISO et9950/9951 à51200ISO,
crop central256² avant DCP. Capture/provenance et binaire sont vérifiés,
partitions conservées, pas de RAW nouvellement décodé ni réserve consultée.
L'intensité est `I=0,25R+0,5G+0,25B`, pas une luminance photométrique.
Les niveaux noirs2048, blanc16383 et gains du pilote restent explicites :
pas de nouvelle calibration de production.

Seize voisins : huit directions, distances4 et8pixels. Ajustement sur
`[16,112)²`, contrôle sur `[144,240)²` ; aucune empreinte de voisinage commune,
ce que vérifie un test. Cela ne rend pas indépendants des motifs fixes ou
des corrélations longue portée. Prior spatial : quadrant d'ajustement de
l'AUTRE noir de développement ; deux directions par paire.

Deux prédicteurs partagent le même système régularisé :

`A = Cov(X,X) + 0,01 Cov(NX,NX) + ε Id`.

Le membre droit ordinaire est `Cov(X,Y)` ; celui corrigé est
`Cov(X,Y) − Cov(NX,NY)`. Le second retire la prévisibilité du bruit de la
cible, sans soustraire la covariance d'observation au dénominateur. Le plancher
numérique relatif est10⁻⁸ de la variance moyenne des voisins. Les fonctions
d'ajustement n'acceptent ni vérité connue ni pixels de contrôle.

Par direction : un aplat et16 motifs (quatre orientations, périodes16/32,
amplitudes0,0015/0,003, phase0,37, moyenne0,03), soit68 cas au total.
Le résidu réel du noir, non recentré comme image, est ajouté au signal
synthétique. Les covariances sont centrées comme statistiques, pas une
correction de DC du signal. Ce rejeu **ne reproduit pas** le bruit photonique
ou la dépendance au signal du CFA/RCD. Quatre prises, pas68 photos indépendantes.

### 41.2. Prévisibilité : limite de rapport signal/bruit, pas loi par ISO

Au niveau réel du pilote, σI du bruit de contrôle vaut environ0,000404 à1600,
contre0,0114–0,0116 à51200. Le rapport variance du motif / variance du bruit
va de3,44 à27,55 dans le premier cas, mais de0,0042 à0,0344 dans le second.
Il serait faux d'attribuer directement l'écart de prédiction au boîtier/ISO.

| Rejeu | Variante | Gain médian du signal prédit,1600 | Gain médian,51200 |
| --- | --- | ---: | ---: |
| Bruit du pilote | Ordinaire | 0,985 | 0,146 |
| Bruit du pilote | Covariance cible corrigée | 0,984 | 0,085 |
| Bruit égal,σ d'ajustement0,002 | Ordinaire | 0,730 | 0,733 |
| Bruit égal,σ d'ajustement0,002 | Covariance cible corrigée | 0,721 | 0,714 |

Le contrôle à bruit égal est gelé après la première mesure. Le niveau0,002
reprend celui du§40, pas un niveau choisi pour faire gagner une variante.
Chaque résidu et son prior sont multipliés par un facteur issu de LEUR quadrant
d'ajustement ; facteurs conservés par cas. C'est un contrôle diagnostique
non physique, pas une photographie à un autre ISO.

Les scores de covariance prédictive corrigée des textures et des aplats se
séparent dans ce petit banc à1600, mais se recouvrent à51200 au bruit réel.
À bruit égal, les deux distributions ISO deviennent proches. La correction
du membre droit ne domine pas la régression ordinaire ; ne pas la promouvoir.
Les quelques millièmes de variance de bruit présents dans la prédiction ne
sont **pas** une mesure de débruitage final : aucune image filtrée n'est proposée.
La bonne réponse sur des sinusoïdes ne garantit ni un détail unique ni un mur.

### 41.3. Rejeu des poids gelés : estimer le bruit dans le résidu

Premier audit conservé :
[`v001`](../raw/structure-residual-v001-20261030T134436Z.json).
La v002 ajoute une formule utilisant seulement la forme normalisée de la
covariance du prior ; ni poids ni prédictions ne sont réajustés :
[`gel v002`](../raw/structure-residual-freeze-v002-20261030T134552Z.json),
[`audit v002`](../raw/structure-residual-v002-20261030T134552Z.json).
Les MSE rejouées doivent retrouver celles enregistrées dans les deux parents.
272 observations méthode/cas, pas272 acquisitions indépendantes.

Pour `r=Y−wᵀX`, la compensation soustractive est :

`σ² ≈ Var(r) − wᵀ Cov(NX,NX) w + 2 wᵀ Cov(NX,NY)`.

Elle utilise l'amplitude du prior. En normalisant les covariances du prior
par sa variance cible, on obtient `Cxx` et `cxy`, et une seconde possibilité :

`σ² ≈ Var(r) / [1 + wᵀ Cxx w − 2 wᵀ cxy]`.

La seconde formule de résidu n'utilise plus l'amplitude du prior. **Toute la
chaîne n'en est pas indépendante** : les poids gelés ont encore été ajustés
avec cette amplitude dans la régularisation, et la variante corrigée dans
son membre droit. L'information privilégiée externe n'a donc pas disparu.

Le résidu contient aussi l'erreur de prédiction du signal et un terme croisé
signal/bruit. Pas de garantie universelle d'absence de biais. Les termes
oracle utilisant le bruit de contrôle réellement injecté sont conservés pour
le diagnostic seulement ; ils ne servent pas au calcul opérationnel proposé.
Les estimations négatives ou les dénominateurs invalides restent explicites,
sans borne silencieuse ; aucun cas de ce type dans ce petit rejeu.

Pour la variante ordinaire, formule à forme normalisée :

| Source du bruit | σ estimé / σ injecté, min–médiane–max |
| --- | ---: |
|1600, amplitude du pilote | 0,994 – 1,003 – 1,017 |
|51200, amplitude du pilote | 0,999 – 1,004 – 1,013 |
|1600, normalisé0,002 | 0,998 – 1,012 – 1,033 |
|51200, normalisé0,002 | 0,999 – 1,011 – 1,031 |

À1600 non normalisé, la dispersion brute de l'observation atteint jusqu'à
5,37fois le σ du bruit parce qu'elle contient le signal. Le σ du résidu
ordinaire non compensé est encore surestimé jusqu'à12,3 % ; la géométrie
du prédicteur transmet elle-même du bruit et justifie la compensation.
La dispersion brute n'est **pas** le MAD multi-échelle de production :
ce tableau n'est pas une victoire mesurée sur l'estimateur livré.

À51200, bien estimer le bruit ne signifie pas récupérer le faible détail :
sa variance est très petite devant le bruit, donc l'estimation peut rester
juste tout en ne prédisant presque aucune structure. Ne pas confondre ces
deux objectifs, ni recommencer à optimiser uniquement un estimateur interne.

### 41.4. Décision et épreuve suivante

**Piste utile pour désengager l'estimation de la texture régulière**, pas
encore de témoin certifié ni d'amélioration de rendu intégrable. Aucun seuil
de détection n'est choisi dans les écarts observés. Les deux noirs par ISO
et les motifs sont désormais des données de développement consultées.

Prochaine étape bornée : ajuster le prédicteur sans amplitude de bruit externe,
garder séparée la forme de corrélation, puis éprouver des détails uniques,
contours, textures irrégulières et perturbations de ce prior. Un estimateur
qui ne marche qu'avec le niveau exact d'un autre noir ne résout pas l'ouverture
d'une photo seule. Comparer ensuite au MAD multi-échelle ACTUEL et au filtre
réel à force identique ; il faudra conserver les structures propres sans
abandonner les vrais aplats bruités. Pas de nouveau balayage de forces.

Six contrats Python passent : séparation des empreintes, prédiction propre
calculable, non-fuite du contrôle, non-finitude, compensation scalaire et
invariance de la formule normalisée à l'amplitude du prior. Calculs courts,
pas de budget pleine image qualifié. Aucun moteur/profil/IHM modifié ;
aucun export, push, fusion dev, tag ou nouvelle photo demandés.

## 42. Épreuve sans amplitude externe : détails uniques et prior imparfait — 30 octobre 2026

### 42.1. Hypothèse et protocole figé

Suite autorisée du§41 : supprimer l'amplitude de bruit externe du prédicteur,
éprouver des détails non périodiques et une forme de bruit inexacte, puis
confronter au module actuel. Un bon σ sur des sinusoïdes ne suffit pas.

Preuves :
[`gel`](../raw/structure-amplitude-free-freeze-v001-20261030T135620Z.json),
[`mesures`](../raw/structure-amplitude-free-v001-20261030T135620Z.json).
Le même ensemble de quatre captures de noirs de développement authentifiées
est réutilisé, aucun RAW ou jeu réservé décodé. Les niveaux explicites du
pilote et son absence de calibration de production restent ceux du§36.

Le prédicteur utilise uniquement `Cov(X,X)` et `Cov(X,Y)` de l'observation
d'ajustement : ridge fixé à `10⁻⁴ trace(CovXX)/16`, plancher numérique10⁻²⁰.
La fonction d'ajustement ne reçoit ni prior, ni vérité, ni contrôle. La
formule du résidu utilise la forme normalisée de corrélation : l'amplitude
externe n'entre désormais dans AUCUNE de ces deux étapes. Les facteurs de
normalisation des bruits du banc ne sont pas des paramètres du prédicteur.
La forme externe du bruit, elle, reste nécessaire.

Huit cas prévus : aplat, produit sinusoïdal sentinelle, textures irrégulières
fines/grossières, contour absent de l'ajustement, ligne unique2px, petite
tache gaussienneσ2px et contour présent dans les deux régions. Textures
gaussiennes aléatoires lissées par noyauxσ0,8/3, graines74042/74043 fixes,
amplitude RMS0,0015 ; détails uniques d'amplitude0,003. La tache n'est pas
un pixel impulsionnel. Ajustement et contrôle conservent les empreintes
disjointes du§41 ; les motifs uniques sont volontairement absents de
l'ajustement, ce qu'un contrat vérifie.

Deux directions par paire de noirs, deux ISO, amplitude du pilote et contrôle
à σ d'ajustement0,002 : **64 cas**, pas64 acquisitions indépendantes. Chaque
cas est analysé en intensité caméra et dans ses trois plansB3 de luminance
(pas des plans couleur). Noyau actuel `[1,4,6,4,1]/16`, pas1/2/4, réflexion101.
Le calcul Python f64 reproduit le support mathématique, pas les arrondis
du C++ f32 au bit près. Les références de forme sont l'autre noir du même
ISO, sa rotation90°, un noir de l'autre ISO et une covariance blanche.

La covariance blanche est ici une indépendance supposée des COEFFICIENTS
du plan analysé (`Cxx=Id,cxy=0`), **pas** un bruit caméra blanc propagé à
travers B3. C'est délibérément une référence inadéquate aux échelles corrélées.
256 observations cas/plan et1 024 évaluations de forme, pas autant de photos.

### 42.2. Comparateur réel et unités

La sonde `structureBaselineProbe`, liée au `CameraDenoise.cpp` figé du banc,
reçoit les pixels f32 en mémoire via stdin JSON/base64. Pas de fichier pixel
ni TIFF. Pour chaque cas, elle applique réellement100/100 à l'observation
et au témoin propre, soit128 exécutions du module. Les entrées sont
achromatiques, RGB équilibré signé avant LCP/DCP ; aucune qualification
de chroma ou de rendu DCP n'en découle.

Mesures au contrôle96² : MSE et gain du motif. L'observateur conserve les
modèles, témoins et nœuds ; son σI global n'est pas le modèle conditionnel
effectivement routé en chaque pixel. Le comparer à l'écart-type du bruit
connu est descriptif, pas une preuve que le MAD doit être corrigé par un
facteur uniforme. Le bruit réel de noir contient aussi queues de distribution
et motifs fixes ; MAD gaussien et écart-type ne sont pas interchangeables.

Le bruit injecté est le résidu de noir postRCD ajouté au signal synthétique
puis arrondi en f32. Cela n'est toujours pas une génération CFA dépendante
du signal. Les empreintes des pixels reçus par la sonde et les contextes
de capture parents sont conservés dans les preuves.

### 42.3. Résultat : succès sur motifs réguliers, échec comme estimateur général

À1600ISO, amplitude du pilote, médiane des deux directions ; ratio σ estimé
au σ réellement injecté, forme de bruit du même ISO :

| Motif | Ratio σ au premier planB3 | Ratio σ au troisième planB3 | Gain du témoin propre, filtre actuel100/100 |
| --- | ---: | ---: | ---: |
| Aplat | 1,000 | 1,021 | — |
| Sinusoïde | 1,021 | 1,021 | 0,377 |
| Texture irrégulière fine | 2,733 | 5,835 | 0,232 |
| Texture irrégulière grossière | 1,053 | 2,439 | 0,704 |
| Contour nouveau | 1,090 | 3,198 | 1,000 |
| Ligne unique | 1,098 | 1,424 | 1,000 |
| Petite tache | 1,002 | 1,144 | 1,000 |
| Contour déjà rencontré | 1,027 | 1,023 | 1,000 |

Retirer l'amplitude externe n'a donc pas détruit la réussite sur la
sinusoïde, mais celle-ci ne se généralise pas à la texture irrégulière.
La partie imprévisible du signal reste dans l'erreur de prédiction et est
interprétée comme du bruit. Le changement de contenu entre ajustement et
contrôle explique aussi la différence entre contour nouveau et déjà rencontré.
Le modèle global ne sait pas certifier une nouvelle structure locale.

Le filtre actuel conserve les petites formes propres de ce banc. Sur la
ligne bruitée à1600, son gain observé est0,945–0,957 ; sur la tache bruitée,
0,886–0,899. Ces valeurs ne sont ni une moyenne photographique ni une
qualité visuelle qualifiée. Elles empêchent néanmoins de présenter le
problème comme une destruction systématique de tout petit détail.

Pour la texture fine, au premier planB3, le σ global observé du module vaut
environ2,679fois l'écart-type du bruit injecté, contre2,733 pour le candidat.
Le candidat ne démontre donc pas de sortie de cette confusion à cette échelle.
Ces deux statistiques ne sont pas identiques par construction ; ne pas
transformer leur proximité en preuve d'équivalence de rendu.

À51200 au bruit du pilote, le ratio maximal candidat au premier planB3
n'est que1,007 ; après égalisation à0,002, il atteint1,142 (1,132 sur le
résidu1600 égalisé). Comme au§41, une bonne estimation à très fort bruit
peut seulement masquer la petite puissance du détail : pas une meilleure
préservation ni une propriété favorable de cet ISO.

### 42.4. Sensibilité à la forme de corrélation

Sur l'aplat1600, au troisième planB3, ratio médian σ estimé / σ injecté :
forme correspondante1,021 ; forme tournée1,009 ; autre ISO1,033 ; hypothèse
de coefficients blancs0,439. Au premier plan, le blanc vaut0,999 contre1,000
avec la forme correspondante. Il serait donc trompeur de conclure à la
robustesse au prior à partir du seul plan fin.

La géométrie du prédicteur et les filtresB3 corrèlent le bruit ; ignorer cette
propagation peut sous-estimer fortement son niveau. Rotation et transfert
entre les deux ISO changent moins ces cas, mais quatre captures d'un seul
boîtier ne qualifient ni l'invariance au boîtier, ni la BdB, ni le dématriçage,
ni la dépendance au signal. Aucun profil universel n'est revendiqué.

### 42.5. Décision et suite bornée

**Ne pas substituer cet estimateur au modèle de production et ne pas
l'utiliser comme veto universel de texture.** Aucun seuil, retard ou ridge
n'est retouché pour sauver les cas défavorables. Le résultat ne réfute pas
toute prédiction locale, mais ferme le remplacement général par ce modèle
ajusté dans une autre région et à voisins4/8.

Deux enseignements guident la suite :

- La bonne estimation du bruit doit être confrontée à des détails irréguliers,
  pas seulement aux motifs prédictibles. Garder ces sentinelles dans les
  prochaines épreuves ; la précision moyenne de σ ne sera plus un critère seul.
- Une référence de bruit physiquement ancrée et sa propagation jusqu'aux
  plansB3 pourraient empêcher de gonfler σ avec la texture. Comparer un
  modèle de bruit externe connu à l'estimation aveugle, à force inchangée,
  afin de déterminer si le facteur limitant est l'estimation ou le filtrage.
  Le signed RAW actuel rend cette question plus pertinente que les anciens
  essais réalisés avant suppression des bornes. Ne pas recopier directement
  le σ des noirs sur toute luminosité : photons, gains et covariance aprèsRCD
  restent à caractériser. Tout raccord d'injection dans le moteur sera
  contractualisé avec Claude avant code ; aucun raccord n'est réalisé ici.

Cette dernière comparaison est une borne expérimentale/oracle, pas une
solution déployable avec une vérité inconnue ni une demande de nouvelles
photos. Réutiliser d'abord le corpus de développement déjà authentifié.
Le gain tonal reste différé ; la production conserve son comportement.

Les six contrats spécifiques couvrent l'invariance d'amplitude de la forme,
l'absence de fuite du contrôle, le rejet non fini, les motifs uniques absents
de l'ajustement, les plans constants et les priors/dénominateurs invalides.
Campagne terminée en25,3s, pas budget pleine image ni latence IHM qualifiés.
Aucun fichier moteur/profil/IHM modifié, pas de push, fusion dev ou tag.

## 43. Même filtre, même force : modèle aveugle contre modèle de bruit seul — 30 octobre 2026

### 43.1. Contrat et parité de la sonde

Comparaison autorisée par l'utilisateur après le§42. Le contrat est écrit
au journal partagé AVANT code : aucun fichier moteur ou interface publique
modifié. Une génération mécanique produit, dans le build du banc seulement,
`CameraDenoise.cpp` courant avec un hook unique après `estimate()` et avant
la recherche de bruit mesuré. Le filtrage qui suit est inchangé. Le marqueur
est unique et le générateur échoue si cette frontière change ; pas de copie
de filtre maintenue manuellement ni de seconde implémentation de production.

Preuves :
[`gel de comparaison`](../raw/noise-model-ablation-freeze-v001-20261030T141237Z.json),
[`mesures`](../raw/noise-model-ablation-v001-20261030T141237Z.json).
Les octets des sources, le source généré complet, les outils et les réglages
sont identifiés. Le SHA du commentaire d'en-tête généré concerne le texte
normaliséUTF-8/LF ; l'autre SHA canonique concerne les octets du fichier
d'origine, dont les fins de ligneCRLF. Ces empreintes ne sont pas censées
être égales. Aucun fichier de Claude n'est édité dans ce raccord privé.

La sonde instrumentée et la sonde liée au module ORIGINAL partagent le même
code de mesure. Contrôles terminés :

- 64/64 paires de payloads observé/propre identiques aux entrées du§42 ;
- 128/128 empreintesSHA256 de sorties complètes identiques en mode aveugle
  entre les deux modules,64 bruitées et64 propres ;
- 64/64 identités exactes du témoin propre avec un modèle explicitement nul,
  malgré la force100/100. Il s'agit d'un contrat, pas du réglage utilisateur0.

Les paramètres ne sont pas retouchés : force100/100, même image, même code
de filtrage, mêmes marges/portes/bilanDC. La comparaison reste entièrement
en mémoire, sans exportTIFF. Les images sources de développement et les
limites du rejeu postRCD achromatique du§42 restent les mêmes.

### 43.2. Ce que signifie ici « bruit connu »

Le bruit réellement ajouté est disponible parce que le signal synthétique
est connu. La sonde applique le MÊME estimateur MAD/covariance à une entrée
constante0,03 plus ce bruit seul (`observé−propre+0,03`, arrondisf32 conservés),
puis fige ses modèles globaux dans le filtre de l'observation complète.
Ce modèle est débarrassé de la texture synthétique, mais reste une estimation
robuste : **pas la covariance physique exacte, ni un optimum mathématique**.

Quatre voies sont comparées :

1. Modèle actuel complet, routage conditionnel conservé.
2. Modèle actuel limité au global (`pooled`) : contrôle de l'effet du routage.
3. Modèle global capturé sur le même bruit seul : référence privilégiée.
4. Modèle global capturé sur l'AUTRE noir de la même paire : approximation
   indépendante de la réalisation injectée, même facteur d'échelle du banc,
   sans normalisation à la variance de la réponse attendue.

Comparer3 à2 isole mieux la contamination du modèle que comparer seulement3
à1, qui change aussi son routage. L'indépendance de réalisation de4 ne signifie
pas indépendance de séance ou de motif fixe du capteur.

Les témoins propres reçoivent aussi le même modèle externe NON NUL que les
observations bruitées : c'est un stress de transfert du détail à budget fixé,
pas un cas réellement sans bruit qui devrait recevoir ce prior. Le contrat
de modèle nul reste distinct. Les résumés de l'observateur sont émis AVANT
le hook ; le modèle effectivement rejoué est le global du run de capture
nommé dans les mesures, pas le résumé aveugle du run après substitution.

### 43.3. Résultat1600 : contamination de l'estimation effectivement limitante

Au bruit du pilote1600, médianes des deux directions ; erreurRGB au signal
connu, ratio au module actuel complet. Un ratio<1 signifie une erreur moindre :

| Motif | Modèle du même bruit seul / actuel | Modèle de l'autre noir / actuel | Gain propre actuel | Gain propre au budget du même bruit |
| --- | ---: | ---: | ---: | ---: |
| Aplat | 0,983 | 0,981 | — | — |
| Texture irrégulière fine | 0,107 | 0,107 | 0,232 | 0,945 |
| Texture irrégulière grossière | 0,265 | 0,265 | 0,704 | 0,927 |
| Sinusoïde | 0,312 | 0,311 | 0,377 | 0,838 |
| Contour nouveau | 1,040 | 1,038 | 1,000 | 0,993 |
| Ligne unique | 1,024 | 1,022 | 1,000 | 0,948 |
| Petite tache | 0,985 | 0,982 | 1,000 | 0,859 |
| Contour déjà rencontré | 1,130 | 1,127 | 1,000 | 0,993 |

Sur la texture fine, le gain d'erreur n'est pas expliqué par le seul passage
au global : modèle aveugle global / actuel≈0,998, puis modèle du bruit seul /
aveugle global≈0,107. Pour la texture grossière, ces rapports valent1,098
puis0,241 ; pour la sinusoïde1,444 puis0,216. Le routage actuel protège donc
déjà certains cas mieux que le global aveugle, sans éliminer la contamination.

Exemple texture fine, première direction1600 : σI global aveugle par échelle
commence à0,000839,0,000583,0,000319. Sur le même bruit seul :0,000237,
0,000107,0,0000507 ; sur l'autre noir :0,000235,0,000106,0,0000513.
La proximité des deux profils de bruit seul est mesurée, pas obtenue par
recalage sur la réponse attendue. Les différences de modèle peuvent donc
expliquer une part importante de la perte de texture dans ce banc.

**Point concret : le filtre actuel est capable de bien mieux conserver ces
textures si son modèle n'absorbe pas leur énergie.** Le prior externe réduit
l'erreur fine d'environ89 % dans ces deux cas1600, mais ce n'est pas un gain
mesuré sur des photos ni une preuve de meilleure esthétique. Il ne justifie
pas de supprimer le routage conditionnel ni de réduire toute force globalement.

### 43.4. Limites : contours, fort bruit et égalisation

Les contours et la ligne peuvent régresser, jusqu'à environ13 % de MSE
relative sur le contour déjà rencontré. Le modèle constant de bruit seul
n'est donc pas un remplacement universel. Une petite variation absolue dans
un cas déjà bien traité peut produire un ratio non négligeable ; conserver
les MSE absolues par cas dans le JSON, pas seulement ces rapports.

Au bruit réel du pilote51200, le modèle du même bruit seul / actuel vaut
environ1,039 pour la texture fine,1,034 pour la grossière et1,029 pour la
sinusoïde. Le détail y est très faible devant le bruit : un modèle moins
contaminé n'assure pas une amélioration du filtre dans ce régime. Ne pas
confondre résolution d'un biais d'estimation et récupération d'information.

À amplitude égale0,002, les résultats sont mixtes : modèle du même bruit seul /
actuel, texture fine1,057 sur le résidu1600 contre0,897 sur celui51200 ;
texture grossière0,927 contre0,842. Le contour déjà rencontré régresse
d'environ5–6 % dans les deux cas. Cela confirme la nécessité de qualifier
la forme du bruit et le compromis de filtrage, pas de choisir un coefficient
ISO qui reproduirait une seule ligne du tableau.

Les quatre noirs, les motifs et leurs deux directions sont déjà du
développement. L'autre noir n'est pas une nouvelle validation indépendante.
Le profil ne modélise ni le bruit photonique à toutes luminosités, ni tous
les gainsBdB, ni la covariance d'un nouveau boîtier aprèsRCD. Le transporter
tel quel à une photo claire ou à un autre appareil serait injustifié.

### 43.5. Décision et suite

La comparaison confirme la priorité : **empêcher la texture de gonfler le
modèle de bruit**, plutôt que remplacer d'abord tout le filtre. Ne pas
intégrer le modèle global de ces noirs dans la production. Les voies de
bruit seul restent une borne expérimentale, pas une option utilisateur.

Prochaine préparation utile : un modèle de bruit caractérisé et dépendant
du signal, puis sa propagation/qualification jusqu'aux coefficients du
débruitage. Utiliser d'abord les prises noires/éclairées de développement
déjà disponibles et auditer ce qu'elles permettent effectivement ; ne pas
inventer les paramètres manquants. Garder les contrôles de textures, contours,
noirs et routage conditionnel. L'objectif n'est pas seulement unσ exact,
mais une amélioration sur l'image à force identique sans sacrifier les cas
actuellement conservés. Toute interface moteur déployable sera séparément
convenue avec Claude ; aucun changement moteur n'est demandé par ce jalon.

Quatre contrats Python du générateur/payloads/réducteur passent. Campagne
terminée en127,4s,832 appels au module en incluant captures, témoins et
parités ; ce n'est pas un budget de rendu applicatif. Aucun filtre de
production, profil, interface publique ou IHM modifié, pas de publication,
fusion dev ou tag. Pas de nouvelle photo requise pour cette comparaison.

## 44. Audit de la caractérisation CFA dépendante du signal — 30 octobre 2026

### 44.1. Réutiliser le travail existant, pas réajuster les mêmes données

Suite autorisée du§43. L'audit retrouve dans le rapport historique
`patch_noise`,§§455–458, une loi CFA déjà ancrée sur des noirs répétés,
ainsi qu'un transfert sur une autre séance déjà inspectée. Ces études ne
sont pas relancées sous un nouveau nom. Le nouveau travail vérifie leur
filiation, les unités et la compatibilité avec les captures signées actuelles.

Preuve canonique :
[`gel v002`](../raw/signal-noise-bridge-freeze-v002-20261030T143445Z.json),
[`audit v002`](../raw/signal-noise-bridge-v002-20261030T143445Z.json).
La v001 est conservée ; la v002 renforce les gardes de boîtier et de périmètre
ISO/phases, sans modifier les paramètres historiques. Sources parentes et
mesures utiles sont conservées avec leur provenance dans le nouvel audit.

Pas de nouveau RAW décodé, pas de référence réservée ouverte. Les pixels
relus sont seulement les quatre captures de noirs de développement déjà
qualifiées aux§§36–43. Les autres résultats sont repris des JSON canoniques,
pas d'une nouvelle vérification de disponibilité de chaque RAW de la carte.

### 44.2. Données et domaines réellement couverts

Un boîtier : **Canon EOS R6 Mark III**. La préparation repose sur26 groupes
éclairés,72 prises,312 moments (trois régions×quatre phases par groupe), et
31 noirs de développement. Les groupes de contrôle de noirs restent séparés
et ne réajustent pas les références.24pentes (six ISO×quatre phases) et
72ancrages de noir locaux (trois régions par pente), pas72 nouveaux noirs.
Toutes les scènes d'ajustement ont une durée1/200s.

La loi conservée est `v_i=b_i+a·s_i` en DN : `s_i` est la moyenne brute moins
le noir moyen local de développement, `b_i` sa variance temporelle centrée,
et `a` une pente commune par ISO/phase. L'interception n'est pas libre.
Les deux vertsG1/G2 sont distincts ; l'ordre spatialR/G1/G2/B n'est pas
l'ordre des indices couleurR/G/B/G2. Pas de conversion en électrons.

| ISO | Groupes d'ajustement | Signal rouge ajusté,DN au-dessus du noir | Signal rouge du transfert historique,DN | Mesures rouges de transfert à1/200s |
| --- | ---: | ---: | ---: | ---: |
|1600 |5 |0,77–29,81 |70,46–10 301,16 |3 |
|3200 |5 |1,53–54,89 |67,74–10 851,00 |3 |
|6400 |5 |2,97–120,92 |71,03–5 355,93 |6 |
|12800 |5 |6,81–234,67 |— |0 |
|25600 |1 |343,96–449,26 |— |0 |
|51200 |5 |24,58–941,32 |498,67–8 707,51 |0 |

Les autres phases ont leurs domaines séparés dans le JSON. Les petits
domaines d'ajustement, surtout aux faibles ISO, ne couvrent pas à eux seuls
les tons moyens et clairs. Le transfert historique apporte toutefois des
mesures plus lumineuses :464 mesures au total sur42 paires déjà étudiées,
dont48 mesures sur seulement quatre paires à la durée commune1/200s.
Il ne s'agit ni de464 acquisitions indépendantes ni d'une validation neuve.

Ne pas étendre les extrema de ce transfert en un domaine continu certifié.
Les autres durées restent des diagnostics confondus avec niveau de signal
et conditions de prise de vue. À25600, un seul groupe ne permet pas le
contrôle hors groupe. ISO100/4000/16000, autres boîtiers et interpolations
non étudiées restent sans modèle qualifié dans ce catalogue.

### 44.3. Compatibilité avec le chemin signé : ce qui est effectivement vérifié

Les mêmes quatre sources noires sont identifiées parSHA256 entre les
anciennes lecturesrawpy et les captures du développement actuel. Trois ROI
de256² par source, quatre phases :48couples moyenne/écart-type, **écarts
strictement nuls sur les deux statistiques**. Taille visible4639×6959,
origines des régions, blanc16383, motif CFA et valeurs DN entières concordent.
Cela confirme l'échelle brute de ces témoins ; ce n'est pas une nouvelle
parité de tous les pixels historiques ni une qualification des72 scènes.

Les références de noir du modèle sont MESURÉES, pas déduites des anciennes
valeurs de noir déclarées par LibRaw. Le traitement signé change la gestion
après soustraction/normalisation, pas ces statistiques brutes contrôlées.
Le socle de loi CFA peut donc être repris comme hypothèse de recherche sans
repartir d'un nouvel ajustement. Cela ne valide pas sa propagation dans RCD.

Chaque point d'ajustement du JSON de loi est également rattaché à sa scène,
sa phase/ROI et son ancrage noir :312cohérences de moments vérifiées.
La filiation du transfert aux mêmes paramètres figés est contrôlée, sans
rechoisir les points ni améliorer rétrospectivement les erreurs publiées.

### 44.4. Contrat mathématique de normalisation, avant RCD

Avec le diviseurD et le gain de phaseg du contexte explicite :

`X = g·(DN − B_runtime)/D`.

Le modèle est ancré sur `s=E[DN]−B_reference`, pas forcément sur le même
noir que le runtime. Sa conversion donne :

`Var(X) = A·E[X] + B`,

`A = g·a/D`,

`B = g²·[b + a·(B_runtime − B_reference)]/D²`.

Le carré du gain/diviseur porte sur la variance ; exprimé en fonction du
signal déjà équilibréX, le coefficientA ne porte plus qu'un seul facteurg.
Omettre la différence d'origine des noirs décale le modèle. Les contrôles
incluent une moyenne normalisée négative admissible : pas de borne à0.
Le modèle concerne l'espérance latente du signal, pas une substitution
directe de chaque pixel bruité dans une formule de variance.

Vingt-quatre témoins de normalisation (deux ISO capturés×quatre phases×trois
ROI) donnent chacun trois contrôles : ancrage noir et deux bornes du domaine,
soit72égalités entre calcul direct et coefficients normalisés. Gains,
noirs2048 et diviseur14335 sont ceux du PILOTE, pas une valeur générale
à coder pour toutes les photos. Les paramètres DN des autres ISO restent
dans leur domaine brut ; aucun contexte de normalisation non mesuré n'est inventé.

Ces coefficients sont **CFA avant dématriçage**, pas le RGB caméra de
`CameraDenoise`, et encore moins unσB3 après courbes/couleur. Les quatre
phases, RCD adaptatif, médiane, gains et corrélations doivent être transportés
ou mesurés avant tout branchement. Aucun coefficient n'est installé dans
`config`, dans un profil DCP ou dans une bibliothèque du moteur.

### 44.5. Pourquoi le noir seul ne décrit pas une zone éclairée

Exemple de calcul, PAS mesure sur photo : pour une espérance CFA équilibrée
de0,03, la loi figée prévoit à1600 un rapportσtotal/σnoir de4,45–4,56 en
rouge,6,15–6,29 sur les verts et4,54–4,58 en bleu selon la ROI. Ce point
est hors du domaine d'ajustement1600 ; les mesures historiques de transfert
plus lumineux constituent seulement un appui diagnostique, pas une validation
continue de cette évaluation. À51200, les mêmes calculs sont dans le domaine
ajusté : environ1,29–1,52 selon phase et ROI.

Ce ne sont pas des rapports de bruit RGB ou affiché aprèsRCD/DCP. Ils
illustrent surtout pourquoi les§§38–43 sont explicitement des ablations
à bruit de lecture seul : leur signal moyen0,03 ne les transforme pas en
photos physiquement bruitées avec le terme dépendant du signal attendu.
Ne pas transporter directement le profil noir du§43 aux zones éclairées.

Autre distinction : `b_i` est une variance TEMPORELLE centrée, tandis que
le résidu d'une image noire peut porter une structure spatiale commune.
Les JSON historiques en gardent des diagnostics, sans l'identifier sûrement
comme bruit fixe capteur. Une simulation qui reprend un vrai résidu noir
ne doit pas lui ajouter une seconde fois le bruit de lectureb ; il faut
traiter séparément le terme de signal et les composantes communes.

### 44.6. Décision et étape suivante

**Base de recherche réutilisable, pas profil de production qualifié.** Le
catalogue conserve par boîtier/ISO/phase les pentes, ancrages locaux, domaines,
preuves et limites. Pas de moyenne globale des troisb_i, pas d'extrapolation
ISO ou de déduction d'un rendement électronique. La variance seule ne décrit
pas la corrélation ni les queues du bruit qui affectent le filtre.

La prochaine épreuve peut maintenant porter sur la propagation du terme
dépendant du signal : modèle figé, scènes synthétiques à signal connu,
bruit appliqué dans le CFA avant le RCD signé/médiane actuels, puis moments
et covariances des trois plansB3. Comparer lecture seule et terme de signal
sans compter deux foisb, conserver la distinction entre source brute et
composante commune, et séparer simulation contrôlée et vérité physique.
Le tout reste sur le banc, sans étendre à toute photo un profil calibré sur
quelques prises. Les données actuelles suffisent à préparer cette épreuve ;
aucune nouvelle prise ni export utilisateur demandé à ce jalon.

Cinq tests vérifient transport affine/origine noire, gain simple surA et
quadratique surB, moyenne signée, indices des verts et contexte invalide.
Aucun paramètre réajusté, aucune production/profil/IHM modifiés ; aucun
push, fusion dev ou tag. Les réserves restent fermées.

## 45. Propagation du budget CFA dans RCD signé et B3 — 30 octobre 2026

Preuves : [mesures complètes](../raw/cfa-signal-transport-v001-20261030T205623Z.json)
et [protocole figé avant calcul](../raw/cfa-signal-transport-freeze-v001-20261030T205623Z.json).
La sonde lie le `Demosaic.cpp` courant sans le modifier. Aucun débruitage,
DCP, TIFF de mesure ou accès à la réserve finale. Durée de campagne : 9,59 s
pour de petites fenêtres, pas une mesure de latence applicative.

### 45.1. Méthode et séparation des informations

Quatre noirs de développement déjà qualifiés, deux à ISO 1600 et deux à
ISO 51200 ; fenêtre CFA centrale 256×256, contrôle intérieur 128×128.
Le résidu réel est centré sur le noir moyen de développement figé, par phase
R/G1/G2/B, et non recentré pour chaque prise. Il garde donc sa structure
spatiale et les écarts entre prises. Ce centrage simule une calibration locale,
sans remplacer le noir du décodeur de production.

Au résidu noir s'ajoute seulement un terme gaussien indépendant de variance
`a·signal_DN`, puis les gains/diviseur du pilote. **Ne pas rajouter b** : le
résidu réel contient déjà le bruit de lecture. La loi décrit des seconds
moments ; elle ne fournit pas un modèle physique complet de photons/électrons.
La variance réalisée du terme ajouté représente 94,4–105,1 % de son espérance
sur les fenêtres mesurées, sans ajustement a posteriori.

Signaux CFA équilibrés : 0,003/0,03 à ISO 1600 et 0,03/0,1 à ISO 51200,
plus le noir. Chaque niveau positif a un aplat et une texture irrégulière fine
bornée, de moyenne voisine du même niveau. Son écart-type est 0,000471 au
niveau 0,003, puis 0,000941 aux autres niveaux. Le niveau 0,03 à ISO 1600
est hors domaine d'ajustement historique ; les autres niveaux restent dans
ce domaine, phase par phase. Cela ne valide pas leur réalisme photographique.

Quatre graines préparent les profils sur un noir, quatre autres contrôlent
sur l'autre noir ; les rôles sont ensuite inversés. Chaque prise reçoit un
tirage indépendant. Les répétitions réutilisent cependant le même résidu
noir : elles ne créent pas de nouvelles acquisitions réelles indépendantes.

100 transports bruités et dix témoins propres, 128 comparaisons de modèles,
34 diagnostics par différence de paires. Le résidu de sortie est rapporté
au RCD du signal propre correspondant, avant et après médiane. Covariances
3×3 et moyennes des axes I/Co/Cg conservées, au pixel et sur trois plans B3.
B3 Python en float64 : mêmes noyaux/supports, pas parité bit à bit avec C++.
Les différences de paires suppriment le commun ; elles ne sont pas une
vérité sur tout le bruit visible d'une seule image.

### 45.2. Résultats sans modification du filtre

Trois prédictions : profil du noir seul ; profil d'aplat au même niveau ;
extrapolation affine de covariance à partir du noir et du premier niveau
positif. Les deux dernières sont préparées sur les seules graines de
calibration de l'autre prise, jamais sur le résidu du cas contrôlé.

Après médiane, erreur relative de covariance (norme de Frobenius), médiane
sur niveaux, plans et inversions des deux prises :

| ISO et signal contrôlé | Noir seul | Aplat au même niveau | Affine depuis le niveau bas |
| --- | ---: | ---: | ---: |
| 1600, aplat | 85,7 % | 1,84 % | 1,93 % |
| 1600, texture fine | 86,0 % | 3,40 % | 4,41 % |
| 51200, aplat | 61,9 % | 2,11 % | 3,85 % |
| 51200, texture fine | 61,9 % | 2,11 % | 3,83 % |

Le profil au niveau correspondant donne, sur les aplats après médiane,
des rapports σ prédit/mesuré de 0,978–1,029 à ISO 1600 et 0,976–1,027 à
ISO 51200. Sur la texture fine à ISO 1600/niveau 0,003, le premier plan B3
descend à 0,887 en Co et 0,898 en Cg : l'aplat ne représente pas exactement
la propagation non linéaire de la médiane sur une texture. Avant médiane,
les rapports de cette famille restent entre 0,977 et 1,026.

L'extrapolation affine n'est pas exacte non plus : son minimum après
médiane vaut 0,887 sur la texture à ISO 1600 et 0,950 à ISO 51200. Toutes
les matrices prédites restent positives semi-définies dans ce banc, sans
projection ni correction forcée. Cette propriété n'est pas garantie pour
une autre extrapolation. Dans les noirs après médiane, 48,7–56,2 % des
valeurs par canal restent négatives : pas de borne silencieuse à zéro.

### 45.3. Décision et prochaine épreuve bornée

La propagation dépendante du signal est une piste concrète pour éviter
que la texture serve d'estimateur de bruit. Le profil noir seul ne convient
pas aux tons éclairés. Un profil d'aplat simulé n'est toutefois pas encore
un modèle universel : quatre noirs réutilisés, deux ISO, deux niveaux et
une seule texture achromatique ne qualifient ni les contours, ni les scènes
colorées, ni un autre boîtier. Une petite erreur globale de covariance peut
masquer une erreur relative chroma plus grande, d'où les axes séparés.

Prochaine étape : comparer à filtre et force identiques les modèles courant,
noir seul et dépendant du signal sur ces mêmes entrées, mesurer erreur au
signal propre et rétention des textures, puis étendre aux contours/couleurs
avec protocole figé avant calcul. Ne pas optimiser des seuils sur ce banc
ni présenter une meilleure prédiction de variance comme un meilleur rendu.
Six tests Python et le contrat natif signé passent ; cinq suites ciblées
passent. Aucun moteur, profil ou IHM modifié ; pas de push, dev ou tag.

## 46. Modèle dépendant du signal à filtre identique — 30 octobre 2026

Campagnes successives, conservées sans réécriture :

- [Gaussien, 21:01 UTC](../raw/signal-filter-v001-20261030T210129Z.json),
  [protocole figé](../raw/signal-filter-freeze-v001-20261030T210129Z.json).
- [Poisson mis à l'échelle, 21:06 UTC](../raw/signal-filter-v001-20261030T210633Z.json),
  [protocole figé](../raw/signal-filter-freeze-v001-20261030T210633Z.json).
- [Premier audit des deux campagnes](../raw/signal-filter-audit-v001-20261030T211032Z.json).

### 46.1. Question, comparaison et contrôles

Une bonne prédiction de variance (§45) ne démontre pas une amélioration du
filtre. Cette épreuve emploie le même `CameraDenoise.cpp`, la même force
100/100 et le même bruit CFA propagé par RCD signé/médiane. Quatre noirs,
deux ISO et deux niveaux déjà décrits ; ajouter au témoin plat les textures
fine/grossière et les contours achromatiques/colorés. Deux graines de contrôle
par prise : 80 cas par campagne, ROI de mesure x/y=144, côté96 dans 256².
Le signal propre de référence est lui aussi passé dans RCD/médiane.

Les modèles comparés sont : courant conditionnel ; pooled aveugle courant ;
pooled estimé sur l'autre noir ; pooled estimé sur un résidu d'aplat indépendant
au même niveau de signal et sur l'autre noir. Un pooled estimé sur le bruit
exact du cas reste un diagnostic privilégié, pas un profil déployable.
Le profil externe est ici **encore estimé par le MAD/covariance robustes du
module** : il ne faut pas le confondre avec une injection des moments vrais.

Le hook unique d'instrumentation est placé après l'estimation ; le corps du
filtre reste inchangé. Chaque campagne vérifie 160 empreintes de sorties
original/bypass et 80 identités à modèle nul. Les observateurs continuent de
décrire le modèle avant substitution ; ils ne prouvent pas le profil appliqué.
MSE RGB, MSE/biais I/Co/Cg, projection du signal et empreintes sont conservés.
Ces axes caméra ne sont pas la luminance d'affichage. Les stress propres
avec un budget non nul ne représentent pas un cas réel à bruit nul.

La seconde campagne remplace seulement la distribution du terme de signal :
`a·(Poisson(signal_DN/a) − signal_DN/a)`. Même moyenne nulle et variance
`a·signal_DN` que le gaussien ; cela **n'identifie pas a à un gain physique
capteur calibré**. Quatre tests contrôlent moments, asymétrie, signal nul et
identité du flux gaussien précédent. Les réalisations ne sont pas identiques
entre distributions ; leurs ratios ne constituent pas un essai causal sur
les mêmes pixels. Durées : 220,77 s puis 220,44 s sur fenêtres seulement.

Correction explicite de métadonnées : le gel de la seconde campagne contient
encore deux libellés rédigés « Gaussian ». Le champ structuré `distribution`,
les appels `normalized_draws` et son source figé identifient bien
`scaled_poisson`. L'audit fournit les libellés corrigés, sans modifier le gel
ou les mesures ni prétendre à une nouvelle exécution.

### 46.2. Résultats et contre-exemples

Ratios médians de MSE RGB du profil externe au courant, réunissant les deux
niveaux et quatre cas par niveau ; inférieur à1 = meilleur :

| ISO / contenu | Gaussien | Poisson mis à l'échelle |
| --- | ---: | ---: |
| 1600, aplat | 1,004 | 0,970 |
| 1600, texture fine | 1,101 | 1,075 |
| 1600, texture grossière | 1,004 | 0,994 |
| 1600, contour achromatique | 0,902 | 0,866 |
| 1600, contour coloré | 1,002 | 0,989 |
| 51200, aplat | 1,018 | 0,995 |
| 51200, texture fine | 1,005 | 0,985 |
| 51200, texture grossière | 1,003 | 1,001 |
| 51200, contour achromatique | 1,008 | 0,960 |
| 51200, contour coloré | 1,010 | 0,986 |

À1600, les huit cas de texture fine régressent en gaussien, jusqu'à+17,5 %
de MSE ; le groupe régresse également avec Poisson. Le gain sur les contours
achromatiques est réel dans ce banc, mais ne compense pas ces contre-exemples.
Le profil noir seul laisse nettement trop de bruit aux niveaux éclairés
(rapports médians par cellule d'environ2,9 à8,5 en gaussien).

Les modèles courant et pooled aveugle donnent ici des résultats presque
identiques (écart maximal de ratio MSE inférieur à0,8 % en gaussien).
Les fenêtres ont surtout un niveau de signal dominant : ce résultat ne valide
pas un remplacement du routage conditionnel sur une image entière.

### 46.3. Décision intermédiaire, avant injection des covariances

**Ne pas remplacer globalement le modèle courant par ce pooled externe.**
Les grands gains du banc à bruit de lecture seul (§43) ne se transfèrent
pas à tous les cas comportant le terme de signal. Cela n'annule pas la
contamination par texture démontrée sur d'autres témoins, et ne démontre
pas non plus que la propagation physique serait inutile.

Ambiguïté restante : le profil externe utilise encore l'estimateur robuste.
La prochaine épreuve doit injecter les covariances empiriques de cinq plans
B3, calculées sur plusieurs simulations de calibration indépendantes, sans
réestimer leur amplitude par MAD. Même filtre, mêmes cas et même force,
contrôle explicite de PSD et parité des pixels avant comparaison. Pas de
nouvelle loi de force ni de recherche de seuils sur ces quatre noirs.

Revue de l'historique effectuée avant toute nouvelle piste : `patch_noise`
§§177–194 et249–250/273–274 ont déjà exploré NLM, guidage, faible rang et
Wiener. Ne pas les redécouvrir sous un autre nom ni relancer une chaîne de
micro-variantes sur les mêmes quatre photos. Ce jalon teste une différence
précise : le terme CFA de bruit dépendant du signal manquait aux ablations.
Aucun changement de production ou nouveau filtre promu.

## 47. Injection directe des covariances : piste positive au banc — 30 octobre 2026

Suite chronologique du §46, sans changer le filtre ni sa force :

Banc et contrôles locaux : commit `d00d507` sur `patch_raw_bench`. Les gels
conservent les sources exacts exécutés avant leur commit, pas seulement le
SHA du parent Git.

- [Gaussien, 21:13 UTC](../raw/signal-filter-v001-20261030T211312Z.json),
  [gel](../raw/signal-filter-freeze-v001-20261030T211312Z.json).
- [Poisson mis à l'échelle, 21:15 UTC](../raw/signal-filter-v001-20261030T211541Z.json),
  [gel](../raw/signal-filter-freeze-v001-20261030T211541Z.json).
- [Audit strict des quatre campagnes](../raw/signal-filter-audit-v001-20261030T212013Z.json)
  après le [premier audit à quatre](../raw/signal-filter-audit-v001-20261030T211810Z.json)
  après le premier [audit à trois campagnes](../raw/signal-filter-audit-v001-20261030T211527Z.json).

### 47.1. Ce qui change, ce qui reste contrôlé

Le §46 laissait une ambiguïté : son profil « bruit connu » était encore
réestimé par MAD et sélection du quart des témoins les plus calmes. Cette
fois, quatre graines de calibration produisent les résidus d'un aplat au
niveau considéré, sur l'autre noir. Leur covariance centrée moyenne est
calculée directement sur **cinq plans B3**, en I/Co/Cg, puis injectée au
même point après estimation. Pas de réestimation MAD de son amplitude.

Le module utilise toujours son `fromCovariance`, sa régularisation de
corrélation, ses énergies locales, la force100/100 et sa correction DC.
Le seul nouveau chemin est dans la copie générée du banc ; aucune source
moteur ou interface de production modifiée. Les covariances empiriques ne
sont pas une vérité physique exacte : bruit de lecture réel réutilisé,
terme de signal simulé, référence aplatie et mesure spatiale finie. Les deux
plans B3 supplémentaires ont aussi un nombre effectif de mesures plus faible
que le nombre de pixels, à cause de la corrélation spatiale.

Contrôle fixé avant calcul : première des deux graines de contrôle du §46,
deux prises, deux niveaux, cinq contenus, deux ISO, soit40 cas par distribution.
Tous leurs hashes d'entrée et sorties du filtre de référence correspondent
aux cas précédents : il s'agit d'un rejeu apparié, pas de40 nouvelles scènes.
Chaque campagne ajoute80 parités original/bypass et40 identités modèle nul.
Durées125,17 s et125,74 s, toujours sur fenêtres, pas budget applicatif.

Les matrices doivent être finies, symétriques et positives semi-définies,
avec tolérance numérique déclarée ; une matrice non PSD est refusée, jamais
rendue valide par projection. Contrôles natifs effectués : covariance nulle
= identité exacte, covariance négative = échec explicite. Trois tests
vérifient les cinq plans, le témoin constant signé et la concordance exacte
des trois premiers plans Python avec l'épreuve du §45. Le calcul reste en
float64 côté Python, distinct de l'arrondi float32 du module.

### 47.2. Résultat confirmé dans les deux distributions

Rapports médians de MSE RGB au filtre courant, par groupe (quatre cas chacun) :

| ISO / contenu | Covariance directe, gaussien | Covariance directe, Poisson |
| --- | ---: | ---: |
| 1600, aplat | 0,665 | 0,671 |
| 1600, texture fine | 0,936 | 0,930 |
| 1600, texture grossière | 0,738 | 0,740 |
| 1600, contour achromatique | 0,599 | 0,578 |
| 1600, contour coloré | 0,709 | 0,713 |
| 51200, aplat | 0,723 | 0,687 |
| 51200, texture fine | 0,718 | 0,693 |
| 51200, texture grossière | 0,706 | 0,685 |
| 51200, contour achromatique | 0,718 | 0,692 |
| 51200, contour coloré | 0,720 | 0,685 |

Les80/80 cas améliorent la MSE RGB **et chacune des trois MSE I/Co/Cg**.
Ce gain porte sur une erreur quadratique à la référence propre, pas sur un
pourcentage de bruit visuel ou de détails restitués. À1600, les rapports
σI du profil empirique au MAD de référence sont environ0,985–1,30 selon
niveau/prise/plan ; cela ne prouve pas que toute la différence de rendu vient
de la variance I : les autres variances et corrélations changent également.
Pas d'attribution causale à un coefficient isolé sans ablation supplémentaire.

La restitution des textures les plus faibles n'est pas résolue. Exemple
gaussien, prise0 à1600/niveau0,003 : projection du signal bruité fin0,280
avec le courant,0,362 avec covariance directe ; grossier0,637→0,800.
Ce sont des réalisations particulières avec un terme croisé bruit/signal,
pas des espérances établies. Les stress propres au budget non nul restent
fortement lissés (gain fin environ0,05–0,10) ; ne pas les présenter comme une
mesure de fidélité d'une image sans bruit ni comme une absence de perte.
Sur le contour coloré de ce même témoin, projection0,931→0,921 malgré la
MSE meilleure : l'amélioration globale n'exclut pas une légère atténuation
supplémentaire d'un détail.

### 47.3. Attribution : une seule calibration, sans avantage du moyennage

Dernier contrôle ciblé, [21:21 UTC](../raw/signal-filter-v001-20261030T212101Z.json)
et [gel](../raw/signal-filter-freeze-v001-20261030T212101Z.json) : comparer
le profil MAD de référence à la covariance directe de **la même unique
réalisation** de référence, au lieu de la moyenne de quatre. Les autres
paramètres restent figés. Cela sépare le changement de méthode d'estimation
du gain éventuel dû au nombre de simulations. Durée123,66 s.

Le gain persiste sur40/40 cas, en RGB et sur chaque axe I/Co/Cg. À1600,
rapports médians0,677 sur l'aplat,0,942 sur la texture fine et0,607 sur le
contour achromatique ; à51200, rapports médians par groupe0,716–0,734.
Pire ratio RGB de ce contrôle0,962. Le moyennage de quatre réalisations ne
suffit donc pas à expliquer le gain du §47.2. Ne pas attribuer davantage
l'effet au seul MAD, à la sélection des témoins calmes, à une variance
isolée ou aux corrélations : cette décomposition n'a pas été effectuée.

[Audit final des cinq campagnes](../raw/signal-filter-audit-v001-20261030T212327Z.json) :
80 rejeux gaussiens et40 Poisson vérifiés par empreintes d'entrée et de
sortie de référence. Les audits précédents restent conservés. Aucun ancien
JSON ni résultat intermédiaire négatif supprimé ou corrigé silencieusement.

### 47.4. Conclusion, arrêt de cette campagne et suite utile

**Piste retenue pour qualification, pas pour intégration immédiate :** un
budget de covariance propagé indépendamment du contenu apporte un gain
reproductible à filtre identique. Le réestimer ensuite par le MAD courant
peut perdre ce bénéfice. L'échec intermédiaire du §46 ne réfutait donc pas
le modèle dépendant du signal ; il concernait cette façon de le convertir
en paramètres du filtre. Aucun coefficient ajusté aux réponses de contrôle.

Cette conclusion suffit à clore l'épreuve, plutôt que multiplier les
micro-variantes sur les mêmes noirs. Les280 évaluations cumulées des cinq
campagnes réutilisent160 observations simulées distinctes, dont120 rejeux
pour les variantes directes ; elles reposent sur **quatre acquisitions réelles**,
pas280 validations indépendantes. Pas d'intervalle de confiance généralisant
le gain à toutes les photos, tous les ISO ou tous les boîtiers.

Prochaine qualification distincte : préparer un budget par contexte caméra
(loi caractérisée, ISO, noir/blanc et gains BdB), vérifier son transfert sur
signaux colorés et niveaux locaux variés, puis un rendu sur des scènes RAW
réelles avec comparateurs fixes. Contrôler détails, biais, chroma, optiques,
DCP, ressources et rendu final avant toute promotion. La latence d'un modèle
simulé par image ou précalculé/cache n'est pas encore qualifiée ; ne pas
présenter ces profils d'aplats comme un profil universel déployable.

Les réserves finales restent fermées jusqu'à un candidat/protocole figé.
Aucun nouvel export utilisateur demandé pour conclure ce banc. Production
inchangée ; neuf suites ciblées passent, pas de push, fusion dev ou tag.

## 48. Préparation du budget par contexte caméra — 30 octobre 2026

Points1 et2 autorisés après le §47, **sur le banc seulement**.
`camera_noise_budget.py` sélectionne le catalogue par identité explicite du
boîtier et ISO exact. Il lit modèle/ISO et normalisation dans les captures
authentifiées, non dans le seul nom d'un fichier. L'alias `EOS R6 Mark III`
correspond explicitement à `Canon EOS R6 Mark III` ; aucun emprunt R7/R6,
ISO voisin ou interpolation implicite. Le catalogue reste celui du §44,
exploratoire, pour le R6 Mark III ; transport mesuré à1600 et51200 seulement.

### 48.1. Contrat des unités et des frontières

Contexte : empreinte du catalogue, boîtier, ISO, région capteur caractérisée,
noir par phase, blanc, diviseur et gains R/G/B/G2, étape signée après
RCD/médiane avant LCP/DCP. Pour chaque phase :

`signal_DN = moyenne_caméra·D/g + B_runtime − B_reference`

`variance_caméra = (b + a·max(signal_DN, 0))·g²/D²`.

Pour le domaine latent positif, cette expression rejoint `A·X+B` du §44.
Le plancher porte sur l'espérance physique utilisée pour la variance,
**pas sur les pixels signés**. Les signaux latents négatifs et les sorties
du domaine d'ajustement restent explicitement signalés. Dans les simulations,
le résidu noir réel fournit le bruit de lecture ; b n'est pas ajouté une
seconde fois comme bruit synthétique.

La couleur/niveau locaux viennent uniquement d'un guide moyen31×31 du
RGB caméra observé, puis de la moyenne de la zone. Pas de MAD, variance
de texture ou référence propre pour préparer le budget. Un aplat simulé
indépendant à cette moyenne/couleur, avec l'autre noir, sert à propager
le budget dans RCD signé/médiane/B3. C'est encore une préparation empirique
coûteuse de recherche, pas une table analytique ou un profil prêt à déployer.

L'identité d'un profil inclut contexte, couleur/niveau exacts, référence
noire, binaire de transport, graines, distribution, recette et géométrie
de mesure. Pas de quantification ou réutilisation approchée silencieuse.
Cette empreinte prépare un contrat de réutilisation ; **aucun cache de
production ni raccordement IHM n'est ajouté**.

Dix tests couvrent sélection stricte, deux verts, gains au carré/diviseur
inverse au carré, origine du noir, variance affine, conservation signée,
identité des profils/géométrie, séparation des graines et absence de mutation
des métadonnées. Les témoins BdB (R×1,2/B×0,8), noir+4 DN et diviseur10000
testent les transformations ; ce ne sont pas des calibrations physiques de
nouveaux boîtiers, modes RAW, compressions ou durées d'exposition. La sélection
de ces modes/durées et de la température capteur reste à contractualiser
avant une utilisation réelle. Aucun autre boîtier déclaré compatible.

## 49. Transfert vers niveaux locaux, couleurs et contours — 30 octobre 2026

### 49.1. Première campagne, puis contrôle de géométrie

[Première mesure](../raw/context-transfer-v001-20261030T214522Z.json),
[gel avant calcul](../raw/context-transfer-freeze-v001-20261030T214522Z.json),
[audit](../raw/context-transfer-audit-v001-20261030T214938Z.json).
Durée227,41 s. 144 cas : deux ISO, trois contextes, deux distributions,
six contenus et deux prises noires. Les trois fenêtres64² sont à gauche,
sur la transition, et à droite ; cinq plans B3, soit2160 comparaisons de
covariances. Les répétitions restent fondées sur quatre noirs réutilisés.

Contenus : neutre, aplat chaud/froid, saut de luminosité, transition de
couleur et texture colorée fine. Une graine produit le guide observé,
trois autres mesurent le résidu de contrôle ; les quatre graines de
calibration sont distinctes, sur l'autre noir. La référence propre n'est
utilisée que pour noter les erreurs et le guide, jamais pour préparer le profil.

Premier diagnostic : erreurs médianes de covariance d'environ5–10 % sur
les trois premières bandes selon contenu/zone, et environ20–30 % sur
les deux bandes les plus larges. Les écarts-types extrêmes dépassent ces
médianes. Ce passage ne suffit pas à attribuer l'écart à la couleur :
le profil était mesuré au centre128², tandis que les contrôles64² proches
des bords voient une autre géométrie aux grandes échelles.

Un contrôle a donc été figé **sans retoucher graines, guide, loi ou seuils** :
[gel v002](../raw/context-transfer-freeze-v002-20261030T215015Z.json).
Les profils sont mesurés exactement aux mêmes fenêtres que les contrôles,
et cette géométrie entre désormais dans leur identité. Le premier passage
reste conservé comme diagnostic, pas une validation silencieusement corrigée.

### 49.2. Rejeu apparié et résultats

[Mesure v002](../raw/context-transfer-v002-20261030T215015Z.json),
[audit du rejeu](../raw/context-transfer-audit-v001-20261030T215449Z.json),
[audit final avec synthèses](../raw/context-transfer-audit-v001-20261030T215553Z.json).
Durée226,98 s. 576 identités d'entrée (guide et trois contrôles de chaque cas)
et2160 covariances de contrôle sont exactement conservées entre les deux
campagnes ; seules les prédictions changent. Les72 témoins de normalisation
affine, répartis sur six contextes et quatre phases, concordent avec le
calcul direct de variance. Aucun paramètre ajusté en voyant les réponses.

Erreurs relatives de covariance en norme de Frobenius, médianes sur prises,
ISO, contextes, distributions et zones, avec poids égal par comparaison :

| Contenu | Plans B3 0–2 | Plans B3 3–4 |
| --- | ---: | ---: |
| Aplat neutre | 7,4 % | 28,5 % |
| Aplat chaud | 7,3 % | 25,5 % |
| Aplat froid | 6,6 % | 28,7 % |
| Saut de luminosité | 9,3 % | 25,9 % |
| Transition colorée | 7,7 % | 26,6 % |
| Texture colorée fine | 6,4 % | 26,5 % |

Le changement de géométrie ne supprime donc pas l'écart des grandes bandes.
Leur erreur est aussi présente sur les aplats : elle ne démontre pas un
défaut exclusivement lié à la couleur ou aux contours. Les fenêtres64²
ne fournissent que peu d'informations spatialement indépendantes à ces
échelles ; les mêmes résidus noirs sont réutilisés. Les moyennes de quatre
calibrations/trois contrôles ne donnent pas une référence de précision
arbitraire. Les covariances individuelles avant moyenne ne sont pas conservées
dans cette campagne : une analyse d'incertitude plus complète devra les mesurer.

Les médianes cachent des contre-exemples. Sur le saut de luminosité, à1600,
témoin BdB, gaussien, prise1, zone de transition, premier plan : σCo prédit
=0,670 fois le mesuré. Sur les grandes bandes, le rapport de σ atteint
1,825 sur l'aplat neutre. Ne pas convertir une médiane de covariance en
précision garantie par axe ou pixel. Les percentiles95, maxima et rapports
par axe/zone/plan figurent dans l'audit ; ce sont des statistiques descriptives,
pas des intervalles de confiance sur de nouvelles photos.

Le guide observé diffère au maximum de0,00264 de son témoin propre sur un
canal de la moyenne locale, en unités caméra. Il est réellement aveugle
mais pas exact. 256 des1728 entrées de phase des profils préparés sont hors
domaine du fit historique ; les drapeaux sont conservés, pas masqués par
l'ancien transfert exploratoire. Aucun signal latent négatif rencontré
dans ces scènes simulées ; son traitement signé/plancher est testé au niveau
unitaire seulement, pas qualifié ici sur une scène réelle proche du noir.

### 49.3. Bilan des deux points et prochaine qualification

**Point1 réalisé au banc :** sélection et conversion par contexte, guide
observé, clés de profils et refus explicites fonctionnent sur leur périmètre.
Ce n'est pas une caractérisation automatique de tout nouveau boîtier : six
ISO discrets du catalogue R6 Mark III, dont deux transportés ici ; les autres
ISO/boîtiers sont refusés au lieu d'être interpolés silencieusement.

**Point2 mesuré, pas validé globalement :** transfert encourageant sur les
bandes fines et aplats colorés, mais trop d'incertitude/écarts aux grandes
échelles et un contre-exemple net au passage sombre/clair. Ne pas brancher
ce profil d'aplat local comme budget universel de production. Cette campagne
ne rejoue pas le débruitage ni le DCP : elle qualifie la prédiction du bruit,
pas le gain de rendu du §47 sur de nouvelles photos.

Suite utile avant les RAW réels : distinguer la variabilité de mesure des
grandes bandes d'un biais de modèle (covariances par réalisation, références
plus nombreuses/support plus large), puis vérifier une propagation qui garde
les transitions spatiales plutôt que de les remplacer par un seul aplat.
Ne pas régler le guide ou gonfler σ pour absorber ces erreurs. Le corpus
actuel suffit à préparer ces contrôles ; aucune nouvelle prise demandée.

288 évaluations =144 cas rejoués, pas288 validations indépendantes ; les deux
campagnes reposent toujours sur quatre noirs. Les réserves finales restent
fermées. Dix tests du budget et trois de l'audit passent ; huit suites ciblées
passent. Aucun moteur/profil/IHM modifié, aucun cache applicatif ou budget de
latence certifié, pas de push/dev/tag. Calculs clos.

## 50. Audit des biais et réplication du guide spatial — 30 octobre 2026

Suite autonome demandée par l'utilisateur, sans production. [Gel avant calcul](../raw/spatial-budget-freeze-v001-20261030T220813Z.json),
[mesures par réalisation](../raw/spatial-budget-v001-20261030T220813Z.json),
[audit et critères](../raw/spatial-budget-audit-v001-20261030T221518Z.json).

### 50.1. Biais reconnus, sans effacer les précédents

- Les mêmes quatre noirs et motifs ont servi à de nombreuses décisions.
  Les gains du §47 ne constituent pas une validation indépendante du moteur.
- La loi empirique a/b génère le bruit de signal et sert au prédicteur :
  cela vérifie la propagation et ses hypothèses, pas la vérité du capteur.
- Une grande bande B3 voit un support jusqu'à125 pixels. Une fenêtre64²
  ne contient pas autant d'informations indépendantes que son nombre de pixels.
- Réutiliser le même résidu noir dans tous les tirages ajoute une réalisation
  de bruit de lecture figée, pas32 acquisitions indépendantes. L'incertitude
  Monte-Carlo du seul terme variable ne décrit pas celle de cette réalisation.
- Une meilleure MSE ou covariance moyenne ne garantit pas la restitution des
  textures ou la fidélité après DCP. Les axes et cas extrêmes restent suivis.

Correction de lecture des §§48–49 : leurs gros écarts mesurés restent vrais,
mais ne prouvent pas un biais systématique du modèle de grande échelle.
Le conditionnement sur un champ noir et le petit support de mesure peuvent
contribuer. La non-promotion était justifiée ; incriminer uniquement le guide,
la couleur ou la loi de signal ne l'aurait pas été.

### 50.2. Réplication fixée et données encore utilisées

Nouvelles graines :32 calibration/32 contrôle, guide séparé ; nouveaux
aplats, diagonales de luminance/couleur et rampe courbe, aux niveaux dans
le domaine caractérisé. Quatre nouveaux noirs pour le **transport** :0005,
0006,9952,9953, sélectionnés par rôle développement et ordre des noms avant
mesure, avec SHA distincts des anciennes références. Ils ont déjà participé
à la caractérisation et viennent de la même séance/R6 Mark III : pas de
holdout physique vierge. Les réserves finales restent fermées.

32 cas, deux ISO, deux distributions et deux prises. Comparaison àplat local,
guide spatial observé31×31 et guide propre privilégié. Sur les aplats seulement,
un diagnostic réutilise aussi le même noir de contrôle. Ces diagnostics ne
sont jamais sélectionnés comme solutions déployables. Les covariances par
réalisation et leur séparation commun/variable conditionnelle sont conservées.
La partie commune n'identifie pas le FPN du capteur.

Le transport par lots bornés à huit fenêtres conserve le même RCD signé :
16 parités scalaire/lot vérifiées dans la campagne, sans parallélisme imbriqué.
Durée403,22 s, fenêtres256² ; pas une qualification de latence applicative.

### 50.3. Résultat : guide spatial non promu

Critères fixés : erreur médiane de covariance sur plats≤15 %, tous les
rapportsσ entre0,8 et1,25, médiane des transitions meilleure que l'aplat.
Deux critères passent, celui des extrêmes échoue. Aucun seuil n'est retouché.
Le guide spatial améliore peu certaines moyennes et garde des contre-exemples,
y compris avec le guide propre privilégié. Il n'est pas retenu comme nouvelle
piste stable de rendu.

Diagnostic utile : sur les aplats, covariance médiane à3,08 % avec le même
noir privilégié, contre7,57 % avec un autre noir ; rapportsσ respectivement
0,888–1,132 et0,744–1,246. Le terme variable conditionnel est prédit à environ
3–4 % d'erreur médiane, la composante commune conditionnelle à environ17–19 %.
L'identité du champ noir importe donc dans ce banc. Cela ne valide ni un
modèle de bruit fixe physique, ni une soustraction de ce champ à une photo.

**Piste suivante figée :** prédire un budget statistique de lecture à partir
de plusieurs noirs, plutôt que transporter le hasard d'un seul champ.
La simulation interne et le rejeu de résidus réels doivent rester séparés ;
une réussite du modèle sur son propre générateur ne suffira pas à qualifier
le boîtier. Pas de réouverture des réserves, pas de modification de production.

## 51. Budget statistique de lecture multi-noirs : piste stable au banc — 30 octobre 2026

[Gel avant mesure](../raw/spectral-budget-freeze-v001-20261030T222031Z.json),
[données par réalisation](../raw/spectral-budget-v001-20261030T222031Z.json),
[premier audit](../raw/spectral-budget-audit-v001-20261030T223006Z.json),
[audit final recomputant les scores/critères](../raw/spectral-budget-audit-v001-20261030T223115Z.json).
Durée409,60 s sur fenêtres, pas qualification de latence de l'application.

### 51.1. Ce qui change dans le modèle et dans la cible du test

La cible devient un **budget attendu sur un ensemble de réalisations**, et
non la covariance conditionnelle d'un champ noir particulier. Les anciens
échecs ne sont pas effacés : changer cette cible ne « répare » pas le critère
du §50 sur les mêmes données. Le modèle, les graines et le conditionnement
changent ; ne pas attribuer causalement toute la différence au seul spectre.

Par ISO, trois acquisitions noires de calibration : anciennes références
0003/0004 plus0007 à1600 ;9950/9951 plus9954 à51200. Trois fenêtres par
photo, soit neuf tuiles, **pas neuf acquisitions indépendantes**. Identités
SHA distinctes des contrôles0005/0006 et9952/9953. Ces données restent toutes
de développement et de la même séance/R6 Mark III ; pas d'ouverture de la
réserve finale. Les contrôles ont déjà été vus au §50, ce que le gel déclare.

Le modèle calcule une densité spectrale croisée4×4 des phases R/G1/G2/B,
après retrait du seul constant spatial par phase/prise. Les corrélations
spatiales et entre phases sont ainsi représentées sous une hypothèse
stationnaire. Les amplitudes sont normalisées aux b temporels du catalogue,
pas ajustées aux réponses de contrôle. Une racine spectrale hermitienne
transforme un bruit gaussien réel indépendant en mosaïque corrélée ; le
terme de signal indépendant a·signal s'ajoute, puis RCD signé/médiane/B3.

Hypothèses et limites : gaussien stationnaire pour la lecture, grille
périodique128² par phase, drift constant retiré, pas de modèle physique de
pixels chauds ni de FPN identifié. La forme provient de structures spatiales
de noirs, l'amplitude d'une variance temporelle : ce lien reste à qualifier
physiquement. Seules les valeurs propres négatives d'arrondi dans la tolérance
1e-10 du maximum spectral sont ramenées à zéro, avec nombre/tolérance sauvés ;
un spectre véritablement non PSD est refusé. Ce n'est pas une projection
destinée à faire passer un modèle statistiquement invalide.

### 51.2. Deux domaines, jamais fusionnés pour déclarer un succès

- **Simulation spectrale interne** : le bruit de lecture et le terme de
  signal varient tous deux à chaque réalisation. Le générateur partage les
  hypothèses du prédicteur : test de cohérence/stabilité, pas vérité capteur.
- **Rejeu fondé sur des noirs réels** : alternance des deux noirs physiques
  de contrôle par ISO, avec nouveaux tirages de bruit de signal. Ni32
  acquisitions réelles par cas, ni photo éclairée indépendante. Le guide
  observé reste séparé des graines de calibration/contrôle.

32 cas au total, nouvelles graines110001…110032 et130001…130032, guide101001.
Les niveaux nominaux des motifs sont dans le domaine du fit, vérifiés
phase par phase. Même contexte explicite, mêmes nouveaux motifs et mêmes
seuils d'ingénierie que précédemment ; aucune correction après lecture des scores.

Les trois critères passent **séparément dans chaque domaine** : plats en
médiane≤15 %, tous les rapportsσ entre0,8 et1,25, médiane des transitions
meilleure avec le guide spatial qu'avec l'aplat local. L'audit recalcule
chaque comparaison depuis les32 covariances de calibration/contrôle et
refuse un désaccord avec les critères sauvés. Un test vérifie qu'un succès
interne ne peut masquer un échec du rejeu réel.

| Domaine / guide spatial observé | Erreur médiane, bandes0–2 | Bandes3–4 | Rapportsσ, tous axes/cas |
| --- | ---: | ---: | ---: |
| Simulation spectrale interne | 2,16 % | 8,83 % | 0,864–1,111 |
| Rejeu fondé sur noirs réels | 4,14 % | 9,26 % | 0,823–1,205 |

Sur les seules transitions du rejeu réel, médiane5,71 % avec le guide spatial
contre6,17 % avec l'aplat local. L'apport spatial est **modeste et non dominant** :
percentile95 d'erreur27,56 % contre24,59 %, maximum33,47 % contre31,56 %.
Aux grandes échelles, médiane9,26 % contre9,03 % pour l'aplat. Ne pas présenter
le guide comme meilleur partout, ni ces pourcentages comme un gain de bruit
visuel ou de détail. Aucun débruitage final n'est appliqué dans cette étude.

### 51.3. Décision et biais résiduels

**Piste retenue : un budget statistique de covariance multi-noirs, propagé
indépendamment de la variance de texture**, plutôt qu'une référence faite
d'une seule réalisation noire figée. Il franchit les critères du banc sous
les hypothèses et sur la population simulée déclarées. Cela ne démontre pas
que la FFT/simulation gaussienne soit indispensable : un témoin apparié avec
simple banque de plusieurs noirs n'a pas encore été comparé. Pas d'affirmation
de supériorité universelle du modèle spectral.

Pas de sélection d'une méthode privilégiée à signal propre, pas d'ISO ou de
boîtier emprunté, pas de seuil retouché après échec. Les limites restantes
sont explicites : adaptation de l'étude à des résultats antérieurs, même
loi génératrice/prédictrice pour le signal, contrôles physiques déjà vus,
six noirs de calibration/quatre de contrôle seulement et une seule séance.
Les32 tirages et neuf tuiles par ISO ne multiplient pas ce nombre de prises.
Ne pas déclarer l'analyse « sans biais » ou indépendante de caractérisation.

Suite avant production : confronter ce budget au filtre réel avec mesures
de détail/couleur/biais, puis à des RAW photographiques et références fixes.
Qualifier aussi coût, interpolation/caches et domaines caméra/ISO. Garder la
comparaison àplat/guide spatial et les cas extrêmes, sans imposer le guide
spatial comme vainqueur global. Le positif au banc justifie cette qualification,
pas une intégration silencieuse ni une revendication de rendu proche de LR.

Tests du modèle spectral : champs réels/déterministes, amplitude attendue,
corrélations entre phases, refus de données dégénérées. Tests du banc/audit :
graines disjointes, fournisseur de lecture appelé par réalisation, lots bornés,
non-mélange des domaines, absence de recouvrement des sources et refus d'un
critère enregistré incohérent. Production inchangée, pas de push/dev/tag.

## 52. Budget multi-noirs dans le filtre réel : gain d'erreur, veto de détail — 30 octobre 2026

[Protocole figé avant calcul](../raw/population-filter-freeze-v001-20261030T223846Z.json),
[mesures complètes](../raw/population-filter-v001-20261030T223846Z.json),
[premier audit](../raw/population-filter-audit-v001-20261030T224548Z.json),
[audit complété des gains sur entrées bruitées](../raw/population-filter-audit-v001-20261030T224629Z.json).
Durée de campagne416,06s. Aucun moteur ni fichier photographique modifié.

### 52.1. Question, témoins et critères avant mesure

Après le budget attendu positif du§51, tester son utilité dans le filtre
réel, pas seulement sa justesse face à une covariance.64cas : deux ISO,
deux domaines séparés (interne spectral/rejeu réel), deux distributions
de signal (gaussienne/Poisson redimensionnée), quatre motifs et deux graines
de contrôle. Motifs nouveaux : aplat, structure faible achromatique,
structure faible colorée, bord courbe. Graines170001–170032 pour calibration,
190001–190002 pour contrôle et151001 pour guide. Pas d'ajustement des
coefficients ou de sélection des motifs après leurs scores.

Même filtre et commande100/100, ROI exacte(y144,x144,96×96), cinq plansB3.
Le guide moyen31×31 provient d'un tirage observé distinct, jamais du signal
propre. Deux profils de covariance moyens comparés :

- modèle spectral du§51, lecture gaussienne indépendante par tirage ;
- banque simple des trois noirs physiques de calibration par ISO, tuile
  centrale parcourue cycliquement avec les mêmes32 graines de signal.

La vérité synthétique reste le signal propre **après** RCD/médiane : ce
test ne mesure donc pas la perte de détail déjà causée par le dématriçage.
Le profil est constant sur la ROI, pas encore variable par pixel. Le hook
`ExternalCovariance` remplace les nœuds conditionnels par un profil pooled :
calcul du filtre et force identiques, **routage spatial du modèle différent**.
Cette confusion potentielle est conservée, pas attribuée arbitrairement
à la qualité du modèle de bruit.

Critères conservateurs figés par domaine/candidat : ratio d'erreurRGB
médian<1, percentile95≤1,10 et pire≤1,25 ; pire ratio d'erreur chroma≤1,25 ;
perte de gain sur stress propre≤0,02 face au courant ; excès de biais moyen
opposé≤0,10fois le RMS d'erreurRGB de l'entrée. Seuils d'ingénierie pour
ce banc, pas intervalles de confiance ni normes de qualité photographique.

### 52.2. Résultats séparés et comparateur simple

| Domaine | Budget | Ratio médian MSE RGB au courant | Pire ratio RGB | Ratio médian MSE chroma | Veto détail propre |
| --- | --- | ---: | ---: | ---: | --- |
| Simulation interne | Spectral |0,645|0,727|0,764|Échec|
| Simulation interne | Banque simple |0,596|0,659|0,722|Échec|
| Rejeu de noirs réels | Spectral |0,720|0,758|0,810|Échec|
| Rejeu de noirs réels | Banque simple |0,663|0,711|0,770|Échec|

Tous les autres critères passent dans les deux domaines. Le gain mesuré
est une baisse d'erreur face au signal synthétique connu, **pas un pourcentage
de bruit visible supprimé**. La banque simple a une erreurRGB plus basse
que le spectral sur les64cas, avec environ8,2% d'écart médian spectral/banque
dans chaque domaine. Cela ne démontre ni un meilleur rendu perceptuel de la
banque ni une covariance plus juste : un filtre plus fort peut améliorer
la MSE en supprimant du détail. La FFT n'est pas justifiée comme complication
indispensable par cette épreuve.

Autre différence à ne pas masquer : la banque conserve l'amplitude spatiale
des résidus noirs, tandis que le modèle spectral la normalise au terme
temporel b. Ce témoin compare deux recettes de budget, **pas la seule forme
spectrale à amplitude identique**. Il ne permet donc pas d'attribuer l'écart
de MSE à la FFT ou à la seule non-gaussianité du bruit réel.

256parités pixel à pixel original/instrumenté et128identités à modèle nul
passent. Les profils et les32covariances de calibration par profil sont
conservés avec chaque cas. L'audit recalcule leur moyenne, les critères,
la grille, les identités et les résultats par motif/ISO ; aucune moyenne
favorable d'un domaine ne masque l'autre.

### 52.3. Pourquoi le stress propre bloque, sans exagérer la régression

Le stress applique au signal propre un budget dérivé d'une entrée bruitée.
Le courant, lui, réestime son bruit sur cette entrée propre. Cette dissymétrie
est volontaire, figée et sévère : elle teste la protection du signal face à
un budget non nul, **pas une situation physique de bruit réellement nul**.
Le gain propre courant est voisin de1 ; sous profil spectral il descend
vers0,607 sur la structure luma et0,130–0,165 sur la structure colorée.
Pire perte de gain0,8705. Veto échoué, sans relâchement après résultat.

Il serait faux d'en déduire87% de détails perdus sur les photos bruitées.
Sur le rejeu réel, gains médians sur les entrées effectivement bruitées :

| ISO | Motif | Courant | Spectral | Banque simple |
| --- | --- | ---: | ---: | ---: |
|1600|Structure luma faible|0,771|0,770|0,762|
|1600|Structure couleur faible|0,349|0,341|0,340|
|1600|Bord courbe|1,013|1,008|1,008|
|51200|Structure luma faible|0,733|0,727|0,724|
|51200|Structure couleur faible|0,530|0,508|0,491|
|51200|Bord courbe|0,959|0,955|0,954|

Le filtre courant atténue donc déjà fortement les détails colorés faibles
de ces cas. Le budget externe conserve une partie de ce défaut, avec une
perte supplémentaire modeste mais réelle sur ce banc. Le gain supérieur
en MSE ne résout pas à lui seul l'objectif « mieux protéger les structures ».
Les gains bruités restent des projections descriptives sur peu de tirages,
pas une probabilité universelle de conservation d'un détail.

### 52.4. Décision, biais et prochaine cible

Pas de promotion de la substitution globale. Ne pas rejeter pour autant le
budget statistique qualifié au§51 : un bon budget n'est pas un filtre
protecteur du détail, et son insertion pooled supprime ici l'adaptation
conditionnelle. Le stress propre n'est pas un oracle de qualité photo.

Biais restants inchangés : étude adaptative, une séance R6III, deux noirs
physiques de contrôle réutilisés par ISO, loi de signal commune au générateur
et au prédicteur, un seul réglage100/100 et une ROI. Pas de nouveaux boîtiers,
pas de DCP/LCP ni de validation visuelle. La réserve finale reste fermée.
Les416s sont un temps de campagne multi-variantes, pas une latence de
débruitage pleine image ; aucun plafond RAM production qualifié ici.

Prochaine cible utile : conserver le routage local/conditionnel du filtre
en y raccordant le budget, avec un témoin qui isole ce seul changement.
Mesurer le compromis résidu/détail sur les **entrées bruitées**, conserver
le stress propre séparé et les échecs figés, sans optimiser aveuglément
la MSE ni relancer le spectral comme solution supposée supérieure. Pas
de changement de coefficients ou de rendu de production à ce jalon.

## 53. Conserver le routage local et auditer le budget CPU — 30 octobre 2026

[Gel de l'ablation](../raw/local-budget-freeze-v001-20261030T225231Z.json),
[mesures complètes](../raw/local-budget-v001-20261030T225231Z.json),
[premier audit](../raw/local-budget-audit-v001-20261030T225809Z.json),
[audit avec compte des sorties modifiées](../raw/local-budget-audit-v001-20261030T225831Z.json).
Durée332,90s, étude seulement ; aucun fichier moteur modifié.

### 53.1. Protocole apparié, pas nouveau jeu indépendant

L'utilisateur demande la prochaine piste et l'adaptation du nombre de threads
à la machine. Rejouer exactement les64cas, profils et graines du§52 pour
isoler le raccordement : vérification des empreintes CFA/RGB, des résultats
courant/profil constant/modèle nul.640parités passent ; l'audit vérifie aussi
l'égalité des métriques historiques. Même filtre, commande100/100, domaines
séparés et budget externe constant par ROI. Aucune nouvelle donnée physique.

Deux nouvelles opérations dans le seul artefact instrumenté :

- **Plafond diagonal** : chaque sigma courant, pooled ou conditionnel, est
  limité par le sigma externe de la même échelle/canal. Les128échantillons,
  corrélations, supports, transitions et portes structurelles sont conservés.
  Jamais de hausse de sigma ; les corrélations externes ne sont pas injectées.
- **Secours seul** : remplacer le pooled par le budget externe sans modifier
  les valeurs ou supports conditionnels. Contrôle de routage, pas un modèle
  complet de variance dépendant du signal.

Tous les critères du§52 restent en vigueur. Ajouter avant calcul un veto :
aucune perte de gain projeté sur entrée bruitée>0,02 par cas face au courant.
Pas de modification de seuil ou de coefficient après lecture des résultats.

### 53.2. Résultat négatif utile : ne pas confondre protection et progrès

| Rejeu réel | Budget | Ratio médian MSE RGB | Pire ratio MSE | Perte maximale de gain bruité | Stress propre |
| --- | --- | ---: | ---: | ---: | --- |
| Plafond diagonal | Spectral |1,00160|1,01742|0,00090|Passe|
| Plafond diagonal | Banque |1,00000|1,01152|0,00051|Passe|
| Secours seul | Spectral |1,00000|1,00000|0,00000|Échec|
| Secours seul | Banque |1,00000|1,00000|0,00000|Échec|

Le plafond passe les critères de détail/couleur, mais échoue au gain médian
d'erreur. Dans le domaine interne également, ratio médian1 ; pires ratios
1,00735(spectral)/1,00226(banque). Il protège le stress propre sans apporter
le bénéfice de bruit recherché. Ne pas l'annoncer comme amélioration stable.

Le remplacement du secours ne change pas les sorties bruitées du rejeu réel,
malgré un stress propre encore atténué (pire perte de gain0,39250). Le domaine
interne est lui aussi pratiquement inchangé. Cela est compatible avec un
routage conditionnel dominant sur ces entrées ; **son occupation exacte n'est
pas mesurée**, donc pas d'explication exclusive prouvée. Le résultat ne
permet pas de conclure que le secours est inutile sur toutes les photos.

Aucune des huit combinaisons route/domaine/budget ne passe tous les critères.
Les seuils et les anciennes preuves sont conservés. Le budget du§51 n'est
pas réfuté ; ces deux modes de raccordement ne le rendent pas exploitable
avec un bénéfice démontré. Étape à étudier : prédire les covariances en fonction
du signal local lui-même, plutôt que plafonner le modèle courant ou agir
uniquement hors support. Préserver des témoins à routage/force identiques
et un contrôle de détail bruité ; ne pas recycler cette grille comme validation
indépendante ni déduire un meilleur rendu de la seule baisse de MSE.

### 53.3. Threads : adaptation existante et extension à prévoir

`CameraDenoise::workerThreads()` interroge déjà `hardware_concurrency()` et
renvoie `max(1,min(8,host>1 ? host-1 : 1))` : au plus8threads **caller inclus**,
un thread logique réservé quand l'indication matérielle est supérieure à1,
repli1 si elle est inconnue. `apply()` réduit encore ce budget au nombre de
tuiles. Aucun besoin de garder huit workers sur une petite machine.

La campagne consigne20threads logiques visibles par Python et son indication
de disponibilité au processus ; la sonde native annonce un **budget** de8.
Il ne faut pas confondre ce budget et les workers réellement actifs : sur
une fenêtre256×256, le filtre n'a qu'une tuile à distribuer. Ces332,90s de
campagne ne mesurent donc pas l'accélération multithread d'une image entière.
L'indication matérielle n'est ni la charge CPU instantanée ni une garantie
sur les ressources laissées libres par les autres applications.

L'adaptation demandée est donc déjà partielle. Pour l'étendre proprement :
budget CPU partagé entre aperçu/export/préchargement, priorité à l'interactif,
nombre de workers borné par les tâches et la mémoire, gestion des sous-appels
parallèles pour éviter la surallocation. Comparer ensuite1/2/4/8workers et
plus sur des images entières, vérifier parité, annulation, fin des threads,
latence et mémoire avant de relever le plafond. Le plafond8 est conservé
dans cette étude pour ne pas mélanger modification mathématique et ressources.
Pas de nouveau contrôleur ni de réglage de production annoncé comme livré.

## 54. Stabilisation et orientation neuronale pour la bêta — 31 octobre 2026

### 54.1. Décision validée par l'utilisateur

Après les résultats du§53, l'utilisateur valide l'arrêt des essais
incrémentaux et le maintien du débruitage actuel. Les variantes de banc non
retenues ne sont pas intégrées. Conserver les preuves JSON, les limites et
les outils utiles ; cette stabilisation n'est ni une nouvelle livraison,
ni un tag, ni une validation générale de tous les boîtiers.

La voie privilégiée pour la prochaine amélioration importante devient un
débruitage neuronal local, prévu pour la bêta. Objectif : meilleur compromis
bruit/détail, pas reproduction exacte de Lightroom. Le choix de cette piste
ne garantit pas une faible perte d'information : suppression de texture,
hallucination de détail, dérive couleur et robustesse restent à mesurer.
Le budget physique étudié reste une connaissance réutilisable, sans campagne
supplémentaire automatique dans l'immédiat.

### 54.2. Machine de référence et capacités dégradées assumées

L'utilisateur choisit sa machine comme première cible de qualification.
Les machines moins adaptées peuvent avoir moins de capacités dans le mode
avancé, mais doivent conserver l'accès au moteur de base. Ne pas imposer
la qualification du neuronal sur toutes les configurations avant ce premier
jalon ; ne pas déclarer pour autant les autres configurations compatibles.

Lors de la future qualification, figer un inventaire CPU, threads accessibles,
RAM, GPU/VRAM si utilisé, OS, pilotes, backend et version du modèle. Les20threads
logiques observés au§53 ne constituent pas à eux seuls un profil matériel
complet, ni une preuve de capacité GPU ou de mémoire suffisante.

Prévoir une détection de capacités avant lancement et un budget de ressources.
Sur matériel plus limité : traitement plus lent ou par tuiles si qualifié,
limitations explicites du mode avancé, voire désactivation avec explication.
Le débruitage de base reste utilisable sans dépendre de la disponibilité du
backend neuronal. Pas de substitution silencieuse par un autre rendu, de
changement implicite de résolution/couleur, ni de prétention que tous les
matériels produiront les mêmes résultats sans vérification. Prévoir annulation
et libération mémoire ; une insuffisance de ressources ne doit pas bloquer
l'application ou empêcher le retour au moteur de base.

### 54.3. Préparation de la bêta, sans implémentation anticipée

1. Définir domaine d'entrée/sortie, emplacement dans la chaîne RAW et traitement
   des valeurs signées, afin de conserver couleur et normalisation cohérentes.
2. Choisir architecture, poids et backend après examen de leurs licences,
   provenance et coûts ; aucun modèle ou téléchargement décidé ici.
3. Constituer une réserve de qualification non consommée par le réglage des
   poids ou des seuils. La grille actuelle reste un banc de développement,
   pas un test indépendant réutilisable comme tel.
4. Comparer au moteur courant à cadrage/rendu identiques : détail faible,
   texture réelle, couleur, biais dans les noirs, artefacts et cohérence entre
   tuiles. Combiner mesures et validation visuelle utilisateur.
5. Qualifier sur la machine de référence latence, pic mémoire, annulation et
   réactivité ; définir ensuite les capacités minimales et les limitations
   annoncées sur les autres configurations, sans délai chiffré promis ici.

Cette décision clôt la séquence de recherches actuelle. Elle n'autorise ni
push/fusion/tag, ni changement du moteur, ni lancement d'un entraînement.

## 55. Préparation B4 pendant la qualification alpha — 31 octobre 2026

À la demande de l'utilisateur, cadrage documentaire et inventaire matériel sans
nouveau décodage. Le [JSON v001](../raw/preparation-b4-v001-20261031T132231Z.json)
conserve observations et limites ; la
[référence B4](../../architecture/restauration-neuronale.md) décrit les modèles,
contrats et portes du banc. i7-12700K,20 threads annoncés, environ15,77 Gio de RAM
et RTX3060 de12 Gio observés : pas de qualification de latence ou de CUDA.

NAFNet SIDD largeur32 et DRUNet proposés comme premiers témoins RGB, Restormer
ensuite. Aucun de ces poids n'est déclaré compatible avec le RGB caméra signé
avant DCP. Réserve historique14/14/14 conservée, pas de nouvelle réserve
indépendante revendiquée. Les comparaisons devront distinguer perte de
l'adaptateur RGB, filtrage neuronal et effet des tuiles.

Branche locale `patch_neural_restoration` créée à `dev b19b4c3`, sans worktree
ni implémentation. Documentation sur `patch_documentation`. Aucun poids
téléchargé, paquet installé, entraînement ou inférence lancé, pas d'upload
de photos/code/inventaire matériel pendant les recherches publiques.
Prochaine porte : artefacts/droits et protocole gelés, puis pilote sur petits
buffers après libération du budget des tests alpha. Aucun push/fusion/tag.

## 56. Corpus pour nos propres poids et rafales lunaires — 31 octobre 2026

L'utilisateur retient une architecture éprouvée avec nos propres poids et propose
un SSD de 500 Go à préparer lui-même. Le
[JSON de besoins v001](../raw/besoins-corpus-b4-v001-20261031T134511Z.json) conserve
l'audit de métadonnées historiques, les tailles de fichiers et le repérage de D:.
La [section9 du cadrage B4](../../architecture/restauration-neuronale.md#9-orientation-retenue-et-besoins-dimages--31-octobre-2026)
porte la recette proposée : trois scènes pilotes, 192 RAW, avant extension.

Sur D:, 115 CR3 récents candidats lunaires, trépied confirmé par l'utilisateur ;
premier aperçu embarqué vérifié, pas de dématriçage ni de métrique. Le trépied
ne fait pas de leur moyenne une référence propre. Ne pas mélanger ces noms
Canon réutilisés avec les homonymes de base. Aucun fichier copié/supprimé,
aucune action SSD, aucun téléchargement ou entraînement. SIDD et CC0 de
raw.pixls.us envisagés sous audit, pas de corpus externe approuvé en bloc.

## 57. Premier contrat neuronal synthétique — 31 octobre 2026

Autorisation utilisateur de démarrer les tests et d'installer PyTorch. Worktree
isolé `patch_neural_restoration`, commit `b6ad93d`, PyTorch2.9.1+cpu et
NumPy2.3.5 dans `.venv-neural`, sans toucher au Python des tests alpha.
[JSON canonique](../raw/neural-contracts-v001-20261031T190924Z.json) :14 tests
réussis,29 995 paramètres,1,36s interne,2 threads intra-op/1 inter-op.

Contrats et gradients seulement. Un mini-ajustement sur un patch artificiel
n'est pas une campagne d'apprentissage photographique. Ni GPU/ONNX, ni bruit
Canon/détail/couleur/raccords, ni latence pleine image qualifiés. Aucun RAW,
réserve ou TIFF LR lu par le banc ; aucun poids préentraîné acquis, aucun poids
entraîné sauvegardé. Le moteur reste inchangé. Le cours13 et ses TP pédagogiques
sont fournis séparément. Pas de publication, fusion ou tag.

## 58. Rafales lunaires et faisabilité des références — 31 octobre 2026

[Preuve canonique](../raw/moon-audit-v001-20261031T194552Z.json), outil `1aec51c`
sur `patch_neural_restoration`, sept tests de lecteur réussis. Les115 CR3 de
D:/DCIM/101EOSR6 déclarent R6 Mark III à200mm :34 vues ISO1600/f5,6 et81 vues
ISO1000/f9, toutes au1/200s. Ne pas les fusionner sans séparation/raccord.

Trois JPEG embarqués mesurés, pas le signal RAW : changement du cadre/niveaux,
zone lunaire d'environ300pixels. Les canaux encodés à255 du premier aperçu
ne prouvent pas une saturation capteur. Pas de classement de netteté sur
Laplacien confondu avec exposition. Série conservée comme candidate de contrôle
de détail/halos, pas cible propre ou données d'apprentissage qualifiées.

Réutilisation de l'audit de mouvement du27 octobre sur les14 vues de développement :
jusqu'à5,19px à800px de large,0,427° et échelle1,00458. Pas de nouveau calcul
sur la rafale42 ni de pixels de réserve consultés. Moyenne brute non recevable
comme vérité ; un futur recalage reste une possibilité à qualifier. Détails et
limites dans la [section11 de B4](../../architecture/restauration-neuronale.md).
Aucune source modifiée, pas d'entraînement, de push/fusion/tag.

## 59. Normalisation, masques et partitions neuronales — 31 octobre 2026

En attendant les nouvelles prises, contrats du banc sur `patch_neural_restoration`
(`cbe1770`, puis garde-fou `041af0b`). Échelle commune explicite aux trois buffers,
conservation du signal signé aux arrondis float32 près, masques de validité et
refus des scènes/sources/références partagées entre groupes.14 tests passent.

Pilote artificiel fixé avant résultats :8 apprentissage,4 validation,4 test,
64 étapes Adam à0,001, pas de sélection de checkpoint. Les sorties restent
quasi identitaires, aucun gain utile démontré ; ne pas transformer ce banc
technique en optimisation sur16 graines. Sigma est calculé depuis la cible
synthétique, pas depuis un modèle Canon qualifié.

Preuves : [initiale](../raw/neural-data-pilot-v001-20261031T204455Z.json) et
[rejeu du garde-fou](../raw/neural-data-pilot-v001-20261031T204714Z.json), pertes
et métriques identiques. Aucun RAW, réserve ou TIFF LR lu ; aucun poids conservé.
Le prochain jalon est la réception/qualification du corpus et des références,
pas une intégration moteur. La [section12 B4](../../architecture/restauration-neuronale.md)
décrit les contrôles et limites. Aucun push/fusion/tag.

## 60. Placement du débruitage couleur signé/non signé — 31 octobre 2026

Étude ciblée demandée par l'utilisateur et Claude après le changement local
`e88c5b1` de `patch_validation_presets`. Elle ne reprend pas la recherche
incrémentale de modèles suspendue au §54 : les filtres et leurs paramètres
restent inchangés, seul leur placement est comparé. Le développement signé
est la cible déclarée pour les futurs boîtiers ; cela ne vaut pas validation.

Le banc de `patch_raw_bench` (`be72812`, puis compléments de provenance)
compile un moteur figé en worktree détaché `raw_color_engine_e88c5b1`.
Aucun fichier moteur de Claude n'est modifié. Trois variantes prévues :

- Après les tons : chemin courant de Claude.
- Avant DCP : chroma seule de `CameraDenoise`, sans débruitage postérieur.
- Hybride exploratoire : chroma caméra dans les ombres, puis filtre post-tons.

Pour l'hybride, poids pré-DCP fixé avant résultats : guide caméra
I = 0,25 R + 0,5 G + 0,25 B, lissé à sigma 4 pixels ; poids 1 sous 0,002,
transition smoothstep jusqu'à 0,02, puis 0. Le post-traitement garde la même
force. Il cumule deux filtres et ne représente donc pas une force perceptuelle
égale, ni un optimum. Pas de réglage des seuils après observation des résultats.

Les deux politiques de développement sont testées à couleur 25, 50 et 100,
luminance et netteté à zéro, optiques désactivées, pleine définition.
Masques IPR fixés sur son neutre signé et réutilisés dans les deux modes ;
Lightroom conserve ses propres masques, sans comparaison pixel à pixel entre
logiciels. Tuiles 64×64, statistiques sur intérieur 40×40 ; bins de Y linéaire
sRGB [0 ; 0,0003[, [0,0003 ; 0,003[, [0,003 ; 0,03[, [0,03 ; 0,3[,
[0,3 ; 0,8[. Les noirs nuls restent admissibles. Les fractions de valeurs
négatives avant et après DCP servent au diagnostic, pas à démontrer seules
qu'une information a été correctement conservée ou restaurée.

Avant les variantes, le banc exige l'identité à zéro (dont un vrai rerendu
caméra sans correction), la parité exacte du chemin post-tons contre le
serveur de production figé à 0 et 50, puis vérifie que le neutre retenu reste
inchangé. Ces contrôles sont implémentés mais **pas encore exécutés sur RAW**.
Six tests synthétiques du choix de masques et des dénominateurs passent.

Audit terminé : 28 RAW, 28 neutres Lightroom et 84 états couleur seule,
Camera Neutral, sans optique, sans retouche lumière/couleur ni netteté,
dimensions et balance des blancs appariées à leur neutre. Les identités,
XMP, empreintes et protocole sont dans le
[gel initial](../raw/colour-placement-freeze-v001-20261031T212937Z.json).
Ce gel décrit la première tentative, pas les compléments ultérieurs du banc.

Tentative du 31 octobre à 22 h 29 Paris : arrêt avant la première scène,
RAM disponible inférieure au minimum fixé de 5 Gio. Aucun rendu comparatif
mesuré, aucune variante promue, aucun TIFF IPR écrit. Libérer de la mémoire
puis créer un nouveau gel avec les outils courants ; ne pas écraser ce témoin.
Même après calcul, texture réelle et bruit restent mêlés sans vérité propre ;
une baisse de chroma ne sera pas assimilée automatiquement à une amélioration.

### Reprise et correction du banc — 31 octobre, après 22 h 48 Paris

Après libération de mémoire par l'utilisateur, une seconde tentative a terminé
l'audit et commencé 7599, mais s'est arrêtée sur une erreur de traçabilité :
le vérificateur Python traitait le chemin Qt `:/config/raw-normalization.json`
comme un chemin Windows. Ce n'est pas un défaut du décodage ni du filtre.
Le [gel de cette tentative](../raw/colour-placement-freeze-v001-20261031T214845Z.json)
et sa [preuve d'arrêt](../raw/colour-placement-failure-v001-20261031T214845Z.json)
sont conservés ; aucune scène comparative complète n'a été livrée.

Correctif local du banc `80beb79` : le serveur transmet explicitement le type
de ressource et les octets du manifeste embarqué ; Python vérifie leur SHA256
sans accès fichier fictif. Les ressources sur disque restent vérifiées comme
fichiers. Deux tests de non-régression ajoutés, soit huit tests ciblés passés.
Sur un vrai rendu signé neutre 7599 (6984×4660), le contrôle natif passe et
le SHA256 du manifeste embarqué correspond exactement au fichier du moteur
figé. Cela valide ce contrôle de provenance, pas la qualité du débruitage.

Contrôle suivant : [quatre identités natives](../raw/colour-placement-identity-v001-20261031T221824Z.json)
passent sur 7599, modes signé et non signé, placements pré-DCP et hybride,
avec rerendu caméra effectif à force zéro. Les images sont exactement égales
au neutre post-tons de leur mode. Ce témoin ne qualifie pas encore les forces
non nulles ni les 27 autres scènes. Campagne complète relancée à 23 h 18 Paris,
PID local 4008, mêmes filtres et protocole ; résultats encore attendus.

## Conclusion actuelle et fiche de reprise

- Travail local dans `patch_raw_bench`, builds isolés ; pas de modification des
  fichiers moteur attribués à Claude, pas de push/dev/tag.
- Décision du31octobre (§54), prioritaire sur les anciennes fiches de suite :
  études incrémentales suspendues, moteur actuel conservé, voie neuronale locale
  à préparer pour la bêta. Première qualification sur la machine utilisateur ;
  capacités avancées potentiellement limitées ailleurs, moteur de base maintenu.
  Aucun réseau/modèle/backend ni minimum matériel encore qualifié.
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
- Candidat(c1) qualifié au niveau synthétique (§30), puis mesuré par Claude
  (§32) : la texture I ne bloque plus la chroma par le veto partagé, mais
  le bénéfice réel est limité et le résidu à70/25 persiste. Pas de promotion.
- Repli global préparé au-dessus de(c1) (§31) :77 contrôles, sept suites et
  intégration optique passent. Mesuré ensuite par Claude et intégré localement
  dans d4779e5 (§33), avec limites résiduelles et coût de campagne documentés.
- Retour(c1) reçu et commande maître esquissée (§32) : fonction C++ testée
  ici sans caller ;82 contrôles et sept suites passent. Claude prend son
  raccordement en charge. Repère LR85–90 pour les scènes bruitées seulement,
  pas équivalence universelle ; texture à faible bruit à valider visuellement.
- Nouveau chantier algorithmique (§33), étude seulement : repli intégré
  localement par Claude, retour visuel positif ; première sonde confirme DC
  caméra global ; les+3 niveaux sont maintenant reproduits et localisés
  sur les masques exacts (§34), sans attribution causale complète. Un masque
  bruité peut lui-même créer une hausse après débruitage parfait : comparer
  des régions géométriques/références indépendantes avant correction.
  CFA/VST et auto-similarité cadrés,
  réutilisation tierce non juridiquement qualifiée. Aucun nouveau filtre promu.
- Cellules complètes et références LR mesurées (§35) :7599 s'assombrit dans
  toutes les223 cellules sélectionnées, LR également ;9153 varie peu et3135
  n'a pas de cellule admissible. Pas de compensation DC justifiée. Reprendre
  l'inventaire CFA/rafales plutôt que retoucher les seuils de ce diagnostic.
- Inventaire/pilote CFA terminé (§36) :179 empreintes conformes, partitions
  conservées ; six sources de développement testées en module signé à niveaux
  explicites,180 mesures et sortie flottante exacte. Réserve finale fermée.
  Mouvement de la rafale éclairée déjà connu : pas d'oracle temporel non recalé.
  Prochaine cible : signal CFA connu avec bruit de lecture réel, avant toute
  VST ; aucun nouveau filtre ou coefficient de production validé.
- Piste Jensen vérifiée (§37) sans nouveau débruitage : possible en général,
  mais le terme de distribution du F complet atténue les hausses des masques
  fins en luma encodée ; pas d'explication simple par concavité seule.
  D0 confirmé sans changement du code DCP. Pas de compensation photométrique.
- Premier préfiltre CFA testé (§38),32cas à signal connu et résidu noir réel :
  Wiener local simple et combinaison non retenus, protection de détail
  insuffisante ; post courant meilleur en MSE médiane. Faible texture propre
  aussi atténuée par le post courant : cible structurelle, pas hausse aveugle
  deσ. Aucun nouveau filtre intégré ni qualification photo/ressources complète.
- Gain tonal étudié au niveau faisabilité (§39) : dérivée du rendu distincte
  du rapport d'éclaircissement ; résidu maximal réutilisable sur un petit témoin
  global seulement. Ni adaptation locale, ni cache production, ni gain photo
  validés. Prochaine qualification : réutilisation sur corpus puis vrai DCP,
  avec garde-fous de détail, biais, force maître et ressources.
- Priorité utilisateur revenue aux structures faibles (§40) :32 motifs propres
  contaminent l'estimation ; quatre contrôles à signal local identique sont
  conservés avec un entourage propre uniforme. Travailler sur l'acceptation
  des témoins et la prévisibilité spatiale, sans confondre corrélation bruit
  RCD et structure. Aucun nouveau filtre ni seuil promu ; gain tonal différé.
- Prédiction spatiale étudiée (§41) : la structure périodique est reconnue
  quand le rapport signal/bruit le permet ; le correctif de covariance cible
  ne domine pas. Compensation de variance résiduelle encourageante, prior
  encore privilégié. Suite : retirer son amplitude de l'ajustement, tester
  détails non périodiques et confronter au MAD courant, sans confondre
  exactitude du bruit estimé et préservation réelle du détail.
- Épreuve sans amplitude externe terminée (§42) :64cas, bruit réel de noirs
  et détails non périodiques, comparateur réel100/100. Estimateur non retenu
  comme remplacement général : texture fine encore contaminante et forte
  dépendance à la corrélation aux grandes échelles. Préparer une comparaison
  à modèle de bruit connu pour isoler estimation et filtrage, sans promettre
  un oracle exploitable ni intégrer de nouveau veto de texture.
- Comparaison à filtre/force identiques terminée (§43), parité instrumentée
  contrôlée sur128 sorties. Modèle bruit seul : forte amélioration des textures
  à1600, confirmée avec l'autre noir ; pas de gain au fort bruit51200 et
  régressions de contours. Estimation mieux ancrée prioritaire, mais pas
  de remplacement global prêt. Suite : caractérisation dépendante du signal
  et qualification de propagation/routage avant toute interface de production.
- Audit CFA dépendant du signal terminé (§44) sans nouveau fit :24pentes,
  72ancrages locaux et312moments historiques conservés ;48couples de moments
  noirs identiques dans le chemin signé. Normalisation avec décalage de noir
  vérifiée, pas transportRGB/B3. Préparer la propagation avantRCD du terme
  de signal, sans doubler le bruit noir ni confondre modèle temporel et FPN.
- Propagation simulée terminée (§45) : 100 cas bruités, 128 comparaisons,
  profil d'aplat dépendant du signal encourageant ; limite chroma de 11,3 %
  sur texture après médiane. Pas de nouveau filtre ni qualification physique.
  Suite : isoler le bénéfice réel à filtre/force identiques et protéger les
  structures, avant toute proposition d'interface moteur.
- Comparaison réaliste à filtre identique (§46) puis injection directe (§47) :
  le pooled externe réestimé par MAD n'est pas un remplacement global retenu.
  Les covariances empiriques injectées directement améliorent80/80 contrôles
  RGB et I/Co/Cg dans deux distributions, sans régler de coefficient sur les
  réponses. Piste positive à qualifier sur contextes/RAW réels, pas candidat
  de production validé ; détails très faibles encore atténués. Campagne close,
  pas de nouvelle variante aveugle ni de réexploration NLM/Wiener sans différence
  de protocole justifiée. Réserves fermées, aucun calcul actif à la clôture.
- Budget par contexte et transfert préparés/mesurés (§§48–49). Refus des
  contextes inconnus,72 témoins de variance normalisée ; guide observé sans
  MAD de texture.144 cas rejoués à géométrie identique,576 identités et2160
  covariances de contrôle conservées. Grandes bandes encore imprécises,
  contre-exemple σCo0,670 au saut tonal : pas de promotion globale.
  Suite : mesurer leur incertitude puis conserver la structure spatiale des
  transitions dans le budget, avant une revue sur RAW réels. Réserves fermées.
- Audit des biais et réplication (§50) : nouveau guide spatial non promu,
  échec du critère des extrêmes malgré32+32 réalisations. La réalisation de
  lecture figée explique une partie du problème du banc ; ce n'est pas une
  identification du FPN physique. Quatre noirs nouveaux au transport mais
  déjà dans caractérisation ; pas de fausse réserve indépendante.
- Piste statistique multi-noirs (§51) : deux domaines séparés passent les
  critères fixés. Budget de covariance attendu stable sur ce banc, pas rendu
  photo qualifié. Guide spatial modestement meilleur en médiane, plus mauvais
  sur certains extrêmes ; pas d'imposition globale. Contrôles déjà examinés,
  hypothèses et effectifs physiques limités, réserves finales fermées.
- Qualification dans le filtre réel (§52) :64cas,256parités et128identités.
  MSE médiane du rejeu réel0,720fois le courant (spectral),0,663fois (banque),
  mais veto de détail sur stress propre échoué pour les deux. Ni validation
  photo ni réfutation du budget ; substitution pooled et routage confondus.
  La banque simple bat le spectral en MSE, pas preuve de meilleur détail.
  Suite : raccordement local conservant l'adaptation aux structures, gains
  bruités et stress propre séparés ; aucun nouveau seuil après échec.
- Routage conservé (§53) :64cas appariés,640parités. Plafond diagonal sans
  bénéfice MSE médian, secours seul pratiquement neutre sur bruité ; aucune
  variante promue. Suite : budget dépendant du signal local, pas simple cap.
  Adaptation des workers déjà présente (host-1, plafond8, puis nombre de tuiles) ;
  contrôle CPU partagé et montée au-delà de8 restent à qualifier sur pleine
  image. Aucun moteur/parallélisme de production modifié.
