*[Français](README.md) | [English](README_en.md)*

# Mémoire canonique des tests — JSON

Décision confirmée le **9 octobre 2026** : les **JSON versionnés et horodatés
font autorité sur les données**, en remplacement de la règle CSV initiale.
CSV et graphiques sont des dérivés pour comparaisons et tableaux. Un seul
rapport évolutif par ensemble interprète les campagnes successives. Les images
peuvent être supprimées après analyse suivant la règle du 10 octobre : exports
IPR seulement si résultat négatif, exports IPR et Lightroom de la série si
résultat positif et validé. Préserver RAW et neutres communs ; une conclusion
ambiguë ou une mémoire incomplète bloque la suppression. Journaliser les cibles.

## Organisation et versions

Conserver les JSON canoniques dans `docs/validation/raw/`. Exemple de nom,
à adapter à l'heure réelle : `mesure-lumieres-v001-20261009T160000Z.json`.
`Z` indique UTC ; les rapports peuvent afficher les heures en Europe/Paris.
Ne pas déplacer les anciennes preuves pour cette seule règle.

Chaque nouvelle campagne contient `campaign_id`, `schema_version`,
`data_version`, `measured_at_utc` (ISO 8601 avec fuseau), versions du logiciel,
de l'analyseur et du protocole. Une génération d'images est une campagne.
Les versions restent nécessaires avant même le dépôt GitHub.

JSON publié = immuable : correction ou nouvelle exécution crée une nouvelle
version horodatée. Indiquer `supersedes_campaign_id` et le motif si une version
est remplacée. Ne jamais écraser les anciennes mesures ni les adapter à une
nouvelle conclusion. En cas de divergence, le JSON canonique fait foi sur
le CSV ou le rapport ; corriger/régénérer ces dérivés.

## Informations à préserver avant suppression des images

- Mesures par scène, moteur, fonctionnalité et valeur : métrique, valeur
  numérique sans arrondi de publication, unité, zone/ROI, nombre de pixels
  retenus et dénominateur des taux.
- Sources et neutres : identifiant, chemin lors de l'analyse, SHA-256 si
  calculé, taille, dimensions, orientation, profondeur, ICC, profil de
  développement, WB, optique et normalisation.
- Traitement : autres réglages, version/build du logiciel, protocole et
  analyseur, échantillonnage, géométrie, espace colorimétrique, seuils et
  formules. Courbes, points et ROI restent structurés en JSON.
- Audit : inclus/exclu/non mesuré, motifs et informations manquantes.
  Une information inconnue reste explicitement inconnue, pas supposée conforme.

JSON UTF-8 strict : mesure absente = `null` avec statut/motif, jamais zéro
inventé ni `NaN`/`Infinity`. Une empreinte non calculée reste absente et signalée.
Ne pas reconstituer un horodatage ou des réglages inconnus. CSV exports : UTF-8,
séparateur virgule, décimales avec point, identifiant/version du JSON source
et règles d'agrégation explicites.

## Clôture et comparaison des versions

1. Vérifier couverture scènes/états/neutres et audit des réglages.
2. Publier puis relire le JSON : validité, unités, dénominateurs, exclusions,
   valeurs manquantes, provenance et complétude.
3. Générer CSV, synthèses et courbes depuis **ce JSON** ; citer campagne/version
   dans le rapport unique et la matrice.
4. Comparer les versions à protocole, métriques, scènes et réglages compatibles ;
   sinon signaler explicitement le changement de conditions.
5. Signaler les limites de mémoire avant suppression des images par l'utilisateur.
   La suppression autorisée le 10 octobre reste conditionnée au verdict explicite,
   au contrôle des chemins et à la mémoire complète ; elle exclut les RAW et neutres.

Après suppression, les métriques stockées restent comparables et leurs synthèses
recalculables. **Nouvelle métrique pixel, nouvelles ROI ou inspection visuelle
exigent les images.** Les empreintes ne reconstruisent pas les pixels. Un JSON
ne sauvegarde pas les images et ne prouve pas une qualité universelle.

## Transition historique

Les JSON/TXT de `../raw/` et CSV de `../metrics/` restent conservés. Certains
JSON n'ont pas encore tous les champs de version/provenance ; les réglages IPR
ne sont pas toujours traçables dans les TIFF. Ne pas annoncer une complétude
ou comparabilité universelle. Préserver les valeurs disponibles et signaler
les champs non récupérables lors des reprises. Ce contrat ne modifie aucune
ancienne mesure et ne certifie pas une migration déjà réalisée. Ce dossier
contient le contrat, pas une seconde copie des données.
