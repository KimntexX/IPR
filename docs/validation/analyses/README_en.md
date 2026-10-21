*[Français](README.md) | [English](README_en.md)*

# Validation Analyses

Each feature set has a single evolving report; its dated campaigns remain explicitly separated there. Each campaign points to its protocol, raw data, software version, and decision. Old dated names remain as compatible references, not reports to be maintained in parallel.

Since October 9, [versioned and timestamped JSONs](../data/README.md) are authoritative for data. Before deleting exports, preserve measurements, audits, and provenance. October 10 rule: IPR only if negative verdict; IPR and Lightroom of the series if positive and validated. RAW and common neutrals are preserved; no deletion on ambiguous verdict or incomplete memory. Lights group exposure, contrast, shadows, blacks, whites, and HL in the [existing common report](analyse-validation-lumieres-base-2026-10-02.md). Old separate reports of these controls remain historical; do not update them in parallel. Future conclusions remain in the common report.

## Reference Reports

- [Color](analyse-couleur.md): temperature, tint, vibrance, saturation, gray levels; first native tooling verification on 40 states, without new visual validation or Lightroom comparison.
- [Colorimetric Base](analyse-base-colorimetrique.md).
- [Color Mixer](analyse-color-mixer.md): four grouped campaigns.
- [Noise](analyse-bruit.md): two campaigns and fix grouped.
- [CSV and Derived Graphics](../metrics/README.md).

Other families below retain their existing single report; no need to rename a file solely for its date. Historical links below refer to the corresponding campaign in the reference report.

## Recent Campaigns

- [Camera Neutral Lights — six scenes, October 9](analyse-validation-lumieres-base-2026-10-02.md#campagne-complete-lumieres--nouveaux-neutres-9-octobre-2026): 144 complete and audited variants; EV revalidated, positive Shadows amplitude, and Whites/HL saturation to be continued.
- [Camera Neutral Exposure — October 9](analyse-validation-lumieres-base-2026-10-02.md#revalidation-de-lexposition--camera-neutral-9-octobre-2026): EV law revalidated on six scenes; LR 0037 and optics 9421 complements required.
- [Noise Reduction v2 — revalidation and extended corpus](analyse-bruit-v2-2026-10-05.md): 145 variants, 24 neutrals, and twelve scenes; progressivity and dark stability confirmed, overall quality and chromatic calibration to be continued.
- [CPU Noise Fix](correctif-bruit-2026-10-05.md): progressive thresholds, prefiltered guide, luminance, and gamut; automated tests and v2 campaign available.
- [Noise Reduction](analyse-bruit-2026-10-04.md): 80 retained variants, export audits, confirmed activity but unvalidated calibration; synthetic diagnosis of thresholds, edge protection, and dark bias.
- [Mixer — intermediate values](analyse-color-mixer-intermediaires-2026-10-04.md): progressivity at ±25/±50 after archiving extremes.
- [Mixer — eight bands, extremes](analyse-color-mixer-huit-bandes-2026-10-04.md): consolidation of 54 variants by application.
- [Mixer red/yellow/green](analyse-color-mixer-red-yellow-green-2026-10-04.md): first photographic results, luminance response, and limits of the green corpus.
- [Mixer — extended corpus](analyse-color-mixer-corpus-etendu-2026-10-04.md): green on two new scenes, other bands, and distinction between violet/magenta.

## Migrated History

- [Base Lights](analyse-validation-lumieres-base-2026-10-02.md), [Blacks and Shadows](analyse-noirs-ombres-2026-10-02.md), [Whites and Highlights](analyse-blancs-hautes-lumieres-2026-10-02.md).
- [Contrast](analyse-contraste-2026-10-02.md), [Texture](analyse-texture-2026-10-02.md), [Clarity](analyse-clarte-2026-10-02.md), and [Veiling Correction](analyse-correction-voile-2026-10-02.md).
- [Temperature](analyse-temperature-2026-10-02.md), [Tint](analyse-nuance-2026-10-02.md), [Vibrance](analyse-vibrance-2026-10-02.md), and [Saturation](analyse-saturation-2026-10-02.md).

## Additional Diagnostics

- [DNG Export Not Displayed by Lightroom](diagnostic-export-dng-lightroom-2026-10-03.md).
