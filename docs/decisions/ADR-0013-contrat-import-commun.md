# ADR-0013 — Une frontière d'import commune aux formats

Date : 16 octobre 2026. Statut : direction acceptée par l'utilisateur,
implémentée localement ; qualification limitée aux tests et corpus documentés.

## Contexte

Conserver un développement caméra pour les RAW et une conversion ICC pour les
images déjà développées est nécessaire. Dupliquer le rendu aval, les contrôles
ou la lecture des métadonnées dans l'éditeur augmenterait le risque de divergence.
L'utilisateur valide une architecture à entrées spécialisées et moteur commun.

## Décision

Introduire `ImageImport::Result` et `ImageImport::complete()` comme frontière
commune. `ImageImport::load()` orchestre RAW et raster. Extraire `ImageMetadata`
hors de `RawDecoder`, sans alias ni ancienne implémentation de lecture raster.
Le cache et la retouche rapide de profil reconstruisent le même résultat et
passent les mêmes contrôles avant d'être consommés par l'éditeur.

Le contrat impose le master RGBA float32 Rec.2020 linéaire D65, les métadonnées,
la provenance, les avertissements et les capacités effectives. Les paramètres
caméra, DCP/ICC et conversions de sortie ne sont pas appliqués une seconde fois.
Le proxy reste une responsabilité de l'éditeur, après le contrat pleine résolution.

## Conséquences

- Un seul contrôle de frontière, indépendant de l'extension et sans chemin legacy.
- Pas de traitement RAW ajouté à un JPEG ; pas de lecture ICC appliquée au
  signal caméra comme s'il s'agissait d'une image développée.
- Retour du cache et retouche de profil soumis au même contrat ; conservation
  du comportement d'erreur de l'éditeur et de ses retouches non destructives.
- Coût de validation linéaire dans les pixels, sans clone/conversion à cette
  étape. Le travail d'optimisation devra mesurer ce coût, pas le supposer nul.
- Les codecs, plugins et domaines de caractérisation restent spécialisés.
  L'interface commune ne certifie pas tous les formats/boîtiers ni leur fidélité.

Le [contrat détaillé](../architecture/import-images.md), son diagramme et les
limites sont maintenus dans l'architecture. Les résultats et preuves restent
dans le rapport unique de base colorimétrique et les mémoires JSON horodatées.
