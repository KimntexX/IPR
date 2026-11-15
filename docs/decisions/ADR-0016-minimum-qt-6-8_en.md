<!-- translation-source: docs/decisions/ADR-0016-minimum-qt-6-8.md; sha256: 579ea67632257309ab8f3246ed5496988f07213ea46d1ee0fc247f3b23097619 -->
*[Français](ADR-0016-minimum-qt-6-8.md) | [English](ADR-0016-minimum-qt-6-8_en.md)*

# ADR-0016 — Qt 6.8 minimum in every mode

- **Date:** 2026-10-17
- **Status:** Accepted
- **Decision makers:** user; applied on `patch_light`

## Context

The 15 October 2026 cross-platform study selected Qt 6.8 as distribution minimum
without applying it: CMake still required 6.4 and system Linux CI used Ubuntu
24.04's Qt 6.4.2. The discrepancy had a real cost on 17 October: Qt 6.4.2 mishandles
`RGBA64`→`RGBA32FPx4`, dividing color twice by alpha. A 16-bit PNG read at
alpha 10000/17000 yielded red 0.588 instead of 0.153. Qt 6.8.3 gives the exact
value. Retaining Qt 6.4 would require workarounds.

## Decision

Qt **6.8** is the build/runtime minimum for system and standalone modes:

- `find_package(Qt6 6.8 …)`; no Qt<6.8-specific path.
- Linux CI uses SDK Qt 6.8.3 in both modes. Ubuntu 24.04, limited to Qt 6.4.2,
  no longer receives a system archive; standalone remains possible.
- Build the system `.deb` on Debian 13(Qt 6.8.2); Fedora/Arch retain repositories
  already beyond6.8.
- macOS remains Homebrew(system) and official SDK 6.8.3(standalone).

## Consequences

- Benefits: one minimum version, no version workaround; named `Bt2020`
  primaries available everywhere.
- Costs/risks: no system package for Ubuntu 24.04/Debian 12; local builds there
  require a Qt≥6.8 SDK.
- Compatibility: `QDataStream::Qt_6_4` in `.ipr` sidecars(ADR-0001) is an
  unchanged file format; old sidecars remain readable.
- Required validation: full four-platform CI with Qt≥6.8, local Linux SDK 6.8.3
  build and regenerated Debian 13 packages at next delivery workflow.

## Links

- [CI/build specification](../architecture/ci-et-build-multiplateforme.md)
- Code: `CMakeLists.txt`, `cmake/Deployment.cmake`, `.github/actions/setup/action.yml`,
  `.github/workflows/{ci,delivery,linux-distributions}.yml`
- [ADR-0001](ADR-0001-sidecar-ipr_en.md), sidecar format.
