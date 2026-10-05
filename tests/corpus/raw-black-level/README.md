# Corpus local — niveaux de noir CR3

## Contenu et confidentialité

Ce corpus documente les RAW présents dans les deux dossiers fournis pour valider le repli de niveau noir CR3 Canon :

| Étiquette du manifeste | Dossier source local | RAW recensés |
|---|---|---:|
| `25-08-26` | `C:\Users\victo\Pictures\RAW\25-08-26` | 83 CR3 |
| `05-08-26` | `C:\Users\victo\Pictures\RAW\05-08-26` | 120 CR3 |
| **Total** | | **203 CR3** |

Les photographies ne sont ni copiées ni modifiées par le projet. Le manifeste ne contient que les noms de fichiers et les métadonnées nécessaires au diagnostic (boîtier, objectif, prise de vue, dimensions, niveaux noir/blanc, résultat de l'estimation). Il ne lit pas les aperçus JPEG, les coordonnées GPS, les personnes ou le contenu visuel. Les chemins racines ne sont pas stockés dans le CSV : les étiquettes identifient les deux dossiers indiqués ci-dessus.

La présence d'un fichier dans l'inventaire ne signifie pas que son rendu a été comparé à une référence visuelle. Les mesures de l'estimateur sont des vérifications de cohérence du niveau noir, pas une certification de rendu colorimétrique.

## Résultat de l'inventaire du 28 septembre 2026

Les **203/203** fichiers ont été ouverts et décompressés avec LibRaw, sans erreur. Tous rapportent un Canon EOS R6 Mark III, réparti entre deux objectifs :

| Objectif | Fichiers |
|---|---:|
| Canon RF 24–105 mm F2.8 L IS USM Z | 173 |
| Sigma 70–200 mm F2.8 DG OS HSM Sports 018 | 30 |

Les sensibilités couvrent ISO 100 à ISO 64 000. Le repli estime un niveau pour les quatre positions CFA dans **tous les fichiers**, entre 512 et 2081 codes. Les 101 images ISO 100 ont une médiane de 512 pour les quatre canaux ; ce niveau représente environ 3,1 % du maximum LibRaw (16383). Cette observation a conduit à abaisser la borne basse de garde de 5 % à 2 %. La garde supérieure reste à 25 %.

Le corpus élargit la vérification à plusieurs scènes, sensibilités, objectifs et séries de prise de vue, mais **ne couvre qu'un modèle de boîtier**. Il ne permet donc pas encore d'étendre sans réserve le comportement à d'autres appareils Canon ou à d'autres fabricants.

## Fichiers

- `raw-black-level-inventory.csv` : inventaire généré par le probe LibRaw, avec une ligne par RAW lisible et un statut de décodage/estimation.
- `../../raw_black_level_corpus_probe.cpp` : outil qui régénère l'inventaire en parcourant un ou plusieurs répertoires.
- `../../../RawBlackLevelEstimator.cpp` : algorithme pur testé unitairement et réutilisé par la production.
- `../../raw_black_level_estimator_test.cpp` : tests synthétiques des gardes et des résultats.

## Régénérer le manifeste sous WSL

Depuis la racine du dépôt, après configuration avec LibRaw :

```sh
cmake --build build-wsl-libraw --target rawBlackLevelCorpusProbe
./build-wsl-libraw/rawBlackLevelCorpusProbe \
  25-08-26 /mnt/c/Users/victo/Pictures/RAW/25-08-26 \
  05-08-26 /mnt/c/Users/victo/Pictures/RAW/05-08-26 \
  > tests/corpus/raw-black-level/raw-black-level-inventory.csv
```

Le probe ouvre et décompresse chaque RAW séquentiellement afin d'inspecter la mosaïque et les marges masquées, sans effectuer le dématriçage couleur ni appliquer de profil DCP/LCP. L'exécution peut durer plusieurs minutes pour plusieurs centaines de CR3. Les fichiers sources restent en lecture seule.

Pour un corpus différent, remplacer ou ajouter des paires `étiquette répertoire`. Les sous-répertoires sont parcourus ; les extensions CR2, CR3 et DNG sont inventoriées. Les formats autres que CR3 Canon doivent obtenir `not_applicable` pour ce repli.

## Interprétation du statut

- `estimated` : CR3 Canon sans niveau noir/motif utilisable ; niveaux médians estimés séparément pour les quatre positions CFA.
- `not_applicable` : autre format/fabricant, métadonnées noir existantes ou aucune raison d'activer le repli.
- `invalid_input` : CFA ou géométrie de marges invalide.
- `insufficient_samples` : moins de 64 échantillons d'une position CFA dans les marges.
- `implausible_level` : au moins un niveau estimé hors de la plage de garde 2–25 % du niveau blanc déclaré.
- `decode_error` : LibRaw n'a pas pu ouvrir ou décompresser le fichier ; consulter `decode_status`.

Le statut `estimated` attendu dépend des tags réellement décodés pour chaque fichier. Il ne faut pas forcer l'estimation sur les CR3 qui fournissent déjà leur niveau noir.
