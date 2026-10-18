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
| Ombres, noirs, HL, blancs | Plan A : plages fixes recalées sur LR | Campagnes du 18 octobre ; validation visuelle à faire, noirs à régénérer |

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

Bornes de zones (18 octobre 2026) : sans contrainte, le premier ajustement
sur six scènes sombres a placé les noirs au gris moyen (−2,6 EV), en apprenant
l'adaptation de LR à ces images. Le centre de chaque réglage est désormais
contraint à la zone correspondante de l'histogramme de LR, découpé sur son axe
encodé (repère communiqué par l'utilisateur ; valeurs approximatives) :
noirs 0–10 % (< −6,6 EV), ombres 10–33 % (−6,6 à −3,5 EV), exposition
33–66 %, HL 66–90 % (−1,35 à −0,34 EV), blancs 90–100 % (> −0,34 EV).

Ajustement retenu (18 octobre 2026, huit scènes) : les règles de séparation
des tests d'export (un réglage ne déplace pas de plus de 2 niveaux sRGB les
gris de l'autre extrémité ni les tons moyens) sont des contraintes de
l'ajustement, pas des tests à assouplir. Les tons bas s'effacent sur les 2 EV
sous le blanc, les HL sur le dernier EV, laissé aux blancs. En éclaircissement,
le canal sRGB le plus fort passe par la même épaule douce que la luminance :
un plafond dur figeait les couleurs saturées à 1 (écrêtage blancs +25 :
30 % contre 2,4 % pour LR). Écart |EV| moyen aux quantiles, huit scènes :

| Réglage | Écart | Limite connue |
| --- | --- | --- |
| Ombres | 0,25 EV | +100 : tons très profonds (q5) moins remontés que LR (−0,9 EV) |
| Noirs | 0,31 EV | +100 : q25 moins remonté (−0,7 EV) ; LR déplace les tons moyens. Négatif : orteil doux puis point noir tardif |
| HL | 0,24 EV | +100 : écrêtage 8,6 % contre 37 % pour LR (scène claire) |
| Blancs | 0,38 EV | +100 : tons moyens moins éclaircis (jusqu'à −1,9 EV) |

Ces écarts restants viennent surtout de l'adaptation de LR aux scènes sombres
(blancs +100 y éclaircit les tons moyens d'un facteur 5,7), interdite par les
plages fixes : c'est le périmètre du plan B. Validation visuelle à suivre.

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
- IHM : surligner sur l'histogramme la zone d'un réglage Lumières au survol de
  son curseur, comme LR (noirs, ombres, exposition, HL, blancs).

## Production des références Lightroom — pistes d'automatisation

Les exports LR manuels (jusqu'à 64 par paire de scènes) sont le goulot des
campagnes. Deux pistes sont retenues par l'utilisateur, pour plus tard :

1. **Fichiers XMP préparés** (test à prévoir) : copies du RAW, chacune avec un
   `.xmp` dérivé du neutre où seul le réglage testé change. Ouverture du dossier
   dans Lightroom (onglet Local, qui lit et écrit ces fichiers annexes), puis un
   seul export TIFF 16 bits sRGB avec métadonnées. Premier essai sur deux images
   pour vérifier que Lightroom relit bien les `.xmp` préparés.
2. **Camera Raw scripté dans Photoshop**, avec la version d'essai disponible,
   pour les validations des autres fonctionnalités. Vérifier d'abord l'égalité
   avec les exports LR sur quelques neutres. L'API cloud Lightroom
   (Firefly Services) est écartée : offre entreprise, sortie TIFF 16 bits non
   documentée, RAW envoyés en ligne.
