# Plan — `ImageEditor` en orchestrateur

État : **plan, aucun code**, 5 novembre 2026. Prolonge la décision de l'utilisateur du 19 octobre et la règle
d'[AGENTS.md](../../AGENTS.md) : `ImageEditor` évolue vers un simple cadenceur de modules de traitement.

## 1. Où on en est

Mesuré sur `patch_ui_object_removal` (`82b0a4e`), avec la Retouche.

| Mesure | Valeur |
| --- | --- |
| `imageeditor.cpp` | 3 476 lignes, 187 fonctions, 47 inclusions |
| `imageeditor.h` | 617 lignes |
| Propriétés QML | 50 |
| Commandes QML | 106 |
| Variables membres | 99 |
| Boucles par pixel | 1 accès direct, aucun `parallel_for_` |
| Appels OpenCV | 20, de conversion et de réduction surtout |

**Le traitement d'image est déjà presque entièrement sorti**, dans `ToneRecovery`, `LocalAdjustments`, `Retouch`,
`DetailProcessing` et les autres modules. Ce qui reste est de l'orchestration **mêlée à quatre autres métiers** :

| Métier | Lignes environ | Ce que c'est |
| --- | --- | --- |
| Accesseurs, réglages un par un et divers | 1 180 | 87 fonctions, souvent la même sur 5 lignes |
| Chargement | 500 | `beginLoad`, `applyLoadedImage`, proxy, préchargement, clés de cache |
| Chaîne de rendu | 310 | `renderWorkingImage`, `renderTones`, `renderPreview`, clés d'étages |
| Export et lots | 290 | `exportImage`, `exportFiles`, `exportSelection`, `applyToFiles` |
| Développement RAW et profils | 215 | Options, profils, statut |
| Recette et compagnon | 170 | Sauvegarde, restauration, copier-coller |
| Masques, géométrie, retouche, historique, métadonnées | 410 | Commandes par panneau |

Ces lignes viennent d'un classement par nom de fonction : **c'est un ordre de grandeur**, à affiner au premier palier.

## 2. La cible

```text
QML
 └─ ImageEditor : façade QObject. Tient l'état courant, relie les modules, publie les signaux.
     ├─ RenderChain      fonctions libres : recette + source → image. Aucun état, aucun Qt d'interface.
     ├─ ImageSession     chargement, proxy, préchargement, source courante.
     ├─ EditSession      réglages courants, validation, historique, compagnon.
     ├─ ExportService    export, lots, sélection.
     ├─ RawController    développement, profils, statut.
     └─ Panneaux         geometry, masks, retouch (QObject exposés, comme editor.stacking).
```

**Critères de fin**, comme objectifs et non comme preuves : `imageeditor.cpp` sous 800 lignes, aucune boucle par
pixel, aucun appel OpenCV hors types transmis, et un nouveau panneau qui n'ajoute **aucune** fonction à `ImageEditor`.
`StackSession` montre déjà ce modèle : `editor.stacking` est un objet à part, et l'éditeur ne sait presque rien de lui.

**Règle de découpage.** Selon AGENTS.md : des fonctions libres là où il n'y a pas d'état, une classe seulement pour
un état, un invariant ou une ressource. Pas d'abstraction sans bénéfice concret, et pas de découpage pour un score.

## 3. Ordre des paliers

Du plus sûr au plus délicat. Chaque palier est une fusion à part, sans aucun changement de comportement ni de rendu.

| Palier | Travail | Risque | Protection |
| --- | --- | --- | --- |
| **E0** | Filet de sécurité : mesurer la couverture réelle, ajouter les tests manquants, figer l'état de référence | Nul | Suite verte, parité de rendu de référence |
| **E1** | `RenderChain` : sortir `renderWorkingImage`, `renderTones`, `stageKeys`, `cropped`, `maskValues` | **Faible** : déjà statiques | Parité octet par octet sur le corpus. **Fait**, voir ci-dessous |
| **E2** | Réglages par table : un seul accès par nom, à la place des 87 fonctions proches | Faible | Tests existants ; la table pilote aussi recette et clés. **Fait en partie**, voir ci-dessous |
| **E3** | `ExportService` : export, lots, sélection | Moyen : tâches, annulation | Tests d'export et de lot ; fichiers identiques au bit près |
| **E4** | `RawController` : options, profils, statut | Moyen | `editorRawDevelopmentTests`, parité |
| **E5** | Panneaux `geometry`, `masks`, `retouch` en objets exposés | Moyen : **contrat QML** | Tests QML ; IHM-CONN à jour ; moteur puis QML sur branche enfant |
| **E6** | `EditSession` : réglages, historique, compagnon | **Élevé** : au centre de tout | Tests d'historique et de restauration ; conversion `.ipr` intacte |
| **E7** | `ImageSession` : chargement, proxy, préchargement | **Élevé** : concurrence, mémoire, latence d'ouverture | `editorImageCacheTests`, mesures d'ouverture, contrôles de ressources |
| **E8** | Reste : façade, nettoyage, retrait de ce qui ne sert plus | Faible | Suite complète |

**E1 d'abord, parce qu'il a le plus de valeur** : c'est lui qui ouvre les trois chantiers suivants.

### État au 5 novembre 2026

Branche locale `patch_render_chain`, construite sur la retouche B3 en attente de validation, **à ne fusionner qu'après elle**.
`imageeditor.cpp` passe de 3 476 à 2 917 lignes.

- **E0 :** `render_chain_test` protège désormais le contrat de la chaîne : source partagée pour une recette neutre, refus des sources
  invalides, chaque étage actif, cache sans effet sur les pixels, surimpressions, recadrage. Aucun outil de couverture n'est installé :
  **la couverture n'a pas été mesurée**.
- **E1 : parité sur deux niveaux.** Sur le corpus réel, le serveur de rendu donne des pixels et une déclaration identiques avant et
  après sur [934 états sur 934](../validation/raw/render-chain-parity-v001-20261105.json), un sur onze des 10 266 états, 29 scènes.
  Sur image de synthèse, les hachages sont identiques sur 26 états à deux échelles, en couvrant ce que le serveur n'exerce pas :
  aperçu, cache, surimpressions, masques, retouche. Cette seconde vérification a été faite avec une sonde temporaire, non conservée.
- **E2 :** 21 fonctions quasi identiques cèdent la place à `setAdjustment` et `AdjustmentTable`, qui porte noms et bornes. **Restent hors de la
  table** la recette, les clés de cache et les 40 fonctions restantes, ce qui demandera plus que ce palier sans risque : à reprendre
  avec E6. Une valeur qui n'est pas un nombre est désormais refusée, seul changement de comportement.

## 4. Ce que ce plan débloque

| Chantier | Lien |
| --- | --- |
| [Sortie d'OpenCV](roadmap-couche-image-sans-opencv.md) | Moins de `cv::` dans l'éditeur, et une interface de rendu derrière laquelle changer le type image. **À faire après E1.** |
| GPU adaptatif ([issue #22](https://github.com/KimntexX/IPR/issues/22), pour la v1) | `RenderChain` est le seul endroit où brancher un second chemin de calcul |
| Aperçu adaptatif | Rendre une zone visible à pleine résolution suppose une chaîne appelable sur une région |
| Masques qui suivent la photo | Les coordonnées sont à un seul endroit |
| Redondances de la release | Déjà reportées à cette refonte |

## 5. Règles d'exécution

- **Protéger avant de déplacer.** Conformément à la règle du 31 octobre, le comportement attendu est couvert par
  des tests avant la refactorisation. Une refactorisation n'ajoute pas de fonctionnalité, et ne se mêle à aucune.
- **Preuve à chaque palier.** Parité de rendu au bit près sur le corpus, JSON horodaté, et le temps d'ouverture et
  de réaction des curseurs, qui ne doivent pas se dégrader. Une boucle déplacée reste dans le même `.cpp` que ses appels.
- **Pas de double implémentation :** l'ancien chemin disparaît dans le même palier.
- **Contrat QML stable jusqu'à E5**, puis mis à jour dans le [suivi des raccordements](suivi-raccordements-ihm.md).
- **Coordination.** Une refonte de ce fichier entre en conflit avec toute branche qui le touche. Fusionner d'abord
  `patch_ui_object_removal`, puis enchaîner les paliers sans laisser de longue branche ouverte. Codex ne touche pas à
  ce fichier à ce jour : à reconfirmer au lancement.
- **Validation.** Aucune fonction nouvelle à valider à l'œil, mais une séance d'usage courant à E6 et E7, où un
  défaut de concurrence ne se verrait pas forcément dans les tests.

## 6. Risques

| Risque | Parade |
| --- | --- |
| Concurrence cachée : le chargement, le rendu et le préchargement se croisent | E6 et E7 en dernier, contrôles de ressources, annulation à chaque point |
| Latence dégradée par une copie introduite en déplaçant du code | Mesures avant et après, poste 16 Go |
| Un seul grand `EditSession` à la place d'un seul grand `ImageEditor` | Vérifier à chaque palier qu'un module a un seul métier ; ne pas viser un nombre de classes |
| Effort sans gain visible pour l'utilisateur | Enchaîner avec un chantier qui en dépend (aperçu adaptatif) pour que le bénéfice arrive vite |

## 7. Décision à prendre

Quand lancer E0 et E1 : je les proposerais **après la validation de B3**, avant B5, pour éviter d'ajouter encore des
commandes à `ImageEditor`. Les paliers suivants peuvent s'intercaler entre les lots, sans bloquer les fonctionnalités.
