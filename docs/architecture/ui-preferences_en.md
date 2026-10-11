# Global UI Preferences

Implementation: `src/app/UiPreferences.{h,cpp}`, `Main.qml`, `Setting.qml`, and `SettingsSection.qml`. Current schema: **2**, updated October 4, 2026.

## Two Distinct Persistences

The **`ipr-ui.ini`** file belongs to the application user: it preserves the UI presentation, independent of the opened photo. Sidecars **`.ipr`** remain responsible for photographic development, curves, and per-image annotations. An open section or selected HSL band is not a photographic setting and does not trigger a render.

INI is chosen for its readability, named groups, and Qt integration. JSON would be relevant for nested structures or complex lists; an SQLite database would suit a future catalog, not this UI state. The choice is recorded in [ADR-0004](../decisions/ADR-0004-preferences-ihm-ini.md).

## Location and Usage

If `config/ipr-ui.ini` exists next to the executable, the application uses this local file: this is portable mode, which requires a writable folder. Otherwise, the path is calculated by `QStandardPaths::AppConfigLocation`, with organization and application names `IPR`. It is neither relative to the working directory nor placed next to photos. Qt determines the directory according to the system; **View → Preferences File...** displays the exact path. The file is created at first launch of this version, if the user directory is accessible. [QStandardPaths Documentation](https://doc.qt.io/qt-6/qstandardpaths.html).

The versioned template [config/ipr-ui.ini](../../config/ipr-ui.ini) is embedded in CMake resources (`:/config/ipr-ui.ini`). It provides values for missing keys; it does not replace already recorded choices. Modifying this template requires recompilation. To customize an existing installation:

1. Read the path in the View menu and close IPR.
2. Make a backup copy of the user file.
3. Modify desired values with a UTF-8 editor, preserving schema version 2 and key names (do not manually modify the version).
4. Restart IPR. Values are read at startup; there is no monitoring of external modifications during execution.

To return to initial values, with the application closed, rename the user file to backup, then restart IPR. No photographic sidecar is affected. The user file may be reordered/rewritten by QSettings: the versioned template constitutes the commented reference.

## Schema 2 Contract

| Group | Keys and Values | Current Effect |
| --- | --- | --- |
| `meta` | `schemaVersion=2` | Contract version, not modifiable by QML API. |
| `window` | `normalWidth=1440`, `normalHeight=900`, `normalX=0`, `normalY=0`, `positionSaved=false`, `state=normal` | Normal window size/position; normal, maximized, or full-screen state. |
| `theme` | `background`, `panel`, `canvas`, `text`, `secondaryText` | Opaque colors `#RRGGBB`: background, surfaces, preview, and main control texts. |
| `layout` | `settingsWidth=320`, `imageInfoWidth=250`, `filmstripHeight=120` | Panel dimensions in Qt logical units. |
| `layout` | `showImageInfo`, `showHistogram`, `showFilmstrip` | Visibility, also modifiable in View. |
| `sections` | `rawDevelopment`, `light`, `curves`, `color`, `mixer`, `grading`, `effects`, `denoise`, `sharpening`, `export` | Ten independent groups; initially RAW Development / Profile and Light are open. |
| `selection` | `toneCurveChannel=0`, `mixerBand=0`, `gradingZone=0` | Last curve channel, mixer band, and grading zone accessed. |
| `export` | `format=JPEG`, `quality=92` | Last chosen format and JPEG quality; not the destination directory. |

Sizes are not yet adjustable by separator dragging: they are customized in INI. Colors concern connected surfaces and main texts, not a complete light/dark theme system: some nested modules, borders, and controls keep their current palette.

### Bounds and Error Handling

- Normal size: width 1000-8192, height 650-8192; coordinates ±200000.
- Settings width: 260-360; info: 210-300; ribbon: 100-260.
- Curve channel: 0-3; HSL band: 0-7 (violet 6, magenta 7); grading zone: 0-2.
- JPEG quality: 1-100; format: JPEG, PNG, TIFF, or DNG.
- Booleans: `true`, `false`, `1`, or `0`; valid and opaque theme colors.

On loading, a known invalid value reverts to template value and is repaired if the file is writable. The API rejects new invalid values and unknown keys. External keys already present are preserved in INI but are not exposed as application features.

A schema higher than 2 is opened read-only: the application does not overwrite a file with a newer version. A parsing or writing error is reported in the console and preferences dialog. The interface remains usable, but persistence is no longer guaranteed for this execution.

Migration from schema 1, or an old file without version: the old `selection/mixerBand=6` (magenta) becomes 7; other selections are unchanged. The schema is rewritten to 2. A schema 2 that selects 6 preserves this violet choice at subsequent starts; no photographic value is copied or modified. See [ADR-0005](../decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md).

### Reserved Parameters, No Current Effect

The `[future]` group contains:

```ini
cropGrid=thirds
maskOverlayColor=#80e55353
maskOverlayOpacity=0.45
aiEnabled=false
gpuBackend=auto
catalogEnabled=false
exportQueueConcurrency=1
```

These values are opaque strings preserved, **not active options**. They do not enable cropping, masks, AI, GPU, catalog, or export queue. Their support will require a specification, type validation, and if their contract changes, a schema migration. They are never executed or used as library names to load.

## Lifecycle and Reliability

`main.cpp` creates `UiPreferences` before the QML engine and exposes it as `uiPreferences`. Reference values are read from the resource, then overridden by the user file. QML initialization guards prevent component default values from overwriting restored state.

Each accepted change immediately updates memory and arms a **400 ms** timeout. Close events are grouped; only known modified keys are written. `sync()` is also called at window closure, `aboutToQuit`, and service destruction. Writing uses `QSettings::IniFormat` and preserves Qt's atomic synchronization requirement. This avoids direct unprotected rewriting; however, a forced closure before timeout ends may lose the latest modifications. [QSettings Documentation](https://doc.qt.io/qt-6/qsettings.html).

The application never restores a minimized state. Maximizing or going full-screen does not overwrite normal dimensions. At restart, the service chooses the screen with the largest overlap with the memorized geometry, or the main screen if the old screen has disappeared. Geometry is repositioned within the available area respecting the application's minimum dimensions. A screen with logical space less than 1000×650 may still require UI adaptation; this limitation is not resolved by persistence. The window manager remains sovereign, particularly for positioning under Wayland. Simultaneous multi-instance usage is not a validated contract: do not edit the file while an instance is running.

## Checks

`uiPreferencesTests` covers initial values, bounds, colors, INI roundtrip, unknown keys, unchanged future version file, off-screen geometry, and preservation of normal dimensions. `uiPreferencesQmlTests` verifies sections, selections, format/quality, absence of photo settings modification, and connection of the main window to a temporary INI.

The [matrix](../validation/matrice-validation.md) distinguishes these automated tests from actual Windows/DPI/multi-screen recipe. Tests never read or write user personal preferences.

## Sources

- Qt: [QSettings](https://doc.qt.io/qt-6/qsettings.html), INI and synchronization.
- Qt: [QStandardPaths](https://doc.qt.io/qt-6/qstandardpaths.html), multiplatform user location.
- Repository sources: [UiPreferences.cpp](../../src/app/UiPreferences.cpp), [Main.qml](../../src/qml/Main.qml), [Setting.qml](../../src/qml/Setting.qml), [INI template](../../config/ipr-ui.ini).
