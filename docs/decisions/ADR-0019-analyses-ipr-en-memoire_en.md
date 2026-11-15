<!-- translation-source: docs/decisions/ADR-0019-analyses-ipr-en-memoire.md; sha256: bdf937250717f01009ebfdc75881f56d993bfb11b79c93d39539b6c1fe902529 -->
*[Français](ADR-0019-analyses-ipr-en-memoire.md) | [English](ADR-0019-analyses-ipr-en-memoire_en.md)*

# ADR-0019 — IPR states rendered in memory for analysis

- **Date:** 2026-10-20
- **Status:** Accepted; implemented on `patch_in_memory_analysis`, user
  validation/merge pending at the original decision
- **Decision makers:** user

## Context

**Documentation observation 31 October 2026:** server, shared recipes and
in-memory analysis exist in `dev`(`b19b4c3`). Branch status above describes the
original 20 October decision, not an outstanding merge. Evidence below remains
dated and is not cross-platform qualification by this review alone.

Each campaign wrote a full-resolution16-bit TIFF per IPR state(about258 MB
for 32 MP), read it back for analysis, then relied on user deletion.
On20 October 2026, `tests/image` held737 IPR TIFFs(190.5 GB) and 712 Lightroom
references(138 GB) on an87%-full disk; full19-scene Light regeneration(about245 GB)
had been abandoned for lack of space. IPR TIFF was only intermediate;
measurement JSON is the sole canonical memory.

## Decision

- `src/tools/TestRecipes` alone owns controls/values, development recipe
  (Camera Neutral, RAW calibration, optics off outside optical series),
  supports and provenance declaration.
- `iprRenderServer` reads stdin JSON; per state it returns JSON header
  (dimensions, ICC, declaration), then TIFF-export uint16 RGB samples through
  `ImageEditor::renderSrgb16`. Keep the current source master in memory;
  independently decoded states(auto WB/optics) are decoded then released.
- `tests/analysis --base` renders root-level base sources and reads Lightroom
  TIFFs. Base IPR TIFFs are no longer analyzed. One scene per server; server
  count follows free memory(`--render-workers` overrides).
- `iprGenerateTests` remains on-demand visual review using the same recipes.
  Lightroom references remain once-exported TIFFs.

## Consequences

- **Benefits:** no measurement TIFF writes; stable campaign disk footprint;
  no need to manually delete those IPR exports; full campaigns possible again.
- **Costs/risks:** rerunning needs the same recorded-SHA-256 executable.
  Memory limits parallelism: estimated2.5 GiB/server for 32 MP, requiring
  measurement; on a loaded16 GB machine often only one fits. Windows/Linux/macOS
  equivalence is not verified.
- **Compatibility/migration:** historical JSON stays valid/unchanged.
  New JSON uses `ipr_pixels.kind=in_memory_render`; IPR pixel hashes cover
  samples, not a file.
- **Validation:**
  - native small-corpus test:50 Light states bit-identical to generator TIFFs,
    including ICC/settings; eight-group campaigns without writing files;
  - real CR3(6V1A0007): neutral, Whites+100 and Contrast+50 bit-identical to
    18 October TIFFs; Highlights−100 differs as expected after 19 October model
    adjustment(commit818ffca);
  - measured20 October throughput:100 Light states, two CR3(6V1A0007/0043),
    one server,4.2 GiB free,437s or4.4s/state including render/transfer/measurement;
    100`measured`, no issue. The old generator also held one render at a time;
    gain concerns disk, write/read and single-pass handling, not render time itself.

## Links

- Code: `src/tools/TestRecipes.*`, `src/tools/render_server.cpp`,
  `src/tools/generate_test_images.cpp`, `tests/analysis/render.py`
- Tests: `testImageGeneratorTests`, `tests/test_analyse_groupes.py`
- [Analyzer guide](../validation/tools/analyseurs-groupes.md),
  [generator guide](../validation/tools/generateur-images-tests.md)
- Rule: test-image-generation section in `AGENTS.md`.
