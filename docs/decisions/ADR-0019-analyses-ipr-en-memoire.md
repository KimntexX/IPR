# ADR-0019 — États IPR rendus en mémoire pour les analyses

- **Date :** 2026-10-20
- **Statut :** Accepté ; implémenté sur `patch_in_memory_analysis`, validation
  utilisateur et fusion restantes
- **Décideurs :** utilisateur

## Contexte

Chaque campagne écrivait un TIFF 16 bits pleine résolution par état IPR
(environ 258 Mo pour 32 Mpx), que les analyseurs relisaient puis que
l'utilisateur supprimait à la main. Le 20 octobre 2026, `tests/image`
contenait 737 TIFF IPR (190,5 Go) pour 712 références Lightroom (138 Go),
sur un disque à 87 % ; une régénération Lumières complète des 19 scènes
(environ 245 Go) avait été abandonnée faute de place. Le TIFF IPR n'était
qu'un intermédiaire : le JSON de mesures est la seule mémoire canonique.

## Décision

- `src/tools/TestRecipes` porte seul le catalogue des contrôles et valeurs,
  la recette de développement (Camera Neutral, calibration RAW, optique
  désactivée hors série optique), les supports et la déclaration de provenance.
- `iprRenderServer` lit des requêtes JSON sur l'entrée standard et renvoie,
  pour chaque état, un en-tête JSON (dimensions, ICC, déclaration) puis les
  échantillons uint16 RGB de l'export TIFF, via `ImageEditor::renderSrgb16`.
  Il garde en mémoire le master de la source courante ; un état à décodage
  propre (balance automatique, optique) est décodé puis libéré.
- `tests/analysis` avec `--base` rend les états IPR des sources à la racine de
  base et lit les TIFF Lightroom. Les TIFF IPR de base ne sont plus analysés.
  Une scène par serveur ; le nombre de serveurs suit la mémoire libre
  (`--render-workers` pour l'imposer).
- `iprGenerateTests` reste l'outil de revue visuelle, à la demande, avec les
  mêmes recettes. Les références Lightroom restent des TIFF exportés une fois.

## Conséquences

- **Bénéfices :** aucun TIFF IPR écrit pour mesurer ; place disque stable
  d'une campagne à l'autre ; plus de suppression manuelle de ces exports ;
  campagnes complètes de nouveau possibles.
- **Coûts et risques :** refaire une analyse impose de refaire le rendu avec
  le même exécutable (SHA-256 consigné). Le parallélisme est limité par la
  mémoire : environ 2,5 Gio estimés par serveur pour 32 Mpx, à mesurer ; sur le
  poste de 16 Go chargé, un seul serveur tient souvent. L'équivalence entre
  Windows, Linux et macOS n'est pas vérifiée.
- **Compatibilité / migration :** les JSON historiques restent valides et
  inchangés. Les nouveaux JSON indiquent `ipr_pixels.kind = in_memory_render` ;
  l'empreinte des pixels IPR porte sur les échantillons, pas sur un fichier.
- **Validation :**
  - test natif sur un petit corpus : les 50 états Lumières rendus en mémoire
    sont identiques bit à bit aux TIFF du générateur, ICC et réglages compris ;
    campagnes de huit groupes sans aucun fichier écrit ;
  - CR3 réel (6V1A0007) : neutre, Blancs +100 et Contraste +50 identiques bit
    à bit aux TIFF du 18 octobre 2026 ; Hautes lumières −100 diffère, comme
    attendu, le modèle HL ayant été réajusté le 19 octobre (commit 818ffca) ;
  - débit mesuré le 20 octobre 2026 : 100 états Lumières sur deux CR3
    (6V1A0007, 6V1A0043), un serveur (4,2 Gio libres), 437 s, soit 4,4 s par
    état rendu, transféré et mesuré ; 100 `measured`, aucun problème.
    L'ancien générateur ne tenait lui aussi qu'un rendu à la fois sur ce poste :
    le gain porte sur le disque, l'écriture/relecture et le passage unique,
    pas sur le temps de rendu lui-même.

## Liens

- Code : `src/tools/TestRecipes.*`, `src/tools/render_server.cpp`,
  `src/tools/generate_test_images.cpp`, `tests/analysis/render.py`
- Tests : `testImageGeneratorTests`, `tests/test_analyse_groupes.py`
- Guides : [analyseurs](../validation/tools/analyseurs-groupes.md),
  [générateur](../validation/tools/generateur-images-tests.md)
- Règle : section « Génération des images de tests » d'`AGENTS.md`
