<!-- translation-source: docs/decisions/ADR-0008-separation-travail-affichage-export.md; sha256: f6de40392a7533f9fce68e92f57aebbbf5e9c5253c5b95664cc784d619e8a328 -->
*[Français](ADR-0008-separation-travail-affichage-export.md) | [English](ADR-0008-separation-travail-affichage-export_en.md)*

# ADR-0008 — Separate working space, ICC display and export

**Date:** 15 October 2026. **Status:** user-approved directive; implementation
pending. Generalized ProPhoto working space remains a candidate to evaluate,
not a decided migration.

## Context

Screen gamut must not determine RAW decoding/calibration. Changing screens
does not change photographic information to preserve. At the initial decision,
`WorkingImage` used floating linear sRGB; ProPhoto was an intermediate domain
for DCP tables/curves, not every adjustment's contract. `ColorPipeline::toSrgb()`
explicitly expected linear sRGB and provided shared preview/export conversion.

## Decision

- Separate development/edits, display conversion to screen ICC and export
  conversion to the selected output space.
- Default target: the system-associated screen ICC profile. Plan manual
  selection in future advanced settings for special cases/diagnostics.
- Screen/display-profile changes must not cause RAW decode, DCP/calibration
  change or changes to edits/exports.
- Missing usable profiles need a documented, reported fallback without
  inventing screen characterization.
- Prepare/cache display transforms; invalidate only relevant output.
  Avoid double color management among application, Qt and OS.
- Soft proofing is a distinct later feature.

## Limits and prerequisites

Specify per-OS discovery, multi-screen behavior, rendering intents, SDR/HDR
and Qt/OS responsibility. Assigning a profile does not convert pixel values.
Do not apply a screen transform to ordinary exports or store a hardware screen
profile as a portable photographic `.ipr` setting.

Study a floating wide-gamut working space, including ProPhoto D50 primaries,
to delay gamut reduction until output. Audit primary-, white-point- and
transfer-dependent operations. No screen profile replaces working space;
changing tags is not migration.

## Validation and delivered state

Test adjustment/export-byte invariance on screen changes, cache, restoration,
invalid profiles and absence of double conversion. Revalidate pipeline-sensitive
controls before wide-gamut migration. Existing JSON evidence stays historical
and unchanged.

**This decision itself changes neither rendering code nor UI.**

See [detailed architecture](../architecture/gestion-couleurs-affichage.md) and
[RAW roadmap](../architecture/roadmap-ouverture-raw.md).

**15 October study supplement, no decision change:** the
[documentary comparison, §43](../validation/analyses/analyse-base-colorimetrique.md#43-étude-préalable-prophoto-et-espaces-de-calcul-comparables--15-octobre-2026)
identifies linear Rec.2020 and ACEScg as alternatives to evaluate. ProPhoto is
not adopted by default and conditional automatic migration is not triggered.
Working/display/export separation remains accepted.

**Later15 October decision:** after numerical comparison, the user selects
linear D65 Rec.2020 for the master. See
[ADR-0009](ADR-0009-espace-travail-rec2020-lineaire_en.md). It complements this
directive without itself enabling ICC display. Subsequent local master port
is tracked in ADR-0009 and RAW/profile report§46.
