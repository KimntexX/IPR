# Fonctions du panneau Modifier de Lightroom — état dans IPR

Inventaire du 26 octobre 2026 : chaque fonction des panneaux de réglage de Lightroom
(application de bureau), son état dans IPR et ce qui la planifie. « Validé » renvoie à la
[matrice de validation](../validation/matrice-validation.md) ; une fonction absente de
tout document est notée **non planifiée**. Les comparaisons se font en boîte noire, sur
des exports Lightroom, jamais par rétro-ingénierie.

## Inventaire

| Panneau Lightroom | Fonction | État dans IPR | Planification |
| --- | --- | --- | --- |
| Profil | Profils caméra (Camera Neutral…) | Fait : profils DCP par boîtier, choix par image | Base colorimétrique : parité LR non validée |
| Profil | Profils créatifs, intensité du profil | Absent | Non planifié |
| Général | Automatique (tonalité) | Absent | Non planifié |
| Général | Noir et blanc | Fait : niveaux de gris | Mélangeur N&B absent (voir Couleur) |
| Général | HDR (édition en haute gamme dynamique) | Absent ; le moteur garde des valeurs > 1 | Non planifié |
| Lumière | Exposition, contraste, hautes lumières, ombres, blancs, noirs | Fait ; ombres, noirs, hautes lumières et blancs validés | Exposition et contraste : comparaison LR aux valeurs intermédiaires à faire |
| Lumière | Courbe par points (RVB, rouge, vert, bleu) | Fait, non validé | Campagne LR prête (`IPR Courbes`, 36 états, `patch_curves`) |
| Lumière | Courbe paramétrique (hautes lumières, clairs, sombres, ombres, séparateurs) | Absent | Non planifié |
| Lumière | Outil de réglage ciblé sur l'image | Absent | Non planifié |
| Couleur | Balance des blancs : température, nuance, telle quelle, automatique | Fait, validé (avant la base Rec.2020 : revalidation courte) | — |
| Couleur | Balance des blancs : pipette, préréglages (lumière du jour, nuageux…) | Absent | Non planifié |
| Couleur | Vibrance, saturation | Fait, validé le 22 octobre | — |
| Couleur | Mélangeur TSL 8 bandes | Fait, calé sur LR | Revue du magenta |
| Couleur | Mélangeur noir et blanc | Absent | Non planifié |
| Couleur | Couleur du point | Interface seule, désactivée (IHM-CONN-011) | Moteur à caler sur des exports LR |
| Couleur | Color grading (3 zones, global, fusion, balance, luminance) | Fait, validé le 26 octobre | — |
| Effets | Texture, clarté, correction du voile | Fait, validé le 25 octobre (réserves) | — |
| Effets | Vignettage après recadrage (quantité, milieu, rondeur, contour progressif, hautes lumières, style) | Absent | Non planifié ; dépend du recadrage |
| Effets | Grain : quantité | Fait, conforme à LR | — |
| Effets | Grain : taille, rugosité | Absent | Décision à prendre |
| Détail | Netteté : gain, rayon, masquage | Fait, non validé | Campagne LR à préparer |
| Détail | Netteté : détail ; netteté IA (juin 2026) | Absent | Non planifié |
| Détail | Réduction du bruit : luminance, couleur | Fait, en étude (Codex, `patch_noise`) | Étude des voisinages en cours |
| Détail | Bruit : détail, contraste, lissage ; débruitage IA ; super résolution, détails RAW | Absent | Non planifié |
| Optique | Profil d'objectif (distorsion, vignettage), aberration chromatique latérale | Fait (LCP), tests synthétiques | Comparaison LR sur quelques optiques à faire |
| Optique | Correction manuelle de distorsion et de vignettage | Absent | Non planifié |
| Optique | Défrangeage (violet, vert) | Absent | Non planifié |
| Optique | Flou d'objectif (IA) | Absent | Non planifié |
| Géométrie | Upright (auto, niveau, vertical, complet, guidé) | Absent | Non planifié |
| Géométrie | Transformations manuelles (vertical, horizontal, rotation, aspect, échelle, décalages) | Absent | Non planifié |
| Recadrer | Recadrage, format, redressement, rotation, miroir | Absent | Non planifié |
| Masquage | Pinceau, dégradés linéaire et radial, plages de couleur, de luminance et de profondeur | Absent | Non planifié |
| Masquage | Sujet, ciel, arrière-plan, objets, personnes (IA) | Absent | Non planifié |
| Retouche | Correcteur, tampon, suppression (générative ou non), suppression des distractions, yeux rouges | Absent | Non planifié |
| Flux | Copier/coller des réglages, application et export par lot | Fait (`patch_batch`, 26 octobre) | Recette manuelle sur RAW |
| Flux | Préréglages utilisateur, historique et annulation, instantanés, versions | Absent | Non planifié |
| Flux | Export JPEG, PNG, TIFF ; DNG | Fait ; DNG à corriger pour Lightroom | — |
| — | Flou gaussien | Propre à IPR (pas dans LR) | Cible à définir |

## Ordre proposé

1. **Passage en beta** : la règle demande de valider toutes les fonctions de réglage déjà
   présentes. Restent les courbes (exports prêts), la netteté, l'exposition et le contraste
   comparés à LR, le bruit (étude en cours), la décision sur la taille et la rugosité du grain,
   la revalidation courte de la balance des blancs.
2. **Réglages globaux manquants**, chacun calé sur des exports LR comme les précédents :
   couleur du point (interface prête), courbe paramétrique, mélangeur noir et blanc,
   défrangeage, pipette et préréglages de balance des blancs, distorsion et vignettage manuels.
3. **Flux de travail** : historique et annulation, préréglages, instantanés ; puis recadrage,
   redressement et miroir, nécessaires au vignettage après recadrage et à la géométrie
   (Upright, transformations).
4. **Retouches locales** : masques (pinceau, dégradés, plages), correcteur et tampon, yeux
   rouges. Les fonctions IA de Lightroom (masques sujet/ciel, suppression générative,
   débruitage, netteté, flou d'objectif, super résolution) demandent des modèles et des
   licences à étudier séparément.

Chaque nouveau panneau intégré fait évoluer la version mineure après la beta (règle de
versionnage d'`AGENTS.md`).
