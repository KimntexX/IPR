*[Français](guide-creation-profils-dcp-2026-10-01.md) | [English](guide-creation-profils-dcp-2026-10-01_en.md)*

# Guide complet — création de profils DCP indépendants d’Adobe

## Objectif

Ce guide décrit la production de DCP propres à IPR à partir de mesures. Le DCP
est un profil d’entrée : après dématriçage, il transforme le RGB du capteur
vers une colorimétrie de référence. Il ne corrige pas distorsion, vignettage ou
aberration chromatique : cela relève des LCP ou d’une future source Lensfun.

La première cible est un **DCP double illuminant** par modèle de boîtier :
illuminant A (tungstène) et D50/D65 (jour). Il sert de profil généraliste. Un
DCP à illuminant unique est plus simple mais seulement fiable près de sa source
de calibration.

Les noms de boîtier et les EXIF ne sont pas une calibration. Il faut une charte
aux valeurs de référence connues et des prises de vue contrôlées.

```text
Charte + lumière contrôlée + RAW non altérés
                  │
                  ▼
Mesure des patchs ↔ valeurs de référence
                  │
                  ▼
Matrices + LUT couleur + métadonnées DCP
                  │
                  ▼
profil DCP IPR validé → profiles/dcp/ → profiles/index.json
```

Un profil de base cherche une restitution mesurable, pas le « look » JPEG d’un
constructeur. Les styles créatifs doivent rester des réglages ou profils
séparés et optionnels.

## Matériel et logiciels

| Élément | Recommandation | Rôle |
|---|---|---|
| Charte | ColorChecker 24/Passport mate, ou équivalent avec données de référence | couleurs connues |
| Boîtier | un modèle et firmware consignés | capteur caractérisé |
| Objectif | focale fixe ou zoom verrouillé, sans filtre | réduire les variations optiques |
| Installation | trépied, fond noir mat, niveau | netteté, cadrage, peu de reflets |
| Lumière chaude | illuminant A, tungstène non teinté | ancrage chaud |
| Lumière jour | soleil propre à midi ou source D50/D65 contrôlée | ancrage froid |
| DCamProf | outil externe de génération DCP | matrices, LUT et fichier DCP |
| ArgyllCMS | outil externe de lecture de charte | mesures `.ti3` |

RawPedia recommande les chartes mates avec données de référence et le couple
illuminant A + D50/D65 pour un DCP généraliste. Les chartes brillantes, le
flash, les fluorescents et les LED non caractérisées sont à éviter pour cette
calibration. [Guide RawPedia](https://rawpedia.pixls.us/how_to_create_dcp_color_profiles/)

DCamProf est un outil GPL-3.0 de génération de DCP/ICC. ArgyllCMS fournit des
outils de mesure mais est AGPL. Pour IPR, ils doivent d’abord être employés
**hors du binaire IPR**, dans un environnement de calibration séparé ; ne pas
les lier à l’application sans revue de licence. [DCamProf](https://github.com/Beep6581/dcamprof), [ArgyllCMS](https://www.argyllcms.com/)

## Procédure détaillée

### 1. Préparer une calibration traçable

Créer un dossier hors du dépôt, sans publier les RAW par défaut :

```text
calibration/
  Canon_EOS_R6_Mark_III/2026-10-01/
    README.md
    daylight/RAW/      daylight/exports/
    tungsten/RAW/      tungsten/exports/
    output/
```

Dans le `README`, noter modèle et firmware, numéro/âge de la charte, objectif
et focale, ISO, ouverture, exposition, source lumineuse, lieu/heure,
opérateur, versions DCamProf/ArgyllCMS/LibRaw et hash SHA-256 des RAW. Cette
provenance est indispensable pour reproduire ou retirer un profil.

### 2. Régler le boîtier et cadrer la charte

1. Utiliser le RAW pleine taille, jamais sRAW/mRAW.
2. Régler l’ISO natif le plus bas ; désactiver les traitements qui affectent le
   RAW si possible ; retirer filtres colorés, UV et ND.
3. Utiliser un trépied et une ouverture entre f/5,6 et f/8.
4. Placer une charte nette dans le tiers central : pas trop petite, pas plein
   cadre, sans reflet spéculaire et devant un fond sombre mat.
5. Photographier -1/0/+1 EV pour retenir une vue sans patch écrêté.

La vue retenue ne doit présenter ni patch saturé, ni flou, ni ombre, reflet,
éclairage hétérogène, mélange de sources ou métadonnée incohérente. Le détail
des contraintes de distance, angle et exposition est donné dans le [protocole
RawPedia](https://rawpedia.pixls.us/how_to_create_dcp_color_profiles/).

### 3. Prendre les deux séries

**Jour (D50/D65).** Photographier loin de murs, arbres, voitures et toute
surface colorée. Préférer soleil haut/direct ; éviter aube, crépuscule, smog,
ciel épais et réflexions environnantes.

**Tungstène (illuminant A).** Employer une véritable lampe tungstène non
teintée. La placer assez loin pour un éclairage uniforme ; orienter charte et
boîtier pour supprimer le reflet. Une LED, un fluorescent ou une halogène ne
sont pas des substituts à l’illuminant A pour un profil universel.

Conserver les trois expositions de chaque série. Un profil de scène unique
(musée, intérieur, mariage) peut être créé avec une seule source réelle, mais
ne doit pas être présenté comme un DCP universel.

### 4. Préparer deux TIFF neutres

Le générateur doit recevoir des valeurs non déjà modifiées par un profil caméra,
une courbe, une correction de teinte ou une balance des blancs créative.

Le flux de référence consiste à ouvrir chaque RAW dans RawTherapee, appliquer
le profil *Neutral*, tourner la charte 24 patchs (brun en haut à gauche, noir
en bas à droite), recadrer aux repères puis exporter `daylight.tif` et
`tungsten.tif` **sans appliquer la balance des blancs**. Vérifier que le flux
choisi ne réinjecte pas un profil Adobe ; le but est un RGB capteur aussi neutre
que possible. [Préparation DCamProf](https://rawpedia.pixls.us/how_to_create_dcp_color_profiles/)

### 5. Mesurer et générer le DCP

Exemple pour ColorChecker 24. Adapter les chemins vers `ColorChecker.cht`, le
fichier de référence `cc24_ref.cie`, les exécutables et le nom réel du boîtier.
Vérifier les options selon la version DCamProf installée.

```sh
# Mesure des 24 patchs : sortie .ti3 et image de diagnostic.
scanin -v -dipn tungsten.tif ColorChecker.cht cc24_ref.cie tungsten-diag.tif
scanin -v -dipn daylight.tif ColorChecker.cht cc24_ref.cie daylight-diag.tif

# Description colorimétrique de chaque illuminant.
dcamprof make-profile -i StdA tungsten.ti3 tungsten.json
dcamprof make-profile -i D50 -C daylight.ti3 daylight.json

# DCP double illuminant : ordre A puis D50/D65 impératif.
dcamprof make-dcp \
  -n "Canon EOS R6 Mark III" \
  -d "IPR Canon EOS R6 Mark III v1" \
  -c "IPR calibration; see provenance manifest" \
  -t acr -o neutral \
  tungsten.json daylight.json Canon-EOS-R6-Mark-III-IPR-v1.dcp
```

L’ordre `tungsten.json`, puis `daylight.json`, est significatif. Conserver le
lissage LUT par défaut pour le premier profil généraliste : il limite le risque
de surajustement des seules couleurs de la charte. [Exemple de commandes et
ordre des illuminants](https://rawpedia.pixls.us/how_to_create_dcp_color_profiles/)

### 6. Valider avant publication

La validation doit utiliser les RAW de charte **et** des RAW réels distincts.

- Mesurer erreur moyenne et maximale (par exemple ΔE00), surtout sur gris,
  tons chair, rouges et bleus saturés. Archiver patchs exclus et diagnostics.
- Comparer des images réelles sous jour, tungstène et lumière mixte : neutralité
  des gris, saturation, continuité de balance des blancs, dégradés sans cassure.
- Ne pas modifier les curseurs IPR pour dissimuler un défaut de profil.
- Définir des seuils IPR après une première série inter-boîtiers ; ne pas
  inventer un seuil universel avant d’avoir une référence de comparaison.

Un profil d’entrée repose sur une caractérisation mesurée d’un périphérique et
sur un espace de référence indépendant. C’est la même logique générale qu’un
profil ICC. [ICC : profil d’entrée et espace de connexion](https://www.color.org/getting-started/)

### 7. Installer dans IPR

1. Copier le DCP validé dans `profiles/dcp/` localement.
2. Déclarer le boîtier dans `profiles/index.json`.
3. Ouvrir un RAW de contrôle, vérifier le DCP sélectionné avec le diagnostic,
   puis exporter un PNG/TIFF de référence.
4. Lancer `cameraProfileEngineTests`, `rawDecoderTests` et une comparaison
   visuelle avant publication.

```json
{
  "cameraMake": "Canon",
  "cameraModel": "Canon EOS R6 Mark III",
  "dcp": "dcp/Canon-EOS-R6-Mark-III-IPR-v1.dcp"
}
```

## Publication, licence et évolution

Avant de publier, vérifier les droits sur les RAW de charte, les données de
référence de la charte, le DCP généré et les marques mentionnées. Ajouter un
manifeste de provenance avec auteur, date, versions des outils, illuminants,
hash du DCP, licence et limites connues. Ne jamais inclure de table ou payload
Adobe.

| Phase | Livrable | Risque |
|---|---|---|
| POC, un boîtier | DCP double illuminant et procédure validée | faible si privé |
| 3–5 boîtiers | schéma de provenance, ΔE00, non-régression | moyen |
| Assistant IPR | capture guidée et archivage de métadonnées | moyen |
| Générateur interne | remplacement de DCamProf | élevé : science couleur et LUT |

Ne pas commencer par le générateur interne. DCamProf doit être la référence
externe : il permet de définir les données requises et de comparer un futur
algorithme IPR sans dépendre des profils Adobe.

## Sources principales

- [DCamProf — dépôt, compilation et GPL-3.0](https://github.com/Beep6581/dcamprof)
- [RawPedia — prise de vue et génération de DCP](https://rawpedia.pixls.us/how_to_create_dcp_color_profiles/)
- [ArgyllCMS — outils de profilage et AGPL](https://www.argyllcms.com/)
- [International Color Consortium — profils d’entrée](https://www.color.org/getting-started/)
