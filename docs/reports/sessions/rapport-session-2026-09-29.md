# Rapport de session — 29 septembre 2026

**Périmètre :** développement RAW Canon, rendu couleur et tonal, export des
images et comparaison mesurée avec Lightroom sur le corpus local.

## Synthèse

La session a consolidé le chemin complet de traitement des fichiers RAW :
ouverture CR2/CR3/DNG avec LibRaw, estimation de niveau noir Canon lorsque les
métadonnées sont insuffisantes, application de profils DCP/LCP, espace de
travail sRGB linéaire, réglages non destructifs et exports JPEG/PNG/TIFF/DNG.

Le point principal a été de rapprocher le développement du CR3
`6V1A9933.CR3` de sa référence Lightroom sans écraser les couleurs ni créer de
coupures entre zones tonales. Les contrôles noirs, ombres, hautes lumières et
blancs sont maintenant pondérés continûment par la luminance, composés par
zone, puis appliqués une seule fois par mise à l'échelle RGB. Cette méthode
préserve mieux la chromaticité que l'ancienne succession de corrections par
canal.

Une sonde de régression locale rend désormais le CR3 de référence avec les
réglages Lightroom et compare le résultat au JPEG Lightroom. Elle confirme un
rapprochement mesuré, tout en documentant les limites d'une reproduction exacte
du moteur Adobe.

## Travaux réalisés

### 1. Import RAW, métadonnées et profils

- Les extensions CR2, CR3 et DNG sont décodées par LibRaw lorsqu'il est
  disponible.
- Les CR3 Canon pour lesquels LibRaw ne fournit pas de niveau noir exploitable
  utilisent une estimation par photosite CFA dans les marges masquées du
  capteur. Le repli n'est pas utilisé lorsqu'un niveau noir fiable est déjà
  présent dans les métadonnées.
- Le moteur DCP sélectionne les profils caméra et réalise la transformation vers
  l'espace de travail sRGB linéaire.
- La sélection LCP est basée sur un score objectif/boîtier plutôt que sur une
  première correspondance générique. Pour `6V1A9933.CR3`, la sonde confirme la
  sélection de :
  - DCP : `Canon EOS R6 Mark III Adobe Standard` ;
  - LCP : `Canon (Canon RF 24-105mm F2.8 L IS USM Z) - RAW`.
- La provenance colorimétrique et les informations de prise de vue sont
  conservées dans `ColorPipeline::WorkingImage`.

### 2. Balance des blancs et exposition

- `WorkingImage` transporte une température de référence facultative issue de
  la source.
- Pour les RAW, cette référence est estimée depuis les multiplicateurs/matrices
  LibRaw ; pour les formats standards, une recherche best-effort de métadonnées
  est réalisée.
- La température initiale et la réinitialisation utilisent cette valeur source,
  avec 6500 K comme repli explicite.
- La plage d'exposition correspond maintenant réellement à `[-5, +5] EV` :
  l'exposition est appliquée avec `2^EV`, et non plus comme une variation
  divisée par 100.
- L'interface expose la température de 2000 à 50000 K et la nuance de -150
  (vert) à +150 (magenta).

### 3. Réglages tonals

Le rendu dans [`imageeditor.cpp`](../../../src/editor/imageeditor.cpp) sépare les fonctions :

- **exposition** : facteur global en lumière linéaire ;
- **contraste** : courbe de luminance ancrée sur le gris linéaire 18 % ;
- **noirs et ombres** : masques de basse luminance continus ;
- **hautes lumières et blancs** : masques de haute luminance et compression
  d'épaule.

Les quatre réglages tonals ne sont plus appliqués par multiplications
successives sur les canaux RGB. Les couples noirs/ombres et hautes
lumières/blancs sont d'abord fusionnés par une composition bornée. La luminance
cible est ensuite calculée une fois et reportée sur RGB par ratio.

Conséquences :

- pas de frontière nette entre les zones ;
- réglages à `-100` non destructifs : un signal non nul est conservé ;
- noirs et ombres à `-100` ne s'assombrissent plus mutuellement deux fois ;
- meilleure conservation de la chromaticité dans les zones compressées.

La transition d'ombres est progressivement réduite jusqu'à 0,56 de luminance
linéaire afin de ne pas assombrir les tons moyens et les hautes lumières.

### 4. Export

- Les exports JPEG, PNG et TIFF passent par une écriture atomique avec
  `QSaveFile` et remontent les erreurs d'écriture à l'interface.
- L'extension est normalisée en fonction du format choisi :
  JPEG (`.jpg`/`.jpeg`), PNG (`.png`), TIFF (`.tif`/`.tiff`) et DNG (`.dng`).
- L'export DNG est un **DNG RGB linéaire 16 bits rendu** :
  - réglages IPR intégrés ;
  - balises DNG, matrice sRGB vers XYZ D50, `AsShotNeutral` et profil ICC sRGB ;
  - pas de mosaïque CFA source, ni de latitude RAW, ni de métadonnées de prise
    de vue recopiées.
- Le DNG produit est relu avec LibRaw dans le test d'intégration.
- Les dialogues QML proposent le nom `nom-source-IPR.<format>`.
- Le backend accepte les URL `file:` et les chemins absolus Windows tels que
  `C:/Photos/image.jpg`, même si Qt les interprète avec un schéma à une lettre.

Le message *« La destination d'export doit être un fichier local »* vu dans
l'application Windows reste à valider sur le binaire effectivement exécuté.
Les sources et les tests LibRaw comportent le correctif, mais aucun exécutable
Windows `appIPR.exe` reconstruit à partir de ces sources n'était présent dans le
répertoire de travail. Une reconstruction et un test manuel du binaire Windows
cible sont nécessaires.

### 5. Comparaison avec `6V1A9933`

Le corpus local contenait les fichiers privés suivants lors de cette session
(non versionnés ; leur disponibilité actuelle n'est pas garantie) :

- `tests/image/6V1A9933.CR3`, RAW source ;
- `tests/image/6V1A9933.jpg`, rendu Lightroom de
  référence ;
- `tests/image/6V1A9933-2.dng`, DNG RAW avec les
  réglages XMP Lightroom ;
- `tests/image/6V1A9933.xmp`, sidecar de référence.

Les métadonnées Lightroom indiquent :

```text
ProcessVersion = 15.4
WhiteBalance = As Shot
Exposure2012 = -0.48
Contrast2012 = +41
Highlights2012 = -100
Shadows2012 = -100
Whites2012 = -100
Blacks2012 = -100
CameraProfile = Adobe Standard
ToneCurveName2012 = Linear
```

La sonde
[`tonal_reference_comparison_probe.cpp`](../../../tests/tonal_reference_comparison_probe.cpp)
applique à IPR `-0,49 EV`, `+41` de contraste et `-100` aux quatre réglages
tonals. Elle rend deux variantes : balance automatique et balance boîtier
(*As Shot*), puis compare des échantillons RGB/luminance au JPEG Lightroom.

Résultat final :

| Mesure | IPR, balance boîtier | JPEG Lightroom |
|---|---:|---:|
| Écart RGB absolu moyen | 21,18 / 255 | référence |
| Écart de luminance moyen | 20,94 / 255 | référence |
| Luminance p05 | 6 | 0 |
| Luminance p50 | 28 | 21 |
| Luminance p95 | 183 | 178 |

La variante *As Shot* est légèrement meilleure que la balance automatique
(21,18 contre 21,36 d'écart RGB absolu moyen). Le réglage antérieur mesuré dans
la session produisait 23,19 : l'itération tonale réduit ainsi l'écart d'environ
8,7 %.

Le DNG Lightroom porte les mêmes réglages XMP mais reste un DNG mosaïque ;
LibRaw ne développe pas ces réglages Adobe intégrés. Il confirme donc les
paramètres de référence, tandis que le JPEG est la référence pixellisée utilisée
pour la comparaison.

## Validation

La validation a été exécutée exclusivement avec le build LibRaw demandé :

```sh
cmake -S . -B build-wsl-libraw -DIPR_ENABLE_LIBRAW=ON -DBUILD_TESTING=ON
cmake --build build-wsl-libraw --target appIPR imageExportTests \
    tonalReferenceComparisonProbe -j2
ctest --test-dir build-wsl-libraw --output-on-failure
```

Résultat : **6/6 tests réussis**.

Les cinq tests généraux couvrent le pipeline couleur, les exports
JPEG/PNG/TIFF/DNG, le décodage RAW, le repli de niveau noir et les profils
DCP/LCP. Le sixième test, `tonalReferenceComparisonProbe`, rend deux fois le
CR3 de 32 Mpx et a pris environ 114 secondes. Il est conditionnel à la présence
du corpus local : il ne s'ajoute pas dans un clone qui ne contient pas les
photos privées.

Le test échoue si la variante *As Shot* dépasse un écart RGB absolu moyen de 25
niveaux avec le JPEG Lightroom. Les sorties de contrôle restent dans le
répertoire de build.

## Limites connues

- IPR n'émule pas intégralement Adobe Camera Raw / Lightroom Process Version
  15.4. Les curseurs homonymes ne sont pas contractuellement identiques.
- Le rendu Lightroom référencé inclut notamment son moteur DCP, ses choix de
  démosaïçage, ses corrections d'optique, la réduction de bruit couleur et
  l'accentuation ; IPR n'implémente pas encore toute cette chaîne.
- La sonde redimensionne la référence après orientation pour comparer les deux
  rendus. Les géométries diffèrent légèrement entre le CR3 et le DNG après les
  traitements d'optique.
- Le DNG exporté par IPR est volontairement un rendu linéaire, non un
  transcodage RAW rééditable.
- Le build et les tests ont été réalisés sous WSL avec LibRaw. Le comportement
  du dialogue de sauvegarde Windows reste à vérifier avec le binaire Windows
  reconstruît.
- Le corpus de niveau noir documenté couvre actuellement un seul boîtier Canon.
  Les CR3 d'un autre boîtier, attendus de l'utilisateur, doivent être analysés
  avant d'étendre le repli de niveau noir.

## Priorités proposées pour la suite

1. **Valider le binaire Windows réel.** Recompiler l'exécutable avec LibRaw,
   ouvrir un CR3, vérifier le nom d'export proposé et confirmer les exports
   JPEG/PNG/TIFF/DNG depuis le dialogue natif Windows.
2. **Importer les réglages XMP.** Lire explicitement les réglages Lightroom
   compatibles (au minimum exposition, contraste, quatre tons, balance des
   blancs et profil), les afficher comme provenance et demander confirmation
   avant de les appliquer au développement IPR.
3. **Ajouter une courbe tonale et un histogramme.** Concevoir une courbe
   éditable dans l'espace de luminance, définir son ordre avec les réglages
   actuels et partager strictement son rendu entre aperçu/export.
4. **Élargir la colorimétrie.** Comparer des mires et des images de référence,
   étudier un espace de travail à gamut large, le gamut mapping et le profil
   écran, sans rompre le contrat sRGB actuel.
5. **Réduction de bruit et netteté.** Évaluer des traitements RAW/RGB sur
   plusieurs ISO, avec mesures de détail, de chrominance et de bruit résiduel.
6. **Préchargement et cache.** Produire des vignettes indépendantes, précharger
   les images voisines de façon annulable et borner un cache LRU.
7. **GPU après profilage.** Mesurer d'abord les coûts CPU et les transferts
   mémoire ; comparer Qt Quick/RHI, OpenCL ou un backend approprié, avec
   précision vérifiée et repli CPU.
8. **Corpus multi-boîtiers.** À réception des CR3 d'un autre boîtier, créer le
   manifeste sans copier les originaux, vérifier les marges CFA et confirmer que
   le repli de niveau noir reste strictement ciblé.
