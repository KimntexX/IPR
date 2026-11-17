# B9 — préparation de la fusion HDR

État : préparé le 5 novembre 2026 ; **H1, H2, H3 et H5 codés sur `patch_hdr` le 16 novembre 2026,
qualifiés sur synthèse** (§8), **puis mesurés le 17 novembre sur sept séries réelles du 60D** (§9) ;
il manque une scène à fort contraste en lumière qui ne scintille pas. Précise la
[section B9 de la feuille de route](roadmap-beta.md#12-b7b8b9--empilement-panorama-et-hdr) et le
[plan multi-images](empilement-et-panorama.md). Branche proposée : `patch_hdr`, moteur d'abord, panneau
sur une branche enfant. Le panorama HDR vient après, une fois chacun qualifié.

## 1. Deux opérations à ne pas confondre

| | Fusion visuelle d'expositions | HDR radiométrique |
| --- | --- | --- |
| Entrée | Images développées | Signal caméra linéaire |
| Sortie | Une image déjà rendue | Un signal à grande dynamique, à retoucher ensuite |
| Principe | Garder en chaque endroit la vue la mieux exposée | Estimer la luminance de la scène à partir de toutes les vues |
| Avantage | Simple, aucun format nouveau | Reste une vraie source : balance, tons et profil restent libres |

**Je recommande le HDR radiométrique seul**, comme l'empilement : il prolonge ce qui existe, et Lightroom
fait de même d'après ses [guides d'utilisation](https://www.peachpit.com/articles/article.aspx?p=2337459).
Une fusion visuelle n'apporterait rien que les curseurs de tons ne donnent déjà à partir de ce résultat.

## 2. Ce que B7 et B8 fournissent déjà

| Élément | État | À adapter pour le HDR |
| --- | --- | --- |
| Décodage en signal caméra, un fichier par image dans un tampon | Prêt (`FrameSpool`) | Rien |
| Recalage (`StackAlignment`) | Prêt, sur racine de luminance | **Égaliser l'exposition avant de comparer**, et ignorer les zones écrêtées ou bouchées de chaque vue |
| Champ local pour la parallaxe | Prêt | À qualifier entre expositions |
| Rejet de ce qui bouge (`StackMerge`) | Prêt à exposition constante | **À refaire** : le bruit diffère d'une vue à l'autre, et une zone écrêtée n'est pas un mouvement |
| Session, onglet Assembler, export | Prêts | Un troisième mode |
| Écart de gain d'empilement | Refuse au-delà de 25 % | Sans objet : ici, l'écart est le sujet |

## 3. Le cœur du calcul

**Écart d'exposition.** Le temps, l'ISO et l'ouverture donnent l'écart nominal ; on le **mesure** ensuite sur
les zones bien exposées dans les deux vues, comme les gains de l'empilement, car les valeurs réelles s'écartent
des valeurs nominales. Un écart mesuré très différent du nominal se signale : on a peut-être affaire à un
changement de lumière.

**Pondération.** En chaque pixel, une moyenne pondérée des vues ramenées à la même exposition. Le poids suit la
précision réelle : inverse de la variance de bruit attendue dans cette vue à ce niveau, nul au-delà du seuil
d'écrêtage et, plus doucement, dans le bruit de noir. La vue longue domine dans les ombres, la courte dans les
hautes lumières. Tout se calcule **par canal, avant dématriçage si possible, sinon en signal caméra** : à trancher
en H1, car un écrêtage sur un seul canal se traite mal après dématriçage.

**Mouvements.** Une vue de référence est choisie, et une autre ne contribue en un pixel que si elle y est
compatible avec la référence, compte tenu de l'écart d'exposition et du bruit attendu. Cela évite les fantômes,
au prix de garder le bruit de la référence dans les zones qui bougent. C'est un compromis à montrer, pas à cacher.

**Ce qui reste sans information.** Une zone écrêtée dans toutes les vues reste écrêtée : le résultat le signale,
et ne l'invente pas. Même règle que le remplissage : ce qui est inventé doit se savoir.

## 4. Le vrai verrou : le format de sortie

`CameraDng` écrit du LinearRaw **16 bits entiers**, avec une marge limitée à 8 fois le blanc, soit 3 stops
([`CameraDng.cpp`](../../src/raw/CameraDng.cpp)). Un bracketing de ±2 stops la dépasse, et 16 bits entiers
manquent de précision dans les ombres d'un résultat étendu.

Il faut donc un **DNG à virgule flottante**, écrit et relu par IPR. Plusieurs guides décrivent le DNG HDR de
Lightroom comme 16 bits flottants, d'autres comme 32 bits : **à établir directement sur un fichier produit par
Lightroom**, et dans la spécification DNG. Sa lecture par d'autres logiciels n'est pas acquise : des utilisateurs
signalent des [images roses dans Capture One](https://support.captureone.com/hc/de/community/posts/360012417318-Unable-to-open-a-DNG-file-from-Lightroom-HDR-merge)
et un [refus de PTGui](https://groups.google.com/g/ptgui/c/jJdAN3jgEo8). C'est un point à vérifier, et il rejoint
l'[issue #9](https://github.com/KimntexX/IPR/issues/9).

**Le pipeline après la fusion.** Le rendu travaille en flottant, mais il faut qualifier ce qui suppose un signal
proche de 1 : statistiques de `ToneRecovery`, courbes de profil au-delà du blanc, histogramme. Une image ouverte
sans retouche sera sombre avec de grandes réserves : décider si IPR propose une compensation initiale.

## 5. Paliers

| Palier | Contenu | Critère de passage |
| --- | --- | --- |
| H0 | Corpus et plans gelés ; analyse d'un DNG HDR de Lightroom | Format établi, corpus suffisant |
| H1 | DNG flottant écrit et relu ; choix du domaine | Aller-retour sans perte mesurée |
| H2 | Fusion sur série au trépied, sans mouvement | Linéarité et bruit mesurés, voir §6 |
| H3 | Recalage à main levée entre expositions | Déplacements connus retrouvés sur synthèse et photos |
| H4 | Mouvements et fantômes | Sujets mobiles sans dédoublement |
| H5 | Mode dans l'onglet Assembler | Parcours complet, validation de l'utilisateur |
| H6 | Panorama HDR : fusionner chaque colonne, puis assembler | Après B8 et B9 qualifiés séparément |

## 6. Validation

Les mesures sont possibles parce que le signal attendu est physique :

- **Linéarité :** sur une zone bien exposée dans plusieurs vues, le résultat doit égaler chaque vue ramenée à la
  même exposition, à la tolérance de bruit près.
- **Gain de bruit :** dans les ombres, le bruit du résultat doit suivre celui de la vue longue, mesuré sur aplats.
- **Dynamique :** comparer le nombre de stops utiles avant et après, sur une charte ou une scène à fort contraste.
- **Synthèse :** des vues simulées à exposition, déplacement et bruit connus, pour des tests automatiques.
- **Repère Lightroom :** son export HDR comme comparaison en boîte noire, jamais comme cible.
- **Ressources :** temps, mémoire, disque plein, annulation, tampon toujours vidé.

## 7. Ce qu'il me faut de l'utilisateur

Une recherche sur les EXIF des CR2 de `Pictures\RAW` ne trouve aucun bracketing : au moins trois vues en moins de
trois secondes, même ouverture et même ISO, deux stops d'écart. Les CR3 et les autres dossiers n'ont pas été lus.
Pour H0 et H2 :

1. **Trois séries au trépied**, ±2 stops, 3 à 5 vues, sur le R6 III et sur le 60D : une scène à fort contraste
   sans mouvement, une intérieure avec fenêtre, et un coucher de soleil.
2. **Deux séries à main levée** du même type, pour H3.
3. **Une série avec un sujet mobile**, par exemple des feuillages ou des passants, pour H4.
4. **Un DNG HDR produit par Lightroom** à partir d'une de ces séries, avec son export TIFF.
5. Si des bracketings existent déjà sur tes disques, leurs dossiers : cela peut éviter une séance.

## 8. Réalisé le 16 novembre 2026, sur synthèse

Sans série bracketée réelle, les paliers qui n'en dépendent pas ont été codés et testés sur des scènes de
synthèse dont la luminance est connue. Ces mesures valident le calcul, pas le rendu sur des photos.

**H1 — format.** Les DNG de signal caméra (empilement, panorama, HDR) sont désormais en **DNG 1.4, flottants
16 bits** (binaire16 IEEE), blanc à 1 par défaut des données flottantes, compressés par Deflate après le
prédicteur flottant. Précision relative de 2⁻¹¹ à tout niveau, aucune limite de dynamique, valeurs sous le
noir conservées. Les DNG déjà exportés au premier format (entiers 16 bits) restent lus ; seul le format courant
est écrit. Aller-retour mesuré : erreur relative ≤ 2⁻¹¹ de 10⁻⁴ à 1000 fois le blanc. **Reste à vérifier :**
l'ouverture dans Lightroom, Photoshop et DxO ([#9](https://github.com/KimntexX/IPR/issues/9),
[#23](https://github.com/KimntexX/IPR/issues/23)) ; elle n'a pas été testée sur ce nouveau format.

**Domaine.** La fusion travaille sur le signal caméra **après dématriçage**, celui que l'empilement utilise
déjà. L'écrêtage d'un seul canal se traite par le canal le plus haut du pixel et de ses voisins (3 × 3) : un
voisin dématricé d'un site écrêté ne compte pas non plus. Le passage avant dématriçage reste possible si les
séries réelles montrent des franges colorées aux bords des hautes lumières.

**H2 — fusion** (`src/hdr/HdrMerge`). Moyenne pondérée par l'inverse de la variance attendue (bruit de photons
et de lecture), avec un poids qui s'annule près de l'écrêtage ; les poids lisent le niveau de la scène dans la
vue la moins bruitée qui l'a vue, pas la valeur bruitée de chaque vue. Un pixel écrêté partout reste celui de
la vue la plus courte, et est compté. Mesures sur une scène de 14 IL, vues à −2, 0 et +2 IL :

| Mesure | Résultat |
| --- | --- |
| Vues exactes : fusion égale à la scène | erreur ≤ 10⁻⁵ sur 12 IL |
| Bruit relatif dans les ombres | 10,2 % (vue longue 11,4 %, référence 25 %) |
| Hautes lumières vues par la seule vue courte | sa précision exacte, sans biais |
| Sujet passé devant la vue longue | aucun fantôme (niveau 0,993 de la scène) |

**H3 — recalage et expositions** (`src/hdr/HdrJob`). Chaque vue est recalée sur la référence après avoir été
ramenée à son exposition et à la plage que les deux voient (au-dessus du bruit de la plus sombre, sous le genou
de la plus claire) ; le rééchantillonnage est celui de l'empilement, désormais partagé. Les expositions sont
**mesurées** sur les vues recalées, par canal, sur des échantillons de 2 × 2 pixels tous les 8 pixels
qu'aucun écrêtage voisin ne touche, choisis sur la vue la plus claire seule : des blocs entiers tiraient le
rapport vers 1 (2,87 pour 4), et un choix sur la vue sombre bruitée le biaisait de 2 % à −6 IL. Une vue trop
éloignée de la référence est mesurée par une vue intermédiaire. Un écart de plus d'un tiers d'IL avec les
EXIF est signalé (lumière changeante). Sur une série à −6, −3, 0 et +2 IL décalée de quelques pixels :
décalages retrouvés à 0,03 pixel, expositions à 0,7 %, vue à −6 IL mesurée par celle à −3 IL ; bruit des
ombres 7,6 % contre 17,9 % pour la référence seule ; hautes lumières que la référence écrêtait retrouvées à
3,9 %.

**H5 — onglet Assembler.** Troisième mode « Fusion HDR » : référence au choix (recalage et zones mobiles),
choix entre supprimer les fantômes et garder toutes les vues (trépied, scène immobile). L'exposition nominale
vient du temps, de l'ISO et de l'ouverture, désormais lue dans les métadonnées ; le bruit, de la loi mesurée
du boîtier à cet ISO, sinon d'un capteur plein format générique. **Le résultat garde les niveaux de
l'exposition médiane**, quelle que soit la référence, ses hautes lumières au-delà du blanc. Le message donne
l'étendue en IL, la part écrêtée partout et les vues écartées avec leur raison.

**Le pipeline après la fusion.** Un DNG HDR se rouvre dans IPR avec un rendu fini. La courbe de tons du profil
s'arrête au blanc : **le premier rendu écrête au blanc**. L'exposition agit sur le signal caméra avant le
profil, si bien que −6 IL font réapparaître des hautes lumières jusqu'à 64 fois le blanc (test). Le curseur
« Hautes lumières », appliqué après le profil, ne les voit pas : c'est l'exposition qui les ramène.

## 9. Premières séries réelles, le 17 novembre 2026

**Corpus.** Une lecture des EXIF des RAW des cartes `D:` et `E:` et de `Pictures\RAW` (1789 fichiers, sans
écriture) trouve sept séries du 60D prises le 13 novembre 2026 entre 17 h 44 et 17 h 48 dans
`E:\DCIM\101EOSR6` : une nature morte d'intérieur (peluche noire et blanche, sweat orange, mur blanc,
parquet), 70 mm, f/5,6, le temps divisé par deux à chaque pas, deux vues ou plus par pas. Les CR3 du R6 III
n'en contiennent aucune. La scène est peu contrastée : aucune zone n'est écrêtée dans toutes les vues, la
série la plus longue couvre 5 IL. Le trépied a bougé entre les vues, jusqu'à 24 pixels.

| Série | Vues | ISO | Temps | Étendue |
| --- | --- | --- | --- | --- |
| `60d-iso100-a` | `IMG_9365`–`9370` | 100 | 1/40 à 1/160 s | 2 IL |
| `60d-iso400-a` | `IMG_9371`–`9381` | 400 | 1/20 à 1/320 s | 4 IL |
| `60d-iso800` | `IMG_9382`–`9395` | 800 | 1/40 à 1/320 s, aller et retour | 3 IL |
| `60d-iso400-b` | `IMG_9396`–`9405` | 400 | 1/20 à 1/320 s | 4 IL |
| `60d-iso100-b` | `IMG_9406`–`9410` | 100 | 1/10 à 1/20 s | 1 IL |
| `60d-iso100-c` | `IMG_9411`–`9417` | 100 | 1/40 à 1/160 s | 2 IL |
| `60d-iso1600` | `IMG_9429`–`9460` | 1600 | 1/100 à 1/3200 s | 5 IL |

**Le DNG HDR de Lightroom.** Fusion de `IMG_9371`, `9375` et `9379` par l'utilisateur (Lightroom Classic,
Camera Raw 18.7). Lecture des étiquettes, sans décompilation : **DNG 1.7, LinearRaw, flottants 16 bits,
tuiles de 400 × 432, compression JPEG XL (52546) avec perte** (distance 0,5), masque de transparence,
`BaselineExposure` +2, blanc déclaré à 32768. Les données sont aux niveaux de la vue la plus sombre ; +2 IL
les ramènent à ceux de la vue médiane, le choix d'IPR. IPR ne lit pas le JPEG XL : le fichier ne s'ouvre pas.
Son XMP porte `AutoTone="True"` (réglages automatiques du dialogue de fusion), `HDREditMode="1"` (affichage
HDR jusqu'à +2,3 IL) et le profil Adobe Standard.

**Comparaison des signaux, hors rendu** (décodage du JPEG XL par `imagecodecs` dans un dossier temporaire,
hors dépôt). Fusions recalées au pixel, rapport Lightroom / IPR par canal :

| Mesure | Résultat |
| --- | --- |
| Niveau médian, à la même échelle | 0,0216 contre 0,0210 (0,04 IL) |
| Rapport par canal (80 % des tons) | R 0,999, V 1,004, B 0,999 |
| Rapport sur le décile le plus clair | 0,947 : Lightroom 5 % plus bas |
| Balance inscrite (neutre caméra) | IPR (0,511 ; 1 ; 0,608), celle de `IMG_9375` ; Lightroom (0,553 ; 1 ; 0,561), celle de `IMG_9379` |

Les deux fusions sont donc le même signal à 0,5 % près sur l'essentiel des tons. **L'écart visible vient du
rendu** : réglages automatiques et affichage HDR de Lightroom, profil, et balance automatique du boîtier qui a
changé d'une vue à l'autre (chaque logiciel reprend celle d'une vue différente). L'écart de 5 % dans les plus
hautes lumières rejoint la cause suivante.

**Scintillement de l'éclairage.** Les rapports entre vues, mesurés directement sur les sites verts du RAW
(noir soustrait, sans passer par IPR), varient selon la rangée et la zone :

| Paire | Temps | Rapport selon la rangée |
| --- | --- | --- |
| `IMG_9375` / `9376` | 1/80 et 1/80 s | plat à ± 0,3 % |
| `IMG_9371` / `9372` | 1/20 et 1/20 s | plat à ± 0,5 % |
| `IMG_9379` / `9380` | 1/320 et 1/320 s | ± 2 % |
| `IMG_9382` / `9386` | 1/40 et 1/160 s | onde de ± 2,5 % |
| `IMG_9371` / `9379` | 1/20 et 1/320 s | de 1,07 en haut à 0,95 |

Une source artificielle scintille (100 Hz), et l'obturateur à fente des poses courtes en saisit des phases
différentes selon la rangée ; à rangée égale, l'écart dépend encore de la zone, signe de plusieurs sources.
**Les vues courtes ne sont pas cohérentes avec les longues à quelques pourcents près.** Ce n'est pas une
non-linéarité d'IPR ; aucune fusion ne peut la corriger sans modèle du scintillement. Pour les séries de
validation : lumière du jour seule, ou poses de 1/50 s et plus longues.

**Audit des sept séries** ([`tests/hdr_audit.cpp`](../../tests/hdr_audit.cpp), outil manuel `iprHdrAudit`).
Chaque série est décodée comme dans l'application puis fusionnée par `HdrJob`, la vue du pas médian en
référence, une fois avec le rejet des fantômes et une fois en gardant toutes les vues. Les 14 preuves sont
`docs/validation/raw/b9-hdr-audit-v001-60d-*-20261117T*.json`. Elles ont été mesurées avec l'outil avant son
commit, d'où `candidate_dirty` ; il est versionné avec elles. Les sources sont restées intactes (empreintes
SHA-256 relues).

- **Linéarité :** moyennes de la fusion contre chaque vue ramenée par les expositions mesurées, sur des blocs
  de 16 pixels que cette vue expose entre 1 % et 80 % de son blanc.
- **Bruit :** deux fusions de vues distinctes (la première, puis la seconde de chaque pas) ne diffèrent que
  par leur bruit, comme la référence et sa jumelle. Leur écart sur les tuiles de 32 pixels les plus planes
  donne le rapport entre le bruit de la fusion et celui d'une vue seule, sans image vraie. On le compare à deux
  cibles :
  - celle des lois de bruit (photons et lecture) ;
  - celle qui prend aussi en compte la part proportionnelle au signal mesurée sur les jumelles : 0,65 à 1,9 %
    selon la série, ce sont les écarts de réponse des photosites, que la moyenne réduit moins bien.

| Série | Expositions mesurées vs EXIF | Linéarité (rapport par niveau) | Bruit ombres : mesuré / lois / mesuré-cible | Bruit tons moyens : idem | Fantômes faux (part des échantillons) |
| --- | --- | --- | --- | --- | --- |
| `60d-iso100-a` | ≤ 0,022 IL | 0,986 à 1,012 | 0,50 / 0,53 / 0,49 | 0,53 / 0,53 / 0,53 | 19 % |
| `60d-iso100-b` | ≤ 0,004 IL | 0,991 à 1,006 | 0,89 / 0,83 / 0,94 | 0,87 / 0,82 / 0,89 | 4,4 % |
| `60d-iso100-c` | ≤ 0,017 IL | 0,995 à 1,005 | 0,51 / 0,53 / 0,49 | 0,58 / 0,53 / 0,58 | 16 % |
| `60d-iso400-a` | ≤ 0,027 IL | 0,975 à 1,050 | 0,29 / 0,31 / 0,28 | 0,42 / 0,35 / 0,37 | 2,6 % |
| `60d-iso400-b` | ≤ 0,028 IL | 0,987 à 1,013 | 0,29 / 0,31 / 0,28 | 0,40 / 0,35 / 0,37 | 1,9 % |
| `60d-iso800` | ≤ 0,041 IL | 0,977 à 1,015 | 0,49 / 0,49 / 0,48 | 0,54 / 0,51 / 0,52 | 0,9 % |
| `60d-iso1600` | ≤ 0,226 IL (1/3200 s) | 0,961 à 1,037 | 0,31 / 0,29 / 0,30 | 0,38 / 0,34 / 0,35 | 0,8 % |

Les colonnes de bruit sont lues en gardant toutes les vues. Les ombres sont les niveaux inférieurs à 2⁻⁸ du
blanc de la référence, les tons moyens les niveaux de 2⁻⁷ à 2⁻². Il n'y a eu aucune vue écartée.

**Lecture.**

1. **Les expositions mesurées suivent les EXIF à 0,04 IL près** jusqu'à 1/320 s. À 1/3200 s, l'écart atteint
   0,23 IL : c'est l'obturateur, qui est imprécis à ces vitesses. Il reste sous le seuil d'alerte d'un tiers
   d'IL, et il justifie de mesurer les expositions.
2. **La linéarité est à ± 1,5 %** sur les séries aux poses longues ou au scintillement faible. Elle descend à
   ± 5 % sur `iso400-a` et `iso1600`, là où le scintillement est le plus fort. L'écart suit la rangée, pas le
   niveau (voir plus haut).
3. **Le bruit atteint la cible mesurée dans les ombres** : un rapport de 0,29 à 0,31 sur 4 à 5 IL, soit 1,7 IL
   gagnés sur la vue de référence. Dans les tons moyens, il reste à 10–15 % au-dessus de cette cible. La cause
   probable, non démontrée, est l'incohérence entre vues due au scintillement.
   À 2⁻¹¹ du blanc de la référence (+6 IL), la vue seule n'est plus que du bruit coloré, alors que la fusion
   rend la fourrure.
4. **Le rejet des fantômes se trompe sur une scène immobile éclairée qui scintille** : jusqu'à 19 % des
   échantillons à ISO 100, où le bruit faible laisse la seule tolérance de 3 % ; le bruit des ombres monte
   alors de 0,50 à 0,59. À ISO 400 et au-delà, il reste sous 3 %. L'option « Garder toutes les vues » existe
   pour ce cas. Un réglage plus robuste (tolérance qui suit l'écart mesuré entre vues de même exposition) reste
   à décider sur une scène qui bouge vraiment (H4).
5. **Ressources :** 5 à 56 s de fusion après décodage (32 vues de 18 Mpx pour la plus longue), 1,5 Go de
   mémoire au plus, tampon vidé.

### Reste à faire

| Palier | Ce qui manque | Qui |
| --- | --- | --- |
| H0 | Séries à fort contraste du §7, en lumière qui ne scintille pas (jour, ou poses ≥ 1/50 s), R6 III et 60D | Utilisateur |
| H1 | Ouverture du DNG flottant dans Photoshop et DxO (Lightroom : fait, #23) ; lecture du DNG 1.7 JPEG XL de Lightroom par IPR (dépendance libjxl à décider) | Utilisateur |
| H2 | Fait sur le 60D en lumière scintillante (§9) ; à refaire sur une scène à fort contraste, où des zones sont écrêtées | Claude, avec les séries |
| H3–H4 | Main levée, sujet mobile, robustesse du rejet des fantômes au scintillement | Claude, avec les séries |
| H5 | Décider d'une compensation initiale à l'ouverture (premier rendu écrêté au blanc) ; validation visuelle | Utilisateur |
| H6 | Panorama HDR | Après qualification de B8 et B9 |
