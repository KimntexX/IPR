# Analyse du réglage Vibrance

**Date :** 2 octobre 2026  
**Corpus :** huit scènes TIFF sRGB neutres, avec deux extrêmes Lightroom et IPR dans `tests/image/base/vibrance`.

## Méthode

La chrominance relative en RGB linéaire est `C = (max(R,G,B) - min(R,G,B)) / max(R,G,B)`. Chaque export est comparé au TIFF neutre produit par le même moteur. Les résultats sont séparés entre couleurs peu saturées (`C < 0,25`) et couleurs déjà saturées (`C ≥ 0,50`). La variation relative `ΔC/C` est le critère principal de l'adaptation : une chrominance absolue plus importante induit naturellement un écart absolu plus grand, même si le réglage protège bien cette couleur.

Les mesures sont reproductibles avec `tests/analyse_vibrance.py` et conservées dans `mesure-vibrance-brut-2026-10-02.txt`.

## Résultats

| Moteur | Variante négative : ΔC | ΔC/C faible C | ΔC/C forte C | `Y` | Clipping | Variante positive : ΔC | ΔC/C faible C | ΔC/C forte C | `Y` | Clipping |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Lightroom | `-0,3669` | `-94,8 %` | `-74,0 %` | `0,8202` | `1,284 %` | `+0,2245` | `+108,1 %` | `+39,5 %` | `0,9603` | `4,934 %` |
| IPR | `-0,1718` | `-73,2 %` | `-28,4 %` | `1,0004` | `1,002 %` | `+0,1420` | `+68,3 %` | `+22,7 %` | `0,9993` | `2,405 %` |

## Verdict

Vibrance est **validée fonctionnellement** :

- les variantes ont le bon signe dans les deux moteurs ;
- dans IPR, l'effet relatif est toujours plus fort sur les couleurs peu saturées que sur les couleurs déjà vives (`73,2 % > 28,4 %` en diminution, `68,3 % > 22,7 %` en renforcement) ;
- la luminance IPR reste neutre à moins de `0,1 %` et le clipping est contenu.

IPR est volontairement plus modéré que Lightroom : l'amplitude relative atteint environ `68–76 %` de Lightroom dans les couleurs peu saturées, et `38–57 %` dans les couleurs déjà vives. Cette différence ne remet pas en cause le comportement du contrôle ; la modifier par un simple facteur global risquerait de perdre la protection des couleurs saturées. La parité fine sera traitée avec le chantier colorimétrie, après validation fonctionnelle des autres réglages.
