# Audit et tri de publication du banc RAW

Demande de l'utilisateur du 1er novembre 2026 : auditer, trier puis publier
`patch_raw_bench` sur GitHub, sans fusion dans `dev`. Branche synchronisée avec
`origin/dev` par une fusion normale, sans réécriture d'historique.

## Périmètre conservé

- Outils du banc, contrats et tests : captures intermédiaires, parité, études
  statistiques et comparaison de placement couleur ; lanceur de fin de test.
- Preuves JSON canoniques, essais négatifs compris. Elles sont nécessaires à
  la traçabilité et ne constituent pas un ancien chemin de production à supprimer.
- Dernière campagne complète : 28 scènes, 112 parités de production et 672 lignes
  scène/état (560 IPR dont 504 états couleur non nuls, 112 références Lightroom).
  Les 28 empreintes de fichiers et le gel source ont été vérifiés avant publication.
- Les échecs de préparation sont conservés séparément, jamais comptés comme
  rendus photographiques réussis.

Le placement courant reste post-tons. Le pré-profil seul n'est pas supérieur
globalement ; l'hybride peut réduire davantage les taches dans les noirs mais
modifie parfois leur luminosité. Aucune promotion de production dans cette livraison.

## Tri effectué

Le code source et les preuves sont distingués des builds, journaux opérationnels,
captures binaires, images originales et profils tiers. Ces derniers restent locaux
et ignorés ; aucun nouveau fichier de ces types n'est ajouté. Les pièces de
coordination entre agents et les environnements Python restent également locaux.
Le README du banc précise désormais son statut de recherche et ses outils.

Les nouvelles preuves couleur sont ajoutées sans modification de leurs mesures.
Une règle `.gitattributes` ciblée désactive la conversion de fins de ligne pour
ces fichiers : leurs empreintes portent sur leurs octets réels. Aucun ancien
JSON publié n'est réécrit pour cette règle. Aucun RAW, TIFF, build ou historique
Git n'a été supprimé. Pas de compression alternative qui casserait les chemins
ou remplacerait les JSON canoniques par des résumés.

## Taille et contrôles

Au premier inventaire après synchronisation de `dev`, 223 blobs Git non encore
accessibles sur `origin` représentaient 149 261 091 octets, soit environ 142 Mio
non compressés. Le plus grand était une preuve statistique de 41,3 Mio. Ce n'est
pas le volume du pack réseau, qui bénéficie de la compression Git.
Les nouveaux résultats locaux représentaient environ 168 Mio supplémentaires.
Le dossier complet hérite aussi de données déjà publiées sur d'autres branches :
ne pas les compter toutes comme un nouvel envoi.

L'auditeur examine les blobs historiques inaccessibles depuis les références
`origin` récupérées, pas uniquement l'arbre courant, ainsi que les nouveaux
fichiers non ignorés. Il vérifie les JSON stricts, quelques familles de secrets,
des champs sensibles nommés (GPS, numéros de série, jetons, profils encodés),
les types privés/générés et un seuil local de 100 Mio par blob. Les résultats
ne contiennent jamais les valeurs potentiellement sensibles détectées.

L'inventaire initial ne présente pas de signal dans ces contrôles. Une recherche
complémentaire des champs GPS/série, ICC/DCP/LCP/images encodés et tables de
profils dans les nouvelles preuves JSON n'a retourné aucun champ correspondant.
Les chemins locaux Windows, noms de prises, boîtiers, réglages et empreintes
sont conservés comme provenance. Ils ne permettent pas de télécharger les photos.

Une preuve horodatée `raw-bench-publication-audit-*` dans
`docs/validation/raw/` consigne le dernier état audité avant publication.
Les fichiers nouveaux sont ajoutés explicitement ; aucun `git add` global.
Les octets des 34 nouvelles preuves ont aussi été comparés à leur contenu dans
l'index Git, sans différence. Les CRLF de ces JSON immuables sont reconnus comme
fins de ligne, sans désactiver les autres contrôles d'espaces des fichiers source.
Vingt-six tests ciblés passent (audit 6, placement 8, lanceur 12), ainsi que le
contrôle documentaire, sans erreur ni avertissement de lien local.

## Limites

Il s'agit d'un audit ciblé, pas d'une recherche exhaustive de tous les secrets,
d'une qualification juridique, d'un nouveau test de qualité photographique ou
d'une garantie de reproduction sans corpus privé. L'audit différentiel ne
certifie pas les fichiers déjà présents sur `origin`. La taille du dépôt et
la durée de clonage augmentent ; cette branche n'est pas une version distribuable.

La publication de la branche seule n'autorise ni sa fusion dans `dev`, ni un
tag, ni une publication de fichiers tiers. Le chemin couleur caméra reste
disponible pour les comparaisons de recherche jusqu'à la bêta, selon la décision
de l'utilisateur ; l'application continue de l'appeler avec chroma à zéro.
