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
Luminance seule puis chrominance seule : **0/25/50/100** dans le générateur
courant ; 75 en complément manuel. Sur 0043/9890,
ajouter 1/5/10 pour l'activation. Paramètres LR secondaires fixes et enregistrés.
Tester ensuite séparément composantes combinées et couplage bruit/netteté.
Une série LR IA ou un DNG déjà amélioré est une autre approche, pas un témoin
de curseur classique ; absence de balise IA ne prouve pas l'absence d'IA.

Après correctif, exporter un nouveau neutre et vérifier ses différences avec
l'ancien avant d'attribuer un changement au débruitage. Vérifier aperçus,
exports pleine résolution, restauration et toutes les valeurs de support.

## Mesure native

Pixels adjacents 16 bits, pas décimation spatiale ni image redimensionnée.
L'analyseur unifié travaille sur des ROI natives adjacentes, jusqu'à 256×256.
Sans ROI déclarées, il prépare une grille 4×4 de candidats puis retient jusqu'à
six faibles gradients et trois détails distincts selon le neutre. Les coordonnées
sont fixes par référence, pas nécessairement homologues entre moteurs. Ces
choix ne sont pas ceux de l'ancien analyseur (80 candidats, six aplats/quatre
détails) : ne pas comparer leurs agrégats comme s'il s'agissait d'une même méthode.
Les aplats automatiques restent à confirmer visuellement.

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
python tests/analyse_bruit.py --base tests/image/base --strict --data-version 1
```

Les valeurs IPR proviennent des rendus en mémoire déclarés, les valeurs LR du
XMP. Chaque série doit avoir une référence propre au moteur. Références ambiguës,
géométrie différente ou état non auditable :
corriger l'entrée ou fournir un mapping explicite à l'analyseur.
Les ROI automatiques ne remplacent pas une sélection sémantique de zones
planes/texturées ; annoter les régions dans le mapping si nécessaire.
Les mémoires des campagnes précédentes restent des preuves, pas une API
de compatibilité à utiliser pour les nouveaux exports.
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
