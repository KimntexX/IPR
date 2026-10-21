# ADR-0020 — Lecture permanente des noirs par IPR

- **Date :** 2026-10-20
- **Statut :** Accepté ; implémenté sur `patch_light`, validation utilisateur restante
- **Décideurs :** utilisateur

## Contexte

Les niveaux par défaut de LibRaw ne sont pas toujours justes : sur certains CR3 du
R6 Mark III, le noir est nul ou incohérent alors que les pixels masqués portent un
piédestal ; sur le 60D, le blanc est le même pour tous les modes ISO (ADR-0011,
extension). IPR ne corrigeait les noirs que par un repli réservé aux CR3 Canon dont
le noir LibRaw manquait. L'utilisateur demande d'intégrer la lecture des noirs de
façon permanente, sans dépendre du format ni d'une liste de boîtiers.

## Décision

- Sur **chaque** RAW, après le décodage de la mosaïque, IPR mesure le noir optique
  par canal CFA (médiane) : zones déclarées Canon (gauche, sinon haut), zones masquées
  déclarées par le format pour les autres marques, marges en dernier recours pour
  Canon seulement (toutes les marges ne sont pas masquées ailleurs).
- La mesure est comparée au noir de LibRaw (commun + canal). Celui-ci est **gardé**
  s'il concorde à `max(8, blanc / 1000)` codes près, **remplacé** sinon. Un motif
  spatial de noir déclaré par LibRaw et une mesure invalide ou implausible gardent la
  valeur de LibRaw. Le choix est journalisé (`IPR black`) et signalé dans les
  métadonnées (`opticalBlackLevelEstimated`).
- `RawBlackLevelEstimator` devient deux fonctions pures, sans LibRaw : `measure()` et
  `decide()`. L'ancien repli CR3 et ses gardes de format disparaissent (pas de legacy).
- Les blancs restent du ressort des règles du catalogue (ADR-0011, ADR-0012).

## Conséquences

- **Bénéfices :** le défaut de noir de LibRaw est corrigé pour tout format et toute
  marque disposant de pixels masqués, sans règle par boîtier ; prépare le décodage
  propre d'IPR (ADR-0018), dont c'est une brique.
- **Coûts et risques :** une mesure par ouverture (histogramme des zones masquées,
  coût négligeable devant le décodage) ; la tolérance est une valeur de prototype.
- **Compatibilité :** les 49 RAW du corpus (28 CR3, 21 CR2) donnent des rendus
  identiques bit à bit avant/après ; les règles R6/R7/60D sont inchangées.
- **Validation :** tests unitaires de mesure et de décision ; validation visuelle sur
  de nouveaux boîtiers à leur arrivée.

## Liens

- Code : `src/raw/RawBlackLevelEstimator.*`, `src/raw/RawDecoder.cpp` (`readBlackLevel`)
- Tests : `rawBlackLevelEstimatorTests`
- ADR liés : ADR-0011, ADR-0012, ADR-0018
