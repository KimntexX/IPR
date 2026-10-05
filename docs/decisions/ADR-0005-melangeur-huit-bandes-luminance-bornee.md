# ADR-0005 — Mélangeur huit bandes et luminance sélective bornée

- **Date :** 4 octobre 2026
- **Statut :** Accepté pour l'implémentation ; calibration photographique à confirmer
- **Complète :** ADR-0001 (sidecar), ADR-0002 (rendu), ADR-0004 (préférences)

## Contexte

La campagne HSL a montré une attribution des gris à H=0, des gains de luminance
positive de plus de ×20 dans certaines zones sombres et un couplage saturation/Y.
Le dernier cercle utilisé pour « violet » pilotait en réalité le magenta à 300°.
Voir [le corpus étendu](../validation/analyses/analyse-color-mixer-corpus-etendu-2026-10-04.md).

## Décision

Isoler le calcul dans `src/core/ColorMixer.{h,cpp}`, utilisé par le rendu commun
aperçu/export, avec huit centres HSV du **RGB linéaire** : rouge 0°, orange 30°,
jaune 60°, vert 120°, cyan 180°, bleu 240°, violet 270°, magenta 300°.
Les masques triangulaires, calculés sur la teinte source, s'annulent aux centres
voisins. Leur somme vaut un sur le cercle ; les commandes sont additionnées
avant application, sans multiplication successive dans les chevauchements.

Pour S=(max−min)/max, t=clamp(S/0,05,0,1) et q=t²(3−2t), les poids sont q·wᵢ.
Les achromatiques sont inchangés exactement ; la réponse augmente continûment
sur les faibles chromas. Les pixels comportant un canal négatif sont laissés
intacts par ce module, sans écrêtage caché.

- Teinte : H'=H+18°·Σ(q·wᵢ·Hᵢ/100).
- Saturation : S'=clamp(S·(1+Σ(q·wᵢ·Sᵢ/100)),0,1).
- Après reconversion HSV→RGB, rétablir la luminance source Y (coefficients
  0,2126/0,7152/0,0722), puis appliquer Y'=Y·2^[0,5·Σ(q·wᵢ·Lᵢ/100)].
- Les commandes importées sont bornées à ±100. Le gain sélectif est ainsi entre
  1/√2 et √2, sans terme additif qui pousse les faibles signaux vers le blanc.
- 0,05 et 0,5 EV sont des **choix initiaux empiriques**, nommés dans le code et
  testés, pas une norme ni une reproduction de l'algorithme Lightroom.

La conservation de Y est un contrat **linéaire avant conversion de sortie**.
Le gamut mapping et l'écrêtage peuvent modifier la luminance exportée. Ce modèle
ne rend pas HSV perceptuellement uniforme, ne récupère pas les pixels écrêtés et
ne résout pas l'écart du développement RAW neutre entre IPR et Lightroom.

## Compatibilité

Le sidecar devient **v4**, huit triplets, lu avec les v1/v2/v3. Correspondances :

| Ancien format | Correspondance anciens indices → nouveaux | Bandes ajoutées à zéro |
| --- | --- | --- |
| v1/v2, six bandes | 0→0, 1→2, 2→3, 3→4, 4→5, 5→7 | orange 1 et violet 6 |
| v3, sept bandes | 0–5 inchangés, 6→7 | violet 6 |
| v4, huit bandes | 0–7 inchangés | aucune |

L'ancien dernier triplet conserve donc son identité **magenta**, même si les
exports avaient été rangés dans un dossier violet. Aucune duplication vers
violet, aucun renommage automatique des TIFF. Courbes, grading, bruit et
annotations suivent les tableaux sans changement de disposition relative.
La prochaine sauvegarde réécrit en v4 ; les anciennes applications ne lisent pas
v4. Sauvegarder les sidecars avant un retour à un ancien exécutable. Les valeurs
migrent, mais le nouveau modèle et les supports modifiés changent le rendu des
réglages non nuls. Les réglages tous à zéro gardent le même rendu de base.

L'INI devient **schéma 2** : `selection/mixerBand` accepte 0–7 ; l'indice 6 de
schéma 1 (ou d'un ancien fichier sans version) est migré vers 7. L'indice 6 d'un
schéma 2 reste violet. Les clés externes sont conservées et les versions
supérieures à 2 restent inchangées, en lecture seule.

## Validation et suite

Tests dédiés : `colorMixerMathTests`, `colorMixerNeutralTests`, `imageExportTests`,
`colorMixerQmlTests`, tests des préférences et géométrie du panneau étroit.
Le comparateur conserve par défaut `--ipr-mixer-version legacy-seven` pour les
anciens TIFF ; `--ipr-mixer-version eight` déclare les nouveaux exports.
Ce choix est fourni par l'opérateur, pas déduit du contenu TIFF IPR.
Régénérer les séries IPR, particulièrement violet/magenta sur 6V1A0043 et vert
sur 6V1A0037. Vérifier ±100 puis ±25/±50, transitions, halos, gamut et cohérence
aperçu/export avant de déclarer la calibration photographique validée.
