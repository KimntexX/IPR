# B9 — préparation de la fusion HDR

État : préparé le 5 novembre 2026 ; **H1, H2, H3 et H5 codés sur `patch_hdr` le 16 novembre 2026,
qualifiés sur synthèse seulement**, en attente des séries réelles (§8). Précise la
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

### Reste à faire

| Palier | Ce qui manque | Qui |
| --- | --- | --- |
| H0 | Séries réelles du §7 et un DNG HDR de Lightroom avec son export TIFF | Utilisateur |
| H1 | Ouverture du nouveau DNG flottant dans Lightroom, Photoshop et DxO | Utilisateur |
| H2–H4 | Mesures du §6 sur les séries réelles : linéarité, bruit sur aplats, dynamique, fantômes sur sujet mobile | Claude, avec les séries |
| H5 | Décider d'une compensation initiale à l'ouverture (premier rendu écrêté au blanc) ; validation visuelle | Utilisateur |
| H6 | Panorama HDR | Après qualification de B8 et B9 |
