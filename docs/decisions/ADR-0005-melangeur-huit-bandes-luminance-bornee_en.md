<!-- translation-source: docs/decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md; sha256: 98f3a51f8d49c697b2a9c0ad0f5478465f51bc450e81aaea80ac0d4972340237 -->
*[Français](ADR-0005-melangeur-huit-bandes-luminance-bornee.md) | [English](ADR-0005-melangeur-huit-bandes-luminance-bornee_en.md)*

# ADR-0005 — Eight-band mixer and bounded selective luminance

- **Date:** 4 October 2026
- **Status:** Accepted for implementation; photographic calibration pending
- **Complements:** ADR-0001(sidecar), ADR-0002(rendering), ADR-0004(preferences)

## Context

**Reading on 31 October 2026:** this retains the initial 4 October model and
migrations. The current mixer has evolved toward response tables described in
the [single report](../validation/analyses/analyse-color-mixer.md). The master
and luminance coefficients now follow ADR-0009. Old comparator switches at the
end are not current CLI instructions; use the [analyzer guide](../validation/tools/analyseurs-groupes.md).

The HSL campaign showed grays assigned to H=0, positive luminance gains above
×20 in some dark regions and saturation/Y coupling. The final circle labeled
“purple” actually controlled300° magenta. See the
[extended corpus](../validation/analyses/analyse-color-mixer-corpus-etendu-2026-10-04.md).

## Decision

Isolate computation in `src/core/ColorMixer.{h,cpp}` for shared preview/export
rendering, with eight HSV centers in **linear RGB**: red 0°, orange 30°,
yellow 60°, green 120°, cyan 180°, blue 240°, purple 270°, magenta 300°.
Triangular source-hue masks vanish at neighboring centers. They sum to one
around the circle; add controls before applying them, not successive
multiplication in overlaps.

For S=(max−min)/max, t=clamp(S/0.05,0,1) and q=t²(3−2t), weights are q·wᵢ.
Achromatics are exactly unchanged and low-chroma response is continuous.
Pixels with a negative channel remain untouched, without hidden clipping.

- Hue: H'=H+18°·Σ(q·wᵢ·Hᵢ/100).
- Saturation: S'=clamp(S·(1+Σ(q·wᵢ·Sᵢ/100)),0,1).
- After HSV→RGB, restore source Y(coefficients 0.2126/0.7152/0.0722), then
  apply Y'=Y·2^[0.5·Σ(q·wᵢ·Lᵢ/100)].
- Imported controls are bounded to ±100; selective gain stays within 1/√2 and√2,
  without an additive term pushing weak signals toward white.
- 0.05 and 0.5 EV are named/tested **initial empirical choices**, not a standard
  or reproduction of Lightroom's algorithm.

Y preservation is a **linear, pre-output-conversion** contract. Gamut mapping
and clipping may alter exported luminance. This does not make HSV perceptually
uniform, recover clipped pixels or fix neutral RAW-development differences
between IPR and Lightroom.

## Compatibility

Sidecar becomes **v4**, eight triplets, read alongside v1/v2/v3:

| Old format | Old→new indices | Added zero bands |
| --- | --- | --- |
| v1/v2, six bands | 0→0,1→2,2→3,3→4,4→5,5→7 | Orange 1,purple6 |
| v3, seven bands | 0–5 unchanged,6→7 | Purple 6 |
| v4, eight bands | 0–7 unchanged | None |

The old final triplet retains **magenta** identity even if exports were stored
under purple. No duplication into purple or automatic TIFF renaming.
Curves, grading, noise and annotations follow the arrays without relative
layout change. Next save writes v4; old applications cannot read it.
Back up sidecars before using an older executable. Values migrate but the
new model/supports change nonzero-control rendering. All-zero settings keep
the same starting rendition.

INI becomes **schema 2**: `selection/mixerBand` accepts 0–7; old schema 1(or
unversioned) index 6 migrates to 7. Schema 2 index 6 stays purple. External keys
are preserved; versions above 2 remain unchanged/read-only.

## Validation and next steps

Dedicated tests: `colorMixerMathTests`, `colorMixerNeutralTests`, `imageExportTests`,
`colorMixerQmlTests`, preferences and narrow-panel geometry tests.
The old comparator defaulted to `--ipr-mixer-version legacy-seven` for old
TIFFs; `--ipr-mixer-version eight` declared new exports. The operator supplied
that choice; it was not inferred from TIFF contents.
Regenerate IPR series, especially purple/magenta on 6V1A0043 and green on 6V1A0037.
Check±100 then±25/±50, transitions, halos, gamut and preview/export before
declaring photographic calibration validated.
