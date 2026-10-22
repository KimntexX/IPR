*[Français](README.md) | [English](README_en.md)*

# Documentation technique IPR (LaTeX)

La source principale est `ipr-documentation-technique.tex`. Les chapitres sont séparés dans `chapters/` et la bibliographie dans `references.bib`.

Mise en forme : suivre les [conventions LaTeX communes](../../AGENTS.md) (12 pt, interligne 1,5, chapitres sur une nouvelle page et paragraphes espacés uniformément).

## Compiler sous Windows

Installer une distribution LaTeX (MiKTeX ou TeX Live) avec TikZ et la langue française. Depuis ce dossier :

```powershell
./build.ps1
```

Le PDF est produit dans `build/ipr-documentation-technique.pdf`.

En solution de repli, exécuter successivement :

```powershell
pdflatex -interaction=nonstopmode -halt-on-error ipr-documentation-technique.tex
bibtex ipr-documentation-technique
pdflatex -interaction=nonstopmode -halt-on-error ipr-documentation-technique.tex
pdflatex -interaction=nonstopmode -halt-on-error ipr-documentation-technique.tex
```

Les fichiers de construction peuvent être supprimés avec `./build.ps1 -Clean`.

## Règle de maintenance

Relecture du 22 octobre 2026 sur `dev` (`a15f703`) : master Rec.2020,
sidecar v6, contrôleur mémoire, ordre des traitements et diagrammes actualisés.
Les chapitres réservés restent vides. Les campagnes historiques décrites dans
le chapitre Qualité ne sont pas de nouveaux résultats de cette relecture.
Voir [le bilan documentaire](../../reports/documentation-audit-2026-10-22.md).


Actualisation du 8 octobre 2026 : version documentaire 0.3, recette RAW v5,
changement DCP sans réouverture visuelle, diagrammes actualisés et mise en
forme 12 pt / interligne 1,5. Le PDF historique était nommé
`build/ipr-documentation-technique-v03.pdf` (30 pages) : artefact local non suivi,
pas un document téléchargeable du dépôt.
Le nom habituel est régénéré avec `./build.ps1` après fermeture de son aperçu. Les données et
graphiques reproductibles sont référencés depuis
[metrics](../../validation/metrics/README.md), sans les confondre avec des
benchmarks du build actuel.

Les chapitres « Recadrage et effets géométriques », « Masques et retouche locale » et « Intégration de l'intelligence artificielle » sont délibérément vides : ils ne doivent être complétés qu'après validation de leur conception. Les rapports d'expérimentation datés sont organisés dans `docs/validation` et `docs/reports` ; la migration est consignée dans `docs/reports/migration-registry.md`.
