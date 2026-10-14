# Protocole — Développement RAW, profils et base colorimétrique

Version 2 — 14 octobre 2026. Statut : recette courante ; fidélité absolue non
certifiée. Appliquer le [socle commun](protocole-commun.md).

## Base d'ouverture

Inventorier les RAW à la racine de `tests/image/base` à chaque campagne : le
corpus étendu du 14 octobre contient 19 RAW, R6 Mark III et R7. Neutres propres IPR/LR,
Camera Neutral réellement appliqué, WB As Shot, même optique déclarée,
normalisation expérimentale désactivée. Distinguer exposition, WB, rendu de
profil et gamut. Relever chemins/empreintes DCP/LCP, boîtier/objectif/ISO,
dimensions, gains WB disponibles et version du moteur.

Pour génération : groupe `raw` = neutre + `auto-wb` 0/1, RAW requis. Le
générateur accepte seulement `all` ou un groupe, aucune option individuelle
historique. Pour un TIFF neutre seul, exporter depuis l'IHM après vérification
des réglages et du profil. Le catalogue ne génère pas des sweeps de
DCP/LCP/normalisation ; tracer ses options optiques réellement appliquées,
sans assimiler son neutre au témoin sans optique d'une sonde diagnostique.

## Profils et état RAW

1. Neutre → Camera Neutral → Adobe Standard → retour Neutral : vérifier
   différence, neutralité des commandes utilisateur et maintien des retouches,
   sans réouverture de l'image. Tracer latence et cache séparément du rendu.
2. Tester WB automatique et retour As Shot ; conservation/restauration de la
   configuration avant décodage, sans confusion avec température relative.
3. Normalisation off/on puis off : préserver les gains et réglages choisis,
   vérifier éligibilité et statut de calibration, identité au retour au témoin.
4. Redémarrer avec sidecar ; déplacer/modifier un profil de test dans un espace
   isolé, jamais l'installation Adobe ; vérifier message/repli explicite et
   absence de réutilisation silencieuse d'une ressource obsolète.

Mesures : Y, RGB linéaire, chroma/teinte et clipping par régions homologues,
géométrie avant MAE, résidus par scène. Fidélité physique : charte réelle
documentée, deux éclairages séparés et répétitions, gris contrôlés, référence
Lab/blanc/observateur compatibles, ΔE00 validé contre des vecteurs connus.
Nuanciers de prises d'escalade = contrôle contextuel après identification
marque/matière/référence, pas charte de substitution.

## Méthodes spécialisées et historique

Le [protocole base v1](protocole-base-colorimetrique-v1.md) conserve la méthode
charte/nuanciers et la bibliographie. Ses anciens états Adobe Standard et ses
limitations IHM sont **datés**, pas des instructions actuelles ; le présent
protocole et le socle priment pour les campagnes nouvelles.
Le [diagnostic blancs Canon](protocole-blancs-canon.md) isole noir/blanc capteur
et ajustement automatique LibRaw : il ne teste ni Kelvin ni le curseur Blancs.
Conserver cette méthode séparée, ce n'est pas un doublon de Lumières.

Outils : `tests/analyse_base_colorimetrique.py`,
`tests/analyse_base_colorimetrique_v2.py`, `tests/analyse_canon_white_levels.py`
et sondes de la campagne concernée ; consulter leur `--help` et leurs mappings
avant de relancer. Les références anciennes peuvent avoir été supprimées.
Tests `rawDecoderTests`, `cameraProfileEngineTests`, `editorRawDevelopmentTests`.
Rapport unique : [base colorimétrique](../analyses/analyse-base-colorimetrique.md).

## Investigation du rendu initial — branche `patch_raw_opening`

La validation photographique Couleur reste suspendue tant que les résidus du
neutre ne sont pas maîtrisés ; ne pas ajuster les curseurs pour les masquer.
La sonde `rawOpeningProbe` décode tous les RAW de la racine, WB caméra,
Camera Neutral, normalisation/LCP/CA désactivés, sans restauration de sidecar.
Elle mesure le rendu courant et deux ablations depuis le même RGB caméra :
courbe DCP avec préservation de teinte HSV ProPhoto ; réglage affiché 5200/+5.
Ce dernier n'est pas une preuve d'équivalence physique avec la WB Lightroom.
Le témoin rejoué sur la grille native doit reproduire exactement la sortie
DCP du décodage complet. Aucune modification des profils ou du code de rendu.

Références LR à ajouter pour les scènes sans TIFF : Camera Neutral, **As Shot**,
SDR sRGB 16 bits, exposition et commandes à zéro, courbe utilisateur linéaire,
netteté/bruit/effets/corrections optiques automatiques désactivés si possibles.
Ne pas imposer 5200/+5 aux autres scènes/boîtiers. Nom conseillé :
`base/neutre/IDENTIFIANT-LR-CameraNeutral-AsShot.tif`. Tracer les corrections
embarquées imposées par LR au lieu de supposer qu'elles sont absentes.
État incomplet/incompatible = diagnostic, pas une validation.

Les PNG de la sonde sont des aperçus de sélection/inspection, jamais des
sources de mesures ; les JSON immuables conservent statistiques, empreintes,
paramètres et exclusions. Pas de MAE/DeltaE de pixels avant harmonisation
géométrique, pas de gain global ajusté aux seuls écarts moyens.
