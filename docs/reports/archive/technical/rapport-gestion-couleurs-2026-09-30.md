# Rapport — gestion des couleurs

Date : 30 septembre 2026

## Périmètre

La gestion couleur d'IPR travaille sur une image RGBA flottante en sRGB
linéaire. L'alpha est conservé hors des réglages et le rendu est partagé par
l'aperçu, les exports JPEG/PNG/TIFF et le DNG rendu.

## Chaîne de traitement

1. Le décodage RAW applique le développement LibRaw et les profils DCP/LCP
   disponibles. Les images standards sont converties depuis leur profil ICC,
   avec repli sRGB explicite lorsque nécessaire.
2. L'exposition opère en EV (`2^EV`) et le contraste autour du gris linéaire
   18 %.
3. La température et la nuance utilisent la balance boîtier *As Shot* comme
   référence pour les RAW, puis les réglages de tons agissent avec des masques
   continus.
4. Les courbes luminance/RGB permettent une correction manuelle à points.
5. La sortie utilise une compression de gamut sRGB qui préserve la luminance
   et réduit la chroma vers un neutre, au lieu d'écrêter indépendamment R, G
   et B.

## Vibrance et saturation

Les deux curseurs sont compris entre `-100` et `+100`.

- La saturation change uniformément l'écart RGB autour de la luminance. À
  `-100`, l'image devient grise en conservant sa luminance.
- La vibrance module ce changement avec la chrominance du pixel. Les couleurs
  peu saturées sont davantage affectées, ce qui limite la sursaturation des
  couleurs déjà intenses.

Ces réglages précèdent les courbes RGB : une courbe de canal reste donc la
dernière correction utilisateur de ce canal. Ils sont non destructifs, bornés
dans `ImageEditor` et inclus dans chaque format d'export.

## Limites connues

Le pipeline reste sRGB linéaire. Il ne fournit ni espace de travail large
gamut, ni rendu perceptuel configurable, ni corrections sélectives HSL par
teinte. L'émulation bit à bit de Lightroom reste hors périmètre, notamment
pour son démosaïçage et ses corrections locales propriétaires.
