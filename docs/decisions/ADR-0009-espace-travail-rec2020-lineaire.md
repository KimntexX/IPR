# ADR-0009 — Master de travail Rec.2020 linéaire

- Date : 15 octobre 2026.
- Statut : accepté ; port du master implémenté localement le 15 octobre 2026,
  qualification visuelle et multi-OS restant à faire avant livraison.
- Complète [ADR-0008](ADR-0008-separation-travail-affichage-export.md) ; précise
  l'espace du pipeline décrit par [ADR-0002](ADR-0002-pipeline-lineaire-et-calibration.md).

## Contexte

Le comparatif post-DCP sur sept scènes distingue changement de coordonnées,
opérateurs dépendants des primaires et pertes antérieures dans les tables DCP.
Les neutres et opérations invariantes restent équivalents numériquement.
Rec.2020 est choisi pour le master photographique, non parce qu'il supprimerait
à lui seul l'écrêtage ou reproduirait Lightroom.

Preuve : [campagne immuable v001](../validation/raw/comparatif-espaces-travail-v001-20261015T100527950069Z.json).

## Décision

- Master unique aux primaires Rec.2020, blanc D65, transfert linéaire,
  `CV_32FC4`, alpha droit. Conserver les coordonnées finies négatives et > 1.
- Convertir les pixels ; ne jamais simplement remplacer leur étiquette.
- Garder les tables DCP dans leur domaine ProPhoto prévu. Rec.2020 concerne
  leur sortie vers le master, pas la réinterprétation de leurs coordonnées.
- Conserver dans un premier lot les destinations sRGB actuelles. La gestion
  ICC écran de l'ADR-0008 reste une évolution distincte ; ni PQ ni HLG ne sont
  activés par cette décision.
- Requalifier chaque opérateur dépendant des primaires, la restauration et les
  sorties. Ne pas conserver un moteur historique sélectionnable en production.
  Les preuves historiques JSON et les comparaisons diagnostiques restent conservées.

## Conséquences et limites

Le master du code courant est désormais Rec.2020 linéaire. Ce port n'active pas
la calibration par défaut, ne remplace pas LibRaw et ne valide pas les essais
HL. Il impose des conversions explicites, une identité de moteur/cache
renouvelée et des tests de tous les groupes photographiques avant livraison.
La taille d'un tampon RGBA float ne change pas, mais les temporaires et le
temps de calcul doivent être mesurés dans l'application.

Le [plan d'intégration](../architecture/integration-rec2020.md) conserve les
lots, risques et critères d'arrêt et distingue l'étude initiale du port réalisé.
Aucune livraison Git ni validation visuelle globale n'est déclarée.
