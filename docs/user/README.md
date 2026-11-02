*[Français](README.md) | [English](README_en.md)*

# Livre utilisateur IPR

Manuel français de la version alpha, réconcilié avec les sources de `dev`
(`b19b4c3`) le **31 octobre 2026**. Les branches non intégrées ne sont pas
présumées livrées. Il décrit l’utilisation,
pas les algorithmes de la [documentation technique](../technical/latex/README.md).

**État vérifié dans les sources au 31 octobre 2026 :** copier/coller,
sauvegarde manuelle et `Ctrl+clic` sont raccordés dans `dev`, ainsi que
l'application et l'export par lot. La duplication physique `Ctrl+D` reste
indisponible dans cette référence ; `Ctrl+S` est fonctionnel. Le panneau Bruit
expose une commande globale et ses contrôles avancés, avec une case expérimentale
pour le signal RAW signé. Cette relecture n'est pas une nouvelle validation
photographique. Voir le [bilan IHM](../architecture/suivi-raccordements-ihm.md)
et la [feuille de route bêta](../architecture/roadmap-beta.md).

- [Source principale LaTeX](latex/ipr-livre-utilisateur.tex).
- [Source anglaise](latex/ipr-livre-utilisateur_en.tex).
- [Chapitres par famille](latex/chapters/).
- Compilation : `powershell -ExecutionPolicy Bypass -File docs/user/latex/compile.ps1`
  depuis la racine du worktree (les deux langues ; `-Language fr` ou `en`
  pour une seule). Sur Linux/macOS, lancer trois passes de
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
| Commandes de fichier et contexte | `ApplicationActions.qml`, `PhotoContextMenu.qml` |
| Zoom, comparaison, Espace, molette | `src/qml/ImageViewport.qml` |
| Champs, pas clavier et reset | `src/qml/AdjustmentControl.qml` |
| Familles et domaines des valeurs | `src/qml/Setting.qml` |
| Courbes, mélangeur, grading | Composants QML correspondants sous `src/qml/` |
| Sélection et navigation | `ThumbnailRibbon.qml`, `FilmstripNavigation.qml` |
| Notes et libellés | `ImageInfoPanel.qml` |
| Préférences | Composants Preferences et `src/app/UiPreferences.cpp` |
| Sidecars, application groupée, export | `src/editor/imageeditor.cpp`, lecture seule pour la relecture |

Une fonction exposée n’est pas une fonction validée sur tous les appareils.
Les limites et raccordements restent suivis dans le
[suivi IHM](../architecture/suivi-raccordements-ihm.md) et la
[matrice de validation](../validation/matrice-validation.md).
Mettre à jour le chapitre concerné et l’annexe clavier avec chaque évolution
utilisateur. Ne pas inventer de raccourcis issus d’autres logiciels.

Mise en page : 12 pt, interligne 1,5, chapitres sur une nouvelle page,
conformément aux [conventions documentaires](../AGENTS.md).
La version anglaise est maintenue sur le chantier documentaire dédié.

Référence pour les conventions de clavier et la correspondance Ctrl/Command :
[QKeySequence — documentation officielle Qt](https://doc.qt.io/qt-6/qkeysequence.html).
