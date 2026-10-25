# Protocole — Développement RAW, profils et base colorimétrique

Version 3 — 16 octobre 2026. Statut : recette courante ; fidélité absolue non
certifiée. Appliquer le [socle commun](protocole-commun.md).

Pour les nouvelles prises physiques et les banques avec mire, appliquer le
[protocole spécialisé de mires couleur](protocole-mires-couleur.md). Il distingue
caractérisation mesurée, références caméra de recherche et simple comparaison
de rendu. Il n’annonce pas un analyseur MCP déjà disponible.

## Base d'ouverture

Inventorier les RAW à la racine de `tests/image/base` à chaque campagne : le
corpus étendu du 14 octobre contient 19 RAW, R6 Mark III et R7. Neutres propres IPR/LR,
Camera Neutral réellement appliqué, WB As Shot, même optique déclarée,
calibration active pour le rendu accepté ; témoin désactivé seulement comme
diagnostic distinct. Ne pas confondre case demandée et application effective.
Distinguer exposition, WB, rendu de
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

### Essais isolés de courbe DCP et exposition caméra

`rawOpeningTrials` et `tests/run_raw_opening_trials.py` comparent les mêmes
échantillons RGB caméra, sur une grille de pas 8. Départ : une synthèse
complète récente, références LR et DCP d'empreintes inchangées. Aucun TIFF
n'est généré, aucune image ou installation Adobe n'est modifiée.

1. Dans un dossier de build neuf, créer une copie diagnostique de chaque DCP
   en masquant uniquement le tag de courbe ; vérifier les autres tags inchangés.
2. Reconstruire le traitement de courbe à partir du signal avant celle-ci.
   Le rejeu linéaire par canal doit correspondre à la production à moins de
   `2e-5` d'erreur absolue maximale RGB flottante. Arrêter en cas d'écart.
3. Comparer interpolation linéaire et spline cubique naturelle (dérivée seconde
   nulle aux extrémités), chacune par canal et par interpolation des extrema
   préservant la teinte HSV ProPhoto. Cette spline n'est pas certifiée Adobe.
4. Sur les seules scènes attestées par l'audit DNG, ajouter l'exposition caméra
   à celle du profil, avant tables/courbe. Aucun transfert de valeur vers un
   autre boîtier ; les niveaux noir/blanc et la calibration restent inchangés.
5. Conserver Y moyen, quantiles, noirs, écrêtage et hors-gamut par variante,
   ainsi que le rapport G/R d'une région centrale fixe (sémantique fleur
   uniquement pour 0081). Comparer LR par distributions indépendantes.

Tester identité, passage aux nœuds et courbure de la spline ; vérifier les
empreintes des sources avant/après. Les preuves JSON immuables sont canoniques.
Un meilleur Y moyen ne suffit pas à choisir une variante : considérer teintes,
écrêtage et scènes dégradées. L'intégration de production et la recette IHM
restent des étapes distinctes.

### Décodages radiométriques par scène

Après l'audit DNG/LibRaw complet, `tonalBaseProbe --radiometric` réalise deux
décodages natifs par RAW : témoin courant et blanc DNG fixé **avant**
soustraction des noirs, avec ajustement automatique du maximum désactivé.
Les noirs existants sont conservés : ce n'est pas une substitution des noirs
DNG, en particulier pour les valeurs non uniformes par canal.

À chaque RGB caméra, appliquer Camera Neutral avec/sans exposition caméra
auditée et avec/sans préservation de teinte. Courbe linéaire actuelle,
matrices de calibration inchangées, pas de LCP/CA ni sidecars. Vérifier
l'identité des gains WB et des noirs entre les deux décodages, et le Y moyen
du témoin contre la campagne d'ouverture (tolérance absolue `1e-7`).

Comparer Y, quantiles, noirs, clipping, hors-gamut et région centrale fixe.
Regrouper par boîtier : moyenne signée **et** erreur absolue, scènes dégradées
et hausse maximale de clipping. Une moyenne signée proche de zéro peut
masquer des corrections opposées. Les PNG sous build sont des aperçus,
pas des références de mesure ou une recette IHM.

### Localisation des limites HL dans les étapes DCP

Utiliser `tests/analyse_opening_highlights.py` avec une mémoire des exports
`RAW_Audit` qualifiés. Le périmètre ciblé courant est 0043, 0081, 0524, 9940
et les trois R7 : contrôles sombres/colorés et scènes à canaux hauts.

1. Décoder avec calibration auditée, WB caméra, DCP exact, sans optique/CA.
2. Conserver une grille caméra de pas 8, identique pour toutes les variantes.
3. Masquer uniquement les identifiants de tags dans des copies DCP temporaires
   pour rejouer cumulativement matrices/exposition, Hue/Sat, LookTable, courbe.
   Ne modifier ni les matrices ni les données de tables, ni le profil d'origine.
4. Arrêter si le rejeu complet diffère de la sortie du décodage de plus de
   `1e-6` en erreur maximale absolue RGB flottante.
5. Mesurer bornes, Y linéaire sRGB et quantiles par étape. Reconstituer ProPhoto
   avec les matrices exactes du moteur pour localiser les bornages en amont
   de la conversion de gamut ; ne pas lui attribuer les poids Y de sRGB.
6. Séparer bornage en ProPhoto, hors-gamut sRGB et codes hauts après encodage
   TIFF. Les taux de borne linéaire et de codes TIFF n'ont pas le même seuil.

Une ablation n'est pas un rendu alternatif validé. Le tampon RGB caméra est
déjà passé par la sortie 16 bits LibRaw ; ses valeurs à la borne ne prouvent
ni saturation du photosite ni localisation de toute perte antérieure.
Les JSON immuables conservent sources, empreintes, effectifs et variantes.

La [décision de calibration par défaut](../../decisions/ADR-0007-calibration-raw-par-defaut.md)
est une cible d'architecture ; le protocole continue de vérifier son application
effective, et ne suppose pas que ce défaut soit déjà implémenté.

### Préparation expérimentale de la conservation de réserve

Le socle `tests/raw_highlight_headroom.h` est isolé du moteur. Pour un signal
ProPhoto linéaire `p` et une table `F` définie sur le cube unité, l'hypothèse
à étudier est :

```text
s = max(1, max(p))
q = clamp(p / s, 0, 1)
p_sortie = p + s × (F(q) − q)
```

Seules les coordonnées `q` sont bornées. La correction de table est remise
à l'échelle sur le signal d'origine. Une table identité conserve exactement
les valeurs négatives et supérieures à 1. Dans le cube unité, on retrouve
`F(p)` aux arrondis près. Le résidu signé est une convention numérique
expérimentale, pas une reconstruction physique de lumière négative.

Une épaule scalaire indépendante sert à préparer les essais de sortie :
identité jusqu'à `k`, puis `k + (1−k) × (1−exp(−(x−k)/(1−k)))`.
La valeur `k=0,85` est seulement un exemple de test. Elle n'est associée à
aucun boîtier ni profil. Cette épaule asymptote vers 1 et modifie le blanc
fini : elle n'est pas un réglage de production recommandé. Ne pas la brancher
par canal sans étude de l'effet sur les couleurs.

```powershell
cmake --build build-windows-msvc --config Release --target rawHighlightHeadroomTests
ctest --test-dir build-windows-msvc -C Release -R '^rawHighlightHeadroomTests$' --output-on-failure
```

Les tests synthétiques vérifient identité, résidu signé, coordonnées bornées,
continuité C0 aux frontières, rampe neutre avec une table synthétique, épaule
C1 et rejet de valeurs non finies. La continuité C1 de l'extension de table
n'est pas garantie ; ses dérivées et les permutations de canaux devront être
étudiées avec les données réelles. Le test ne certifie pas une fidélité DNG.

Le raccordement est désormais réalisé dans `tests/dcp_headroom_experiment.h` :
la matrice/exposition est mesurée par des vecteurs de base, puis inversée pour
consulter les vraies tables isolées via le moteur existant. Pas de nouvelle
implémentation de l'interpolation des tables. Le rejeu séquentiel doit rester
à moins de `2e-5` du rendu direct en erreur maximale RGB flottante. Conserver
son erreur dans la preuve, distincte du rejeu complet direct du § précédent.

Comparer témoin, tables étendues/courbe bornée, tables/courbe étendues,
épaule avant courbe, gamut doux seul et combinaisons. Un second contrôle limite
le gamut doux aux HL par mélange `courant + w×(doux−courant)` avec
`w=smoothstep((Y−0,25)/0,5)`, borné entre 0 et 1. Y est la luminance linéaire
sRGB du rendu courant après gamut. À Y ≤ 0,25, le témoin est conservé ; les
seuils sont expérimentaux, fixes entre boîtiers et non ajustés aux références LR.

Qualifier les PNG natifs 16 bits sur la même grille avec
`tests/qualify_headroom_candidates.py` : changements Delta E00/Y/chroma/teinte
par rapport à IPR courant, sur ensemble, 5 % les plus lumineux, tons sombres
et ROI centrale fixe. Conserver les empreintes des aperçus, du parent JSON,
des analyseurs et leurs versions. Un code haut évité peut être un simple
déplacement numérique, pas une récupération de détail.

Les campagnes réelles initiales sont au §42 du rapport unique. Pas de
validation physique ou de pleine résolution à partir de ces grilles de pas 8.
Voir la [roadmap](../../architecture/roadmap-ouverture-raw.md).

### Comparer les espaces sans confondre primaires et bornages

`tests/compare_working_spaces.py` traite les sept scènes ciblées avec DCP,
WB caméra, calibration et optique désactivée identiques entre branches. Le
signal de départ est la sortie DCP linéaire sRGB non bornée au pas 8 ; aucun
changement n'est apporté aux opérations internes DCP dans ce comparatif.

- Vérifier toutes les références LR avant la campagne. Arrêter si une image
  manque ou si son empreinte/profil/WB ne correspond plus à la mémoire qualifiée.
- Reconstruire les matrices depuis les primaires et les blancs documentés
  au §43, conserver ces constantes et les matrices dans le JSON. Adapter D50
  vers D65 par Bradford pour ProPhoto ; ne pas affecter une étiquette sans
  conversion des valeurs.
- Contrôler un rejeu DCP natif à `1e-6` près, puis une reproduction de la
  sortie actuelle par l'analyseur à quatre codes natifs 16 bits maximum.
- Tester identité, EV ±1 et gain de chroma autour du même Y XYZ D65. Après
  retour vers sRGB, arrêter si l'erreur linéaire maximale au même opérateur
  sRGB dépasse `5e-6`. Les composantes négatives et supérieures à 1 restent
  conservées ; aucun gamut mapping propre à chaque master n'est introduit.
- Isoler ensuite une puissance signée par canal et un bornage `[0,1]` explicite.
  Leurs différences entre espaces sont attendues et ne classent pas leur
  fidélité. Ne pas attribuer les effets du bornage au seul choix des primaires.
- Appliquer le même gamut mapping sRGB de sortie à toutes les branches et
  encoder en 16 bits. Conserver Delta E00/chroma/teinte par rapport au témoin
  du même opérateur IPR, sur la même grille. Pour le bornage, conserver aussi
  les changements au rendu courant sans cet opérateur.
- Comparer à LR uniquement les distributions des neutres et du contrôle de
  bornage avec leur référence neutre, sans MAE de géométries différentes.
  Ne pas comparer un EV +1 à un neutre LR comme si c'était une validation EV.

L'analyse ne migre pas les fonctions de production. Ses timings NumPy et son
volume logique de tampons ne sont ni une mesure de latence applicative ni un
pic RSS. Les PNG au pas 8 ne prouvent pas les détails pleine résolution. La
conservation d'une sortie post-DCP ne restaure pas les HL déjà bornées en amont.

## Qualification du port Rec.2020 — port local et qualification partielle

Décision : [ADR-0009](../../decisions/ADR-0009-espace-travail-rec2020-lineaire.md).
La recette détaillée, les seuils proposés et les lots sont dans le
[plan d'intégration](../../architecture/integration-rec2020.md#6-lots-ordonnés-et-critères-de-passage).
Ne pas interpréter le comparatif post-DCP précédent comme une validation des
curseurs portés. Les analyseurs actuels ne constituent pas encore un lanceur
complet de qualification de cette migration.

Le code a été porté après l'étude. Les tests synthétiques C++/QML et le rejeu
des sept neutres disposent désormais de preuves au §46 du rapport. Les
étapes photo par réglage, détails pleine résolution, benchmarks répétés et
multi-OS ci-dessous restent à exécuter ; la section n'annonce pas leur validation.

1. Identifier source, moteur, ressources et recette de référence. Vérifier les
   RAW et les neutres LR Camera Neutral/As Shot ; conserver l'état de calibration
   et désactiver les mêmes corrections optiques de part et d'autre.
2. Tester matrices float signées/étendues, alpha, imports ICC et domaine des
   tables DCP, puis neutres à sortie sRGB fixe. Critère linéaire proposé `5e-6` ;
   conserver max, moyenne, percentiles et cas en échec, pas seulement un agrégat.
3. Rejouer les curseurs réels selon chaque protocole de groupe : zéro,
   intermédiaires, extrêmes et combinaisons pertinentes. Tester aussi les
   courbes RGB, les négatifs, les HL et les carnations. Distinguer variation
   attendue d'un opérateur dépendant des primaires et régression non expliquée.
4. Tester profil/WB à chaud, annulations, alternance de boîtiers, réouverture
   `.ipr`, cache et restauration ; préserver réglages, étoiles et drapeaux.
5. Vérifier aperçu/histogramme sRGB, PNG/JPEG/TIFF et DNG séparément : pixels,
   transfert, profils/tags cohérents, lecture dans un logiciel indépendant.
6. Mesurer pleine résolution CPU/RSS à froid et à chaud, puis lancer la suite
   complète et la CI multi-OS. La validation visuelle utilisateur reste requise.

Enregistrer les nouvelles preuves en JSON immuables horodatés avec domaine de
travail, version moteur, provenance, géométrie et limites. Conserver ce seul
rapport d'analyse RAW/profils ; les anciennes campagnes restent historiques.
Ne supprimer aucune photographie automatiquement.

### Résidu par boîtier : recette de coefficient expérimental

Après l'audit des neutres, utiliser les
[outils R7 de recalage et de balayage natif](../tools/analyseurs-groupes.md#résidu-r7--recalage-gain-et-exposition-de-base).
Arrêter en cas de source modifiée ou d'échec du recalage. Distinguer
statistiques indépendantes, recouvrement recalé et masque non écrêté ; leur
différence ne mesure pas le seul effet du cadrage. Conserver les résultats
par scène, tonalité et canal, avec l'objectif de sélection annoncé et une
validation où la scène testée ne contribue pas au coefficient sélectionné.

Un gain mesuré après rendu n'est pas directement une exposition avant DCP.
Vérifier celle-ci dans le moteur natif sur des manifestes d'essai isolés, avec
le témoin courant et le même masque pour toutes les variantes. Pour les hauts
ISO, moyenner la sortie linéarisée complète avant décimation plutôt que
d'utiliser une grille ponctuelle susceptible d'aliaser le bruit. Contrôler
MAE, biais moyen, couleurs et codes hauts séparément ; une baisse des codes
hauts ne prouve pas la récupération des détails.

La [campagne R7 du §48](../analyses/analyse-base-colorimetrique.md#48-résidu-r7--recalage-et-coefficient-candidat--15-octobre-2026)
identifie un candidat, pas une validation du boîtier à tous les ISO. Garder
les valeurs DNG documentées distinctes d'une compensation de rendu empirique.
Avant intégration/généralisation : contrôle visuel, corpus bas/moyen ISO et
absence de régression sur les autres couples boîtier/profil.
