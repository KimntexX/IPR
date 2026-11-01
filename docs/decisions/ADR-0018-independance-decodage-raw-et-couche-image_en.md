<!-- translation-source: docs/decisions/ADR-0018-independance-decodage-raw-et-couche-image.md; sha256: 8de5e5ba8d13d66bf45fb940e6af0362ae2716b291057f83b946b0b2cf996e9a -->
*[Français](ADR-0018-independance-decodage-raw-et-couche-image.md) | [English](ADR-0018-independance-decodage-raw-et-couche-image_en.md)*

# ADR-0018 — RAW-decoding and image-layer independence

- **Date:** 2026-10-20
- **Status:** accepted direction; initially **not scheduled before completion
  of basic feature integration**. Revises the [1 October dependency report](../architecture/references/rapport-dependances-opencv-libraw-2026-10-01.md).
- **Decision makers:** user

## Context

**31 October 2026 update, superseding the initial schedule:** separation has
started and IPR floating development is integrated in `dev`(`b19b4c3`).
LibRaw still reads/decompresses(`unpack`) and supplies metadata.
`RawDevelopment` develops eligible Canon Bayer inputs; ineligible paths retain
LibRaw fallback. Camera output is float32; signed mode remains experimental,
off by default.

Current user direction: qualify IPR processes first for Canon CR3(R6 Mark III),
then CR2(60D), full/compressed modes with dedicated corpora, then R5/6D Mark II.
**LibRaw remains planned for other brands:** total removal in the original
decision is no longer the target. The user contacted Canon Developer and awaits
a response; no manufacturer information received or new codec qualified is
claimed. OpenCV separation remains separate, not started by this observation.
See the [RAW separation report](../validation/analyses/analyse-developpement-raw.md).
Estimates/start restrictions below are historical.

The 1 October report recommended retaining LibRaw/OpenCV because removing them
did not reduce opening time. That performance criterion remains correct but
does not address dependency risk.

- **Canon CR3:** LibRaw 0.22.2 reports zero/inconsistent black on some R6 Mark III
  CR3 despite masked-pixel pedestal. IPR compensates through `RawBlackLevelEstimator`
  ([profile engine](../camera-profile-engine.md)). On 20 October the user found
  the same black issue in RawTherapee, which reads RAW through LibRaw since 5.11.
- **Sony:** ARW6(A7R VI, a7 V) illustrates ongoing new-format pressure. Codec
  uses public SMPTE RDD34; LightCraft decodes A7R VI, while a7 V was not
  publicly verified in this review.
- **Nikon:** NEF HE/HE\* use JPEG XS/TicoRAW, patented by intoPIX. LibRaw 0.22
  refuses them and darktable does not support them in this review.
- **Precedent:** LightCraft(Rust, MIT/Apache-2.0) wrote roughly ten format
  decoders using black-box oracles and AI agents; independence is attainable
  ([analysis](../architecture/roadmap-decodeurs-raw.md#lightcraft-consulté-le-20-octobre-2026)).
- **User observation:** relying on an external framework for camera/format
  integration is a serious IPR problem.

At the original decision, IPR already interpreted optical black, sensor modes
(ADR-0011), characterization catalog(ADR-0012), DCP color, LCP and WB estimation.
LibRaw supplied container, decompression and `dcraw_process` demosaicing.
OpenCV use was limited to `core`/`imgproc`.

## Original decision

1. **Target:** IPR RAW decoders, then IPR image layer, as **distinct workstreams**
   on separate durable branches: LibRaw first, OpenCV second.
2. **Initial start:** after basic-feature integration/validation(proposed beta
   entry). Exception: immediately remove unused OpenCV `imgcodecs` and vcpkg
   `jpeg`/`png`/`tiff` options; unused-code cleanup changes no pixels.
3. **Oracle stages:** bitwise validation for unchanged-image steps(mosaic,
   metadata); rendering-changing steps(demosaicing/filter kernels) use difference
   measurements then user validation.
4. **Original no-dual-implementation delivery:** LibRaw was to remain production
   and branch oracle during work, then leave app/tests at switch(no legacy).
   Reference hashes remain in JSON; an external out-of-repo tool supplies later
   camera references. The current other-brand retention direction above
   supersedes total removal.
5. **Clean implementation:** use public format descriptions/observable behavior,
   not copied LGPL(LibRaw, rawler/dnglab) or GPL(RawTherapee, modified dcraw) code.
   IPR has no selected license; reciprocal-code reuse first requires that
   choice. Permissively licensed LightCraft(MIT/Apache-2.0) code/docs may be
   read/ported with attribution; IPR's oracle still validates it.
6. **Camera integration:** tools, minimal corpus, automation and AI agents
   described in the [RAW-decoder roadmap](../architecture/roadmap-decodeurs-raw.md).
   Agents prepare/measure/document; the user validates.
7. **Initial format scope:** CR3(R6 Mark III/R7, then R5), CR2(announced
   6D Mark II), DNG, then Sony; Nikon reevaluation excludes HE/HE\* without
   a freely available route. Current priority includes 60D as stated above.

## Consequences

- **Benefits:** integrate cameras/modes on IPR's schedule, remove black-level
  workaround, tiled decompression/parallel demosaicing(probable opening gain,
  not measured), control rendering precision.
- **Costs/risks:** internal unmeasured estimates were 9–15 weeks for CR3 and
  4–8 for OpenCV. Each camera becomes IPR work, the cost that brought
  RawTherapee back to LibRaw. Demosaicing/blur changes require Light/color/
  detail revalidation.
- **Compatibility:** sidecar format unchanged; ADR-0012 `decoder_id`/
  `decoder_version` rules need IPR-decoder requalification, not copied LibRaw rules.
- **Validation:** bitwise full-corpus oracle, reader robustness, render
  campaigns and user visual validation before `dev` merge.

## Links

- [RAW-decoder roadmap](../architecture/roadmap-decodeurs-raw.md),
  [OpenCV-independent layer](../architecture/roadmap-couche-image-sans-opencv.md)
- Code: `src/raw/RawDecoder.cpp`, `src/raw/RawBlackLevelEstimator.cpp`,
  `CMakeLists.txt`, `vcpkg.json`
- Related: ADR-0011/0012/0013/0017.
- [Revised 1 October dependency report](../architecture/references/rapport-dependances-opencv-libraw-2026-10-01.md).
