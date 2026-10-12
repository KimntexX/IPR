*[Français](README.md) | [English](README_en.md)*

# Validation

Pour réaliser les campagnes soi-même :
[procédure de tests en autonomie](procedure-tests-autonomes.md)
(commandes, préparation des fichiers, mesures et décision de validation).

La validation sépare strictement :

- `protocols/` : procédure à reproduire avant une campagne ;
- `analyses/` : conclusions interprétées ;
- `raw/` : JSON canoniques versionnés et horodatés faisant autorité, et anciennes sorties brutes non réécrites ;
- [data/](data/README.md) : contrat de conservation de la mémoire JSON et de ses dérivés ;
- [metrics/](metrics/README.md) : CSV et courbes dérivés, avec provenance ;
- [matrice-validation.md](matrice-validation.md) : statut unique des fonctionnalités.

Pour produire les variantes IPR sans manipulation répétée de l'IHM :
[générateur d'images de test](tools/generateur-images-tests.md), exécutable
`iprGenerateTests` et lanceur Windows `scripts/generate-test-images.ps1`.

Pour les mesurer selon les mêmes groupes : [analyseurs natifs unifiés](tools/analyseurs-groupes.md),
`tests/analyse_groupes.py` ou une entrée dédiée par groupe, manifestes/mappings
explicites et JSON immuables. Les diagnostics historiques restent séparés.

Conserver un seul rapport par ensemble de fonctionnalités conformément à la
[règle du 9 octobre](data/README.md). Exemple : le
[rapport évolutif de base colorimétrique](analyses/analyse-base-colorimetrique.md).
Les campagnes successives y restent distinguées, avec JSON canoniques versionnés
et horodatés dans `raw/`. Les anciennes preuves y restent conservées.

Les familles historiques ont été migrées le 3 octobre 2026 ; leur correspondance est consignée dans [le registre de migration](../reports/migration-registry.md). Les prochaines campagnes doivent créer directement leurs fichiers dans cette arborescence.

Depuis le 8 octobre, les campagnes mélangeur et bruit sont regroupées dans
leurs rapports évolutifs respectifs. Les anciens noms restent des renvois,
sans copie du contenu. Les journaux de séance et les mesures brutes ne sont
pas fusionnés avec les conclusions actuelles.
