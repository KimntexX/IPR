# Protocole de validation — Température

**Date :** 2 octobre 2026  
**Références disponibles :** `tests/image/base/neutre` (état neutre) et `tests/image/base/temperature` (deux exports Lightroom par scène).

## Objectif

Vérifier que le curseur Température IPR déplace la chromaticité dans le même sens et avec une amplitude comparable à Lightroom, sans faire varier anormalement la luminance ni écraser les canaux.

## Exports IPR attendus

Pour chacune des huit scènes, exporter un TIFF sRGB 16 bits dans `tests/image/base/temperature` :

| Référence Lightroom | Export IPR attendu | Réglage Température |
|---|---|---|
| `6V1Axxxx.tif` | `6V1Axxxx-IPR.tiff` | même température que cette référence Lightroom |
| `6V1Axxxx-2.tif` | `6V1Axxxx-IPR-2.tiff` | même température que cette référence Lightroom |

Les deux états doivent être notés explicitement lors de l’export (par exemple `2000 K` et `50000 K`). Le suffixe ne suffit pas à lui seul pour déduire de manière certaine l’état chaud ou froid.

Conserver tous les autres réglages à leur valeur neutre : exposition `0`, contraste `0`, Noirs/Ombres/Hautes lumières/Blancs `0`, nuance `0`, saturation et vibrance `0`. Utiliser la même balance automatique ou *As Shot* que les références Lightroom ; pour le corpus actuel, *As Shot* est le choix de comparaison recommandé.

## Mesures prévues

La validation comparera chaque état au TIFF neutre propre à son moteur :

- gains moyens linéaires R, G et B ;
- variation de `log(R/B)` pour mesurer l’axe chaud/froid sans dépendre de l’exposition globale ;
- stabilité de la luminance moyenne ;
- pourcentage de pixels dont un canal est écrêté ;
- cohérence du signe et de l’amplitude IPR par rapport à Lightroom.

Le moteur IPR est prêt : le curseur accepte `2000–50000 K` et applique une adaptation chromatique Bradford entre la température détectée du RAW et la température demandée. La comparaison quantitative pourra démarrer dès que les 16 TIFF IPR sont présents.
