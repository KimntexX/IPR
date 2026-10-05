# ADR-0004 — Préférences globales d’IHM dans un INI utilisateur

- **Date :** 4 octobre 2026.
- **Statut :** accepté.
- **Périmètre :** présentation de l’interface, sans réglages photographiques.

## Contexte

**Évolution :** [ADR-0005](ADR-0005-melangeur-huit-bandes-luminance-bornee.md)
porte le schéma courant à 2 pour migrer la sélection magenta (6→7).

L’utilisateur souhaite retrouver taille de fenêtre, sections ouvertes et couleurs
au redémarrage, avec des clés préparées pour les fonctions futures. Les sidecars
`.ipr` sont liés aux photos et ne doivent pas porter cet état global.

## Décision

Créer le service C++ `UiPreferences`, basé sur `QSettings::IniFormat`, avec un
fichier `ipr-ui.ini` sous `QStandardPaths::AppConfigLocation`. Le modèle du dépôt
est embarqué comme ressource CMake ; les valeurs manquantes sont complétées sans
remplacer les préférences valides. Le schéma 1 est validé, les écritures sont
regroupées pendant 400 ms et synchronisées à la fermeture. Un schéma futur est
laissé inchangé et ouvert en lecture seule.

La géométrie normale est distincte de l’état maximisé/plein écran. Les clés
`future/*` sont réservées et sans effet ; leur activation fera l’objet d’un
contrat explicite. Aucun secret, image ou réglage de développement n’est stocké
dans cet INI.

## Alternatives et conséquences

- JSON : meilleur pour des structures imbriquées ; pas nécessaire pour ces
  groupes de scalaires, demanderait un gestionnaire d’écriture supplémentaire.
- Registre Windows : intégration native mais moins lisible et non portable.
- SQLite : adapté à un catalogue futur, disproportionné pour cet état d’IHM.
- INI à côté de l’exécutable : installations parfois non inscriptibles et
  préférences partagées entre utilisateurs ; rejeté comme chemin par défaut.

Le fichier se modifie application fermée ; il n’y a pas de rechargement à chaud
ni de garantie multi-instance. Les clés réservées ne constituent pas une
promesse d’implémentation. Les préférences d’export n’attestent pas de la
compatibilité du DNG avec Lightroom, qui reste à corriger séparément.

## Validation et références

- `uiPreferencesTests` et `uiPreferencesQmlTests`, fichiers temporaires isolés.
- [Contrat complet et procédure](../architecture/ui-preferences.md).
- [QSettings](https://doc.qt.io/qt-6/qsettings.html) et
  [QStandardPaths](https://doc.qt.io/qt-6/qstandardpaths.html).
