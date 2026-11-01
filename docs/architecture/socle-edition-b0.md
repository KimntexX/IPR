# Socle d'édition bêta (B0)

Lot B0 de la feuille de route bêta (`roadmap-beta.md`, encore sur la branche
`patch_documentation`) : les contrats communs
aux panneaux Géométrie, Calques, Retouche et aux traitements neuronaux, avant
leurs raccordements. Ce document commence par l'inventaire B0.1 de l'existant ;
les décisions B0.2 et les interfaces B0.3 le compléteront. Aucun code moteur
n'est modifié à cette étape.

## B0.1 — Inventaire de l'existant (dev `2facc24`, 1er novembre 2026)

### Commandes

`ImageEditor` expose 72 commandes à l'interface (`Q_INVOKABLE`, `imageeditor.h`) :
un setter par réglage (`setBrightness`… `setSharpeningMasking`, courbes,
mélangeur, étalonnage, couleur du point), les remises à zéro, l'ouverture,
la sélection, l'application et l'export par lot, copier/coller, enregistrer,
exporter. Chaque setter borne sa valeur, modifie `m_settings`, puis planifie un
rendu (80 ms) et l'enregistrement du `.ipr` (450 ms).

| Besoin bêta | Existant | Manquant |
|---|---|---|
| Commande annulable | Non : un setter écrit directement l'état | Objet commande (avant/après), regroupement d'un glissement de curseur en une seule entrée |
| Commande d'un panneau | Setters par réglage, nommés pour l'interface | Interface courte par module (B1 Géométrie, B2 Calques…), pas 30 setters de plus dans `ImageEditor` |
| Réglage par nom | `adjustmentValue(name)` en lecture seule | Lecture et écriture uniformes, utiles à l'historique et au copier/coller sélectif |

### Recette et persistance

| Élément | Existant | Manquant |
|---|---|---|
| Recette | `EditRecipe::Settings` (réglages), `RawDevelopment` (DCP, LCP, normalisation, empreintes), `UserImageMetadata` (note, libellé, drapeau) | Opérations ordonnées (calques, géométrie, retouches), références vers des actifs |
| Fichier | `Sidecar` : `.ipr` binaire `QDataStream`, version 9, écriture atomique `QSaveFile`, lecture des versions 1 à 9 avec conversion (décision de l'utilisateur, audit C-13), valeurs bornées à la lecture (C-05) | Format extensible pour des opérations de longueur variable ; stockage des actifs volumineux (masques, retouches calculées) hors du `.ipr` |
| Identité de la source | Taille et date de la photo dans l'en-tête | Empreinte ou identité stable si la photo est déplacée (le `.ipr` suit la photo s'il est déplacé avec elle) |
| Session | `denoiseBeforeProfile` non persisté, volontairement | Décision : l'enregistrer ou non dans la recette |

### Historique

Aucun annuler/refaire : seules la remise à zéro et la relecture du `.ipr`
existent. À créer entièrement : pile d'opérations, regroupement, profondeur,
conservation (session ou fichier), interaction avec copier/coller et lot.

### Tâches longues

| Tâche | Exécution | Annulation | Fin et fermeture |
|---|---|---|---|
| Chargement | `QtConcurrent`, `m_loadWatcher` | Jeton atomique ; demande suivante mise en file | Attendue à la destruction |
| Préchargement | Pool dédié (1 thread) | Jeton + génération | Attendue |
| Aperçu embarqué | Pool dédié (1 thread) | Révision | Pool détruit après ses tâches |
| Rendu | `QtConcurrent`, révision ; le dernier rendu demandé gagne | Pas d'annulation : le résultat obsolète est ignoré | Attendu |
| Export | `QtConcurrent` | Aucune | Attendu |
| Application par lot | `QtConcurrent` | Aucune | Attendue |
| Export par lot | `QtConcurrent` + `QPromise` (progression) | Jeton atomique | Attendu |

Mémoire : `ImageCacheController` réserve un pic estimé avant chaque tâche
lourde (file d'attente FIFO, pression mémoire, refus explicite) et libère la
réservation sur tous les chemins (`qScopeGuard`). Il manque un contrat commun :
état, progression, annulation, révision de source et de recette, publication
atomique du résultat, file partagée et budget VRAM pour B4.

### Caches

| Cache | Clé | Contenu | Invalidation |
|---|---|---|---|
| `DecodedImageCache` (via `ImageCacheController`) | Fichier, taille, date, balance auto, DCP + empreinte, manifeste, LCP + empreinte, options optiques, signal caméra | Master développé ou proxy 2048 px, signal caméra | Clé ; LRU sous budget (25 % de la RAM, plafond 8 Gio) ; pression mémoire |
| `RenderStageCache` | Réglages des étages, cumulés | Sorties des étages coûteux de l'aperçu (tons, couleur, bruit, contraste local, voile) | Changement de source ou d'échelle ; au plus deux sorties gardées |
| `PreparedProfileCache` | Chemin et type du profil | DCP/LCP analysés | LRU 64 Mio, 16 entrées ; chargement unique partagé |
| `ThumbnailProvider` | Fichier visible | Vignettes du ruban | Fenêtre visible seulement |

À ajouter : les entrées géométrie, masques, modèle neuronal et paramètres dans
les clés ; des caches d'opérations locales (calques, retouches) avec leurs
propres invalidations.

### Coordonnées et géométrie

- L'orientation EXIF est appliquée au décodage (RAW : `RawDevelopment::orient` ;
  rasters : `setAutoTransform`). Le master est donc déjà orienté.
- Les corrections d'objectif (LCP) sont appliquées au développement, avant le master.
- La couleur du point prend des coordonnées normalisées [0, 1] sur l'aperçu.
- Il n'existe ni espace « document » (après recadrage ou rotation) ni
  transformation inverse : à définir pour B1 (poignées, recadrage) et B2
  (pinceaux, masques), puis tenir aperçu, proxy et export cohérents.

### Ordre de rendu et domaines couleur

Master Rec.2020 linéaire, alpha droit (non prémultiplié). Étages, dans
`renderWorkingImage` :

1. tons : signal caméra (balance, exposition, débruitage avant profil
   expérimental) → DCP, puis gris, exposition, contraste, balance, ombres et lumières ;
2. couleur perceptive : vibrance, saturation, mélangeur, couleur du point, étalonnage ;
3. réduction du bruit ;
4. texture et clarté ;
5. voile ;
6. courbes, flou, netteté, grain.

Export : même chaîne en pleine résolution (`pixelScale` 1), puis sRGB ou DNG
linéaire. La géométrie (B1) et les calques (B2) n'ont pas encore de place
dans cet ordre : à fixer en B0.2 (par exemple, géométrie après les étages
globaux et avant les calques, à justifier).

### Plusieurs sources

Rien : une seule image ouverte à la fois, aucun dossier tampon. Le contrat
multi-source (B7 empilement, B8 panorama, B9 HDR) reste à écrire.

### Tests qui protègent déjà ces points

`sidecarTests`, `editorBatchTests`, `folderRefreshTests`, `imageExportTests`,
`editorImageCacheTests`, `imageCacheControllerTests`, `renderStageCacheTests`,
`preparedProfileCacheTests`, `thumbnailProviderTests`, `editorLifecycleTests`,
`editorRawDevelopmentTests`. Aucun ne couvre l'annulation, l'historique ou des
opérations ordonnées : ils sont à écrire avec B0.3.

## Décisions B0.2

Prises avec l'utilisateur le 1er novembre 2026 : voir
[ADR-0021](../decisions/ADR-0021-socle-edition-beta.md). Questions d'origine :

1. **Stockage des actifs volumineux** (masques, retouches calculées) : à côté
   du `.ipr`, dans un dossier caché par photo, ou dans un dossier de projet.
2. **Historique** : profondeur, et conservation pendant la session seulement
   ou avec la photo.
3. **Débruitage avant profil** : l'enregistrer dans la recette ou non.
4. **Place de la géométrie et des calques** dans l'ordre de rendu.
5. **Format du `.ipr`** pour des opérations ordonnées : prolonger le binaire
   versionné (avec conversion des anciennes versions) ou passer à un format
   structuré, toujours avec lecture des versions précédentes.

## Navigation entre les panneaux

Retenue par l'utilisateur le 1er novembre 2026 (maquette A) : le panneau de
droite reçoit une rangée d'onglets à icônes sur toute sa largeur, avec un petit
congé aux angles et le nom du panneau au survol : Réglages, Géométrie (B1),
Masques (B2), Retouche (B3), puis Assembler (B7 à B9 : empilement, panorama
horizontal et vertical, fusion HDR), qui crée une nouvelle image en tâche de
fond. Un onglet n'apparaît qu'avec la livraison de son panneau. Les curseurs et
contrôles des panneaux reprennent les composants existants de l'interface.
L'histogramme reste dans l'onglet Réglages seulement ; les sections de cet
onglet se replient, avec une flèche plus grande qu'aujourd'hui.
Raccordement QML sur `patch_ui`.

## B0.3 — Mise en place (branche `patch_editing_core`)

Choix techniques : [ADR-0021](../decisions/ADR-0021-socle-edition-beta.md).

1. **Exécuteur de tâches** (`src/tasks/TaskRunner`) : priorités interactive,
   premier plan et fond, progression, annulation coopérative, révision par
   canal, fermeture bornée. Les sept mécanismes existants n'y sont pas encore
   migrés.
2. **Recette CBOR** (`src/core/Sidecar`, `src/core/RecipeCodec`) : format 10 en
   sections nommées ; anciens `.ipr` 1 à 9 lus et convertis au premier
   enregistrement ; fichier plus récent ouvert en lecture seule.
3. **Historique** (`src/core/EditHistory`) : instantané complet de la recette
   par étape, 100 étapes, instantanés nommés, enregistré compressé dans le
   `.ipr`. Un geste encadré par l'IHM (appui puis relâchement d'un curseur)
   fait une seule étape, choix de l'utilisateur du 1er novembre 2026. Le
   développement RAW (profil, objectif) n'entre pas encore dans l'historique.
   Raccordement IHM : IHM-CONN-005 du
   [suivi des raccordements](suivi-raccordements-ihm.md).
4. **Cartes de valeurs locales** (`src/core/LocalValues.h`) : par réglage, un
   flottant par pixel du rendu, dans les unités du curseur global ; le module
   applique en chaque pixel la valeur globale plus la valeur locale. Sans carte,
   le chemin global est inchangé au bit près.
   - 4a, fait : exposition (signal caméra avant DCP, et module `Exposure` sans
     DCP), contraste (somme bornée à [-100, 100]), saturation et vibrance.
     Sans carte, rendu identique à l'octet à celui de `d8d922d` : 203 rendus
     sur 203 (29 RAW de la racine de base, états neutre, exposition +1 et -0,5,
     contraste +50 et -50, saturation -50, vibrance +50), serveur de rendu en
     mémoire, 2 novembre 2026.
   - 4b, à venir : ombres, noirs, hautes lumières, blancs (courbes aujourd'hui
     tabulées pour une seule valeur) et balance des blancs.
   - 4c, à venir : traitements de voisinage (clarté, voile, bruit, netteté).
   - Le passage des cartes dans la chaîne de rendu, avec leurs clés de cache,
     se fait avec les masques (B2), seuls producteurs de cartes.
