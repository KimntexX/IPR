# Matrice de validation des réglages

**Dernière consolidation :** 5 octobre 2026.  
**Légende :** Validé = protocole, export et conclusion disponibles ; À revalider = changement de pipeline ou corpus insuffisant ; À faire = fonction présente mais sans recette complète.

| Fonction | Statut | Preuve / source historique | Prochaine action |
| --- | --- | --- | --- |
| Exposition (EV) | Validé | [analyse de base](analyses/analyse-validation-lumieres-base-2026-10-02.md) | Conserver le corpus EV0 et vérifier à chaque changement tonal. |
| Noirs et ombres | Validé | [analyse noirs/ombres](analyses/analyse-noirs-ombres-2026-10-02.md) | Ajouter des scènes à bruit élevé. |
| Blancs et hautes lumières | Validé après v3 | [analyse blancs/HL](analyses/analyse-blancs-hautes-lumieres-2026-10-02.md) | Vérifier la stabilité avec courbes activées. |
| Contraste | Validé | [analyse contraste](analyses/analyse-contraste-2026-10-02.md) | Revalider si les masques tonaux changent. |
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
| Sidecar `.ipr` | À faire | [rapport session](../reports/sessions/rapport-session-2026-10-02.md) | Effectuer la recette ouverture, invalidation et restauration. |
| Application par lot | À faire | [rapport session](../reports/sessions/rapport-session-2026-10-02.md) | Vérifier cibles, annotations et sidecars individuels. |
| Export DNG / Lightroom | Non validé | [diagnostic DNG](analyses/diagnostic-export-dng-lightroom-2026-10-03.md) | Corriger les métadonnées, puis valider avec le SDK Adobe et Lightroom. Le décodage LibRaw seul ne suffit pas. |
| Sections de réglages / export | Tests automatisés réussis | `settingsQmlTests` : 9 sections, clavier, conservation des valeurs et largeurs 232/280/320 px | Vérifier le confort d'utilisation et les différentes résolutions/DPI sous Windows. |
| Dialogue d'export / noms uniques | Tests automatisés réussis sous WSL et export natif Windows | `settingsQmlTests` : filtre, suffixe, format accepté et propositions -2/-3/-4 dans la dernière destination choisie, différente du dossier source ; `imageExportTests` : JPEG/PNG/TIFF/DNG, noms uniques et octets existants préservés ; verrou du temporaire Windows reproduit puis corrigé, test isolé dans cyan réussi | Confirmer visuellement les quatre types et les noms proposés dans le dialogue natif Windows, y compris après changement de dossier. |
| Préférences globales d’IHM | Recette automatisée, validation Windows à compléter | `uiPreferencesTests` et `uiPreferencesQmlTests` ; [contrat INI](../architecture/ui-preferences.md) | Vérifier redémarrage, fenêtres maximisées, multi-écrans/DPI et visibilité des panneaux. |
| Zoom / navigation dans l'aperçu | Tests automatisés réussis sous Windows et WSL ; recette visuelle Windows à compléter | `imageViewportQmlTests` et chargement de la fenêtre via `uiPreferencesQmlTests` ; [contrat de zoom](../specifications/zoom-image.md) ; build Windows et suite complète 13 CTest / 39 Python réussis | Vérifier portrait/paysage, clavier, pavé tactile et DPI Windows. Le zoom utilise le proxy 2048 px, pas le RAW pleine résolution. |
| Comparaison origine / actuel | Recette automatisée Windows et WSL ; recette visuelle à compléter | `imageViewportQmlTests` : capture des deux côtés, géométrie commune, séparation mobile, raccourci, transparence ; `imageExportTests` : neutralité, stabilité, sidecar, reset et libération ; [contrat](../specifications/zoom-image.md) | Vérifier confort sur photos réelles et DPI Windows. Origine = source décodée/profilée avant réglages, pas RAW brut ou JPEG intégré ; référence renouvelée au redécodage de WB automatique. |
| Build Windows / sélection de tests | Build et sélection vérifiés sous Windows | [contrat de build](../architecture/tests-cibles-et-build-windows.md) : build Release, 11 CTest natifs + 13 Python réussis au bootstrap ; modification du seul comparateur : sa seule suite Python (13 tests) réussie ; second plan sans changement vide | Maintenir la cartographie en ajoutant une fonction ; suite complète pour bootstrap, changements transversaux et CI GitHub. Packaging et installation vierge restent à valider. |

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
