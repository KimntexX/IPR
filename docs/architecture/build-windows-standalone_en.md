<!-- translation-source: docs/architecture/build-windows-standalone.md; sha256: 22ec4fcfbf6350356df2349f584d67b187dd8079e9b5e3edd8f17781ec5b1fb5 -->
*[Français](build-windows-standalone.md) | [English](build-windows-standalone_en.md)*

# IPR — Windows x64 distributions

Reference reviewed on 1 November 2026: `dev` `2facc24`, release
`v0.1.92-alpha`. Building, startup and the Windows installation cycle are
verified by the [production CI](https://github.com/KimntexX/IPR/actions/runs/41347235082).
This does not replace manual testing on a user's machine without an SDK.

## Portable ZIP and installer

Both distributions use the same verified standalone payload:

- ZIP: extract the whole archive and run `bin/appIPR.exe`. Preferences remain
  in `bin/config/ipr-ui.ini`; the folder must be writable.
- Inno Setup: `IPR-<version>-windows-x64-setup.exe`, per-user installation under
  `%LOCALAPPDATA%/Programs/IPR`, without elevation by default. Start menu
  shortcut, optional desktop shortcut, upgrades with a constant AppId and
  Windows uninstallation.

Target: Windows 10/11 x64. Windows ARM and 32-bit Windows are not certified.
Qt/QML and third-party DLLs come from the existing packaging process.
The `system` mode is separate and is not wrapped in this installer.

## Preferences and profiles

The installer omits `bin/config/ipr-ui.ini`. Existing `UiPreferences`
behaviour then uses `QStandardPaths::AppConfigLocation/ipr-ui.ini`,
initialised with embedded defaults. The installed manifest has its own
inventory and the fingerprint of its source portable manifest.

Public calibration stays in `bin/config/raw-normalization.json`: a
distributed resource, separate from user preferences.
Personal profiles are discovered in `QStandardPaths::AppDataLocation/profiles`:
`index.json`, `dcp/`, `lcp/`. The uninstaller does not manage them.
Restart after adding profiles. Bundled profiles are under `bin/profiles/`,
with an empty public index.

Calibration is requested by default on first opening; choices saved in the
`.ipr` are restored afterwards. Actual application depends on the characterised
camera, profile and sensor mode: a checked box does not prove calibration of
an unknown camera. The current written format is 9; versions 1 through 9
remain readable and are converted, following the user's decision of
1 November 2026.

DCP/LCP loading is cross-platform: portable root, macOS bundle resources and
`share/IPR/profiles/` on Unix. Adobe discovery is not fully uniform:
automatic on Windows, a WSL case on Linux, no implicit Adobe root on macOS.
`IPR_ADOBE_CAMERARAW_DIR` works on all three operating systems.

No private DCP/LCP is included. The SHA-256 inventory is checked before
compilation; modified or extra files are refused. This does not replace a
legal audit. There is no current private Adobe-profile packaging option.

## Local build

Prepare a Release standalone build with the same version label as the package.
From the repository root, adapt the paths and version in this example:

```powershell
$env:IPR_ISCC = 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe'
./scripts/package-windows.ps1 -BuildDirectory build-ci -OutputDirectory dist `
  -Mode standalone -Version 0.1.20-alpha -QtRoot 'C:/Qt/6.8.3/msvc2022_64' `
  -RuntimeDirectory 'build-ci/vcpkg_installed/x64-windows/bin' -Installer
```

The version is illustrative, not a delivered milestone. Inno Setup 6.3+ is
required; CI installs 6.7.3. Without `-Installer`, only the ZIP is requested.
The script does not rebuild C++, refuses existing outputs and does not install
IPR on the developer's computer. Outputs remain under `dist/`, outside Git.

To compile only the setup from an already measured staging folder:

```powershell
python scripts/windows_installer.py --stage 'dist/.staging/IPR-<label>-windows-x64-standalone' `
  --version '<label>' --output-dir dist --iscc 'PATH/ISCC.exe'
```

The label must match the manifest. This script does not turn a build directory
or a system package into a standalone package.

## CI and validation limits

Patch branches verify packages without publishing downloadable binaries.
On `dev`, the delivery workflow provides artifacts of the exact validated commit.
On `main`, production CI verifies staging and setup without publishing an
archive; the explicit release workflow builds from the tested `dev` SHA
referenced by the promotion, checks both CI gates and publishes only the
verified bytes after authorisation. The GitHub `release` environment must
be configured with the intended approval protections.

This release's policy permits Windows system/standalone and Ubuntu standalone.
No macOS package is published; both macOS system architectures remain compiled
in CI ([#20](https://github.com/KimntexX/IPR/issues/20)).

The disposable hosted Windows GitHub runner tests silent installation,
reinstallation of the same package, QML startup outside the development
environment and uninstallation. An unowned file must survive. An existing
IPR installation blocks this test; it is not run automatically locally.

Manual checks still needed on a user machine without an SDK: standard account,
paths with spaces/accents, personal preferences/profiles, upgrades between
different versions and real RAW opening. Photos and sidecars must remain
unchanged. Distribution signing is not configured; a Windows warning may
remain, without justifying disabling security.

## References

- [Inno: non-administrative mode](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm).
- [Inno: AppId identity](https://jrsoftware.org/ishelp/topic_setup_appid.htm).
- [Inno: official releases](https://jrsoftware.org/isdl.php).
- [Qt: application locations](https://doc.qt.io/qt-6/qstandardpaths.html).
