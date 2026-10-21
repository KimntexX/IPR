*[Français](README.md) | [English](README_en.md)*

# Validation

To perform campaigns independently: [autonomous testing procedure](procedure-tests-autonomes.md) (commands, file preparation, measurements, and validation decision).

Validation strictly separates:

- `protocols/` : procedure to reproduce before a campaign;
- `analyses/` : interpreted conclusions;
- `raw/` : canonical versioned and timestamped JSONs that are authoritative, and old raw outputs that are not rewritten;
- [data/](data/README.md): contract for preserving JSON memory and its derivatives;
- [metrics/](metrics/README.md): derived CSV and curves with provenance;
- [matrice-validation.md](matrice-validation.md): unique status of features.

To produce IPR variants without repeated UI manipulation: [test image generator](tools/generateur-images-tests.md), executable `iprGenerateTests` and Windows launcher `scripts/generate-test-images.ps1`.

To measure according to the same groups: [unified native analyzers](tools/analyseurs-groupes.md), `tests/analyse_groupes.py` or a dedicated entry per group, explicit manifests/mappings, and immutable JSON. Historical diagnostics remain separate.

Maintain only one report per feature set according to the [October 9 rule](data/README.md). Example: the [evolving colorimetric base report](analyses/analyse-base-colorimetrique.md). Successive campaigns remain distinguished there, with canonical versioned and timestamped JSONs in `raw/`. Historical evidence remains preserved there.

Historical families were migrated on October 3, 2026; their correspondence is recorded in [the migration registry](../reports/migration-registry.md). Future campaigns should create their files directly in this tree.

Since October 8, mixer and noise campaigns have been grouped in their respective evolving reports. Old names remain as references, without content duplication. Session logs and raw measurements are not merged with current conclusions.
