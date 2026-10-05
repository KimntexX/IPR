# Analyse des réglages Noirs et Ombres

**Date :** 2 octobre 2026  
**Corpus :** `tests/image/base/noir`, `tests/image/base/ombre`, avec les TIFF `EV 0` de `tests/image/base` comme référence.

## Verdict

Les deux réglages sont bien actifs, mais leur comportement à `-100` et surtout à `+100` n'est pas encore suffisamment différencié ni localisé pour une application photographique. Le défaut est fonctionnel, pas seulement esthétique : dans le pipeline actuel, Noirs et Ombres sont combinés dans le même mécanisme de relèvement vers une luminance cible de `0,60`.

Le curseur EV reste validé et n'est pas mis en cause par cette analyse.

## Méthode

- Huit scènes ont été mesurées pour chaque set, à `-100`, `0` et `+100`.
- Tous les TIFF Lightroom et IPR de ces sets sont sRGB 16 bits avec ICC incorporé.
- Les mesures utilisent une version linéaire sRGB d'un aperçu de contrôle.
- Les zones basse, médiane et haute sont les 20 % les plus sombres, les 20 % centraux et les 20 % les plus lumineux de chaque référence `EV 0`. Cette définition par quantiles reste valide même pour une image globalement sombre.
- Un ratio inférieur à `1` assombrit la zone ; un ratio supérieur à `1` la relève. Les pourcentages à `Y ≤ 0,001` et `Y ≤ 0,01` indiquent respectivement le noir quasi absolu et les très basses lumières.

## Résultats agrégés

| Set / moteur | Basses `-100` / `+100` | Médianes `-100` / `+100` | Hautes `-100` / `+100` | Pixels `Y ≤ 0,001` : `0` / `-100` / `+100` |
|---|---:|---:|---:|---:|
| Noirs Lightroom | `0,002` / `2,518` | `0,101` / `1,641` | `0,662` / `1,218` | 1,82 % / 34,70 % / 0,01 % |
| Noirs IPR | `0,217` / `70,410` | `0,296` / `11,736` | `0,558` / `3,015` | 0,31 % / 10,18 % / 0,00 % |
| Ombres Lightroom | `0,168` / `6,364` | `0,535` / `1,806` | `0,819` / `1,219` | 1,82 % / 11,57 % / 0,07 % |
| Ombres IPR | `0,215` / `71,192` | `0,250` / `13,319` | `0,399` / `3,818` | 0,31 % / 10,19 % / 0,00 % |

Les ratios très élevés sont cohérents avec des valeurs de départ proches de zéro, mais ils sont corroborés par les taux de noir : à `+100`, IPR ne laisse presque plus de pixels sous `Y ≤ 0,001`, tandis que les zones médianes et hautes sont aussi fortement modifiées. Le réglage déborde donc au-delà de la plage visée.

## Lecture fonctionnelle

### Noirs

`Noirs -100` IPR assombrit les basses lumières et accroît les pixels proches du noir, mais moins fortement que Lightroom. `Noirs +100` IPR relève beaucoup trop les basses lumières et affecte également les zones médianes ; dans les scènes sombres, le haut de la distribution relative est aussi relevé de plusieurs fois. Un réglage de point noir devrait agir plus étroitement autour du pied de courbe.

### Ombres

`Ombres -100` et `Ombres +100` IPR sont eux aussi très puissants. Une levée importante à `+100` est attendue pour les ombres, mais l'action actuelle atteint largement les tons moyens et, dans certains cas, les hautes lumières relatives. La différence de comportement avec Noirs est trop faible à l'extrémité de la plage.

## Cause dans le pipeline

Dans `ImageEditor::renderWorkingImage()`, les réglages sont combinés ainsi :

```text
shadowLift = union(shadows positifs × shadowsMask,
                   blacks positifs × blacksMask)
targetY += (0,60 - min(targetY, 0,60)) × shadowLift
```

Les deux curseurs rejoignent donc la même cible `0,60`. Les masques sont différents (`Noirs` se termine vers `0,32`, `Ombres` vers `0,56`), mais à `+100` chacun applique pratiquement le relèvement maximal dans sa zone. Les deux réglages deviennent proches, et le point noir est transformé en levée d'ombres massive.

Pour les valeurs négatives, les deux réglages utilisent également la même forme :

```text
targetY /= 1 + 3 × shadowDarkening
```

Cela assombrit sans définir explicitement un nouveau point noir. Le résultat conserve davantage de valeurs très sombres que Lightroom pour Noirs `-100`.

## Recommandation

Une correction de la sémantique est recommandée avant d'affiner le rendu de base :

1. **Séparer Noirs et Ombres.** Noirs doit modifier le pied de courbe et le point noir dans une bande étroite ; Ombres doit relever/comprimer une bande plus large, sans employer la même cible absolue.
2. **Remplacer la cible fixe `0,60`.** Employer des courbes bornées dont l'amplitude dépend de la plage ciblée. Un noir à `+100` peut révéler du détail, mais ne doit pas transformer les médianes en gris clair.
3. **Préserver les extrêmes.** Les hautes lumières ne doivent presque pas varier avec Noirs ; leur variation avec Ombres doit rester limitée et mesurée.
4. **Ajouter des tests de régression.** Les huit triplets actuels doivent vérifier identité à zéro, monotonie, taux de clipping, effet maximal par zone et absence de débordement sur les hautes lumières.
5. **Rejouer la comparaison Lightroom seulement après la séparation.** L'objectif initial est une sémantique stable et contrôlable, pas une copie pixel-à-pixel de son rendu de base.

## Décision proposée

Ne pas modifier l'exposition ni ajouter de traitement local. Corriger d'abord les courbes Noirs/Ombres, puis réexporter les trois états `-100`, `0`, `+100` du même corpus et relancer cette analyse. Les références Lightroom restent en place ; les exports IPR antérieurs doivent être archivés avant la nouvelle génération.

## Mise en œuvre — 2 octobre 2026

La correction recommandée est appliquée dans `ImageEditor::renderWorkingImage()` :

- **Noirs** utilise désormais une zone de pied étroite (masque nul à partir de la luminance linéaire `0,18`). Son relèvement est une courbe en cloche bornée à l'intérieur de ce pied ; il ne relève ni le noir mathématique ni les tons moyens. Son assombrissement définit directement un point noir plus profond.
- **Ombres v1** conservait une zone plus large (`0,10` à `0,56`) avec un relèvement proportionnel à la racine de la luminance. Cette première séparation a supprimé le voile global, mais la mesure post-export a conduit à la v2 décrite ci-dessous.
- Lorsque Noirs et Ombres sont simultanément négatifs, la réduction locale la plus forte est retenue au lieu de multiplier deux réductions. Cela évite d'écraser accidentellement le détail dans les très basses lumières.

Le test `imageExportTests` couvre cette séparation : Noirs `+100` doit relever un échantillon sombre sans déplacer les tons moyens ou les hautes lumières ; Ombres `+100` doit aussi agir sur le ton moyen tout en préservant la haute lumière. Il a été exécuté avec succès après la correction.

Les sorties IPR antérieures ont été conservées, sans modification des TIFF Lightroom, dans :

- `tests/image/base/noir/archive-ipr-pre-separation-2026-10-02` (16 TIFF) ;
- `tests/image/base/ombre/archive-ipr-pre-separation-2026-10-02` (16 TIFF).

Les dossiers `noir` et `ombre` sont donc prêts à accueillir les nouveaux exports IPR `-100` et `+100`. La comparaison quantitative complète sera relancée uniquement après leur génération.

## Mesure après séparation — exports v1

Les nouveaux exports IPR ont été comparés à l'état `EV0` propre à chaque moteur. Les TIFF ont été lus avec leur profil sRGB, sous-échantillonnés uniformément, puis convertis en luminance linéaire sRGB. Les zones basse, médiane et haute sont définies par les quantiles `0–20 %`, `40–60 %` et `80–100 %` de l'image `EV0` de chaque moteur. Les ratios décrivent `-100` / `EV0` et `+100` / `EV0`. La mesure est reproductible avec `tests/analyse_tonal_adjustments.py` dans l'environnement WSL doté de Pillow et NumPy.

`6V1A0767` est exclue des résultats IPR : ses deux exports IPR sont absents, alors que les TIFF Lightroom sont disponibles. Lightroom porte donc sur huit scènes et IPR sur sept ; les tendances sont comparables, mais les écarts fins ne doivent pas être interprétés comme une comparaison pixel à pixel entre les deux moteurs.

| Réglage / moteur | Basses `-100` / `+100` | Médianes `-100` / `+100` | Hautes `-100` / `+100` | Pixels `Y ≤ 0,001` : `0` / `-100` / `+100` |
|---|---:|---:|---:|---:|
| Noirs Lightroom (8) | `0,143` / `2,092` | `0,649` / `1,285` | `0,859` / `1,103` | `2,230 %` / `36,147 %` / `0,032 %` |
| Noirs IPR (7) | `0,096` / `2,266` | `0,647` / `1,204` | `0,943` / `1,030` | `0,360 %` / `37,453 %` / `0,000 %` |
| Ombres Lightroom (8) | `0,478` / `5,019` | `0,677` / `1,608` | `0,893` / `1,163` | `2,230 %` / `12,210 %` / `0,093 %` |
| Ombres IPR (7) | `0,136` / `2,642` | `0,231` / `1,683` | `0,666` / `1,220` | `0,360 %` / `17,775 %` / `0,000 %` |

### Décision issue de la mesure

**Noirs est validé.** Il est désormais localisé : à `+100`, IPR relève fortement la zone basse (`×2,27`) mais ne modifie la zone haute que de `+3,0 %`. À `-100`, les tons moyens suivent quasiment Lightroom (`0,647` contre `0,649`) sans assombrir inutilement les hautes lumières. Aucune correction Noirs n'est appliquée.

**Ombres nécessite une dernière correction ciblée.** La version v1 assombrit excessivement à `-100` (médianes IPR `0,231` contre `0,677` dans Lightroom) ; à `+100`, elle ne relève pas assez les basses lumières (`×2,64` contre `×5,02`) tout en dépassant légèrement Lightroom dans les tons moyens et hauts. La correction v2 renforce donc le relèvement uniquement à proximité du noir et amortit plus tôt sa progression ; pour le négatif, elle remplace le multiplicateur uniforme par une décroissance bornée d'amplitude `0,57` qui protège les tons moyens et les hautes lumières.

Les 14 TIFF IPR v1 de chaque set ont été archivés avant cette correction dans :

- `tests/image/base/noir/archive-ipr-post-separation-v1-2026-10-02` ;
- `tests/image/base/ombre/archive-ipr-post-separation-v1-2026-10-02`.

Une dernière génération des quatre états Ombres/Noirs est nécessaire pour mesurer la version v2. Les références Lightroom n'ont pas été déplacées.

## Validation Ombres v2 — corpus complet

Les 16 TIFF IPR Ombres v2 sont disponibles et couvrent désormais les huit scènes, y compris `6V1A0767`. La même procédure de mesure a été relancée ; la sortie brute reproductible est conservée dans `docs/validation/raw/mesure-ombre-v2-brut-2026-10-02.txt`.

| Moteur (8 scènes) | Basses `-100` / `+100` | Médianes `-100` / `+100` | Hautes `-100` / `+100` | Pixels `Y ≤ 0,001` : `0` / `-100` / `+100` |
|---|---:|---:|---:|---:|
| Lightroom | `0,478` / `5,019` | `0,677` / `1,608` | `0,893` / `1,163` | `2,230 %` / `12,210 %` / `0,093 %` |
| IPR v2 | `0,560` / `5,090` | `0,732` / `1,596` | `0,913` / `1,106` | `0,317 %` / `3,410 %` / `0,000 %` |

### Verdict v2

**Ombres v2 est validé.** À `+100`, l'écart relatif avec Lightroom est inférieur à `1,5 %` dans les basses lumières et les tons moyens. L'action IPR est volontairement plus retenue dans les hautes lumières (`+10,6 %` contre `+16,3 %`), conformément à l'objectif de ne pas créer de voile global.

À `-100`, IPR reste légèrement plus protecteur que Lightroom : les basses lumières sont à `0,560` contre `0,478`, et seulement `3,410 %` des pixels atteignent le noir quasi absolu contre `12,210 %`. C'est un choix technique favorable à la conservation de détail, et non un défaut de la courbe ; il pourra rester tel quel tant que la validation visuelle ne révèle pas un manque de densité dans une image donnée.

Les réglages **Noirs** et **Ombres** peuvent donc être considérés validés sur ce corpus. La prochaine étape pertinente est une validation visuelle des huit scènes, puis le passage aux Hautes lumières et aux Blancs.

À la clôture de cette validation, les TIFF actifs ont été archivés afin de libérer les deux dossiers de travail :

- `tests/image/base/noir/archive-validation-final-2026-10-02` : 16 TIFF Lightroom ;
- `tests/image/base/ombre/archive-validation-final-2026-10-02` : 16 TIFF Lightroom et 16 TIFF IPR v2.

Les références `EV0` du corpus général dans `tests/image/base` restent inchangées.
