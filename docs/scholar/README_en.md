*[Français](README.md) | [English](README_en.md)*

# IPR Scholar

LaTeX courses dedicated to the physical, mathematical, and signal processing models effectively used by IPR. Each `cours-*.tex` file is standalone; `moteur-graphique-ipr.tex` is the detailed implementation guide.

Formatting: follow the [common LaTeX conventions](../AGENTS.md) (12 pt, 1.5 line spacing, chapters on a new page, and uniformly spaced paragraphs).

| Document | Subject |
| --- | --- |
| `cours-01-espace-lineaire-colorimetrie.tex` | linear space, luminance, and gamut |
| `cours-02-exposition-tonalite.tex` | EV, contrast, shadows, and highlights |
| `cours-03-balance-blancs-adaptation.tex` | Planck, Bradford, and tint |
| `cours-04-couleur-hsv-mixer-grading.tex` | vibrance, saturation, HSL, and grading |
| `cours-05-contraste-local-gaussien.tex` | texture, clarity, and blur |
| `cours-06-voile-atmospherique.tex` | atmospheric model and DCP |
| `cours-07-courbes-interpolation.tex` | tone curves and interpolation |
| `cours-08-bruit-grain-signal.tex` | noise, grain, and reproducibility |
| `cours-09-raw-profils-appareil.tex` | RAW, DCP, and LCP |
| [cours-10-filtre-haar-ondelettes.tex](cours-10-filtre-haar-ondelettes.tex) | Haar 1D/2D, multi-scale, MAD, noise, corrected exercises, and IPR application; [PDF](cours-10-filtre-haar-ondelettes.pdf) |
| [cours-11-architecture-images.tex](cours-11-architecture-images.tex) | Complete image architecture: physics, sampling, binary, memory, compression, formats, RAW, color, metadata, Canon/Sony/Nikon, and exercises; [PDF](cours-11-architecture-images.pdf) |
| `moteur-graphique-ipr.tex` | pipeline, concurrency, persistence, and constant registry |

## Windows Compilation

With MiKTeX or TeX Live:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

PDFs are produced at the root of `scholar/`, alongside their `.tex` sources. LaTeX auxiliary files are ignored by Git; `-Clean` only removes known generated artifacts, including PDFs.

The script uses `pdflatex` and `bibtex`, without dependency on `latexmk` or Perl.

Values described as "empirical" are intentionally separated from standardized constants: they must be validated by the IPR corpus before any modification.

## Practical Work — Haar

Course 10 has a dedicated bibliography `references-haar.bib` and [executable NumPy examples](references/haar-exemples.py), without access to photographs.
From the repository root:

```bash
python docs/scholar/references/haar-exemples.py
```

It explicitly distinguishes the Haar/MAD estimator of the engine from actual noise reduction on Gaussian residuals. Its addition is automatically handled by `build.ps1`, which discovers standalone `.tex` sources.

## Course 11 — Image Architecture

The course follows seven parts: scene/sensor, bytes/memory/containers, compression, formats, RAW/color/metadata development, manufacturers, then software architecture and diagnostics. Chapters are distributed in `cours-11/`; only the main file is compiled. The annotated bibliography is in `references-images.bib` and appears in the PDF with clickable links. Sony and Nikon have a documented baseline and an explicit list of verifications to complete on real files; local Canon measurements are not extrapolated to them.

Educational Python lab, without external dependencies, from the root:

```bash
python docs/scholar/references/images-exemples.py
python docs/scholar/references/images-exemples.py --output-dir build-scholar-images-tp
python docs/scholar/references/images-exemples.py --inspect build-scholar-images-tp/deux-pixels.tiff
```

Without options, the program does not create any files and runs its ten self-tests. The output option creates only three synthetic PPM/PNG/TIFF images in a **new** directory, without overwriting. The PNG reader is a bounded RGB8 subset and intentionally incomplete: it is not a production codec. Twelve corrected exercises accompany the course.

To compile only this course, from `docs/scholar/`:

```powershell
pdflatex -interaction=nonstopmode -halt-on-error cours-11-architecture-images.tex
bibtex cours-11-architecture-images
pdflatex -interaction=nonstopmode -halt-on-error cours-11-architecture-images.tex
pdflatex -interaction=nonstopmode -halt-on-error cours-11-architecture-images.tex
```

PDFs remain at the root of Scholar. No changes to the IPR engine or validation photographs are induced by this course.

With the November 2025 local LaTeX distribution, multipage tables may emit `ignored error: Infinite glue shrinkage found in box being split`. This is [longtable issue #1907](https://github.com/latex3/latex2e/issues/1907), non-blocking for this PDF; no MiKTeX reinstallation has been performed.
