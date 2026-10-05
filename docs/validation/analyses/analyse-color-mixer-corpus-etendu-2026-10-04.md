# Mélangeur — Vert complémentaire et autres couleurs

**Date :** 4 octobre 2026. **Périmètre :** vert sur 6V1A0037/6V1A0043 ; orange,
cyan et bleu sur 6V1A0327 ; violet et références magenta sur 6V1A0327/6V1A0043.
Cette campagne complète [la première analyse](analyse-color-mixer-red-yellow-green-2026-10-04.md).

## Conclusion

Les nouvelles images rendent le **vert suffisamment représenté** pour observer
sa réponse. Les trois commandes fonctionnent sur les deux scènes, mais la teinte
est beaucoup moins ample que Lightroom et la luminance +100 est beaucoup plus
forte. Il ne s’agit plus d’un résultat fondé sur quelques pixels de bruit.

Orange et cyan montrent aussi des réponses dans le bon sens, avec une luminance
sélective trop énergique **par rapport à la référence Lightroom choisie**. La
comparaison bleu est partielle : le TIFF Lightroom +100 de luminance reçu lors
de la mesure est en réalité marqué −100.

Violet et magenta ne sont pas encore deux bandes indépendantes dans IPR.
L’utilisateur confirme avoir utilisé le dernier cercle pour les exports violet ;
le code l’appelle `Magentas` et le centre moteur est 300°. Comparer ces exports
au `Purple` Lightroom n’est donc **pas une comparaison à bandes identiques**.
Les références Lightroom magenta sont mesurées, sans inventer d’exports IPR
absents. Le mélangeur complet ne peut pas être déclaré validé.

Le défaut déjà confirmé sur les gris en luminance rouge demeure à corriger.
Aucune modification du rendu, des bandes ou des sidecars n’est effectuée ici.

## Protocole et mesures conservées

Le [protocole](../protocols/protocole-validation-color-mixer-2026-10-04.md) garde
les paires (1,2) teinte, (3,4) saturation, (5,6) luminance, avec impair −100 et
pair +100. Les TIFF sans suffixe dans une couleur sont des variantes −100,
pas des neutres. Les neutres sont directement dans `tests/image/base`.

Mesures de référence pour cette campagne :

- [Vert 6V1A0037, v2](../raw/mesure-color-mixer-green-6V1A0037-2026-10-04-v2.json).
- [Vert/violet/magenta 6V1A0043](../raw/mesure-color-mixer-green-violet-magenta-6V1A0043-2026-10-04-v1.json).
- [Orange/cyan/bleu/violet/magenta 6V1A0327](../raw/mesure-color-mixer-orange-cyan-blue-violet-magenta-6V1A0327-2026-10-04-v1.json).
- [Bleu/violet 6V1A0327, v2](../raw/mesure-color-mixer-blue-violet-6V1A0327-2026-10-04-v2.json) :
  paires IPR violet complétées, référence LR bleu n°6 absente.
- [Violet 6V1A0043, v2](../raw/mesure-color-mixer-violet-6V1A0043-2026-10-04-v2.json) :
  dernière paire IPR complétée.
- [Audit du nouveau TIFF n°7](../raw/inventaire-export-supplementaire-6V1A0327-7-2026-10-04.json) :
  jaune +100 et Purple +100 simultanés, pas une référence bleu +100.

Les fichiers précédents restent conservés ; une régénération ultérieure d’un
TIFF ne modifie pas rétroactivement l’état enregistré dans ces mesures.

Le lecteur travaille en RGB natif 16 bits puis float32, sans conversion 8 bits
ni interpolation. Le pas est 5, soit 1 291 776 pixels réels par scène. Pour les
nouvelles passes, l’alignement privilégie le **neutre IPR** ; les quatre choix de
recadrage d’un pixel LR sont comparés, sans certifier une registration optique.
Les réponses sont relatives au neutre de leur propre application. Les masques
communs viennent du HSV **RGB linéaire de sortie du neutre LR**, pas du RAW
avant pipeline ni des masques propriétaires Lightroom.

Le comparateur a été étendu pour accepter une série incomplète ou LR seule,
signaler les états manquants, auditer les profils sRGB et distinguer Purple de
Magenta. Le masque diagnostique violet est centré à 270° ; il **n’ajoute aucune
bande au moteur** et ne change pas les supports des sept bandes IPR.

## Couverture des scènes

| Bande / scène | Pixels au cœur du masque | Part de l’échantillon | Usage |
| --- | ---: | ---: | --- |
| Vert / 6V1A0037 | 241 381 | 18,686 % | Scène principale pour le vert. |
| Vert / 6V1A0043 | 5 086 | 0,394 % | Confirmation sur une seconde scène. |
| Orange / 6V1A0327 | 656 747 | 50,841 % | Très bonne couverture de cette bande. |
| Cyan / 6V1A0327 | 3 959 | 0,306 % | Tendances mesurables, autre scène souhaitable. |
| Bleu / 6V1A0327 | 2 803 | 0,217 % | Tendances mesurables, référence +100 à corriger. |
| Violet / 6V1A0327 | 543 | 0,042 % | Insuffisant seul. |
| Violet / 6V1A0043 | 1 854 | 0,144 % | Meilleur corpus, mais bande IPR non équivalente. |
| Magenta / 6V1A0327 | 163 | 0,0126 % | Insuffisant et pas de variante IPR. |
| Magenta / 6V1A0043 | 5 678 | 0,440 % | Références LR exploitables, pas de variante IPR. |

Le cœur correspond à ±15° du centre, S ≥ 0,15 et 0,01 < Y < 0,9 sur le neutre LR.
Ces effectifs ne mesurent pas toute l’étendue d’un masque interne de développement.

## Vert : résultats sur les deux nouvelles images

Les valeurs suivantes sont des moyennes de zone ; elles ne sont pas des gains
à recopier dans le moteur. La variation S est exprimée sur l’échelle 0–1.

| Scène / commande, −100 puis +100 | Lightroom | IPR |
| --- | --- | --- |
| 6V1A0037 — Δ teinte | Environ −57,5° / +42,1° | Environ −11,2° / +11,2° |
| 6V1A0037 — Δ S | Environ −0,614 / +0,267 | Environ −0,283 / +0,265 |
| 6V1A0037 — rapport Y, luminance | Environ ×0,712 / ×1,340 | Environ ×0,409 / ×19,74 |
| 6V1A0043 — Δ teinte | −59,19° / +46,83° | −9,59° / +9,61° |
| 6V1A0043 — Δ S | −0,435 / +0,315 | −0,203 / +0,199 |
| 6V1A0043 — rapport Y, luminance | ×0,724 / ×1,314 | ×0,461 / ×22,08 |

Le vert n’est pas inactif et les commandes ne sont pas inversées. La réponse
est moins ample en teinte, moins désaturante au minimum, et plus énergique en
luminance aux deux extrêmes. La luminance +100 présente la même tendance sur
deux scènes distinctes ; elle mérite une refonte du modèle de gain, pas une
conclusion basée sur la seule première image pauvre en verts.

Les grands rapports sont aussi favorisés par les zones initialement sombres.
Ils ne signifient ni un facteur uniforme sur toute l’image, ni une exposition
appliquée à l’ensemble de la scène. Le passage à un V HSV proche de 1 peut
cependant créer une hausse très importante dans une plage colorée sombre.

## Orange et cyan

| Bande / commande, −100 puis +100 | Lightroom | IPR |
| --- | --- | --- |
| Orange — Δ teinte | −18,86° / +25,60° | −14,43° / +14,44° |
| Orange — Δ S | −0,462 / +0,168 | −0,380 / +0,321 |
| Orange — rapport Y, luminance | ×0,756 / ×1,271 | ×0,166 / ×16,25 |
| Cyan — Δ teinte | −30,26° / +42,12° | −14,30° / +14,30° |
| Cyan — Δ S | −0,277 / +0,325 | −0,284 / +0,264 |
| Cyan — rapport Y, luminance | ×0,672 / ×1,292 | ×0,163 / ×5,18 |

Les commandes agissent dans le sens attendu. Orange +100 de saturation est plus
fort que LR, tandis que le cyan a une amplitude de saturation plus proche de la
référence sur cette scène. La luminance est plus extrême dans les deux bandes.
L’orange est particulièrement probant car il représente plus de la moitié des
échantillons : cet écart ne vient pas d’un masque portant sur quelques pixels.

Comme pour les premières bandes, conserver V pendant une saturation HSV ne
conserve pas Y. La saturation orange IPR −100 augmente ici Y d’environ 21 %, et
+100 le réduit d’environ 19 % dans le masque. Il faut distinguer ce couplage
entre paramètres d’un défaut de direction du curseur de saturation.

## Bleu : résultat partiel

- Teinte LR : −47,71° / +52,01° ; IPR : −7,22° / +7,11°.
- Saturation LR : −0,141 / +0,163 ; IPR : −0,0575 / +0,0575.
- Luminance −100 : LR ×0,927 ; IPR ×0,709.
- Luminance +100 IPR : ×10,27 ; **pas de comparaison LR +100 valide** dans la
  passe enregistrée.

Le XMP incorporé dans `blue/6V1A0327-6.tif` indique `LuminanceAdjustmentBlue=-100`.
Le comparateur le marque invalide et n’utilise pas cette paire pour valider la
parité de luminance. L’utilisateur a été invité à le régénérer à +100.
Les chiffres teinte/saturation restent exploitables ; l’absence d’un état LR
correct n’efface pas toutes les mesures de cette bande.

À la seconde passe, ce fichier n°6 n’est plus présent. Le nouveau
`violet/6V1A0327-7.tif` contient `LuminanceAdjustmentYellow=+100` et
`LuminanceAdjustmentPurple=+100`, sans réglage bleu +100. Il est explicitement
exclu, pas renommé ni substitué. L’emplacement d’un éventuel export bleu correct
a été demandé à l’utilisateur ; sa paire reste à compléter.

## Violet : mesure possible, équivalence impossible pour le moment

L’utilisateur confirme avoir utilisé **le dernier cercle de l’IHM**. Il y a
actuellement sept contrôles, avec un dernier libellé `Magentas`, une pastille
d’apparence violette et un centre moteur 300°. Lightroom possède séparément
`Purple` et `Magenta`. Il manque donc une **distinction indépendante entre ces
deux couleurs**, pas simplement un nom ou une pastille à renommer.

Sur 6V1A0043, le masque violet fournit 1 854 pixels. Les exports disponibles
donnent par exemple une teinte LR −54,03° / +51,87°, contre −5,50° / +5,50°
avec le dernier contrôle IPR. Ce constat décrit deux opérations différentes :
il ne démontre pas un défaut de gain sur une hypothétique bande Purple IPR.
Les comparaisons croisées sont explicitement marquées **non équivalentes**.

États IPR manquants lors de la première passe :

- 6V1A0327 : n°2, n°4 et n°6.
- 6V1A0043 : n°6.

L’utilisateur les a ensuite ajoutés ; les deux séries IPR violet sont maintenant
**complètes et remesurées en v2**. Sur 6V1A0043, la luminance +100 du dernier
contrôle donne ×4,640 de Y, contre ×1,373 pour Purple LR ; sur 6V1A0327, ×5,270
contre ×1,060. Ces chiffres portent toujours sur des bandes non équivalentes.
L’analyse de 6V1A0043
confirme qu’un corpus violet existe, mais la séparation des bandes demeure
un préalable à sa validation photographique.

## Magenta : références Lightroom seulement

Aucun TIFF IPR magenta n’est présent dans les deux séries. Les six états LR
ont été mesurés pour chaque image ; aucune MAE IPR/LR magenta ni aucun verdict
sur des paramètres IPR non testés n’est produit.

6V1A0043 est la meilleure référence fournie : 5 678 pixels au cœur du masque,
contre 163 pour 6V1A0327. Ses réponses LR pourront servir à la future validation
de la bande dédiée. Ne pas déplacer ou renommer les exports violet actuels en
magenta sans connaître les paramètres réellement utilisés.

## Neutres et écarts de base

| Scène | Y moyen Lightroom | Y moyen IPR |
| --- | ---: | ---: |
| 6V1A0327 | 0,12045 | 0,04705 |
| 6V1A0037 | Environ 0,06524 | Environ 0,03667 |
| 6V1A0043 | 0,02824 | 0,01382 |

IPR est plus sombre sur les trois neutres. Les mesures relatives à chaque
application permettent de séparer en partie cet écart de la réponse du
mélangeur, sans rendre identiques les profils, masques et courbes de développement.
Les écarts ne justifient donc pas des corrections basées sur une MAE directe
unique ou un multiplicateur arbitraire.

## Suite recommandée

**Suivi après autorisation utilisateur :** les étapes 1–3 ci-dessous sont désormais
implémentées : huit bandes, gris protégés, gains cumulés, Y préservé pour teinte
et saturation en linéaire et luminance bornée ±0,5 EV. Voir
[ADR-0005](../../decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md).
Les mesures de ce rapport restent celles du moteur antérieur ; elles n'ont pas
été remplacées par des chiffres simulés ni ne valident la nouvelle calibration.
La phrase « non exécutées » plus bas décrit la demande d'analyse initiale.

1. Corriger la protection des achromatiques dans la luminance rouge, puis
   ajouter un vrai test de non-régression sur gris, noirs et faibles chromas.
2. Définir deux bandes clairement identifiées pour violet et magenta si leur
   séparation est retenue : IHM, moteur, sidecar versionné, migration des
   anciennes sept bandes et préférences de sélection. Ajouter une pastille seule
   ne suffit pas ; le contrôle existant doit garder une migration explicite.
3. Revoir le modèle de luminance sélective, qui pousse actuellement V vers 1,
   puis contrôler saturation/Y et transitions sur ces trois scènes avant
   d’ajuster les amplitudes de teinte.
4. Compléter le TIFF LR bleu +100. Les états violet ont été ajoutés, mais les
   bandes restent non équivalentes. Régénérer les
   TIFF IPR après un changement de moteur, en conservant cette campagne comme
   référence avant correction.

Les étapes 1–3 restent des recommandations, pas des modifications exécutées
dans cette demande d’analyse.

## Vérifications

- Comparateur : **5 tests Python réussis**, avec correspondance Purple,
  distinction des centres diagnostics/moteur, audit des séries invalides,
  inspection CLI et correspondance explicite de fichier.
- `colorMixerQmlTests` et `imageExportTests` : réussis.
- La sonde précédente sur les gris reste la preuve des deux échecs de
  protection en luminance rouge ; les tests existants réussis ne l’annulent pas.
- Aucune image source, aucun sidecar et aucun réglage du moteur modifié.
