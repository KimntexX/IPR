# ADR-0010 — Compensation de rendu liée au couple boîtier/DCP

**Statut :** accepté et intégré localement ; build Windows et vérifications réussis.
**Date :** 16 octobre 2026. **Branche :** `patch_raw_opening`.

## Contexte

**Évolution du 16 octobre :** la première intégration à trois RAW/ISO décrite
ci-dessous est étendue par l'[ADR-0011](ADR-0011-regles-raw-par-mode-et-configuration-vivante.md).
Le schéma courant est 3, sans liste de photos/ISO ; la configuration est suivie
automatiquement et la calibration demandée par défaut. La séparation entre
baseline source, offset DCP et compensation externe reste inchangée.

Le [§48 du rapport RAW/profils](../validation/analyses/analyse-base-colorimetrique.md#48-résidu-r7--recalage-et-coefficient-candidat--15-octobre-2026)
identifie une compensation R7 Camera Neutral de −0,02 EV avant le DCP.
La valeur DNG source +0,35 EV reste documentée ; la compensation ne constitue
pas une nouvelle mesure de cette métadonnée. L'utilisateur accepte le rendu
et demande son intégration sans modifier le R6, avec une évolution possible
vers des DCP IPR contenant leurs propres paramètres.

## Décision

- Séparer `cameraBaselineExposureEv` (source) et
  `profileExposureCompensationEv` (préférence de rendu) dans les options DCP.
- Composer le gain une seule fois avant les tables HSV/LookTable et la courbe :
  `gain = 2^(BaselineExposureOffset_DCP + baseline_source + compensation_profil)`.
  Le master de sortie reste Rec.2020 linéaire D65 ; aucun gain tardif sRGB.
- Déclarer les compensations dans `profile_rendering` du manifeste existant
  `config/raw-normalization.json`, sans nouveau fichier de configuration ni
  modification des fichiers Adobe. Chaque entrée lie marque, modèle,
  SHA-256 DCP, ISO autorisés, baseline source attendue, EV et provenance.
- Activer l'entrée seulement après résolution de la calibration RAW exacte
  déjà auditée (empreinte source, profil, ISO, noirs, backend, WB caméra).
  Pour le R7 courant : ISO 20000/32000, base +0,35, compensation −0,02.
  Aucune généralisation aux autres RAW du modèle ni aux ISO non testés.
- Une autre caméra, un profil différent/modifié ou un ISO hors périmètre
  n'héritent pas du coefficient : zéro compensation. Une entrée applicable
  invalide/ambiguë provoque le refus de la calibration, sans application partielle.

## Chemin fonctionnel et état

1. À l'ouverture/restauration, l'éditeur retrouve profil et manifeste choisis.
2. `RawDecoder` décode les métadonnées, résout `RawNormalization`, puis prépare
   les options du contexte DCP immuable pour cette image seulement.
3. `CameraProfileEngine` compose le gain avec les paramètres natifs du DCP,
   avant les opérateurs non linéaires ; les retouches manuelles suivent.
4. Les retouches température/nuance réutilisent ces options depuis la source
   caméra : la compensation n'est ni perdue ni cumulée à chaque déplacement.
5. Le statut IHM distingue base, compensation et base effective. La calibration
   RAW doit être activée ; cet ADR ne change pas son choix par défaut (ADR-0007).

Le contenu complet du manifeste participe déjà au cache et à l'empreinte de
restauration. L'identité moteur ajoute `|profile-render=1` ; les anciens pixels
ne sont pas réutilisés sous la nouvelle recette. Aucun nouveau champ `.ipr`
ni duplication du coefficient dans chaque photo : la règle est dérivée du
profil/manifeste persistés. Un changement de profil invalide la sélection et
résout à nouveau ses paramètres. Le chemin rapide sans calibration ne reçoit
pas cette compensation ; un contexte calibré n'est pas réutilisé pour un autre
profil, puisque son niveau blanc peut lui aussi dépendre de la recette.

## Futurs DCP IPR

Privilégier le paramètre natif **BaselineExposureOffset (51109)** lorsqu'un
DCP IPR doit embarquer un décalage de rendu : il est déjà interprété dans le
moteur au même emplacement. Ne pas inventer un tag Adobe ni réécrire leurs
profils. L'auteur doit choisir entre offset natif et compensation externe
équivalente, pour ne pas compter deux fois le même ajustement.

Les paramètres non représentables par les tags DCP documentés pourront être
associés dans les métadonnées de notre bibliothèque de profils, avec schéma,
empreinte, domaine et provenance explicites. Ce déplacement futur n'est pas
implémenté ici ; il ne doit pas mélanger niveaux physiques du RAW, rendu
artistique du profil et réglages manuels de la photo. Un nouveau DCP a une
nouvelle empreinte : il ne récupère pas automatiquement le correctif Adobe R7.

## Conséquences et limites

La correction ajoute une sélection bornée dans le manifeste déjà lu et une
addition au gain existant, sans nouveau décodage, table pleine image ou passe
pixel. Ce n'est pas une mesure de latence. Les R6 ne reçoivent aucune règle.
Les écarts bleus/ombres et les HL ne sont pas déclarés résolus ; validation
R7 bas/moyen ISO et autres boîtiers restent à faire avant extension de portée.

Vérifier tests de sélection/isolation, composition DCP et WB, cache/restauration,
puis rejeu des 16 R6 identiques et des 3 R7 conformes au candidat natif accepté.

Preuve exécutée : [rejeu de production des 19 RAW](../validation/raw/integration-profile-rendering-v001-20261015T220248871933Z.json).
Les 16 témoins R6 et les 3 candidats R7 sont exactement identiques sur la grille
contrôlée au pas 8. Les 22 CTest Windows sont réussis ; ceci ne vaut pas
certification pleine résolution de chaque pixel ni validation de tous les ISO.

Pour une ancienne recette dont le manifeste a changé, l'IHM refuse ses nouveaux
octets tant que leur sélection n'a pas été confirmée : **Choisir le manifeste…**,
puis sélectionner le `config/raw-normalization.json` courant et vérifier
« compensation −0,02 EV ; base effective 0,33 EV ». Ne pas contourner le garde-fou
en modifiant manuellement les empreintes `.ipr` ni supprimer les sidecars.
