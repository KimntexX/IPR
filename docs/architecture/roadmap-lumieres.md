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
| Ombres, noirs, HL, blancs | Plan B : formes de LR et poids par image | Campagnes du 18 octobre au soir ; validation visuelle à faire |

Comparaisons IPR/LR à réglages égaux : calibration RAW de l'IHM, optique
coupée des deux côtés. Corpus : six scènes sombres du protocole, plus deux
scènes claires (6V1A0524 R6 Mark III, 7Y6A7003 R7) pour ne pas ajuster les
paramètres sur des images sombres seulement.

## Plan A — plages tonales fixes (remplacé par le plan B)

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

Règle de zones redéfinie (18 octobre 2026, après validation visuelle de
l'utilisateur). L'ancienne règle de `verifyBlackShadowSeparation` et
`verifyHighlightWhiteSeparation` isolait chaque réglage : noirs +100 ne devait
pas déplacer de plus de 2 niveaux sRGB les gris moyens (110) ni clairs (220),
blancs et HL pas plus de 2 niveaux le gris sombre (35). Les captures de
l'utilisateur montrent l'inverse dans LR : noirs et blancs atteignent les tons
moyens. **Nouvelle règle : un réglage peut agir sur toute la plage tonale,
mais son effet, en EV de lumière linéaire, est le plus faible à l'extrémité
opposée de l'histogramme** (marge 0,03 EV ; les tons voisins du blanc, limités
par la marge, sont exclus de la comparaison). Forces minimales et ordre des
tons inchangés. Les bornes de centres sont élargies (noirs < −2 EV, HL −4 à
−0,34 EV, blancs > −3 EV) ; les ombres, jugées correctes, gardent leur ajustement.

Autres éléments : orteil doux des noirs négatifs (y² / (y + orteil)) avant un
point noir tardif ; tons bas effacés sur les 2 EV sous le blanc, HL sur le
dernier EV ; épaule douce du canal sRGB le plus fort en éclaircissement ; courbe
échantillonnée par rendu et rendue croissante (combinaisons sans inversion).
Écart |EV| moyen prévu aux quantiles, huit scènes :

| Réglage | Écart | Limite connue |
| --- | --- | --- |
| Ombres | 0,25 EV | +100 : légère perte de détail (validation visuelle), compensable |
| Noirs | 0,12 EV | proche de LR des deux côtés |
| HL | 0,20 EV | +100 : écrêtage 9 % contre 37 % pour LR (scène claire) |
| Blancs | 0,26 EV | +100 : tons moyens moins éclaircis que LR (jusqu'à −1,7 EV) |

Blancs positifs : aucune courbe fixe ne reproduit LR. Sur 6V1A0043, LR remonte
le quantile q75 de plus de 4 EV ; sur 6V1A0007, il écrête 45 % de l'image. Un
point blanc fixe assez fort sur-remonte les tons profonds (+1,5 à +2 EV) :
LR place son point blanc selon le contenu de l'image, ce qui relève du plan B.

## Plan B — poids par image (engagé le 18 octobre 2026)

Décision de l'utilisateur après validation visuelle du plan A : les courbes ne
peuvent pas être génériques ; chaque image reçoit des poids, calculés à son
ouverture et mémorisés avec elle dans le cache.

Analyse préalable : les exports LR sont alignés pixel à pixel sur leur neutre
LR (même RAW, même géométrie), ce qui donne la courbe de transfert propre à LR,
image par image, sans passer par le rendu d'IPR. Constats sur huit scènes :

- **Noirs et blancs négatifs ne s'adaptent pas** : même courbe sur toutes les
  scènes (dispersion 0,05 EV pour noirs +100 ; noirs −100 = point noir fixe
  vers 2^-5).
- **Ombres, HL et blancs positifs s'adaptent** : forme fixe, gain propre à
  l'image (blancs +100 : de +0,65 EV sur 6V1A0524 à +4,7 EV sur 6V1A0043). Un
  gain par image ramène l'écart de 0,42–0,48 EV (courbe fixe) à 0,05–0,17 EV.
- **Couleur** : LR applique noirs et blancs **canal par canal** ; à noirs −100,
  il sature les couleurs (×1,40), ce qu'une échelle RGB commune ne fait pas
  (désaturation vue par l'utilisateur sur 6V1A9950).

Mise en œuvre :

- Formes de LR (nœuds tous les EV, quatre niveaux de curseur, interpolation
  linéaire), ajustées par `scripts/fit_tone_recovery.py`, qui génère
  `src/core/ToneRecoveryModel.h`.
- Gain par image prédit par deux statistiques de luminance (moyenne et
  quantiles du log2 Y), borné à la plage vue à l'ajustement ±0,3 en log.
- Statistiques mesurées une fois sur le proxy neutre au chargement, portées
  par l'image de travail (cache, master reconstruit à l'export) : aperçu et
  export identiques, stables pendant le glissement d'un curseur.
- Ombres et HL sur la luminance ; noirs et blancs par canal. Pas de
  remontée sous 2^-14 (aucune donnée LR : plancher de bruit préservé).

Validation croisée (chaque scène prédite sans l'avoir vue), écart moyen à ±100 :
ombres 0,27–0,31 EV, noirs 0,06–0,21, HL 0,08–0,11, blancs − 0,04, blancs +
0,39 (pire 0,77). Limite : huit scènes, un boîtier principal ; le gain des
blancs positifs reste le moins bien prédit. **Élargir le corpus de références
LR** (piste XMP ci-dessous) améliorerait directement la prédiction.

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

1. **Fichiers XMP préparés — échec avec Lightroom (version cloud), 18 octobre 2026.**
   `scripts/prepare_lr_xmp_batch.py` crée, par scène et par valeur, un lien
   physique vers le RAW et un `.xmp` minimal (réglages Camera Raw du neutre LR,
   seul le réglage testé changé). Lightroom affiche bien les fichiers mais **ne
   lit pas ces `.xmp`** : il applique ses réglages par défaut (Adobe Color,
   optique active, accentuation 40, bruit 25 ; curseurs Lumières à 0). Les
   exports du premier lot sont donc inutilisables. Les mêmes lots restent
   exploitables par Camera Raw (Photoshop, Bridge), qui lit les `.xmp` annexes
   des RAW ; à vérifier sur deux images avant tout lot complet.
2. **Camera Raw scripté dans Photoshop**, avec la version d'essai disponible,
   pour les validations des autres fonctionnalités. Vérifier d'abord l'égalité
   avec les exports LR sur quelques neutres. L'API cloud Lightroom
   (Firefly Services) est écartée : offre entreprise, sortie TIFF 16 bits non
   documentée, RAW envoyés en ligne.
