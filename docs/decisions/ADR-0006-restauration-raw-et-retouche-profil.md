# ADR-0006 — Recette RAW par image et changement de profil comme retouche

- **Date :** 8 octobre 2026
- **Statut :** Accepté ; recette visuelle Windows à compléter
- **Complète :** ADR-0001, ADR-0003 et ADR-0005

## Décision

Le sidecar `.ipr` v5 conserve les champs v4 puis ajoute, dans cet ordre : chemin DCP, SHA-256 DCP, chemin du manifeste, SHA-256 du manifeste, activation de normalisation et identifiant du moteur incluant la version LibRaw. Le lecteur accepte v1 à v5 ; les anciennes versions n'ont aucune calibration activée implicitement. Les anciens exécutables ne lisent pas v5.

Les options RAW et la WB automatique sont restaurées **avant** le décodage et la recherche dans le cache. Le DCP résolu automatiquement est figé par image quand son chemin est connu. L'éditeur privilégie Camera Neutral lorsqu'il est disponible ; les outils de diagnostic conservent leur politique par défaut.

Un changement de DCP ne doit pas vider l'image affichée ni réinitialiser zoom, comparaison et retouches. Un worker applique le nouveau DCP à une copie indépendante du RGB caméra déjà dématricé, équilibré en blanc et corrigé par le LCP. Une erreur conserve le rendu et la recette précédents. L'origine de comparaison est renouvelée après succès : c'est la base développée avec le nouveau profil, avant les retouches.

## Constat d'intégration — 22 octobre 2026

La décision du 8 octobre et ses coûts initiaux ci-dessous sont historiques.
Le format courant est v6 ; l'optique est persistée. La calibration suit le
catalogue par modèle/profil/mode (ADR-0011/0012). Les entrées du contrôleur
mémoire peuvent retenir un signal caméra réduit avec le proxy ; le modèle
« un tampon actif de 384 Mio hors LRU » n'est plus le contrat de cache.
Le chemin rapide reste conditionné à la compatibilité de la recette.
Voir [le contrôle mémoire](../architecture/controle-memoire-images.md)
et [le contrat RAW/profils](../camera-profile-engine.md).

## Limites et coûts de l'implémentation initiale

- Un seul tampon caméra actif, plafonné à **384 Mio**, hors budget du LRU d'images développées. Il consomme 12 octets par pixel et est libéré au changement d'image. Les workers peuvent prolonger temporairement sa durée de vie.
- Le chemin rapide est réservé au développement DCP, sans normalisation active et avec la même WB de décodage. WB automatique, calibration, image sans DCP, tampon trop grand ou absent imposent encore un décodage. Le rendu précédent reste visible pendant ce travail.
- Un retour depuis le LRU peut ne pas disposer du tampon pré-DCP ; le premier changement non mémorisé redécode alors le RAW. Les changements suivants peuvent bénéficier du tampon.
- La normalisation reste limitée aux couples RAW/DCP et à la version LibRaw audités. L'option enregistrée n'est pas une preuve d'application.
- Limite initiale levée le 9 octobre : le sidecar v6 fige aussi identité/SHA-256 du LCP et activations optiques. Une ressource modifiée/absente suspend les corrections, avec avertissement. Voir le [contrat optique](../camera-profile-engine.md#persistance-et-commandes-optiques-9-octobre-2026).
- Les empreintes servent à signaler les ressources manquantes/modifiées. La relocalisation automatique recherche un fichier de même nom et de même SHA-256 sous les dossiers `profiles/` ou `config/` de l'exécutable. Un repli est signalé et ne promet pas le rendu initial. Une mise à jour du moteur n'émule pas les anciens algorithmes.

## Interface et sauvegarde

La réinitialisation générale concerne les retouches : DCP, calibration et mode de WB RAW restent conservés. Une commande distincte « Réinitialiser profil / calibration » revient au choix automatique et désactive la calibration sans effacer les retouches ni modifier le mode de WB. Les contrôles RAW sont désactivés sur les images non RAW.

L'écriture reste atomique avec `QSaveFile`, différée de 450 ms et vidée à la fermeture normale de l'éditeur. Une fermeture pendant un changement RAW conserve la dernière base validée. Les erreurs d'écriture sont signalées. La copie des retouches à une sélection conserve les choix RAW/WB et annotations propres aux cibles, pour ne pas propager un profil de boîtier incompatible.

## Validation

`editorRawDevelopmentTests` couvre restauration avant décodage, redémarrage, valeurs et pixels identiques, réinitialisations indépendantes, empreintes modifiées, absence d'héritage entre photos, navigation coalescée, changement de DCP sans appel au décodeur et retour arrière sur erreur. Les profils synthétiques et images temporaires n'altèrent pas les originaux.

La vérification photographique demeure nécessaire : confort réel, continuité du zoom/comparatif et temps de changement sur plusieurs scènes. Ne pas confondre la réduction du travail avec un facteur d'accélération mesuré.
