*[Français](README.md) | [English](README_en.md)*

# Canonical Test Memory — JSON

Decision confirmed on **October 9, 2026**: **versioned and timestamped JSONs are authoritative for data**, replacing the initial CSV rule. CSV and graphics are derivatives for comparisons and tables. A single evolving report per set interprets successive campaigns. Images can be deleted after analysis according to the October 10 rule: IPR exports only if negative result; IPR and Lightroom exports of the series if positive and validated. Preserve RAW and common neutrals; an ambiguous conclusion or incomplete memory blocks deletion. Log targets.

## Organization and Versions

Keep canonical JSONs in `docs/validation/raw/`. Example name, to be adapted to actual time: `mesure-lumieres-v001-20261009T160000Z.json`. `Z` indicates UTC; reports can display times in Europe/Paris. Do not move old evidence for this rule alone.

Each new campaign contains `campaign_id`, `schema_version`, `data_version`, `measured_at_utc` (ISO 8601 with timezone), software versions, analyzer version, and protocol version. An image generation is a campaign. Versions remain necessary even before GitHub deposit.

Published JSON = immutable: correction or new execution creates a new timestamped version. Indicate `supersedes_campaign_id` and reason if a version is replaced. Never overwrite old measurements or adapt them to a new conclusion. In case of divergence, the canonical JSON prevails over CSV or report; correct/regenerate these derivatives.

## Information to Preserve Before Image Deletion

- Measurements per scene, engine, feature, and value: metric, numeric value without publication rounding, unit, zone/ROI, number of retained pixels, and denominator of rates.
- Sources and neutrals: identifier, path during analysis, SHA-256 if calculated, size, dimensions, orientation, depth, ICC, development profile, WB, optics, and normalization.
- Processing: other settings, software version/build, protocol and analyzer, sampling, geometry, color space, thresholds, and formulas. Curves, points, and ROI remain structured in JSON.
- Audit: included/excluded/not measured, reasons, and missing information. Unknown information remains explicitly unknown, not assumed compliant.

Strict UTF-8 JSON: absent measurement = `null` with status/reason, never invented zero or `NaN`/`Infinity`. An uncomputed fingerprint remains absent and reported. Do not reconstruct a timestamp or unknown settings. CSV exports: UTF-8, comma separator, decimal point, source JSON identifier/version, and explicit aggregation rules.

## Closure and Version Comparison

1. Verify scene/state/neutral coverage and settings audit.
2. Publish then reread the JSON: validity, units, denominators, exclusions, missing values, provenance, and completeness.
3. Generate CSV, syntheses, and curves from **this JSON**; cite campaign/version in the single report and matrix.
4. Compare versions with compatible protocol, metrics, scenes, and settings; otherwise explicitly signal the change of conditions.
5. Report memory limits before user deletes images. Deletion authorized on October 10 remains conditioned on explicit verdict, path control, and complete memory; it excludes RAW and neutrals.

After deletion, stored metrics remain comparable and their syntheses recalculable. **New pixel metrics, new ROIs, or visual inspection require images.** Fingerprints do not reconstruct pixels. A JSON does not save images and does not prove universal quality.

## Historical Transition

JSON/TXT from `../raw/` and CSV from `../metrics/` remain preserved. Some JSONs do not yet have all version/provenance fields; IPR settings are not always traceable in TIFFs. Do not announce universal completeness or comparability. Preserve available values and report unrecoverable fields during reprocessing. This contract does not modify any old measurement and does not certify a migration already performed. This folder contains the contract, not a second copy of the data.
