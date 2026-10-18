# Roadmap — réglages Lumières

**Ouverture :** 18 octobre 2026. Branche de travail : `patch_light`.
Réglages : exposition, contraste, ombres, noirs, hautes lumières (HL), blancs.
Mesures et décisions : [rapport Lumières](../validation/analyses/analyse-validation-lumieres-base-2026-10-02.md) ;
protocole : [protocole Lumières](../validation/protocols/protocole-lumieres.md).

## État courant

| Réglage | État | Référence |
| --- | --- | --- |
| Exposition | Avant le DCP, sur le signal caméra ; conforme à LR à ±0,03 EV | Validé visuellement, 18 octobre |
| Contraste | S à extrémités fixes, domaine log, par canal | Validé, réserve à +100 (≈ 3 % plus fort que LR) |
| Ombres, noirs, HL, blancs | Plan A en cours : plages fixes recalées sur LR | Campagnes du 18 octobre |

Comparaisons IPR/LR à réglages égaux : calibration RAW de l'IHM, optique
coupée des deux côtés. Corpus : six scènes sombres du protocole, plus deux
scènes claires (6V1A0524 R6 Mark III, 7Y6A7003 R7) pour ne pas ajuster les
paramètres sur des images sombres seulement.

## Plan A — plages tonales fixes (en cours)

Masques et courbes identiques pour toutes les photos, ajustés sur les
références LR, par paires qui se recouvrent :

- **Tons bas** : noirs (point noir progressif, effet plus précoce en négatif,
  positif atténué et étendu) et ombres (progression et assombrissement profond).
- **Tons hauts** : HL élargies vers les tons moyens ; blancs positifs en
  déplacement du point blanc avec épaule douce, sans écrêtage.
- Tests de combinaison par paire, une régénération unique, puis validation
  visuelle de l'utilisateur réglage par réglage.

Avantage : comportement prévisible et identique d'une photo à l'autre.
Limite : la parité avec LR variera selon le contenu de l'image.

## Plan B — plages adaptatives (évolution tracée, non engagée)

Lightroom calcule ses plages à partir de l'histogramme de chaque image. Dans
ces scènes sombres, blancs à +100 éclaircit les tons moyens d'un facteur 5,7,
et noirs à −100 les rend quasiment noirs. Le plan B rendrait les masques
d'IPR adaptatifs : points noir et blanc, bornes de zones tirées de la
distribution de luminance de l'image.

Conditions avant de l'engager :

- Décision de l'utilisateur, après évaluation du plan A sur des photos variées.
- Statistiques calculées sur le **master linéaire avant les réglages tonaux**,
  jamais sur l'histogramme d'affichage. Celui-ci est correct (test du 18 octobre),
  mais calculé après réglages sur l'aperçu sRGB 8 bits du proxy : s'y appuyer
  ferait réagir un réglage à son propre effet.
- Comportement stable pendant le glissement d'un curseur et identique entre
  aperçu et export (statistiques figées par image, pas par rendu).
- Corpus élargi : scènes claires, contre-jour, faible et fort contraste,
  plusieurs boîtiers ; campagnes JSON comparées au plan A.

## Points reportés

- Corrections optiques par défaut **après validation des tests complets**
  (décision du 18 octobre), avec références LR réexportées dans le même état.
- Contraste : réserve à +100 ; noirs profonds plus remontés que LR à −100
  (×4,53 contre ×2,86 sur les 20 % bas), à revoir avec ombres et noirs.
- Transition visuelle entre l'aperçu du boîtier et le rendu IPR (contrôle mémoire).
