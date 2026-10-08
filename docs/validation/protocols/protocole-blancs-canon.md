# Protocole — Normalisation des niveaux blancs Canon

Date : 7 octobre 2026. Analyse associée : [rapport évolutif unique](../analyses/analyse-base-colorimetrique.md).

**Statut :** exécuté sur les six RAW, trente variantes. **Périmètre :** diagnostic local, aucun
changement du moteur de production et aucune publication des photographies.

## Objectif en langage simple

Un RAW stocke des nombres proportionnels au signal reçu par le capteur.
Le noir est le nombre mesuré sans lumière ; le blanc est le niveau utilisé
pour normaliser le signal. Un blanc trop élevé peut assombrir le rendu ;
un blanc trop bas peut le rendre plus clair mais faire perdre des détails
dans les hautes lumières. Ce blanc n'est **ni la balance des blancs/Kelvin,
ni le curseur Blancs de l'interface**.

La v3 a corrigé la précision sRGB et l'estimation du noir optique, mais
IPR reste plus sombre que Lightroom. Nous testons maintenant si les niveaux
blancs candidats des métadonnées Canon expliquent une partie de cet écart.
Nous ne cherchons pas à éclaircir artificiellement chaque image pour la
faire ressembler à Lightroom.

## Modèle et hypothèse

Pour un canal capteur, une normalisation simplifiée est :

```text
signal_normalisé = (signal_RAW − noir) / (blanc − noir)
gain_théorique = (blanc_A − noir) / (blanc_B − noir)
écart_EV_théorique = log2(gain_théorique)
```

Le pipeline réel ajoute gains WB, dématriçage, transformations DCP, LCP et
gestion du gamut. Cette formule prédit une direction et un ordre de
grandeur, pas une égalité de toutes les valeurs RGB finales.

LibRaw peut également réduire son `maximum` selon le maximum observé dans
l'image (`data_maximum`). Il faut distinguer ce mécanisme du niveau blanc
issu des métadonnées : nous testons donc les deux séparément.

## Entrées et éléments constants

Les six CR3 existants : 6V1A0007, 0037, 0043, 0327, 9421 et 9950 ; mêmes
empreintes que l'[audit indépendant Canon](../raw/audit-canon-metadata-20261007.json).
9421 est la scène ISO 3200. Références : TIFF IPR v3 et TIFF Lightroom
sRGB 16 bits déjà présents dans `tests/image/base/neutre`.

Tous les essais repartent d'un nouveau décodage du même RAW. Constants :
mesure du noir optique corrigée, gains WB As Shot, Kelvin estimé, DCP,
LCP, dématriçage, dimensions, absence d'auto-brightness, sortie sRGB
analytique 16 bits. Aucun curseur utilisateur, gain d'exposition ou
nouvelle courbe tonale n'est ajouté. Les niveaux blancs sont modifiés
**uniquement en mémoire dans la sonde**, jamais dans le RAW ou l'application.

Les profils réellement trouvés sont consignés. La sonde doit reproduire
le lot v3 : si le témoin ne correspond pas, ne pas attribuer une amélioration
au blanc avant d'avoir expliqué cette différence.
Les photographies restent dans le corpus local fourni par l'utilisateur ;
ce protocole n'établit ni ne transfère leurs droits de redistribution.

## Variantes : cinq décodages par scène

| Variante | Blanc avant soustraction du noir | Ajustement automatique du maximum |
| --- | --- | --- |
| `baseline` | Valeur actuelle de LibRaw | Valeur par défaut actuelle |
| `full_fixed` | 16383, témoin 14 bits | Désactivé |
| `normal_auto` | Blanc normal candidat Canon | Valeur par défaut actuelle |
| `normal_fixed` | Blanc normal candidat Canon | Désactivé |
| `specular_fixed` | Blanc spéculaire candidat Canon | Désactivé |

Les candidats sont lus dans ColorData version 66, mots 0x294/0x295 :
13200/13995 pour cinq scènes et 13535/14351 pour 9421. Ils restent
**non certifiés** : leur présence et leur plausibilité ne constituent pas
une spécification complète Canon. « Normal » et « spéculaire » sont ici
des interprétations de travail ; nous ne les intervertissons pas.

La sonde renseigne les deux copies `color.maximum` et `rawdata.color.maximum`
après unpack : LibRaw restaure la seconde au début de `raw2image`.
Elle n'utilise pas `user_sat`, appliqué après soustraction du noir dans
LibRaw 0.22.2, pour éviter une ambiguïté d'unité. Le callback avant
`scale_colors` observe le maximum réellement utilisé sans le modifier.

## Mesures et lecture des résultats

1. Contrôler les empreintes RAW, les gains WB et les profils constants.
2. Tracer `maximum` avant décodage, après noir, avant traitement, avant
   mise à l'échelle et après traitement ; noter `data_maximum` et le seuil.
3. Comparer les RGB caméra et sRGB linéaires DCP sur une grille native de
   pas 8, après application LCP pleine résolution. Mesurer gain moyen,
   valeurs négatives, dépassements de 1 et bornage.
4. Comparer la sortie sRGB encodée au témoin et aux TIFF v3/LR : ΔE00
   moyen/p95, MAE RGB, luminance Y moyenne, gris et clipping des canaux.
5. Examiner spécialement les pixels de hautes lumières sélectionnés sur
   le témoin, avec **le même masque pour toutes les variantes**. Un gain
   de luminosité n'est pas un succès s'il écrête ces détails.

Un ΔE00 plus faible indique une plus grande proximité au rendu Lightroom,
**pas automatiquement une meilleure fidélité physique**. Le ratio des
moyennes Y exprimé en EV n'est pas un réglage à appliquer au logiciel.
Ne pas confondre un canal nul avec un pixel entièrement noir.

## Décision attendue

Une variante mérite une étude supplémentaire si son effet correspond à
la normalisation attendue, reste cohérent entre scènes et n'introduit pas
un clipping disproportionné. Une amélioration uniquement due à
l'ajustement automatique doit être attribuée à ce mécanisme, pas à une
lecture correcte des métadonnées. Aucun niveau blanc ne sera intégré
automatiquement au moteur sur le seul critère de proximité Lightroom.
Critère préalable de validité : écart maximal du témoin par rapport à v3
≤1 code RGB sur 65535, empreintes RAW conformes et aucun changement de
WB/profils/géométrie entre variantes. Les métriques de clipping et ΔE00
sont ensuite des diagnostics, pas des seuils de fidélité certifiée.

Limites : seulement six scènes d'un boîtier, sans charte physique ; WB/LCP
Lightroom pas complètement harmonisés ; correction géométrique résiduelle
et comparaison de 0/1 pixel uniquement. Une scène non saturée ne permet
pas de mesurer directement la saturation physique du capteur.

## Exécution

Depuis la racine du projet, avec les DLL Qt/OpenCV/LibRaw accessibles :

```powershell
cmake --build build-windows-msvc --config Release --target tonalBaseProbe -j2
.\build-windows-msvc\Release\tonalBaseProbe.exe --white-levels `
  docs/validation/raw/audit-canon-metadata-20261007.json `
  build-canon-blancs-ab-20261007
```

Puis, sous WSL/Linux, depuis la même racine :

```bash
python3 tests/analyse_canon_white_levels.py \
  --probe build-canon-blancs-ab-20261007 \
  --reference docs/validation/raw/mesure-base-colorimetrique-v3-20261007.json \
  --output docs/validation/raw/mesure-blancs-canon-ab-20261007.json
python3 -m unittest discover -s tests -p test_analyse_base_colorimetrique.py
```

Les sorties existantes sont refusées. Choisir de nouveaux noms pour refaire
une campagne. Les flottants locaux restent dans `build-*` ; les mesures
de synthèse sont conservées dans `docs/validation/raw`, et la conclusion
est ajoutée au rapport unique. Aucun nouvel export utilisateur nécessaire.

## Sources techniques

- [Sonde et paramètres réellement appliqués](../../../tests/tonal_base_probe.cpp).
- [Décodeur et mesure du noir de production](../../../src/raw/RawDecoder.cpp).
- LibRaw 0.22.2 installé : `src/postprocessing/dcraw_process.cpp`,
  `src/preprocessing/raw2image.cpp`, `src/utils/utils_libraw.cpp` : ordre
  soustraction du noir, `adjust_maximum`, `user_sat`, `scale_colors`.
- [Audit des champs Canon et limites d'interprétation](../raw/audit-canon-metadata-20261007.json).
- [Analyse v3 et bibliographie CIEDE2000](../analyses/analyse-base-colorimetrique.md).

## Résultat de la première exécution

Le témoin reproduit exactement les grilles des six TIFF v3. Le blanc normal
augmente Y de 15,06 à 25,20 %, le spéculaire de 8,34 à 17,79 %. Le rendu
reste plus sombre que Lightroom et le clipping augmente sur 0037/0043/9421.
Aucune variante n'est intégrée au moteur : les candidats expliquent une
partie de l'écart, pas sa totalité, et leur sémantique reste à confirmer.

[Mesures conservées](../raw/mesure-blancs-canon-ab-20261007.json) et
[conclusion détaillée, section 17](../analyses/analyse-base-colorimetrique.md).
Contrôles : 20 tests analyseur, 10 tests sélection et 3 suites C++ ciblées
réussis ; seul l'exécutable de diagnostic a été recompilé.
