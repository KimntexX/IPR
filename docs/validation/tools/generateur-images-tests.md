# Générateur d'images IPR de test

Contrat aligné — 12 octobre 2026, branche `patch_validation` à recompiler.
Source : [generate_test_images.cpp](../../../src/tools/generate_test_images.cpp).
Lanceur Windows : [generate-test-images.ps1](../../../scripts/generate-test-images.ps1).

## Utilisation

Depuis PowerShell à la racine du projet :

```powershell
.\scripts\generate-test-images.ps1 couleur
.\scripts\generate-test-images.ps1 lumiere
.\scripts\generate-test-images.ps1 all
```

**Un seul argument** : `all` ou `raw`, `lumiere`, `courbes`, `couleur`,
`melangeur`, `grading`, `effets`, `bruit`, `nettete`, `optique`.
Pas de paramètres d'images, de valeurs, de sortie ou de campagne. Un argument
absent ou inconnu affiche l'usage et arrête le programme avant toute écriture.

Le programme prend **toutes les images directement à la racine de
`tests/image/base`**. Il ne descend pas dans les sous-dossiers : les TIFF LR,
IPR et autres références qui s'y trouvent ne deviennent pas des entrées.
Les RAW reconnus par le moteur et TIFF/PNG/JPEG/BMP/WebP sont acceptés ;
les `.ipr`, `.xmp` et autres fichiers non image sont ignorés. Deux sources
avec le même nom sans extension sont refusées pour éviter des sorties ambiguës.

Le lanceur retrouve le build Release et ses dépendances dans
`build-windows-msvc`. Pour compiler le générateur :

```powershell
cmake --build build-windows-msvc --config Release --target iprGenerateTests
```

Avec les dépendances déjà accessibles, lancement direct :

```powershell
.\build-windows-msvc\Release\iprGenerateTests.exe couleur
```

Sous Linux/macOS : `cmake --build build --target iprGenerateTests`, puis
`./build/iprGenerateTests couleur`. L'exécutable retrouve `base` en remontant
depuis le répertoire courant, puis depuis son dossier de build si nécessaire.

## Dossiers partagés et noms explicites

Les chemins ci-dessous sont relatifs à `tests/image/base`. Les dossiers
existants sont réutilisés ; seuls ceux qui manquent sont créés. Aucun rangement
par date, numéro de campagne ou sous-dossier par source.

| Groupe | Réglages et destinations |
| --- | --- |
| `raw` | `raw/auto-wb` |
| `lumiere` | `lumiere/exposure`, `contrast`, `shadows`, `blacks`, `highlights`, `whites` |
| `courbes` | `courbes/curve-luminance`, `curve-red`, `curve-green`, `curve-blue` |
| `couleur` | `couleur/temperature`, `couleur/nuance`, `couleur/niveaux_gris`, `couleur/vibrance`, `couleur/saturation` |
| `melangeur` | `color-mixer/red`, `orange`, `yellow`, `green`, `cyan`, `blue`, `violet`, `magenta` |
| `grading` | `grading/shadows`, `grading/midtones`, `grading/highlights` |
| `effets` | Dossiers existants `texture`, `clarte`, `correction_de_voile` ; `effets/blur`, `effets/grain` |
| `bruit` | `bruit/luminance`, `bruit/couleur` |
| `nettete` | `nettete/sharpening-amount`, `sharpening-radius`, `sharpening-masking` |
| `optique` | `optique/lens-correction`, `optique/chromatic-aberration` |
| Neutres frais | `neutre/<source>-IPR-neutre-<groupe>.tiff` ; suffixe `all` pour une exécution globale |

Chaque variante est nommée `<source>-IPR-<réglage>-<valeur>.tiff`.
Exemples :
`couleur/temperature/6V1A0327-IPR-temperature-2000.tiff`,
`color-mixer/red/6V1A0327-IPR-mixer-red-hue--50.tiff`.
Le double tiret signifie une valeur négative, pas une numérotation automatique.
Les valeurs décimales utilisent un point, par exemple `exposure--0.5`.

Les TIFF Lightroom peuvent garder leur nom dans les mêmes dossiers :
le générateur ne les renomme, ne les écrase et ne les supprime jamais.
Chaque exécution crée son neutre frais ; elle ne réutilise pas un neutre
potentiellement ancien. Un neutre par groupe permet de générer plusieurs
groupes successivement sans conflit entre leurs références.

## Séries fixes

| Réglage | Valeurs |
| --- | --- |
| Exposition | −1, −0,5, 0, +0,5, +1 EV |
| Contraste, ombres, noirs, HL, blancs | −100, −75, −50, −25, 0, +25, +50, +75, +100 |
| Température | 2000, 3500, 5200, 6500, 9000, 50000 K |
| Nuance | −150, −100, −50, −25, 0, +25, +50, +100, +150 |
| Vibrance, saturation, texture, clarté, voile, chaque composante du mélangeur | −100, −50, −25, 0, +25, +50, +100 |
| Bruit luminance/chrominance, grain | 0, 25, 50, 100 |
| Netteté quantité | 0, 25, 50, 100, 200 |
| Netteté rayon | 0,3 ; 1 ; 2 ; 3 |
| Netteté masquage | 0, 25, 50, 100 |
| Grading teinte, par zone | 0°, 60°, 120°, 180°, 240°, 300° |
| Grading saturation, par zone | 0, 25, 50, 100 |
| Courbes, par canal | −100, −50, 0, +50, +100 |
| WB automatique, objectif, aberration chromatique | 0, 1 |

Chaque variante repart de réglages neufs : aucun cumul entre commandes.

- Neutre et commandes hors WB : température de référence du décodage,
  nuance 0, WB automatique désactivée.
- Température et nuance : support manuel **5200 K / nuance 0**. Les variantes
  température gardent la nuance à 0 ; les variantes nuance gardent 5200 K.
  Les fichiers température 5200 et nuance 0 sont les ancres manuelles. Le
  neutre As Shot du groupe ne remplace pas ces ancres pour une comparaison WB.
- RAW : Camera Neutral est préféré quand disponible, avec le repli habituel
  du moteur sinon. Le DCP/LCP réellement signalé est affiché dans la console ;
  ne pas supposer Camera Neutral appliqué si absent. Normalisation
  expérimentale d'environnement désactivée ; sidecars ignorés.
- Rayon/masquage : quantité de netteté fixe 50. Grading teinte : saturation
  fixe 25 ; grading saturation : teinte fixe 30°. Ces supports sont nécessaires
  pour rendre le réglage observable.
- Courbes : points `(0,0)`, `(0.5, 0.5 + 0.004 × valeur)`, `(1,1)`.
  Ce sont des presets diagnostiques, pas des curseurs Lightroom équivalents.
- Les corrections objectif et CA sont activées dans le décodage de base.
  Les séries optiques testent chacune 0/1 avec l'autre correction désactivée :
  comparer leur état 1 à leur état 0, pas au neutre général. Le statut réel de
  correction est affiché ; sans profil adapté, une activation peut rester sans
  effet. Optique et WB automatique nécessitent un RAW ; elles sont ignorées
  avec un message explicite pour les sources déjà développées.
- Profils DCP/LCP arbitraires, calibration RAW, masques, recadrage, IA et gestes
  IHM ne sont pas simulés par ces séries. Leur validation a sa recette dédiée.

## Écriture et nettoyage

Sorties **TIFF 16 bits sRGB SDR, ICC embarqué, pleine résolution**, suivant le
même moteur de rendu que l'IHM. Aucun proxy de prévisualisation, JPEG ou HDR.

Le programme ne produit **aucun fichier auxiliaire** : ni manifeste JSON,
mapping, journal, CSV ou sidecar. Il n'existe aucun mode ancien ou option
de compatibilité. Les anciennes recettes de campagnes sont supprimées.

Toutes les destinations sont vérifiées avant de générer. Si un export IPR ou
le neutre du groupe existe déjà, le programme s'arrête sans l'écraser :
supprimer manuellement les résultats IPR à régénérer avant de relancer.
Les dossiers redirigés hors de base sont refusés. Une réserve minimale de
512 Mio est contrôlée avant chaque export. Le programme estime aussi l'espace
de toute la série avant écriture, après un décodage supplémentaire par source,
sans garder plusieurs images décodées en mémoire. C'est une estimation avec
marge, pas une garantie mathématique pour toutes les géométries optiques.
`all` sur toute la banque peut
occuper un volume considérable : commencer par un groupe. Un échec laisse
les TIFF déjà écrits en place, sans suppression automatique.

**Après analyse, l'utilisateur supprime les images manuellement.**
Aucune image existante n'a été supprimée lors de cette refonte.

## Analyses et vérification

Les JSON **de mesures** versionnés et horodatés restent la mémoire canonique
des analyses ; ne pas les confondre avec les anciens manifestes de génération.
Les noms explicites identifient les états ; le TIFF contient aussi une déclaration
JSON dans `ImageDescription` (tag 270) : réglages complets, source/exécutable
SHA-256, date UTC, profil et corrections réellement rapportés par le moteur,
référence relative sous base. Aucun fichier supplémentaire n'est créé.
Les noms DCP ne prouvent pas leur identité cryptographique ; cette limite est
conservée dans les mesures. Les déclarations ne constituent pas une signature
contre la falsification d'un TIFF.

```bash
python tests/analyse_couleur.py --base tests/image/base --strict --data-version 1
```

L'analyseur lit ces déclarations et le XMP LR. Un ancien TIFF IPR sans déclaration
n'est pas déduit de son nom : utiliser une association explicite vérifiable ou
régénérer. Les groupes sans contrôle XMP connu (courbes/grading/optique, par
exemple) demandent une association opérateur et restent diagnostics si non auditables.
Les anciens JSON d'analyse restent conservés comme preuves historiques.

Les tests ciblés `testImageGeneratorTests` et `imageExportTests` couvrent le
contrat actuel : groupes/all, lecture non récursive, isolation des commandes
et violet/magenta, ancres WB, TIFF/ICC, non-écrasement IPR/LR, absence
d'auxiliaires et préservation des originaux/sidecars. Ils utilisent un petit
corpus temporaire, sans générer des séries dans la banque de validation.
