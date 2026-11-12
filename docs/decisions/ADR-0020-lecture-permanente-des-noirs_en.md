<!-- translation-source: docs/decisions/ADR-0020-lecture-permanente-des-noirs.md; sha256: 40ad3d560d47a439e3249f43ff9f69658a5a9525a641053a6cabb6ebefadda89 -->
*[Français](ADR-0020-lecture-permanente-des-noirs.md) | [English](ADR-0020-lecture-permanente-des-noirs_en.md)*

# ADR-0020 — Permanent black-level reading by IPR

- **Date:** 2026-10-20
- **Status:** Accepted; implemented on `patch_light`, user validation pending
- **Decision makers:** user

## Context

LibRaw defaults are not always correct: some R6 Mark III CR3 files report zero/
inconsistent black while masked pixels have a pedestal; 60D white is identical
for every ISO mode(ADR-0011 extension). IPR formerly corrected black only
through a fallback for Canon CR3 with missing LibRaw black. The user requests
permanent black reading independent of format or camera lists.

## Decision

- On **every** RAW after mosaic decoding, measure optical black by CFA-channel
  median: declared Canon areas(left, otherwise top), format-declared masked
  areas for other brands, margins as last resort for Canon only(not every
  margin is masked on other brands).
- Compare with LibRaw black(common+channel). **Keep** it if agreement is within
  `max(8,white/1000)` codes; **replace** otherwise. A declared spatial black
  pattern or invalid/implausible measurement retains LibRaw. Log the decision
  (`IPR black`) and expose metadata(`opticalBlackLevelEstimated`).
- `RawBlackLevelEstimator` becomes two pure functions independent of LibRaw:
  `measure()` and `decide()`. Remove the old CR3 fallback/format guards.
- White levels remain catalog-rule responsibility(ADR-0011/0012).

## Consequences

- Benefits: fix LibRaw black for any format/brand with masked pixels without
  a camera-specific rule; building block for IPR decoding(ADR-0018).
- Costs/risks: one masked-area histogram per opening, negligible relative
  to decoding; tolerance is a prototype choice.
- Compatibility:49 corpus RAWs(28 CR3,21 CR2) render bit-identically before/after;
  R6/R7/60D rules remain unchanged.
- Validation: measurement/decision unit tests; visual validation on new
  cameras when available.

## Links

- Code: `src/raw/RawBlackLevelEstimator.*`, `src/raw/RawDecoder.cpp` (`readBlackLevel`)
- Tests: `rawBlackLevelEstimatorTests`
- Related ADRs: ADR-0011, ADR-0012, ADR-0018.
