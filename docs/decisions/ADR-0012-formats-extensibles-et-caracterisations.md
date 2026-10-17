# ADR-0012 — Formats extensibles et stockage des caractérisations

Date : 16 octobre 2026. Statut : décision de structure acceptée sur demande
utilisateur ; premier lot implémenté localement, transition complète restante.
Complète les ADR-0010/0011, sans modifier le rendu R6/R7 accepté.

## Décision

Séparer trois responsabilités : le décodeur comprend le fichier ; une
caractérisation interprète un boîtier/mode et son décodeur ; le DCP définit
la transformation couleur et le rendu. Aucun de ces éléments ne rend à lui
seul le moteur universel.

### Formats encore inconnus

`RawDecoder::registerFormatHandler()` est le point d'entrée explicite. Un
adaptateur enregistre ses extensions, une fonction de décodage et un
`FormatOptions` contenant lecteur de vignette facultatif, identifiant et
version du décodeur. Les fonctions s'exécutent hors verrou du registre.

Il n'existe pas de joker qui prétendrait décoder un format inconnu. Sans
adaptateur : erreur explicite ; sans lecteur de vignette : vignette absente,
jamais tentative silencieuse par LibRaw. Les doublons sont refusés sans
enregistrement partiel. Les extensions normalisées sont alphanumériques.

Enregistrer les adaptateurs au démarrage, avant la découverte des dossiers.
Le modèle et les fournisseurs utilisent les extensions enregistrées. Il
ne s'agit **pas** d'un chargeur de plugins DLL ni d'une détection par signature
du contenu : ces deux mécanismes restent à concevoir si nécessaires.
Tout adaptateur doit vérifier son contenu, fournir les métadonnées et produire
le master RGBA float fini Rec.2020 linéaire D65. Cette vérification de sortie
ne garantit pas la justesse colorimétrique du décodage.

### Stockage retenu : DCP et JSON complémentaires

| Support | Rôle retenu | Avantages | Limites / coût |
| --- | --- | --- | --- |
| DCP | Matrices, illuminants, tables, courbe et offset de rendu natif | Portable entre moteurs prenant en charge ces tags ; profil couleur autonome | Ne décrit pas la décompression ni tous les modes/niveaux du capteur ; tags privés non interopérables ; ne pas modifier les profils Adobe |
| JSON versionné | Catalogue de caractérisations et associations de rendu IPR | Lisible, contrôlable en revue Git, provenance et conditions explicites ; aucune dépendance nouvelle | Validation stricte, cohérence et invalidation à maintenir ; pas de requêtes transactionnelles complexes |
| SQLite | Éventuel index/cache dérivé de bibliothèque à grande échelle | Transactions, index et relations, accès partiel | Schéma/migrations et outillage supplémentaires, diff Git binaire peu lisible ; pas justifié pour trois règles |

**Maintenant :** conserver `config/raw-normalization.json` comme catalogue
unique schéma 4, déjà embarqué et déployé. `entries` contient les règles de
mode et leur domaine de preuve ; `profile_rendering` contient les préférences
de rendu propres au couple boîtier/DCP. Les JSON d'analyse horodatés restent
les preuves canoniques : le catalogue de production n'est pas leur substitut.
Les sidecars `.ipr` restent les recettes des photos, pas une copie de la
base de caractérisation. Le chemin persistant du catalogue reste inchangé.

**Cible après caractérisation physique :** paramètres capteur indépendants du
DCP, et paramètres de rendu associés au profil. Les règles R6/R7 actuelles
sont empiriques et restent liées au SHA-256 DCP de leur validation. Le premier
lot ne supprime pas cette précaution et ne transforme pas ces valeurs en
mesures physiques universelles. Un futur offset placé dans un DCP IPR via
`BaselineExposureOffset` remplace son complément JSON correspondant : ne pas
additionner deux fois le même réglage.

Si le catalogue devient volumineux, envisager des sources JSON par boîtier
assemblées en un catalogue de distribution, puis un index SQLite **recréable**.
Ne pas créer aujourd'hui une seconde source de vérité ni un import SQL.

## Contrat courant et coût

Chaque entrée de schéma 4 déclare `format_extension`, `decoder_id`,
`decoder_version`, marque, modèle, bornes noir/blanc natifs, blanc retenu,
baseline en EV, SHA-256 DCP et provenance. Identifiants/version et marque/modèle
sont comparés exactement ; ne pas généraliser une règle par ressemblance de nom.
Le contexte appartient à chaque image, jamais au premier boîtier du dossier.

`CameraCharacterization` charge le catalogue et sélectionne le mode sans lire
le RAW ni le DCP. `RawNormalization` applique encore les conditions WB/baseline,
vérifie le profil, valide les valeurs et résout le complément de rendu.
Un format, modèle, mode ou décodeur non couvert conserve le chemin non calibré,
sans coefficients empruntés à un autre boîtier.

Limites : catalogue de 1 Mio maximum, 256 règles, cache de huit documents
parsés immuables indexés par contenu et protégé par mutex. Les octets du petit
catalogue sont relus/hachés pour détecter immédiatement une mise à jour ; la
sélection reste linéaire dans les règles. Le DCP est encore haché pour vérifier
son identité. Ce lot évite de parser chaque fois, mais ne supprime pas toutes
les E/S et ne promet pas une réduction mesurée du temps d'ouverture.

Les bornes scalaires 16 bits, les quatre niveaux noirs et la WB caméra sont
le domaine implémenté actuellement. Un autre CFA, une autre échelle ou une
baseline native déjà renseignée peut demander un nouvel adaptateur/contrat,
pas seulement une ligne JSON. Aucun Sony/Nikon n'est déclaré validé.

## Ajouter un nouveau boîtier

1. Vérifier d'abord que le décodeur comprend le format et le mode de capture.
   Sinon, implémenter/enregistrer l'adaptateur et son lecteur de vignette.
2. Auditer identité exacte, firmware si pertinent, profondeur, CFA, compression,
   échelle des niveaux, noirs natifs, blanc de saturation et WB. Ne pas déduire
   une calibration physique du seul nom commercial ou d'une photo bien rendue.
3. Construire les preuves JSON horodatées et le domaine d'application ; conserver
   provenance, méthode, unités et limitations. Les actuelles fenêtres de noirs
   ne remplacent pas une identification complète des modes si ceux-ci se recouvrent.
4. Ajouter une règle de catalogue seulement après essais. Associer un DCP
   réellement compatible ; mesurer séparément les paramètres de rendu. Un
   nouveau SHA-256 DCP ne récupère pas automatiquement l'offset d'un autre.
5. Tester niveaux bas/hauts ISO, RAW compressé/non compressé si disponible,
   scènes neutres/couleurs/HL et alternance avec les boîtiers déjà validés.
6. Livrer les données et leurs preuves dans le workflow de validation habituel.
   Les recettes référencent le catalogue ; elles ne recopient pas ses coefficients.

Pour une future caractérisation plus riche, prévoir conceptuellement : ID et
révision stables, mode de lecture explicite, plage d'ISO, loi noir/blanc et ses
unités, gains/WB, modèle de bruit lecture/photons si mesuré, incertitude,
source des mesures, licence et références des preuves. Ces champs ne sont
**pas interprétés aujourd'hui** ; leur ajout devra étendre le contrat et les
tests, pas placer des valeurs non utilisées dans le catalogue actuel. Les
corrections propres à l'objectif restent séparées (LCP ou autre modèle optique).

## Transition et validation

1. **Implémenté :** registre décodeur/vignette, identité de décodeur, module de
   sélection générique et catalogue schéma 4, sans parseur legacy schéma 3.
2. **Implémenté localement :** `CameraSignal` extrait le contrat RGB caméra
   pré-DCP et le rendu commun utilisés par LibRaw avec DCP et le changement
   rapide de profil. RGB float fini, identité, WB déjà appliquée et neutre
   normalisé sont contrôlés. Le tampon devient la source immuable ; les gains
   de rendu sont appliqués une fois. LibRaw porte encore la décompression,
   le dématriçage, la normalisation et l'optique. Le repli sans DCP ne change pas.
3. **Cache implémenté le 17 octobre :** DCP/LCP parsés immuables dans un LRU
   partagé borné, concurrence et invalidation par contenu testées (ADR-0014).
   Les caractérisations physiques indépendantes du profil restent à établir.
4. **À faire :** qualification R5 CR3 et 6D Mark II CR2 à réception, puis autres
   marques. Ne pas réutiliser les règles CR3 R6/R7 pour ces fichiers.

Les tests synthétiques couvrent extension inconnue, lecteur de vignette dédié,
contrat du master, identité du décodeur, données de marque/format fictifs,
ambiguïtés et mise à jour du cache. Le rejeu des 19 RAW contrôle séparément le
rendu déjà accepté. Une interface extensible n'est pas une prise en charge
effective de nouveaux formats ni une validation sur tous les ISO.

## Sources consultées le 16 octobre 2026

- [Adobe — DNG, spécification et SDK](https://www.adobe.com/support/downloads/dng/dng_sdk.html) : séparation des données RAW et des profils ; métadonnées privées possibles, sans interopérabilité implicite.
- [Library of Congress — tags TIFF/DNG](https://www.loc.gov/preservation/digital/formats/content/tiff_tags.shtml) : `BaselineExposure` et `BaselineExposureOffset` (51109), dont la somme intervient dans l'exposition de rendu.
- [SQLite — format de fichier applicatif](https://www.sqlite.org/appfileformat.html) : avantages transactionnels et accès partiel ; le choix JSON pour le petit catalogue est une décision IPR, pas une conclusion prescrite par SQLite.
