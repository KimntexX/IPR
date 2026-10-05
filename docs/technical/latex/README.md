# Documentation technique IPR (LaTeX)

La source principale est `ipr-documentation-technique.tex`. Les chapitres sont séparés dans `chapters/` et la bibliographie dans `references.bib`.

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

Les chapitres « Recadrage et effets géométriques », « Masques et retouche locale » et « Intégration de l'intelligence artificielle » sont délibérément vides : ils ne doivent être complétés qu'après validation de leur conception. Les rapports d'expérimentation datés sont organisés dans `docs/validation` et `docs/reports` ; la migration est consignée dans `docs/reports/migration-registry.md`.
