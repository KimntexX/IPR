*[Français](README.md) | [English](README_en.md)*

# IPR Technical Documentation (LaTeX)

The main source is `ipr-documentation-technique.tex`. Chapters are separated in `chapters/` and the bibliography in `references.bib`.

Formatting: follow the [common LaTeX conventions](../../AGENTS.md) (12 pt, 1.5 line spacing, chapters on a new page, and uniformly spaced paragraphs).

## Compiling on Windows

Install a LaTeX distribution (MiKTeX or TeX Live) with TikZ and French language. From this folder:

```powershell
./build.ps1
```

The PDF is produced in `build/ipr-documentation-technique.pdf`.

As a fallback, run sequentially:

```powershell
pdflatex -interaction=nonstopmode -halt-on-error ipr-documentation-technique.tex
bibtex ipr-documentation-technique
pdflatex -interaction=nonstopmode -halt-on-error ipr-documentation-technique.tex
pdflatex -interaction=nonstopmode -halt-on-error ipr-documentation-technique.tex
```

Build files can be deleted with `./build.ps1 -Clean`.

## Maintenance Rule

October 8, 2026 update: documentation version 0.3, RAW recipe v5, DCP change without visual reopening, updated diagrams, and 12 pt / 1.5 line spacing formatting. Since the PDF open in the IDE is locked, the new compilation is available in [build/ipr-documentation-technique-v03.pdf](build/ipr-documentation-technique-v03.pdf) (30 pages). The usual name can be regenerated with `./build.ps1` after closing its preview. Data and reproducible graphics are referenced from [metrics](../../validation/metrics/README.md), without confusing them with current build benchmarks.

The chapters "Cropping and Geometric Effects", "Masks and Local Retouching", and "AI Integration" are intentionally empty: they should only be completed after validating their design. Experiment reports are organized in `docs/validation` and `docs/reports`; the migration is recorded in `docs/reports/migration-registry.md`.
