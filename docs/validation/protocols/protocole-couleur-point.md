# Protocole — Couleur du point

Version 1 — 27 octobre 2026. Statut : **préparation, campagne non exécutée**.
Appliquer le [socle commun](protocole-commun.md). Comparaison en boîte noire sur
des exports Lightroom, jamais par rétro-ingénierie.

## 1. Modèle à caler

Le moteur `PointColor` (`src/core/PointColor.h/.cpp`) agit dans l'étape couleur
perceptuelle, après le mélangeur et avant le color grading. Chaque point porte sa
couleur source en Oklab, ses changements de teinte, saturation et luminance
(−100 à +100), sa variance (−100 à +100) et sa gamme (0 à 100, 50 par défaut).

- **Voisinage** : poids w = f(Δteinte) · f(Δln chroma) · f(Δclarté), f lisse,
  1 à la source et 0 au-delà d'une demi-largeur qui croît avec la gamme ; les
  gris sont exclus.
- **Effets** : rotation de teinte, gain de chroma (−100 rend la source grise),
  gain de clarté, pondérés par w. La variance resserre (−) ou écarte (+) les
  teintes et chromas du voisinage autour de la source.
- **Plusieurs points** : ils agissent dans l'ordre de prélèvement, et des
  voisinages qui se recouvrent se partagent leur portée (comportement décrit par
  Adobe).

Toutes les constantes (`Calibration`) sont **provisoires** : demi-largeurs,
amplitudes maximales et forme de f sont à remplacer par les valeurs mesurées.

## 2. Préréglages modèles, dans Lightroom

Lightroom écrit chaque point dans le sidecar ou le préréglage : un texte de
`crs:PointColors` de 19 nombres (3 pour la source, puis teinte, saturation,
luminance et gamme divisées par 100, puis trois trapèzes de 4 valeurs pour les
sous-gammes de teinte, de saturation et de luminance) et la variance, divisée
par 100, dans `crs:ColorVariance`. Relevé sur le modèle du 27 octobre
(6V1A0327) ; IPR ne décode pas la source et la recopie telle quelle. Un sidecar
sans point porte des −1. Pour chaque couleur source :

1. Ouvrir une photo de la base, panneau Couleur, onglet **Couleur du point**,
   prélever la couleur à la pipette.
2. Régler les valeurs repères : teinte **+11**, saturation **+22**, luminance
   **+33**, variance **+44**, gamme **66**. Ne pas déplier les sous-gammes.
3. Transmettre le sidecar `.xmp` de la photo (métadonnées enregistrées), ou un
   préréglage `PC <couleur>` avec la seule case **Couleur du point** cochée
   (clic droit, **Afficher dans l'Explorateur**).

Couleurs sources proposées : orange de peau (6V1A0327), vert de feuillage, bleu
de ciel, rouge saturé, prélevées sur des plages unies. Le générateur refuse un
modèle dont une valeur repère manque ou apparaît deux fois.

## 3. Génération et exports

```text
python scripts/make_lr_point_color_presets.py --samples tests/image/base/6V1A0327.xmp --labels Orange
    [--states complete|reduced]
```

L'archive `tests/image/lr-presets/IPR-Couleur-point-presets.zip` s'importe dans
le panneau Préréglages ; chaque préréglage pose la base neutre IPR puis un état.
Les sous-gammes du modèle sont recopiées : après import, appliquer `G000` à une
photo et vérifier que la gamme affiche 0 avant de lancer les exports.

| Phase | Sources | États (`--states`) | Exports sur 28 scènes |
| --- | --- | --- | --- |
| 1 — forme | 1 (orange) | `complete` : T, S, L à ±25/50/100 ; V ±50/100 ; S −100 aux gammes 0/25/75/100 (26) | 728 |
| 2 — généralité | 3 autres | `reduced` : S −100 aux gammes 0 à 100 ; T, S, L +50 (8) | 672 |

Exporter selon le socle commun (TIFF sRGB 16 bits), à la géométrie du neutre
`grading/neutre`, dans `tests/image/base/couleur-point/` (sous-dossier par source). Une source
s'applique à toutes les scènes : son voisinage est mesuré sur toutes les
couleurs du corpus, pas seulement près du pixel prélevé.

## 4. Mesures

L'étude lit l'état dans les réglages inscrits dans chaque TIFF et compare
l'export au neutre de la même scène, en Oklab, pixels écrêtés exclus.

- **Voisinage** : avec saturation −100, la perte de chroma relative d'un pixel
  donne directement son poids. Profils par axe (teinte, chroma, clarté) et par
  gamme, puis forme de f ; source estimée au maximum du poids.
- **Réponses** : rotation, gain de chroma et de clarté en fonction de la valeur
  pondérée par le poids mesuré ; linéarité aux valeurs intermédiaires.
- **Variance** : resserrement ou écartement des teintes et chromas autour de la
  source.
- **Généralité** (phase 2) : demi-largeurs selon la teinte, la chroma et la
  clarté de la source ; une loi par couleur source si la forme varie.

L'étude `tests/study_point_color.py` est à écrire sur les premiers exports :
leur codage réel décide de la lecture des états. Elle produira un JSON versionné
et horodaté sous `docs/validation/raw/`, puis la comparaison IPR passera par le
serveur de rendu en mémoire, une fois les réglages portés par la recette.

## 5. Hors campagne pour l'instant

Deux points qui se recouvrent (partage et ordre), sous-gammes de teinte, de
saturation et de luminance, visualisation des plages : phase 3, après le calage
d'un point seul. Côté IHM : pipette dans l'aperçu, pastilles, recette et `.ipr`
(IHM-CONN-011).
