*[Français](README.md) | [English](README_en.md)*

# Architecture

This area contains reference documents that describe components, data contracts, and rendering flows, without campaign results or session logs.

## Current References

- [LaTeX Technical Manual](../technical/latex/README.md): user diagrams, classes, and sequences, pipeline, and glossary.
- [Rendering engine and working image](../imageeditor-render-working-image.md): `WorkingImage` contract, order of operations, and colorimetric invariants.
- [Camera Profile Engine](../camera-profile-engine.md): DCP/LCP, local selection, and limits.
- [RAW Restoration and Profile Adjustment](../decisions/ADR-0006-restauration-raw-et-retouche-profil.md): v5 sidecar, pre-DCP stamp, and distinct resets.
- [Detailed CPU Engine Guide](../scholar/moteur-graphique-ipr.tex): explanation of constants, asynchrony, and sidecars.
- [Architecture and Profile References](references/): dependency licenses, DCP creation, and Adobe profile usage note.
- [CI and Multiplatform Build](ci-et-build-multiplateforme.md): Windows/macOS/Linux matrix, prerequisites, packaging, and GitHub roadmap.
- [Denoising and Sharpening](denoising-sharpening.md): algorithms, pipeline order, AI/GPU limits, and expected validation.
- [Source Tree](../../src/README.md): C++/QML domains and internal `iprEngine` library.
- [UI Preferences](ui-preferences.md): user INI, geometry, sections, colors, and extensible schema.
- [Standalone Windows Build](build-windows-standalone.md): portable configuration, dependencies, and restrictions on third-party profiles.
- [Measurements and Curves](../validation/metrics/README.md): CSV provenance and comparison limits.

## Update Rule

A modification of the responsibilities of `ImageEditor`, `ColorPipeline`, `RawDecoder`, `CameraProfileEngine`, cache, or sidecar must update the LaTeX manual and, if the choice is durable, create or modify an ADR.
