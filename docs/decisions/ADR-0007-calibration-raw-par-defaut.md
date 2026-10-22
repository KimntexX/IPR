# ADR-0007 — Calibration RAW active par défaut

**Date :** 15 octobre 2026. **Statut :** décision acceptée par l'utilisateur,
comportement par défaut implémenté le 16 octobre 2026 ; panneau avancé intégré
dans `dev`, constat documentaire du 22 octobre 2026.

## Contexte

La calibration radiométrique auditée corrige le niveau blanc et l'exposition
de base avant le rendu DCP. Elle est actuellement activée explicitement
dans l'IHM et mémorisée par image. Le corpus couvert reste limité à des
couples RAW/DCP exacts ; l'activation demandée n'atteste pas son application.

## Décision

La calibration sera **active par défaut**. Sa désactivation sera accessible
dans des **paramètres avancés à définir ultérieurement**, pas dans le parcours
ordinaire des réglages photographiques. Cette décision porte sur la calibration
RAW auditée, pas sur la balance des blancs automatique ni les corrections optiques.

Conserver les gardes d'éligibilité et un diagnostic séparant l'intention
« activée » de l'état effectif « appliquée / non applicable / erreur ».
Ne jamais appliquer une recette d'un autre RAW ou d'un autre DCP par défaut.
Un fichier non couvert doit être signalé comme tel : l'activation par défaut
n'étend pas le domaine de validation et ne vaut pas calibration universelle.

## Conséquences et travaux restant à définir

- Définir le panneau avancé, la portée globale ou par image de la dérogation,
  sa persistance et le comportement à la réinitialisation.
- Définir explicitement le traitement des sidecars existants désactivés ; ne
  pas modifier silencieusement les retouches utilisateur lors de cette note.
- Appliquer la politique de manière cohérente à l'IHM, au générateur et aux
  exports, en traçant l'état effectif et la recette du rendu.
- Tester première ouverture, restauration, changement de profil, cache et
  fichiers non éligibles avant livraison du nouveau comportement.

La décision initiale du 15 octobre ne changeait pas le code. Sa mise en œuvre
du 16 octobre et le traitement des recettes existantes suivent désormais
l'[ADR-0011](ADR-0011-regles-raw-par-mode-et-configuration-vivante.md) : défaut actif,
configuration suivie automatiquement, désactivation enregistrée respectée,
règles par modèle/mode sans liste de photos. Les contraintes de corpus décrites
dans le contexte précédent sont historiques, pas les gardes du schéma courant.

## Constat d'intégration — 22 octobre 2026

Le panneau Préférences / Avancé · RAW expose l'interrupteur pour l'image
ouverte et le choix du manifeste. La portée courante est par image et persistée
par sidecar, pas une préférence globale remplaçant toutes les recettes.
La demande globale d'évolution reste suivie dans les raccordements IHM.
Ce constat ne remplace pas les décisions et conditions historiques ci-dessus.

## Références

- [Architecture du moteur RAW/profils](../camera-profile-engine.md).
- [ADR-0002 — Pipeline linéaire et calibration](ADR-0002-pipeline-lineaire-et-calibration.md).
- [Rapport évolutif, §39](../validation/analyses/analyse-base-colorimetrique.md#39-réexports-investigation-de-lactivation-et-étude-hl--15-octobre-2026).
