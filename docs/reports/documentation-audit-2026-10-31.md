*[Français](documentation-audit-2026-10-31.md) | [English](documentation-audit-2026-10-31_en.md)*

# Documentation — réconciliation et traduction des documents vivants

Date : 31 octobre 2026. Branche : `patch_documentation`, worktree dédié.
Référence de production **locale** : `dev` `b19b4c3`. Pas de contrôle de la
CI distante ni de certification de toutes les plateformes dans cette passe.

## 1. Périmètre demandé et limites

L'utilisateur demande de vérifier l'actualité de la documentation et de
traduire les documents manquants, en donnant priorité aux **documents vivants**
(manuels, architecture, décisions, protocoles). Les archives historiques ne
sont pas prioritaires. Les JSON de mesures ne sont ni traduits ni réécrits.

**Ce lot ne termine pas toute la demande.** Les références restantes sont
inventoriées ; une présence anglaise n'atteste ni son actualité ni sa qualité.
Les anciennes dates et réserves de validation ne deviennent pas des validations
du31octobre par simple relecture ou traduction.

## 2. Synchronisation et corrections françaises

La branche documentaire avait 6 commits propres et 198 commits de retard sur
`dev`. Fusion locale `a9a5d4d`, sans squash/rebase ni modification de `dev`.
Les conflits ont été réconciliés en conservant les corrections de présentation
et les états validés plus récents du moteur ; pas de sélection globale aveugle
d'une version de la documentation.

Corrections principales :

- manuel utilisateur : commandes réellement raccordées, collage avec balance
  telle quelle du destinataire, export par lot, roue Global/fusion/balance,
  commande globale de bruit et mode signé expérimental ;
- architecture bruit : distinguer chemin classique et pré-LCP/DCP, état de
  session, défaut désactivé, limites de validation et plafond des threads ;
- ADR-0002/0005 : identifier les contrats historiques sRGB/mélangeur et anciens
  commutateurs d'analyse, sans les annoncer comme recettes actuelles ;
- ADR-0012/0018 et roadmap décodeurs : développement flottant déjà intégré,
  décompression/métadonnées encore LibRaw, priorité Canon et LibRaw conservé
  pour les autres marques, attente du retour Canon Developer ;
- matrice : actualiser la ligne Bruit sans déclarer la qualité universelle
  acquise ; variantes du banc non promues, recherche incrémentale suspendue ;
- manuel technique français : ajouter le constat du développement flottant,
  sans le confondre avec un nouveau codec ;
- cours4 : distinguer le modèle pédagogique historique du mélangeur des
  tables du moteur courant ; reconstruire le PDF associé.

La feuille de route bêta et le rapport RAW courants ont été repris depuis
mes commits de `patch_raw_bench`, avec 74 nouvelles preuves JSON déjà commitées.
Les outils de banc et les modifications de moteur de cette branche ne sont
pas importés. Leurs résultats restent des études, pas une livraison du moteur.
Les travaux non commités d'autres agents n'ont pas été repris.

## 3. Traductions réalisées dans ce lot

- livre utilisateur complet : racine LaTeX et 21 chapitres, raccourcis inclus,
  avec les trois familles futures laissées réservées comme en français ;
- README du livre et compilation des deux langues (`-Language fr|en|all`) ;
- les 20 ADR et leur index anglais ;
- feuille de route bêta ;
- contrats d'import commun et de contrôle mémoire ;
- actualisation anglaise de l'architecture bruit, de l'index Architecture et
  de l'index documentaire ;
- présent compte rendu bilingue.

Les liens vers un document non encore traduit restent vers le français,
plutôt que pointer vers un équivalent fictif. Les versions anglaises existantes
du manuel technique demandent encore une réconciliation : un avertissement
explicite figure dans leur README, notamment pour l'ancien master sRGB et les
anciennes gardes de calibration.

## 4. Inventaire et prévention des dérives

`python scripts/audit_translations.py` distingue absence, présence non vérifiée,
source modifiée, mauvais rattachement et empreinte source concordante.
L'empreinte est le SHA-256 du texte français UTF-8/LF, indépendant des fins de
ligne Git Windows/Linux. Elle ne remplace pas la vérification du sens.

Instantané versionné :
[inventaire v001](documentation-translation-inventory-v001-20261031.json).
Il porte les chemins, tailles, références françaises et périmètres ; les
archives, rapports d'analyse et cours restent distingués des références vivantes.
AGENTS.md et les arbres de build générés sont exclus, pas déclarés traduits.

## 5. Vérifications et suite non terminée

- 3 tests de l'inventaire : statuts, erreurs de rattachement et neutralité CRLF/LF ;
- compilation des livres FR/EN : 49 pages chacun, 12 pt/interligne 1,5 ;
- aucun débordement ni référence indéfinie relevés dans les journaux finaux ;
- contrôle visuel ciblé de deux pages anglaises, dont les raccourcis ; pas
  inspection exhaustive de chaque page ;
- manuel technique français et cours4 recompilés ;
- contrôles documentaires UTF-8/JSON/liens locaux et `git diff --check` ;
  pas de vérification exhaustive des ancres ni des liens externes.

Priorités suivantes, avant de déclarer les documents vivants entièrement à jour :

1. Réconcilier les chapitres techniques anglais et leurs diagrammes avec les
   sources françaises actuelles, puis compiler/relire le PDF anglais.
2. Traduire les références longues restantes : moteur de profils, pipeline,
   roadmaps et spécifications ; ne pas reprendre leurs anciennes hypothèses
   de décodage sans les comparer au code courant.
3. Traduire et vérifier les protocoles et guides d'analyse/génération.
4. Revoir les autres versions anglaises existantes, dont les README de build,
   CI et validation, sans confondre « fichier présent » et « document relu ».
5. Traiter ensuite les cours et rapports suivant la priorité utilisateur ;
   archives historiques en dernier.

Aucun code photographique ni fichier image du corpus modifié. Aucun push,
fusion dans `dev`/`main`, tag, nouvelle campagne photo ou nouvelle validation
de rendu n'est déclaré par ce travail documentaire.
