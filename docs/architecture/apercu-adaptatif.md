# Aperçu adaptatif : zone visible en pleine résolution

Branche `patch_adaptive_preview`, ouverte le 12 novembre 2026 depuis `dev`
(`b7a4b14`, bêta-1). Chantier décidé par l'utilisateur le 24 octobre 2026 et
lancé après la livraison de la bêta-1.

## Point de départ

La bêta-1 rend **toute l'image** en pleine résolution, par le rendu de l'export,
dès que le zoom agrandit l'aperçu de 2048 px ([contrat](../specifications/zoom-image.md)).
Les pixels sont justes, mais le coût est celui d'un export à chaque réglage :
environ 96 Mio de plus et une attente de plusieurs secondes sur un RAW de 24 Mpx
chargé d'effets.

Le but est de rendre **seulement la zone visible**, avec une marge, en
gardant le même résultat que l'export.

## Invariant

Pour tous réglages et pour toute zone, le rendu de la zone est **strictement
égal** au recadrage du rendu pleine image sur cette zone (décision de
l'utilisateur du 12 novembre 2026) : deux tuiles voisines se raccordent sans
couture. Un test compare les deux sur des zones au centre,
au bord et dans les coins, pour chaque étape et pour leurs combinaisons.

**Repli** : un réglage dont une étape ne sait pas encore se rendre par zone
garde le rendu de toute l'image de la bêta-1. Le chantier n'est jamais un
compromis sur les pixels : il étend la liste des étapes rendues par zone.

## Audit des étapes (`RenderChain::workingImage`)

| Étape | Portée spatiale | Dépendance globale | Rendu par zone |
| --- | --- | --- | --- |
| Signal caméra, BdB, exposition de scène (DCP) | par pixel | — | direct |
| Débruitage du signal caméra (expérimental) | voisinage | **modèle de bruit estimé sur des aplats de toute l'image** (`CameraDenoise::estimate`) | repli d'abord |
| Géométrie (rotation, perspective, échelle) | rééchantillonnage | — | zone source = image inverse de la zone affichée (`Geometry::toSource`), plus la marge de l'interpolation |
| Exposition, contraste, courbes, couleur (`PerceptualColour`) | par pixel | — | direct |
| Ombres/noirs/hautes lumières/blancs (`ToneRecovery`) | par pixel | statistiques mesurées au chargement | direct (statistiques déjà partagées) |
| Retouches | zone et sa source, n'importe où | contexte proportionnel à la taille de la zone | élargir la région aux retouches qui la touchent, ou repli |
| Masques (`LocalAdjustments::valueMaps`) | paramétriques, plage de luminance par pixel | — | cartes calculées sur la région seule (nouvelle entrée de région) |
| Bruit de luminance et de couleur (`DetailProcessing::reduceNoise`) | rayon jusqu'à 24 px plein format | **σ du bruit estimé sur tout le plan** (`estimateNoiseSigma`, `estimateDenoiseSigma`) | σ mesuré une fois sur l'image entière puis transmis |
| Texture, clarté (`LocalContrast`) | bandes jusqu'à σ = 256 px plein format, flous à résolution réduite par puissances de 2 | — | marge d'environ 3σ (768 px) ; réductions alignées sur la grille de la pleine image |
| Voile (`Dehaze`) | flous σ = 64 et 128 px à ¼ de résolution | **lumière du ciel : 99ᵉ centile de toute l'image** | lumière du ciel mesurée une fois, transmise ; marge d'environ 400 px |
| Flou, netteté | rayon fixe | netteté : σ du bruit estimé sur le plan | σ transmis ; marge du rayon |
| Grain | bruit tiré par position (`gaussian(x, y)`), puis flou σ 0,75 | — | coordonnées absolues transmises : identique par construction |
| Recadrage, sortie sRGB | — | — | direct |

**Constats** : trois mesures globales (σ du bruit pour le débruitage et la
netteté, lumière du ciel du voile, modèle du débruitage caméra) et deux grands
rayons (clarté, voile). Sans clarté ni voile, la marge tient en quelques
dizaines de pixels, et la zone rendue est à peu près l'écran ; avec la clarté,
la marge atteint environ 770 px de chaque côté.

## Conception proposée

1. **Contexte global** : un petit objet de mesures calculées une fois par
   source et par préfixe de réglages qui les influencent (σ du bruit à l'entrée
   du débruitage et de la netteté, lumière du ciel), à partir de l'image
   entière, et mis en cache comme les étapes. Le rendu par zone le reçoit au lieu
   de remesurer. L'export s'en sert aussi, ce qui garde un seul chemin de calcul.
2. **Rendu de région** : `RenderChain` reçoit une région de la toile (en
   pixels de la pleine image) et une marge calculée d'après les étapes
   actives. Il rend la région élargie à partir de la zone source
   correspondante, puis rogne la marge. Les modules qui lisent des
   coordonnées (grain, masques, retouches, réductions de la clarté) reçoivent
   l'origine de la région.
3. **Affinage progressif par tuiles** (décidé par l'utilisateur le 12 novembre
   2026) :
   - l'aperçu agrandi s'affiche aussitôt ; la zone visible est découpée en
     tuiles de 512×512 pixels du fichier, rendues en pleine résolution l'une
     après l'autre, **en spirale depuis le centre de la vue** (depuis le
     pointeur après un zoom à la molette ou au clic), et posées au-dessus de
     l'aperçu agrandi dès qu'elles sont prêtes ;
   - **couronne de préchargement** : une bande de 512 px autour de la zone
     visible est rendue ensuite, pour qu'un déplacement trouve ses tuiles prêtes ;
   - **déplacement** : les tuiles qui sortent de la zone visible et de sa
     couronne, du côté opposé au mouvement, sont libérées ; les tuiles en
     attente de ce côté sont retirées de la file, et celle en cours de rendu
     est abandonnée à son prochain point d'annulation ; la file est
     réordonnée en spirale depuis le nouveau centre, la couronne d'abord dans
     le sens du mouvement ;
   - **changement de réglage** : toutes les tuiles sont masquées et la file
     annulée ; on repart de l'aperçu agrandi des nouveaux réglages. Une tuile
     d'autres réglages n'est jamais montrée ;
   - **étapes à grand rayon partagées** : clarté (bandes jusqu'à σ = 256 px) et
     voile (cartes à ¼ de résolution) sont des variations lentes, déjà
     calculées sur des images réduites ; leurs couches réduites sont calculées
     une fois pour la zone visible, sa couronne et leur marge, puis lues par
     chaque tuile. Une tuile n'a alors qu'une petite marge (rayon du bruit, de
     la netteté, de l'interpolation) : sans cela, une marge de 770 px autour
     d'une tuile de 512 px multiplierait les pixels calculés par 16 ;
   - un niveau intermédiaire (mi-résolution) n'est ajouté que si les tuiles
     pleine résolution restent trop lentes à venir.
4. **Proxy adapté aux ressources** (second volet) : taille de l'aperçu selon
   la mémoire et les threads de l'hôte ([cible de ressources](controle-memoire-images.md)).

## Mesure du rendu actuel — 12 novembre 2026

Sonde `detailRenderProbe` (`tests/detail_render_probe.cpp`), meilleur de 2 rendus,
20 threads, sans cache d'étapes ni réseau
([preuve](../validation/raw/adaptive-preview-full-resolution-cost-v001-20261112T114126Z.json)).

| Réglages | R6 III 32 Mpx (4639×6959), plein / aperçu | 60D 18 Mpx, plein / aperçu |
| --- | --- | --- |
| Neutre | 0,9–1,0 s / 80 ms | 0,6 s / 90 ms |
| Tons et couleur | 5,9–6,1 s / 0,52 s | 3,3 s / 0,53 s |
| + bruit (luminance 40, couleur 30) | 8,7 s / 0,74 s | 4,8 s / 0,76 s |
| + netteté 40 | 6,4–6,5 s / 0,54–0,61 s | 3,6 s / 0,56 s |
| + texture 20, clarté 30 | 7,1 s / 0,65 s | 3,9 s / 0,65 s |
| + voile 25 | 6,5 s / 0,82 s | 3,7 s / 0,63 s |
| Tout | 11,1–11,2 s / 1,2 s | 6,2 s / 1,0 s |

Pic de mémoire du processus : 3,7 Gio avec le RAW de 32 Mpx chargé.

**Lecture.**

- Le R6 III fait **32 Mpx**, non 24 : le zoom de la bêta-1 attend 6 à 11 s
  après chaque réglage. Le rendu par zone est donc nécessaire, pas un confort.
- Le coût suit le nombre de pixels : rapport 11 à 12 entre le plein format
  (32,3 Mpx) et l'aperçu (2,8 Mpx, 11,6 fois moins de pixels). Une zone d'écran
  2560×1440 (3,7 Mpx) sans grande marge coûterait donc
  environ 0,7 à 1,3 s ; avec la marge de la clarté (≈ 770 px), environ 12 Mpx
  et 2,5 à 4 s.
- « Tons et couleur », sans aucun filtre de voisinage, coûte déjà 6 s : une
  étape par pixel est anormalement lente en pleine résolution. La profiler
  avant le rendu par zone peut diviser tous les temps.

### Première correction : boucles par pixel parallélisées — 12 novembre 2026

Le contraste global, les ombres/hautes lumières globales, la balance des blancs
hors DCP, le niveau de gris, la surimpression des poids et `ipr::allFinite`
(appelé sur toute la source à chaque rendu) tournaient sur un seul thread. Ils
passent par `ipr::parallelRows`, ligne par ligne, sans changer le calcul d'un
pixel (80 tests CTest identiques).

| Réglages (R6 III 32 Mpx) | Avant | Après |
| --- | --- | --- |
| Neutre | 0,92 s | 0,55 s |
| Contraste seul | 3,54 s | 1,08 s |
| Ombres/hautes lumières seules | 1,62 s | 0,83 s |
| Tons et couleur | 5,7–6,1 s | 2,55 s |
| + bruit | 8,7 s | 5,1 s |
| Tout | 11,2 s | 7,2 s |

Le 60D passe de 6,2 à 4,0 s pour « tout »
([preuve](../validation/raw/adaptive-preview-full-resolution-cost-v002-20261112T121921Z.json)).
Ce gain vaut aussi pour l'aperçu et l'export. Restent les plus coûteux :
exposition de scène (≈ 0,9 s), vibrance (≈ 0,9 s) et débruitage (≈ 2,5 s).

### Rendu par zone, premier lot — 12 novembre 2026

`RenderChain::workingRegion` rend une zone du canevas pleine résolution
(avant recadrage) à partir de la seule fenêtre de la source qu'elle lit
(`Geometry::sourceWindow` : noyau d'interpolation, arrondi, bord répété et flou
d'anticrénelage compris). `RenderChain::measureWhole` fournit ce que la zone
ne peut pas mesurer sur ses propres pixels : les statistiques des tons de la
source et, quand la géométrie rééchantillonne, le **plancher de la
déformation** (`Geometry::warpFloor`, minimum de chaque canal avant la
déformation, troisième mesure globale trouvée en codant, absente de l'audit
ci-dessus). La déformation par zone suit les blocs d'arrondi de la sortie
entière, ce qui la rend identique à l'octet près.

Recettes rendues par zone : signal caméra et DCP, noir et blanc, exposition,
contraste, balance des blancs, quatre tons, couleur perceptuelle (saturation,
vibrance, mélangeur, étalonnage, couleur du point), courbes, géométrie
complète. Les autres (masques, retouches, bruit, texture, clarté, voile, flou,
netteté, grain) répondent `rendersByRegion == false` : le rendu entier reste
leur chemin.

Tests : `geometryTests` (9 géométries × 2 qualités × 6 zones, RVB et
couverture) et `renderChainTests` (13 recettes × 5 zones, refus des recettes
non couvertes, des zones hors canevas et des mesures absentes), égalité octet
par octet. Sur RAW réels, sonde `detailRenderProbe --regions`
([preuve](../validation/raw/adaptive-preview-region-render-v001-20261112T203440Z.json)) :
toutes les zones identiques à l'octet près.

| Recette (R6 III 32 Mpx) | Image entière | Mesures globales | Écran 2560×1440 | Tuile 512 |
| --- | --- | --- | --- | --- |
| Tons et couleur | 2,2 s | 25 ms | 0,32–0,34 s | 35 ms |
| + courbe | 2,8 s | 26 ms | 0,35–0,37 s | 35 ms |
| + rotation 2,5° | 3,0 s | 1,0 s | 0,36–0,37 s | 35 ms |
| + perspective | 3,5 s | 0,95 s | 0,37 s | 36–41 ms |

Sur le 60D (18 Mpx), une tuile coûte 28 à 38 ms et l'image entière 1,2 à
1,8 s. La mesure du plancher rend une fois le signal caméra de toute
l'image ; elle ne dépend que des réglages en amont de la géométrie
(signal caméra, noir et blanc) et se met en cache sur eux.

### Tuiles dans l'éditeur et la vue — 12 novembre 2026

`DetailTiles` (fonctions libres, sans état) donne la grille de 512 pixels sur
l'image affichée (le recadrage en pleine résolution), l'ordre des rendus
(spirale depuis le point de départ, puis couronne de 512 pixels triée le long
du déplacement : droit devant, puis sur le côté, derrière en dernier) et les
tuiles à garder. `ImageEditor` orchestre : une tuile à la fois, chacune
courte (30 à 50 ms), de sorte qu'un déplacement ou un réglage n'attend jamais
plus d'une tuile ; une tuile revenue alors qu'elle n'est plus voulue est jetée
plutôt qu'interrompue. Les mesures de l'image entière sont prises par la
première tuile et gardées sous la clé `RenderChain::wholeMeasuresKey`
(réglages en amont de la géométrie et géométrie). La vue QML garde les
tuiles chargées d'un réglage à l'autre de la liste (adresse stable par
réglages) et annonce sa partie visible.

Mémoire : une tuile occupe 1 Mio (8 bits) ; une vue 2560 × 1440 et sa couronne
en gardent une quarantaine, environ 40 Mio, contre 96 à 128 Mio pour l'image
détaillée entière.

### Netteté, flou et grain par zone — 13 novembre 2026

Ces trois étapes lisent quelques pixels autour de chacun : la zone rend les
étapes pixel par pixel sur elle-même agrandie de leur portée cumulée (flou du
curseur, flou et minimum/maximum de la netteté, flou du grain), les applique,
puis garde la zone. Le grain, tiré par position, reçoit l'origine de la zone.

La netteté mesure le bruit de **toute** son entrée (un échantillon par pas de
`√(pixels/16384)` sur des lignes régulières, lu à 3 lignes près). Selon la
décision de l'utilisateur (export inchangé), cette mesure reste celle de l'image entière,
mais elle est prise **par bandes** : seules les lignes échantillonnées et leurs
voisines sont rendues, avec la même fonction d'échantillonnage que le rendu
entier (`DetailProcessing::appendSharpenNoiseSamples`), d'où la même valeur.
Le signal caméra de toute la source est rendu une seule fois pour toutes les
bandes (et pour le plancher de la géométrie). La mesure est gardée tant que les
réglages en amont de la netteté ne changent pas (`wholeMeasuresKey`) : changer
le gain, le rayon ou le masque de netteté ne la refait pas.

Égalité stricte : `renderChainTests` (netteté, flou, grain, combinaison avec
rotation ; mesure par bandes sur une image de 1100 × 960) et sonde sur RAW
réels ([preuve](../validation/raw/adaptive-preview-region-render-v002-20261113T161558Z.json)),
toutes zones identiques à l'octet près.

| Recette (R6 III 32 Mpx) | Image entière | Mesures globales | Tuile 512 |
| --- | --- | --- | --- |
| Tons et couleur + netteté 40 | 2,4 s | 1,1 s | 33–34 ms |
| + rotation 2,5° | 3,3 s | 1,4 s | 37–39 ms |
| Tons et couleur + grain 25 + flou 2 | 2,5 s | 21 ms | 35–38 ms |

Restent rendus en image entière à cette date : bruit (luminance et couleur),
texture et clarté (traitées ci-dessous), voile, masques, retouches.

### Texture et clarté par zone — 13 novembre 2026

Les deux effets découpent la luminance en bandes de σ = 1 à 256 pixels ; à
partir de σ = 4, chaque bande passe par un plan réduit (réduction de zone,
flou, agrandissement bilinéaire). Ces bandes ne dépendent pas des curseurs.

- **Couches réduites partagées** : la mesure globale rend une fois le canevas
  entier jusqu'aux effets et enregistre, pour chaque bande réduite, son plan
  réduit flouté (`LocalContrast::Layers`, environ un tiers des pixels du
  canevas par effet). Chaque zone calcule elle-même les bandes fines (σ = 1 et 2,
  portée 8 pixels par effet) et agrandit sur sa zone les plans réduits
  (`ipr::resizeRegionInto`, même liste de prélèvements que le redimensionnement
  entier, mêmes sommes dans le même ordre).
- **Clé** : réglages en amont des effets, effets actifs et, sous une clarté, la
  texture (la clarté lit la sortie de la texture). Bouger la clarté, ou la
  texture seule, ne refait pas la mesure.
- La netteté prend sa mesure de bruit sur ce même rendu entier.

Égalité stricte : `imageResizeTests` (réductions de zone aux rapports
irréguliers et agrandissement, parties aux bords), `renderChainTests` (texture,
clarté négative et positive, combinaisons avec netteté, grain, flou et rotation,
image de 1100 × 960, mesure prise à une clarté servant à une autre) et sonde
sur RAW réels ([preuve](../validation/raw/adaptive-preview-region-render-v003-20261113T164357Z.json)).

| Recette (R6 III 32 Mpx) | Image entière | Mesures globales | Tuile 512 |
| --- | --- | --- | --- |
| Tons et couleur + texture 20, clarté 30 | 3,5 s | 3,4 s | 49–56 ms |
| + netteté 40 (clarté 30) | 3,3 s | 3,0 s | 46–50 ms |

La première vue après un changement en amont coûte donc autant que l'image
entière ; ensuite, déplacements et curseurs de texture et de clarté ne coûtent
que des tuiles. Restent rendus en image entière : bruit, voile, masques,
retouches.

## Étapes

1. ✅ Banc de mesure (ci-dessus). Reste : profilage de « tons et couleur »
   (6 s par pixel seul en pleine résolution) ; outil de comparaison zone /
   recadrage du plein rendu.
2. Contexte global extrait des trois mesures, sans changer les pixels (test
   d'égalité stricte avec la version actuelle).
3. ✅ Rendu de région pour les étapes « direct » et la géométrie, repli pour les
   autres ; invariant testé (ci-dessus).
4. Voile, débruitage par zone ; puis masques et retouches. Netteté, flou,
   grain, texture et clarté ✅ (13 novembre 2026).
5. ✅ Vue et ordonnanceur : tuiles en spirale, couronne de 512 px, libération et
   abandon du côté opposé au déplacement, masquage au changement de réglage
   (pour les recettes rendues par zone ; les autres gardent l'image entière).
6. Proxy adapté aux ressources.

## Décisions de l'utilisateur

- 12 novembre 2026 : égalité stricte entre une zone et le recadrage du rendu
  entier ; affinage progressif par tuiles de 512 px en spirale depuis le
  centre ; couronne de préchargement de 512 px ; libération et arrêt des tuiles
  du côté opposé au déplacement. La clarté et le voile partagent leurs couches
  réduites entre les tuiles au lieu d'un repli sur le rendu entier.
