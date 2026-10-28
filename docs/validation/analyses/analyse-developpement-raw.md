# Séparation du développement RAW — rapport évolutif

## Bilan actuel — 28 octobre 2026

Socle X1/X2 préparé sur `patch_raw_bench` depuis `dev` (`6e614ca`). Le
comparateur et les captures après `unpack()` et via l'observateur C2 existent ;
vingt-six tests Python passent via trois groupes CTest. C4 corrigé est
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
La borne de sortie conserve cependant un biais positif ; B n'est pas déployé.

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

## Conclusion actuelle et fiche de reprise

- Branche/worktree : `patch_raw_bench`, build isolé ; aucun fichier moteur
  ou fichier attribué à Claude modifié. Aucun push, fusion vers `dev` ou tag.
- X1/X2 : observateur C2 raccordé, parité de module exacte sur30
  sources ; témoins bruit historiques rejoints aux frontières entières. Les profils, calibration automatique,
  orientation et calibration du pipeline complet rejoignent C3 sur le corpus ;
  replis et performance répétée restent à qualifier séparément.
- I2 accepté par Claude au journal, unités confirmées et prises en compte.
  I3 implémenté avec portée de comparaison explicite et huit tests.
- Contrôle de lecture étendu terminé :30 fichiers,90 ROI, aucun écart.
- C3 intégré localement, campagne complète terminée : 60/60 cas passent.
  Reprendre ces preuves sans relancer un succès. Cette couverture de X1/X2/X3
  ne vaut pas qualification des replis, de tous les formats ou du plafond RAM.
- X4 terminé depuis un snapshot C4 séparé, six paires, sans modification des
  sources C3. Réserve des bornes internes signalée à Claude ; ne pas qualifier
  le mode B d'entièrement signé ni le promouvoir sans nouvelles vérifications.
- La réserve précédente concerne `42f99b6` : corrigée par Claude dans `ee8f26d`,
  nouveau X4 terminé avec capture pré-borne et invariance A. Ne pas confondre
  les deux campagnes ni reprendre le verdict historique comme validation B.
- B est signé jusqu'à la pré-sortie, puis borné : la sortie ne conserve donc
  pas toute la distribution pour un éventuel débruitage aval. Revue visuelle
  A/B et choix de la frontière de traitement restent à faire, production A inchangée.
- L'étude de bruit est suspendue comme demandé ; ses preuves sur `patch_noise`
  ne sont pas promues en validation du développement flottant.
