# Règles documentaires

## Génération des documents en anglais — règle du 11 octobre 2026

La génération des versions anglaises des documents INDEX.md et README.md dans tout le projet est effectuée par l'assistant IA sur la branche **`patch_documentation`**. Cela inclut également la traduction des fichiers de documentation technique LaTeX et des documents markdown dans les sous-dossiers.

Les traductions doivent :
- Conserver la terminologie technique exacte (DCP, LCP, RAW, EV, etc.)
- Maintenir la structure et les liens internes
- Respecter le format des fichiers source (LaTeX, markdown)
- Ajouter les bascules de langue dans les en-têtes : `*[Français](README.md) | [English](README_en.md)*`
- Être validées avant fusion dans `dev`
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
- La suppression des exports après analyse est manuelle, par l'utilisateur.
  Aucun générateur ou analyseur ne déclenche de nettoyage automatique.
  Préserver les RAW et les mémoires JSON ; signaler les limites de
  reproductibilité sans pixels : une empreinte n'est pas une sauvegarde.
- Les anciennes données JSON/TXT/CSV restent historiques : ne pas inventer
  les métadonnées manquantes ni annoncer une migration complète non vérifiée.

Précision du 11 octobre 2026 : pour les prochaines générations, les exports
sont placés dans les dossiers existants partagés entre logiciels, sans dossiers
datés ni fichiers auxiliaires de génération. Leur suppression après analyse
est désormais **manuelle, par l'utilisateur** ; ne plus appliquer automatiquement
la suppression conditionnelle historique du 10 octobre. Les JSON versionnés et horodatés des
mesures restent la source de vérité. Décrire le générateur courant seulement,
sans maintenir les anciens modes ou leurs tests de compatibilité.

## Protocoles fonctionnels — consolidation du 10 octobre 2026

- Maintenir un protocole évolutif par groupe sous `validation/protocols/`,
  sans date dans le nom des nouveaux fichiers. Les campagnes portent leurs
  propres versions et horodatages dans les JSON immuables.
- Utiliser `validation/protocols/protocole-commun.md` pour les conditions
  transversales ; éviter de recopier ce socle dans chaque recette.
- Les anciens protocoles de curseurs fusionnés restent des renvois stables,
  pas des documents à maintenir en parallèle. Conserver les méthodes
  spécialisées distinctes lorsqu'elles testent une autre hypothèse (exemple :
  niveaux blancs capteur Canon versus curseur Blancs).
- Décrire seulement les options/outils réellement disponibles. Signaler les
  analyseurs manquants, les conversions 8 bits/redimensionnements historiques
  et les associations incompatibles avec les déclarations TIFF courantes.
- Un protocole prêt n'est pas une validation : ne pas modifier les verdicts
  historiques ni réutiliser une campagne de profil différent sans audit.

## Conventions de rédaction LaTeX


Règle demandée le 8 octobre 2026, applicable à tous les nouveaux documents LaTeX sous `docs/` et aux prochaines reprises de mise en page des documents existants, y compris Scholar et le manuel technique.

- Le corps du texte est en **12 pt**.
- L'interligne du corps du texte est **1,5**, avec `setspace` et `\onehalfspacing`.
- Chaque chapitre commence sur une **nouvelle page**. Préférer les classes `report` ou `book`, dont `\chapter` assure ce comportement. Pour un document `article`, les sections tenant lieu de chapitres doivent être précédées de `\clearpage`.
- Les paragraphes sont séparés dans les sources par une ligne vide, sans retrait de première ligne, avec un espacement vertical uniforme de `0.5\baselineskip`. Ne pas utiliser `\\` comme séparation de paragraphes.
- L'espacement des paragraphes suit donc l'interligne du texte ; **chaque paragraphe ne commence pas sur une nouvelle page**. Les sauts de page restent automatiques, sauf aux limites des chapitres.
- Les tailles des titres, légendes, notes et blocs de code peuvent être adaptées à leur rôle ; ne pas réduire le corps du texte pour faire tenir une page.
- Conserver les paragraphes explicatifs courts et les blocs de consignes
  (par exemple « Gestes et clavier ») sur une seule page, avec leur intitulé.
  S’il manque de place, déplacer le bloc entier à la page suivante ; ne pas
  laisser une consigne reprendre au début de la page suivante au milieu d’une
  phrase. Utiliser les mécanismes de pagination LaTeX (`samepage`, réservation
  d’espace ou pénalité de coupure), pas une succession de `\\` ou de lignes vides.
- Éviter les lignes veuves/orphelines. Pour un manuel composé de paragraphes
  courts tenant chacun sur une page, `\interlinepenalty=10000` avec
  `\clubpenalty=10000`, `\widowpenalty=10000` et `\raggedbottom` permet de
  conserver ces paragraphes entiers sans étirer les blancs. Pour un passage
  trop long, le découper en paragraphes cohérents ou autoriser localement une
  coupure maîtrisée ; ne pas rendre un bloc plus haut qu’une page insécable.

Préambule de référence pour un nouveau document :

```latex
\documentclass[12pt,a4paper,oneside]{report}
\usepackage{setspace}
\onehalfspacing
\setlength{\parindent}{0pt}
\setlength{\parskip}{0.5\baselineskip}
```

Cette convention ne déclenche pas, à elle seule, une réédition ni une recompilation globale des PDF existants. Lors d'une reprise de mise en page, vérifier aussi les coupures, les tableaux, les diagrammes et la pagination du PDF généré.
