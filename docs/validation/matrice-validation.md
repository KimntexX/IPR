# Matrice de validation des réglages

**Dernière relecture documentaire :** 1er novembre 2026, synchronisation avec `dev` (`2facc24`). Les corrections de statut ci-dessous n'ajoutent aucune nouvelle campagne ni validation photographique.
**Légende :** Validé = protocole, export et conclusion disponibles ; À revalider = changement de pipeline ou corpus insuffisant ; À faire = fonction présente mais sans recette complète.

**Port du 15 octobre 2026 :** le master est Rec.2020 linéaire D65. Tous les
verdicts photographiques sRGB historiques ci-dessous sont des antécédents,
pas une revalidation sous Rec.2020. Les invariants automatisés et les neutres
ont leur contrôle propre ; la qualification courante doit être lue par groupe et campagne. Les campagnes
Lumières des 18–21 octobre sont postérieures au port et ont leur verdict propre,
avec la réserve HL explicitement maintenue.

Rendu initial : travaux intégrés dans `dev`, avec des investigations historiques sur `patch_raw_opening` ;
[19 RAW, 46 TIFF présents audités, 19 références LR As Shot](analyses/analyse-base-colorimetrique.md#31-référence-9421-as-shot-conforme-et-reprise--14-octobre-2026).
Les premiers essais Couleur du 13 octobre sur `patch_color` sont historiques ; leurs états alors non fusionnés ne décrivent pas le modèle livré et validé ensuite dans `dev`.

Mesures transversales du 17 octobre : [performance d’ouverture et navigation](analyses/analyse-performance-ouverture.md).
Trois RAW, deux campagnes JSON ; découverte de profils coûteuse sans sélection
persistée, cache de pixels 256 Mio trop petit pour les entrées mesurées.
Le diagnostic est suivi d’une intégration dans `dev` du contrôleur mémoire
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
| Base colorimétrique à l'ouverture | Camera Neutral accepté visuellement ; calibration demandée par défaut à la première ouverture, choix restauré depuis le sidecar ; application effective selon catalogue modèle/profil/mode ; **validé pour l'alpha par l'utilisateur (1er novembre 2026)** : aucune différence visuelle visible ; fidélité/parité LR non mesurées | [Rapport unique](analyses/analyse-base-colorimetrique.md), §24–26 : cinq RAW exacts calibrés ; comparaison finale de onze paires, Neutral plus proche sur les cinq scènes comparables ; 17/18 CTest WSL (JPEG 9933 en échec), 76 + 12 tests Python ; preuves Windows et packaging décrits dans le rapport. | Contrôler la restauration du choix DCP/calibration ; vérifier état exact des exports, optique et résidus 9421/9950 ; Standard, JPEG 9933 et généralisation après audit ; charte physique. |
| Exposition (EV) | Parité Lightroom mesurée le 30 octobre 2026 sur 28 scènes de −2 à +2 EV : 0,6 % de l'effet LR inexpliqué, amplitude IPR/LR 1,00 ; **validé par l'utilisateur (31 octobre 2026)** : en négatif, IPR assombrit plus que LR, visible surtout à partir de −3 EV, accepté comme choix technique | [parité IPR/LR](raw/etude-validation-ipr-v001-20261030T141855Z.json) (`tests/study_validation_ipr.py`) ; [campagne complète Camera Neutral](analyses/analyse-validation-lumieres-base-2026-10-02.md#campagne-complete-lumieres--nouveaux-neutres-9-octobre-2026) | Parité non mesurée au-delà de ±2 EV. Suivi : [issue #11](https://github.com/KimntexX/IPR/issues/11). |
| Noirs et ombres | Ombres corrigées : état actuel accepté visuellement par l'utilisateur le 10 octobre ; curseur Noirs **validé par l'utilisateur (1er novembre 2026)** ; tests synthétiques/export réussis | [version figée](analyses/analyse-validation-lumieres-base-2026-10-02.md#version-figee-apres-validation-visuelle--10-octobre-2026) ; gain ≤×4 et ordre tonal préservé ; aucune parité LR universelle annoncée | Étendre aux autres boîtiers/objectifs ; comparer les nouvelles campagnes ; Noirs reste séparé. |
| Blancs et hautes lumières | HL corrigées : état actuel accepté visuellement par l'utilisateur le 10 octobre ; curseur Blancs **validé par l'utilisateur (1er novembre 2026)** ; tests RGB/export réussis | [version figée](analyses/analyse-validation-lumieres-base-2026-10-02.md#version-figee-apres-validation-visuelle--10-octobre-2026) ; marge fondée sur maxRGB ; aucune parité LR universelle annoncée | Évolutions possibles après tests d'autres boîtiers/objectifs ; Blancs inchangé et à traiter séparément. |
| Contraste | Loi réajustée le 31 octobre 2026 sur 28 scènes (plage 24 EV, pivot 0,225, force croissante vers +100) ; **contrôle visuel de l'utilisateur (31 octobre 2026)** : écarts faibles, variation plus forte que LR sur les images très lumineuses acceptée comme choix technique | [parité avant](raw/etude-validation-ipr-v001-20261030T141855Z.json) 24,2 % de l'effet LR inexpliqué (amplitude 1,14) ; [candidat écarté](raw/etude-validation-ipr-v002-20261030T214413Z.json) 12,4 % (amplitude 0,88) ; [loi retenue](raw/etude-validation-ipr-v003-20261030T223058Z.json) 12,1 %, médiane 8,9 %, amplitude 0,98–1,03 | Le pivot de Lightroom est propre à chaque image (−1,3 à −3,2 EV en sortie) ; aucune statistique simple ne l'explique encore. Les scènes à pivot haut (6V1A9940, 6V1A0007) restent les plus éloignées. Suivi : [issue #12](https://github.com/KimntexX/IPR/issues/12). |
| Température | Validé ; parité Lightroom remesurée le 30 octobre 2026 sur 28 scènes de 2800 à 10000 K : 2,0 % de l'effet LR inexpliqué, amplitude 0,98 (0,95 à 2800 K) ; **validé par l'utilisateur pour l'alpha (31 octobre 2026)** : légère différence dans les bleus extrêmes (2000 K), non choquante | [analyse température](analyses/analyse-temperature-2026-10-02.md) ; [parité IPR/LR](raw/etude-validation-ipr-v001-20261030T141855Z.json) | Revoir les bleus extrêmes (2000 K) plus tard. Suivi : [issue #10](https://github.com/KimntexX/IPR/issues/10). |
| Nuance | Validé ; parité Lightroom remesurée le 30 octobre 2026 sur 28 scènes de −100 à +100 : 1,1 % de l'effet LR inexpliqué, amplitude 0,98 ; **contrôle visuel de l'utilisateur (31 octobre 2026)** : aucune différence notable avec LR | [analyse nuance](analyses/analyse-nuance-2026-10-02.md) ; [parité IPR/LR](raw/etude-validation-ipr-v001-20261030T141855Z.json) | Ajouter des gris neutres et carnations. |
| Vibrance / saturation | **Validé par l'utilisateur (22 octobre 2026)**, réserve sur les grands ciels bleus saturés à +100 | [rapport Couleur](analyses/analyse-couleur.md#rotation-de-teinte-de-la-vibrance--22-octobre-2026) : 49 scènes, campagnes v004 (saturation) et v008 (vibrance), 637 états chacune, aucune exclusion ; écart ≤ 0,031 jusqu'à +50 et à toutes les valeurs négatives, 0,143 sur les couleurs peu saturées à +100 | Ciels saturés (6V1A9141) repris en bêta avec les calques et la détection de forme. |
| Texture | **Validé par l'utilisateur (25 octobre 2026)**, livré en `v0.1.45-alpha` | [rapport Effets](analyses/analyse-effets.md#effets-20261024-texture) : réponse de Lightroom par échelle reproduite à ±0,02 de 1 à 16 pixels à toutes les valeurs | Peau, bruit et feuillage. |
| Clarté | **Validé par l'utilisateur (25 octobre 2026) avec réserve**, livré en `v0.1.45-alpha` | [rapport Effets](analyses/analyse-effets.md#effets-20261024-clarte) : contraste local de Lightroom suivi échelle par échelle ; composante tonale adaptée à chaque scène non reproduite (§6, pistes closes §6 ter et quater) | Halos sur contre-jour. |
| Correction du voile | **Validé par l'utilisateur (25 octobre 2026) avec réserves**, livré en `v0.1.45-alpha` | [rapport Effets](analyses/analyse-effets.md) §7 à 10 quinquies : onze scènes ; luminance inexpliquée 0,05 à 0,09 en négatif, 0,22 à 0,27 en positif ; adaptation de Lightroom à chaque scène non modélisée | Scènes sans voile (faux positifs), scènes nocturnes. |
| Mélangeur HSL | Tables de réponse Lightroom (22 octobre 2026) ; couleurs jugées proches de LR par l'utilisateur, vibrance à 0 ; magenta décalé de −10° à sa demande ; **validé par l'utilisateur (1er novembre 2026)** | [rapport mélangeur](analyses/analyse-color-mixer.md) : 2 808 états, sept bandes sous 3° de rotation et 0,03 de saturation ; réglages combinés 0,4° à 2,7° | Revue du magenta ; curseurs voisins réglés en sens opposé (LR les applique vraisemblablement l'un après l'autre). |
| Color grading | Recalé sur Lightroom (25–26 octobre 2026), fusion, balance, luminance et roue Global comprises ; **validé par l'utilisateur (26 octobre 2026)** | [rapport grading](analyses/analyse-grading.md) : 28 scènes ; part de l'effet LR inexpliquée 0,075 / 0,071 / 0,129 (ombres / tons moyens / hautes lumières, ancien grading 14,7 / 0,76 / 0,99), 0,076 sur 228 états fusion/balance, 0,017 sur 342 états luminance/Global | Hautes lumières à fusion 0 (0,47) ; écart des neutres dans les tons clairs ; deux zones opposées qui se chevauchent. |
| Couleur du point | **Calée sur Lightroom et validée par l'utilisateur (28 octobre 2026)**, livrée en `v0.1.58-alpha` ; teinte gardée telle qu'IPR la rend, à la demande de l'utilisateur | [rapport Couleur du point](analyses/analyse-couleur-point.md) : 1 848 exports, TSV du ProPhoto linéaire ; IPR/LR poolé gamme 0,06, saturation 0,07, luminance 0,10, teinte 0,12 | Variance positive trop forte (×1,22–1,26) à étudier ; réponses mesurées sur une source. |
| Courbes | **Calées sur Lightroom et validées par l'utilisateur (28 octobre 2026)**, livrées en `v0.1.59-alpha` ; extrémités déplaçables | [rapport Courbes](analyses/analyse-courbes.md) : 1 008 exports ; spline naturelle sur ProPhoto encodé sRGB, par canal ; IPR/LR 2,9 % (RVB) et 2,6 % (canaux) non expliqués, amplitude 1,00 | Ordre RVB puis canal et interaction avec les autres réglages non mesurés. |
| Grain / flou | Grain **conforme à Lightroom** (taille 25, rugosité 50), validé le 25 octobre 2026 ; flou propre à IPR **validé tel quel par l'utilisateur (1er novembre 2026)** | [rapport Effets](analyses/analyse-effets.md#effets-20261024-grain) | Décider des curseurs Taille et Rugosité de Lightroom ; cible et non-régression du flou. |
| Réduction de bruit | Luminance : parité mesurée le 31 octobre 2026 (28 scènes, 10 états, chemins actuel et signal caméra) ; IPR retire moins de bruit que Lightroom à réglage égal et garde plus de détail fin, **réponse conservée comme choix technique (utilisateur, 31 octobre 2026)**. Couleur : nouveau traitement après les tons dans les deux modes (médian, filtre guidé, couleur relative à la luminance) ; taches de couleur au niveau de Lightroom (0,21 contre 0,25 à 25), bruit de teinte presque retiré (0,03 contre 0,50), couleur des contours au niveau de Lightroom en médiane (0,68 contre 0,67–0,83) ; **approuvé par l'utilisateur dans l'état avec réserve (31 octobre 2026)** : 7Y6A7599 (ISO 32 000) reste en dessous de Lightroom | [parité initiale](raw/etude-validation-bruit-v001-20261031T122448Z.json) ; candidats couleur v002 à v004 ; [couleur retenue](raw/etude-validation-bruit-v005-20261031T212814Z.json) (`tests/study_validation_noise.py`) | Améliorer les images très bruitées en bêta ; placement de la couleur après les tons confirmé par l'étude de Codex (1er novembre 2026, 28 scènes) : il conserve le mieux la luminosité ; avant le profil seul n'est pas meilleur en général, l'hybride réduit les taches dans les noirs profonds mais assombrit (≈ 2 % en médiane) et cumule deux filtres. Le développement signé reste la cible. Suivi : [issue #14](https://github.com/KimntexX/IPR/issues/14) (algorithme proposé : [#6](https://github.com/KimntexX/IPR/issues/6)). |
| Débruitage neuronal CFA (bêta-1) | Intégration locale partielle, 11 novembre 2026 : préférences, recette, annulation et installation vérifiée. Sur R6 III ISO 4000, activation / forces 100 et 50 / retour exact à l’original affiché vérifiés sous DirectML. | [Preuve](raw/neural-editor-beta1-integration-v002-20261111T131222Z.json) ; [contrat du module](../../src/neural/README.md). | Vrai modèle sur scènes inédites et autres ISO, revue visuelle utilisateur et CI globale à finir ; balance automatique refusée temporairement. Suivi bruit : [issue #14](https://github.com/KimntexX/IPR/issues/14), à compléter pour le neuronal avant livraison. |
| Netteté / couplage bruit-netteté | Parité Lightroom mesurée le 31 octobre 2026 sur 28 scènes et 15 états ; **conservée dans l'état par l'utilisateur (31 octobre 2026)** : IPR suit le Détail 0 de Lightroom (détail fin ×1,42 contre ×1,57, grain ×1,55 contre ×1,63 à gain 75) ; le Détail de Lightroom (25 par défaut) accentue l'échelle la plus fine et le grain (×4,6 à gain 75), sans équivalent dans IPR | [parité IPR/LR](raw/etude-validation-nettete-v001-20261031T123556Z.json) (`tests/study_validation_sharpening.py`) ; [stratégie d'implémentation](../architecture/denoising-sharpening.md) | Aperçu : rayon ramené à l'échelle de l'image (audit C-01), l'aperçu montre désormais la netteté de l'export ; **validé dans l'état par l'utilisateur (1er novembre 2026)**. Affinage prévu en bêta avec les réseaux de neurones (seuls ou avec une mise à jour de l'algorithme) ; le masquage d'IPR retire plus de texture que celui de Lightroom. Suivi : [issue #13](https://github.com/KimntexX/IPR/issues/13). |
| Sidecar `.ipr` / retouche DCP et optique | Format courant 9 ; anciennes versions lues et converties ; tests Windows réussis | [ADR-0006](../decisions/ADR-0006-restauration-raw-et-retouche-profil.md) et [contrat optique](../camera-profile-engine.md#persistance-et-commandes-optiques-9-octobre-2026) : DCP/LCP/calibration/WB avant décodage, SHA-256, lecture v1–v9, options objectif/CA indépendantes, cache, sauvegarde atomique et restauration pixel-identique synthétique | Comparaison avant/après **validée par l'utilisateur (1er novembre 2026)** ; zoom amélioré, non prioritaire (aperçu adaptatif). Reste : déplacement d'une photo avec son `.ipr` vers un autre dossier, permissions. Les anciennes versions du format restent lues et converties (**décision de l'utilisateur du 1er novembre 2026** : pas de rupture des fichiers utilisateur). |
| Correction objectif / aberration chromatique | Commandes indépendantes intégrées ; tests synthétiques réussis ; validées dans l'état par l'utilisateur le 30 octobre 2026, **puis corrigées** dans `dev` : le modèle du profil travaille en unités de focale ; mesuré sur un 60D, **correction vérifiée par l'utilisateur le 5 novembre 2026** ; comparaison à un export Lightroom et mesure en plein format restantes ([issue #26](https://github.com/KimntexX/IPR/issues/26), [analyse](analyses/analyse-panorama.md#un-défaut-de-la-correction-dobjectif-révélé-en-chemin)) | `cameraProfileEngineTests` : neutralité avec deux options désactivées, CA seule, géométrie/vignettage seuls et compatibilité LCP ; `editorRawDevelopmentTests` : cache, redémarrage, ressources modifiées/manquantes ; `settingsQmlTests` : présence des commandes | Évolution future demandée : réglage avancé de l'intensité (distorsion, vignettage, aberrations). CA latérale seulement avec modèle LCP ; franges longitudinales non traitées. |
| Application de réglages par lot | Reprise le 26 octobre 2026 : en tâche de fond ; une balance des blancs « telle quelle » reste celle de chaque cible (lue dans ses métadonnées, environ 2 ms par RAW, même valeur que le développement), une balance personnalisée est copiée ; développement RAW, balance automatique et annotations de la cible conservés ; échecs nommés dans le statut. Copier/coller suit la même règle | `editorBatchTests` : cibles, annotations, image ouverte, échec d'écriture nommé, balance telle quelle et personnalisée, collage | **Fonctionnement validé par l'utilisateur (1er novembre 2026)** sur un dossier de RAW (reproduit aussi sur trois CR3) ; le bouton du ruban était passé inaperçu : la commande est dans le menu du clic droit et le menu Édition, et une cible sans `.ipr` garde le développement RAW qu'elle a à l'ouverture ; **nouvel emplacement validé par l'utilisateur (1er novembre 2026)**. Suivi : [issue #15](https://github.com/KimntexX/IPR/issues/15). |
| Export par lot | Implémenté le 26 octobre 2026 : « Exporter la sélection » (section Export) ; chaque image avec sa propre recette, en pleine résolution, l'une après l'autre sous le contrôle mémoire ; noms uniques `nom-IPR.ext` sans jamais remplacer un fichier ; progression, annulation entre deux images, échecs nommés | `editorBatchTests` : trois images exportées avec leur recette, fichier existant conservé, annulation | **Testé et validé par l'utilisateur le 30 octobre 2026.** Temps et mémoire non mesurés. |
| Export DNG / Lightroom | **Lu par Lightroom (utilisateur, 31 octobre 2026)** après l'écriture de `NewSubfileType` 0, vérifiée par `imageExportTests`. **Photoshop (Camera Raw) et DxO PhotoLab non vérifiés** : licences expirées | [diagnostic DNG](analyses/diagnostic-export-dng-lightroom-2026-10-03.md) ; matrice et illuminant corrigés, ICC linéaire | Photoshop : même moteur Camera Raw que Lightroom, comportement attendu identique mais non constaté. DxO : n'ouvre un DNG tiers que si le boîtier d'origine est pris en charge, identifié par la marque et le modèle ; le DNG d'IPR déclare « IPR / Rendered Linear DNG » et serait probablement refusé. Piste : écrire la marque et le modèle du boîtier en gardant `UniqueCameraModel` à IPR (variantes de test A/B préparées). Suivi : [issue #9](https://github.com/KimntexX/IPR/issues/9). |
| Sections de réglages / export | Tests automatisés réussis | `settingsQmlTests` : 10 sections dont Développement RAW / Profil, clavier, conservation des valeurs et largeurs contraintes | Vérifier confort, résolutions/DPI Windows et lisibilité du statut de calibration. |
| Dialogue d'export / noms uniques | Tests automatisés réussis sous WSL et export natif Windows | `settingsQmlTests` : filtre, suffixe, format accepté et propositions -2/-3/-4 dans la dernière destination choisie, différente du dossier source ; `imageExportTests` : JPEG/PNG/TIFF/DNG, noms uniques et octets existants préservés ; verrou du temporaire Windows reproduit puis corrigé, test isolé dans cyan réussi | Confirmer visuellement les quatre types et les noms proposés dans le dialogue natif Windows, y compris après changement de dossier. |
| Préférences globales d’IHM | Recette automatisée, validation Windows à compléter | `uiPreferencesTests` et `uiPreferencesQmlTests` ; [contrat INI](../architecture/ui-preferences.md) | Vérifier redémarrage, fenêtres maximisées, multi-écrans/DPI et visibilité des panneaux. |
| Ruban / actualisation du dossier | Le dossier ouvert est surveillé : ajout, suppression et renommage hors d'IPR apparaissent dans le ruban sans perdre sa position ; une image supprimée quitte la sélection ; la disparition de l'image ouverte est signalée. **Validé par l'utilisateur (1er novembre 2026)** | `folderRefreshTests` | Dossier supprimé puis recréé non suivi. Suivi clos : [issue #16](https://github.com/KimntexX/IPR/issues/16). |
| Zoom / navigation dans l'aperçu | Tests automatisés réussis sous Windows et WSL ; recette visuelle Windows à compléter | `imageViewportQmlTests` et chargement de la fenêtre via `uiPreferencesQmlTests` ; [contrat de zoom](../specifications/zoom-image.md) ; build Windows et suite complète 13 CTest / 39 Python réussis | **Retour utilisateur du 30 octobre 2026 : correct jusqu'à 100 %, insuffisant au-delà** (ni performant ni pratique). Le zoom agrandit le proxy 2048 px au lieu du RAW pleine résolution : chantier « aperçu adaptatif » (zone visible rendue en pleine résolution, proxy adapté aux ressources). Vérifier ensuite portrait/paysage, clavier, pavé tactile et DPI Windows. |
| Comparaison origine / actuel | Recette automatisée Windows et WSL ; recette visuelle à compléter | `imageViewportQmlTests` : capture des deux côtés, géométrie commune, séparation mobile, raccourci, transparence ; `imageExportTests` : neutralité, stabilité, sidecar, reset et libération ; [contrat](../specifications/zoom-image.md) | Vérifier confort sur photos réelles et DPI Windows. Origine = source décodée/profilée avant réglages, pas RAW brut ou JPEG intégré ; référence renouvelée au redécodage de WB automatique. |
| Build Windows / sélection de tests | Build et sélection vérifiés sous Windows | [contrat de build](../architecture/tests-cibles-et-build-windows.md) : build Release, 11 CTest natifs + 13 Python réussis au bootstrap ; modification du seul comparateur : sa seule suite Python (13 tests) réussie ; second plan sans changement vide | Maintenir la cartographie en ajoutant une fonction ; suite complète pour bootstrap, changements transversaux et CI GitHub. Packaging et installation vierge restent à valider. |
| Windows autonome | ZIP et installateur : construction, lancement et cycle installation/désinstallation vérifiés en CI pour v0.1.92-alpha | [contrat de packaging](../architecture/build-windows-standalone.md), [CI production](https://github.com/KimntexX/IPR/actions/runs/41347235082) ; contenu public sans DCP/LCP privés | Recette manuelle Windows 10/11 sans SDK : RAW, export, permissions, DPI, GPU, profils personnels et mise à niveau entre versions différentes. La CI ne remplace pas cette recette. |
| Socle d'édition (B0) : tâches, recette CBOR, historique | **Validé en partie**, livré dans `v0.2.97-beta` : rendu identique à la référence (870/870), conversion de 50 `.ipr` réels, construction finale acceptée par l'utilisateur le 2 novembre 2026 ; actifs et contrats des panneaux non implémentés | [bilan B0.4](../architecture/socle-edition-b0.md#b04--bilan-2-novembre-2026), [parité](raw/b0-foundation-parity-v001-20261102T131536Z.json), [conversion](raw/b0-sidecar-conversion-v001-20261102T131357Z.json) | [Issue #21](https://github.com/KimntexX/IPR/issues/21) |
| Géométrie (B1) : orientation, recadrage, transformations | **Validé en partie** : campagne numérique dans les tolérances, parité 870/870 sur tout le corpus, validation visuelle de l'utilisateur le 2 novembre 2026 ; latence avec retouches et masque après transformation ouverts | [Analyse](analyses/analyse-geometrie.md), [audit](raw/b1-geometry-audit-v001-20261102T170514Z.json), [parité](raw/b1-geometry-neutral-parity-v001-20261102T170539Z.json) | [Issue #22](https://github.com/KimntexX/IPR/issues/22) |
| Masques (B2) : formes, cartes de valeurs, recette, commandes | **Validé en partie**, intégré à `dev` (`v0.5.110-beta`) : tests et mesures ; dégradés, pinceau, plage, superposition et réactivité validés à l'usage par l'utilisateur ; design du panneau à revoir | [Analyse](analyses/analyse-masques.md), [audit](raw/b2-masks-audit-v001-20261103T131500Z.json) | [Issue #24](https://github.com/KimntexX/IPR/issues/24) |
| Redressement d'après les lignes (B5, D1) | **Validé en partie**, intégré à `dev`, raccordé au panneau Géométrie : horizons justes à l'œil, faux positifs connus (fils électriques) ; sur 470 photos neuves de bâtiments, 134 perspectives proposées, justes à l'œil sauf une, mais lecture moins stable (95e centile jusqu'à 4,38°). [Analyse](analyses/analyse-detection.md), [issue 30](https://github.com/KimntexX/IPR/issues/30). | [analyse-detection.md](analyses/analyse-detection.md) | Jugement de l'utilisateur, stabilité sur les façades |
| Panorama (B8) : une rangée, verticale ou plusieurs rangées, raccords, tampon disque | **Validé en partie**, intégré à `dev` : assemblage sur les deux axes validé par l'utilisateur sur ses séries, tests synthétiques et série réelle de 14 photos mesurés ; remplissage accepté comme premier jet, avec des erreurs sur le sol et la végétation ([issue #27](https://github.com/KimntexX/IPR/issues/27)) ; vraie série verticale et plein format à mesurer ([issue #25](https://github.com/KimntexX/IPR/issues/25)) | [Analyse](analyses/analyse-panorama.md), [deux rangées](raw/b8-panorama-audit-v001-deux-rangees-20261104T082708Z.json) | Valider dans l'application |
| Retouche (B3) : effacer, corriger, tampon | **Validé en partie**, sur `patch_ui_object_removal`, non fusionné : tests et parcours par l'agent sur un CR2 ; **version acceptée par l'utilisateur le 5 novembre 2026** ; grille de cas de la feuille de route, écart aperçu/export et latence propre à faire ([issue #28](https://github.com/KimntexX/IPR/issues/28)) | [Analyse](analyses/analyse-retouche.md), [mesures](raw/b3-retouch-audit-v001-20261105T113028Z.json) | Grille de cas |
| Empilement antibruit (B7) : recalage, tampon disque, fusion | **Validé en partie** : bruit à 1/√N sur une rafale de 42 vues et 34 vues lunaires, onglet Assembler validé par l'utilisateur sur une autre série ; DNG dans Lightroom, Photoshop et DxO, et turbulence à l'usage, à valider | [Analyse](analyses/analyse-empilement.md), [rafale](raw/b7-stack-audit-v001-20261102T225332Z.json), [Lune](raw/b7-stack-audit-v001-20261102T230314Z.json), [turbulence](raw/b7-stack-audit-v001-20261103T123739Z.json) | [Issue #23](https://github.com/KimntexX/IPR/issues/23) |

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
