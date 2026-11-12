<!-- translation-source: docs/user/README.md; sha256: 2f65af92d3f1329de1571261239a1dbf728813104bac71bcd20de2258ac7a2f9 -->
*[Français](README.md) | [English](README_en.md)*

# IPR user guide

French alpha manual reconciled against `dev` sources (`b19b4c3`) on
**31 October 2026**, with a matching English edition. Changes on unmerged
branches are not assumed delivered. It describes usage, not the algorithms
of the [technical documentation](../technical/latex/README_en.md).

**Source-checked state on 31 October 2026:** copy/paste, manual saving and
`Ctrl+click` are connected in `dev`, as are batch application and export.
Physical duplication with `Ctrl+D` remains unavailable in this reference;
`Ctrl+S` works, as do `Ctrl+Z`, `Ctrl+Shift+Z` and `Ctrl+Y` for the history. Noise has a master control and advanced controls, with an
experimental signed-RAW checkbox. This review is not a new photographic
validation. See the [UI status](../architecture/suivi-raccordements-ihm.md)
and [beta roadmap](../architecture/roadmap-beta.md).

- [French LaTeX root](latex/ipr-livre-utilisateur.tex).
- [English LaTeX root](latex/ipr-livre-utilisateur_en.tex).
- [Chapters by family](latex/chapters/).
- Build from the worktree root:
  `powershell -ExecutionPolicy Bypass -File docs/user/latex/compile.ps1`.
  This builds both languages; use `-Language fr` or `en` for one.
  On Linux/macOS, create `build` under `docs/user/latex` and run three passes of
  `pdflatex -interaction=nonstopmode -halt-on-error -output-directory=build ipr-livre-utilisateur_en.tex`
  from that directory.
- Local PDFs: `docs/user/latex/build/ipr-livre-utilisateur.pdf` and
  `ipr-livre-utilisateur_en.pdf`. The generated build directory is ignored;
  sources and build script are versioned.

## Organization

Getting started, navigation/comparison, information and selection, then one
chapter per family: RAW/profile, light, curves, color, mixer, grading, effects,
noise, sharpening and optics. Preferences, export, troubleshooting, glossary
and keyboard reference complete the guide.

**Cropping/geometry**, **masks/local editing** and **AI** match the technical
manual's reserved families: they remain empty, without fictitious procedures.
Extended-hover tooltips are reserved for beta.

## Traceability and maintenance

| Reference | Source checked |
| --- | --- |
| Menus, opening, layout | `src/qml/Main.qml` |
| File/context commands | `ApplicationActions.qml`, `PhotoContextMenu.qml` |
| Zoom, comparison, Space, wheel | `src/qml/ImageViewport.qml` |
| Fields, keyboard steps, reset | `src/qml/AdjustmentControl.qml` |
| Families and value ranges | `src/qml/Setting.qml` |
| Curves, mixer, grading | Corresponding QML components under `src/qml/` |
| Selection/navigation | `ThumbnailRibbon.qml`, `FilmstripNavigation.qml` |
| Ratings/labels | `ImageInfoPanel.qml` |
| Preferences | Preferences components and `src/app/UiPreferences.cpp` |
| Sidecars, batch application, export | `src/editor/imageeditor.cpp`, read-only for this review |

An exposed function is not a function validated on every camera. Limits and
connections remain tracked in [UI follow-up](../architecture/suivi-raccordements-ihm.md)
and the [validation matrix](../validation/matrice-validation.md).
Update the relevant chapter and keyboard appendix with every user-facing
change. Do not invent shortcuts from other applications.

Layout: 12 pt,1.5 line spacing and chapters starting on new pages, following
the [documentation conventions](../AGENTS.md). English is maintained in the
dedicated documentation workstream.

Keyboard conventions and Ctrl/Command mapping:
[QKeySequence, official Qt documentation](https://doc.qt.io/qt-6/qkeysequence.html).
