<!-- translation-source: docs/decisions/ADR-0012-formats-extensibles-et-caracterisations.md; sha256: e738cdb49b7b12a9625eeea43984d526928b7c1292837057a9c4f24e6a470e70 -->
*[Français](ADR-0012-formats-extensibles-et-caracterisations.md) | [English](ADR-0012-formats-extensibles-et-caracterisations_en.md)*

# ADR-0012 — Extensible formats and characterization storage

Date: 16 October 2026. Status: user-requested structural decision accepted;
first package locally implemented, full transition pending.
Complements ADR-0010/0011 without changing accepted R6/R7 rendering.

## Decision

Separate decoder understanding of files, characterization of camera/mode
and decoder, and DCP color transformation/rendering. No one element alone
makes the engine universal.

### Formats not yet known

`RawDecoder::registerFormatHandler()` is the explicit entry. An adapter
registers extensions, decode function and `FormatOptions` with optional
thumbnail reader, decoder ID/version. Callbacks execute outside registry lock.

There is no wildcard pretending to decode unknown formats. No adapter means
explicit error; no thumbnail reader means no thumbnail, never silent LibRaw
attempt. Reject duplicates atomically. Normalized extensions are alphanumeric.

Register at startup before folder discovery. Models/providers use registered
extensions. This is **not** a DLL plugin loader or content-signature detector;
design those separately if needed. Every adapter must verify content, supply
metadata and produce a finite linear D65 Rec.2020 float RGBA master.
Output validation does not guarantee decode color accuracy.

### Selected storage: complementary DCP and JSON

| Storage | Selected role | Benefits | Limits/cost |
| --- | --- | --- | --- |
| DCP | Matrices, illuminants, tables, curve, native rendering offset | Portable among engines implementing tags; self-contained color profile | Does not describe decompression/all sensor modes/levels; private tags lack interoperability; do not modify Adobe profiles |
| Versioned JSON | Characterization catalog and IPR rendering associations | Readable/Git-reviewable, explicit provenance/conditions, no new dependency | Strict validation/coherence/invalidation; no complex transactional querying |
| SQLite | Possible derived index/cache for large libraries | Transactions, indexes, relations, partial access | Extra schema/migrations/tools, binary Git diffs; unjustified for three rules |

**Now:** keep embedded/deployed `config/raw-normalization.json` as the sole
schema-4 catalog. `entries` holds mode rules/evidence scope; `profile_rendering`
holds camera/DCP rendition preferences. Timestamped analysis JSON remains
canonical evidence; production catalog is not its substitute. `.ipr` sidecars
are photo recipes, not characterization-database copies. Catalog path stays unchanged.

**After physical characterization:** sensor parameters independent of DCP,
rendering parameters associated with profiles. Current empirical R6/R7 rules
remain tied to validated DCP SHA-256; the first package removes neither
that precaution nor turns them into universal physical measurements.
Future IPR DCP `BaselineExposureOffset` replaces the equivalent JSON supplement;
never add the same adjustment twice.

For a large catalog, consider per-camera JSON assembled for distribution,
then a **rebuildable** SQLite index. Create no second truth source or SQL import now.

## Current contract and cost

Each schema-4 rule declares `format_extension`, `decoder_id`, `decoder_version`,
make/model, native black/white bounds, selected white, EV baseline, DCP SHA-256
and provenance. Compare IDs/versions/make/model exactly, not by similar names.
Context belongs to each image, not the folder's first camera.

`CameraCharacterization` loads/selects modes without reading RAW/DCP.
`RawNormalization` still applies WB/baseline conditions, checks profiles,
validates values and resolves rendering supplement. Uncovered format/model/
mode/decoder retains uncalibrated path without another camera's coefficients.

Limits: 1 MiB catalog, 256 rules, eight immutable parsed documents cached by
content under mutex. Re-read/hash the small catalog to detect updates;
selection remains linear in rule count. Still hash DCP identity. This avoids
reparsing, not all I/O, and promises no measured opening speedup.

Implemented domain: scalar 16-bit bounds, four black levels, camera WB.
Another CFA/scale or already supplied native baseline may need a new adapter/
contract, not merely a JSON line. No Sony/Nikon qualification is declared.

20 October 2026 supplement: optional integer `specular_white_min` and
`specular_white_max` must appear together. They bound file-metadata Canon
specular white; bounded rules never match missing values. Rules without them
ignore this field(R6/R7 unchanged). Audit via `scripts/audit_camera_levels.py`.

## Add a camera

1. Check decoder support for format/capture mode; otherwise implement/register
   adapter and thumbnail reader.
2. Audit exact identity, relevant firmware, bit depth, CFA, compression, level
   scale, native blacks, saturation white and WB. Neither marketing name nor
   one well-rendered photo proves physical calibration.
3. Build timestamped JSON evidence/application scope: provenance, method,
   units and limits. Current black windows do not completely identify overlapping modes.
4. Add catalog rule only after trials; associate a genuinely compatible DCP
   and measure rendering parameters separately. New DCP hashes do not inherit offsets.
5. Test low/high ISO, compressed/uncompressed RAW if available, neutrals/colors/
   highlights and alternation with already validated cameras.
6. Deliver data/evidence under the normal validation workflow. Recipes
   reference catalog, not duplicate its coefficients.

For richer future characterization, conceptually allow stable ID/revision,
explicit readout mode, ISO range, black/white laws/units, gains/WB, measured
read/shot-noise model, uncertainty, measurement source, license and evidence
references. These fields are **not interpreted today**; extend contracts/tests
before adding them, not unused catalog values. Lens corrections remain
separate(LCP/other optical models).

## Transition and validation

**31 October 2026 update:** the LibRaw role in the second package below
describes its initial integration. `RawDevelopment` now handles floating
development of eligible Canon Bayer inputs; reading, decompression and
metadata remain LibRaw. See ADR-0018 and the RAW separation report.
This observation qualifies no new codec or camera.

1. **Implemented:** decoder/thumbnail registry, decoder identity, generic
   selection and schema-4 catalog, without legacy schema-3 parser.
2. **Locally implemented initial package:** `CameraSignal` extracts pre-DCP
   camera RGB/common rendering for LibRaw+DCP and fast profile changes.
   Check finite RGB float, identity, applied WB and normalized neutral.
   Buffer becomes immutable source; rendering gains apply once. At this
   initial package LibRaw still handled decompression, demosaicing,
   normalization and optics; non-DCP fallback stayed unchanged.
3. **17 October cache implemented:** immutable DCP/LCP in bounded shared LRU,
   concurrency/content invalidation tested(ADR-0014). Profile-independent
   physical characterizations remain to establish.
4. **Pending:** R5 CR3 and 6D Mark II CR2 on receipt, then other brands.
   Do not reuse R6/R7 CR3 rules for those files.

Synthetic tests cover unknown extensions, dedicated thumbnail reader, master
contract, decoder identity, fictitious brand/format data, ambiguities and
cache updates. The 19-RAW replay separately checks accepted rendering.
An extensible interface is not actual new-format support or all-ISO validation.

## Sources consulted 16 October 2026

- [Adobe DNG specification/SDK](https://www.adobe.com/support/downloads/dng/dng_sdk.html): RAW/profile separation; private metadata without implicit interoperability.
- [Library of Congress TIFF/DNG tags](https://www.loc.gov/preservation/digital/formats/content/tiff_tags.shtml): `BaselineExposure`, `BaselineExposureOffset`(51109) and their rendering-exposure sum.
- [SQLite application file format](https://www.sqlite.org/appfileformat.html): transactions/partial access; selecting JSON for this small catalog is IPR's decision, not SQLite's prescription.
