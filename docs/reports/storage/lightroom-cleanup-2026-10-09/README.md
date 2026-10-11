*[Français](README.md) | [English](README_en.md)*

# Nettoyage des références Lightroom — 9 octobre 2026

À la demande de l'utilisateur, les anciennes références Lightroom en Adobe
Standard sont supprimées pour préparer les comparaisons avec Camera Neutral.
Périmètre : `tests/image/` uniquement, archives comprises.

## Bilan exécuté

- 423 TIFF supprimés : **81 977 111 750 octets**, soit **81,98 Go / 76,35 Gio**.
- Corpus : 1 030 fichiers avant, 607 après ; présence et taille des 607 fichiers
  conservés contrôlées après suppression.
- Cinq TIFF Lightroom **Camera Neutral** conservés dans `base/neutre` :
  6V1A0007, 6V1A0043, 6V1A0327, 6V1A9421 et 6V1A9950.
- RAW, DNG, sidecars, exports IPR, mesures et rapports conservés.
- Espace libre C: relevé après : 299 710 681 088 octets, environ 279,13 Gio.
  Ce relevé dépend aussi des autres activités du disque ; ce n'est pas le gain
  imputable à cette seule opération.

## Critères et exceptions

Chaque suppression exige un TIFF sans `IPR` dans son nom, une identification
Lightroom dans le champ Software et le profil **Adobe Standard** dans le XMP.
Le profil de développement XMP est distinct du profil ICC de sortie, par exemple
sRGB : un TIFF sRGB n'est pas nécessairement développé avec Camera Neutral.
Les chemins absolus sont vérifiés dans le corpus ; les liens sont refusés.
Taille et date sont contrôlées avant suppression et une empreinte SHA-256 est
enregistrée. Aucun dossier n'est supprimé récursivement.

Cinq fichiers Lightroom sans profil identifiable restent en place :

- `tests/image/6V1A0027.jpg` ;
- `tests/image/6V1A9933.jpg`, `6V1A9933-2.jpg`, `6V1A9933-2-2.jpg` ;
- `tests/image/base/archive-ev-reference/6V1A0007-2.tif`.

## Preuves et reprise des tests

- [Inventaire et métadonnées avant](inventory.csv).
- [Journal des 423 suppressions et empreintes](deletion-log.csv).
- [Inventaire après](inventory-after.csv).
- [Résumé machine](summary.json).
- [Script d'inventaire](../../../../scripts/inventory-lightroom-references-20261009.py).
- [Script de suppression borné](../../../../scripts/remove-standard-lightroom-references-20261009.ps1).
- [Premier nettoyage des intermédiaires IPR](../cleanup-2026-10-09/README.md).

**Suppression définitive, sans passage par la corbeille.** Les empreintes et les
mesures ne sauvegardent pas les pixels ; récupération uniquement depuis une
éventuelle sauvegarde externe. Les preuves historiques restent consultables,
mais les comparaisons utilisant ces anciens TIFF ne sont plus relançables avec
le corpus local. Régénérer les références Lightroom en Camera Neutral avec la
balance des blancs et les options optiques du protocole avant la nouvelle campagne.
Les scripts refusent d'écraser cet audit : ce n'est pas une purge automatique.
