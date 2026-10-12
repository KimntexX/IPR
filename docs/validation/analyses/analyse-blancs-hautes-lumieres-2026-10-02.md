# Analyse des réglages Blancs et Hautes lumières

**Date :** 2 octobre 2026  
**Corpus :** huit scènes neutres de `tests/image/base/neutre`, comparées aux exports `-100` et `+100` des dossiers `tests/image/base/blanc` et `tests/image/base/hl`.

## Méthode

- Chaque moteur est comparé à son propre TIFF neutre `EV0`, afin de ne pas confondre une différence de rendu de base avec l'action du curseur.
- Les TIFF sRGB sont sous-échantillonnés uniformément puis mesurés en luminance linéaire sRGB.
- Les zones basse, médiane et haute correspondent respectivement aux quantiles `0–20 %`, `40–60 %` et `80–100 %` de l'image neutre.
- Les ratios indiquent la luminance de `-100` ou `+100` divisée par celle de l'état neutre. La mesure brute est disponible dans `mesure-blancs-hautes-lumieres-brut-2026-10-02.txt` et peut être rejouée avec `tests/analyse_tonal_adjustments.py blanc hl`.

## Résultats

| Réglage / moteur (8 scènes) | Basses `-100` / `+100` | Médianes `-100` / `+100` | Hautes `-100` / `+100` | Pixels `Y ≤ 0,001` : `0` / `-100` / `+100` |
|---|---:|---:|---:|---:|
| Blancs Lightroom | `0,921` / `4,031` | `0,816` / `2,883` | `0,818` / `1,926` | `2,230 %` / `2,255 %` / `1,205 %` |
| Blancs IPR | `1,000` / `1,000` | `1,000` / `1,000` | `0,984` / `1,077` | `0,317 %` / `0,317 %` / `0,317 %` |
| Hautes lumières Lightroom | `0,825` / `1,573` | `0,753` / `1,575` | `0,598` / `1,527` | `2,230 %` / `3,892 %` / `1,206 %` |
| Hautes lumières IPR | `1,000` / `1,000` | `1,000` / `1,000` | `0,997` / `1,063` | `0,317 %` / `0,317 %` / `0,317 %` |

## Diagnostic

Les réglages IPR sont fonctionnels, mais insuffisamment étendus sur ce corpus :

- **Blancs** ne modifie ni les basses ni les médianes ; même la zone haute ne varie que de `-1,6 %` à `+7,7 %`, contre environ `-18 %` à `+93 %` dans Lightroom.
- **Hautes lumières** est encore plus retenu : les basses et médianes restent inchangées et la zone haute varie seulement de `-0,3 %` à `+6,3 %`, contre `-40 %` à `+53 %` dans Lightroom.
- Le taux de pixels proches du noir ne change pas dans IPR, ce qui est attendu pour des contrôles de haut de courbe. Le problème n'est donc pas un clipping noir, mais une activation trop tardive et une amplitude trop faible dans le haut du signal.

Dans le moteur actuel, les masques démarrent à des luminances linéaires élevées : Hautes lumières commence à `0,35` et Blancs à `0,60` (sur le canal maximal). Une grande partie des tons que Lightroom considère encore comme récupérables ou éclaircissables n'active donc pratiquement pas le traitement IPR.

## Recommandation

Ne pas étendre les corrections aux noirs ou aux ombres, qui sont validés. Corriger séparément les deux contrôles du haut de courbe :

1. **Hautes lumières :** avancer le début du masque, puis employer une épaule de compression plus forte à `-100` et une levée bornée à `+100`. Cette courbe doit commencer à agir dans les tons moyens supérieurs tout en conservant les noirs et les couleurs saturées.
2. **Blancs :** conserver une action plus haute que Hautes lumières, mais abaisser son seuil d'activation et dissocier davantage son épaule de la récupération des Hautes lumières. Il doit pouvoir déplacer le point blanc sans créer de voile global.
3. Ajouter une régression synthétique avec un dégradé couvrant les luminances `0,10` à `1,00`, afin de verrouiller la localisation et l'amplitude de chaque curseur avant de régénérer les huit TIFF.

La colorimétrie doit attendre cette correction : une récupération insuffisante des hautes lumières peut modifier la saturation apparente et fausser la comparaison de teinte, de profil DCP ou de balance des blancs.

## Correctif v2 appliqué

Le moteur a été corrigé avant la nouvelle génération :

- le masque **Hautes lumières** passe de `smoothStep(0,35, 0,95, Y)` à `smoothStep(0,08, 0,70, Y)` ; son levier positif rapproche progressivement le ton du blanc (amplitude maximale `0,70`) et son levier négatif réduit localement la luminance (amplitude maximale `0,52`) ;
- le masque **Blancs** passe de `smoothStep(0,60, 1,00, max(R,G,B))` à `smoothStep(0,10, 0,65, max(R,G,B))` ; il possède désormais une levée du point blanc (amplitude `0,95`) et une récupération négative plus retenue (amplitude `0,22`) ;
- les deux contrôles sont séparés et se composent uniquement si l'utilisateur les règle ensemble.

Une régression synthétique couvre désormais les quatre extrêmes : Hautes lumières et Blancs doivent modifier un ton clair, conserver le noir et préserver l'ordre des valeurs proches du blanc. `imageExportTests` a été exécuté avec succès après la correction.

Les 16 exports IPR v1 de chacun des dossiers `blanc` et `hl` ont été archivés dans `archive-ipr-pre-correction-v1-2026-10-02`. Les TIFF Lightroom restent disponibles pour la comparaison v2.

## Mesure v2

Les nouveaux exports IPR v2 couvrent les huit scènes. La mesure brute est conservée dans `mesure-blancs-hautes-lumieres-v2-brut-2026-10-02.txt`.

| Réglage / moteur (8 scènes) | Basses `-100` / `+100` | Médianes `-100` / `+100` | Hautes `-100` / `+100` | Pixels `Y ≤ 0,001` : `0` / `-100` / `+100` |
|---|---:|---:|---:|---:|
| Blancs Lightroom | `0,921` / `4,031` | `0,816` / `2,883` | `0,818` / `1,926` | `2,230 %` / `2,255 %` / `1,205 %` |
| Blancs IPR v2 | `1,000` / `1,000` | `0,966` / `1,377` | `0,860` / `1,477` | `0,317 %` / `0,317 %` / `0,317 %` |
| Hautes lumières Lightroom | `0,825` / `1,573` | `0,753` / `1,575` | `0,598` / `1,527` | `2,230 %` / `3,892 %` / `1,206 %` |
| Hautes lumières IPR v2 | `1,000` / `1,000` | `0,963` / `1,132` | `0,704` / `1,215` | `0,317 %` / `0,317 %` / `0,317 %` |

### Verdict v2

Le correctif v2 est une amélioration nette : l'action positive dans la zone haute passe de `+7,7 %` à `+47,7 %` pour Blancs, et de `+6,3 %` à `+21,5 %` pour Hautes lumières. Les branches négatives sont désormais proches du comportement attendu dans cette zone (`0,860` contre `0,818` pour Blancs ; `0,704` contre `0,598` pour Hautes lumières).

La validation reste **partielle**. Les levées positives demeurent trop faibles par rapport à Lightroom, surtout dans les tons moyens : Blancs IPR v2 atteint `×1,377` contre `×2,883`, et Hautes lumières `×1,132` contre `×1,575`. Le maintien des basses lumières à l'identique est en revanche volontaire et préférable à une levée globale qui empiéterait sur les réglages Ombres/Noirs.

La prochaine correction, si l'objectif est de se rapprocher de Lightroom sur toute la plage utile, doit uniquement avancer et renforcer les **branches positives** des deux masques. Les branches négatives et les seuils protégeant les basses lumières peuvent être conservés. Les exports v2 restent en place pour servir de référence avant cette éventuelle v3.

## Correctif v3 appliqué

La v3 ne modifie pas les branches négatives validées en v2. Elle introduit deux masques dédiés à la levée positive :

- Hautes lumières `+100` : `smoothStep(0,02, 0,50, Y)` au lieu d'utiliser le masque de récupération `0,08–0,70` ;
- Blancs `+100` : `smoothStep(0,02, 0,50, max(R,G,B))` au lieu d'utiliser le masque de récupération `0,10–0,65`.

Ces seuils élargissent exclusivement la levée dans les tons moyens et clairs ; les noirs restent hors masque et les courbes `-100` demeurent identiques à v2. Une nouvelle génération IPR est requise pour la validation quantitative.

## Validation v3

La v3 a été mesurée sur les huit scènes. Les valeurs `-100` proviennent des archives v2, puisque leurs branches sont inchangées ; les valeurs `+100` sont les nouveaux TIFF v3. La mesure brute est disponible dans `mesure-blancs-hautes-lumieres-v3-brut-2026-10-02.txt`.

| Réglage / moteur (8 scènes) | Basses `-100` / `+100` | Médianes `-100` / `+100` | Hautes `-100` / `+100` |
|---|---:|---:|---:|
| Blancs Lightroom | `0,921` / `4,031` | `0,816` / `2,883` | `0,818` / `1,926` |
| Blancs IPR v3 | `1,000` / `1,177` | `0,966` / `2,074` | `0,860` / `1,903` |
| Hautes lumières Lightroom | `0,825` / `1,573` | `0,753` / `1,575` | `0,598` / `1,527` |
| Hautes lumières IPR v3 | `1,000` / `1,066` | `0,963` / `1,528` | `0,704` / `1,474` |

### Verdict v3

**Hautes lumières est validé.** À `+100`, IPR v3 atteint `×1,528` dans les tons moyens et `×1,474` dans la zone haute, contre `×1,575` et `×1,527` dans Lightroom. L'écart est inférieur à `4 %` sur les deux plages utiles. Le bas de courbe reste protégé.

**Blancs est validé comme réglage de point blanc.** La zone haute atteint `×1,903`, contre `×1,926` dans Lightroom (écart inférieur à `2 %`). Les tons moyens et sombres sont moins levés qu'avec Lightroom ; c'est un choix intentionnel qui évite de transformer Blancs en réglage d'exposition ou d'Ombres. La récupération négative v2 demeure volontairement plus protectrice du détail que Lightroom.

Les quatre contrôles tonals Noirs, Ombres, Hautes lumières et Blancs sont désormais validés quantitativement sur ce corpus. Le test suivant peut porter sur le Contraste.

## Revalidation Camera Neutral — 9 octobre 2026

La [campagne complète Lumières](analyse-validation-lumieres-base-2026-10-02.md#campagne-complete-lumieres--nouveaux-neutres-9-octobre-2026)
est complète : six scènes, douze variantes par contrôle et moteur, TIFF natifs
16 bits, profils Camera Neutral/WB As Shot LR audités. Les verdicts v3 ci-dessus
restent ceux de leur ancien corpus, pas une certification du pipeline actuel.

Direction et ciblage IPR cohérents aux deux extrêmes, sans inversion mesurée.
Les hautes présentent les ratios médians −100/+100 suivants : Blancs
**0,841/2,396**, HL **0,747/1,896**. Les basses sont largement préservées.

**Qualité positive à revalider :** le taux moyen de pixels avec au moins un
canal saturé vaut **18,877 % IPR contre 4,746 % LR** pour Blancs +100, et
**5,622 % contre 0,311 %** pour HL +100. Ce n'est pas une mesure de saturation
du capteur. Blancs +100 atteint 41,97 % sur 0007, 32,07 % sur 9950 et 31,16 %
sur 9421. Le réglage fonctionne, mais son extrême/gamut mérite investigation.

Priorité : sonde RGB avant encodage/clipping et tests ±50, puis recette des
zones claires colorées. Borner Y ne garantit pas que chaque canal soit borné.
Pas de correctif de production appliqué lors de cette analyse. Ne pas modifier
la loi EV pour compenser cette limite de rendu. CSV et courbes dans le rapport lié.
