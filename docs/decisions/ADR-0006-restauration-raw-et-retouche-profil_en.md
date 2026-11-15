<!-- translation-source: docs/decisions/ADR-0006-restauration-raw-et-retouche-profil.md; sha256: 54ed910b0603ba9126e4caad9dbf105df5f87c64b1d0fb8072eee4f8aa3f0e77 -->
*[Français](ADR-0006-restauration-raw-et-retouche-profil.md) | [English](ADR-0006-restauration-raw-et-retouche-profil_en.md)*

# ADR-0006 — Per-image RAW recipe and profile changes as edits

- **Date:** 8 October 2026
- **Status:** Accepted; Windows visual recipe pending
- **Complements:** ADR-0001, ADR-0003, ADR-0005

## Decision

`.ipr`v5 keeps v4 fields then adds, in order: DCP path/SHA-256, manifest
path/SHA-256, normalization enablement and engine identifier including LibRaw
version. Read v1–v5; older versions implicitly enable no calibration.
Older executables cannot read v5.

Restore RAW options and automatic WB **before** decoding/cache lookup.
Freeze automatically resolved DCP per image once its path is known. The editor
prefers Camera Neutral when available; diagnostic tools keep their defaults.

A DCP change must not clear the displayed image or reset zoom, comparison or
edits. A worker applies the new DCP to an independent copy of demosaiced,
white-balanced, LCP-corrected camera RGB. Failure preserves previous rendition
and recipe. After success, refresh comparison original: development with the
new profile before edits.

## Integration observation — 22 October 2026

The 8 October decision and initial costs below are historical. Current format
is v6; optics are persisted. Calibration follows model/profile/mode catalog
(ADR-0011/0012). Memory-controller entries may retain reduced camera signal
with the proxy; “one384 MiB active buffer outside LRU” is no longer the cache
contract. Fast-path eligibility still requires recipe compatibility.
See [memory control](../architecture/controle-memoire-images.md) and
[RAW/profile contract](../camera-profile-engine.md).

## Initial implementation limits and costs

- One active camera buffer, capped at **384 MiB**, outside developed-image LRU.
  Twelve bytes/pixel, released on image change; workers can prolong lifetime.
- Fast path initially requires DCP development, inactive normalization and
  identical decode WB. Automatic WB, calibration, no DCP or unavailable/oversized
  buffer still require decode; previous rendering stays visible.
- An LRU return may lack pre-DCP data; the first uncached profile change then
  re-decodes. Later changes may reuse the buffer.
- Normalization remains limited to audited RAW/DCP pairs and LibRaw version.
  Stored enablement is not proof of application.
- Initial limit lifted9 October: v6 also freezes LCP identity/SHA-256 and optics
  switches. Changed/missing resources suspend corrections with warning. See
  [optics contract](../camera-profile-engine.md#persistance-et-commandes-optiques-9-octobre-2026).
- Hashes flag missing/changed resources. Automatic relocation searches matching
  filename/SHA-256 under executable `profiles/` or `config/`. Fallback is reported
  and does not promise original rendering. Engine updates do not emulate old algorithms.

## Interface and saving

General reset affects edits, retaining DCP, calibration and RAW WB mode.
Separate “Reset profile/calibration” selects automatically and disables
calibration without erasing edits or changing WB mode. RAW controls are disabled
on non-RAW images.

Writing remains atomic through `QSaveFile`, delayed450 ms and flushed on normal
editor closure. Closure during RAW change retains the last validated base.
Report write failures. Batch edit copying preserves destination RAW/WB choices
and annotations to avoid spreading an incompatible camera profile.

## Validation

`editorRawDevelopmentTests` covers pre-decode restoration, restart, identical
values/pixels, independent resets, changed hashes, no inheritance between
photos, coalesced navigation, DCP change without decoder calls and error rollback.
Synthetic profiles/temporary images do not alter originals.

Photographic verification remains necessary: real usability, zoom/comparison
continuity and profile-switch time across scenes. Less computation is not
itself a measured speedup factor.
