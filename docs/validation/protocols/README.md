# Protocoles de validation

Commencer par la [procédure de tests en autonomie](../procedure-tests-autonomes.md)
pour les outils, les conventions de fichiers et l'interprétation des résultats.

Créer un protocole depuis [`../../templates/validation-protocol-template.md`](../../templates/validation-protocol-template.md) avant une nouvelle campagne. Nom : `protocole-<fonction>-vN.md`.

## Campagnes récentes

Les séries IPR peuvent être produites avec le
[générateur hors IHM](../tools/generateur-images-tests.md). Son manifeste
définit les états ; les exports Lightroom restent réalisés séparément.

- [Base colorimétrique v1](protocole-base-colorimetrique-v1.md) : six scènes
  existantes, neutres frais, profils/WB tracés, charte physique et prises
  d'escalade identifiées ; comparaison Lightroom distincte de la fidélité.
- [Niveaux blancs Canon](protocole-blancs-canon.md) : cinq variantes par RAW,
  séparation du blanc candidat et de l'ajustement automatique LibRaw,
  témoin v3 et contrôle du clipping des hautes lumières.
- [Bruit v2 après correctif](protocole-bruit-v2.md) : mêmes RAW et Lightroom,
  nouveaux IPR sans écrasement, compléments ISO et textures facultatifs.
- [Réduction de bruit](protocole-validation-bruit-2026-10-04.md) : luminance et
  couleur séparées, pixels adjacents 16 bits, audit des neutres et de la netteté.
- [Mélangeur — valeurs intermédiaires](protocole-color-mixer-intermediaires-2026-10-04.md) :
  douze états par couleur, ±25/±50, progressions avec neutres propres.
- [Mélangeur de couleurs](protocole-validation-color-mixer-2026-10-04.md) :
  paires teinte/saturation/luminance, audit XMP et comparaison 16 bits.

## Historique migré

- [Texture](protocole-validation-texture-2026-10-02.md), [clarté](protocole-validation-clarte-2026-10-02.md), [correction de voile](protocole-validation-correction-voile-2026-10-02.md).
- [Température](protocole-validation-temperature-2026-10-02.md), [nuance](protocole-validation-nuance-2026-10-02.md), [vibrance](protocole-validation-vibrance-2026-10-02.md) et [saturation](protocole-validation-saturation-2026-10-02.md).
