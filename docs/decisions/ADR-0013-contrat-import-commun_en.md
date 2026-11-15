<!-- translation-source: docs/decisions/ADR-0013-contrat-import-commun.md; sha256: 3a3bda468e1fba0790a728603f0f521e3539b02c3c87c23ad4e59b437057f20d -->
*[Français](ADR-0013-contrat-import-commun.md) | [English](ADR-0013-contrat-import-commun_en.md)*

# ADR-0013 — A shared import boundary for formats

Date: 16 October 2026. Status: user-approved direction, implemented locally;
qualification limited to documented tests/corpus.

## Context

RAW camera development and ICC conversion of developed images must remain
specialized. Duplicating downstream rendering, checks or metadata reading
in the editor would increase divergence risk. The user approves specialized
inputs feeding a common engine.

## Decision

Introduce `ImageImport::Result` and `ImageImport::complete()` as the common
boundary. `ImageImport::load()` orchestrates RAW/raster. Extract `ImageMetadata`
from `RawDecoder` without aliases or an old raster-reading implementation.
Cache and fast-profile changes reconstruct the same result and pass the same
checks before the editor consumes it.

The contract requires a D65 linear Rec.2020 float32 RGBA master, metadata,
provenance, warnings and effective capabilities. Camera parameters, DCP/ICC
and output conversions are not applied twice. Proxy generation remains the
editor's responsibility after the full-resolution contract.

## Consequences

- One boundary check independent of extension, without a legacy path.
- No RAW processing added to JPEG; no ICC reading applied to camera signal
  as if it were a developed image.
- Cache returns and profile edits share checks; preserve editor errors and
  non-destructive behavior.
- Linear-in-pixels validation cost without a clone/conversion at this step;
  optimization must measure that cost, not assume zero.
- Codecs, plugins and characterization domains remain specialized; the
  common interface certifies neither every format/camera nor fidelity.

The [detailed contract](../architecture/import-images.md), diagram and limits
live in architecture. Results/evidence remain in the single base-colorimetry
report and timestamped JSON memories.
