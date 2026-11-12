<!-- translation-source: docs/decisions/ADR-0017-imageeditor-cadenceur-modules.md; sha256: 1a051e9dfc6bca8d30f7a5343570bf1000b3ae1da3a01f2d720d8b96a86bb489 -->
*[Français](ADR-0017-imageeditor-cadenceur-modules.md) | [English](ADR-0017-imageeditor-cadenceur-modules_en.md)*

# ADR-0017 — `ImageEditor` as processing-module coordinator

- **Date:** 2026-10-19
- **Status:** Accepted; first step applied on `patch_light`
- **Decision makers:** user; application on `patch_light`

## Context

`imageeditor.cpp` mixed UI state, cache, loading and image processing: about
2,900 lines, including pixel loops and roughly fifteen processing lambdas
in `renderWorkingImage`. sRGB transfer was duplicated there and in
`ColorPipeline.cpp`/`CameraProfileEngine.cpp`. Tonal adjustments lived entirely
in a header, recompiling the editor on model changes. Include guards mixed
`#pragma once` with inconsistent names.

The user plans panels such as layers/filters, geometry and image removal.
Code should remain harmonious after each validation to stay maintainable.

## Decision

- `ImageEditor` becomes a **coordinator** for UI state, cache, loading and
  treatment sequencing, without image processing. `renderWorkingImage`
  calls modules in a fixed order.
- Each treatment is a **module** under `src/core/`: `.h` interface, `.cpp`
  implementation. Pixel loops and their helper functions share one `.cpp`,
  enabling inlining without link-time optimization.
- Without state/resources, expose **free namespace functions**; use classes
  only for state/invariants, such as `ToneRecovery::Curve`/`ImageCacheController`.
- Keep Qt slots, `QtConcurrent`, `qScopeGuard` and `std`-algorithm lambdas;
  turn other processing lambdas into functions.
- Standard `IPR_<PATH>_<FILE>_H` guards, no `#pragma once`.

These rules are recorded in [AGENTS.md](../../AGENTS.md), 19 October 2026.

### Modules from the first step

| Module | Contents |
| --- | --- |
| `ToneRecovery` (+ `ToneStatistics`) | Shadows, blacks, highlights, whites; per-image statistics |
| `Contrast` | Contrast curve |
| `ColorAdjustments` | Grayscale, saturation/vibrance, mixer and grading |
| `DetailProcessing` | Existing noise/sharpening, texture, clarity, dehaze, grain |
| `ToneCurve` | Tone curves and point type |
| `Histogram` | Preview histogram |
| `ImageExporter` | Formats, unique names, linear DNG, write then publish |
| `Sidecar` (+ `EditRecipe.h`) | `.ipr` read/write and edit data |
| `WorkingColor`, `ImageMath` | One sRGB transfer, luminance, `smoothStep` |

## Consequences

- Benefits: `imageeditor.cpp` shrinks from 2,938 to about 2,100 lines;
  `renderWorkingImage` only sequences; modules test/recompile independently;
  generated tonal model is included only by `ToneRecovery.cpp`.
- Costs/risks: bit-level rendition changes could escape functional tests.
  Mitigation: `openingLatencyProbe equivalence`, master/preview/histogram
  SHA-256 for 22 settings on 6V1A0043/0524, identical before/after each step;
  two runs confirm determinism.
- Compatibility/migration: none. `.ipr` stays v6; export messages and QML API
  stay unchanged. `ImageEditor` retains type aliases `Settings`, `RawDevelopment`,
  `UserImageMetadata`, `ExportResult`, `ToneCurvePoint`.
- No visual validation needed for bit-identical output. Other files' processing
  lambdas(`CameraProfileEngine`, `raw/`, `cache/`) and further editor
  responsibilities(loading, cache, selection) remain outside this step.

## Links

- Code: `src/core/`, `src/editor/imageeditor.cpp`
- [Rendering documentation](../imageeditor-render-working-image.md)
- Tests/protocol: 27-test suite; `openingLatencyProbe equivalence`
- [ADR-0001](ADR-0001-sidecar-ipr_en.md), [ADR-0009](ADR-0009-espace-travail-rec2020-lineaire_en.md).
