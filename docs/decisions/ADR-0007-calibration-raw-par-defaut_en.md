<!-- translation-source: docs/decisions/ADR-0007-calibration-raw-par-defaut.md; sha256: 943fb1981f9e8d81a442c17e6d736d37a229db9e22163b28d65e07b73c15aa16 -->
*[Français](ADR-0007-calibration-raw-par-defaut.md) | [English](ADR-0007-calibration-raw-par-defaut_en.md)*

# ADR-0007 — RAW calibration enabled by default

**Date:** 15 October 2026. **Status:** user-approved decision; default behavior
implemented16 October 2026; advanced panel integrated in `dev`, documentation
observation 22 October 2026.

## Context

Audited radiometric calibration corrects white level and baseline exposure
before DCP rendering. At the initial decision it was explicitly enabled in
the UI and stored per image. Coverage was limited to exact RAW/DCP pairs;
requested enablement did not establish effective application.

## Decision

Calibration shall be **enabled by default**. Disablement belongs in
**advanced settings to be defined later**, not ordinary photographic controls.
This concerns audited RAW calibration, not automatic white balance or optics.

Retain eligibility guards and separate requested “enabled” from effective
“applied/not applicable/error”. Never default to another RAW/DCP's recipe.
Unknown files must be reported: default enablement neither expands validation
coverage nor means universal calibration.

## Consequences and work initially left to define

- Define advanced panel, global/per-image override scope, persistence and reset.
- Define handling of existing disabled sidecars without silently changing edits.
- Apply policy consistently in UI, generator and exports, reporting effective
  state and rendering recipe.
- Test first opening, restoration, profile changes, cache and ineligible files
  before delivering new behavior.

The 15 October decision changed no code. Its16 October implementation and
existing-recipe handling now follow [ADR-0011](ADR-0011-regles-raw-par-mode-et-configuration-vivante.md):
active default, automatically tracked configuration, respected stored disablement,
model/mode rules without a photo list. Corpus restrictions in the initial
context are historical, not current-schema guards.

## Integration observation — 22 October 2026

Preferences / Advanced · RAW exposes the open image's switch and manifest
selection. Current scope is per-image with sidecar persistence, not a global
preference replacing every recipe. Global evolution remains tracked in UI
connections. This observation does not replace historical conditions above.

## References

- [RAW/profile engine](../camera-profile-engine.md)
- [ADR-0002](ADR-0002-pipeline-lineaire-et-calibration_en.md)
- [Evolving report, §39](../validation/analyses/analyse-base-colorimetrique.md#39-réexports-investigation-de-lactivation-et-étude-hl--15-octobre-2026)
