# CI and Multiplatform Builds

**Status:** Historical proposal; CI/CD is now implemented. See the [current implementation](ci-et-build-multiplateforme.md). This document's proposal tables have not been brought up to date.
**Scope:** Windows 10/11, macOS, Debian/Ubuntu, Fedora, and Arch Linux.

## Objective

CI (*continuous integration*) automatically executes checks on each proposed change: CMake configuration, compilation, tests, documentation analysis, and preservation of useful logs. It does not publish software by default.

CD (*continuous delivery*) is the second step: for a validated version tag, it produces installable packages, their checksum, and later their signature. This separation avoids confusing a test binary with a user-destined publication.

## Current State and Gaps to Address

**October 8, 2026 update:** Local Windows packaging now exists with Qt deployment, third-party DLLs, profiles/configuration, and SHA-256 inventory. Its offscreen startup without a development PATH is verified. This provides neither installer, nor clean machine certification, nor active GitHub workflow. The counters and proposals below describe the initial plan; consult the [current matrix](../validation/matrice-validation.md) and the [standalone ZIP contract](build-windows-standalone.md) for the delivered state.

The project is already portable in principle: CMake 3.21+, C++17, Qt 6.4+, OpenCV, LibTIFF, and optional LibRaw. However, the following gaps must be resolved before declaring multiplatform support:

| Subject | Current State | Required Work |
| --- | --- | --- |
| Tests | Eleven synthetic targets are recorded on Windows and UNIX with labels and headless Qt environment | Also verify macOS; private corpus probes remain UNIX and outside the public CI base. |
| Qt Contracts | Core/Gui/Quick/Concurrent are explicitly requested | Verify future presets and their dependency versions. |
| Reproducible Dependencies | Paths and package managers are not yet centralized | Add `CMakePresets.json`, a vcpkg manifest for Windows, and versioned Linux/macOS bootstrap scripts. |
| Installation | `install()` installs the executable but not the Qt runtime or third-party libraries | Create a *staging* phase with `cmake --install`, then a platform-specific deployment script. |
| Packages | No package generator or distribution metadata | Add CPack and/or native recipes after choosing license, publisher name, icon, and final identifier. |
| Private Data | RAW corpus and third-party profiles must not be published | CI tests only with synthetic resources, authorized profiles, and explicitly licensed public corpus. |

## What CI Will Do

### Local Policy Already Implemented

[The Windows Bash launcher](tests-cibles-et-build-windows.md) compiles via native tools and applies zone-based selection from a validated code checkpoint. Ordinary builds only run concerned tests. First pass, infrastructure/shared interface, large modification, or unknown zone: full suite. `GITHUB_ACTIONS=true` and `--full-tests` also impose the entire list. This preparation does not yet provision GitHub runners or dependencies.

### 1. Quick Checks on Each Pull Request

1. Format and consistency check for CMake/QML/C++; `git diff --check`.
2. Control of local Markdown links and documentation compilation.
3. CMake configuration, compilation, and CTest of the base without LibRaw on Windows, macOS, and Ubuntu.
4. Preservation as artifact of CMake/CTest logs and, in case of failure, useful reports.

This first barrier is short and does not manipulate private photo corpus, non-redistributable DCP/LCP profiles, or publication secrets.

### 2. Native Integration

A second matrix validates RAW import with LibRaw and TIFF/DNG exports:

| Target | Execution | Goal |
| --- | --- | --- |
| Ubuntu x86_64 | GitHub runner | Compilation and LibRaw tests on the reference Linux platform |
| Windows x86_64 | GitHub runner | MSVC compilation and headless Windows tests |
| macOS x86_64 then arm64 | GitHub runner if available for the repository | Compilation, bundle, and headless startup test |
| Debian/Ubuntu | Debian stable and Ubuntu LTS container | Verify DEB packages and system ABI |
| Fedora | Current Fedora container | Verify RPM and RPM dependencies |
| Arch | Arch Linux container | Verify `PKGBUILD` and rolling release dependencies |

Containers validate Linux package compatibility; they do not replace a graphical interface recipe on a real user machine. GitHub provides hosted Linux, Windows, and macOS runners, but their images evolve weekly: CI will therefore install requested versions rather than implicitly depending on preinstalled software.

### 3. Tag-Controlled Publication

On a signed `vX.Y.Z` tag, after passing the entire matrix:

- Generate packages and archives;
- Run a startup test of the deployed directory;
- Produce SHA-256, component manifest, and release notes;
- Publish only after manual validation of the workflow;
- Sign and notarize only when certificates and publication policy are defined.

Signing secrets must never be accessible to an external pull request.

## Recommended Build Matrix

| Family | Starting Compiler and Architecture | Target Distribution | Initial Delivery Format |
| --- | --- | --- | --- |
| Windows 10/11 | MSVC 2022, x86_64 | Windows 10 22H2 and Windows 11 supported by Qt | Deployed ZIP, then MSIX or NSIS/WiX installer |
| macOS | Apple Clang, arm64 and x86_64 | macOS supported by the retained Qt version | Compressed `.app`; DMG after signing/notarization |
| Debian/Ubuntu | GCC, x86_64 | Debian stable, Ubuntu LTS | `.deb` and `tar.zst` archive |
| Fedora | GCC, x86_64 | Current stable Fedora | `.rpm` and `tar.zst` archive |
| Arch | GCC, x86_64 | Current Arch | `PKGBUILD` and `tar.zst` archive |

The first objective is x86_64 on all platforms. macOS arm64 must be a separate native build; a `universal2` binary will only be delivered after validation of both architectures. ARM64 Windows and Linux can be added later as experimental targets, without delaying x86_64 versions.

## Compilation Prerequisites

| Platform | Common Tools | Qt | IPR Libraries |
| --- | --- | --- | --- |
| Windows | CMake >= 3.21, Ninja, MSVC Build Tools 2022, vcpkg | Qt 6 MSVC x64, same toolchain as dependencies | OpenCV, LibTIFF, LibRaw via vcpkg or consistent MSVC builds |
| macOS | CMake, Ninja, Xcode Command Line Tools, Homebrew or official Qt manager | Qt 6 for the targeted architecture | OpenCV, libtiff, LibRaw; beware of Homebrew arm64/x86_64 prefixes |
| Debian/Ubuntu | CMake, Ninja, GCC/G++, `pkg-config` | `qt6-base-dev`, `qt6-declarative-dev` | `libopencv-dev`, `libtiff-dev`, `libraw-dev` |
| Fedora | CMake, Ninja, GCC/G++, `pkgconf-pkg-config` | `qt6-qtbase-devel`, `qt6-qtdeclarative-devel` | `opencv-devel`, `libtiff-devel`, `LibRaw-devel` |
| Arch | CMake, Ninja, `base-devel`, `pkgconf` | `qt6-base`, `qt6-declarative` | `opencv`, `libtiff`, `libraw` |

Package names are the usual names to verify in the target image during implementation. Bootstrap scripts must fail clearly if a package or version is missing, rather than continuing with an incomplete set.

## Deployment and Packaging

### Windows

Compile with a single ABI, recommended here: **MSVC x64**. Never mix Qt MinGW and MSVC OpenCV/LibRaw libraries. After `cmake --install`, run `windeployqt --qmldir <qml-root>` on `appIPR.exe`, then add OpenCV, LibRaw, and LibTIFF DLLs not covered by the Qt tool. The official Microsoft Visual C++ redistributable is required for MSVC distribution.

The delivery test runs from a clean directory, without Qt in the `PATH`, and verifies at least the application startup and QML loading. The final package must also specify x64 architecture and version.

### macOS

Produce a `.app` bundle with `macdeployqt` or Qt's CMake deployment mechanism, then verify dependencies with `otool -L` and run the application from the bundle. Distribution outside the Mac App Store then requires an Apple account, a Developer ID signing identity, timestamping, and notarization; these are publication prerequisites, not ordinary CI.

### Linux

Do not impose a single portable binary for all distributions on first delivery. Deliver native formats first: DEB for Debian/Ubuntu, RPM for Fedora, and `PKGBUILD` recipe for Arch. Each recipe declares system dependencies Qt/OpenCV/LibTIFF/LibRaw and places application data in standard locations. An AppImage can be evaluated later, after license and embedded library audit.

CPack is a good common base: it builds its packages from what is declared by `install()`. Therefore, the installation and deployment phase must be completed before enabling CPack generators.

## Proposed GitHub Structure

```text
.github/
  workflows/
    ci.yml                 # format, links, multiplatform build + CTest
    docs.yml               # LaTeX/Scholar, PDF artifact
    release.yml            # tags only, controlled packages and publication
    codeql.yml             # to enable after base CI
cmake/
  packaging/               # CPack, staging scripts, and metadata
scripts/
  bootstrap/               # win, macOS, Debian, Fedora, Arch
  deploy/                  # win, macOS, Linux
CMakePresets.json
vcpkg.json
```

Third-party actions will be pinned to a reviewed SHA commit; jobs will use `permissions: contents: read` by default, and the cache will only cover dependency downloads. Build directories will not be reused as a source of truth cache.

## Implementation Sequence

1. **Local CMake Portability**: presets, explicit Qt modules, tests without `UNIX` guard, headless execution, and synthetic corpus.
2. **Bootstrap Scripts**: documented command per platform and reproducible local build.
3. **GitHub CI Level 1**: Ubuntu, Windows, and macOS; compilation, CTest, links, and documentation; no published packages.
4. **Packaging**: complete CMake installation, Qt deployment, DEB/RPM/PKGBUILD recipes, and test artifacts.
5. **Release**: versioning, IPR license, application identifiers, Windows/macOS signing, Linux repository keys, and retention policy.

## Blocking Conditions Before Public Distribution

- Choose and add a license for IPR code.
- Define publisher, reverse domain/application identifier, and final icons.
- Confirm distribution policy for DLLs and libraries, especially OpenCV, LibRaw, and Qt.
- Do not include Adobe profiles, private RAW files, or test exports in the repository or artifacts.
- Define who owns signing certificates and who can trigger a release.

## References

- [GitHub Actions — Build and Test C++](https://docs.github.com/en/actions/use-cases-and-examples/building-and-testing/building-and-testing-cpp)
- [GitHub Actions — Workflow Syntax](https://docs.github.com/en/actions/writing-workflows/workflow-syntax-for-github-actions)
- [GitHub Actions — Hosted Runners](https://docs.github.com/en/actions/concepts/runners/github-hosted-runners)
- [GitHub Actions — Dependency Caching](https://docs.github.com/en/actions/concepts/workflows-and-actions/dependency-caching)
- [Qt — Windows Deployment](https://doc.qt.io/qt-6/windows-deployment.html)
- [Qt — macOS Deployment](https://doc.qt.io/qt-6/macos-deployment.html)
- [Qt — Linux Deployment](https://doc.qt.io/qt-6/linux-deployment.html)
- [Qt — Deployment with CMake](https://doc.qt.io/qt-6/cmake-deployment.html)
- [CMake — CPack](https://cmake.org/cmake/help/latest/module/CPack.html)
