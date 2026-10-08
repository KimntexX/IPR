# Validation

Pour réaliser les campagnes soi-même :
[procédure de tests en autonomie](procedure-tests-autonomes.md)
(commandes, préparation des fichiers, mesures et décision de validation).

La validation sépare strictement :

- `protocols/` : procédure à reproduire avant une campagne ;
- `analyses/` : conclusions interprétées ;
- `raw/` : sorties brutes, non réécrites ;
- [matrice-validation.md](matrice-validation.md) : statut unique des fonctionnalités.

Pour produire les variantes IPR sans manipulation répétée de l'IHM :
[générateur d'images de test](tools/generateur-images-tests.md), exécutable
`iprGenerateTests` et lanceur Windows `scripts/generate-test-images.ps1`.

Pour la base colorimétrique, conserver un seul
[rapport évolutif](analyses/analyse-base-colorimetrique.md) : les campagnes
successives y sont ajoutées, avec leurs mesures datées et immuables dans `raw/`.

Les familles historiques ont été migrées le 3 octobre 2026 ; leur correspondance est consignée dans [le registre de migration](../reports/migration-registry.md). Les prochaines campagnes doivent créer directement leurs fichiers dans cette arborescence.
