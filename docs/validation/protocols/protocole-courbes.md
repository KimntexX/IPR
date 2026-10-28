# Protocole — Courbes de tonalité

Version 1 — 10 octobre 2026. Statut : recette proposée, pas validation acquise.
Appliquer le [socle commun](protocole-commun.md).

## Corpus et préparation

Rampe grise et rampes RGB synthétiques, gris constants, 0007 pour faibles
chromas, 0327 pour couleurs et 9944/9947 pour contre-jour. Lumières et couleur
à zéro. Courbes luminance et R/G/B testées séparément, autres canaux identités.

## États et procédures

Le générateur `courbes` utilise `curve-luminance`, `curve-red`, `curve-green`,
`curve-blue`, valeurs −100/−50/0/+50/+100. Chaque valeur définit les points
`(0,0)`, `(0,5 ; 0,5+0,004×valeur)`, `(1,1)`. Ce sont des courbes diagnostiques,
**pas** des valeurs de curseur Lightroom équivalentes. Dans LR, reproduire les
points lorsque possible, en déclarant domaine/interpolation ; sinon comparer
les formes qualitativement sans revendiquer une identité de modèles.

En IHM, ajouter/déplacer/supprimer les points, changer de canal sans perdre
les autres courbes ; tester extrémités, points proches, tentative d'abscisses
identiques et reset. Documenter le comportement supporté, ne pas supposer une
interpolation particulière ou des courbes non monotones si elles sont interdites.

## Critères / outils

Identité pour chaque canal, conservation des extrémités attendues, continuité
et absence de NaN/inversion pour les courbes monotones ; relever sur les rampes
l'écart au neutre et les marches. Courbe d'un canal : vérifier indépendance des
autres avant les transformations de gamut ; distinguer l'effet couleur attendu
du clipping. Vérifier sidecar, source TIFF et RAW, aperçu/export et domaines
hors [0,1] dans des tests moteur séparés : le TIFF SDR ne certifie pas le HDR.

Génération et [analyseur natif/JSON](../tools/analyseurs-groupes.md) disponibles
via `tests/analyse_courbes.py` : classes fixes de Y et points déclarés.
Points arbitraires, rampes/continuité et HDR restent complémentaires.
Rapport : [analyse des courbes](../analyses/analyse-courbes.md).
La réussite d'un export ne remplace pas ces critères.

## 2. Campagne Lightroom — 26 octobre 2026

Corpus large d'office : les 28 RAW de la racine de `base`, comme le grading.
Préréglages `IPR Courbes` (`scripts/make_lr_presets.py --controls curves`, 36 états),
courbes par points de Lightroom en coordonnées 0–255, une seule courbe modifiée par
état, toutes les courbes écrites :

- courbe RVB composite : point milieu (128) à −64, −32, −16, +16, +32, +64 ; points
  des quarts (64 et 192) à ±32 ; extrémités déplacées (noir relevé à 32, blanc
  abaissé à 223, coupe des noirs à 32, coupe des blancs à 223) ; S doux, S fort,
  S inverse ; une courbe à quatre points intérieurs ;
- courbes rouge, vert et bleu : point milieu à −64, −32, +32, +64 ; quart bas relevé
  (64 → 96), quart haut abaissé (192 → 160).

Exports TIFF 16 bits réduits à 2048 pixels de grand côté dans `courbes`, neutre de la
même géométrie repris de `grading/neutre`. La courbe paramétrique de Lightroom (Hautes
lumières, Clairs, Sombres, Ombres) n'existe pas dans IPR et n'est pas exportée.
