# Performance d’ouverture et navigation — rapport évolutif

État courant : contrôleur mémoire et préchargement intégrés localement après
accord de l’utilisateur ; voir §6. Attente et admission sous pression, puis
mesure sur corpus sous forte occupation externe : voir §7. Les durées ci-dessous restent les témoins
du diagnostic antérieur, pas une mesure du gain livré par le cache adaptatif.

## Conclusion — 17 octobre 2026

Deux causes distinctes sont mesurées sur le pipeline courant :

1. **La découverte des profils domine une ouverture sans sélection mémorisée :
   environ 30 s**, avant même la décompression RAW. Le code recherche encore
   DCP et LCP même avec un DCP explicite et des corrections optiques désactivées.
2. **Le cache de pixels de 256 Mio ne peut conserver aucun des trois RAW testés** :
   chaque entrée représente 937–944 Mio. Revenir sur une image redéclenche donc
   un import. Avec un budget témoin de 3 Gio, les retours passent d’une médiane
   de **4,75 s à 0,78 s**, sans changer le calcul des couleurs.

Il ne faut pas en déduire qu’il suffit d’imposer 3 Gio à tous les utilisateurs.
Ce témoin établit la cause et motive l’intégration décrite au §6. Lors de cette
campagne de diagnostic, aucun correctif de rendu, budget ou sélection n’avait
été appliqué. Seuls une sonde manuelle et deux jalons de diagnostic
conditionnés par `IPR_LOG_RAW_TIMING` ont été ajoutés au chemin RAW.

## Preuves et méthode

- [Protocole reproductible](../protocols/protocole-performance-ouverture.md).
- [Campagne v001 — environnement restreint](../raw/latence-ouverture-v001-20261016T222237987762Z.json).
- [Campagne v002 — permissions normales](../raw/latence-ouverture-v002-20261016T222908532928Z.json),
  référence principale des conclusions ci-dessous.
- [Sonde C++](../../../tests/opening_latency_probe.cpp) et
  [lanceur de mesure](../../../tests/measure_opening_latency.py).
- [Courbe de navigation dérivée du JSON](../metrics/curves/latence-navigation.svg)
  et données numériques dérivées `performance-ouverture.csv`, générées localement
  par `scripts/export_documentation_metrics.py` et non suivies par Git.

Windows, compilation Release, Qt 6.11.2, LibRaw 0.22.2-Release,
20 processeurs logiques / 20 threads OpenCV déclarés. Trois scènes :
`6V1A0081` et `6V1A0043` (EOS R6 Mark III), `7Y6A7599` (EOS R7).
Camera Neutral, calibration appliquée, WB automatique désactivée,
corrections optiques/CA désactivées. Le JSON conserve les commandes, SHA-256,
durées individuelles, dimensions, profils effectifs, logs et compteurs.
L’état Git est local et modifié : le SHA HEAD seul ne décrit pas le binaire ;
les empreintes du binaire et des sources mesurées sont également conservées.

Trois imports directs par scène dans un processus distinct, puis deux séries
de navigation réelle de l’éditeur `A → B → A → C → A → B → A`.
Les navigations se font sur des copies privées avec leurs propres `.ipr`,
à 5200 K / nuance 0. Les imports directs utilisent la température de référence
As Shot estimée. Les originaux, leurs `.ipr`, la configuration et les profils
ont leurs **empreintes inchangées avant/après les deux campagnes**.

## 1. Décomposition de l’import

Campagne v002, durées en secondes. « Chaud » = médiane des imports 2 et 3
dans le même processus, pas une mesure après purge du cache disque système.

| Scène | Premier import | Recherche profils, premier import | Import chaud | Proxy chaud | Aperçus origine/actuel + histogramme, chaud |
| --- | --- | --- | --- | --- | --- |
| 6V1A0081 | 33,960 | 29,991 | 3,937 | 0,045 | 0,548 |
| 6V1A0043 | 3,895 | 0,113 | 3,789 | 0,043 | 0,474 |
| 7Y6A7599 | 33,762 | 29,754 | 3,972 | 0,040 | 0,720 |

Les premiers imports de `0081` et `7599` créent une sélection persistante
avec `QSettings status=0` (succès). `0043`, dans un nouveau processus, réutilise
ensuite la sélection de son couple boîtier/objectif. La recherche tombe à
113 ms ; les imports suivants du même processus sont à 6–8 ms environ.

Sur les imports directs de v002 : décompression `unpack` 222–255 ms ;
niveaux noirs/WB/calibration 2–4 ms ; `dcraw_process` 2,61–2,73 s ;
couleur/optique et assemblage associés 0,64–0,80 s. Le jalon historique
`unpack_and_process` a désormais pour delta le traitement effectué après
`black_wb_calibration` ; son temps écoulé reste cumulatif.

Le proxy n’est donc pas le coût dominant. Le décodage/dématriçage reste un
coût réel après suppression du coût de recherche, mais **remplacer LibRaw ou
porter les calculs sur GPU n’est pas la première réponse aux 30 s observées**.

## 2. Pourquoi la recherche est coûteuse

Dans [RawDecoder.cpp](../../../src/raw/RawDecoder.cpp), `findProfiles()` est
appelé avant l’application du DCP explicite et avant la désactivation de la
sélection LCP. [CameraProfileEngine.cpp](../../../src/profiles/CameraProfileEngine.cpp)
parcourt les répertoires et prépare des profils pour chercher une correspondance.
Le catalogue local `profiles/index.json` est vide dans cette campagne.

Lors des premières recherches, la sonde compte **3 867 préparations** pour
le R6 III et **3 869** pour le R7, contre **7** pour le premier import R6 III
du processus qui réutilise la sélection persistante. Ces nombres agrègent
DCP et LCP : ce ne sont pas 3 867 profils réellement appliqués à une photo.
Le profil effectivement appliqué est bien Camera Neutral ; le log de stockage
de la sélection automatique Adobe Standard ne signifie pas que le DCP explicite
Neutral a été ignoré.

Le cache de profils préparés est borné à 16 entrées et ne remplace pas un
index de recherche. Augmenter arbitrairement sa taille pour garder tous les
profils rencontrés ne résoudrait pas proprement cette phase de découverte.

## 3. Cache des pixels et navigation

Le comptage de [DecodedImageCache](../../../src/cache/DecodedImageCache.cpp)
inclut master RGBA float32, signal caméra RGB retenu et leurs proxies,
sans compter deux fois une allocation partagée.

| Scène | Octets par entrée | Mio | Admise avec 256 Mio |
| --- | --- | --- | --- |
| 6V1A0081 / 6V1A0043 | 982192988 | 936,69 | Non |
| 7Y6A7599 | 989661568 | 943,81 | Non |

Le cache refuse explicitement une entrée plus grosse que son budget.
Dans les sept étapes de navigation v002 à 256 Mio : **zéro entrée conservée**.
Le coût n’est donc pas une copie inutile à chaque insertion : l’insertion
n’a pas lieu. Les temps « moteur prêt » sont :

| Étape | Scène | Cache 256 Mio, s | Cache témoin 3 Gio, s |
| --- | --- | --- | --- |
| 1 | A — 6V1A0081 | 4,832 | 4,833 |
| 2 | B — 6V1A0043 | 4,453 | 4,402 |
| 3 | A — retour | 4,789 | 0,779 |
| 4 | C — 7Y6A7599 | 5,023 | 4,949 |
| 5 | A — retour | 4,829 | 0,806 |
| 6 | B — retour | 4,432 | 0,701 |
| 7 | A — retour | 4,704 | 0,788 |

La médiane des étapes 3/5/6/7 passe de 4,746 s à 0,784 s, soit environ
83,5 % de latence en moins dans ce témoin. Le nouveau budget ne réduit pas
le coût d’une image jamais chargée. Trois entrées occupent 2 954 047 544 octets
comptabilisés ; ce chiffre n’est **ni le RSS ni un pic mémoire global**.
Le contrôle d’un master lors d’un hit coûte encore environ 91–93 ms ;
le rendu des aperçus et le délai de regroupement de 80 ms expliquent une
partie du temps restant. Aucune mesure séparée de la sauvegarde `.ipr` ou
de l’upload GPU n’est disponible dans cette campagne.

## 4. Effet des permissions — ne pas confondre défaut produit et sandbox

La campagne v001 journalise `QSettings status=1` : l’environnement restreint
refuse l’écriture du cache applicatif dans AppData. Les premiers imports
des trois processus prennent 34–65 s, dont 30–60 s pour les profils.
La navigation paie de nouveau la découverte lors du premier R6 III et du
premier R7 (environ 46 s). Cette preuve décrit un **cache persistant indisponible**,
pas une panne générale démontrée chez l’utilisateur.

La campagne v002, autorisée avec les permissions normales, réussit l’écriture
et confirme la réutilisation entre processus. Ne pas annoncer que l’ouverture
prend systématiquement 30–60 s : cela concerne ici une sélection absente ou
non persistée. Un nouveau couple boîtier/objectif peut toutefois déclencher
une recherche à nouveau, ce qui reste un problème de conception à traiter.
Cette campagne a donc alimenté le cache applicatif normal pour ces couples ;
cet état chaud doit être pris en compte lors d’une prochaine mesure manuelle.

## 5. Ordre recommandé des améliorations — non implémentées

1. **Éviter la découverte générale inutile** quand DCP et choix optique sont
   déjà résolus. Indexer les profils par boîtier/objectif ; préparer seulement
   les candidats nécessaires et garder un repli contrôlé. Tester explicitement
   un cache persistant inaccessible et un dossier mixte boîtier/objectif.
   Risque principal : conserver les bonnes règles de compatibilité et d’invalidation.
2. **Repenser le cache des images pour ses tailles réelles** : niveaux proxy/master,
   budget configurable et borne globale adaptée à la RAM disponible, sans
   transformer le témoin de 3 Gio en valeur imposée. Tester éviction, restauration
   de recette, références partagées et variation de boîtier. Vérifier aussi
   l’identité de cache après résolution automatique d’un profil sur une image
   sans sidecar, scénario non couvert par les recettes préinitialisées ici.
3. **Afficher rapidement un aperçu**, puis développer la pleine résolution
   en arrière-plan ; préchargement des voisins strictement borné et abandon
   des résultats obsolètes lors d’une navigation rapide. L’export garde le
   calcul pleine résolution et le rendu accepté. Le gain n’est pas encore mesuré.
4. Après ces actions, profiler le dématriçage, les deux conversions d’aperçu et
   l’histogramme. Ne pas dégrader la qualité ou changer la colorimétrie pour
   gagner des millisecondes sans comparaison numérique et visuelle.

## 6. Intégration du contrôleur mémoire et du préchargement

Après accord utilisateur, le LRU de l’éditeur est remplacé par un contrôleur
sur thread dédié qui réutilise la primitive LRU, sans chemin de navigation
alternatif conservé. [Architecture](../../architecture/controle-memoire-images.md)
et [ADR-0015](../../decisions/ADR-0015-controle-memoire-et-prechargement.md)
décrivent les seuils 80/75 %, la rétention `min(25 % RAM, 8 Gio)`, les
réservations et la marge supplémentaire de sécurité. Le nombre d’images
retenues et la pression apparaissent au pied de fenêtre.

Les admissions, accès au cache et contrôles du master sont hors du thread
IHM. Le préchargement n’effectue qu’un calcul anticipé, voisin suivant puis
précédent. Une sélection de ce voisin en cours peut promouvoir son résultat ;
les autres résultats périmés sont abandonnés. L’image courante est conservée
jusqu’à la réussite du remplacement, y compris lors d’un refus mémoire.
Les profils/configurations et réglages restent isolés par clé et par sidecar.

Un probe de dimensions optionnel est ajouté au registre des formats RAW :
lecture d’en-tête LibRaw, probe propre aux futurs adaptateurs ou taille raster
Qt. La consommation est provisionnée avant décodage, rendu et export IHM,
pas après coup uniquement. Le générateur statique hors ligne n’est pas couvert
par ce contrôleur de navigation. Les vignettes et profils gardent leurs caches
distincts ; la recherche initiale de profils n’est pas optimisée par ce patch.

Les tests couvrent concurrence d’admission, éviction LRU, pression et hystérésis,
marge de commit, sources actives, promotion, absence de sidecar spéculatif,
restauration des retouches, sélection rapide et conservation de l’image/recette
en cas de refus. Les fixtures de petits fichiers injectent désormais une RAM
déterministe ; le sampler Windows garde son contrôle natif séparé. Deux tests
anciens dépendaient de la pression réelle de la machine et ont échoué dans
la première passe complète : la mémoire canonique conserve cette campagne,
sans en faire une validation réussie. L’ancienne assertion de vidage immédiat
des aperçus est remplacée par la conservation jusqu’au remplacement réussi.

Preuves immuables : [v001, première passe non validée](../raw/integration-cache-memoire-v001-20261017T090820222167Z.json),
puis [v002, 26/26 tests natifs réussis](../raw/integration-cache-memoire-v002-20261017T090942983646Z.json)
en 28,83 s, après correction des fixtures. La suite Python complète réussit
également : 237 tests, dont un ignoré. L’application et le générateur Windows
sont reconstruits en Release. Ni CI GitHub, ni nouveau standalone distribué,
ni validation physique Linux/macOS ne sont annoncés par ces preuves.

La politique vise le seuil de 80 %, sans garantie absolue face aux autres
applications et allocations non instrumentées. Les provisions initiales sont
conservatrices : si la RAM est déjà très occupée, un message de refus est
préférable au lancement de plusieurs décodages concurrents. Pic RSS réel,
quantité optimale de voisins, validation Linux/macOS et nouvelle mesure
chronométrée sur corpus restent à faire. Les chiffres du témoin de 3 Gio
ne sont pas transférés automatiquement à cette implémentation adaptative.

## 7. Reprise de validation sur corpus — attente et admission sous pression

Même machine Windows, 15,77 Gio de RAM effective, session ordinaire
(VS Code, navigateur, Claude, antivirus) : **82–85 % d’utilisation globale
avant toute allocation d’IPR**. La sonde ouvre désormais le dossier privé,
ce qui active le préchargement réel. Elle compare la limite `0` (sans
rétention ni préchargement), `auto` en navigation enchaînée et `auto` avec
pause jusqu’à la fin du préchargement. Elle relève aussi les compteurs du
contrôleur et les compteurs mémoire Windows du processus.

**v003 — échec conservé.** Dès la première étape du mode `0`, l’ouverture est
refusée : 84,6 % d’utilisation globale pour un processus IPR de 23 Mio.
La règle « disponible − 20 % RAM − marge ≥ pic estimé » imposait environ
63 % d’utilisation avant d’ouvrir un RAW de 32 MP : IPR était inutilisable
sur ce poste. Le contrôleur respectait l’ADR initial ; la politique était inadaptée.

**Décision utilisateur, même jour** : évincer d’abord les anciennes images, puis
attendre avec message. Si IPR n’a plus rien à libérer, admettre l’ouverture
sous pression, dans la limite de commit Windows. Voir l’amendement de
l’[ADR-0015](../../decisions/ADR-0015-controle-memoire-et-prechargement.md).
L’estimation du pic passe de 96 à 80 o/px après mesure.

| Scène, 32,3 MP | Pic processus mesuré, Gio | Ancienne estimation, Gio | Estimation 80 o/px, Gio | Retenu après import, Gio |
| --- | --- | --- | --- | --- |
| 6V1A0081 / 6V1A0043 (R6 III) | 2,20 | 3,17 | 2,66 | 0,96 |
| 7Y6A7599 (R7) | 2,21 | 3,14 | 2,64 | 0,97 |

**v004 — complète, entrées inchangées.** Les 21 étapes de navigation (3 modes × 7)
réussissent : aucun refus, aucune expiration. Chaque étape compte deux
admissions sous pression (chargement et rendu). Les temps « prêt » restent
de 4,47–5,02 s, comme sans cache. L’utilisation globale échantillonnée
atteint 86–95 % : la rétention et le préchargement restent donc suspendus
(0 hit, 0 entrée retenue). **Ce résultat ne mesure pas le gain du cache** ;
il montre que le contrôleur s’efface correctement lorsque la RAM est déjà
occupée par d’autres logiciels.

En navigation, le pic du processus atteint 3,15 Gio : l’image affichée (0,96 Gio)
reste en mémoire jusqu’à l’arrivée de sa remplaçante, pendant un nouveau décodage
(≈ 2,2 Gio). La réutilisation du cache demande moins de 75 % d’utilisation globale ;
sur 16 Go, il faut fermer une partie des autres applications. Une mesure du
gain dans cet état reste à faire.

Preuves : [v003 en échec](../raw/latence-ouverture-v003-20261017T125928172351Z.json),
[v004 complète](../raw/latence-ouverture-v004-20261017T131826847062Z.json).
Tests synthétiques ajoutés : attente FIFO, annulation par navigation,
abandon à la fermeture, admission sous pression seulement quand IPR n’a plus
rien à libérer, refus immédiat d’une image impossible sous 80 %, et panneau
d’informations décrivant l’image affichée plutôt que la cible en attente.

Intégration : [v003, échec conservé](../raw/integration-cache-memoire-v003-20261017T132350650301Z.json).
Une course **dans le test** comptait comme admises d’emblée des demandes réveillées
pendant les libérations ; le contrôleur n’est pas en cause. Après correction,
huit répétitions consécutives réussissent, puis
[v004, 26/26 tests natifs](../raw/integration-cache-memoire-v004-20261017T132754845683Z.json).
Le build Windows Release complet enregistre son point de validation :
26 CTest et 223 tests Python, dont un ignoré, décomptés depuis les lignes `Ran` des suites.
Ni CI GitHub, ni Linux/macOS, ni standalone distribué ne sont couverts.

**Miniatures et aperçu.** L’utilisateur a signalé que l’image précédente restait
affichée après un changement dans l’IHM ; la sonde moteur ne reproduit pas ce défaut.
Cause identifiée dans le code : les fournisseurs `editor` et `thumbnail` étaient
synchrones avec `asynchronous: true`. Qt les sert alors sur un **seul thread de lecture**.
L’aperçu de l’image ouverte attendait donc derrière les miniatures RAW en file.
Les miniatures passent désormais par un `QQuickAsyncImageProvider` doté
d’un pool dédié de 2 à 4 threads de basse priorité, avec annulation des
délégués sortis du ruban. Test `thumbnailProviderTests` : parallélisme borné,
cache, éviction de fenêtre et annulation. Le correctif reste à confirmer dans l’IHM.

**Rétention à 58 % d’utilisation de départ** (rejeu diagnostique, sans JSON) :
navigation réussie, mais aucune image retenue. L’IHM utilisait déjà ~77 %
avec l’image affichée et la réservation de décodage. Pour respecter la cible de
80 %, chaque ouverture évince l’image précédente, et le préchargement est refusé.
Sur 16 Go, garder un RAW de 32 MP en plus de celui affiché demande donc
moins d’environ 50 % d’utilisation par les autres logiciels.

## 8. Études : aperçus intégrés et rétention allégée — non implémentées

### 8.1 Préchargement par métadonnées et aperçus intégrés

Mesure : [apercus-integres-v001](../raw/apercus-integres-v001-20261017T134839267649Z.json),
19 CR3 de `tests/image/base`, médianes de trois passes, une instance LibRaw
neuve par opération. Le cache disque est chaud.

| Opération | Médiane, ms | Plage, ms |
| --- | --- | --- |
| Ouverture LibRaw : métadonnées, dimensions, liste des aperçus | 0,32 | 0,3–0,4 |
| Miniature du ruban, chemin actuel (plus grand JPEG puis réduction) | 204,2 | 126–237 |
| Plus petit aperçu ≥ 116×49 (160×120) | 0,87 | 0,8–0,9 |
| Aperçu 1620×1080, prêt à afficher | 12,6 | 8,5–14,3 |
| Développement complet (rappel §1) | ≈ 4 000 | — |

Chaque CR3 contient trois aperçus : 160×120 (7–19 Kio), 1620×1080
(107–646 Kio) et un JPEG pleine taille (0,9–5,2 Mio). Le ruban décode
aujourd’hui ce dernier pour afficher une vignette de 116×49.

Pistes, de la plus simple à la plus structurante :

1. **Ruban** : utiliser l’aperçu 1620×1080 (≈ 16 fois plus rapide) ou le 160×120
   (≈ 235 fois plus rapide). Celui-ci peut contenir des bandes noires chez Canon ;
   un contrôle visuel est nécessaire avant de le retenir.
2. **Affichage immédiat** : au clic, afficher l’aperçu 1620×1080 en ~13 ms,
   signalé « aperçu boîtier ». Le développement complet le remplace ensuite.
   Les réglages, l’histogramme et la comparaison restent désactivés
   jusqu’au rendu IPR. Le style d’image du boîtier diffère de Camera Neutral :
   le changement visuel doit être assumé ou atténué.
3. **Préchargement par métadonnées** : pour les voisins, lire seulement
   métadonnées et aperçu 1620×1080 (≈ 7 Mio décodés chacun, ARGB32).
   Dix voisins coûtent environ 70 Mio, contre ~940 Mio par RAW développé.
   La pression mémoire de 16 Go ne l’empêche donc pas.

### 8.2 Rétention allégée

Composition mesurée d’une entrée de 32,3 MP (982 192 988 octets, §3) :

| Composant | o/px | Mo | Usage après ouverture |
| --- | --- | --- | --- |
| Master RGBA float32 pleine résolution | 16 | 516 | Export seulement |
| Signal caméra RGB float32 pleine résolution | 12 | 387 | Changement de DCP sans redécodage |
| Proxy 2048 px, master et signal caméra | 28 | ≈ 78 | Aperçu, réglages, histogramme, zoom |

L’aperçu, les réglages et le zoom utilisent le proxy : seul l’export lit
le master, et seul un changement de DCP lit le signal pleine résolution.

- **Proxy seul pour les images quittées** : ≈ 78 Mo par entrée, 12 fois moins.
  Au retour, l’image est éditable immédiatement. Le master se reconstruit
  en arrière-plan ou au moment d’un export. Sur 16 Go, une quarantaine d’entrées
  tiennent dans le budget actuel au lieu de quatre.
  Risques : export retardé si le master est absent, invalidation des clés,
  et cohérence entre proxy conservé et redéveloppement.
- **Libérer le signal caméra des images quittées** : ≈ 590 Mo par entrée.
  Gain plus faible (−40 %), mais le changement de DCP redécode au retour.
  Cette option est dominée par la précédente, qui peut conserver le signal
  caméra du proxy pour l’aperçu d’un changement de profil.

Ces études ne changent ni le rendu ni la colorimétrie. Une implémentation
devra mesurer la latence de retour, l’export et la mémoire du processus.

## 9. Implémentation des études — 17 octobre 2026

Les trois étapes du §8 sont intégrées localement :

1. **Ruban** : le plus petit aperçu intégré d’au moins 1024 px de grand côté
   (1620×1080 sur les CR3), au lieu du JPEG pleine taille.
2. **Rétention du proxy seul**, avec reconstruction du master à l’export.
3. **Aperçu du boîtier au clic**, badge « Aperçu boîtier », remplacé par le rendu IPR.

La première mesure réelle (v005, complète) n’a retenu aucune image : le pic
admis du décodage déclenchait la pression et évinçait les proxies. La mesure
suivante (v006, échec conservé) a révélé deux défauts :

- **Alpha du proxy** : la réduction `INTER_AREA` dépassait légèrement 1, et un proxy
  restauré échouait à la validation d’un master. Le test factice ne le voyait pas ;
  corrigé et couvert au rapport réel 6959 → 2048.
- **Limite `0` ignorée** : défaut antérieur, l’éditeur comparait la limite demandée
  au budget publié de façon asynchrone. Les modes `0` et `auto` des campagnes
  v004–v005 étaient donc identiques.

Avec la pression établie hors réservations (amendement de
l’[ADR-0015](../../decisions/ADR-0015-controle-memoire-et-prechargement.md)) :
[v007, complète](../raw/latence-ouverture-v007-20261017T141550004165Z.json),
58 % d’utilisation de départ.

| Mode | Retour sur image vue, s | Image jamais ouverte, s | Premier affichage, ms |
| --- | --- | --- | --- |
| Limite `0` | 4,43–5,03 | 4,43–5,03 | 18–30 |
| `auto`, enchaîné | 0,61–0,74 | 4,48–4,93 | 18–30 |
| `auto`, pause et préchargement | 0,64–0,71 | 0,87 (7Y6A7599 préchargée) | 18–30 |

Deux entrées retenues totalisent 0,146 Gio. L’utilisation globale échantillonnée
reste ≤ 74 % et le pic du processus atteint 3,22 Gio. Le premier affichage est
celui de l’aperçu du boîtier, pas du rendu IPR. L’export reconstruit d’un proxy
est couvert par un test factice, pas encore chronométré sur RAW réel.
Intégration [v005, 27/27](../raw/integration-cache-memoire-v005-20261017T141902761671Z.json) ;
validation Windows : 27 CTest, 223 tests Python, dont un ignoré.

**Validation manuelle de l’utilisateur, 17 octobre 2026.** Dossier de 500 CR3,
utilisation de la mémoire de base à 50 % puis à 70 % : aucun problème détecté ;
les miniatures se chargent quasi instantanément, y compris pendant le défilement.
L’état est approuvé. Ce retour qualitatif ne remplace pas une mesure chronométrée.
Amélioration reportée, non bloquante : la transition visuelle entre l’aperçu
du boîtier et le rendu IPR.

## Vérifications de l’outillage

La sonde compile en Release. Sept tests natifs ciblés réussissent : lifecycle,
export/cache, décodeur RAW, import commun, restauration/développement RAW,
profils et cache de profils préparés (16,31 s au total). Les tests Python de
lecture des timings/empreintes (2), de dérivation documentaire (7) et de
sélection CI (7) réussissent également. Ces tests ne constituent pas une
nouvelle comparaison photographique du corpus complet ni une CI GitHub.
Les contrôles documentaires ne relèvent aucun lien local manquant.

## Limites et suite

Ces mesures s’arrêtent à l’aperçu CPU publié par le provider / état prêt.
La première image réellement présentée par QML, l’upload GPU, le parcours du
dossier, la concurrence des vignettes, les clics rapides et les formats raster
JPEG/TIFF/PNG restent à mesurer. Trois RAW et trois répétitions ne constituent
pas une garantie sur tous les matériels, objectifs ou fichiers. Le disque
système n’a pas été purgé ; ni pic RSS, ni charge externe exhaustive n’ont été mesurés.

Les copies privées des campagnes restent sous `build-latency-investigation/`
(environ 98 Mio par campagne, hors petits sidecars). Aucun export ni original
n’a été supprimé. L’ancien [rapport de performance du 1er octobre](../../reports/archive/technical/rapport-optimisation-performance-2026-10-01.md)
reste historique : son pipeline et ses conditions ne permettent pas de chiffrer
une régression temporelle comparable au pipeline Rec.2020 actuel.
