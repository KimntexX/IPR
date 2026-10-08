# Générateur d'images de test IPR

Outil : `iprGenerateTests` (`.exe` sous Windows), source
[`src/tools/generate_test_images.cpp`](../../../src/tools/generate_test_images.cpp).
Il utilise le **moteur de rendu et l'export de l'application**, à pleine
résolution, sans ouvrir l'IHM. Ce n'est pas un simulateur des curseurs.

## Démarrage Windows

Depuis la racine du projet, compiler avec le lanceur habituel :

```bash
bash scripts/build-windows.sh
```

L'exécutable est produit dans `build-windows-msvc/Release/iprGenerateTests.exe`.
Les builds Windows habituels reconstruisent aussi cet outil si ses dépendances
changent, même quand seuls les tests de la zone modifiée sont sélectionnés.
Le lanceur PowerShell prépare les chemins des DLL à partir du cache CMake,
sans changer la configuration globale de Windows :

Si Windows bloque les scripts, cette invocation autorise le lanceur pour le
processus courant uniquement (aucun changement de politique global) :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\generate-test-images.ps1 -List
```

Les exemples avec tableaux ci-dessous s'exécutent dans une session PowerShell
autorisant les scripts. Pour un seul paramètre et les valeurs par défaut,
le même préfixe `powershell.exe -NoProfile -ExecutionPolicy Bypass -File`
peut précéder directement le chemin du lanceur et ses arguments.

```powershell
# Liste complète : identifiants, plages et séries par défaut.
.\scripts\generate-test-images.ps1 -List

# Vérifier le plan sans décoder ni créer de fichiers.
.\scripts\generate-test-images.ps1 -Feature exposure -Values '-1','0','1' `
  -Images '.\tests\image\base\6V1A0327.CR3' `
  -Output '.\tests\image\generated\exposition-v1' -DryRun

# Générer la même série : neutre obligatoire + EV -1, 0, +1.
.\scripts\generate-test-images.ps1 -Feature exposure -Values '-1','0','1' `
  -Images '.\tests\image\base\6V1A0327.CR3' `
  -Output '.\tests\image\generated\exposition-v1'
```

`-Output` doit désigner un **nouveau dossier**, même un dossier vide existant
est refusé. Pour recommencer, utiliser un nouveau nom de campagne. Les chemins
des images sont ceux fournis par l'utilisateur ; aucun dossier n'est parcouru
implicitement. Les chemins avec espaces sont acceptés.

## Séries pratiques

```powershell
# Mélangeur cyan : dans l'ordre teinte, saturation, luminance.
# Un neutre + douze variantes ; numérotation séquentielle de la campagne.
.\scripts\generate-test-images.ps1 `
  -Feature 'mixer-cyan-hue','mixer-cyan-saturation','mixer-cyan-luminance' `
  -Values '-50','-25','25','50' -Images '.\tests\image\base\6V1A0327.CR3' `
  -Output '.\tests\image\generated\mixer-cyan-v1'

# Bruit : une seule composante varie à la fois, netteté à zéro.
.\scripts\generate-test-images.ps1 -Feature 'denoise-luminance','denoise-chrominance' `
  -Values '0','25','50','100' `
  -Images '.\tests\image\base\6V1A0043.CR3','.\tests\image\base\6V1A9421.CR3' `
  -Output '.\tests\image\generated\bruit-v3'

# Neutres frais pour le protocole de base colorimétrique.
$photos = '0037','0043','0327','0007','9950','9421' |
  ForEach-Object { '.\tests\image\base\6V1A' + $_ + '.CR3' }
.\scripts\generate-test-images.ps1 -Feature neutral -Images $photos `
  -Output '.\tests\image\generated\base-colorimetrique-v1'

# Rayon de netteté : quantité fixe explicitée dans le manifeste.
.\scripts\generate-test-images.ps1 -Feature sharpening-radius -SharpeningBase 50 `
  -Values '0.3','1','2','3' -Images '.\tests\image\base\6V1A0043.CR3' `
  -Output '.\tests\image\generated\rayon-v1'
```

Les valeurs personnalisées sont partagées par les fonctionnalités demandées :
elles doivent être valides pour **toutes**. Sans `-Values`, chaque paramètre
emploie ses propres valeurs par défaut. `-Feature all` couvre l'ensemble des
paramètres ci-dessous et requiert des RAW pour `auto-wb` : lancer d'abord
`-DryRun`, car cela peut produire plusieurs centaines de fichiers par photo.

## Fonctionnalités disponibles

| Famille | Identifiants | Valeurs et particularités |
| --- | --- | --- |
| Référence | `neutral` | Un fichier sans réglages, WB de référence du décodage |
| Exposition | `exposure` | −5 à +5 EV, décimales acceptées |
| Lumières | `contrast`, `shadows`, `blacks`, `highlights`, `whites` | −100 à +100 |
| Balance des blancs | `temperature`, `tint`, `auto-wb` | 2000–50000 K ; −150 à +150 ; 0/1 avec redécodage RAW |
| Colorimétrie | `vibrance`, `saturation` | −100 à +100 |
| Détails locaux | `texture`, `clarity`, `dehaze` | −100 à +100 |
| Effets | `blur`, `grayscale`, `grain` | 0–20 ; 0/1 ; 0–100 |
| Bruit | `denoise-luminance`, `denoise-chrominance` | 0–100 ; autres composantes à zéro |
| Netteté | `sharpening-amount`, `sharpening-radius`, `sharpening-masking` | 0–200 ; 0,3–3 ; 0–100 |
| Mélangeur | `mixer-<bande>-<composante>` | Bandes `red`, `orange`, `yellow`, `green`, `cyan`, `blue`, `violet`, `magenta` ; composantes `hue`, `saturation`, `luminance` ; −100 à +100 |
| Color grading | `grading-<zone>-hue`, `grading-<zone>-saturation` | Zones `shadows`, `midtones`, `highlights` ; teinte 0–359°, saturation 0–100 |
| Courbes | `curve-luminance`, `curve-red`, `curve-green`, `curve-blue` | Série diagnostique −100 à +100, définition ci-dessous |

Tous les paramètres sont entiers sauf l'exposition et le rayon. Une valeur
hors plage, non finie, répétée, ou un identifiant inconnu provoque un échec
**avant** création de la campagne.

Les courbes utilisent trois points : `(0,0)`, `(0.5, 0.5 + 0.004 × valeur)`,
`(1,1)`. La valeur zéro donne l'identité ; les extrêmes déplacent le milieu
vers 0,1 ou 0,9. Ce sont des cas reproductibles du moteur, **pas** des curseurs
Lightroom équivalents ni une interface de courbes arbitraires.

Le rayon et le masquage sont testés avec quantité de netteté fixe
`-SharpeningBase 50` (modifiable entre 1 et 200), sans quoi leur effet serait
invisible. Une teinte de grading est testée avec `-GradingSaturation 25`
(1–100) ; la saturation avec `-GradingHue 30` (0–359°). Ces paramètres de
support sont inscrits dans chaque état exporté et ne touchent pas les autres
tests. Le fichier neutre général reste sans netteté ni grading ; pour ces
tests conditionnels, comparer aussi entre variantes à support identique.

## Contrat de génération et traçabilité

1. Vérifier les options et les fichiers, puis réserver un nouveau dossier.
2. Décoder chaque source en suivant le chemin normal d'IPR (ICC/orientation
   inclus), sans rétablir les réglages `.ipr` ou `.xmp`.
3. Fixer la température neutre à la WB de référence du décodage et la nuance
   relative à zéro : cela reproduit un reset IPR, pas une température
   Lightroom universelle.
4. Exporter un neutre, puis recréer des réglages neufs pour **chaque** variante.
   Aucun cumul involontaire entre fonctionnalités.
5. Exporter la source pleine résolution, jamais le proxy de prévisualisation.
6. Mettre à jour atomiquement `manifest.json` après chaque export, puis marquer
   la campagne `complete`. En cas d'erreur, garder les résultats partiels et
   marquer `failed` ; arrêt au premier échec avec code non nul.

Les fichiers sont regroupés par source (indice + nom) puis par fonctionnalité.
Exemple : `001-6V1A0327/mixer-cyan-hue/6V1A0327-IPR-2.tiff` après le neutre
numéro 1. Les photos portant le même nom dans différents dossiers restent
distinctes. Un fichier manifeste fournit : arguments, version IPR/Qt,
empreinte SHA-256 de l'exécutable, source et exports, ordre de génération,
dimensions, profil source/DCP/LCP signalé par le moteur, boîtier/ISO, WB de
référence et **tous les réglages** de chaque état. Les racines de recherche
des profils et l'ordre des bandes/zones/canaux sont également inscrits.

Le manifeste ne prétend pas identifier cryptographiquement les DCP/LCP : les
noms signalés ne sont pas des empreintes des profils. Conserver les mêmes
profils et dépendances pour les campagnes comparées. Leur sélection utilise
la configuration de recherche existante du moteur, sans copier de profils.
L'identité Qt d'application/organisation est identique à celle d'IPR pour
retrouver aussi les profils installés dans son dossier de données utilisateur.

TIFF par défaut : **16 bits, sRGB, SDR**, sans redimensionnement. PNG 16 bits
et JPEG 8 bits sont disponibles avec `-Format`. JPEG n'est pas recommandé
pour MAE/superpositions quantitatives. Le générateur ne propose pas de DNG
ni d'export HDR pour cette campagne de comparaison.

Les originaux, sidecars et préférences IHM ne sont jamais écrits. Leur état
ne sert pas à construire les réglages ; un `.ipr` enregistré dans l'application
est donc volontairement ignoré. L'outil n'est pas un export par lot des
retouches sauvegardées.

## Utilisation directe et autres OS

Si les bibliothèques d'IPR sont déjà dans le chemin d'exécution :

```powershell
& .\build-windows-msvc\Release\iprGenerateTests.exe `
  --feature mixer-violet-hue,mixer-magenta-hue --values=-50,-25,25,50 `
  --input '.\tests\image\base\6V1A0043.CR3' --output '.\tests\image\generated\violet-magenta-v1'
```

`--input` et `--feature` sont répétables ; des chemins positionnels sont
également acceptés. Utiliser `--values=...` pour les séries commençant par un
nombre négatif. `--help`, `--list`, `--version` sont disponibles.

Sous Linux/macOS, avec les dépendances du projet et un build CMake configuré :

```bash
cmake --build build --target iprGenerateTests
./build/iprGenerateTests --feature exposure --values=-1,0,1 \
  --input tests/image/base/6V1A0327.CR3 --output tests/image/generated/ev-v1
```

L'exécutable n'est pas autonome : il utilise les mêmes bibliothèques que
IPR. Le lanceur Windows facilite un usage depuis le build local, mais ne
constitue pas un installeur distribué.

## Limites et vérifications

- L'outil génère **les exports IPR uniquement**. Pour Lightroom, reproduire
  les états du manifeste en suivant le protocole concerné, avec profil,
  espace sRGB et traitements automatiques contrôlés.
- L'arborescence et la numérotation sont propres à la nouvelle campagne :
  ne pas déposer automatiquement les résultats dans les anciens dossiers
  d'analyse dont les numéros ont une signification différente. Utiliser
  `feature`, `value` et `file` du manifeste pour établir la correspondance.
- Un export automatique ne valide ni l'interconnexion curseur/champ ni
  les gestes de l'IHM. Les tests QML et la vérification manuelle restent
  nécessaires. Recadrage, masques et IA non implémentés ne sont pas simulés.
- Le test CTest `testImageGeneratorTests` utilise des images synthétiques,
  couvre les séries post-rendu, l'isolation violet/magenta, les TIFF 16 bits
  ICC, les fichiers `.ipr`/`.xmp` préservés, les erreurs et les campagnes
  partielles. Les tests RAW existants restent complémentaires.
- Une modification de `src/tools/` sélectionne ces tests et les tests
  d'export ; une modification du contrat partagé ou de CMake entraîne la
  suite complète conformément aux règles du projet.

## Vérification d'intégration du 6 octobre 2026

Build Windows MSVC réalisé ; la suite complète a passé 14 tests CTest et
40 tests Python (comparateurs et sélection). Un export neutre du RAW
`6V1A0327.CR3`, hors banque de validation dans
`build-generator-smoke-20261006/neutral-raw/`, a confirmé le chemin RAW réel :
TIFF 16 bits, 4639 × 6959 pixels, DCP « Canon EOS R6 Mark III Adobe Standard »,
LCP « Canon (Canon RF 24-105mm F2.8 L IS USM Z) - RAW », température de
référence environ 4924 K. Ce contrôle valide la génération, **pas** la
fidélité colorimétrique ni l'équivalence du rendu Lightroom.

La version finale a également produit un neutre et la série EV −1/0/+1 depuis
ce RAW en JPEG dans `build-generator-smoke-20261006/exposure-raw-v2/`.
Les racines de profils correspondent à IPR, les quatre états gardent la WB
de référence et le même DCP. Après les compléments multi-images et catalogue
complet en mode plan, les deux tests ciblés générateur/export ont passé.
