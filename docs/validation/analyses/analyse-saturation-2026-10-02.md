# Analyse du réglage Saturation

**Date :** 2 octobre 2026  
**Corpus :** huit scènes TIFF sRGB neutres et deux extrêmes Lightroom/IPR dans `tests/image/base/saturation`.

## Méthode

Les exportations sont comparées au TIFF neutre de leur propre moteur en RGB linéaire. La chrominance est `C = (max(R,G,B) - min(R,G,B)) / max(R,G,B)`. Les mesures brutes proviennent de `tests/analyse_saturation.py` et sont conservées dans `mesure-saturation-brut-2026-10-02.txt`.

## Résultats

| Moteur | Variante négative : ΔC | ΔC/C faible C | ΔC/C forte C | `Y` | Clipping | Variante positive : ΔC | ΔC/C faible C | ΔC/C forte C | `Y` | Clipping |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Lightroom | `-0,4552` | `-87,5 %` | `-87,5 %` | `0,9984` | `1,109 %` | `+0,1939` | `+91,1 %` | `+36,1 %` | `0,9974` | `7,199 %` |
| IPR | `-0,4299` | `-100,0 %` | `-100,0 %` | `1,0007` | `0,999 %` | `+0,2382` | `+89,9 %` | `+45,7 %` | `1,0017` | `3,460 %` |

## Verdict

Saturation est **validée fonctionnellement et globalement** :

- la variante négative IPR produit une désaturation complète, conformément au contrat du curseur `-100` ;
- la variante positive augmente fortement la chrominance, avec une amplification des faibles chrominances très proche de Lightroom (`+89,9 %` contre `+91,1 %`) ;
- la différence sur les couleurs très saturées est cohérente avec la compression de gamut IPR, qui limite également le clipping (`3,460 %` contre `7,199 %`) ;
- la luminance IPR est conservée à moins de `0,2 %` sur les deux extrêmes.

Lightroom ne désature pas entièrement au minimum de ce corpus (`-87,5 %`), alors qu'IPR applique volontairement le facteur nul documenté à `-100`. Il s'agit d'une différence de choix d'implémentation, sans défaut de comportement. La parité stricte, si elle devient souhaitable, relève du chantier colorimétrie/gamut mapping et non d'une correction fonctionnelle immédiate.
