# Protocole — Développement RAW, profils et base colorimétrique

Version 1 — 10 octobre 2026. Statut : recette courante ; fidélité absolue non
certifiée. Appliquer le [socle commun](protocole-commun.md).

## Base d'ouverture

Six scènes : 0037, 0043, 0327, 0007, 9950, 9421. Neutres propres IPR/LR,
Camera Neutral réellement appliqué, WB As Shot, même optique déclarée,
normalisation expérimentale désactivée. Distinguer exposition, WB, rendu de
profil et gamut. Relever chemins/empreintes DCP/LCP, boîtier/objectif/ISO,
dimensions, gains WB disponibles et version du moteur.

Pour génération : groupe `raw` = neutre + `auto-wb` 0/1, RAW requis ;
`-Feature neutral -CameraNeutral` prépare seulement le témoin. Le catalogue
ne génère pas des sweeps de DCP/LCP/normalisation. Tester ces options en IHM
ou par les sondes existantes avec leurs conditions explicitement documentées.

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
