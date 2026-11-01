# ADR-0021 — Socle d'édition bêta : recette, actifs, historique et ordre de rendu

- **Date :** 2026-11-01
- **Statut :** Accepté par l'utilisateur ; implémentation à venir (B0.3, B0.4)
- **Décideurs :** utilisateur ; proposition de Claude

## Contexte

Les panneaux de la bêta (Géométrie B1, Calques B2, Retouche B3, réseaux de
neurones B4 à B6, plusieurs images B7 à B9) ajoutent des opérations ordonnées,
des masques, des résultats calculés lourds et un besoin d'annuler/refaire. Le
socle actuel est inventorié dans le [document B0](../architecture/socle-edition-b0.md) :
la recette ne connaît que des réglages globaux, le `.ipr` est un binaire de
version 9, aucun historique n'existe et la géométrie n'a pas de place dans
l'ordre de rendu.

Pratiques observées ailleurs, en boîte noire (1er novembre 2026) : les logiciels
décrivent les retouches par leurs paramètres dans la recette (Lightroom Classic,
RawTherapee, DxO) et rangent les pixels calculés lourds à part, recalculables
(Lightroom Classic) ; Photoshop garde les pixels des calques dans le fichier
même.

## Décision

### Recette et actifs

- Le `.ipr` décrit chaque retouche par ses **paramètres** : traits de pinceau,
  dégradés, points de tampon, paramètres des calques et de la géométrie.
- Les **résultats calculés lourds** (masques issus d'un modèle, retouches
  neuronales) vont dans un **dossier par photo** à côté d'elle,
  `<photo>.ipr-data/`. S'il disparaît, IPR recalcule depuis la recette quand le
  modèle est disponible, sinon il le signale ; la retouche n'est jamais perdue.
- Les **résultats de détection** (formes, boîtes d'objets) sont gardés : quelques
  kilo-octets par photo. Les **masques de segmentation** utilisés par un calque
  font partie de la retouche ; les masques candidats non utilisés forment un
  **cache** à la résolution de l'aperçu, compressé, invalidé si la photo ou le
  modèle change, sous un **plafond global** réglable (500 Mo par défaut ;
  suppression des photos ouvertes le moins récemment) et vidable par une commande.

### Historique

- Annuler/refaire porte sur des **étapes** ; un glissement de curseur compte pour
  une seule étape.
- L'historique est **conservé dans le `.ipr`**, limité aux **100 dernières
  étapes**, avec des **instantanés** nommés par l'utilisateur.

### Débruitage avant profil

Le **mode de développement réellement utilisé** (signé, avant profil ou non) est
enregistré dans le développement RAW du `.ipr`, comme la version du moteur : une
photo rouverte se redéveloppe à l'identique, et IPR prévient si le mode a changé.
Ce n'est pas un nouveau réglage : la cible reste le choix automatique par
boîtier, qui remplacera la case expérimentale.

### Ordre de rendu

- La **géométrie** (recadrage, rotation, perspective, miroir) s'applique juste
  après les corrections d'objectif, avant les traitements de voisinage (bruit,
  netteté, clarté, voile) : un seul rééchantillonnage, et des effets locaux qui
  ne se décalent pas au recadrage.
- Les **réglages locaux** agissent **dans la chaîne de traitement**, comme le fait
  Lightroom (comportement observé de l'extérieur) : un masque n'est pas un calque
  d'image posé par-dessus, mais une zone (pinceau, dégradés, plage, masque issu
  d'un modèle), combinable par ajout, soustraction et intersection, qui porte ses
  propres valeurs des réglages existants. À chaque étage, la valeur appliquée en
  un pixel est la valeur globale plus la valeur locale pondérée par le masque :
  une exposition locale de +1 se comporte comme une exposition globale de +1 dans
  la zone. Les masques n'ont ni ordre ni mode de fusion : leurs effets
  s'additionnent. Les modules prennent donc, en plus de leurs valeurs globales,
  des cartes de valeurs locales.
- La **retouche** (suppression d'éléments, tampon) est une opération distincte,
  placée avant les réglages locaux, sur l'image géométriquement transformée.

### Format du `.ipr`

- **En-tête** lisible seul : identifiant, version du format, version d'IPR qui a
  écrit le fichier, identité de la photo. IPR sait sans tout lire si le fichier
  est ancien, à mettre à jour ou plus récent que lui.
- **Sections étiquetées** (réglages, développement RAW, géométrie, calques,
  historique…) de longueur connue : une section inconnue se saute, une section
  nouvelle ne réécrit pas les autres.
- **Mise à jour au premier enregistrement** : un fichier ancien est lu, converti
  en mémoire, réécrit au format courant ; pas de copie de sauvegarde.
- **Fichier plus récent qu'IPR** : ouverture en lecture seule avec avertissement.
- **Pendant la bêta**, seul utilisateur : le format et l'architecture peuvent
  changer, en convertissant les `.ipr` existants de l'utilisateur.
- **À partir de la version stable**, règle d'évolution sans rupture : ajouts
  seulement (section ou champ facultatif avec valeur par défaut), jamais de
  changement de sens d'un champ existant ; un changement incompatible ouvre une
  nouvelle version majeure du format, avec conversion automatique ; la CI garde
  un `.ipr` de référence de chaque version publiée et vérifie qu'il se lit, se
  convertit et se rend à l'identique.

### Choix techniques (B0.3, validés le 1er novembre 2026)

- **Format :** le `.ipr` devient un document **CBOR** (format binaire standard,
  lu et écrit par Qt avec `QCborValue`) : ses clés nommées sont les sections ;
  une conversion JSON lisible sert au débogage.
- **Historique :** un **instantané complet de la recette** par étape, compressé
  dans le fichier ; annuler recharge l'instantané précédent. Les actifs de
  `.ipr-data` sont référencés, jamais copiés.
- **Tâches longues :** un **exécuteur commun** (état, progression, annulation,
  révision qui écarte un résultat obsolète, priorités interactive, premier plan
  et fond), réservant la mémoire par le contrôleur actuel ; les mécanismes
  actuels y migrent un à un. Sa robustesse est jugée à la mise en place.
- **Valeurs locales :** chaque module reçoit, en plus de sa valeur globale, une
  **carte de valeurs** facultative ; sans masque, il garde son chemin actuel,
  identique au bit près.
- **Masques :** stockés **vectoriellement** (traits, dégradés, en coordonnées
  normalisées de l'image redressée), rastérisés à la résolution du rendu et mis
  en cache ; les masques issus d'un modèle sont des images dans `.ipr-data`.
- **Géométrie :** une **seule matrice** pour rotation, perspective, échelle et
  miroir, appliquée en une passe (bicubique à l'aperçu, Lanczos à l'export) ; le
  recadrage est une découpe finale.
- **Ordre de travail :** exécuteur de tâches, recette CBOR avec conversion des
  versions 1 à 9, historique, cartes de valeurs locales ; chaque étape avec ses
  tests écrits d'abord.

## Conséquences

- **Bénéfices :** recette légère et lisible, retouches jamais perdues avec le
  cache, réouverture rapide des panneaux, historique repris d'une séance à
  l'autre, rendu identique à la réouverture, évolution du format contrôlée.
- **Coûts et risques :** un dossier `.ipr-data` par photo retouchée avec des
  calques calculés ; un `.ipr` plus gros avec l'historique (borné à 100 étapes) ;
  le recalcul d'un actif perdu exige le modèle correspondant ; un changement de
  rotation ou de perspective recalcule les étages suivants (le recadrage, lui,
  s'affiche comme un cadre sur l'image entière et ne s'applique qu'à la
  validation) ; chaque module concerné doit accepter des valeurs locales par
  pixel, ce qui touche les traitements de voisinage (clarté, voile, bruit,
  netteté) plus que les traitements pixel par pixel.
- **Latence :** une interaction reste sous 250 ms sur l'aperçu, seuil sous lequel
  l'utilisateur ne perçoit pas l'attente (sensibilité humaine de 200–300 à
  600 ms). Mesuré le 1er novembre 2026 sur la machine de l'utilisateur
  (8 threads) : la géométrie coûte 5 à 33 ms sur l'aperçu de 2048 px, et 0,1 à
  0,25 s en pleine résolution à l'export.
- **Compatibilité :** les `.ipr` actuels (versions 1 à 9) restent lus et sont
  convertis au nouveau format à leur premier enregistrement.
- **Validation :** tests de la recette (lecture, conversion, section inconnue,
  fichier plus récent), de l'historique (regroupement, limite, instantanés), du
  cache (plafond, invalidation, perte du dossier), et rendu identique sans
  nouvelle opération (B0.4).

## Alternatives écartées

- Pixels des calques dans le `.ipr` : fichier lourd à chaque enregistrement.
- Cache de détection global sans lien à la photo : perdu au déplacement de la photo.
- Historique de session seulement : on ne retrouve pas son travail le lendemain.
- Copie de sauvegarde de chaque `.ipr` mis à jour : remplacée par la règle
  d'évolution testée en CI.
