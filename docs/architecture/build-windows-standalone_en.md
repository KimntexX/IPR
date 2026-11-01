# IPR — Standalone Windows x64 Version

## Usage

Extract the entire archive into a writable folder, then run `appIPR.exe`. Do not move the executable alone. Qt, OpenCV, and LibRaw do not need to be installed separately: their dependencies are included. The target is Windows 10/11 x64; current trials do not replace testing on a clean machine and every Windows version.

- `config/ipr-ui.ini`: portable UI state, read and updated in this folder.
- `config/raw-normalization.json`: experimental calibration, disabled by default.
- `profiles/dcp` and `profiles/lcp`: local profiles; restart after addition.
- `profiles/index.json`: optional explicit selection by camera/lens.
- `licenses`: texts and notices of dependencies; complete audit before publication.
- `package-manifest.json`: inventory and SHA-256 of files at packaging time.

To reproduce the tested render, open a RAW, select **Camera Neutral** in "RAW Development / Profile", disable auto WB, and enable normalization only for audited RAWs. The manifest is detected in `config`. Check the application status. The profile and RAW activation remain session choices, independent of UI persistence. The current calibration concerns only five exact files: 0007, 0043, 0327, 9421, and 9950 from the exact corpus (SHA-256 fingerprints), with LibRaw 0.22.2-Release. No other RAW is automatically calibrated. Camera Neutral can however be selected for other compatible RAWs, without this specific calibration.

## Profiles and Distribution

The script includes no Adobe profiles by default. The explicit option `-IncludeLocalAdobeProfiles` produces a **local private test copy** with Camera Neutral for Canon EOS R6 Mark III and installed Canon LCPs. This copy must not be published, committed, or redistributed: no redistribution rights for these profiles have been established. See `licenses/ADOBE-PROFILES-NOTICE.md`. The rights of all dependencies must also be verified before public distribution.

## Reproduction from the Repository

Compile the Release version, then from PowerShell at the root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-windows.ps1
# Local test copy only, with installed Adobe profiles:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-windows.ps1 -Destination dist/IPR-Windows-x64-local -IncludeLocalAdobeProfiles
```

The script refuses any existing destination and deletes nothing. Its Qt/OpenCV/vcpkg paths are configurable. Generated folders and `dist/` archives must not enter the repository. They do not contain photographs.

Visual agreement on Camera Neutral has been accepted by the user; exact Lightroom parity and physical colorimetric fidelity are not validated. Adobe Standard rendering remains to be processed, and the JPEG 9933 test still fails.
