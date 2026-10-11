# Protocole — Couleur

Version 1 — 10 octobre 2026. Consolidation de Température, Nuance, Vibrance
et Saturation ; ajout des niveaux de gris. Statut : recette consolidée prête,
**revalidation sur le pipeline courant à effectuer**. Les validations anciennes
ne sont pas automatiquement transférées aux nouveaux profils.

Appliquer le [socle commun](protocole-commun.md). Ne pas confondre Couleur
(commandes globales), [Mélangeur](protocole-melangeur.md) (bandes sélectives)
et [Grading](protocole-grading.md) (zones tonales).

## Corpus ciblé

0327 : roche, textiles/crashpads et peau ; 0043 : fleur violet/magenta et verts ;
0007 : faibles chromas, ciel/voile ; 9421 : carnations et prises sous éclairage
artificiel (bruit et lumière mixte à signaler). Commencer par deux scènes puis
étendre si une famille de couleurs est peu représentée. Ces objets ne sont pas
des références Lab certifiées ; une charte relève du protocole RAW/base.

## États isolés

| Contrôle générateur | Première série | Complément / ancre |
| --- | --- | --- |
| `temperature` | 3500, 5200, 6500, 9000 K | 2000/50000 K : stress, pas cible esthétique ; ajouter la température neutre réelle |
| `tint` | −100, −50, −25, 0, +25, +50, +100 | Plage IPR −150…+150 ; origine relative IPR différente de la nuance As Shot LR |
| `vibrance` | −100, −50, −25, 0, +25, +50, +100 | Saturation à zéro |
| `saturation` | −100, −50, −25, 0, +25, +50, +100 | Vibrance à zéro |
| `grayscale` | 0, 1 | IPR seul d'abord ; LR monochrome a son propre profil/mélange, non équivalent par défaut |

La WB automatique reste désactivée. Pour la nuance Lightroom, conserver sa
nuance As Shot dans le neutre et consigner la valeur absolue **et** le déplacement
par rapport à cette ancre. Ne pas identifier arbitrairement « nuance 0 » dans
IPR à « nuance 0 » dans LR. Pour température/nuance, examiner séparément les
déplacements internes puis la comparaison de rendus à valeurs affichées données.

Depuis PowerShell à la racine :

```powershell
./scripts/generate-test-images.ps1 couleur
```

Le groupe génère les cinq contrôles sur toutes les images à la racine de base.
Les supports de la recette courante sont fixes : température à nuance 0,
nuance à température 5200 K. Utiliser température 5200 et nuance 0 comme
ancres manuelles dans chaque moteur. Le neutre frais As Shot est conservé
séparément ; il n'est pas l'ancre 5200/0. Aucune option de valeurs ou d'ancres
supplémentaires n'est acceptée. Pour les séries et dossiers, consulter le
[guide du générateur](../tools/generateur-images-tests.md#séries-fixes).

Les données historiques du 10 octobre à nuance +5 restent des preuves de
leur campagne, pas des références automatiquement compatibles avec cette
nouvelle recette. Réexporter Lightroom avec les supports courants.

Les neutres LR et IPR partagés sont stockés dans `tests/image/base/neutre`.
Leur balance As Shot n'est pas un zéro manuel : sur le corpus actuel,
Lightroom rapporte 5200 K et nuance +5 en As Shot, ce qui n'est pas une
erreur de l'utilisateur. Auditer le XMP et les corrections optiques de ces
neutres avant comparaison. Ne pas remplacer une nuance 0 manquante par
le neutre As Shot à +5, ni confondre les deux rôles de référence.
La [recette d'audit des neutres](../tools/analyseurs-groupes.md#recette-spécialisée-de-comparaison-wb)
conserve leurs identités, statistiques natives et réserves de provenance.

## Mesures et critères

- Température : gains moyens R/G/B, `Δmean(log(R/B))` sur zones non nulles,
  Y et clipping. Relativement au neutre, une température plus élevée doit
  réchauffer le rendu et une plus basse le refroidir ; vérifier la progressivité
  sans supposer une réponse linéaire en Kelvin ni une luminance identique à LR.
- Nuance : `Δmean(log(sqrt(R×B)/G))`, avec exclusion explicite des zéros.
  Le déplacement négatif va vers le vert, le positif vers le magenta ; contrôler
  gris moyens/carnations, Y et canaux écrêtés.
- Saturation et vibrance : `C=(max−min)/max` en RGB linéaire. Fixer les groupes
  sur le neutre : faible `0,02≤C<0,25`, forte `C≥0,50`. Mesurer `ΔC` et
  `ΔC/C` (exclure gris, noirs et canaux saturés), Y et clipping. Saturation
  doit agir dans les deux sens ; Vibrance positive doit présenter une protection
  relative des couleurs déjà saturées, sans assimiler les deux modèles.
- Niveaux de gris : vérifier `R≈G≈B` à une tolérance déclarée compatible avec
  la quantification 16 bits, absence de dominante et désactivation réversible.
  Mesurer les niveaux tonals ; ne pas supposer une loi Y exacte sans vérifier
  l'implémentation du moteur.
- Pour tous : continuité des gradients, neutres non colorés par saturation seule,
  peau, couleurs hors gamut et dépendance au profil. Une monotonie de moyenne
  n'est pas une garantie pixel par pixel. Les clips préexistants sont séparés
  des clips introduits par le réglage.

## Outils actuels et limites importantes

L'[analyseur de groupe](../tools/analyseurs-groupes.md), `tests/analyse_couleur.py`,
lit les associations déclarées et les TIFF natifs 16 bits, puis écrit un JSON
immuable. Il calcule axes WB, chroma faible/forte, gris, Y, clips et progressions.
Son entrée directe doit encore être adaptée aux nouveaux noms sans manifeste ;
ne pas annoncer de découverte automatique ni utiliser un diagnostic ancien
8 bits/redimensionné pour certifier cette recette.
Les tolérances, références physiques et recette visuelle restent nécessaires :
un code retour nul ne valide pas une fonctionnalité.

## Conclusion et rapport

### Sonde de balance capteur avant DCP

`wbCameraProbe`, cible CMake manuelle exclue du build par défaut, développe
les six RAW 0007/0037/0043/0327/9421/9950 à partir de la racine `base` avec
Camera Neutral, WB boîtier, normalisation et corrections optiques désactivées.
Elle vérifie l'identité exacte du neutre et mesure les températures
2000/3500/5200/6500/9000/50000 K ainsi que les neuf nuances −150…+150 à
5200 K. Aucun TIFF ni manifeste de génération n'est créé : seule une mémoire
JSON diagnostique, versionnée/horodatée et sans écrasement, est publiée.
Les pixels sont les points natifs espacés de huit, sans redimensionnement ;
le rendu DCP est ponctuel et le traitement optique a déjà eu lieu en amont.

```powershell
cmake --build build-windows-msvc --config Release --target wbCameraProbe
./build-windows-msvc/Release/wbCameraProbe.exe . docs/validation/raw/comparaison-wb-20261011
```

Avec le chemin JSON affiché, lancer sous Python avec NumPy/Pillow :

```sh
python tests/compare_wb_camera_probe.py CHEMIN_JSON_DE_LA_SONDE
```

L'analyseur audite les XMP Lightroom dans `base/couleur/temperature` et
`base/couleur/nuance`, puis compare les axes logarithmiques avec la sonde
fraîche. Il garde les mesures/exclusions LR, les réglages IPR, empreintes
sources/RAW/DCP/exécutable et limites de comparaison dans les JSON.
Un état As Shot 5200/+5 reste distinct du support manuel nuance 0.
Ces mesures ne remplacent pas l'acceptation visuelle ni les essais IHM de
continuité, aller-retour au neutre, restauration et export pleine résolution.

Consigner les cinq commandes dans le [rapport Couleur unique](../analyses/analyse-couleur.md).
Les anciennes analyses température/nuance/
vibrance/saturation restent des preuves historiques, sans maintenance parallèle.
Suivre le socle pour IHM, mémoire et nettoyage ; ne pas conclure « validé »
si l'analyseur, les neutres ou la couverture nécessaires manquent.
