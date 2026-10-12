*[Français](README.md) | [English](README_en.md)*

# IPR Scholar

Cours LaTeX consacrés aux modèles physiques, mathématiques et de traitement du signal effectivement employés par IPR. Chaque fichier `cours-*.tex` est autonome; `moteur-graphique-ipr.tex` est le guide d'implémentation détaillé.

Mise en forme : suivre les [conventions LaTeX communes](../AGENTS.md) (12 pt, interligne 1,5, chapitres sur une nouvelle page et paragraphes espacés uniformément).

| Document | Sujet |
| --- | --- |
| `cours-01-espace-lineaire-colorimetrie.tex` | espace linéaire, luminance et gamut |
| `cours-02-exposition-tonalite.tex` | EV, contraste, ombres et hautes lumières |
| `cours-03-balance-blancs-adaptation.tex` | Planck, Bradford et nuance |
| `cours-04-couleur-hsv-mixer-grading.tex` | vibrance, saturation, HSL et grading |
| `cours-05-contraste-local-gaussien.tex` | texture, clarté et flou |
| `cours-06-voile-atmospherique.tex` | modèle atmosphérique et DCP |
| `cours-07-courbes-interpolation.tex` | courbes de tonalité et interpolation |
| `cours-08-bruit-grain-signal.tex` | bruit, grain et reproductibilité |
| `cours-09-raw-profils-appareil.tex` | RAW, DCP et LCP |
| [cours-10-filtre-haar-ondelettes.tex](cours-10-filtre-haar-ondelettes.tex) | Haar 1D/2D, multi-échelle, MAD, bruit, exercices corrigés et application IPR ; [PDF](cours-10-filtre-haar-ondelettes.pdf) |
| [cours-11-architecture-images.tex](cours-11-architecture-images.tex) | Architecture complète d'une image : physique, échantillonnage, binaire, mémoire, compression, formats, RAW, couleur, métadonnées, Canon/Sony/Nikon et exercices ; [PDF](cours-11-architecture-images.pdf) |
| `moteur-graphique-ipr.tex` | pipeline, concurrence, persistance et registre des constantes |

## Compilation Windows

Avec MiKTeX ou TeX Live :

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

Les PDF sont produits à la racine de `scholar/`, à côté de leur source `.tex`. Les fichiers auxiliaires LaTeX sont ignorés par Git ; `-Clean` supprime uniquement les artefacts générés connus, y compris les PDF.

Le script emploie `pdflatex` et `bibtex`, sans dépendance à `latexmk` ou Perl.

Les valeurs qualifiées d'« empiriques » sont volontairement séparées des constantes normalisées : elles doivent être validées par le corpus IPR avant toute modification.

## Travaux pratiques — Haar

Le cours 10 dispose d'une bibliographie dédiée `references-haar.bib` et
d'[exemples NumPy exécutables](references/haar-exemples.py), sans accès aux photographies.
Depuis la racine du dépôt :

```bash
python docs/scholar/references/haar-exemples.py
```

Il distingue explicitement l'estimateur Haar/MAD du moteur et la réduction
de bruit réelle sur résidus gaussiens. Son ajout est automatiquement pris
en charge par `build.ps1`, qui découvre les sources `.tex` autonomes.

## Cours 11 — Architecture des images

Le cours suit sept parties : scène/capteur, octets/mémoire/conteneurs,
compression, formats, développement RAW/couleur/métadonnées,
constructeurs, puis architecture logicielle et diagnostic. Les chapitres
sont répartis dans `cours-11/` ; seul le fichier principal est compilé.
La bibliographie commentée est dans `references-images.bib` et figure
dans le PDF avec des liens cliquables. Sony et Nikon ont une base
documentée et une liste explicite de vérifications à compléter sur de
vrais fichiers ; les mesures Canon locales ne leur sont pas extrapolées.

Laboratoire pédagogique Python, sans dépendance externe, depuis la racine :

```bash
python docs/scholar/references/images-exemples.py
python docs/scholar/references/images-exemples.py --output-dir build-scholar-images-tp
python docs/scholar/references/images-exemples.py --inspect build-scholar-images-tp/deux-pixels.tiff
```

Sans option, le programme ne crée aucun fichier et lance ses dix autotests.
L'option de sortie crée seulement trois images synthétiques PPM/PNG/TIFF
dans un **nouveau** dossier, sans écrasement. Le lecteur PNG est un sous-ensemble
RGB8 borné et volontairement incomplet : ce n'est pas un codec de production.
Douze exercices corrigés accompagnent le cours.

Pour compiler uniquement ce cours, depuis `docs/scholar/` :

```powershell
pdflatex -interaction=nonstopmode -halt-on-error cours-11-architecture-images.tex
bibtex cours-11-architecture-images
pdflatex -interaction=nonstopmode -halt-on-error cours-11-architecture-images.tex
pdflatex -interaction=nonstopmode -halt-on-error cours-11-architecture-images.tex
```

Les PDF restent à la racine de Scholar. Aucun changement du moteur IPR
ni des photographies de validation n'est induit par ce cours.

Avec la distribution LaTeX locale de novembre 2025, les tableaux multipages
peuvent émettre `ignored error: Infinite glue shrinkage found in box being split`.
Il s'agit du [problème longtable #1907](https://github.com/latex3/latex2e/issues/1907),
non bloquant pour ce PDF ; aucune réinstallation de MiKTeX n'a été effectuée.
