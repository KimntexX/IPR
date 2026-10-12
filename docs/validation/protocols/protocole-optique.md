# Protocole — Corrections optiques

Version 1 — 10 octobre 2026. Statut : recette prête, validation photographique
à effectuer. Appliquer le [socle commun](protocole-commun.md).

Choisir un RAW avec LCP compatible et identifiable : lignes droites en bord
de cadre, aplat clair pour vignettage, bord contrasté au centre et dans les
coins pour CA latérale. Le corpus peut servir au repérage ; une prise dédiée
est préférable si ces conditions n'y sont pas réunies. Déclarer focalisation,
focale, ouverture et identités RAW/LCP, sans attribuer au modèle une correction
de franges longitudinales non implémentée.

## Matrice des options

| État | Correction objectif | Suppression CA |
| --- | --- | --- |
| A | Désactivée | Désactivée |
| B | Activée | Désactivée |
| C | Désactivée | Activée |
| D | Activée | Activée |

Même RAW/DCP/WB/réglages ; modifier seulement ces options. Relever statut de
profil, export et crop natif à 100 %. Le générateur ne propose pas cette
matrice : IHM ou test moteur dédié, jamais un identifiant CLI inventé.

Mesurer courbure/déplacement des lignes, ratio centre/coins d'un aplat,
séparation des contours R/G/B, bords exposés/remplis et dimensions. Éclairage
non uniforme et optique mal identifiée limitent la conclusion. Corriger la
géométrie change les correspondances pixels : pas de MAE directe LR/IPR
sans recalage. CA seule et objectif seul doivent être indépendants ; absence
de LCP/paramètre compatible doit donner un état explicite, pas un faux succès.

Tester cache, changements rapides, retour A, redémarrage/sidecar, ressource
absente et cohérence aperçu/export. `cameraProfileEngineTests` et
`editorRawDevelopmentTests` couvrent des cas synthétiques, pas la qualité
photographique de tous les objectifs. Rapport unique Optique à créer lors de
la campagne ; renvoyer depuis Base RAW sans dupliquer ses mesures.
