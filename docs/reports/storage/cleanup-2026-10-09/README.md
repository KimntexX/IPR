*[Français](README.md) | [English](README_en.md)*

# Inventaire et nettoyage des images — 9 octobre 2026

Opération autorisée par l'utilisateur pour libérer l'espace disque. Périmètre :
`tests/image/` uniquement ; ni builds, ni installations, ni autres dossiers personnels.
Les RAW, DNG, références Lightroom, sidecars, neutres actifs, dernières variantes
utiles et preuves de mesure sont conservés.

## Bilan exécuté

Ce bilan décrit le premier nettoyage. À la demande suivante de l'utilisateur,
les références Lightroom Adobe Standard alors conservées ont été supprimées
dans une [opération distincte documentée](../lightroom-cleanup-2026-10-09/README.md).
Les chiffres et preuves ci-dessous restent ceux de l'opération initiale.

| Mesure | Résultat |
|---|---:|
| Corpus avant | 1 186 fichiers ; 240,92 Gio |
| TIFF IPR intermédiaires supprimés | 156 fichiers ; 37,52 Gio / 40,29 Go |
| Corpus après | 1 030 fichiers ; 203,40 Gio |
| Espace libre C: relevé après | 162 213 064 704 octets, environ 151,07 Gio |

Les tailles du corpus sont des tailles logiques des fichiers, pas une mesure
de leur allocation physique NTFS. L'espace libre varie avec les autres activités.

| Famille | TIFF supprimés | Gio |
|---|---:|---:|
| Blancs | 24 | 5,773 |
| Hautes lumières | 24 | 5,773 |
| Contraste | 16 | 3,848 |
| Noirs | 30 | 7,216 |
| Ombres | 30 | 7,216 |
| Nuance | 16 | 3,848 |
| Température | 16 | 3,848 |

## Critères et exceptions importantes

Suppression bornée aux archives IPR intermédiaires explicitement remplacées
par les corrections validées dans les rapports du 2 octobre. Aucun fichier
actif n'est supprimé simplement parce qu'il est ancien ou volumineux.

- Blancs/Hautes lumières : supprimer v1 et la branche positive v2 remplacée
  par v3 ; **conserver les huit négatifs v2 de chaque famille**, encore utilisés
  pour le verdict v3 et absents des dossiers actifs.
- Température : supprimer les anciens froids v1/v2 ; **conserver les huit
  chauds v1**, dont la branche n'a pas changé et qui sont absents du dossier actif.
- Contraste/Nuance : supprimer les variantes antérieures aux corrections
  validées ; conserver les exports actifs.
- Noirs/Ombres : supprimer les états avant séparation et post-séparation v1 ;
  conserver les archives de validation finale et toutes leurs références.
- Mélangeur : conserver les extrêmes et les intermédiaires, qui ne constituent
  pas la même campagne. Les noms identiques de ces séries ne prouvent pas des
  doublons de contenu.
- Bruit, ZIP des neutres et autres fichiers ambigus : conserver. Aucun balayage
  exhaustif des doublons pixel-identiques n'a été effectué.

Sources de décision : analyses [Noirs/Ombres](../../../validation/analyses/analyse-noirs-ombres-2026-10-02.md),
[Blancs/HL](../../../validation/analyses/analyse-blancs-hautes-lumieres-2026-10-02.md),
[Contraste](../../../validation/analyses/analyse-contraste-2026-10-02.md),
[Température](../../../validation/analyses/analyse-temperature-2026-10-02.md) et
[Nuance](../../../validation/analyses/analyse-nuance-2026-10-02.md).

## Traçabilité et récupération

- [Inventaire avant](inventory-before.csv) : chemins, tailles, dates et décision.
- [Volumes par dossier avant](directories-before.csv).
- [Journal des suppressions](deletion-log.csv) : 156 chemins, raisons, tailles,
  SHA-256 calculés avant suppression et statut final `deleted`.
- [Inventaire après](inventory-after.csv).
- [Résumé machine](summary.json).
- Script local borné à cette opération : `scripts/cleanup-obsolete-test-images-20261009.ps1` (non versionné).

Les 1 030 fichiers conservés ont été contrôlés par présence, taille et date UTC
après l'opération ; leurs pixels n'ont pas été intégralement rehachés. Aucune
suppression récursive de dossier n'a eu lieu. Les dossiers vides restent en place.

**Suppression définitive, sans passage par la corbeille.** Les empreintes et
mesures conservées ne sont pas une sauvegarde des pixels. Les anciens TIFF ne
pourront être récupérés qu'à partir d'une sauvegarde externe éventuelle ou
régénérés en reproduisant leur ancien moteur/configuration. Les conclusions
historiques restent consultables, mais certaines mesures intermédiaires ne
peuvent plus être relancées à l'identique depuis le corpus local.

Pour la prochaine campagne : une fonctionnalité à la fois, nouveaux neutres
identifiés, puis conservation des mesures et des seuls exports utiles. Le
script refuse une seconde exécution si cet audit existe : ce n'est pas un outil
de purge automatique pour de futures images.
