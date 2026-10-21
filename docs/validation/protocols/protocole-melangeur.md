# Protocole — Mélangeur de couleurs

Version 1 — 10 octobre 2026. Fusion des recettes extrêmes et intermédiaires.
Statut : protocole prêt, conclusions historiques conservées dans les analyses.
Appliquer le [socle commun](protocole-commun.md).

## Moteur, corpus et états

Huit bandes indépendantes : rouge 0°, orange 30°, jaune 60°, vert 120°,
cyan 180°, bleu 240°, violet 270°, magenta 300°. La campagne sept bandes
est historique : son dernier cercle violet déclaré était en réalité Magentas.
Ne pas renommer ces états ni les assimiler au nouveau violet.

0327 pour rouge/orange/jaune/cyan/bleu ; 0037 pour vert ; 0043 pour
violet/magenta. Compléter avec gris, faibles chromas, peau et gradients entre
bleu/violet/magenta. Viser environ 1000 échantillons propres et une zone
continue par bande (seuil pratique de préparation, pas norme).

Une composante d'une seule bande varie ; toutes les autres sont à zéro.
Nouvelle campagne : `−100, −50, −25, 0, +25, +50, +100`, pour H/S/L
séparément. Les métadonnées TIFF/XMP priment sur la numérotation. Conventions historiques de mémoire :

| Mode | Teinte | Saturation | Luminance |
| --- | --- | --- | --- |
| `extremes` | 1/2 = −100/+100 | 3/4 = −100/+100 | 5/6 = −100/+100 |
| `intermediate` | 1–4 = −50/−25/+25/+50 | 5–8, mêmes valeurs | 9–12, mêmes valeurs |

Le n°1 signifie « sans suffixe », pas neutre. Ne jamais mélanger les deux modes.

## Audit et mesures

Auditer XMP incorporé LR, autres bandes/composantes, réglages hors mélangeur
et neutre propre. Les nouvelles séries utilisent Camera Neutral et bruit/
netteté à zéro, contrairement à certaines anciennes séries Standard à 40/25.
Une bande peu présente, un neutre absent ou un état ambigu empêchent sa validation.

Mesures natives 16 bits, échantillonnage déclaré ; HSV en RGB linéaire,
Y Rec.709. Masque diagnostique fixé sur neutre LR : H à ±15° du centre,
S≥0,15, `0,01<Y<0,9`, avec effectifs. Il ne reconstruit pas les supports internes.
Sans LR, définir un masque interne propre distinct et ne pas présenter les
résultats comme une campagne inter-moteurs identique.

- H : différences angulaires circulaires, exclusion des faibles chromas.
- S : moyenne de `S−S0`.
- L : rapport des sommes de Y au neutre propre, EV si rapport positif.
- Progressivité sur les états ordonnés, avec zéro. Tolérances de la campagne
  intermédiaire : 0,05° pour H, 0,0001 pour ΔS et ratio Y ; conventions de
  calcul, pas seuils de qualité ni garanties pixel par pixel.
- Neutres achromatiques préservés ; transitions de supports, dérive de Y,
  clipping, variations hors bande et couleurs saturées. La luminance sélective
  courante est bornée à ±0,5 EV avant gamut selon ADR-0005.
- MAE directe et MAE des réponses après soustraction des neutres seulement si
  géométrie compatible. Le recadrage automatique d'un pixel est une hypothèse,
  pas une registration certifiée. ΔE76 Lab D65 n'est pas ΔE00 ni vérité physique.

## Outils et reproduction

```bash
python tests/analyse_melangeur.py --base tests/image/base --scene 6V1A0327 --strict --data-version 1
```

Le catalogue est uniquement celui à huit bandes. Les dossiers de bande sont
partagés par H/S/L : les contrôles proviennent des métadonnées, jamais d'une
numérotation universelle. Si plusieurs fichiers LR correspondent au même
état, fournir un mapping explicite ou corriger la série avant mesure.
Les JSON des anciennes campagnes sont conservés, mais ne servent pas
d'implémentation de compatibilité du mélangeur courant.

Rapport unique courant : [analyse Mélangeur](../analyses/analyse-color-mixer.md).
Les rapports extrêmes/intermédiaires restent historiques. Recette IHM : cercles,
valeurs/champs, séparation violet/magenta, reset et restauration par image.
Conservation et clôture : socle commun. Une moyenne monotone ne valide ni
transitions, qualité visuelle ni parité LR.
