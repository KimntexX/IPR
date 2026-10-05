# Analyse du réglage Clarté

**Date :** 2 octobre 2026  
**Corpus :** huit scènes TIFF sRGB neutres, deux extrêmes Lightroom/IPR dans `tests/image/base/clarte`.

## Méthode

Clarté est mesurée sur six zones de 512×512 pixels à définition native. L'indicateur principal est le résidu de luminance après un lissage local 17×17, adapté à l'échelle moyenne du rayon `sigma = 8` du pipeline IPR. Les gradients, la luminance, la dérive de chrominance et le clipping sont également comparés au TIFF neutre de chaque moteur. Le script est `tests/analyse_clarity.py` et les mesures brutes sont dans `mesure-clarte-brut-2026-10-02.txt`.

## Résultats

| Moteur | Variante négative : contraste local | Gradients | `Y` | Δ chrominance | Clipping | Variante positive : contraste local | Gradients | `Y` | Δ chrominance | Clipping |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Lightroom | `0,540` | `0,500` | `0,923` | `0,0075` | `0,727 %` | `2,455` | `2,472` | `1,400` | `0,0274` | `0,731 %` |
| IPR | `0,773` | `0,730` | `0,990` | `0,0026` | `0,326 %` | `1,277` | `1,332` | `1,010` | `0,0026` | `0,322 %` |

## Verdict

Clarté est **validée fonctionnellement et globalement** : les deux directions modifient bien le contraste local à moyenne échelle et restent opposées. IPR privilégie une action protectrice :

- luminance stabilisée à environ `±1 %`, contre une variation Lightroom de `-7,7 %` / `+40,0 %` dans ce corpus ;
- chrominance presque inchangée (`0,0026`) ;
- clipping inférieur à `0,33 %`.

L'amplitude locale IPR est plus modérée que Lightroom, notamment au maximum positif. C'est cohérent avec le masque qui atténue volontairement l'effet sous `0,16` de luminance et au-dessus de `0,72`, pour limiter les halos et l'écrêtage. L'évaluation visuelle sur des bords très contrastés reste utile avant un réglage d'amplitude pour viser une parité Lightroom stricte, mais aucun défaut fonctionnel ou colorimétrique n'est détecté.
