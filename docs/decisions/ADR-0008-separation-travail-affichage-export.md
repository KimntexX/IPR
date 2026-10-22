# ADR-0008 — Séparer espace de travail, affichage ICC et export

**Date :** 15 octobre 2026. **Statut :** directive acceptée par l'utilisateur,
implémentation à venir. Le choix de ProPhoto comme espace de travail généralisé
reste une piste à évaluer, pas une migration décidée.

## Contexte

Le gamut de l'écran ne doit pas déterminer le décodage ou la calibration RAW.
Un écran différent ne change pas le contenu photographique à conserver.
Le moteur actuel utilise `WorkingImage` en sRGB linéaire flottant ; ProPhoto
est un espace intermédiaire des tables/courbes DCP, pas le contrat de travail
de tous les réglages. `ColorPipeline::toSrgb()` attend explicitement ce sRGB
linéaire et produit actuellement la conversion commune aperçu/export.

## Décision

- Distinguer le développement et les retouches, la transformation d'affichage
  vers le profil ICC écran, et la transformation d'export vers l'espace choisi.
- Cible par défaut : employer le profil ICC associé à l'écran par le système.
  Prévoir une sélection manuelle dans les futurs paramètres avancés pour les
  cas particuliers et le diagnostic.
- Un changement d'écran ou de profil d'affichage ne doit déclencher ni décodage
  RAW, ni changement de DCP/calibration, ni modification des retouches ou exports.
- L'absence de profil exploitable devra avoir un repli documenté, signalé,
  sans inventer une caractérisation de l'écran.
- Préparer et mettre en cache les transformations d'affichage ; invalider
  seulement la sortie concernée. Éviter une double gestion des couleurs entre
  application, Qt et système d'exploitation.
- Prévoir l'épreuvage écran comme une fonction distincte et ultérieure.

## Limites et travaux préalables

La découverte des profils par OS, le multi-écran, les intentions de rendu,
le comportement SDR/HDR et la responsabilité Qt/OS restent à spécifier.
Une affectation de profil n'est pas une conversion des valeurs des pixels.
Ne pas appliquer une transformation écran à un export ordinaire ni mémoriser
le profil matériel d'un écran comme un réglage photographique portable `.ipr`.

Il est pertinent d'étudier un espace de travail large gamut en flottant,
notamment aux primaires ProPhoto D50, pour reporter la réduction de gamut
à la sortie. Cela exige un audit des opérations dépendantes des primaires,
du point blanc et de la fonction de transfert. Aucun profil écran ne remplace
l'espace de travail et aucun changement de tag ne vaut migration.

## Validation et état livré

Tester l'invariance des réglages et des octets d'export au changement d'écran,
le cache, la restauration, les profils invalides et l'absence de double
conversion. Revalider les réglages sensibles au pipeline avant une éventuelle
migration large gamut. Les preuves JSON actuelles restent historiques et intactes.

**Cette décision ne modifie ni code de rendu ni IHM aujourd'hui.**

Voir l'[architecture détaillée](../architecture/gestion-couleurs-affichage.md)
et la [roadmap RAW](../architecture/roadmap-ouverture-raw.md).

**Complément d'étude du 15 octobre, sans modification de la décision :**
le [comparatif documentaire, §43](../validation/analyses/analyse-base-colorimetrique.md#43-étude-préalable-prophoto-et-espaces-de-calcul-comparables--15-octobre-2026)
identifie Rec.2020 linéaire et ACEScg comme alternatives à évaluer. ProPhoto
n'est pas adopté par défaut et la migration automatique conditionnelle n'est
pas déclenchée. La séparation travail/affichage/export reste la directive acceptée.

**Décision ultérieure du 15 octobre :** après le comparatif numérique, l'utilisateur
retient Rec.2020 linéaire D65 pour le master. Voir
[ADR-0009](ADR-0009-espace-travail-rec2020-lineaire.md). Ce choix complète la
présente directive sans activer à lui seul l'affichage ICC. Le port local du
master, réalisé ensuite, est suivi dans cet ADR-0009 et le rapport RAW/profils §46.
