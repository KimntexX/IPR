# ADR-0001 — Sidecar non destructif `.ipr`

- **Date :** 2 octobre 2026
- **Statut :** Accepté
- **Décideurs :** Projet IPR

## Contexte

Les réglages et annotations doivent survivre à la fermeture sans modifier le RAW ou le fichier image. Ils doivent aussi être applicables par lot et invalidés lorsqu'ils ne correspondent plus au fichier source.

## Décision

Écrire un fichier voisin `<source>.ipr` avec une en-tête `IPR1`, version 3, taille et date de modification du source. Le contenu est sérialisé avec `QDataStream` version Qt 6.4 et écrit par `QSaveFile` de manière atomique, après un délai de 450 ms. Le lecteur accepte les versions 1 et 2 : les champs de débruitage/netteté absents en v1 reçoivent leurs valeurs neutres ; la bande orange absente en v1/v2 est insérée à zéro sans décaler les six anciennes couleurs. La version 3 est écrite lors de la sauvegarde suivante. Les anciennes versions de l'application ne lisent pas la v3.

## Conséquences

**Évolution du 4 octobre 2026 :** [ADR-0005](ADR-0005-melangeur-huit-bandes-luminance-bornee.md)
porte l'écriture à v4 avec violet/magenta distincts et migrations v1/v2/v3.
La décision initiale v3 ci-dessus décrit l'état antérieur, pas le format courant.

**Évolution du 8 octobre 2026 :** [ADR-0006](ADR-0006-restauration-raw-et-retouche-profil.md) porte l'écriture à v5 pour enregistrer les choix DCP/calibration par image et restaurer la WB avant décodage.

**Évolution du 9 octobre 2026 :** écriture v6, lecture v1–v6 : identité/SHA-256
du LCP, résolution explicite et activations objectif/aberration chromatique
latérale. Voir le [contrat optique](../camera-profile-engine.md#persistance-et-commandes-optiques-9-octobre-2026).

- Le traitement reste non destructif et indépendant des formats RAW/JPEG/TIFF.
- Un changement de taille ou d'horodatage invalide le sidecar plutôt que de risquer de mauvais réglages.
- Le format binaire doit être versionné et disposer d'une migration avant toute évolution incompatible.
- Les réglages peuvent être copiés en lot, mais les annotations de destination existantes sont préservées.

## Validation et liens

- Recette restante : [matrice de validation](../validation/matrice-validation.md).
- Conception : [guide du moteur](../scholar/moteur-graphique-ipr.tex) et [rapport de session historique](../reports/sessions/rapport-session-2026-10-02.md).
