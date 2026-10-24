# Effets — rapport consolidé

## Bilan actuel — 24 octobre 2026

**Dernier résultat :** la [texture](#effets-20261024-texture) reproduit Lightroom à
±0,02 de 1 à 16 pixels à toutes les valeurs ; la [clarté](#effets-20261024-clarte)
est **validée dans un premier état, avec réserve** : son contraste local suit
Lightroom échelle par échelle, mais sa composante tonale adaptée à chaque image
n'est pas reproduite. Voile, grain et flou restent à traiter
([conclusion](#conclusion-actuelle)).

Protocole : [protocole Effets](../protocols/protocole-effets.md). Branche :
`patch_effects`. Les anciennes analyses Texture, Clarté et Voile du 2 octobre 2026
restent des preuves historiques.

<a id="effets-20261024-campagne"></a>

## Campagne du 24 octobre 2026 — corpus et première mesure

### 1. Corpus et exports

Racine de `base` réduite aux scènes de la campagne (les autres RAW sont dans
`base-hors-protocole`). Exports Lightroom des préréglages `IPR Effets`
(`scripts/make_lr_presets.py --controls texture clarity dehaze grain` ; grain à
la taille 25 et à la rugosité 50 par défaut, compagnons et graine ignorés lors de
l'association au neutre) :

- texture et clarté ±25, ±50, ±100 : 6V1A0327, 6V1A0043, 6V1A9947 (contre-jour),
  _MG_7580 (vallée sous la brume, 60D) ;
- clarté, 14 scènes ajoutées depuis le stock de l'utilisateur, choisies par
  éloignement maximal de la clé tonale, de la dynamique, des parts d'ombres et de
  hautes lumières et du contraste local et global : _MG_7840, IMG_7961, IMG_7953,
  IMG_6954, IMG_4173, IMG_8473, 6V1A0032, IMG_6979, IMG_6248, 6V1A9153,
  6V1A0544, 6V1A9539, 6V1A9112, 6V1A0064 ;
- voile ±25, ±50, ±100 : _MG_7580, IMG_4345 (mer voilée, 60D), 6V1A0043 (témoin
  sans voile), 6V1A0327 ;
- grain 25, 50, 100 : 6V1A0327, 6V1A0007.

### 2. Mesure générique

[mesure-effets-v001](../raw/mesure-effets-v001-20261024T150336863214Z.json),
240 états mesurés. Clarté : Lightroom change fortement la luminosité et le contraste
globaux (6V1A0043 : Y 0,79 à −100, 1,39 à +100), IPR presque pas (0,97 à 1,03).
Voile : même sens, IPR plus faible, surtout pour ajouter du voile (IMG_4345 à −100 :
contraste ×0,44 chez Lightroom, ×0,91 chez IPR).

### 3. Réponse par échelle

`tests/study_effects_response.py` compare chaque état au neutre du même moteur,
pixel à pixel, sur le log de la luminance découpé en bandes de différences de
gaussiennes (1 à 256 pixels) : gain de chaque bande, réponse tonale par niveau.
[etude-reponse-effets-v001](../raw/etude-reponse-effets-v001-20261024T151007Z.json),
médiane des quatre scènes :

| Bande (pixels) | fin | 1–2 | 2–4 | 4–8 | 8–16 | 16–32 | 32–64 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Texture +100, Lightroom | 1,56 | 1,48 | 1,43 | 1,34 | 1,19 | 1,07 | 1,02 |
| Texture +100, IPR initial | 2,05 | 1,56 | 1,19 | 1,05 | 1,01 | 1,00 | 1,00 |
| Clarté +100, Lightroom | 1,89 | 1,85 | 1,77 | 1,64 | 1,48 | 1,34 | 1,25 |
| Clarté +100, IPR initial | 1,09 | 1,08 | 1,07 | 1,06 | 1,03 | 1,01 | 1,00 |

La texture initiale (masque flou de 1,2 pixel en lumière linéaire) exagérait les
deux premiers pixels et manquait 2 à 16 ; la clarté initiale (8 pixels, extrêmes
protégés) n'agissait presque pas.

### 4. Gain selon l'amplitude

Sur ses propres paires, Lightroom amplifie bien plus les petites variations que
les grandes : clarté +100, bande 8–16 pixels de 6V1A0043, gain 1,51 à 1,64 sous la
médiane d'amplitude, 0,98 sur le 1 % le plus fort. Modèle ajusté par
`tests/study_effects_amplitude.py` pour chaque bande d (dans le domaine de
l'effet) : d + k d / (1 + (|d|/A)²), k par valeur, A par signe.
[etude-amplitude-effets-v001](../raw/etude-amplitude-effets-v001-20261024T165011Z.json)
(log2) : clarté +100, k 0,83 à 0,92 jusqu'à 32 pixels puis 0,48 à 256, A 0,5 à
0,8 IL au-delà de 4 pixels ; clarté négative sans dépendance à l'amplitude.

<a id="effets-20261024-texture"></a>

## Texture — 24 octobre 2026

- `src/core/LocalContrast` : neuf bandes de 1 à 256 pixels (grandes échelles à
  résolution réduite), gain d'amplitude ci-dessus ; remplace le masque flou de
  `DetailProcessing`, retiré sans option de repli.
- Domaine : dans le log, l'effet de Lightroom par niveau variait d'un facteur 0,28
  à 1,21 autour du modèle ; dans la **racine cubique** de la luminance (proche de
  la clarté CIE L\*), de 0,64 à 1,12. La texture y est ajustée
  ([etude-amplitude-effets-v002](../raw/etude-amplitude-effets-v002-20261024T172402Z.json)),
  puis chaque k est recalé par le rapport entre l'effet de Lightroom et l'effet
  rendu par IPR.
- Résultat ([etude-reponse-effets-v002](../raw/etude-reponse-effets-v002-20261024T173938Z.json)) :

| Texture, bande (pixels) | fin | 1–2 | 2–4 | 4–8 | 8–16 | 16–32 |
|---|---:|---:|---:|---:|---:|---:|
| +100, Lightroom | 1,56 | 1,48 | 1,43 | 1,34 | 1,19 | 1,07 |
| +100, IPR | 1,57 | 1,47 | 1,42 | 1,32 | 1,20 | 1,11 |
| −100, Lightroom | 0,49 | 0,57 | 0,62 | 0,70 | 0,83 | 0,94 |
| −100, IPR | 0,48 | 0,58 | 0,63 | 0,72 | 0,82 | 0,91 |

Réponse tonale à moins de 0,03 IL de Lightroom à tous les niveaux.

<a id="effets-20261024-clarte"></a>

## Clarté — 24 octobre 2026, premier état validé avec réserve

### 5. État livré

Même opérateur que la texture, dans le log2 de la luminance, où le modèle par
bandes explique le mieux la clarté de Lightroom, neuf bandes jusqu'à 256 pixels :
gains moyens de Lightroom reproduits à chaque échelle (clarté +100 : 1,91 contre
1,89 en fin, 1,23 contre 1,19 à 64–128 pixels), petites variations amplifiées,
bords épargnés. Halo mesuré à 8–56 pixels d'un bord de 4 IL : 1,0 IL à +100,
du même ordre que les anneaux de Lightroom sur 6V1A9947 (0,7 à 1,0 IL).

### 6. Réserve — composante tonale adaptée à chaque scène

**Validé par l'utilisateur le 24 octobre 2026 comme premier état, avec la réserve
suivante.** Au-delà du contraste local, la clarté de Lightroom déplace les tons
de façon propre à chaque image : elle éclaircit les images sombres (6V1A0043,
médiane −8,8 IL : +0,29 IL en moyenne ; 6V1A0032 : +0,32) et assombrit les claires
(IMG_6248 : −0,29 ; 6V1A9153 : −0,24), et ses anneaux près des bords sont
dissymétriques selon les tons. IPR ne reproduit pas cette composante.

[etude-clarte-adaptation-v001](../raw/etude-clarte-adaptation-v001-20261024T175927Z.json),
20 scènes : à résolution réduite d'un facteur 4, le modèle par bandes laisse
72 % de la variance de l'effet de Lightroom à +100 (74 % à +50, 64 % à −100).
Explications testées en validation croisée par scène, sans succès :

| Hypothèse | Part de l'effet de Lightroom inexpliquée |
|---|---:|
| Courbe commune du niveau | ≥ 1,00 |
| Courbe du niveau moins la médiane de l'image | ≥ 1,00 |
| Même courbe normalisée par la dynamique | ≥ 1,00 |
| Table niveau × écart à la moyenne locale (64 px), commune | ne se transpose pas |
| Filtre laplacien local (Paris 2011, Aubry 2014), 3 paramètres | 0,84 |

Aucune statistique globale du neutre ne prédit cette adaptation : un calcul de
poids par image à l'ouverture n'a, en l'état, rien de fiable à calculer.
Recherche bibliographique en cours pour d'autres familles d'opérateurs.

<a id="conclusion-actuelle"></a>

## Conclusion actuelle et prochaines actions — 24 octobre 2026

- Texture : conforme à Lightroom, validation visuelle de l'utilisateur en cours.
- Clarté : premier état validé avec la réserve du §6.
- Prochaines actions : correction du voile (négatif 3 à 4 fois trop faible,
  positif faible aux grandes échelles et dans les ombres), puis grain (statistiques
  face à Lightroom) et flou (IPR seul) ; recherche bibliographique sur la clarté,
  soumise à l'utilisateur.
- Limites : quatre scènes pour la texture et le voile, vingt pour la clarté ;
  trois boîtiers Canon ; mesures en luminance (la chrominance suit les rapports de
  canaux) ; halo jugé sur un bord synthétique et sur les anneaux de Lightroom.
