*[Français](README.md) | [English](README_en.md)*

# Protocoles de validation

Consolidation du 10 octobre 2026 : **un protocole évolutif par groupe**, avec
un [socle commun](protocole-commun.md). Une campagne produit une nouvelle
mémoire JSON versionnée/horodatée, pas un protocole daté par curseur.

Commencer par la [procédure autonome](../procedure-tests-autonomes.md), le
[générateur](../tools/generateur-images-tests.md) et la
[matrice](../matrice-validation.md). **Protocole prêt ≠ fonction validée** :
les anciennes validations Standard ne sont pas transférées automatiquement
au pipeline Camera Neutral.

## Catalogue courant

Mesures transversales : [Performance d’ouverture et navigation](protocole-performance-ouverture.md).

Acquisition spécialisée rattachée à RAW : [Mires et caractérisation couleur](protocole-mires-couleur.md)
(matériel, éclairages, captures et données de référence ; campagne non exécutée).

| Groupe | Protocole maintenu | Couverture / limites |
| --- | --- | --- |
| RAW / profils / base | [RAW et profils](protocole-raw-profils.md) | Base, DCP, WB, normalisation, restauration ; génération neutre/auto-WB seulement |
| Lumières | [Lumières](protocole-lumieres.md) | EV, contraste, ombres, noirs, HL, blancs |
| Courbes | [Courbes](protocole-courbes.md) | Luminance et R/G/B ; analyseur natif/JSON, rampes/HDR complémentaires |
| Couleur | [Couleur](protocole-couleur.md) | Température, nuance, vibrance, saturation, gris ; analyseur natif/JSON |
| Mélangeur | [Mélangeur](protocole-melangeur.md) | Huit bandes H/S/L, extrêmes et intermédiaires |
| Couleur du point | [Couleur du point](protocole-couleur-point.md) | Préréglages depuis des modèles Lightroom ; campagne et étude à faire |
| Color grading | [Grading](protocole-grading.md) | Analyseur natif/JSON ; circularité et transitions complémentaires |
| Effets | [Effets](protocole-effets.md) | Texture, clarté, voile, grain, flou ; ROI natives/JSON |
| Bruit | [Bruit](protocole-bruit.md) | Luminance/chrominance, activation, ROI Haar ; IA séparée |
| Netteté | [Netteté](protocole-nettete.md) | Analyseur natif/JSON ; profils de bord et couplage complémentaires |
| Optique | [Optique](protocole-optique.md) | Objectif et CA indépendants ; séries RAW 0/1, audit spécialisé de géométrie/franges nécessaire |
| IHM / persistance / export | [Recette transversale](protocole-ihm-persistance-export.md) | Sidecars, INI, zoom/comparaison, noms ; DNG non validé, export par lot non implémenté |

Tous les groupes photographiques du générateur sont couverts. Recadrage,
géométrie utilisateur, masques et IA restent hors recette de production tant
qu'ils ne sont pas implémentés.

## Fusions et méthodes spécialisées

- Température + Nuance + Vibrance + Saturation → Couleur, avec niveaux de gris.
- Texture + Clarté + Voile → Effets, avec Grain/Flou.
- Mélangeur extrêmes + intermédiaires → Mélangeur.
- Bruit initial + v2 → Bruit.

Les onze anciens fichiers deviennent des renvois stables. Les JSON et verdicts
historiques sont inchangés ; la rédaction ancienne reste dans l'historique Git.

Conserver séparément le [diagnostic blancs Canon](protocole-blancs-canon.md),
rattaché à RAW, distinct de Kelvin et du curseur Blancs.
Le [protocole base v1](protocole-base-colorimetrique-v1.md) conserve charte,
nuanciers et bibliographie ; son contexte IHM/export est historique.

## Mémoire et maintenance

Suivre le [contrat JSON](../data/README.md) et le socle : les images sont
supprimées manuellement par l'utilisateur après analyse. Aucun outil ne lance
de nettoyage automatique. Préserver RAW et preuves JSON ; une empreinte n'est
pas une sauvegarde des pixels.
Un seul rapport par ensemble ; CSV/courbes dérivés.

Créer un groupe depuis le [modèle](../../templates/validation-protocol-template.md).
Employer les [analyseurs unifiés](../tools/analyseurs-groupes.md) pour les nouvelles
campagnes. Les anciens modes/sondes restent historiques ; les recettes
spécialisées non couvertes par les métriques communes sont signalées.
