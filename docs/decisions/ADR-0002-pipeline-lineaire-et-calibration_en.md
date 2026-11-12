<!-- translation-source: docs/decisions/ADR-0002-pipeline-lineaire-et-calibration.md; sha256: fcf897948567b6215c263992467174f8d916469a198439ce0a16e694a003190f -->
*[Français](ADR-0002-pipeline-lineaire-et-calibration.md) | [English](ADR-0002-pipeline-lineaire-et-calibration_en.md)*

# ADR-0002 — Linear pipeline and rendering calibration

- **Date:** 3 October 2026
- **Status:** Accepted; empirical parameters require revalidation after pipeline changes
- **Decision makers:** IPR project

## Context

**Historical scope:** the sRGB decision below dates from 3 October. The current
master is linear Rec.2020 under [ADR-0009](ADR-0009-espace-travail-rec2020-lineaire_en.md).
Do not read this initial decision's Rec.709 constants as current master
constants; intermediate domains remain explicit per module.

Photometric controls, filters and gains need a space whose values are
proportional to energy. Validation also showed that purely theoretical
chromatic adaptation does not perfectly reproduce cold-temperature references.

## Decision

Use floating linear-sRGB RGBA `WorkingImage`. Render before sRGB encoding,
then compress gamut before quantization. Use Krystek + Bradford for temperature
and retain an explicitly documented empirical cold compensation limited to
the branch below as-shot reference temperature.

## Consequences

- Exposure gains, local contrast and dehaze have a coherent mathematical basis.
- Rec.709 constants, sRGB matrices and Bradford are model references, not
  parameters to change for fitting one photo.
- Tonal amplitudes, sigmas, grading factors and cold compensation are
  calibrations requiring corpus, measurement and matrix updates.
- Migration to a perceptual space or new gamut mapping requires sidecar
  rendition-compatibility comparisons.

## Validation and links

- [Adjustment specification](../specifications/reglages-photographiques.md)
- [Courses and constant guide](../scholar/README_en.md)
- [Validation matrix](../validation/matrice-validation.md)
