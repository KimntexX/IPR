<!-- translation-source: docs/release/validation-summary.md; sha256: 18c535667e6d4e3c13166554a39c26990ab5f2d26e2800670ed232ee8feacf61 -->
*[Français](validation-summary.md) | [English](validation-summary_en.md)*

# Release validation summary

Release `v0.1.92-alpha`, status on 1 November 2026. The photographic scope
below is unchanged since `v0.1.86-alpha`: the following six patches concern
deployment, without changes to `src/`. This documentation update is not a
new photographic validation. Full measurements, analyses and evidence stay
on `dev`, at the source commit of this version (see `release-manifest.json`
at the root); the links below lead there.

## Checks and distributions for this release

- Source: `dev` `2facc24332a265f0d24b7939953aa3aed795b583`,
  [successful full CI](https://github.com/KimntexX/IPR/actions/runs/41342811574).
- Filtered promotion `a67c140`: manifest and fingerprints verified; tests,
  analysers and measurements remain on `dev`.
- Merged `main` commit: `23521cdd783d2c23aaa486fd6c3fc2690775e1b7`,
  [successful production CI](https://github.com/KimntexX/IPR/actions/runs/41347235082),
  target of `release-v0.1.92-alpha`.
- Permitted distributions: Windows x64 system and standalone (ZIP and installer),
  Linux Ubuntu standalone. The macOS system arm64/Intel configurations remain
  compiled in CI, but **no macOS package is published for this release**;
  follow-up: [#20](https://github.com/KimntexX/IPR/issues/20).

These checks neither qualify every Linux distribution nor replace full manual
testing on user machines without development tools.

## How IPR is validated

- **Lightroom comparison**: for each adjustment, IPR and Lightroom render the
  same reference photos at the same settings; differences are measured and
  then visually reviewed by the user. Lightroom is a reference, not a target:
  some differences are intentional technical choices.
- **User visual validation** for each adjustment before delivery.
- **Automated tests** on Windows, Ubuntu and macOS (arm64 and Intel) at each
  integration, and a code audit (November 2026).

Adjustment-by-adjustment details are in the
[validation matrix](../validation/matrice-validation.md).

## Covered cameras

Canon EOS R6 Mark III and EOS R7 (CR3), Canon EOS 60D (CR2), developed by
IPR itself (LibRaw remains a fallback). Other cameras open through LibRaw,
without dedicated validation.

## Validated adjustments

| Group | Adjustments | Reservations |
| --- | --- | --- |
| Light | Exposure, contrast, shadows, highlights, blacks, whites | Exposure not measured beyond ±2 EV ([#11](https://github.com/KimntexX/IPR/issues/11)); Lightroom's contrast pivot is image-dependent ([#12](https://github.com/KimntexX/IPR/issues/12)) |
| White balance | Temperature, tint | Extreme blues around 2000 K ([#10](https://github.com/KimntexX/IPR/issues/10)) |
| Colour | Vibrance, saturation, HSL mixer, colour grading, point colour, curves | Large saturated blue skies at +100 vibrance |
| Effects | Texture, clarity, dehaze, grain, blur | Clarity halos in backlighting; dehaze in night scenes |
| Detail | Noise reduction, sharpening | IPR removes less luminance noise than Lightroom and retains more detail (technical choice); very noisy images need improvement ([#14](https://github.com/KimntexX/IPR/issues/14)); sharpening Detail control ([#13](https://github.com/KimntexX/IPR/issues/13)) |
| Optics | Lens correction, lateral chromatic aberration | Strength is not adjustable |
| Base | Camera Neutral profile on opening | Lightroom fidelity not measured |

## Validated workflows

Batch adjustment application and export, selection through the context menu,
open-folder refresh, settings saved in `.ipr` files (older versions remain
readable), JPEG, PNG, TIFF and linear DNG export. Lightroom reads the DNG;
Photoshop and DxO have not been checked
([#9](https://github.com/KimntexX/IPR/issues/9)).

## Known limits

- Above 100%, zoom enlarges the 2048-pixel preview instead of the full-resolution image.
- Histogram messages remain technical ([#17](https://github.com/KimntexX/IPR/issues/17)).
- Validation covers the cameras above, not every camera on the market.
