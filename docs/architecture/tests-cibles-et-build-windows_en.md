# Windows Build and Test Selection

## Daily Command

From the project root, in **WSL or Git Bash**:

```bash
bash scripts/build-windows.sh
```

The Bash launcher uses Python 3.9+ to select tests, then PowerShell and **Windows** CMake/MSVC tools to compile. A launch from WSL produces `appIPR.exe` and `iprGenerateTests.exe` for Windows, not Linux binaries. By default: `build-windows-msvc`, `Release`, two jobs. Close the executable in this folder before linking it; the script refuses to stop it automatically.

Prerequisites: MSVC/Visual Studio Build Tools 2022 x64, CMake and CTest for Windows, Qt MSVC, OpenCV/LibTIFF/LibRaw of the same ABI. The existing CMake cache is reused. On a new machine, provide dependency paths, for example:

```bash
bash scripts/build-windows.sh \
  --cmake-arg=-DCMAKE_PREFIX_PATH=C:/Qt/VERSION/msvc2022_64 \
  --cmake-arg=-DOpenCV_DIR=C:/opencv/build/x64/vc16/lib
```

Similarly add vcpkg toolchain or required LibRaw/LibTIFF paths. These examples do not constitute automatic dependency installation. Python tests of the comparator require NumPy and Pillow in the launcher's Python; the selector itself only uses the standard library.

The script adjusts Qt plugins/PATH of its child processes only for tests. It does not modify installation or user IPR preferences. It does not perform DLL deployment/packaging for a standalone distribution.

## Test Policy

Versioned rules are in [scripts/test-rules.json](../../scripts/test-rules.json). The fourteen synthetic CTest tests are now recorded on Windows as well as Linux/WSL. Private photographic probes remain separate.

The selector compares SHA-256 of code/configuration files to the last successful validation, stored in `build-windows-msvc/.ipr-tested-snapshot.json`. It includes files not tracked by Git and detects additions, deletions, and renames. Photos, archives, documents, and generated outputs are not code and do not trigger CTest. This choice avoids rerunning the entire suite for each build of an already dirty worktree, without requiring a prior commit.

| Modified Zone | Test Examples |
| --- | --- |
| Preferences/INI | C++ preferences, QML restoration, settings panel |
| Color Mixer | math, neutrals, QML swatches, export, Python comparator |
| Image Generator (`src/tools/`) | parameter series, isolation, manifest, preserved sources, 16-bit export |
| RAW Decoding | decoder, black level, profiles, export |
| Local Profiles | DCP/LCP engine, decoder, export |
| QML Settings | width constraints, controls, global restoration |
| QML Preview Zoom | click, drag, wheel, shortcuts, bounds, refresh, and window loading |
| Isolated test file | this test only, not the entire suite |

A full validation is enforced in the following cases:

- First pass or invalid checkpoint/configuration: baseline establishment;
- `--full-tests` request or `GITHUB_ACTIONS=true` environment;
- CMake/toolchain/workflow/test/infrastructure modification or cross `imageeditor.h` interface;
- At least twenty code/configuration files modified;
- Unknown code file in rules: conservative fallback, to be completed later.

Test dependencies are a maintained mapping, not a formal proof of the C++/QML graph. Existing detail/noise tests, cache, and thumbnails do not yet constitute complete dedicated coverage: export/UI provides regression control, and photographic recipe remains necessary.

The checkpoint only advances after compilation and success of all selected tests. An error, a `--no-tests` build, or changed sources during execution never mark the code as tested. A manual `--paths` selection does not advance the global checkpoint either, unless a full suite is actually executed. The first full validation does not repeat on every launch.

## Useful Options

```bash
# Display planned tests without compiling, writing checkpoint, or executing.
bash scripts/build-windows.sh --dry-run

# Test a specific area (paths relative to the repository).
bash scripts/build-windows.sh --paths src/app/UiPreferences.cpp

# Leave IPR open: only compile selected test targets.
bash scripts/build-windows.sh --tests-only --paths src/qml/Setting.qml

# Large modification: entire suite.
bash scripts/build-windows.sh --full-tests

# Compilation only, without declaring code validated.
bash scripts/build-windows.sh --no-tests
```

After success, the script also preserves the plan in `.ipr-test-plan.json`. CTest verifies that each requested test is actually recorded; a missing or failed test results in a non-zero exit code. Release format is the locally verified path; Debug requires corresponding dependencies.

## GitHub

The same launcher forces the full suite if `GITHUB_ACTIONS=true`, even with `--paths`, and refuses `--no-tests` in this context. A future Windows job must first install/configure dependencies, then run the script. Hosted workflows and packaging remain to be provisioned according to [the multiplatform plan](ci-et-build-multiplateforme.md): no incomplete workflows or dependencies on personal paths are added here.

Linux CI compilations can use `cmake --build` then `ctest --test-dir BUILD --output-on-failure` directly for the entire registered list. Labels `unit`, `pipeline`, `export`, `color-mixer`, `preferences`, `raw`, `profiles`, `settings`, `qml`, `viewport`, `detail`, `noise` are available to diagnose a family. Complete CI validation must not become a selective execution.

## Local Verification — October 4, 2026

- First checkpoint: successful Windows Release build, eleven native CTest tests and thirteen Python tests (six selector, seven historical comparator).
- QML control width constraints corrected after detection by native tests; no width assertions removed.
- After extending the only comparator to twelve states: automatic selection of its single Python suite, **thirteen tests passed** after adding the twelve-state non-regression case, no CTest rerun; application build and new checkpoint successful.
- After success, `--dry-run` without changes returns empty lists.

This verification confirms the launcher and selection on the local configuration; it does not replace a test on a clean machine installation.

### Noise Analysis Extension — October 4, 2026

The Python suite `noise_analysis` covers `tests/analyse_noise.py` and its tests. The shared reader `tests/analyse_color_mixer.py` now selects both analysis suites (mixer and noise). The mixer test file alone does not trigger the noise suite. Seven selector tests, thirteen shared comparator tests, and fifteen noise tests succeed in this campaign. An explicit plan for the noise analyzer only selects `noise_analysis`.

Rule file modification remains a cross change: the next automatic build must run the full suite. Targeted analysis tests do not record a new checkpoint and do not constitute new complete validation of the Windows executable.

### Noise Fix — October 5, 2026 Consolidation

`detailProcessingTests` is recorded on Windows and Linux, labels `unit;pipeline;detail;noise`. A change in `DetailProcessing.*` selects this target, `colorPipelineTests`, `imageExportTests`, and the Python `noise_analysis` suite. The single detail test selects its own target.

After CMake/mapping modification, Windows Release build and validation **fully succeeded: 12 CTest, 36 Python tests** (13 comparator, 15 noise, 8 selector). Checkpoint updated after success. The export fixture contains deterministic noise to effectively test both settings; no effect criterion on export removed. Photographic recipe of the new version still awaited; this checkpoint does not validate its perceptual quality.

### Preview Zoom — October 5, 2026

The new target `imageViewportQmlTests` is recorded on Windows and Linux/WSL, labels `unit;viewport;qml`. The `ImageViewport.qml` component selects this target and `uiPreferencesQmlTests`; its single test file selects only its target. `Main.qml` also includes this regression.

After CMake integration and rules update: **Windows Release build, 13/13 CTest and 39 Python tests passed** (13 comparator, 17 noise, 9 selector). Checkpoint updated after success. Both targeted QML tests also succeed under WSL. This validation covers interaction and regressions; zoom does not add full-resolution RAW rendering.

### Original/Current Comparison — October 5, 2026

The same QML targets now cover the movable separation, Ctrl+Shift+C, common zoom, and opaque/translucent source captures. The `imageExportTests` target covers the origin provider and restored settings. `imageeditor.h` interface modification: full Windows suite **13 CTest / 39 Python passed**. After the single QML clip reinforcement, the launcher selects only `imageViewportQmlTests` and `uiPreferencesQmlTests`: **2/2 passed**, build and updated checkpoint.
