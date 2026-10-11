# Protocole — Réduction de bruit

Version 1 — 10 octobre 2026. Fusion du protocole initial et de la revalidation
v2. Statut : recette prête ; historique v2 fonctionnel, qualité à poursuivre.
Appliquer le [socle commun](protocole-commun.md).

## Corpus et conditions

0043/0327 pour détail faible ISO ; 0624 pour pelage/sangle ; 9890 pour aplats
bruités ; 0112 pour ciel sombre ; 9421/1020 ISO 3200, 9734 ISO 8000 pour peau,
textile et lumière mixte. Contrôler disponibilité et bruit du neutre à 100 %,
pas l'ISO seul. 9950/1020 sont les identifiants confirmés des anciens noms
annoncés 1050/9920. ISO 6400 exact n'est pas obligatoire.

Photo complémentaire utile : même scène fixe à ISO 1600/3200/6400/12800,
trépied, même ouverture/cadrage/lumière, temps ajusté sans bougé ; aplat,
ombre non noire, tissu/cheveux, bord net et plages colorées voisines. Pas de
débruitage longue pose qui changerait la capture. Ces prises ne donnent pas
automatiquement une vérité sans bruit.

## Séries

Neutre : luminance 0, couleur 0, gain de netteté 0, IA inactive.
Luminance seule puis chrominance seule : **0/25/50/75/100**. Sur 0043/9890,
ajouter 1/5/10 pour l'activation. Paramètres LR secondaires fixes et enregistrés.
Tester ensuite séparément composantes combinées et couplage bruit/netteté.
Une série LR IA ou un DNG déjà amélioré est une autre approche, pas un témoin
de curseur classique ; absence de balise IA ne prouve pas l'absence d'IA.

Après correctif, exporter un nouveau neutre et vérifier ses différences avec
l'ancien avant d'attribuer un changement au débruitage. Vérifier aperçus,
exports pleine résolution, restauration et toutes les valeurs de support.

## Mesure native

Pixels adjacents 16 bits, pas décimation spatiale ni image redimensionnée.
L'analyseur sélectionne 80 candidats de 256×256, puis six régions peu
structurées et quatre structurées distinctes, coordonnées fixes par série et
moteur. Exclure Y moyen hors `]0,001;0,8[` ou clipping excessif. Les aplats
automatiques restent à confirmer visuellement.

Convertir sRGB linéaire ; Y Rec.709, `Co=R−B`, `Cg=G−(R+B)/2`.
Proxy haute fréquence :

```text
HH = (p00 − p01 − p10 + p11)/2
sigma = median(|HH − median(HH)|)/0,67448975
sigma_C = sqrt((sigma_Co² + sigma_Cg²)/2)
```

Le détail Haar est de norme quadratique unité ; le MAD normalisé suppose une
distribution gaussienne. Dématriçage et débruitage corrèlent le bruit : ce proxy
de sortie n'est pas une mesure exacte du capteur. Mesurer ratios au neutre
propre, dérive Y/Co/Cg, gradient et détail par ROI. Le gradient après deux
flous `[1,4,6,4,1]/16` sur masque fort du neutre (quantile 85 %) aide au diagnostic,
mais une diminution peut enlever bruit ou texture : vérifier à 100 %.

Critères : effet/progressivité du curseur, neutralité à zéro, réduction du
proxy sans perte visuelle inacceptable de texture, halos, saignement chromatique
ni désaturation excessive. Fixer les tolérances de qualité avant mesure.
Pas de PSNR/fidélité absolue sans référence propre ; MAE au neutre mesure le
changement, pas l'erreur. Les ROI automatiques par moteur ne sont pas homologues :
pas de MAE pixel-à-pixel LR/IPR ni de classement absolu sur leurs seuls ratios.

## Outils / mappings

```bash
python3 -m unittest discover -s tests -p test_analyse_noise.py
python3 tests/analyse_noise.py --root tests/image/base --output docs/validation/raw/inventaire-bruit-vNNN-NOUVEL-HORODATAGE.json
python3 tests/analyse_noise.py --root tests/image/base --analyse --output docs/validation/raw/mesure-bruit-vNNN-NOUVEL-HORODATAGE.json
```

Le comparateur lit les séries historiques quatre états : fichier sans suffixe
25, `-2` 50, `-3` 75, `-4` 100, **pas** neutre. Réexports de collision non
renommés automatiquement. Associations explicites :

- `--neutral-file IMAGE_ID IPR CHEMIN` (ou `LR`).
- `--variant-file IMAGE_ID COMPOSANTE LR NUMERO CHEMIN`, numéro 1–4 ; la
  valeur XMP observée reste auditable même si le nom est erroné.
- `--extra-state IMAGE_ID luminance IPR VALEUR CHEMIN` pour 1/5/10.
- `--image-id` limite les scènes ; `--previews DOSSIER` produit des aperçus
  diagnostiques 8 bits, jamais utilisés comme métriques.

Un neutre manquant peut conduire l'outil à une ancre non nulle diagnostique :
cela **ne valide pas** une réponse à zéro. Les états invalides gardent
`state_valid=false`. L'analyse refuse d'écraser une mesure et contrôle les
fichiers pendant la lecture. Le manifeste générateur nécessite des associations
explicites, il n'est pas importé automatiquement.

L'ancien [lanceur v2](../raw/analyse-bruit-v2-2026-10-05.sh) conserve les
mappings de la campagne du 5 octobre (suffixes IPR 5–8 et LR 9890 corrigés).
Il sert à l'historique, pas à certifier la présence actuelle de ces pixels.
Rapport unique courant : [analyse Bruit](../analyses/analyse-bruit.md).
Clôture et nettoyage : socle commun. La [netteté](protocole-nettete.md)
est une recette distincte, puis un test de couplage.

## Références de la méthode consolidée

Références conservées du protocole initial (pas une nouvelle revue documentaire) :

- Donoho et Johnstone, *Ideal spatial adaptation by wavelet shrinkage*, 1994,
  [DOI](https://doi.org/10.1093/biomet/81.3.425). Le proxy Haar ne réimplémente
  pas à lui seul cet algorithme complet.
- [Adobe — débruitage manuel et IA](https://blog.adobe.com/en/publish/2023/04/20/lowdown-using-high-iso-settings-in-photography),
  pour la distinction des approches ; les possibilités exactes du logiciel
  utilisé doivent être auditées dans la campagne, pas déduites d'un article ancien.
