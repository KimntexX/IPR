*[Français](README.md) | [English](README_en.md)*

# Derived Measurements: CSV and Curves

**Rule since October 9:** [versioned and timestamped canonical JSONs](../data/README.md) are authoritative for new campaign data. This folder preserves derived CSV, syntheses, and graphics. The chains below use JSON sources; their presence does not guarantee all fields of the new contract. Syntheses and curves must read canonical JSONs, directly or via their traceable CSV exports.

Documentation update of October 8, 2026. Files in this folder are reproducible from evidence in `../raw/`; these are never modified.

```sh
python3 scripts/export_documentation_metrics.py
python3 scripts/export_documentation_metrics.py --check
python3 -m unittest discover -s tests -p test_documentation_metrics.py
```

The script uses only the standard Python library. CSV files are UTF-8, comma separator, decimal point; explicitly import this format in a French spreadsheet. Large integers such as `file_mtime_ns` must remain text in a spreadsheet to avoid its precision limit. The `--check` control verifies provenance fingerprints, rereads JSON pointer values without precision loss of large integers, and parses all SVGs.

## Data and Provenance

### Camera Neutral Lights Complement — October 9

The subfolder [lumieres-camera-neutral-20261009/](lumieres-camera-neutral-20261009/) contains the synthesis of 144 variants and six response curves. Source: [audited JSON](../raw/mesure-lumieres-camera-neutral-20261009.json), [CSV per scene](../raw/mesure-lumieres-camera-neutral-20261009.csv). Independent generation from historical documentary export:

```bash
python3 scripts/summarize_lighting_validation.py docs/validation/raw/mesure-lumieres-camera-neutral-20261009.json --output docs/validation/metrics/lumieres-camera-neutral-20261009
```

Each scene has equal weight. Summary ratios are medians of ratios per scene; deltas and rates are averages. Curves show three states, not measured progressivity or CPU times. Zone quantiles are specific to each engine's neutral; no guaranteed spatial correspondence. States with warnings are excluded from synthesis. The new campaign has no XMP warnings, but IPR settings remain inferred from names, without manifest. See the Lights report for limits.

- `sources.csv`: source files, family, size, and SHA-256.
- `rapports-tableaux.csv`: numeric cells of tables published in reports (including EV and blacks), with line, section, headers, and source cell. This level of proof is distinct from raw samples.
- `base-raw-profils.csv`, `tonalite.csv`, `balance-blancs.csv`, `couleur-globale.csv`, `melangeur.csv`, `bruit.csv`, `detail.csv`: numeric values, JSON Pointer location or textual line/token, context, and source fingerprint.
- `temps-ouverture.csv`: two states of the October 1 Windows benchmark.
- `tonalite-revisions.csv`: luminance responses, engine, region, revision, and corpus.
- `reglages-reponses.csv`: named measurements of color/detail/WB campaigns.
- `melangeur-progressivite.csv`: eight bands, three components, LR/IPR, and four intermediate values on available scenes. Each graph retains a single scene per band, the one with the most pixels in the band core; no subject or neutral mixing. The CSV preserves all scenes.
- `bruit-progressivite.csv`: residue and gradient ratios of valid states from the extended v2 corpus. Curves illustrate 9950, not an average of different subjects.

The complete digital export also includes parameters and metadata: **a number in this CSV is not automatically a quality score**. Units remain those of the schema/source; no unknown unit is invented. Textual lines preserve their literal context; their tokens may include thresholds, sizes, and identifiers. Use specialized CSV files for tracing. Physical paths of some old measurements may be archived; their evidence data remains available in their JSONs.

## Available Curves and Reading

SVG graphics are in [curves/](curves/).

| Part | Graphics | What They Show / Limit |
| --- | --- | --- |
| RAW opening / profiles / copy / totals | [Opening Times](curves/temps-ouverture.svg) | Before/after historical local; no current build measurement, no published variance. |
| Shadows, whites, highlights, contrast | `tonalite-*.svg` | IPR response to extremes by revision; explicit corpus preserved, no CPU speed or "lower = better" score. |
| Temperature, tint, vibrance, saturation | `reponse-*.svg` | Response of the last available campaign, two documented states; units and conventions in the source. |
| Texture, clarity, veiling | `reponse-*.svg` | Same principle; setting response, not absolute detail quality. |
| Eight-band mixer | `melangeur-<bande>-<composante>.svg` | Progressivity -50/-25/+25/+50, measured relative to each engine's own neutral. |
| Luminance/chrominance noise | `bruit-*.svg` | Haar/MAD residue on 9950 according to the cursor; less residue may also mean texture loss. |
| DCP base | [Profile Comparison](curves/base-profils.svg) | Average ΔE00 of the last exports per scene, not temporal evolution or physical fidelity. |
| EV, blacks | CSV and campaign reports | No comparable series automatically extracted from available logs for a dedicated graph. |
| Curves, grading, sharpening, grain/blur | Recipes to be done | Insufficient photographic measurements for an evolution curve. |
| Vignettes, cache, rendering, export, batch | Benchmarks to be constituted | No invented times; prepare cold/hot repetitions and RSS peaks. |
| Masks, editable geometry, AI | Not started | No performance graphics. |

Missing data remains a gap: no zero point or neutral state result is added without measurement. Revisions are not assimilated to dates, and distinct corpora are not averaged. Historical measurements may correspond to a different pipeline; consult their campaign report.

## Next Speed Campaign

`benchmark-template.csv` prepares columns and ten steps to measure; values are intentionally empty. This file is not an executed campaign.

On an identified machine and dependency version: measure separately opening/profile search/decoding/LCP-DCP/proxy/rendering/thumbnails/export, with RAW identified by SHA-256, profile, parameters, resolution, and cache mode. Keep individual measurements, perform at least several repetitions, then publish median, dispersion, and RSS peak. Do not juxtapose times from different machines as if they proved a code gain.
