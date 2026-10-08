# Tester les fonctionnalités d'IPR en autonomie

Mis à jour le **7 octobre 2026**. Commandes vérifiées dans les scripts du dépôt.
Ce guide décrit les outils disponibles ; il ne déclare pas de nouvelle fonction validée.

## 1. Le parcours à suivre

1. Choisir **une fonctionnalité**, ses valeurs et deux ou trois photos pertinentes.
2. Compiler et lancer les tests automatisés de la zone concernée.
3. Générer un neutre et les variantes IPR depuis les RAW.
4. Si une comparaison Lightroom est souhaitée, exporter les mêmes états séparément.
5. Vérifier fichiers, métadonnées et correspondances **avant** les mesures.
6. Comparer chaque variante au neutre de **son propre moteur**, puis comparer les réponses des moteurs.
7. Examiner les exports à taille réelle, vérifier l'IHM et consigner la conclusion.

Trois conclusions distinctes sont possibles : **fonctionnement correct**,
**qualité visuelle acceptable**, **proximité Lightroom**. Aucune ne prouve à
elle seule la fidélité physique des couleurs, qui demande une référence connue.

## 2. Quel terminal et quel programme ?

Toutes les commandes partent de la **racine IPR**, pas de `tests/`.

| Besoin | Terminal conseillé | Outil |
| --- | --- | --- |
| Compiler Windows et tester le code | WSL / Bash | `scripts/build-windows.sh` |
| Produire les TIFF IPR | PowerShell Windows | `scripts/generate-test-images.ps1` → `iprGenerateTests.exe` |
| Mesurer les TIFF | WSL avec Python 3, NumPy, Pillow | `tests/analyse_*.py` |
| Tester les gestes et l'affichage | Application Windows | `build-windows-msvc/Release/appIPR.exe`, avec son environnement Qt habituel |

Dans PowerShell :

```powershell
Set-Location 'C:\Users\victo\Documents\Projet\Programmation\IPR'
```

Dans WSL :

```bash
cd /mnt/c/Users/victo/Documents/Projet/Programmation/IPR
python3 -c "import numpy, PIL; print('Dépendances Python disponibles')"
```

Sous WSL, écrire `/mnt/c/...` pour les chemins Windows. Dans PowerShell,
utiliser `C:\...` ou `.\...`. Les continuations sont `\` en Bash et le caractère
accent grave en PowerShell : ne pas intervertir les blocs.

### Compiler et tester

Fermer IPR avant un build de l'application. Dans Bash :

```bash
# Voir la sélection, sans rien compiler.
bash scripts/build-windows.sh --dry-run

# Usage quotidien : build Windows et tests concernés par les changements.
bash scripts/build-windows.sh

# Exemple : vérifier uniquement le périmètre du mélangeur.
bash scripts/build-windows.sh --tests-only --paths src/core/ColorMixer.cpp

# Changement transversal ou validation avant intégration.
bash scripts/build-windows.sh --full-tests
```

`--tests-only` ne reconstruit pas l'application à utiliser ensuite pour les
photos. Faire un build normal avant de générer les nouveaux exports.
Un premier passage, CMake, les règles de tests ou un changement transversal
peuvent imposer la suite complète même avec `--paths`.

Attendre un code de sortie nul et aucun test échoué. **« Build réussi » ne
signifie pas « rendu validé ».** Ne pas utiliser `--no-tests` pour conclure
à une validation. Le lanceur prépare les DLL/plugins Qt ; un CTest lancé
directement sans cet environnement peut échouer ou rester bloqué.
Voir le [contrat du build](../architecture/tests-cibles-et-build-windows.md).

## 3. Préparer une campagne sans ambiguïté

- Partir du **même RAW**, jamais du TIFF Lightroom pour un moteur et du RAW
  pour l'autre. Noter le nom, le boîtier, les ISO et le profil.
- Choisir un **nouveau dossier** par génération, par exemple
  `tests/image/generated/mixer-cyan-20261007-a`. Ne pas précréer ce dossier :
  le générateur refuse même un dossier vide existant.
- Garder les originaux, anciens exports et manifestes. Copier les résultats
  vers un corpus d'analyse séparé si nécessaire, sans déplacer les sources.
- Attendre la fin de tous les exports avant de lancer les analyses.
- Ne modifier qu'un paramètre à la fois. Les combinaisons sont une campagne distincte.
- Exporter en **TIFF 16 bits, sRGB, SDR**, taille native, sans netteté de sortie.
  Pour les analyseurs actuels, choisir des TIFF non compressés. Ni JPEG ni HDR
  pour les mesures de précision ; conserver l'ICC et les métadonnées.
- Garder constants WB, DCP, LCP, courbes et traitements automatiques. Une même
  température affichée dans deux moteurs ne garantit pas les mêmes gains WB.

Pour Lightroom : réinitialiser puis vérifier explicitement les réglages.
La netteté et la réduction du bruit ne sont pas nécessairement nulles après
un simple reset. Pour tester un paramètre, annuler tous les autres réglages
de la campagne, y compris masques et automatismes. Le profil conserve son
propre rendu même avec tous les curseurs à zéro.

Pour le bruit classique, désactiver l'amélioration IA et repartir du RAW
original, pas d'un DNG déjà débruité. Si l'IA ne peut pas être évitée, noter
la série **LR IA** et la comparer comme une approche différente, sans conclure
que les curseurs sont équivalents. L'absence de trace IA dans le TIFF n'est
pas une preuve qu'aucune amélioration n'a été utilisée en amont.

## 4. Générer les variantes IPR

Dans PowerShell, consulter les fonctions et valeurs disponibles :

```powershell
.\scripts\generate-test-images.ps1 -List
```

Si l'exécution des scripts est bloquée, ouvrir une session temporaire avec
`powershell.exe -NoProfile -ExecutionPolicy Bypass`, puis y exécuter les
commandes. Cela ne change pas la politique permanente de Windows. Les
exemples contenant des tableaux sont à saisir **dans** cette session.

### Exemple complet : exposition

```powershell
.\scripts\generate-test-images.ps1 -Feature exposure -Values '-1','0','1' `
  -Images '.\tests\image\base\6V1A0327.CR3' `
  -Output '.\tests\image\generated\ev-20261007-a' -DryRun
```

Vérifier le plan, puis relancer exactement la même commande **sans `-DryRun`**.
Un neutre obligatoire est ajouté à la série : ici quatre exports par photo.

Lire le manifeste, qui constitue la correspondance officielle :

```powershell
$campagne = '.\tests\image\generated\ev-20261007-a'
$manifest = Get-Content -Raw -Encoding UTF8 "$campagne\manifest.json" | ConvertFrom-Json
$manifest.status
$manifest.exports | Select-Object feature,value,file,referenceTemperatureKelvin
```

Le statut doit être `complete`. S'il vaut `failed` ou `running`, ne pas
traiter la campagne comme complète. Vérifier également les profils et les
réglages enregistrés dans chaque entrée. Le générateur ignore volontairement
les sidecars : il ne teste pas la restauration de vos retouches enregistrées.

### Valeurs et sujets conseillés pour une première recette

Ces séries sont des points de départ, pas des seuils de certification.

| Fonction / identifiant | Série de départ | Sujet et contrôle principal |
| --- | --- | --- |
| `exposure` | −1, 0, +1 EV | Gris, scène mixte ; éclaircissement progressif, clipping |
| `blacks`, `shadows`, `whites`, `highlights`, `contrast` | −100, −50, 0, +50, +100 | Ombres, blancs, contre-jour ; localisation de l'effet, transitions |
| `temperature` | 3500, 5200, 6500, 9000 K | Gris et peau ; dominante chaude/froide, référence neutre tracée |
| `tint` | −100, −50, 0, +50, +100 | Gris, peau ; axe vert/magenta |
| `vibrance`, `saturation` | −100, −50, 0, +50, +100 | Couleurs faibles/fortes, peau ; clipping et transitions |
| `texture`, `clarity`, `dehaze` | −100, −50, 0, +50, +100 | Détails fins, bords contrastés, voile ; halos et bruit |
| `mixer-<bande>-<composante>` | −50, −25, +25, +50 puis ±100 séparément | Zones de la bande et voisines ; direction, sélectivité, progressivité |
| `denoise-luminance`, `denoise-chrominance` | 25, 50, 75, 100 | Aplats, tissus, feuillage ; bruit résiduel et détails conservés |
| `sharpening-amount` | 0, 25, 50, 100 | Bord net, texture et aplat ; halos et amplification du bruit |
| `sharpening-radius` | 0.3, 1, 2, 3 | Quantité fixe `-SharpeningBase 50` ; largeur des halos |
| `sharpening-masking` | 0, 25, 50, 100 | Quantité fixe ; protéger les aplats sans perdre les contours |
| `grading-<zone>-hue` | 0, 60, 120, 180, 240, 300 | Saturation fixe `-GradingSaturation 25` ; roue et transitions tonales |
| `grading-<zone>-saturation` | 0, 25, 50, 100 | Teinte fixe `-GradingHue 30` ; progression et débordement entre zones |
| `curve-luminance`, `curve-red/green/blue` | −100, 0, +100 | Identité, continuité, absence d'oscillations et clipping |
| `blur`, `grain`, `grayscale`, `auto-wb` | Valeurs de `-List` | Cohérence de l'effet ; `auto-wb` demande un RAW |

Les trois zones grading sont `shadows`, `midtones`, `highlights`. Pour les
courbes, le générateur déplace le point milieu selon une formule documentée :
ce n'est pas un curseur Lightroom équivalent. Pour le rayon, le masquage ou
la teinte grading, comparer aussi les variantes **à paramètres de support
identiques**, pas uniquement au neutre général sans effet.

Photos déjà utiles : 0007 pour voilier/ciel/blancs ; 0037 pour végétation ;
0043 pour fleur violette et fond sombre ; 0327 pour tapis colorés et grimpeur ;
9421 pour intérieur à ISO 3200 ; 9950 pour ciel et végétation. Une scène doit
réellement contenir la couleur ou la texture testée. Ne pas reprendre les
anciens rectangles de ROI sans vérifier leur emplacement.

## 5. Mélangeur : de la génération à la lecture du JSON

### Génération IPR, PowerShell

```powershell
.\scripts\generate-test-images.ps1 `
  -Feature 'mixer-cyan-hue','mixer-cyan-saturation','mixer-cyan-luminance' `
  -Values '-50','-25','25','50' `
  -Images '.\tests\image\base\6V1A0327.CR3' `
  -Output '.\tests\image\generated\mixer-cyan-20261007-a'
```

### Constituer le corpus d'analyse

Les analyseurs ne lisent **pas automatiquement** `manifest.json`. Préparer
un dossier indépendant, par exemple `tests/image/validation/mixer-20261007-a`,
avec les copies nommées comme suit. Ne pas renommer les originaux générés.

```text
mixer-20261007-a/
  neutre/
    6V1A0327.tif          ← neutre LR
    6V1A0327-IPR.tiff     ← entrée feature=neutral du manifeste IPR
  color-mixer/
    cyan/
      6V1A0327.tif        ← LR : teinte −50
      6V1A0327-2.tif      ← LR : teinte −25
      ... jusqu'à -12.tif
      6V1A0327-IPR.tiff   ← IPR : teinte −50
      6V1A0327-IPR-2.tiff ← IPR : teinte −25
      ... jusqu'à -12.tiff
```

| Numéro attendu par l'analyseur | Composante | Valeurs dans l'ordre |
| --- | --- | --- |
| 1 à 4 | Teinte | −50, −25, +25, +50 |
| 5 à 8 | Saturation | −50, −25, +25, +50 |
| 9 à 12 | Luminance | −50, −25, +25, +50 |

**Attention : numéro 1 = fichier sans suffixe numérique pour l'analyseur.**
Le générateur numérote aussi le neutre : dans l'exemple précis ci-dessus,
le premier fichier de teinte porte donc `-IPR-2`, à copier sous `-IPR` dans
le corpus d'analyse. Toujours vérifier `feature` et `value`, pas seulement
soustraire un numéro. Reproduire les douze états dans Lightroom, les autres
bandes/composantes restant à zéro. Cyan correspond à Aqua, violet à Purple.

On peut remplacer une copie de variante par l'option
`--variant-file cyan IPR 1 chemin/vers/le/fichier.tiff`, répétée pour chaque
fichier non standard. Les neutres du mélangeur restent à placer selon
l'arborescence ci-dessus. Conserver une table de correspondance avec le manifeste.

### Audit puis analyse, Bash

```bash
python3 tests/analyse_color_mixer.py cyan \
  --root tests/image/validation/mixer-20261007-a --image-id 6V1A0327 \
  --levels intermediate --ipr-mixer-version eight \
  --output docs/validation/raw/audit-mixer-cyan-20261007-a.json
```

Ouvrir `audit` dans le JSON : aucun `missing_variant`, `wrong_variant_settings`,
`non_mixer_settings_differ` ou `band_identity_mismatch` ne doit être ignoré.
**Un code de sortie nul n'implique pas un audit sans anomalie.** Corriger
le corpus puis relancer vers un nouveau nom de sortie.

```bash
python3 tests/analyse_color_mixer.py cyan \
  --root tests/image/validation/mixer-20261007-a --image-id 6V1A0327 \
  --levels intermediate --ipr-mixer-version eight --analyse \
  --output docs/validation/raw/mesure-mixer-cyan-20261007-a.json
```

Le mélangeur peut écraser un JSON existant : choisir un nom neuf et vérifier
qu'il n'existe pas. Pour ±100, faire un corpus distinct, six variantes
H−/H+/S−/S+/L−/L+, puis utiliser `--levels extremes`. Ne pas mélanger les séries.

### Champs à examiner dans `results.cyan`

- `core_pixels` : présence effective de la couleur. Zéro ou très peu de pixels
  signifie un test non représentatif, pas un réglage validé.
- `variants[].metadata_valid` : état attendu confirmé pour LR ; les états IPR
  dépendent aussi de la bonne correspondance avec le manifeste.
- `core_hue_delta` : déplacement de teinte en degrés, relatif au neutre propre.
- `core_saturation_delta` : variation de saturation HSV ; attention aux
  pixels quasi gris dont la teinte est mal définie.
- `core_Y_ratio` : luminance après/avant, 1 = inchangée, 1,20 = +20 %.
- `outside_MAE_linear` : fuite de l'effet hors bande. Vérifier les transitions
  et la géométrie avant d'accuser le masque du moteur.
- `progression.<hue|saturation|luminance>.IPR` : lire `usable`,
  `monotonic_mean`, `strictly_increasing_mean` et les points. Ce sont des
  moyennes spatiales, pas une preuve de monotonie pour chaque pixel.
- `clipped_channel_percent` et `black_pixel_percent` : surveiller les
  augmentations et leur emplacement visuel.

Les ΔE du comparateur mélangeur sont des **ΔE76**, pas des ΔE00. Une baisse
de l'écart LR ne suffit pas à valider le réglage si le neutre ou les profils
ont changé. Voir le [protocole mélangeur](protocols/protocole-color-mixer-intermediaires-2026-10-04.md).

## 6. Bruit : protocole reproductible

### Génération IPR

```powershell
.\scripts\generate-test-images.ps1 `
  -Feature 'denoise-luminance','denoise-chrominance' -Values '25','50','75','100' `
  -Images '.\tests\image\base\6V1A9421.CR3' `
  -Output '.\tests\image\generated\bruit-20261007-a'
```

Neuf exports par photo : neutre et quatre valeurs par composante, netteté nulle.
Préparer `tests/image/validation/bruit-20261007-a/neutre/` avec les deux neutres,
et les sous-dossiers `bruit/luminance/` et `bruit/couleur/` avec les variantes.
Dans **chaque composante**, les numéros analyseur 1/2/3/4 correspondent à
25/50/75/100 ; numéro 1 sans suffixe, puis `-2`, `-3`, `-4`.
Les noms LR/IPR suivent la même convention qu'au §5. La numérotation ne
continue pas de luminance vers couleur, contrairement à celle du générateur.

Options pour éviter de copier les fichiers non standard :
`--neutral-file 6V1A9421 IPR chemin/neutre.tiff` et
`--variant-file 6V1A9421 couleur IPR 1 chemin/couleur-25.tiff`.
Répéter pour tous les états concernés. Les valeurs supplémentaires 1/5/10
utilisent `--extra-state`, pas les numéros des quatre états standards.

### Audit puis analyse

```bash
python3 tests/analyse_noise.py \
  --root tests/image/validation/bruit-20261007-a --image-id 6V1A9421 \
  --output docs/validation/raw/audit-bruit-20261007-a.json

# Seulement après vérification de issues dans le premier JSON :
python3 tests/analyse_noise.py \
  --root tests/image/validation/bruit-20261007-a --image-id 6V1A9421 \
  --analyse --patch-size 256 --previews build-validation-bruit-20261007-a \
  --output docs/validation/raw/mesure-bruit-20261007-a.json
```

Le script attend les **deux composantes et les deux moteurs** ; une série
manquante peut être mesurée partiellement mais interdit une conclusion complète.
Lire `issues`, puis `results.6V1A9421` et les groupes `IPR-luminance`,
`IPR-couleur`, `LR-luminance`, `LR-couleur`. Vérifier les indicateurs
`protocol_valid`, `state_valid` et `baseline_protocol_valid`.

Dans `variants[].summary` :

| Champ | Lecture pratique |
| --- | --- |
| `flat_sigma_Y_ratio` | Bruit haute fréquence de luminance après/avant ; 0,70 ≈ baisse de 30 % de cet indicateur |
| `flat_sigma_chroma_ratio` | Bruit couleur après/avant ; rechercher une baisse sans taches ni désaturation |
| `flat_mean_Y_ratio` | Dérive lumineuse ; 1 = neutre, 1,003 = +0,3 % |
| `detail_edge_gradient_ratio` | Conservation des contours ; 0,90 ≈ baisse de 10 % du gradient mesuré, pas « 10 % de détails perdus » |
| `max_abs_chroma_mean_drift` | Déplacement moyen des composantes couleur sur les patches |
| `all_regions_identical` | Si vrai à une valeur forte, vérifier état, provenance et contenu réellement bruité |
| `flat_regions`, `detail_regions` | Nombre de zones effectivement disponibles pour conclure |

L'estimation utilise le **MAD** des coefficients HH de Haar, normalisé par
0,67449 pour estimer un sigma sous hypothèse gaussienne. Elle contient aussi
la contribution de certaines textures. Une baisse n'est pas une preuve de
reconstruction correcte : un flou destructeur peut aussi faire baisser le bruit.
Regarder les patches `flat` **et** `detail`, leurs aperçus et les vrais TIFF.
Ne pas comparer uniquement des sigmas absolus de deux moteurs dont les
neutres ont des tonalités différentes.

Voir le [protocole bruit v2](protocols/protocole-bruit-v2.md). Une image à
ISO 3200 peut suffire pour un premier cas si le bruit est visible ; plusieurs
textures et niveaux d'exposition sont plus instructifs qu'un ISO imposé seul.

## 7. Lumières, colorimétrie et autres fonctions

### Base colorimétrique : analyseur précis mais spécialisé

Le script `analyse_base_colorimetrique_v2.py` utilise des pixels natifs
16 bits et CIEDE2000. Il reste limité aux six identifiants du corpus existant,
à leurs ROI historiques, et demande une archive des anciens IPR. Exemple
de répétition **sur ce corpus**, si les fichiers sont toujours présents :

```bash
python3 tests/analyse_base_colorimetrique_v2.py \
  --input tests/image/base/neutre \
  --archive tests/image/base/neutre/6V1A9950-IPR.zip \
  --scenes 6V1A0007 6V1A9421 6V1A9950 \
  --output docs/validation/raw/mesure-base-20261007-a.json \
  --contact-sheet build-validation-base-20261007-a/comparaison.png
```

Lire `scenes[].global.delta_e00_mean`, `delta_e00_median`, `delta_e00_p95`,
`delta_lab_mean_lr_minus_ipr` et `ipr_y_mean`/`lr_y_mean`. Un ΔL* positif
signifie ici **LR plus clair qu'IPR**. `old_export.identical_bytes` compare
les fichiers complets avec l'archive, pas la proximité Lightroom.
Les régions `valid=false` sont exclues ; même une ROI valide en coordonnées
peut être mal nommée. La planche aide à vérifier le sujet, pas à mesurer.

**Ne pas utiliser l'ancien `analyse_base_colorimetrique.py` comme mesure
CIEDE2000** : sa méthode historique a été corrigée. Le
[rapport évolutif unique](analyses/analyse-base-colorimetrique.md) précise
les limites, la WB et les essais DCP. La sonde `tonalBaseProbe` est un outil
de diagnostic du pipeline, pas un test quotidien de curseur.

### Analyseurs historiques : tendances, pas recette générique 16 bits

| Famille | Script existant |
| --- | --- |
| Noirs, ombres, blancs, HL, contraste | `analyse_tonal_adjustments.py` ; noms `noir`, `ombre`, `blanc`, `hl`, `contraste` |
| Température / nuance | `analyse_temperature.py`, `analyse_tint.py` |
| Vibrance / saturation | `analyse_vibrance.py`, `analyse_saturation.py` |
| Texture / clarté / voile | `analyse_texture.py`, `analyse_clarity.py`, `analyse_dehaze.py` |

Ces scripts ont des listes d'images, chemins et suffixes historiques inscrits
dans le code ; plusieurs réduisent/redimensionnent les TIFF en RGB 8 bits,
texture/clarté mesurant des patches eux aussi convertis en 8 bits. Certains
réutilisent une variante archivée si la nouvelle manque. Ils ne consomment
pas le manifeste du générateur et **ne conviennent pas sans adaptation à
une nouvelle campagne arbitraire**. Exemple réservé au corpus historique :

```bash
python3 tests/analyse_tonal_adjustments.py blanc hl \
  --output docs/validation/raw/tendance-blancs-hl-20261007-a.txt
```

Lire les scènes incluses/exclues ; **zéro triplet = aucune validation**.
Comparer les réponses des zones `low`, `mid`, `high` au neutre propre.
Ne pas utiliser ces résultats pour affirmer une précision native 16 bits.
Les fichiers texte de sortie peuvent être écrasés : toujours choisir un nom neuf.

Pour grading, courbes, netteté, grain, flou et les interactions, le générateur
existe mais il n'y a pas encore de comparateur photographique universel avec
critères d'acceptation. Utiliser les tests automatisés, la recette visuelle
ci-dessous et un protocole dédié. Signaler « mesures à compléter » plutôt
que réutiliser un score de mélangeur ou de bruit hors de son domaine.

## 8. Interpréter les mesures sans fausse validation

| Mesure | Signification | Piège à éviter |
| --- | --- | --- |
| MAE RGB | Moyenne des écarts absolus ; préciser encodé/linéaire et normalisation | Petite MAE globale masquant un défaut local ; mauvais calage |
| ΔE76 / ΔE00 | Écart colorimétrique selon deux formules différentes | Les interchanger ; imposer ΔE00 < 3 à une photo non recalée face à LR |
| Y, ΔL* | Luminance linéaire / clarté perceptuelle | Confondre luminosité et dominante WB |
| Ratio | Après/avant ; 1 = égalité ; `(ratio−1)×100` donne la variation en % | Valeur instable lorsque la référence est presque nulle |
| Clipping | Pixels/canaux aux bornes, selon la définition du champ | Un canal noir ≠ pixel entièrement noir ; clipping existant ≠ créé par le réglage |
| P95 | 95 % des échantillons ont une valeur inférieure ou égale | Ignorer les zones localement dégradées parce que la médiane est bonne |
| SHA-256 | Identité du fichier, y compris métadonnées | Empreinte différente ne prouvant pas des pixels différents |

Avant les chiffres : ICC cohérent, dimensions/orientation vérifiées,
géométrie compatible et mêmes états. Le retrait automatique d'un pixel de
bordure ne corrige pas une distorsion optique différente. Ne pas redimensionner
silencieusement les images pour obtenir une superposition.

Pour isoler un réglage, comparer conceptuellement les réponses
`IPR(valeur) − IPR(neutre)` et `LR(valeur) − LR(neutre)` dans la **même métrique**.
L'écart direct `IPR(valeur) − LR(valeur)` mélange la différence de base et
celle du réglage. Les scripts ne calculent pas tous une différence de
différences pixel à pixel : respecter les champs qu'ils fournissent.

Il n'existe pas de seuil universel validé dans le projet pour chaque score.
Définir la tolérance avant la campagne, à partir d'une référence approuvée
et du protocole. Une valeur issue d'un ancien rapport est un résultat observé,
pas automatiquement un seuil de réussite.

## 9. Recette manuelle de l'IHM et de l'image

Pour chaque fonction testée :

1. Ouvrir une photo de test et noter si un `.ipr` restaure des réglages.
   Pour un test neutre, réinitialiser explicitement ; travailler sur des
   copies si les retouches existantes doivent être préservées.
2. Saisir une valeur au clavier, valider : champ, curseur et image doivent
   être cohérents. Déplacer ensuite le curseur et vérifier le champ.
3. Tester minimum, milieu, maximum, valeur nulle, reset du paramètre et reset global.
4. Changer bande HSL ou zone grading : aucune valeur ne doit passer à la
   bande voisine. Déplacer plusieurs fois la roue sans perdre le contrôle.
5. Changer d'image puis revenir : pas de contamination. Un sidecar existant
   peut légitimement restaurer une valeur non nulle ; ce n'est pas un défaut
   de reset. Tester séparément une image sans sidecar et une avec retouches.
6. Comparer origine/actuel. « Origine » est la source déjà décodée/profilée,
   pas le RAW capteur ; une WB automatique redécodée peut renouveler cette référence.
7. Exporter et examiner dans un lecteur gérant les ICC : vue globale puis
   **100 % de l'export pleine résolution**, mêmes zones sur les deux images.
   Le zoom IPR utilise actuellement le proxy d'aperçu : il ne suffit pas
   pour juger bruit, netteté et microtexture à l'échelle du capteur.
8. Rechercher halos, aplats cassés, bandes, taches chromatiques, perte de
   texture, saturation excessive et clipping. Tester une scène où l'effet
   est utile et une où il ne devrait pas dégrader l'image.
9. Vérifier sauvegarde/réouverture et export répété : bon format, incrément
   des noms, absence d'écrasement des fichiers précédents.

Pour l'exposition linéaire, +1 EV correspond idéalement à ×2 avant opérations
non linéaires et clipping ; ne pas exiger que les valeurs codées du TIFF sRGB
soient doublées. Pour le bruit/netteté, juger séparément aplat et texture.
Pour une fonction stochastique comme le grain, ne pas exiger une identité
binaire répétée sans vérifier la politique de graine aléatoire.

## 10. Décider et consigner

**Arrêt / non interprétable :** mauvais état LR, fichiers manquants, mauvaise
source, espace non pris en charge, géométrie incompatible, aucune zone utile,
campagne incomplète. Corriger le protocole avant le moteur.

**À corriger :** effet absent sur une scène pertinente, mauvais sens,
contamination des paramètres, discontinuité, écrasement de fichier, artefact
reproductible. Garder RAW, manifeste, valeur et capture de la zone.

**Fonctionnement confirmé, qualité à poursuivre :** états et progression
cohérents mais différence LR, textures/halos ou fidélité non encore validés.
**Validé sur le corpus** seulement si les critères du protocole, les tests
automatisés et la recette visuelle sont satisfaits ; ne pas généraliser à
tous les boîtiers/ISO/éclairages.

Compléter le rapport existant de la famille, sans créer une nouvelle version
de rapport à chaque essai. Conserver les mesures brutes datées sans les écraser,
puis actualiser la [matrice de validation](matrice-validation.md).

```text
Date / fonctionnalité / identifiant de campagne :
Version ou empreinte du générateur, profils, source RAW :
Commande et valeurs ; paramètres maintenus fixes :
Tests automatisés réussis / échoués :
Audit des fichiers et métadonnées :
Scènes et régions réellement analysées :
Mesures principales, unités, comparaison au neutre :
Observation à 100 %, artefacts éventuels :
Conclusion : fonctionnement / qualité / proximité LR / fidélité physique :
Limites et prochaine action :
Liens vers manifeste, mesures brutes et captures :
```

### Dépannage rapide

- **DLL ou plugin Qt manquant :** utiliser les lanceurs fournis, pas déplacer
  l'exécutable seul. Vérifier le build et son cache CMake.
- **Dossier de sortie existant :** choisir une nouvelle campagne, ne pas
  supprimer les anciennes preuves pour contourner la protection.
- **TIFF non pris en charge :** contrôler compression, 16 bits, ICC sRGB,
  orientation appliquée, fichiers complètement écrits.
- **Valeur `null` :** mesure indisponible, souvent manque de pixels ou référence
  trop faible ; ce n'est ni zéro ni une réussite.
- **Aucun effet :** vérifier manifeste, RAW/TIFF source, binaire reconstruit,
  couleur présente et paramètre de support (netteté/grading).
- **Score surprenant :** vérifier d'abord l'appariement, le neutre, l'optique,
  les automatismes, les dimensions et les ROI.

## Références internes

- [Générateur : contrat, catalogue et options](tools/generateur-images-tests.md).
- [Build Windows et sélection ciblée](../architecture/tests-cibles-et-build-windows.md).
- [Protocoles par famille](protocols/README.md).
- [Matrice de validation](matrice-validation.md).
- [Rapport évolutif de base colorimétrique](analyses/analyse-base-colorimetrique.md).
- [Comparaison et zoom](../specifications/zoom-image.md).
