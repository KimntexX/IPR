*[Français](README.md) | [English](README_en.md)*

# Analyses de validation

Chaque ensemble de fonctionnalités possède un seul rapport évolutif ; ses campagnes
datées y restent explicitement séparées. Chaque campagne pointe vers son protocole,
ses données brutes, la version du logiciel et la décision. Les anciens noms datés
restent des renvois compatibles, pas des rapports à alimenter en parallèle.

Depuis le 9 octobre, les [JSON versionnés et horodatés](../data/README.md)
font autorité sur les données. Avant suppression des exports, préserver mesures,
audits et provenance. Règle du 10 octobre : IPR seulement si verdict négatif,
IPR et Lightroom de la série si positif et validé. RAW et neutres communs sont
préservés ; aucune suppression sur verdict ambigu ou mémoire incomplète.
Lumières regroupe exposition, contraste, ombres, noirs, blancs et HL dans le
[rapport commun existant](analyse-validation-lumieres-base-2026-10-02.md).
Les anciens rapports séparés de ces contrôles restent historiques ; ne plus
les mettre à jour en parallèle. Les futures conclusions restent dans le rapport commun.

## Rapports de référence

- [Performance d’ouverture et navigation](analyse-performance-ouverture.md) :
  mesure native par étape, cache des pixels et cache des profils distincts.

- [Couleur](analyse-couleur.md) : température, nuance, vibrance, saturation,
  niveaux de gris ; première vérification native de l'outillage sur 40 états,
  sans nouvelle validation visuelle ou comparaison Lightroom.

- [Base colorimétrique](analyse-base-colorimetrique.md).
- [Mélangeur de couleurs](analyse-color-mixer.md) : quatre campagnes regroupées.
- [Bruit](analyse-bruit.md) : deux campagnes et correctif regroupés.
- [CSV et graphiques dérivés](../metrics/README.md).

Les autres familles ci-dessous conservent leur rapport unique existant ; inutile
de renommer un fichier uniquement pour sa date. Les liens historiques qui suivent
renvoient à la campagne correspondante dans le rapport de référence.

## Campagnes récentes

- [Lumières Camera Neutral — six scènes, 9 octobre](analyse-validation-lumieres-base-2026-10-02.md#campagne-complete-lumieres--nouveaux-neutres-9-octobre-2026) : 144 variantes complètes et auditées ; EV revalidé, amplitude Ombres positive et saturation Blancs/HL à poursuivre.

- [Exposition Camera Neutral — 9 octobre](analyse-validation-lumieres-base-2026-10-02.md#revalidation-de-lexposition--camera-neutral-9-octobre-2026) : loi EV revalidée sur six scènes ; complément LR 0037 et optique 9421 requis.

- [Réduction de bruit v2 — revalidation et corpus étendu](analyse-bruit-v2-2026-10-05.md) :
  145 variantes, 24 neutres et douze scènes ; progressivité et stabilité sombre
  confirmées, qualité globale et calibration chromatique à poursuivre.
- [Correctif bruit CPU](correctif-bruit-2026-10-05.md) : seuils progressifs,
  guide préfiltré, luminance et gamut ; tests automatisés et campagne v2 disponibles.
- [Réduction de bruit](analyse-bruit-2026-10-04.md) : 80 variantes retenues,
  audits des réexports, activité confirmée mais calibration non validée ;
  diagnostic synthétique des seuils, protection de contours et biais sombre.
- [Mélangeur — valeurs intermédiaires](analyse-color-mixer-intermediaires-2026-10-04.md) :
  progressivité à ±25/±50 après archivage des extrêmes.
- [Mélangeur — huit bandes, extrêmes](analyse-color-mixer-huit-bandes-2026-10-04.md) :
  consolidation des 54 variantes par application.
- [Mélangeur rouge/jaune/vert](analyse-color-mixer-red-yellow-green-2026-10-04.md) :
  premiers résultats photographiques, réponse de luminance et limites du corpus vert.
- [Mélangeur — corpus étendu](analyse-color-mixer-corpus-etendu-2026-10-04.md) :
  vert sur deux nouvelles scènes, autres bandes et distinction violet/magenta.

## Historique migré

- [Lumières de base](analyse-validation-lumieres-base-2026-10-02.md), [noirs et ombres](analyse-noirs-ombres-2026-10-02.md), [blancs et hautes lumières](analyse-blancs-hautes-lumieres-2026-10-02.md).
- [Contraste](analyse-contraste-2026-10-02.md), [texture](analyse-texture-2026-10-02.md), [clarté](analyse-clarte-2026-10-02.md) et [correction de voile](analyse-correction-voile-2026-10-02.md).
- [Température](analyse-temperature-2026-10-02.md), [nuance](analyse-nuance-2026-10-02.md), [vibrance](analyse-vibrance-2026-10-02.md) et [saturation](analyse-saturation-2026-10-02.md).

## Diagnostics complémentaires

- [Export DNG non affiché par Lightroom](diagnostic-export-dng-lightroom-2026-10-03.md).
