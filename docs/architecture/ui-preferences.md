# Préférences globales de l’interface

Implémentation : `src/app/UiPreferences.{h,cpp}`, `Main.qml`, `Setting.qml` et
`SettingsSection.qml`. Schéma courant : **2**, mis à jour le 4 octobre 2026.

## Deux persistances distinctes

Le fichier **`ipr-ui.ini`** appartient à l’utilisateur de l’application : il
conserve la présentation de l’interface, indépendamment de la photo ouverte.
Les sidecars **`.ipr`** restent responsables du développement photographique,
des courbes et des annotations par image. Une section ouverte ou une bande HSL
sélectionnée n’est pas un réglage photographique et ne déclenche pas de rendu.

INI est choisi pour sa lisibilité, ses groupes nommés et l’intégration Qt. JSON
serait pertinent pour des structures imbriquées ou des listes complexes ; une
base SQLite conviendrait à un futur catalogue, pas à cet état d’interface.
Le choix est consigné dans [ADR-0004](../decisions/ADR-0004-preferences-ihm-ini.md).

## Emplacement et utilisation

Le chemin est calculé par `QStandardPaths::AppConfigLocation`, avec les noms
d’organisation et d’application `IPR`. Il n’est ni relatif au dossier de travail,
ni placé à côté des photos ou de l’exécutable. Qt détermine le répertoire selon
le système ; **Affichage → Fichier de préférences…** affiche le chemin exact.
Le fichier est créé au premier lancement de cette version, si le répertoire
utilisateur est accessible. [Documentation QStandardPaths](https://doc.qt.io/qt-6/qstandardpaths.html).

Le modèle versionné [config/ipr-ui.ini](../../config/ipr-ui.ini) est embarqué
dans les ressources CMake (`:/config/ipr-ui.ini`). Il fournit les valeurs des
clés manquantes ; il ne remplace pas les choix déjà enregistrés. Modifier ce
modèle exige une recompilation. Pour personnaliser une installation existante :

1. Lire le chemin dans le menu Affichage et fermer IPR.
2. Faire une copie de sauvegarde du fichier utilisateur.
3. Modifier les valeurs souhaitées avec un éditeur UTF-8, en conservant la
   version du schéma à 2 et les noms des clés (ne pas modifier la version manuellement).
4. Relancer IPR. Les valeurs sont lues au démarrage ; il n’y a pas de surveillance
   des modifications externes pendant l’exécution.

Pour revenir aux valeurs initiales, application fermée, renommer le fichier
utilisateur en sauvegarde puis relancer IPR. Aucun sidecar photographique n’est
touché. Le fichier utilisateur peut être réordonné/réécrit par QSettings : le
modèle versionné constitue la référence commentée.

## Contrat du schéma 2

| Groupe | Clés et valeurs | Effet actuel |
| --- | --- | --- |
| `meta` | `schemaVersion=2` | Version du contrat, non modifiable par l’API QML. |
| `window` | `normalWidth=1440`, `normalHeight=900`, `normalX=0`, `normalY=0`, `positionSaved=false`, `state=normal` | Taille/position de la fenêtre normale ; état normal, maximisé ou plein écran. |
| `theme` | `background`, `panel`, `canvas`, `text`, `secondaryText` | Couleurs opaques `#RRGGBB` : fond, surfaces, aperçu et textes des contrôles principaux. |
| `layout` | `settingsWidth=320`, `imageInfoWidth=250`, `filmstripHeight=120` | Dimensions des panneaux en unités logiques Qt. |
| `layout` | `showImageInfo`, `showHistogram`, `showFilmstrip` | Visibilité, également modifiable dans Affichage. |
| `sections` | `light`, `curves`, `color`, `mixer`, `grading`, `effects`, `denoise`, `sharpening`, `export` | Dépliage indépendant des neuf groupes. Initialement, seule Lumière est ouverte. |
| `selection` | `toneCurveChannel=0`, `mixerBand=0`, `gradingZone=0` | Dernier canal de courbe, bande du mélangeur et zone du grading consultés. |
| `export` | `format=JPEG`, `quality=92` | Dernier format choisi et qualité JPEG ; pas le répertoire de destination. |

Les tailles ne sont pas encore réglables par glissement de séparateurs : elles
se personnalisent dans l’INI. Les couleurs concernent les surfaces raccordées et
les textes principaux, pas un système complet de thèmes clair/sombre : certains
modules imbriqués, bordures et contrôles gardent leur palette actuelle.

### Bornes et traitement des erreurs

- Taille normale : largeur 1000–8192, hauteur 650–8192 ; coordonnées ±200000.
- Largeur réglages : 260–360 ; informations : 210–300 ; ruban : 100–260.
- Canal de courbe : 0–3 ; bande HSL : 0–7 (violet 6, magenta 7) ; zone grading : 0–2.
- Qualité JPEG : 1–100 ; format : JPEG, PNG, TIFF ou DNG.
- Booléens : `true`, `false`, `1` ou `0` ; couleurs de thème valides et opaques.

Au chargement, une valeur connue invalide revient à la valeur du modèle et est
réparée si le fichier est inscriptible. L’API refuse les nouvelles valeurs
invalides et les clés inconnues. Les clés externes déjà présentes sont conservées
dans l’INI, mais ne sont pas exposées comme fonctionnalités de l’application.

Un schéma supérieur à 2 est ouvert en lecture seule : l’application ne réécrit
pas le fichier d’une version plus récente. Une erreur de parsing ou d’écriture
est signalée en console et dans le dialogue de préférences. L’interface reste
utilisable mais la persistance n’est plus garantie pour cette exécution.

Migration depuis le schéma 1, ou un ancien fichier sans version : l'ancien
`selection/mixerBand=6` (magenta) devient 7 ; les autres sélections sont inchangées.
Le schéma est réécrit à 2. Un schéma 2 qui sélectionne 6 conserve ce choix violet
aux démarrages suivants ; aucune valeur photographique n'est copiée ni modifiée.
Voir [ADR-0005](../decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md).

### Paramètres réservés, sans effet actuel

Le groupe `[future]` contient :

```ini
cropGrid=thirds
maskOverlayColor=#80e55353
maskOverlayOpacity=0.45
aiEnabled=false
gpuBackend=auto
catalogEnabled=false
exportQueueConcurrency=1
```

Ces valeurs sont des chaînes opaques conservées, **pas des options actives**.
Elles n’activent ni recadrage, ni masques, ni IA, ni GPU, ni catalogue, ni file
d’export. Leur prise en charge nécessitera une spécification, une validation
typée et, si leur contrat change, une migration de schéma. Elles ne sont jamais
exécutées ni utilisées comme noms de bibliothèques à charger.

## Cycle de vie et fiabilité

`main.cpp` crée `UiPreferences` avant le moteur QML et l’expose comme
`uiPreferences`. Les valeurs de référence sont lues dans la ressource, puis
surchargées par le fichier utilisateur. Les gardes d’initialisation QML évitent
que les valeurs par défaut des composants écrasent l’état restauré.

Chaque changement accepté actualise immédiatement la mémoire et arme une
temporisation de **400 ms**. Les événements rapprochés sont regroupés ; seules
les clés connues modifiées sont écrites. `sync()` est également appelé à la
fermeture de fenêtre, à `aboutToQuit` et à la destruction du service. L’écriture
utilise `QSettings::IniFormat` et conserve l’exigence de synchronisation atomique
de Qt. Cela évite une réécriture directe non protégée ; une fermeture forcée
avant la fin du délai peut néanmoins perdre les toutes dernières modifications.
[Documentation QSettings](https://doc.qt.io/qt-6/qsettings.html).

L’application ne restaure jamais un état minimisé. Maximiser ou passer en plein
écran n’écrase pas les dimensions normales. Au redémarrage, le service choisit
l’écran présentant le plus grand recouvrement avec la géométrie mémorisée, ou
l’écran principal si l’ancien écran a disparu. La géométrie est replacée dans la
zone disponible en respectant les dimensions minimales de l’application.
Un écran dont l’espace logique est inférieur à 1000 × 650 peut encore demander
une adaptation de l’IHM ; cette limitation n’est pas résolue par la persistance.
Le gestionnaire de fenêtres reste souverain, notamment pour la position sous
Wayland. L’usage multi-instance simultané n’est pas un contrat validé : ne pas
éditer le fichier pendant qu’une instance fonctionne.

## Vérifications

`uiPreferencesTests` couvre les valeurs initiales, bornes, couleurs, aller-retour
INI, clés inconnues, fichier de version future inchangé, géométrie hors écran et
conservation des dimensions normales. `uiPreferencesQmlTests` vérifie les
sections, sélections, format/qualité, absence de modification des réglages photo,
et le raccordement de la fenêtre principale à un INI temporaire.

La [matrice](../validation/matrice-validation.md) distingue ces tests automatisés
de la recette réelle Windows/DPI/multi-écrans. Les tests ne lisent et n’écrivent
jamais les préférences personnelles de l’utilisateur.

## Sources

- Qt : [QSettings](https://doc.qt.io/qt-6/qsettings.html), INI et synchronisation.
- Qt : [QStandardPaths](https://doc.qt.io/qt-6/qstandardpaths.html), emplacement utilisateur multiplateforme.
- Sources du dépôt : [UiPreferences.cpp](../../src/app/UiPreferences.cpp),
  [Main.qml](../../src/qml/Main.qml), [Setting.qml](../../src/qml/Setting.qml),
  [modèle INI](../../config/ipr-ui.ini).
