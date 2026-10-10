# Règles documentaires

## Analyses fonctionnelles — règle du 9 octobre 2026

Règle demandée par l'utilisateur, applicable aux prochaines analyses et reprises :

- Maintenir **un seul rapport évolutif par ensemble de fonctionnalités**,
  et non par date, version ou curseur. Lumières regroupe exposition, contraste,
  ombres, noirs, blancs et hautes lumières. Distinguer les campagnes dans ce
  rapport ; les anciens documents restent historiques ou des renvois, sans
  être alimentés en parallèle.
- Les **JSON de mémoire des tests, versionnés et horodatés, font autorité sur
  les données**. Rapports, tableaux, graphiques et CSV sont des interprétations
  ou dérivés ; ils ne remplacent pas les JSON.
- Stocker les JSON canoniques sous `docs/validation/raw/`, suivant
  le [contrat de conservation](validation/data/README.md). Une correction
  crée une nouvelle version horodatée, sans écraser une campagne publiée.
- Avant clôture, préserver mesures par scène/état, unités, audits, exclusions,
  provenance, réglages, versions des outils et protocole. Ne pas se limiter
  aux moyennes ou aux captures de tableaux.
- Règle précisée le 10 octobre 2026 : après chaque fonctionnalité analysée,
  supprimer les exports IPR si le résultat est négatif ; supprimer les exports
  IPR et Lightroom de la série si le résultat est positif et validé. Un résultat
  ambigu, incomplet ou une mémoire JSON incomplète bloque cette suppression.
  Préserver les RAW, les neutres communs et les séries non encore analysées.
  Journaliser les chemins exacts et tailles supprimés après vérification de leur
  confinement dans le corpus. Signaler les limites de reproductibilité sans
  pixels : une empreinte n'est pas une sauvegarde.
- Les anciennes données JSON/TXT/CSV restent historiques : ne pas inventer
  les métadonnées manquantes ni annoncer une migration complète non vérifiée.

## Conventions de rédaction LaTeX

Règle demandée le 8 octobre 2026, applicable à tous les nouveaux documents LaTeX sous `docs/` et aux prochaines reprises de mise en page des documents existants, y compris Scholar et le manuel technique.

- Le corps du texte est en **12 pt**.
- L'interligne du corps du texte est **1,5**, avec `setspace` et `\onehalfspacing`.
- Chaque chapitre commence sur une **nouvelle page**. Préférer les classes `report` ou `book`, dont `\chapter` assure ce comportement. Pour un document `article`, les sections tenant lieu de chapitres doivent être précédées de `\clearpage`.
- Les paragraphes sont séparés dans les sources par une ligne vide, sans retrait de première ligne, avec un espacement vertical uniforme de `0.5\baselineskip`. Ne pas utiliser `\\` comme séparation de paragraphes.
- L'espacement des paragraphes suit donc l'interligne du texte ; **chaque paragraphe ne commence pas sur une nouvelle page**. Les sauts de page restent automatiques, sauf aux limites des chapitres.
- Les tailles des titres, légendes, notes et blocs de code peuvent être adaptées à leur rôle ; ne pas réduire le corps du texte pour faire tenir une page.

Préambule de référence pour un nouveau document :

```latex
\documentclass[12pt,a4paper,oneside]{report}
\usepackage{setspace}
\onehalfspacing
\setlength{\parindent}{0pt}
\setlength{\parskip}{0.5\baselineskip}
```

Cette convention ne déclenche pas, à elle seule, une réédition ni une recompilation globale des PDF existants. Lors d'une reprise de mise en page, vérifier aussi les coupures, les tableaux, les diagrammes et la pagination du PDF généré.
