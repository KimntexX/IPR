<!-- translation-source: docs/architecture/README.md; sha256: 03430aedbf1f92bf62352c4a683f5ebccd1072d27af8a6e32a18d9a7598e6bec -->
*[Français](README.md) | [English](README_en.md)*

# Architecture

This area contains reference documents that describe components, data contracts, and rendering flows, without campaign results or session logs.

## Current References

- [LaTeX Technical Manual](../technical/latex/README.md): user diagrams, classes, and sequences, pipeline, and glossary.
- [Rendering engine and working image](../imageeditor-render-working-image.md): `WorkingImage` contract, order of operations, and colorimetric invariants.
- [Camera Profile Engine](../camera-profile-engine.md): DCP/LCP, local selection, and limits.
- [RAW → DCP rendering path](chemin-ouverture-raw.md): current code, calibration refusals and target architecture without photo lists.
- [Extensible formats and characterization storage](../decisions/ADR-0012-formats-extensibles-et-caracterisations_en.md): future adapters, DCP/JSON/SQLite, first package and remaining transition.
- [RAW opening roadmap](roadmap-ouverture-raw.md): highlights, camera contexts, caches and planned R5/6D Mark II corpus.
- [RAW decoder roadmap](roadmap-decodeurs-raw.md): LibRaw separation, Canon qualification and automated camera integration.
- [OpenCV-independent image layer](roadmap-couche-image-sans-opencv.md): measured use, stages and removal of unused `imgcodecs`.
- [Light roadmap](roadmap-lumieres.md): six controls, integrated plan B, Rec.2020 campaigns and 21 October highlight reservation.
- [Lightroom feature inventory](roadmap-fonctions-lightroom.md): coverage, implementation status and planning.
- [Editing foundation of the beta, B0](socle-edition-b0.md), in French: inventory, decisions, task runner, CBOR recipe, history, local value maps and B0.4 report.
- [Beta action plan](roadmap-beta_en.md): geometry, layers/filters, removal and local noise/sharpening/shapes/objects networks; not implemented.
- [Working, display ICC and export](gestion-couleurs-affichage.md): target separation, distinct from current code.
- [Linear Rec.2020 integration](integration-rec2020.md): master port, initial audit and remaining qualification.
- [RAW Restoration and Profile Adjustment](../decisions/ADR-0006-restauration-raw-et-retouche-profil_en.md): RAW choices introduced in v5, retained with optics in v6; pre-DCP buffer and distinct resets.
- [Detailed CPU Engine Guide](../scholar/moteur-graphique-ipr.tex): explanation of constants, asynchrony, and sidecars.
- [Architecture and Profile References](references/): dependency licenses, DCP creation, and Adobe profile usage note.
- [Camera-profile autonomy study](references/etude-autonomie-profils-camera.md): DCP data/rights, external corpora and proposed MCP analysis bridge.
- [CI and Multiplatform Build](ci-et-build-multiplateforme.md): Windows/macOS/Linux matrix, prerequisites, packaging, and GitHub roadmap.
- [Denoising and Sharpening](denoising-sharpening_en.md): algorithms, pipeline order, AI/GPU limits, and expected validation.
- [Neural B4 preparation](restauration-neuronale_en.md): local models, RGB/RAW contracts, host inventory and proposed bench; no qualified inference.
- [Stacking and panorama](empilement-et-panorama.md): multi-image plan, user anchor, sky/ground alignment, automatic crop and disk scratch directory; not implemented.
- [Source Tree](../../src/README.md): C++/QML domains and internal `iprEngine` library.
- [UI Preferences](ui-preferences.md): user INI, geometry, sections, colors, and extensible schema.
- [Standalone Windows Build](build-windows-standalone.md): portable configuration, dependencies, and restrictions on third-party profiles.
- [Measurements and Curves](../validation/metrics/README.md): CSV provenance and comparison limits.

[Common format import](import-images_en.md): RAW/raster adapters, Rec.2020
contract, metadata, capabilities and cache/profile validation.

[Prepared profile cache](../decisions/ADR-0014-cache-profils-prepares_en.md):
shared DCP/LCP LRU, 64 MiB/16 entries, concurrency and content invalidation.

## Update Rule

[UI connection follow-up](suivi-raccordements-ihm.md): isolated `patch_ui`
workstream, engine interface requests and connection validation.

[Memory control/navigation](controle-memoire-images_en.md): LRU ownership,
reservations and cancelable prefetch, with global-memory-threshold limits.

A modification of the responsibilities of `ImageEditor`, `ColorPipeline`, `RawDecoder`, `CameraProfileEngine`, cache, or sidecar must update the LaTeX manual and, if the choice is durable, create or modify an ADR.
