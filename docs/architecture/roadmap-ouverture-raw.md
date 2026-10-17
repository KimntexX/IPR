# Roadmap — ouverture RAW multi-boîtiers et hautes lumières

**Décision utilisateur :** 15 octobre 2026. Branche de travail : `patch_raw_opening`.
Objectif : un moteur commun, des données adaptées à chaque image et un coût
de sélection faible, y compris dans un dossier mélangeant plusieurs boîtiers.
Les résultats restent dans le [rapport évolutif unique](../validation/analyses/analyse-base-colorimetrique.md).

## État courant — 17 octobre 2026

Le master Rec.2020 est porté et la calibration est maintenant demandée par
défaut, avec respect d'une désactivation enregistrée. Les règles R6/R7 sont
étendues aux photos de modes compatibles sans liste de RAW/ISO, sur autorisation
utilisateur, sans validation universelle des nouveaux ISO (ADR-0011).

Le [premier lot de transition](../decisions/ADR-0012-formats-extensibles-et-caracterisations.md)
ajoute les adaptateurs image/vignette, un catalogue schéma 4 par
format/décodeur/boîtier/mode et un cache borné des documents parsés. Le deuxième
lot extrait `CameraSignal`, contrat RGB caméra pré-DCP et rendu DCP commun ;
la caractérisation physique indépendante du profil reste à réaliser. Le cache
partagé DCP/LCP est implémenté le 17 octobre : voir l'ADR-0014, budget initial
64 Mio/16 entrées, données immuables et préparation concurrente. Le choix de stockage est DCP +
JSON versionné ; SQLite reste un éventuel index dérivé, pas un chantier actif.

Les jalons et comptes rendus datés ci-dessous décrivent le chemin historique
des essais ; cet état courant prévaut pour les décisions déjà intégrées.

## Jalons

| Étape | Travail | Critère de passage | État |
| --- | --- | --- | --- |
| 1. Conserver les HL | Essais hors production : séparer signal étendu et coordonnées des tables, puis étudier une transition douce vers le gamut de sortie | Invariants numériques, rejeu témoin, comparaison des sept scènes ciblées, contrôle des teintes/détails et validation visuelle | Rejeux et deux campagnes sur sept scènes réalisés ; gamut limité aux HL prometteur, extension seule non retenue, contrôle visuel/détails restant ; aucun rendu livré |
| 2. Valider le rendu | Comparer le candidat au témoin sur R7/R6 Mark III, puis sur les nouveaux boîtiers disponibles | Pas de régression sur 0043/0081 ; gradients/HL contrôlés ; preuves JSON, pas seulement rapprochement du Y moyen LR | À faire après les premiers essais |
| 3. Préparer et mettre en cache | DCP préparés partagés et bornés en mémoire ; contexte de développement immuable par image | Dossier mixte R6/R7, alternance et décodages concurrents sans contamination ; invalidation correcte lors d'un changement de profil/calibration | À concevoir puis mesurer |
| 4. Généraliser les recettes | Remplacer les seules recettes expérimentales par empreinte RAW par des règles fondées sur des métadonnées auditables, sur un domaine validé | Données noir/blanc/exposition attestées ; repli explicite si non couvert ; pas de constante copiée d'une photo sur tout un modèle | Non autorisé par les mesures actuelles seules |
| 5. Livrer la politique cible | Calibration active par défaut ; désactivation dans les futurs paramètres avancés | Portée et restauration définies ; états demandé/appliqué distincts ; validation utilisateur avant intégration/livraison | [ADR-0007](../decisions/ADR-0007-calibration-raw-par-defaut.md), décision acceptée, implémentation à venir |

La préparation du point 1 n'autorise pas l'intégration d'un candidat dans le
moteur. Toute généralisation des calibrations reste distincte du changement
de valeur par défaut. Pas de nouveau chemin legacy lors d'un remplacement validé.

## Directive transverse : espace de travail et écran

[ADR-0008](../decisions/ADR-0008-separation-travail-affichage-export.md) acte
la séparation entre master retouché, aperçu ICC écran et export. Le profil
écran système sera la cible d'affichage par défaut, avec dérogation avancée
à prévoir. Changer d'écran ne doit pas redécoder le RAW ni modifier les exports.
L'[option ProPhoto linéaire](gestion-couleurs-affichage.md) comme espace de
travail généralisé est à évaluer avant de figer les conversions de sortie.
Ce n'est pas une migration déjà décidée : elle exige l'audit des réglages et
leur revalidation sensible au pipeline. Les jalons HL actuels restent expérimentaux.

L'[étude préalable des espaces, §43](../validation/analyses/analyse-base-colorimetrique.md#43-étude-préalable-prophoto-et-espaces-de-calcul-comparables--15-octobre-2026)
identifie Rec.2020 linéaire et ACEScg comme alternatives crédibles. La condition
utilisateur de bascule automatique faute d'alternative n'est donc pas remplie :
pas de commit/stash de gel ni de migration dans cette reprise. Proposition :
comparatif ProPhoto/Rec.2020 avec témoin sRGB étendu à bornages contrôlés,
avant décision d'espace et généralisation des réglages.

Le comparatif est ensuite autorisé et exécuté au [§44](../validation/analyses/analyse-base-colorimetrique.md#44-comparatif-exécuté-srgb-étendu-prophoto-et-rec2020--15-octobre-2026) :
sept scènes, trois espaces, six états, sans changement de production.
Pas de gain HL neutre par le seul changement de coordonnées ; ProPhoto et
Rec.2020 restent comparables sur ce corpus. Après cette comparaison,
l'utilisateur a retenu Rec.2020 pour le port des opérateurs réels : voir le
jalon transverse ci-dessous. La préparation ne déclenche pas de migration,
de commit de gel ni de livraison.

## Corpus prévu

- Existant : Canon EOS R6 Mark III et EOS R7, avec les profils DCP propres
  à chaque modèle et les recettes exactes déjà auditées.
- **À venir : Canon EOS R5**, pour étendre la validation comportementale CR3.
- **À venir : Canon EOS 6D Mark II**, modèle précisé par l'utilisateur ; ses
  RAW CR2 étendront les contrôles multi-boîtiers et de non-régression du rendu,
  mais ne constituent pas une validation du conteneur CR3.

Les images annoncées ne sont ni inventoriées ni validées par cette roadmap.
À leur réception, relever modèle réel, format et mode RAW, ISO, profondeur,
WB, niveaux noir/blanc et DCP. Tester séparément RAW/C-RAW si ces modes sont
effectivement fournis. Ne pas transposer la calibration CR3 actuelle au CR2.
Des scènes HL colorées, des neutres, des ombres et plusieurs expositions/ISO
seront utiles ; le corpus exact sera choisi après inventaire.

Sources constructeur, consultées le 15 octobre 2026 :
[EOS R5, RAW CR3](https://cam.start.canon/fr/C003/manual/html/UG-09_Reference_0100.html)
et [EOS 6D Mark II, extension CR2](https://asia.canon/en/support/8203592900).

## Coût et isolation visés

Sélectionner le développement par les métadonnées de **chaque image**, jamais
par le premier fichier du dossier. Partager seulement des données immuables :
profil préparé, tables et calibration versionnée. Conserver séparément matrices
interpolées/WB et réglages propres à l'image. Les caches devront être bornés,
avec des clés incluant les identités DCP/calibration/moteur et les paramètres
pertinents. Une bascule de boîtier ne vide pas tout le cache.

Mesurer à froid, à chaud, en alternance R6/R7 et en concurrence : temps de
sélection, chargement/préparation DCP, décodage, rendu et mémoire. Fixer un
budget après mesures de référence ; aucun chiffre de latence n'est promis ici.

## Décision transverse Rec.2020 — 15 octobre 2026

Après comparaison des espaces, l'utilisateur choisit le master Rec.2020
linéaire D65 ([ADR-0009](../decisions/ADR-0009-espace-travail-rec2020-lineaire.md)).
Le port du master est maintenant implémenté localement : suivre l'état des cinq lots du
[plan d'intégration](integration-rec2020.md). Les tests Windows et le rejeu des
neutres ne remplacent pas la qualification visuelle/multi-OS. Les essais HL ci-dessous restent
séparés ; conserver la même calibration, les mêmes DCP et le mapping sRGB
de sortie pour mesurer le seul effet du port. Ne pas activer implicitement
la calibration par défaut pendant ce chantier.

## Préparation du premier point

Le socle expérimental est dans `tests/raw_highlight_headroom.h`, sans appel
depuis `src/`. Le test `rawHighlightHeadroomTests` ne demande aucune photographie
ni plugin Qt. Il vérifie identité, conservation des valeurs signées/étendues,
coordonnées bornées, continuité C0 et une rampe synthétique, ainsi qu'une courbe
d'épaule C1 indépendante. Il ne valide ni une vraie table DCP ni une image réelle.

Le raccordement aux tables réelles est maintenant réalisé par profils
temporaires isolés, sans réimplémenter leur interpolation. Deux campagnes
ont été exécutées sur sept scènes ; les preuves et conclusions sont au
[§42 du rapport](../validation/analyses/analyse-base-colorimetrique.md#42-essais-réels-des-candidats-hl--15-octobre-2026).
Il reste à contrôler les détails en pleine résolution et à mesurer les
dérivées aux frontières : la continuité C1 du traitement de table n'est
**pas** garantie par le socle. Aucun TIFF existant n'est modifié.
