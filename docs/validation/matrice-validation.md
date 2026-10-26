# Matrice de validation des réglages

**Dernière consolidation :** 16 octobre 2026 (règles RAW par mode et restauration automatique ; autres statuts inchangés).
**Légende :** Validé = protocole, export et conclusion disponibles ; À revalider = changement de pipeline ou corpus insuffisant ; À faire = fonction présente mais sans recette complète.

**Port du 15 octobre 2026 :** le master est Rec.2020 linéaire D65. Tous les
verdicts photographiques sRGB historiques ci-dessous sont des antécédents,
pas une revalidation sous Rec.2020. Les invariants automatisés et les neutres
ont leur contrôle propre ; chaque groupe de réglages reste à requalifier
visuellement suivant son protocole avant validation photographique globale.

Rendu initial : investigation active sur `patch_raw_opening`,
[19 RAW, 46 TIFF présents audités, 19 références LR As Shot](analyses/analyse-base-colorimetrique.md#31-référence-9421-as-shot-conforme-et-reprise--14-octobre-2026).
Le prototype Couleur reste sauvegardé sur `patch_color` ; validation visuelle
et calibration des commandes suspendues jusqu'à stabilisation de la base.

Mesures transversales du 17 octobre : [performance d’ouverture et navigation](analyses/analyse-performance-ouverture.md).
Trois RAW, deux campagnes JSON ; découverte de profils coûteuse sans sélection
persistée, cache de pixels 256 Mio trop petit pour les entrées mesurées.
Le diagnostic est suivi d’une intégration locale du contrôleur mémoire
([ADR-0015](../decisions/ADR-0015-controle-memoire-et-prechargement.md)) :
26/26 tests natifs, préchargement suivant/précédent, seuil global 80/75 %.
Pic RSS, latence réelle du mode adaptatif et validation Linux/macOS restent ouverts.

Étude Couleur du 13 octobre : [comparaison A1/A2/B](analyses/analyse-couleur.md#étude-comparative-des-modèles--13-octobre-2026),
392 états diagnostiques sur sept RAW. Le premier jet A1/A2 est ensuite intégré
localement à la demande de l'utilisateur, avec 7/7 tests natifs ciblés réussis.
Validation visuelle et nouvelle campagne encore attendues ; les courbes et
JSON de l'étude décrivent l'état antérieur (master sRGB), pas les nouveaux exports.

| Fonction | Statut | Preuve / source historique | Prochaine action |
| --- | --- | --- | --- |
| Base colorimétrique à l'ouverture | Camera Neutral accepté visuellement ; normalisation contrôlée disponible dans l'IHM, désactivée par défaut ; fidélité/parité LR non validées | [Rapport unique](analyses/analyse-base-colorimetrique.md), §24–26 : cinq RAW exacts calibrés ; comparaison finale de onze paires, Neutral plus proche sur les cinq scènes comparables ; 17/18 CTest WSL (JPEG 9933 en échec), 76 + 12 tests Python ; preuves Windows et packaging décrits dans le rapport. | Persister le choix DCP/normalisation ; vérifier état exact des exports, optique et résidus 9421/9950 ; Standard, JPEG 9933 et généralisation après audit ; charte physique. |
| Exposition (EV) | Loi et direction revalidées le 9 octobre sur six scènes IPR à ±1 EV ; parité Lightroom non validée | [campagne complète Camera Neutral](analyses/analyse-validation-lumieres-base-2026-10-02.md#campagne-complete-lumieres--nouveaux-neutres-9-octobre-2026) ; neutres LR 0037/9421 corrigés, 12 variantes IPR ×0,5/×2 | Étudier séparément saturation/gamut/HL ; ne pas modifier la loi EV. |
| Noirs et ombres | Ombres corrigées : état actuel accepté visuellement par l'utilisateur le 10 octobre ; tests synthétiques/export réussis | [version figée](analyses/analyse-validation-lumieres-base-2026-10-02.md#version-figee-apres-validation-visuelle--10-octobre-2026) ; gain ≤×4 et ordre tonal préservé ; aucune parité LR universelle annoncée | Étendre aux autres boîtiers/objectifs ; comparer les nouvelles campagnes ; Noirs reste séparé. |
| Blancs et hautes lumières | HL corrigées : état actuel accepté visuellement par l'utilisateur le 10 octobre ; tests RGB/export réussis | [version figée](analyses/analyse-validation-lumieres-base-2026-10-02.md#version-figee-apres-validation-visuelle--10-octobre-2026) ; marge fondée sur maxRGB ; aucune parité LR universelle annoncée | Évolutions possibles après tests d'autres boîtiers/objectifs ; Blancs inchangé et à traiter séparément. |
| Contraste | Validation historique ; fonctionnement cohérent aux extrêmes le 9 octobre | [analyse contraste](analyses/analyse-contraste-2026-10-02.md) ; six scènes, action opposée toe/shoulder ; saturation +100 moyenne 0,291 % IPR | Recette visuelle et rampe synthétique au pivot 18 %, valeurs intermédiaires. |
| Température | Validé | [analyse température](analyses/analyse-temperature-2026-10-02.md) | Revalider la calibration froide après profil ou gamut mapping. |
| Nuance | Validé | [analyse nuance](analyses/analyse-nuance-2026-10-02.md) | Ajouter des gris neutres et carnations. |
| Vibrance / saturation | **Validé par l'utilisateur (22 octobre 2026)**, réserve sur les grands ciels bleus saturés à +100 | [rapport Couleur](analyses/analyse-couleur.md#rotation-de-teinte-de-la-vibrance--22-octobre-2026) : 49 scènes, campagnes v004 (saturation) et v008 (vibrance), 637 états chacune, aucune exclusion ; écart ≤ 0,031 jusqu'à +50 et à toutes les valeurs négatives, 0,143 sur les couleurs peu saturées à +100 | Ciels saturés (6V1A9141) repris en bêta avec les calques et la détection de forme. |
| Texture | **Validé par l'utilisateur (25 octobre 2026)**, livré en `v0.1.45-alpha` | [rapport Effets](analyses/analyse-effets.md#effets-20261024-texture) : réponse de Lightroom par échelle reproduite à ±0,02 de 1 à 16 pixels à toutes les valeurs | Peau, bruit et feuillage. |
| Clarté | **Validé par l'utilisateur (25 octobre 2026) avec réserve**, livré en `v0.1.45-alpha` | [rapport Effets](analyses/analyse-effets.md#effets-20261024-clarte) : contraste local de Lightroom suivi échelle par échelle ; composante tonale adaptée à chaque scène non reproduite (§6, pistes closes §6 ter et quater) | Halos sur contre-jour. |
| Correction du voile | **Validé par l'utilisateur (25 octobre 2026) avec réserves**, livré en `v0.1.45-alpha` | [rapport Effets](analyses/analyse-effets.md) §7 à 10 quinquies : onze scènes ; luminance inexpliquée 0,05 à 0,09 en négatif, 0,22 à 0,27 en positif ; adaptation de Lightroom à chaque scène non modélisée | Scènes sans voile (faux positifs), scènes nocturnes. |
| Mélangeur HSL | Tables de réponse Lightroom (22 octobre 2026) ; couleurs jugées proches de LR par l'utilisateur, vibrance à 0 ; magenta décalé de −10° à sa demande | [rapport mélangeur](analyses/analyse-color-mixer.md) : 2 808 états, sept bandes sous 3° de rotation et 0,03 de saturation ; réglages combinés 0,4° à 2,7° | Revue du magenta ; curseurs voisins réglés en sens opposé (LR les applique vraisemblablement l'un après l'autre). |
| Color grading | Recalé sur Lightroom (25–26 octobre 2026), fusion, balance, luminance et roue Global comprises ; **validé par l'utilisateur (26 octobre 2026)** | [rapport grading](analyses/analyse-grading.md) : 28 scènes ; part de l'effet LR inexpliquée 0,075 / 0,071 / 0,129 (ombres / tons moyens / hautes lumières, ancien grading 14,7 / 0,76 / 0,99), 0,076 sur 228 états fusion/balance, 0,017 sur 342 états luminance/Global | Hautes lumières à fusion 0 (0,47) ; écart des neutres dans les tons clairs ; deux zones opposées qui se chevauchent. |
| Courbes | À faire | Implémentation existante | Protocole de points, HDR et interpolation. |
| Grain / flou | Grain **conforme à Lightroom** (taille 25, rugosité 50), validé le 25 octobre 2026 ; flou propre à IPR, à définir | [rapport Effets](analyses/analyse-effets.md#effets-20261024-grain) | Décider des curseurs Taille et Rugosité de Lightroom ; cible et non-régression du flou. |
| Réduction de bruit | Fonctionnement/progressivité v2 confirmés ; qualité et calibration à poursuivre | [campagne v2](analyses/analyse-bruit-v2-2026-10-05.md) : 12 scènes, 24 neutres, 136 variantes standards + 9 activations ; 24 progressions IPR strictement décroissantes ; dérive médiane Y ≤0,289 % ; ISO 3200/8000 ajoutés ; 17 tests analyseur réussis ; [correctif](analyses/correctif-bruit-2026-10-05.md) | Priorité au bruit couleur résiduel à haut ISO ; comparer LR 50/100 sur 1020/9421/9734 si souhaité, puis composantes combinées, halos, saturation, aperçu/export et sidecars. ISO 6400 facultatif, netteté séparée. |
| Netteté / couplage bruit-netteté | À faire | [stratégie d'implémentation](../architecture/denoising-sharpening.md) ; campagne bruit réalisée à gain de netteté 0 | Tester gain/rayon/masquage, peau, feuillage, halos, ISO élevé et couplage au débruitage. |
| Sidecar `.ipr` / retouche DCP et optique | Implémenté v6 ; tests Windows réussis | [ADR-0006](../decisions/ADR-0006-restauration-raw-et-retouche-profil.md) et [contrat optique](../camera-profile-engine.md#persistance-et-commandes-optiques-9-octobre-2026) : DCP/LCP/calibration/WB avant décodage, SHA-256, lecture v1–v6, options objectif/CA indépendantes, cache, sauvegarde atomique et restauration pixel-identique synthétique | Recette visuelle zoom/comparaison, permissions et déplacement. Les anciens exécutables ne lisent pas v6. |
| Correction objectif / aberration chromatique | Commandes indépendantes intégrées ; tests synthétiques réussis | `cameraProfileEngineTests` : neutralité avec deux options désactivées, CA seule, géométrie/vignettage seuls et compatibilité LCP ; `editorRawDevelopmentTests` : cache, redémarrage, ressources modifiées/manquantes ; `settingsQmlTests` : présence des commandes | Vérifier lignes droites, coins, vignettage et franges à 100 %, aperçu/export. CA latérale seulement avec modèle LCP ; franges longitudinales non traitées. |
| Application de réglages par lot | Implémentée ; recette manuelle à terminer | `applyAdjustmentsToSelection()` : écrit les réglages dans les sidecars des cibles en conservant leurs annotations | Vérifier cibles, annotations, sidecars individuels et erreurs partielles ; ne pas confondre avec l'export par lot. |
| Export par lot | Non implémenté | `ImageEditor::exportImage()` exporte seulement l'image active ; `applyAdjustmentsToSelection()` écrit des sidecars | Concevoir file d'export, progression, annulation, erreurs partielles et noms uniques. |
| Export DNG / Lightroom | Non validé | [diagnostic DNG](analyses/diagnostic-export-dng-lightroom-2026-10-03.md) | Corriger les métadonnées, puis valider avec le SDK Adobe et Lightroom. Le décodage LibRaw seul ne suffit pas. |
| Sections de réglages / export | Tests automatisés réussis | `settingsQmlTests` : 10 sections dont Développement RAW / Profil, clavier, conservation des valeurs et largeurs contraintes | Vérifier confort, résolutions/DPI Windows et lisibilité du statut de calibration. |
| Dialogue d'export / noms uniques | Tests automatisés réussis sous WSL et export natif Windows | `settingsQmlTests` : filtre, suffixe, format accepté et propositions -2/-3/-4 dans la dernière destination choisie, différente du dossier source ; `imageExportTests` : JPEG/PNG/TIFF/DNG, noms uniques et octets existants préservés ; verrou du temporaire Windows reproduit puis corrigé, test isolé dans cyan réussi | Confirmer visuellement les quatre types et les noms proposés dans le dialogue natif Windows, y compris après changement de dossier. |
| Préférences globales d’IHM | Recette automatisée, validation Windows à compléter | `uiPreferencesTests` et `uiPreferencesQmlTests` ; [contrat INI](../architecture/ui-preferences.md) | Vérifier redémarrage, fenêtres maximisées, multi-écrans/DPI et visibilité des panneaux. |
| Zoom / navigation dans l'aperçu | Tests automatisés réussis sous Windows et WSL ; recette visuelle Windows à compléter | `imageViewportQmlTests` et chargement de la fenêtre via `uiPreferencesQmlTests` ; [contrat de zoom](../specifications/zoom-image.md) ; build Windows et suite complète 13 CTest / 39 Python réussis | Vérifier portrait/paysage, clavier, pavé tactile et DPI Windows. Le zoom utilise le proxy 2048 px, pas le RAW pleine résolution. |
| Comparaison origine / actuel | Recette automatisée Windows et WSL ; recette visuelle à compléter | `imageViewportQmlTests` : capture des deux côtés, géométrie commune, séparation mobile, raccourci, transparence ; `imageExportTests` : neutralité, stabilité, sidecar, reset et libération ; [contrat](../specifications/zoom-image.md) | Vérifier confort sur photos réelles et DPI Windows. Origine = source décodée/profilée avant réglages, pas RAW brut ou JPEG intégré ; référence renouvelée au redécodage de WB automatique. |
| Build Windows / sélection de tests | Build et sélection vérifiés sous Windows | [contrat de build](../architecture/tests-cibles-et-build-windows.md) : build Release, 11 CTest natifs + 13 Python réussis au bootstrap ; modification du seul comparateur : sa seule suite Python (13 tests) réussie ; second plan sans changement vide | Maintenir la cartographie en ajoutant une fonction ; suite complète pour bootstrap, changements transversaux et CI GitHub. Packaging et installation vierge restent à valider. |
| Windows autonome | ZIP local produit, démarrage offscreen vérifié sans PATH de développement | [contrat de packaging](../architecture/build-windows-standalone.md) ; profil Neutral, LCP Canon et INI local ; archive privée non redistribuable avec profils Adobe | Recette Windows 10/11 propres : RAW, export, permissions, DPI, GPU et redémarrage. |

## Lecture des statuts et mesures

### Protocoles courants

Le [catalogue consolidé](protocols/README.md) propose une recette par groupe,
dont [Couleur](protocols/protocole-couleur.md), [Lumières](protocols/protocole-lumieres.md),
[Courbes](protocols/protocole-courbes.md), [Grading](protocols/protocole-grading.md)
et [Netteté](protocols/protocole-nettete.md), avec le
[socle commun](protocols/protocole-commun.md). Cette consolidation ne modifie
pas les verdicts historiques. Les [analyseurs natifs/JSON par groupe](tools/analyseurs-groupes.md)
sont disponibles ; charte, HDR, transitions, halos, géométrie et combinaisons
restent des recettes complémentaires.


**Corpus au 9 octobre :** les 423 TIFF Lightroom Adobe Standard ont été
[supprimés sur demande](../reports/storage/lightroom-cleanup-2026-10-09/README.md).
Les cinq références Camera Neutral, RAW et exports IPR restent disponibles.
Les verdicts ci-dessus sont historiques : régénérer les références Lightroom
Camera Neutral avant de relancer les campagnes concernées. Les mesures
conservées ne remplacent pas les pixels supprimés.

Les compteurs anciens cités dans les lignes historiques sont des exécutions
datées, pas le résultat de la suite actuelle. Au 8 octobre, le dernier passage
complet WSL est **17/18** (JPEG 9933) ; les **16/16 CTest Windows** disponibles
ont été relancés avec succès pendant la passe documentaire. Les sondes UNIX
ne sont pas incluses dans ce total Windows. Les tests Python d'analyse/sélection
sont **76/12**, avec **4 tests documentaires** supplémentaires. Voir les rapports de famille
pour les preuves, sans fusionner des jeux de tests différents.
Les [CSV et courbes](metrics/README.md) sont des vues dérivées : vitesse CPU,
progressivité d'un réglage et proximité Lightroom sont des axes distincts.

## Migration Rec.2020 — préparation du 15 octobre 2026

**Master porté localement ; qualification visuelle et multi-OS à compléter.**
Les tests automatisés et le rejeu des neutres sont détaillés au §46 du rapport
RAW/profils. Ils ne valident pas photographiquement tous les réglages. Voir le
[plan de port](../architecture/integration-rec2020.md) et la qualification
ajoutée au [protocole RAW/profils](protocols/protocole-raw-profils.md).
Gates : conversions/neutres, réglages de tous les groupes, cache/restauration,
sorties dont DNG, pleine résolution et performances natives, CI complète,
puis accord visuel utilisateur. Les verdicts historiques restent inchangés.

**Neutres calibrés, campagne du 15 octobre :** les 19 couples Camera Neutral /
As Shot passent l'audit et les exports IPR sont identiques au rejeu natif sur
la grille contrôlée au pas 8. Voir le [§47 du rapport unique](analyses/analyse-base-colorimetrique.md#47-validation-des-exports-neutres-rec2020-avec-calibration--15-octobre-2026)
et sa preuve JSON horodatée. Écart absolu moyen de luminance par scène R6 :
0,503 %. Les trois R7 restent plus lumineux de 2,555 à 4,632 %, avec un cadrage
différent à harmoniser ; des écarts de codes hauts persistent. Ce contrôle
confirme l'application de la calibration, pas la fidélité physique, la parité
LR ni la validation de tous les réglages sur le nouveau master.

**Résidu R7, étude du 15 octobre :** un coefficient candidat de rendu
**−0,02 EV avant DCP** (gain 0,986233, baseline effective +0,33 au lieu de +0,35)
est identifié par recalage et quinze essais natifs ; MAE Y normalisée moyenne
réduite de 25,78 % sur le masque non écrêté. Voir le
[§48 du rapport unique](analyses/analyse-base-colorimetrique.md#48-résidu-r7--recalage-et-coefficient-candidat--15-octobre-2026).
**Au terme de l'étude du 15 octobre, non intégré et non validé visuellement** : trois R7 à ISO
20000/32000 seulement, résidus bleus/ombres et HL encore présents. La baseline
documentée DNG n'est pas réécrite et le coefficient ne s'applique pas aux R6.

**Mise à jour du 16 octobre :** le candidat est accepté par l'utilisateur et
intégré localement. Voir le [§49](analyses/analyse-base-colorimetrique.md#49-intégration-de-la-compensation-r7-au-chemin-dcp--16-octobre-2026)
et l'[ADR-0010](../decisions/ADR-0010-compensation-rendu-par-profil-dcp.md).
Rejeu : 16 R6 identiques au neutre précédent et 3 R7 identiques au candidat
+0,33 EV, au pas 8. La baseline source reste +0,35 et la compensation vaut
−0,02 EV ; calibration active et profils/ISO audités requis. Les limites
bas/moyen ISO, bleus/ombres et HL restent ouvertes. Build Windows, 22 CTest
et suite Python (235 tests, dont un ignoré) réussis ; pas de livraison GitHub.

**Restauration d'une ancienne recette, 16 octobre :** le
[§50](analyses/analyse-base-colorimetrique.md#50-retour-rouge-sur-0081-export-neutre-2--16-octobre-2026)
identifie l'export 0081 « −2 » comme exactement égal au rejeu non calibré au
pas 8. Le manifeste partagé a changé d'empreinte ; la recette conserve
l'ancienne empreinte malgré une calibration demandée. Le coefficient R7
n'est pas appliqué au R6. Il faut confirmer le manifeste courant dans l'IHM ;
une confirmation dédiée et un avertissement plus visible restent à prévoir.

**Correction implémentée ensuite le 16 octobre :** voir le
[§51](analyses/analyse-base-colorimetrique.md#51-ouverture-des-règles-r6r7-aux-nouvelles-photos-et-automatisme--16-octobre-2026)
et l'[ADR-0011](../decisions/ADR-0011-regles-raw-par-mode-et-configuration-vivante.md).
Manifeste schéma 3 vivant, calibration active par défaut, règles modèle/DCP/mode
sans liste de RAW ou d'ISO. Les 19 témoins restent identiques au pas 8 ; les
nouveaux ISO sont autorisés à l'essai, pas déclarés validés. Restauration d'un
manifeste modifié testée avec conservation des retouches ; 22 CTest et 235 tests
Python (un ignoré) réussis. La mise à jour des profils DCP/LCP reste contrôlée.

**Transition formats/caractérisations — 16 octobre :**
voir le [§52](analyses/analyse-base-colorimetrique.md#52-transition-des-formats-et-du-catalogue-de-caractérisations--16-octobre-2026)
et l'[ADR-0012](../decisions/ADR-0012-formats-extensibles-et-caracterisations.md).
Catalogue schéma 4, identités format/décodeur dans les données, callbacks de
vignettes dédiés et filtres d'ouverture dynamiques. Les 19 témoins gardent
0 code d'écart au pas 8 ([preuve v003](raw/integration-profile-rendering-v003-20261016T135724740625Z.json)).
Pas de nouveau boîtier certifié : le contrat caméra commun et la séparation
physique des règles empiriques restent à réaliser.

**Deuxième lot — contrat caméra :** `CameraSignal` centralise le rendu DCP de
l'ouverture et du changement rapide de profil ; contrôles de coordonnées,
WB, identité et valeurs finies. [§53](analyses/analyse-base-colorimetrique.md#53-contrat-rgb-caméra-et-rendu-dcp-commun--16-octobre-2026),
[preuve v004](raw/integration-profile-rendering-v004-20261016T140847990289Z.json) :
19 témoins inchangés au pas 8 ; 22 CTest et 235 tests Python (un ignoré) réussis.
La validation visuelle utilisateur couvre le premier lot ; la caractérisation
physique indépendante du DCP reste à réaliser.

La frontière commune d'import est contrôlée au
[§54](analyses/analyse-base-colorimetrique.md#54-frontière-dimport-commune-raw-et-images-développées--16-octobre-2026) :
23 CTest et 235 tests Python (un ignoré) réussis, tests JPG/PNG/TIFF avec ICC,
alpha, orientation, métadonnées, réimport d'un DNG linéaire exporté. Les 19 RAW
restent identiques au pas 8 ([preuve v006](raw/integration-profile-rendering-v006-20261016T142535955642Z.json)).
Ce contrôle ne qualifie pas tous les DNG ni tous les TIFF spécialisés.

Le cache partagé DCP/LCP est contrôlé au
[§55](analyses/analyse-base-colorimetrique.md#55-cache-partagé-borné-des-profils-dcplcp-préparés--17-octobre-2026) :
24 CTest et 235 tests Python (un ignoré) réussis. Les 19 RAW restent identiques
au pas 8 ([preuve v007](raw/integration-profile-rendering-v007-20261016T220959682955Z.json)).
Budget initial 64 Mio/16 entrées ; invalidation par contenu et concurrence
testées. Pas de CI GitHub couvrant les modifications locales non publiées.

## Recette manuelle — Préférences d’IHM

1. Ouvrir IPR, relever le chemin dans Affichage → Fichier de préférences…,
   déplacer/redimensionner la fenêtre et ouvrir plusieurs sections.
2. Changer canal de courbe, bande HSL, zone grading et format/qualité d’export,
   sans changer de réglage photo. Masquer un panneau via Affichage.
3. Fermer puis relancer : tous ces choix doivent revenir. Vérifier qu’un
   changement d’image restaure ses réglages `.ipr`, pas ceux de l’image précédente.
4. Maximiser, fermer, relancer, puis restaurer : la taille normale précédente
   doit être conservée. Ne pas relancer minimisé.
5. Avec plusieurs écrans, tester des coordonnées négatives et le redémarrage
   après déconnexion de l’écran qui hébergeait la fenêtre. Vérifier à 100/150/200 %
   de mise à l’échelle ; les limites minimales de l’IHM restent applicables.
6. Application fermée et après sauvegarde de l’INI, modifier `theme/canvas` et
   `layout/settingsWidth` : vérifier l’effet au lancement et l’absence de
   défilement horizontal. Restaurer ensuite les valeurs souhaitées.
7. Les clés `[future]` ne doivent déclencher aucune fonctionnalité ni changer
   le rendu des images.
