# Sources IPR

| Répertoire | Responsabilité |
| --- | --- |
| `app/` | Point d'entrée Qt, raccordement QML/C++ et préférences globales d’IHM (`UiPreferences`). |
| `cache/` | Mesure mémoire système, contrôleur sur thread dédié, réservations et LRU des images décodées. |
| `core/` | Contrats `ImageImport::Result`, `ImageMetadata`, `WorkingImage` et pipeline colorimétrique. |
| `editor/` | Orchestration du document, rendu, export et sidecars. |
| `profiles/` | Sélection et application DCP/LCP. |
| `providers/` | Providers d'images et de vignettes pour Qt Quick. |
| `qml/` | Interface Qt Quick, déjà découpée en composants réutilisables. |
| `raw/` | Adaptateurs RAW, contrat `CameraSignal`, caractérisations et estimation du noir CFA. |

`iprEngine` est la bibliothèque interne construite par CMake à partir des domaines C++. L’application, les tests et les sondes y sont liés afin de partager une définition unique des sources et des dépendances.
