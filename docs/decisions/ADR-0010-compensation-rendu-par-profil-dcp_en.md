<!-- translation-source: docs/decisions/ADR-0010-compensation-rendu-par-profil-dcp.md; sha256: c30e47af8f918912941167d7dab3e1130b6988ab4c232b77a7e2bec3a32b33f0 -->
*[Français](ADR-0010-compensation-rendu-par-profil-dcp.md) | [English](ADR-0010-compensation-rendu-par-profil-dcp_en.md)*

# ADR-0010 — Camera/DCP rendering compensation

**Status:** accepted and locally integrated; Windows build/checks passed.
**Date:** 16 October 2026. **Branch:** `patch_raw_opening`.

## Context

**16 October evolution:** [ADR-0011](ADR-0011-regles-raw-par-mode-et-configuration-vivante_en.md)
extends the initial three-RAW/ISO integration below. Current schema is 3,
without photo/ISO lists; configuration updates are tracked automatically
and calibration is requested by default. Source baseline, DCP offset and
external compensation remain distinct.

[RAW/profile report §48](../validation/analyses/analyse-base-colorimetrique.md#48-résidu-r7--recalage-et-coefficient-candidat--15-octobre-2026)
identifies −0.02 EV pre-DCP R7 Camera Neutral compensation. Source DNG +0.35 EV
remains documented; compensation is not a new measurement of that metadata.
The user accepts the rendition and requests integration without changing R6,
with possible later IPR DCPs carrying their own parameters.

## Decision

- Separate source `cameraBaselineExposureEv` and rendition preference
  `profileExposureCompensationEv` in DCP options.
- Compose gain once before HSV/LookTable/curve:
  `gain=2^(BaselineExposureOffset_DCP + source_baseline + profile_compensation)`.
  Output master remains linear D65 Rec.2020; no late sRGB gain.
- Declare compensation in existing `config/raw-normalization.json`
  `profile_rendering`, without another configuration file or Adobe-file changes.
  Each entry binds make/model, DCP SHA-256, allowed ISO, expected source
  baseline, EV and provenance.
- Activate only after resolving already audited exact RAW calibration:
  source hash, profile, ISO, blacks, backend, camera WB. Initially R7
  ISO 20000/32000, baseline +0.35, compensation −0.02; no generalization
  to other model files or untested ISO.
- Other cameras, changed/different profiles or out-of-scope ISO inherit no
  coefficient: zero compensation. Invalid/ambiguous applicable rules refuse
  calibration without partial application.

## Functional path and state

1. Opening/restoration recovers selected profile/manifest.
2. `RawDecoder` reads metadata, resolves `RawNormalization` and prepares
   immutable DCP options for this image only.
3. `CameraProfileEngine` composes gain with native DCP parameters before
   nonlinear operators; manual edits follow.
4. Temperature/tint edits reuse options from camera source: compensation
   is neither lost nor accumulated on each movement.
5. UI distinguishes baseline, compensation and effective baseline.
   RAW calibration must be enabled; this ADR does not change its default(ADR-0007).

The complete manifest already participates in cache/restoration hashing.
Engine identity adds `|profile-render=1`, preventing old-pixel reuse under
the new recipe. No new `.ipr` field or per-photo coefficient duplication:
derive the rule from persisted profile/manifest. Profile changes invalidate
selection and resolve parameters again. Fast uncalibrated path receives no
compensation; a calibrated context cannot be reused for another profile,
because white level may also depend on recipe.

## Future IPR DCPs

Prefer native **BaselineExposureOffset(51109)** to embed rendition offsets
in an IPR DCP; the engine already reads it at the same stage. Do not invent
Adobe tags or rewrite Adobe profiles. Choose native offset or equivalent
external compensation, never double-count the same adjustment.

Parameters not represented by documented DCP tags can accompany the profile
library with explicit schema, hash, domain and provenance. This future move
is not implemented here; separate physical RAW levels, artistic profile
rendering and manual edits. New DCP hashes do not automatically inherit the
Adobe R7 correction.

## Consequences and limits

Adds bounded selection within an already-read manifest and addition to existing
gain, without new decode, full-image table or pixel pass. This is not a latency
measurement. R6 receives no rule. Blue/shadow differences and highlights are
not declared solved; low/mid-ISO R7 and other cameras need validation before expansion.

Check selection/isolation, DCP/WB composition and cache/restoration, then replay
16 unchanged R6 and 3 R7 matching the accepted native candidate.

Executed evidence: [19-RAW production replay](../validation/raw/integration-profile-rendering-v001-20261015T220248871933Z.json).
All 16 R6 controls and 3 R7 candidates match exactly on the step-8 grid.
All 22 Windows CTests pass; this is not full-resolution pixel certification
or validation of every ISO.

For an old recipe whose manifest changed, the UI refuses new bytes until
selection is confirmed: **Choose manifest…**, select current
`config/raw-normalization.json`, then check “compensation −0.02 EV; effective
baseline 0.33 EV”. Do not bypass guards by manually editing `.ipr` hashes
or deleting sidecars.
