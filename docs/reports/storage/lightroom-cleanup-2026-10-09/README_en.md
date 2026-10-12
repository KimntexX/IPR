*[Français](README.md) | [English](README_en.md)*

# Lightroom References Cleanup — October 9, 2026

At the user's request, old Lightroom references in Adobe Standard were deleted to prepare comparisons with Camera Neutral. Scope: `tests/image/` only, including archives.

## Executed Balance

- 423 TIFFs deleted: **81,977,111,750 bytes**, i.e., **81.98 GB / 76.35 GiB**.
- Corpus: 1,030 files before, 607 after; presence and size of the 607 preserved files controlled after deletion.
- Five Lightroom TIFFs **Camera Neutral** preserved in `base/neutre`: 6V1A0007, 6V1A0043, 6V1A0327, 6V1A9421, and 6V1A9950.
- RAW, DNG, sidecars, IPR exports, measurements, and reports preserved.
- Free space C: after: 299,710,681,088 bytes, approximately 279.13 GiB. This measurement also depends on other disk activities; it is not the gain attributable to this single operation.

## Criteria and Exceptions

Each deletion requires a TIFF without `IPR` in its name, Lightroom identification in the Software field, and the **Adobe Standard** profile in the XMP. The development profile XMP is distinct from the ICC output profile, for example sRGB: an sRGB TIFF is not necessarily developed with Camera Neutral. Absolute paths are verified in the corpus; symlinks are rejected. Size and date are controlled before deletion, and a SHA-256 fingerprint is recorded. No folder is recursively deleted.

Five Lightroom files without identifiable profile remain in place:

- `tests/image/6V1A0027.jpg`;
- `tests/image/6V1A9933.jpg`, `6V1A9933-2.jpg`, `6V1A9933-2-2.jpg`;
- `tests/image/base/archive-ev-reference/6V1A0007-2.tif`.

## Proofs and Test Recovery

- [Inventory and metadata before](inventory.csv).
- [Log of 423 deletions and fingerprints](deletion-log.csv).
- [Inventory after](inventory-after.csv).
- [Machine summary](summary.json).
- Local inventory script: `scripts/inventory-lightroom-references-20261009.py` (not versioned).
- Local bounded deletion script: `scripts/remove-standard-lightroom-references-20261009.ps1` (not versioned).
- [First cleanup of intermediate IPR](../cleanup-2026-10-09/README.md).

**Permanent deletion, without recycling bin.** Fingerprints and measurements do not backup pixels; recovery only from a possible external backup. Historical proofs remain consultable, but comparisons using these old TIFFs can no longer be relaunched with the local corpus. Regenerate Lightroom references in Camera Neutral with the white balance and optical options of the protocol before the new campaign. Scripts refuse to overwrite this audit: it is not an automatic purge.
