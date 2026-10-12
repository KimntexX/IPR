*[Français](README.md) | [English](README_en.md)*

# Nettoyage des dossiers build — 10 octobre 2026

Opération demandée par l'utilisateur, limitée à cinq dossiers explicitement
vérifiés sous la racine du projet. **93 fichiers supprimés, 792 331 647 octets
libérés**, soit 792,33 Mo / 755,63 Mio.

## Dossiers supprimés

- `build-canon-profils-tonal-20261007` : copie de 73 fichiers identiques à
  `build-diagnostic-profils-raw-20261007`, vérifiée chemin par chemin par SHA-256.
  Le dossier au nom descriptif reste disponible.
- `build-generator-smoke-20261006` : vieux exports JPEG/TIFF du smoke-test,
  remplacés par les campagnes actuelles et les tests du générateur. Les deux
  manifestes complets sont [conservés](preserved-manifests/) ; aucune donnée RAW
  originale n'est supprimée.
- `build-scholar-haar-preview` et `build-scholar-images-tp-20261008` : aperçus
  de pages et petits fichiers de démonstration générés. Sources et PDF Scholar
  restent présents sous `docs/scholar/`.
- `build-git-cleanup-main-20261009` : deux index Git temporaires du nettoyage
  distant ; ni index Git principal, ni commits, ni branches ne sont supprimés.

## Dossiers conservés

- `build-windows-msvc`, `build-wsl-libraw`, `build-wsl-nolibraw` : builds et
  environnements de test actifs.
- `build` : contient notamment le build Qt Creator dont le chemin est encore
  configuré dans `CMakeLists.txt.user` ; pas de suppression globale.
- Les autres dossiers de calibration/diagnostic : grilles flottantes,
  manifestes de normalisation, références et inventaires encore utiles pour
  les investigations. Un nom `build*` ne garantit pas un cache jetable ; les
  JSON ne remplacent pas les pixels nécessaires à de nouvelles métriques.

Les quinze dossiers conservés sont vérifiés après l'opération par nombre
de fichiers et somme des tailles, pas par rehachage intégral de chaque contenu.
Les chemins supprimés sont bornés à une liste blanche et les liens refusés.

## Audit et récupération

- [Inventaire avant et décisions](inventory.csv).
- [Journal des 93 suppressions, SHA-256 et statut](deletion-log.csv).
- [Résumé machine horodaté](summary.json).
- [Manifestes des anciens exports](preserved-manifests/).
- Script local borné à cette opération : `scripts/cleanup-unused-builds-20261010.ps1` (non versionné).

Suppression définitive, sans corbeille. Les profils de diagnostic supprimés
ont une copie identique conservée ; les aperçus peuvent être régénérés. Les
anciens exports de smoke-test ne sont pas sauvegardés par leurs manifestes
ou empreintes : ils demandent une régénération avec l'ancien build pour une
reproduction exacte. Aucune réécriture des mesures historiques n'a lieu.
