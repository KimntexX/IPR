*[Français](README.md) | [English](README_en.md)*

# Build Folder Cleanup — October 10, 2026

Operation requested by the user, limited to five explicitly verified folders under the project root. **93 files deleted, 792,331,647 bytes freed**, i.e., 792.33 MB / 755.63 MiB.

## Deleted Folders

- `build-canon-profils-tonal-20261007`: copy of 73 files identical to `build-diagnostic-profils-raw-20261007`, verified path by path by SHA-256. The descriptively named folder remains available.
- `build-generator-smoke-20261006`: old JPEG/TIFF exports from smoke test, replaced by current campaigns and generator tests. Both complete manifests are [preserved](preserved-manifests/); no original RAW data is deleted.
- `build-scholar-haar-preview` and `build-scholar-images-tp-20261008`: page previews and small demonstration files generated. Sources and Scholar PDFs remain present under `docs/scholar/`.
- `build-git-cleanup-main-20261009`: two temporary Git indexes from remote cleanup; neither main Git index, nor commits, nor branches are deleted.

## Preserved Folders

- `build-windows-msvc`, `build-wsl-libraw`, `build-wsl-nolibraw`: builds and active test environments.
- `build`: notably contains the Qt Creator build whose path is still configured in `CMakeLists.txt.user`; no global deletion.
- Other calibration/diagnostic folders: floating grids, normalization manifests, references, and inventories still useful for investigations. A `build*` name does not guarantee disposable cache; JSONs do not replace pixels needed for new metrics.

The fifteen preserved folders are verified after the operation by file count and size sum, not by full rehashing of each content. Deleted paths are bounded to a whitelist, and symlinks are rejected.

## Audit and Recovery

- [Inventory before and decisions](inventory.csv).
- [Log of 93 deletions, SHA-256, and status](deletion-log.csv).
- [Timestamped machine summary](summary.json).
- [Manifests of old exports](preserved-manifests/).
- Local script bounded to this operation: `scripts/cleanup-unused-builds-20261010.ps1` (not versioned).

Permanent deletion, without recycling bin. Deleted diagnostic profiles have an identical copy preserved; previews can be regenerated. Old smoke-test exports are not backed up by their manifests or fingerprints: they require regeneration with the old build for exact reproduction. No historical measurement rewriting occurs.
