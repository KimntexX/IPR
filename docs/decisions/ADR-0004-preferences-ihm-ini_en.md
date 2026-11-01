<!-- translation-source: docs/decisions/ADR-0004-preferences-ihm-ini.md; sha256: 2312501c004afe02cd94a102d7705156d3f8544a961bead4a32a43dfaf133426 -->
*[Français](ADR-0004-preferences-ihm-ini.md) | [English](ADR-0004-preferences-ihm-ini_en.md)*

# ADR-0004 — Global UI preferences in a user INI file

- **Date:** 4 October 2026.
- **Status:** accepted.
- **Scope:** interface presentation, not photographic adjustments.

## Context

**Evolution:** [ADR-0005](ADR-0005-melangeur-huit-bandes-luminance-bornee.md)
raises the current schema to 2 for migrating magenta selection(6→7).

The user wants window size, open sections and colors restored at restart,
with keys reserved for future features. Per-photo `.ipr` sidecars must not
carry this global state.

## Decision

Create C++ `UiPreferences` using `QSettings::IniFormat`, storing `ipr-ui.ini`
under `QStandardPaths::AppConfigLocation`. Embed the repository template as
a CMake resource; fill missing values without replacing valid preferences.
Schema 1 is validated; writes are grouped over 400 ms and synchronized on closure.
A future schema stays unchanged and opens read-only.

Normal geometry is distinct from maximized/full-screen state. Reserved
`future/*` keys have no effect until an explicit contract activates them.
Store no secrets, images or development settings in this INI.

## Alternatives and consequences

- JSON: better for nesting, unnecessary for scalar groups and would need
  another write manager.
- Windows registry: native but less readable and nonportable.
- SQLite: suitable for a future catalog, excessive for UI state.
- INI beside executable: installations may be unwritable and preferences
  shared across users; rejected as default.

Edit with the application closed; no hot reload or multi-instance guarantee.
Reserved keys do not promise implementation. Export preferences do not prove
DNG/Lightroom compatibility, which needs separate correction.

## Validation and references

- `uiPreferencesTests`, `uiPreferencesQmlTests`, isolated temporary files.
- [Complete contract/procedure](../architecture/ui-preferences.md).
- [QSettings](https://doc.qt.io/qt-6/qsettings.html) and
  [QStandardPaths](https://doc.qt.io/qt-6/qstandardpaths.html).
