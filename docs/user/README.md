# Livre utilisateur IPR

Manuel français de la version alpha, fondé sur l’interface de `patch_ui`,
vérifiée dans les sources le **19 octobre 2026**. Il décrit l’utilisation,
pas les algorithmes de la [documentation technique](../technical/latex/README.md).

- [Source principale LaTeX](latex/ipr-livre-utilisateur.tex).
- [Chapitres par famille](latex/chapters/).
- Compilation : `powershell -ExecutionPolicy Bypass -File docs/user/latex/compile.ps1`
  depuis la racine du worktree. Sur Linux/macOS, lancer trois passes de
  `pdflatex -interaction=nonstopmode -halt-on-error -output-directory=build ipr-livre-utilisateur.tex`
  depuis `docs/user/latex`, après création du dossier `build`.
- PDF local : `docs/user/latex/build/ipr-livre-utilisateur.pdf`. Ce dossier généré
  est ignoré par Git ; les sources et le script sont versionnés.

## Organisation

Prise en main, navigation/comparaison, informations et sélection, puis un
chapitre par famille : RAW/profil, lumière, courbes, couleur, mélangeur,
grading, effets, bruit, netteté, optique. Préférences, export, dépannage,
glossaire et aide-mémoire clavier complètent le parcours.

Les chapitres **recadrage/géométrie**, **masques/retouche locale** et **IA**
reprennent les familles réservées du manuel technique : ils restent vides,
sans procédures fictives. Les mémos de survol prolongé sont réservés à la bêta.

## Traçabilité et maintenance

| Sujet utilisateur | Source de contrôle |
| --- | --- |
| Menus, ouverture, disposition | `src/qml/Main.qml` |
| Zoom, comparaison, Espace, molette | `src/qml/ImageViewport.qml` |
| Champs, pas clavier et reset | `src/qml/AdjustmentControl.qml` |
| Familles et domaines des valeurs | `src/qml/Setting.qml` |
| Courbes, mélangeur, grading | Composants QML correspondants sous `src/qml/` |
| Sélection et navigation | `ThumbnailRibbon.qml`, `FilmstripNavigation.qml` |
| Notes et libellés | `ImageInfoPanel.qml` |
| Préférences | Composants Preferences et `src/app/UiPreferences.cpp` |
| Sidecars, application groupée, export | `src/editor/imageeditor.cpp`, lecture seule dans ce chantier |

Une fonction exposée n’est pas une fonction validée sur tous les appareils.
Les limites et raccordements restent suivis dans le
[suivi IHM](../architecture/suivi-raccordements-ihm.md) et la
[matrice de validation](../validation/matrice-validation.md).
Mettre à jour le chapitre concerné et l’annexe clavier avec chaque évolution
utilisateur. Ne pas inventer de raccourcis issus d’autres logiciels.

Mise en page : 12 pt, interligne 1,5, chapitres sur une nouvelle page,
conformément aux [conventions documentaires](../AGENTS.md).
La traduction anglaise relève du chantier documentaire dédié.
