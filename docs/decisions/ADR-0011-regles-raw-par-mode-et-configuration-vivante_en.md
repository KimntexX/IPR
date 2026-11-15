<!-- translation-source: docs/decisions/ADR-0011-regles-raw-par-mode-et-configuration-vivante.md; sha256: 20e325bfc78c22beedceb77b5f4f366d223491695b8ae726d9ec9efb8ae71611 -->
*[Français](ADR-0011-regles-raw-par-mode-et-configuration-vivante.md) | [English](ADR-0011-regles-raw-par-mode-et-configuration-vivante_en.md)*

# ADR-0011 — Sensor-mode RAW rules and live configuration

**Date:** 16 October 2026. **Status:** user-approved, locally implemented.

## Decision and scope

The user visually accepts R6/R7 rendering and requests other photos, including
SD-card files, without corpus registration or manifest reselection after updates.
This trial expansion does not validate every market ISO/mode.

Use **schema 3 only**. Rules bind make/model, DCP SHA-256 and sensor-level mode,
without RAW hashes or ISO lists. Blacks are read/estimated per image, never
replaced with a corpus photo's blacks.

| Model/mode | Native plausibility bounds | Target white | Source baseline | Profile compensation |
| --- | --- | ---: | ---: | ---: |
| R6 Mark III, low black | Total per-channel black 400–640; native white 13000–17000 | 13995 | +0.26 EV | 0 |
| R6 Mark III, high black | Total per-channel black 1600–2400; native white 13000–17000 | 14351 | +0.28 EV | 0 |
| R7 | Total per-channel black 0–4096; native white 13000–17000 | 16384 | +0.35 EV | −0.02 EV |

These are prototype plausibility windows around observed modes, **not
manufacturer specifications** or black estimates. Check native white before
changing it, avoiding application at another scale such as unscaled 12-bit.
Requirements remain Canon CR3, camera WB, no native baseline already applied,
no unsupported spatial black pattern and compatible LibRaw version.
Different/changed DCP, model or unknown mode: no borrowed neighboring rule;
not-applied status and ordinary rendering remain available. Refuse ambiguous/
invalid rules. Other ISO may be tried in recognized modes, including R7 correction.

## Automatic behavior and restoration

Request calibration by default on first opening. Explicitly disabled existing
recipes remain disabled; enabled ones remain enabled. Dedicated profile/
calibration reset retains explicit disablement; the advanced panel was still
to be designed at this decision.

The selected manifest is **live configuration**: automatically track current
content whether embedded, deployed beside the app or user-selected.
Parse/validate before application. Report digest changes; new digest enters
cache and saved recipe. Do not reuse old rendering. Missing/invalid manifests
are not silently replaced with another.

DCP/LCP identities remain separately verified: following configuration permits
neither incompatible profiles nor silent profile-byte replacement. This fixes
the report's §50 0081 scenario, where an old manifest hash indirectly disabled
R6 calibration after an R7 rule was added.

## Implementation, no legacy and validation

`RawNormalization::resolve()` no longer reads/hashes RAW; decoded metadata
and DCP identity suffice. Two R6 modes use actual blacks; R7 coefficient does
not pass to R6. `ImageEditor::beginLoad()` follows manifest changes before
cache lookup/decode without erasing edits/annotations.

Replace schema-2 parser and whitelist tests, without legacy mode. Historical
analysis JSON stays immutable. Preparation tools propose explicit mode rules,
not new photo lists. Replace external schema-2 manifests with current ones;
automatically following bytes does not convert old data formats.

Tests: unlisted photos/names/ISO, R6 modes, R7→R6 isolation, incompatible scale,
modified DCP refusal, automatic update after old-hash restoration, cache and
edit preservation. The 19-source corpus must retain step-8 accepted controls;
the user observes new SD photos. Integration needs no card access/deletion/copy.

## 20 October 2026 extension: EOS 60D(CR2)

The user adds 60D to end-of-alpha targets. LibRaw reports the same black(about
2048) and white(12279) across ISO modes; Adobe reads each file's Canon specular
white, capped at 15000, and compensates exposure(net −0.34 EV across modes).
Rules may therefore bound specular white with optional `specular_white_min`/`max`.
Four Adobe-derived 60D rules follow, evidence `audit-niveaux-eos60d-v001`:

| 60D mode | Specular white | Target white | Source baseline |
| --- | --- | ---: | ---: |
| ISO multiples of 160(observed 160/1250) | 11100–11400 | 11222 | −0.50 EV |
| ISO 100, specular 12279 | 12200–12400 | 12279 | −0.34 EV |
| ISO 100, specular 12749 | 12600–12900 | 12749 | −0.28 EV |
| Observed ISO 400–6400 | 15000–15800 | 15000 | 0 |

Black 1900–2200, native white 12000–12600, 60D Camera Neutral DCP.
Unobserved specular white matches no rule(ordinary rendering). Mean IPR/LR
neutral difference on 21 CR2: 4.8→0.9 L\*(`ecart-neutre-60d-v001`);
CR3 unchanged. User visual validation remains required.

## References

- [Single RAW/profile report](../validation/analyses/analyse-base-colorimetrique.md)
- [Rendering path](../architecture/chemin-ouverture-raw.md)
- [ADR-0007](ADR-0007-calibration-raw-par-defaut_en.md), [ADR-0010](ADR-0010-compensation-rendu-par-profil-dcp_en.md).
