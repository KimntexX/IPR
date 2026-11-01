# ADR-0014 — Cache partagé borné des profils préparés

Date : 17 octobre 2026. Statut : implémentation locale sur demande utilisateur,
qualification par tests et rejeu du corpus ; pas de publication Git implicite.

## Décision

Partager les profils DCP/LCP parsés et immuables, pas les paramètres ni les
pixels d'une photo. Un magasin unique dans `PreparedProfileCache` sert la
compatibilité DCP/LCP, la recherche de profils, le rendu DCP, les retouches WB
caméra et les corrections optiques. Les parseurs précédents sont remplacés par
des parseurs d'un instantané d'octets ; aucune option legacy n'est conservée.

Le budget initial est **64 Mio et 16 entrées**, commun aux deux types, avec
éviction LRU. Il est configurable par l'API C++, pas par un nouveau réglage IHM.
Le coût comptable inclut objets, capacités des vecteurs/chaînes et une provision
pour les structures auxiliaires ; il s'agit d'une estimation conservatrice,
pas d'une mesure exacte du tas Qt ou du RSS du processus.

## Identité et invalidation

Clé : type/révision du parseur + SHA-256 des octets lus. Deux chemins contenant
le même profil peuvent partager les tables ; les règles de compatibilité qui
utilisent le nom du fichier restent évaluées avec le chemin demandé.

Le fichier est relu/haché à chaque demande. Le cache évite le reparsage, **pas
toutes les E/S**. Une modification de même taille/date est détectée, une source
supprimée ou illisible n'est pas remplacée par une ancienne entrée. Les octets
hachés sont ceux effectivement parsés. Les erreurs ne sont pas mises en cache.
La limite de lecture demeure 64 Mio par fichier.

## Concurrence et durée de vie

Un mutex protège index, LRU et compteurs. Lecture, empreinte et préparation
ont lieu hors de ce verrou ; une préparation en cours pour la même clé est
partagée avec les demandeurs en attente (single-flight). Des clés différentes
peuvent être préparées simultanément. Un échec du parseur réveille les demandeurs.
Les loaders internes ne doivent pas demander récursivement la même clé.

Le rendu possède une référence partagée constante. Une éviction ne détruit
pas un objet encore utilisé. Un profil dépassant le budget est utilisable,
mais non conservé dans le LRU. Un budget ou un nombre d'entrées nul désactive
la rétention, pas le rendu. Vider/reconfigurer le magasin change sa génération :
une ancienne préparation ne peut pas repeupler la nouvelle génération.

**La borne couvre les références conservées par le magasin**, pas les octets
lus en cours, les objets encore utilisés après éviction ni les buffers images.
La concurrence active et les allocations de rendu doivent donc être suivies
séparément ; ce cache n'est pas une borne globale de mémoire du logiciel.

## Ce qui reste propre à l'image

La WB, son neutre, les expositions source/profil, l'interpolation d'illuminants,
les réglages de capture et les cartes de remappage optique ne sont pas mémorisés
comme état mutable du profil. R6 et R7 ne partagent pas de coefficient de rendu.
Les matrices/tables et modèles stockés sont lus via `shared_ptr<const ...>`.
Le cache ne change pas les opérations mathématiques du rendu ni les recettes.

Les caches de sélection de chemins, le catalogue de caractérisations, le LRU
d'images décodées et le tampon caméra actif sont des mécanismes distincts.
Leur budget et leur invalidation ne sont pas remplacés par cet ADR.

## Vérifications

Tests : hits, éviction LRU, limite en octets/entrées, profil trop grand pour le
budget, suppression, modification de taille/date identiques, huit lecteurs,
erreur de parseur et vidage en cours de préparation. Les fixtures DCP comparent
sortie sans rétention, sortie chaude, profil modifié et alternance concurrente
R6/R7 fictive. Les fixtures LCP vérifient une préparation unique et des réglages
de capture concurrents sans contamination. Ce ne sont pas de nouveaux boîtiers
caractérisés physiquement.

Le rejeu natif des 19 RAW conserve les compteurs de cache dans sa mémoire JSON.
Les temps globaux de décodage ne permettent pas d'isoler le gain du cache :
ne pas annoncer un pourcentage d'accélération sans benchmark dédié.
