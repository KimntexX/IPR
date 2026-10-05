# Décision de dépendances — OpenCV et LibRaw

## Conclusion

Conserver **OpenCV** et **LibRaw** est la décision recommandée. Leur retrait ne
réduit pas le temps d'ouverture constaté et déplacerait un volume important de
code éprouvé dans IPR. Le risque fonctionnel est particulièrement élevé pour
les CR2/CR3 ; aucun remplacement direct ne couvre le décodage, le
dématriçage, les métadonnées et le rendu actuels avec un coût inférieur.

Les mesures de cette session montrent que le coût restant se trouve surtout
dans le développement LibRaw, et non dans le chargement de DLL ou la couche
OpenCV. Retirer une bibliothèque avant de remplacer ce travail ne procure donc
pas de gain de performance mesurable.

## État de l'implémentation

| Dépendance | Rôle actuel dans IPR | Si elle disparaît |
|---|---|---|
| OpenCV | images `cv::Mat`/`CV_32FC4`, resize du proxy, remappage LCP, filtres, histogramme et calculs du pipeline | il faut réécrire ou remplacer une grande partie du pipeline pixel et ses tests |
| LibRaw | lecture CR2/CR3/DNG, métadonnées, niveau noir, décompression et développement RAW 16 bits | CR2/CR3/DNG ne sont plus importables tant qu'un moteur RAW complet n'est pas livré |

Qt ne décode pas les RAW et OpenCV ne remplit pas ce rôle. LibRaw expose une
API de chargement, de décompression et de post-traitement ; son `open_file()`
lit aussi les métadonnées. [API C++ LibRaw](https://www.libraw.org/docs/API-CXX.html)

## Option 1 — conserver les deux dépendances (recommandée)

Coût : faible. Risque : faible, limité aux mises à jour habituelles et à la
validation du bundle Windows. Le code existant garde ses résultats et ses
tests. OpenCV 4.5+ est sous licence Apache-2.0 ; la conformité consiste à
inclure l'avis de licence, pas à ouvrir le code d'IPR. [Licence OpenCV](https://opencv.org/license/)

LibRaw est distribué sous LGPL 2.1 ou CDDL 1.0 selon le choix du projet ; le
mode de liaison et la distribution des avis doivent être revus avant une
distribution commerciale. Sa documentation indique aussi que plusieurs
processeurs LibRaw peuvent travailler en parallèle, mais que chacun consomme
de la mémoire. [Licence et présentation LibRaw](https://www.libraw.org/docs)

## Option 2 — retirer OpenCV seulement

Avantage possible : réduire le nombre de DLL et la taille du déploiement. Ce
gain ne réduit pas le temps du décodage RAW, et le remplacement n'est pas
gratuit : IPR devrait fournir des images flottantes, resize de qualité,
remappage bilinéaire, LUT/courbes, convolutions et traitements par pixel avec
une précision et des performances comparables.

Estimation d'ingénierie : **6 à 12 semaines** pour un remplacement ciblé,
puis validation visuelle et de performance ; risque **élevé** de régressions
sur les bords LCP, les formats/canaux et les exports. C'est une estimation
interne, non une promesse ni une donnée d'un éditeur. Une bibliothèque de
remplacement déplacerait surtout la dépendance au lieu de la supprimer.

## Option 3 — retirer LibRaw

Cette option supprime immédiatement la prise en charge réelle de CR2/CR3/DNG.
Le remplacement candidat le plus proche, RawSpeed, se décrit lui-même comme
un premier étage : il ne lit presque aucune métadonnée hors balance des blancs,
ne fait ni correction couleur/WB, ni dématriçage, ni vignette affichable. Il
faudrait donc écrire et valider tout ce qui suit la mosaïque RAW, en plus de
l'intégration des données de boîtiers. [RawSpeed : périmètre et limites](https://github.com/darktable-org/rawspeed)

Estimation d'ingénierie : **12 à 24 semaines minimum** pour retrouver un
chemin CR2/CR3 comparable, avec risque **très élevé** sur la compatibilité des
boîtiers, la colorimétrie, le bruit, les artefacts de dématriçage et les mises
à jour de formats. RawSpeed impose en outre de livrer ses définitions de
caméras avec la version exacte du code, ce qui augmente le coût de maintenance.

Le SDK DNG Adobe n'est pas un substitut à LibRaw pour les RAW propriétaires :
il traite le format DNG et les flux convertis vers DNG. [Présentation DNG
Adobe](https://helpx.adobe.com/camera-raw/desktop/dng-and-file-formats/digital-negative.html)

## Option 4 — `cv::UMat` / OpenCL (point D)

`cv::UMat` conserve OpenCV : ce n'est ni un retrait de dépendance ni une
solution au développement interne LibRaw. Dans la mesure actuelle, la partie
DCP/LCP OpenCV a été ramenée à environ 0,8 s grâce au CPU multi-cœurs, tandis
que LibRaw reste autour de 2,8 s. Une migration immédiate risquerait les copies
hôte/GPU et des résultats variables selon les pilotes, pour une zone devenue
secondaire.

Décision : ne pas attaquer D à l'aveugle. Faire d'abord un benchmark sur le PC
Windows cible, rendu et export inclus. Si le GPU accélère un proxy complet de
façon répétable, limiter l'expérimentation à une branche et conserver le
chemin `cv::Mat` comme référence de non-régression.

## Plan de performance à plus fort rendement

1. Vérifier sur une installation Windows normale le cache persistant de
   sélection DCP/LCP, qui évite le scan Adobe au lancement suivant.
2. Si l'utilisateur doit voir quelque chose en moins d'une seconde, afficher
   d'abord la vignette embarquée LibRaw, puis remplacer par le rendu complet.
   LibRaw fournit `unpack_thumb()` et `dcraw_make_mem_thumb()` ; la qualité
   et les réglages ne seraient que provisoires. [Vignettes LibRaw](https://www.libraw.org/docs/API-CXX.html)
3. Ajouter la progression et l'annulation LibRaw : son callback est appelé
   pendant le post-traitement et peut interrompre le travail. Cela ne réduit
   pas le temps CPU, mais rend l'attente contrôlable. [Callback LibRaw](https://www.libraw.org/docs/API-CXX.html)
4. Ne réévaluer OpenCL qu'après ces gains, avec des mesures sur le matériel
   réellement déployé.
