# Matrice de validation des réglages

**Dernière consolidation :** 10 octobre 2026.
**Légende :** Validé = protocole, export et conclusion disponibles ; À revalider = changement de pipeline ou corpus insuffisant ; À faire = fonction présente mais sans recette complète.

| Fonction | Statut | Preuve / source historique | Prochaine action |
| --- | --- | --- | --- |
| Base colorimétrique à l'ouverture | Camera Neutral accepté visuellement ; normalisation contrôlée disponible dans l'IHM, désactivée par défaut ; fidélité/parité LR non validées | [Rapport unique](analyses/analyse-base-colorimetrique.md), §24–26 : cinq RAW exacts calibrés ; comparaison finale de onze paires, Neutral plus proche sur les cinq scènes comparables ; 17/18 CTest WSL (JPEG 9933 en échec), 76 + 12 tests Python ; preuves Windows et packaging décrits dans le rapport. | Persister le choix DCP/normalisation ; vérifier état exact des exports, optique et résidus 9421/9950 ; Standard, JPEG 9933 et généralisation après audit ; charte physique. |
| Exposition (EV) | Loi et direction revalidées le 9 octobre sur six scènes IPR à ±1 EV ; parité Lightroom non validée | [campagne complète Camera Neutral](analyses/analyse-validation-lumieres-base-2026-10-02.md#campagne-complete-lumieres--nouveaux-neutres-9-octobre-2026) ; neutres LR 0037/9421 corrigés, 12 variantes IPR ×0,5/×2 | Étudier séparément saturation/gamut/HL ; ne pas modifier la loi EV. |
| Noirs et ombres | Ombres corrigées : état actuel accepté visuellement par l'utilisateur le 10 octobre ; tests synthétiques/export réussis | [version figée](analyses/analyse-validation-lumieres-base-2026-10-02.md#version-figee-apres-validation-visuelle--10-octobre-2026) ; gain ≤×4 et ordre tonal préservé ; aucune parité LR universelle annoncée | Étendre aux autres boîtiers/objectifs ; comparer les nouvelles campagnes ; Noirs reste séparé. |
| Blancs et hautes lumières | HL corrigées : état actuel accepté visuellement par l'utilisateur le 10 octobre ; tests RGB/export réussis | [version figée](analyses/analyse-validation-lumieres-base-2026-10-02.md#version-figee-apres-validation-visuelle--10-octobre-2026) ; marge fondée sur maxRGB ; aucune parité LR universelle annoncée | Évolutions possibles après tests d'autres boîtiers/objectifs ; Blancs inchangé et à traiter séparément. |
| Contraste | Validation historique ; fonctionnement cohérent aux extrêmes le 9 octobre | [analyse contraste](analyses/analyse-contraste-2026-10-02.md) ; six scènes, action opposée toe/shoulder ; saturation +100 moyenne 0,291 % IPR | Recette visuelle et rampe synthétique au pivot 18 %, valeurs intermédiaires. |
| Température | Validé | [analyse température](analyses/analyse-temperature-2026-10-02.md) | Revalider la calibration froide après profil ou gamut mapping. |
| Nuance | Validé | [analyse nuance](analyses/analyse-nuance-2026-10-02.md) | Ajouter des gris neutres et carnations. |
| Vibrance / saturation | Validé | [analyse saturation](analyses/analyse-saturation-2026-10-02.md) | Étendre aux couleurs hors gamut. |
| Texture | Validé | [analyse texture](analyses/analyse-texture-2026-10-02.md) | Ajouter peau, bruit et feuillage. |
| Clarté | Validé | [analyse clarté](analyses/analyse-clarte-2026-10-02.md) | Contrôler halos sur contre-jour. |
| Correction du voile | Validé | [analyse voile](analyses/analyse-correction-voile-2026-10-02.md) | Tester scènes sans voile pour les faux positifs. |
| Mélangeur HSL | Fonctionnement aux extrêmes et progressivité moyenne à ±25/±50 confirmés ; recette visuelle/calibration à poursuivre | [extrêmes](analyses/analyse-color-mixer-huit-bandes-2026-10-04.md) : 54 variantes/application, 27 paires ; [intermédiaires](analyses/analyse-color-mixer-intermediaires-2026-10-04.md) : 96 variantes/application, 24 progressions IPR strictement croissantes avec neutre, dérive Y teinte/saturation <0,01 %, audits LR conformes ; 13 tests comparateur réussis ; [correctif](../decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md) | Vérifier transitions, faibles chromas, carnations et aperçu/export. Résidus rouge/bleu historiques non reproduits dans les intermédiaires, cause ancienne et ±100 à retracer. Parité Lightroom non validée ; calibration séparée du rendu neutre. |
| Color grading | À faire | Implémentation du 2 octobre 2026 | Vérifier continuité tonale, luminance et clipping. |
| Courbes | À faire | Implémentation existante | Protocole de points, HDR et interpolation. |
| Grain / flou | À faire | Implémentation existante | Définir une cible esthétique et des critères de non-régression. |
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
