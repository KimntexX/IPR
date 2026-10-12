*[Français](README.md) | [English](README_en.md)*

# Validation Protocols

October 10, 2026 consolidation: **one evolving protocol per group**, with a [common base](protocole-commun.md). A campaign produces a new versioned/timestamped JSON memory, not a dated protocol per cursor.

Start with the [autonomous procedure](../procedure-tests-autonomes.md), the [generator](../tools/generateur-images-tests.md), and the [matrix](../matrice-validation.md). **Ready protocol ≠ validated feature**: old Standard validations are not automatically transferred to the Camera Neutral pipeline.

## Current Catalog

| Group | Maintained Protocol | Coverage / Limits |
| --- | --- | --- |
| RAW / profiles / base | [RAW and Profiles](protocole-raw-profils.md) | Base, DCP, WB, normalization, restoration; neutral/auto-WB generation only |
| Lights | [Lights](protocole-lumieres.md) | EV, contrast, shadows, blacks, HL, whites |
| Curves | [Curves](protocole-courbes.md) | Luminance and R/G/B; native analyzer/JSON, complementary ramps/HDR |
| Color | [Color](protocole-couleur.md) | Temperature, tint, vibrance, saturation, grays; native analyzer/JSON |
| Mixer | [Mixer](protocole-melangeur.md) | Eight H/S/L bands, extremes, and intermediates |
| Color grading | [Grading](protocole-grading.md) | Native analyzer/JSON; circularity and complementary transitions |
| Effects | [Effects](protocole-effets.md) | Texture, clarity, veiling, grain, blur; native/JSON ROIs |
| Noise | [Noise](protocole-bruit.md) | Luminance/chrominance, activation, Haar ROI; separate AI |
| Sharpening | [Sharpening](protocole-nettete.md) | Native analyzer/JSON; edge profiles and complementary coupling |
| Optics | [Optics](protocole-optique.md) | Lens and CA independent; no generator sweep |
| UI / persistence / export | [Cross-functional Recipe](protocole-ihm-persistance-export.md) | Sidecars, INI, zoom/comparison, names; DNG not validated, batch export not implemented |

All photographic groups from the generator are covered. Cropping, user geometry, masks, and AI remain outside production recipes until implemented.

## Mergers and Specialized Methods

- Temperature + Tint + Vibrance + Saturation → Color, with gray levels.
- Texture + Clarity + Veiling → Effects, with Grain/Blur.
- Mixer extremes + intermediates → Mixer.
- Initial noise + v2 → Noise.

The eleven old files become stable references. Historical JSONs and verdicts remain unchanged; old wording remains in Git history.

Separately maintain the [Canon Whites diagnostic](protocole-blancs-canon.md), attached to RAW, distinct from Kelvin and the Whites cursor.
The [v1 base protocol](protocole-base-colorimetrique-v1.md) preserves charter, color charts, and bibliography; its UI/export context is historical.

## Memory and Maintenance

Follow the [JSON contract](../data/README.md) and the base: negative → IPR exports; positive and validated → IPR and LR of the series; ambiguous/incomplete memory → no deletion. Preserve RAW/common neutrals, log targets. One report per set; derived CSV/curves.

Create a group from the [template](../../templates/validation-protocol-template.md). Use the [unified analyzers](../tools/analyseurs-groupes.md) for new campaigns. Old modes/probes remain historical; specialized recipes not covered by common metrics are reported.
