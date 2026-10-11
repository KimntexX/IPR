# Protocole — IHM, restauration et export

Version 1 — 10 octobre 2026. Statut : recette transversale prête ; n'annonce
ni export par lot ni conformité DNG Lightroom. Cette famille ne relève pas
des sweeps photographiques du générateur.

## Préparation

Deux images A/B, portrait/paysage, copies de travail avec sidecars autorisés ;
fichiers de destination jetables sans toucher aux originaux. Sauvegarder l'INI
avant modification de test. Relever build/OS/DPI, chemins et formats. Données
JSON horodatées et verdicts selon le [socle commun](protocole-commun.md), même
pour une recette sans métrique pixel. Les tests n'autorisent pas la publication
des profils Adobe ou des images du corpus.

## Scénarios

1. Chaque section : champ↔slider, reset, bornes, clavier, sections déroulantes,
   largeur sans scroll horizontal ; mélangeur cercles et grading capture souris.
2. A réglée → B différente → A, fermeture → relance : valeurs et pixels de
   l'export A reproductibles avec mêmes ressources ; image, profil, WB,
   normalisation, optique et annotations restaurés, sans fuite des réglages de B.
3. DCP changé sur A : garder les retouches, ne pas déclencher la navigation
   comme une réouverture ; changements rapides, fermeture pendant rendu,
   résultat final correspondant au dernier état, cache invalidé si ressource change.
4. INI : taille normale/maximisée, sections, couleur de fond, panneaux,
   multi-écrans et écran déconnecté, DPI 100/150/200 %, sauvegarde/reprise.
   Les clés futures inactives ne doivent pas modifier les pixels.
5. Zoom/clic/boutons/Ctrl+/Ctrl−/Ctrl-molette, déplacement et comparaison
   origine/actuel : même géométrie, séparation mobile, aucun export de l'écran
   partagé à la place du rendu courant. Le proxy de zoom n'est pas du RAW 1:1.
6. Export JPEG/PNG/TIFF : type du dialogue = choix, extension cohérente,
   destination différente, nom existant → −2/−3/−4 sans écrasement ; annulation,
   accès refusé/verrou, message utile et absence de temporaire parasite.
   Lire l'ICC, dimensions, profondeur attendue et ouvrir dans un lecteur tiers.
7. DNG : diagnostic séparé ; tester un lecteur indépendant et Lightroom.
   LibRaw qui décode n'est pas preuve de conformité ; garder « non validé »
   jusqu'à une recette réussie, sans assimiler DNG à TIFF.
8. Multisélection/application de réglages : seules les cibles changent,
   annotations conservées, sidecars individuels et erreurs partielles tracées.
   **Export par lot non implémenté** : ne pas le cocher réussi.

## Preuves / automatisation

Tests ciblés selon le code concerné : `settingsQmlTests`, `colorMixerQmlTests`,
`uiPreferencesTests`, `uiPreferencesQmlTests`, `imageViewportQmlTests`,
`imageExportTests`, `editorRawDevelopmentTests`, `testImageGeneratorTests`.
Les lancer via le script build Windows (DLL et plugins Qt préparés), pas en
double-cliquant des exécutables de tests sans leur environnement.
Chronométrer ouverture, changement de profil, restauration et export sur même
machine/corpus ; séparer froid/chaud, dimensions, cache, n répétitions et dispersion.
Les temps ne sont pas des scores de fidélité des pixels.

Consigner les résultats dans les rapports de familles correspondants, sans
dupliquer les campagnes photographiques. Captures/messages complètent les JSON ;
aucune suppression d'image de référence commune sur simple succès d'un test IHM.
