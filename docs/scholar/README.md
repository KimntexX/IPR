# IPR Scholar

Cours LaTeX consacrés aux modèles physiques, mathématiques et de traitement du signal effectivement employés par IPR. Chaque fichier `cours-*.tex` est autonome; `moteur-graphique-ipr.tex` est le guide d'implémentation détaillé.

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
