# B9 — préparation de la fusion HDR

État : **préparation, aucun code**, 5 novembre 2026. Précise la
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
