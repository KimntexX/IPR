# Préparation de l'intégration de Rec.2020 linéaire

Étude puis port local du 15 octobre 2026, sur `patch_raw_opening`. **Master
Rec.2020 linéaire implémenté ; qualification visuelle/multi-OS à compléter.** Décision :
[ADR-0009](../decisions/ADR-0009-espace-travail-rec2020-lineaire.md).
Les résultats photographiques restent dans le seul
[rapport RAW/profils, §44–45](../validation/analyses/analyse-base-colorimetrique.md).

## 1. Périmètre et preuve disponible

Le [JSON comparatif v001](../validation/raw/comparatif-espaces-travail-v001-20261015T100527950069Z.json)
porte sur sept scènes, trois espaces et six opérations diagnostiques, soit
126 variantes post-DCP. L'erreur linéaire maximale des opérations invariantes
est de `7,153e-7`, sous le garde-fou `5e-6`. Ce résultat autorise la préparation,
pas la validation des curseurs réels, des détails pleine résolution ni des performances
du moteur C++. Les chronométrages NumPy ne sont pas des budgets applicatifs.

Ne pas mélanger cette migration avec une nouvelle courbe tonale, une retouche
des coefficients empiriques, une nouvelle calibration ou une extension des
tables DCP. Une conversion réversible après un bornage ne récupère pas le
signal perdu en amont. Les essais HL restent une investigation distincte.

## 2. Contrat mathématique cible

RGB désigne des coordonnées **linéaires**, non les valeurs vidéo R'G'B', PQ
ou HLG. Primaires CIE xy : R `(0,708 ; 0,292)`, G `(0,170 ; 0,797)`,
B `(0,131 ; 0,046)` ; blanc D65 `(0,3127 ; 0,3290)`.

Pour une primaire `(x,y)`, former la colonne `(x/y, 1, (1-x-y)/y)`.
Avec ces trois colonnes dans `P` et le blanc `W` normalisé à Y=1 :

```text
S = inverse(P) W
M_RGB→XYZ = P diag(S)
RGB_destination = inverse(M_destination→XYZ) A M_source→XYZ RGB_source
```

`A` est l'identité entre sRGB D65 et Rec.2020 D65 ; depuis ProPhoto D50,
utiliser l'adaptation chromatique déjà définie par le moteur, une seule fois.
Calculer les matrices en double à la préparation, appliquer au tampon float.
Pour le raccordement DCP existant, composer sa matrice PCS/D50 déjà validée
avec le changement sRGB→Rec.2020 : cela conserve ses arrondis colorimétriques
sans créer de pixels sRGB intermédiaires. Les matrices générales des coordonnées
de travail restent dérivées des primaires ci-dessus.
La ligne Y de la matrice Rec.2020 donne approximativement :

```text
Y = 0,2627002 R + 0,6779981 G + 0,0593017 B
```

Centraliser les coefficients dérivés, plutôt que recopier ces arrondis.
L'alpha ne subit ni matrice RGB ni fonction de transfert. `[0,1]` est le cube
nominal, pas une obligation de borner le master ; rejeter NaN/Inf explicitement.
Un DCP appliquant une courbe produit un master linéaire au sens du transfert
de stockage, pas nécessairement un signal physique scene-linear non rendu.

Le port utilise **sur toutes les versions** le constructeur aux coordonnées
xy explicites avec transfert `Linear`. Le simple espace nommé `Bt2020` ne
spécifie pas notre contrat linéaire. Les primaires nommées `Bt2020` existent
depuis Qt 6.8, désormais le minimum du projet
([ADR-0016](../decisions/ADR-0016-minimum-qt-6-8.md)) ; le chemin explicite,
unique et sans branche de version, reste celui du contrat.

## 3. Flux cible et frontières

```text
RAW → décodage / calibration / optique / DCP dans son domaine
                                         ↓ conversion explicite
ICC source → conversion float → master Rec.2020 linéaire → réglages
                                                        ├→ sRGB linéaire → gamut → encodage → aperçu/export
                                                        └→ ICC écran (évolution ultérieure, indépendante)
```

Le chemin sans DCP reçoit aussi une conversion de sortie LibRaw→master ;
ne pas supposer qu'une sortie LibRaw 16 bits sRGB permet de récupérer des
valeurs déjà bornées. Pour les imports TIFF/PNG/JPEG, éviter une conversion
intermédiaire ICC→sRGB **entier** avant Rec.2020 : elle pourrait perdre des
couleurs avant le master. Qualifier un chemin float et ses profils matriciels
ou LUT ; ne pas promettre leur prise en charge sans test.

À l'implémentation, privilégier une sortie DCP directement dans le master
avec une matrice finale préparée. Un passage transitoire par sRGB **flottant
étendu** est mathématiquement possible sans gamut mapping, mais ne doit pas
devenir un second moteur de rendu ni un aller-retour permanent par réglage.

## 4. Audit du code et décisions d'intégration proposées

Les constats ci-dessous décrivent le code audité **avant le port**. L'état
courant est détaillé en fin de document ; ne pas interpréter ce tableau de
préparation comme le contrat actuel.

| Zone | Constat actuel | Travail requis / risque |
| --- | --- | --- |
| `ColorPipeline::WorkingImage` | `CV_32FC4`, étiquette sRGB linéaire | Identité Rec.2020 centrale, contrôles aux frontières ; bloquant si pixels et étiquette divergent |
| `ColorPipeline::fromSourceImage` | ICC converti via RGBA64 entier vers sRGB | Conversion float vers le master, alpha/ICC absent/ICC LUT testés ; risque de bornage préalable |
| `fromLinearSrgb`, `RawDecoder::decode`, `applyProfile` | Sorties marquées sRGB linéaire | Convertir une seule fois tous les chemins, y compris sans DCP et changement de profil à chaud |
| `CameraProfileEngine` | Tables dans ProPhoto D50 ; matrice finale vers sRGB | Conserver le domaine des tables ; changer la matrice finale et le contrat de `renderFromCameraSignal` ensemble |
| `renderWorkingImage` : WB RAW | Recalcule depuis `dcpContext` | Retour dans le master même après température/nuance, sans double application DCP ni redécodage |
| WB des images non RAW | Matrices Bradford et normalisation en sRGB | Conjuguer les matrices par les changements de base ; même blanc de référence, aucun gain arbitraire |
| Exposition / flou | Gain scalaire / convolution RGB | Invariance théorique ; vérifier ordre, frontières et arrondis |
| Gris / luminance / tonalité | `COLOR_RGB2GRAY` et poids Rec.709 | Gris défini par Y du master, pas par les coefficients implicites OpenCV ; masques et contraste requalifiés |
| Ombres/HL/blancs/noirs | Y mais aussi maximum des canaux | Adapter le domaine de ces heuristiques ; remplacer Y seul ne garantit pas le même rendu |
| Vibrance / saturation | Protection `(max-min)/max`, axe gris de Y | Protection dépendante des primaires ; valider carnations, fleurs et faibles chromas avant tout réajustement A1/A2 |
| `ColorMixer::apply` | Teinte HSV des RGB, refus des minima négatifs | Définir la signification des huit bandes dans la cible ; ne pas convertir seulement les poids Y |
| Grading | Couleurs HSV synthétisées et Y sRGB | Palette et action des roues doivent désigner les mêmes couleurs ; tester les trois zones et transitions |
| Courbes | Courbe Y puis courbes RGB par canal | Y doit rester comparable ; courbes de canaux volontairement dépendantes du master, à annoncer et revalider |
| `DetailProcessing` | Y Rec.709 + inverse Co/Cg spécifique | Modifier ensemble séparation/reconstruction, tests identité et conservation Y avant bruit/netteté réels |
| Texture, clarté, voile, grain | Masques Y et/ou statistiques de canaux | Inspecter chaque seuil et bornage ; ne pas appliquer un remplacement global de constantes |
| Histogramme | Canaux du master puis transfert sRGB, avant mapping de l'aperçu | Proposition : histogramme de sortie sRGB, dérivé du même tampon que l'aperçu ; pas des canaux Rec.2020 encodés comme sRGB |
| `toSrgb`, `mapToSrgbGamut` | N'acceptent/prescrivent que le master sRGB | Convertir master→sRGB avant le mapping actuel ; conserver ce mapping pour isoler le changement de base |
| `writeLinearDng` | Chemin distinct, pixels linéaires + matrices/ICC sRGB | Conversion explicite avant écriture ; auditer la cohérence des tags DNG et du profil avec le transfert réellement écrit |
| Cache LRU / proxy | Buffers float et contexte caméra partagés | Identité master/moteur dans les clés ; comptage des allocations et eviction restent bornés |
| `.ipr` | Version de moteur stockée ; changement signalé | Préserver réglages/métadonnées, recalculer les pixels ; ne pas promettre une identité du rendu ancien |
| Vignettes | Provider distinct du master de retouche | Ne pas convertir aveuglément les aperçus JPEG intégrés ; vérifier leurs profils et éviter une double conversion |

Sources : [ColorPipeline](../../src/core/ColorPipeline.cpp),
[profils](../../src/profiles/CameraProfileEngine.cpp),
[RAW](../../src/raw/RawDecoder.cpp), [rendu/export/sidecar](../../src/editor/imageeditor.cpp),
[mélangeur](../../src/core/ColorMixer.cpp), [détails](../../src/core/DetailProcessing.cpp),
[cache](../../src/cache/DecodedImageCache.cpp), [vignettes](../../src/providers/ThumbnailProvider.cpp).

### Deux points mathématiques à ne pas manquer

Pour `Co=R-B`, `Cg=G-(R+B)/2`, et `Y=wR R+wG G+wB B` avec somme des poids 1,
la reconstruction est :

```text
R = Y + (1-wR-wG/2) Co - wG Cg
G = Y + (1/2-wR-wG/2) Co + (1-wG) Cg
B = Y - (wR+wG/2) Co - wG Cg
```

Changer la séparation sans cette inverse produit une dérive colorée dans le
débruitage. Le test séparation→reconstruction doit précéder les essais photo.

Le HSV RGB n'est pas une teinte perceptuelle indépendante des primaires.
Proposition pour le premier port : définir explicitement les bandes sur
Rec.2020, qualifier leur réponse et conserver leurs noms ; ne pas ajouter
en parallèle un « ancien mélangeur ». Si les décalages sont inacceptables,
étudier ensuite un opérateur perceptuel via XYZ/Oklab, en remplacement et non
comme correctif caché. Ce choix d'opérateur reste à valider ; le choix du
master Rec.2020 ne suffit pas à le trancher.

## 5. Restauration, ressources et asynchronisme

`cacheKeyForFile` inclut source, WB, ressources et optique, mais n'exprime pas
explicitement l'espace de travail. Ajouter une identité commune de moteur et
de master aux clés des pixels/proxies. L'identité de moniteur ne doit jamais
entrer dans une recette photo ou invalider le décodage RAW.

La recette stocke actuellement une version `ipr-render-2026-10-14-…` et le
backend LibRaw. Renouveler ce jeton à la migration. Un changement de coordonnées
internes ne requiert pas forcément un nouveau format binaire `.ipr` : ne
changer le schéma que si des champs changent. Les numéros d'étoiles, drapeaux,
couleurs et valeurs des réglages ne doivent pas être perdus. Les anciennes
preuves ne deviennent pas des références Rec.2020.

Le retour agrégé de `renderWorkingImage` transmet espace, provenance et
température mais pas `dcpContext`. Définir explicitement les objets terminaux
et ceux rééditables ; tester les contextes nécessaires au profil/WB à chaud,
sans propager inutilement un gros tampon jusqu'à l'export.

Lors d'un changement rapide profil/image, publier uniquement le résultat de
la requête courante. Tester les alternances R6/R7 et les annulations, sans
partage de WB/matrice mutable entre boîtiers. Une recette incomplète doit
produire un avertissement fidèle, pas un succès de restauration fictif.

## 6. Lots ordonnés et critères de passage

| Lot | Livrable prévu | Condition avant le suivant |
| --- | --- | --- |
| 0 — Référence | État source identifié, empreintes RAW/DCP/config, sortie actuelle et timings natifs | Rejeu des sept neutres et paramètres audités ; pas de nouvelles références LR nécessaires aujourd'hui |
| 1 — Contrat et frontières | Matrices centrales, imports, RAW/DCP/WB, sorties sRGB et DNG | Aller-retour étendu/alpha ; neutres équivalents ; aucun chemin mal étiqueté |
| 2 — Réglages réels | Port Y/tonalité puis couleur/grading/détails/effets et courbes | Tests ciblés par groupe, zéro neutre, rampes continues et absence de NaN/Inf ; pas de retuning simultané |
| 3 — État et IHM | Proxy/cache/recette, histogramme sRGB proposé, profil à chaud | Aller-retour `.ipr`, changement d'image/profil sans réglages perdus ni résultat obsolète |
| 4 — Qualification | Photos pleine résolution, performances, suite complète et CI multi-OS | Validation visuelle utilisateur, preuves JSON, aucune régression inexpliquée avant fusion |

Ces lots sont des étapes de travail, pas des modes alternatifs accessibles à
l'utilisateur. Ne pas livrer un lot intermédiaire avec certains opérateurs
en sRGB interprétant des pixels Rec.2020. La migration remplace le contrat
courant sans option legacy ; le retour arrière éventuel passe par Git.

### Seuils proposés, non résultats acquis

- Matrices / identité / exposition : erreur absolue max `5e-6` dans le
  domaine synthétique défini par les fixtures (inclure valeurs <0 et >1) ;
  mesurer séparément les amplitudes très élevées avec une tolérance relative.
- Neutre sur la même grille et la même sortie : Delta E00 moyen ≤ `0,01`,
  P99 ≤ `0,05`, différence maximale ≤ quatre codes 16 bits. Investiguer tout
  dépassement, notamment près des discontinuités du mapping, plutôt que l'accepter
  comme un « nouveau look ». Seuils à confirmer en pleine résolution.
- Zéro des réglages : identité ; paramètres actifs : stabilité, monotonie
  lorsqu'attendue et objectifs du protocole de groupe. Ne pas imposer
  l'égalité RGB à une courbe de canal après changement des primaires.
- Restaurer une recette dans le nouveau moteur : mêmes pixels que son
  application directe avec ce moteur ; avertissement si la version enregistrée
  diffère. Pas de garantie de reproduire les pixels d'une ancienne version.
- Aucune augmentation inexpliquée de l'écrêtage à sortie fixe. Une baisse des
  pixels blancs n'est pas une preuve de récupération de détails.

## 7. Performance et périmètre de tests

Préparer matrices et profils hors des boucles ; fusionner les transformations
linéaires adjacentes, sans traverser une table ou un opérateur non linéaire.
Un master RGBA float occupe `16 × largeur × hauteur` octets hors pas/padding ;
les conversions ne doivent pas conserver un second master sRGB complet.
Le cache compte déjà les allocations partagées de master, proxy et `cameraRgb` :
revalider ce comptage, les limites et la libération des temporaires.

Mesurer natif pleine résolution, à froid/chaud : décodage, DCP, conversion,
rendu proxy, export, profil à chaud, pic RSS et nombre d'allocations retenues.
Faire au moins dix passages à chaud, présenter médiane/P95, et distinguer
premier accès disque des suivants. Mesurer aussi deux boîtiers alternés.
Établir la référence avant de fixer un budget ; une hausse médiane >10 %
sera un signal d'investigation proposé, pas un gain ou un plafond déjà prouvé.
Le GPU n'est pas requis pour commencer : les matrices seules ne justifient
pas un transfert supplémentaire CPU/GPU de toute l'image.

Mettre à jour les tests du contrat courant : `color_pipeline_test.cpp`, tests
RAW/profil/WB, mélangeur, détails, rendu, cache, export DNG et restauration.
Vérifier leurs noms exacts dans CMake et les règles de sélection CI lors du
port. Le générateur et les sondes doivent appeler le même moteur que l'IHM,
avec recette/empreintes conservées dans les JSON d'analyse, pas de fichiers
auxiliaires ajoutés par le générateur.

Le changement transversal de master est une **grosse modification** : suite
complète finale, même si les lots utilisent des tests ciblés. Ajouter les
cas à ce [protocole RAW/profils](../validation/protocols/protocole-raw-profils.md)
et aux protocoles de groupe existants, sans créer d'analyse parallèle.

## 8. Corpus et besoin utilisateur

Les RAW et références Lightroom actuels suffisent au démarrage et aux tests
de neutres : pas de nouvelle génération LR demandée maintenant. Vérifier
leur présence avant exécution ; les JSON seuls ne contiennent pas les pixels.
Conserver profil Camera Neutral, As Shot, optique désactivée et état de
calibration identique entre témoin et candidat. Ne pas changer automatiquement
ce dernier état au motif de l'ADR-0007.

Priorités visuelles : fleur orange `6V1A0081`, `6V1A0043`, HL `6V1A9940`,
et R7 `7Y6A7003/7298/7599`, complétés par `6V1A0524`. Après le port,
rejouer tous les groupes actifs et bruit/netteté à 100 %. Les prochains R5
(CR3) et 6D Mark II (CR2) restent attendus, pas requis pour coder les matrices,
mais nécessaires à l'élargissement des conclusions multi-boîtiers.

## 9. Sources et limites documentaires

- [ITU-R BT.2020-2, tableau 3](https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2020-2-201510-I!!PDF-E.pdf),
  vérifié le 15 octobre 2026 : définition des primaires et du blanc. Le master
  IPR proposé n'adopte pas l'encodage vidéo de cette recommandation.
- [darktable 5.6, profil d'entrée](https://docs.darktable.org/usermanual/5.6/en/module-reference/processing-modules/input-color-profile/),
  vérifié le 15 octobre 2026 : exemple de séparation entrée/travail/sortie,
  pas une validation de notre implémentation.
- [W3C CSS Color 4, conversions](https://www.w3.org/TR/css-color-4/#converting-colors) :
  transformations et coordonnées étendues ; bibliographie comparative au §43
  du rapport. Les choix d'algorithmes IPR ci-dessus sont des propositions internes.
- En-tête Qt local `C:/Qt/6.11.2/msvc2022_64/include/QtGui/qcolorspace.h`
  et sources C++ liées dans l'audit : contrats observés dans cette installation,
  sans présumer du comportement d'un autre moteur ICC ou d'une autre version.
- [Qt, QColorSpace](https://doc.qt.io/qt-6/qcolorspace.html#Primaries-enum),
  vérifié le 15 octobre 2026 : primaires `Bt2020` depuis 6.8 et constructeur
  aux coordonnées xy disponible avant cette version.

**État lors de la clôture initiale de l'étude :** aucun changement C++/QML de production,
aucune nouvelle mesure photo, aucune migration, aucun build distribué ni
publication Git effectués par cette étude. La prochaine action technique est
le lot 0 puis le port cohérent des lots 1–3, avant qualification du lot 4.

## 10. Port réalisé après autorisation utilisateur

Le code courant utilise un seul master Rec.2020 D65 linéaire. Les entrées
ICC passent par RGBA float (conversion analytique des sRGB connus, Qt float
pour les autres profils) sans intermédiaire sRGB entier. Les sorties LibRaw
sans DCP sont converties une fois ; l'optique reste avant le changement de
primaires pour ne pas réinterpréter les plans CA. Avec DCP, le raccordement
PCS/D50 établi est composé directement vers Rec.2020 : un essai remplaçant
simultanément ses arrondis a été rejeté par le test des neutres.

Y est centralisé dans `WorkingColorSpace.h`. Gris, tonalité, couleur, détails,
Co/Cg et courbes utilisent le nouveau master. Les huit centres HSV et les
courbes RGB sont définis dans Rec.2020 ; les couleurs des roues changent donc
de base de calcul. Les matrices WB non RAW conservent leurs axes sRGB
calibrés par conjugaison, sans aller-retour des pixels. Les levées positives
Ombres/HL gardent un contrôle scalaire de réserve **sRGB de livraison** :
ce contrôle ne borne ni ne remplace le master, et reste indépendant du moniteur.

Le mapping de sortie retourne une dérivée sRGB linéaire explicitement marquée,
non réinjectable comme master. L'histogramme est calculé sur l'aperçu sRGB
après mapping. Le DNG stocke des canaux sRGB linéaires 16 bits, avec ICC linéaire
et matrice XYZ D65→RGB cohérente avec l'illuminant D65. Les clés de cache et
les recettes `.ipr` portent `ipr-render-rec2020-linear-d65-v1`. Le schéma binaire
du sidecar ne change pas ; les paramètres sont restaurés dans le moteur actuel,
avec avertissement de changement de moteur, sans ancien renderer sélectionnable.

Les sondes historiques conservent une frontière d'analyse sRGB étendue explicite
pour leurs métriques, pas un moteur legacy. Leurs pixels d'entrée viennent tous
du moteur actuel. Voir le rapport RAW/profils §46 pour les preuves et limites.
La qualification photographique de chaque réglage, les détails pleine résolution,
les performances natives répétées et les autres OS ne sont pas déclarés validés
par la compilation et les tests Windows.
