# ADR-0011 — Règles RAW par mode capteur et configuration vivante

**Date :** 16 octobre 2026. **Statut :** accepté par l'utilisateur, implémenté localement.

## Décision et portée

L'utilisateur accepte visuellement les rendus R6/R7 et demande à ouvrir
d'autres photos, notamment sur carte SD, sans inscription au corpus et sans
resélection du manifeste après mise à jour. Cette ouverture à l'essai ne
constitue pas une validation de tous les ISO ou modes du marché.

Le manifeste courant passe au **schéma 3 uniquement**. Les entrées sont des
règles par marque, modèle, SHA-256 DCP et mode de niveaux capteur, sans
empreinte RAW ni liste d'ISO. Les noirs restent ceux lus/estimés sur chaque
image ; ils ne sont jamais remplacés par les noirs d'une photo du corpus.

| Modèle / mode | Bornes natives de contrôle | Blanc cible | Base source | Compensation du profil |
| --- | --- | ---: | ---: | ---: |
| R6 Mark III, noir bas | Noir total par canal 400–640 ; blanc natif 13000–17000 | 13995 | +0,26 EV | 0 |
| R6 Mark III, noir haut | Noir total par canal 1600–2400 ; blanc natif 13000–17000 | 14351 | +0,28 EV | 0 |
| R7 | Noir total par canal 0–4096 ; blanc natif 13000–17000 | 16384 | +0,35 EV | −0,02 EV |

Ces fenêtres sont des contrôles de plausibilité du prototype autour des
modes observés, **pas des spécifications constructeur** ni des estimations
du noir. Le blanc natif est contrôlé avant modification, afin de ne pas
appliquer ces constantes à une autre échelle, notamment 12 bits non rééchelonnés.
Les paramètres continuent d'exiger CR3, Canon, WB caméra, absence de baseline
native déjà appliquée, pas de motif spatial noir non géré et version LibRaw
compatible. DCP différent/modifié, autre modèle ou mode inconnu : pas de
copie d'une règle voisine ; état non appliqué et rendu ordinaire disponibles.
Les règles ambiguës ou invalides sont refusées. Les autres ISO sont autorisés
à l'essai sur les modes reconnus, y compris pour le correctif R7.

## Automatisme et restauration

La calibration est demandée par défaut à la première ouverture. Une recette
existante explicitement désactivée reste désactivée ; une calibration enregistrée
active reste active. La réinitialisation dédiée profil/calibration conserve sa
fonction explicite de désactivation ; le futur panneau avancé reste à concevoir.

Le manifeste sélectionné devient une **configuration vivante** : son contenu
actuel est suivi automatiquement, qu'il soit embarqué, livré à côté de
l'application ou choisi par l'utilisateur. Il est parsé et validé avant
application. Un changement de digest est annoncé ; le nouveau digest entre
dans le cache et dans la recette sauvegardée. Aucun ancien rendu n'est réutilisé.
Un manifeste absent ou invalide n'est pas remplacé silencieusement par un autre.

Les DCP/LCP restent des ressources dont l'identité est vérifiée séparément :
suivre la configuration n'autorise ni un profil incompatible, ni le remplacement
silencieux de ses octets. Cette politique corrige le scénario 0081 du §50,
où une ancienne empreinte du manifeste désactivait indirectement la calibration
du R6 après l'ajout d'un réglage R7.

## Implémentation, absence de legacy et validation

`RawNormalization::resolve()` ne lit/hash plus le RAW : seules les métadonnées
déjà décodées et l'identité du DCP sont nécessaires. Deux modes R6 sont
distingués par leurs noirs réels ; le coefficient R7 n'est pas transmis au R6.
`ImageEditor::beginLoad()` suit les mises à jour du manifeste avant le lookup
cache et le décodage, sans effacer retouches ou annotations.

L'ancien parseur schéma 2 et ses tests de liste blanche sont remplacés, sans
mode legacy. Les JSON historiques d'analyse restent immuables. Les outils
de préparation produisent des règles de mode explicitement proposées, pas une
nouvelle liste de photos. Un ancien manifeste externe schéma 2 doit être remplacé
par le manifeste courant ; la mise à jour automatique des octets ne convertit
pas un ancien format de données.

Tests : photos/noms/ISO non répertoriés, modes R6, isolation R7→R6, détection
échelle incompatible, refus d'un DCP modifié, mise à jour automatique après
restauration d'une ancienne empreinte, cache et conservation des retouches.
Le corpus de 19 sources doit rester identique aux témoins acceptés au pas 8 ;
les nouvelles images SD restent à observer par l'utilisateur. Aucun accès,
effacement ou copie de carte n'est nécessaire à cette intégration.

## Extension du 20 octobre 2026 : EOS 60D (CR2)

Sur décision de l'utilisateur, le 60D entre dans les boîtiers de la fin de l'alpha.
Pour LibRaw, ses modes ISO ont le même noir (≈ 2048) et le même blanc (12279) ;
Adobe lit, lui, le blanc spéculaire Canon de chaque fichier, plafonné à 15000, et
compense par sa base d'exposition (net −0,34 EV pour tous les modes). Une règle peut
donc borner ce blanc spéculaire (`specular_white_min`/`max`, facultatifs) ; quatre
règles 60D en découlent, valeurs d'Adobe, preuve `audit-niveaux-eos60d-v001` :

| Mode 60D | Blanc spéculaire | Blanc cible | Base source |
| --- | --- | ---: | ---: |
| ISO multiples de 160 (160, 1250 vus) | 11100–11400 | 11222 | −0,50 EV |
| ISO 100, spéculaire 12279 | 12200–12400 | 12279 | −0,34 EV |
| ISO 100, spéculaire 12749 | 12600–12900 | 12749 | −0,28 EV |
| ISO 400–6400 vus | 15000–15800 | 15000 | 0 |

Noir 1900–2200, blanc natif 12000–12600, DCP Camera Neutral 60D. Un blanc
spéculaire non observé n'est rattaché à aucune règle (rendu ordinaire). Écart du
neutre IPR/LR sur 21 CR2 : 4,8 → 0,9 L\* en moyenne (`ecart-neutre-60d-v001`) ;
CR3 inchangés. Validation visuelle de l'utilisateur à obtenir.

## Références

- [Rapport unique RAW/profils](../validation/analyses/analyse-base-colorimetrique.md).
- [Graphe du chemin de rendu](../architecture/chemin-ouverture-raw.md).
- [ADR-0007](ADR-0007-calibration-raw-par-defaut.md), [ADR-0010](ADR-0010-compensation-rendu-par-profil-dcp.md).
