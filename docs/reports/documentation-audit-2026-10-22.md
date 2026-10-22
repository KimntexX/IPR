# Audit documentaire — 22 octobre 2026

## État courant

**Branche :** `patch_documentation`, worktree dédié.
**Référence intégrée :** `origin/dev` à `a15f70308c7e610d7af35dc42e3b88899f7501cf`.
Fusion de synchronisation : `e4813a8c338bf6b94551837828617fcba900d5b9`.

**Conclusion : la condition « tous les documents français à jour » n'est pas
encore établie. La traduction anglaise globale n'est donc pas lancée.**
Le contrôle structurel ne constitue pas une relecture sémantique exhaustive.
Les versions anglaises existantes ne sont pas certifiées alignées.

## 1. Synchronisation et périmètre

La branche documentaire a été synchronisée avec `dev` par fusion, sans
rebase ni modification des branches des autres agents. Les travaux locaux
Couleur et les études Bruit n'ont pas été copiés. Aucun code de production,
JSON de mesure, résultat historique ou tag n'a été modifié.

L'inventaire des fichiers suivis Markdown/LaTeX sous `docs/`, plus
`README.md`, contient **211 candidats sources hors suffixe `_en`** :
46 ont un chemin homologue anglais, 165 n'en ont pas. Ce comptage par nom
n'est ni une détection de langue ni une preuve de qualité des traductions.
Il exclut les sources hors de ce périmètre, bibliographies, fichiers non
suivis et PDF.

## 2. Corrections françaises effectuées

| Document | Correction |
| --- | --- |
| README principal | Calibration active par défaut, sous conditions de caractérisation ; retrait du défaut désactivé. |
| Spécification des réglages | Master Rec.2020, statut historique des validations, calibration par catalogue et onze sections dont optique. |
| Manuel technique, chapitre 1 | Périmètre multiplateforme, distinction build CI et recette utilisateur. |
| Manuel technique, chapitre 2 | Calibration dans les préférences avancées ; deux débordements de mise en page corrigés. |
| Manuel technique, chapitre 6 | Luminance Rec.2020, sidecar v6, séparation modèle courant et essais Couleur non fusionnés. |
| Manuel technique, chapitre 10 | État courant distingué des chiffres historiques ; persistance RAW/optique déjà implémentée. |
| Matrice de validation | Retrait des verdicts historiques présentés comme validation actuelle ; défaut RAW et cache intégrés. |
| Contrat de mémoire des tests | Suppression manuelle des exports, jamais automatique par les outils. |
| Distribution Windows | Chemins et paramètres conformes au script courant ; installateur distingué comme travail de branche. |
| Règles documentaires | Ordre chronologique et conclusion courante datée des rapports. |

Les équations et états modifiés ont été confrontés notamment à
`WorkingColorSpace.h`, `ColorAdjustments.cpp`, `DetailProcessing.cpp`,
`Sidecar.h`, `UiPreferences.cpp`, `config/ipr-ui.ini`,
`scripts/ci.py` et `scripts/package-windows.ps1`.
Cela ne certifie pas les autres équations du manuel.

## 3. Vérifications exécutées

- `scripts/ci_documents.py` : **0 erreur, 0 avertissement de lien local**.
  Le contrôle couvre lecture UTF-8, JSON et existence des cibles Markdown ;
  il ne teste pas les liens externes, les ancres ni l'exactitude du contenu.
- `git diff --check` : aucune erreur d'espacement.
- `docs/technical/latex/build.ps1` : compilation réussie, **36 pages**,
  après accès autorisé au cache MiKTeX.
- Dernier journal LaTeX : pas de `Overfull` ni de référence `undefined`.
  Une inspection visuelle complète du PDF reste à faire.

Ces vérifications ne sont pas une nouvelle campagne photographique ou CI.

## 4. Écarts restants avant traduction globale

1. **Recette sémantique restante.** Les protocoles, le livre utilisateur,
   les diagrammes et les cours n'ont pas tous été confrontés intégralement
   au code courant. Les anciens rapports doivent rester datés : ne pas
   transformer leurs chiffres en résultats actuels.
2. **Installateur non intégré dans cette référence.** `patch_ci_cd` contient
   `4a46701` et `5701fb3`, avec des modifications françaises de distribution,
   profils et chapitres techniques. Contrôler la CI et intégrer le travail
   selon le workflow autorisé, puis réconcilier ces chapitres ; ne pas
   fusionner silencieusement son code depuis la branche documentaire.
3. **Couleur et Bruit en cours.** Les études et cours encore locaux ou sur
   leurs branches doivent être stabilisés et livrés avant d'être décrits
   comme production. Ils ne rendent pas rétroactivement faux les documents
   historiques, mais empêchent de déclarer le dépôt entier synchronisé.
4. **Couverture bilingue.** Auditer les 46 homologues présents avant réemploi ;
   les 165 absences par nom sont une liste de travail, pas 165 traductions
   automatiquement requises (inclut renvois, règles et historiques).

## 5. Ordre de reprise proposé

Terminer la relecture des contrats d'architecture et des protocoles courants,
puis du manuel utilisateur et des diagrammes. Réconcilier les livraisons des
branches séparées lorsqu'elles sont intégrées dans `dev`. Geler alors une
référence française identifiée par commit, traduire par famille en conservant
les liens et bascules de langue, puis compiler les documents anglais et
contrôler leur parité. Ne pas attendre une validation de chaque fonction
photographique pour traduire : il faut décrire correctement les limites,
pas les supprimer artificiellement.

Aucune publication GitHub ni fusion dans `dev` n'est effectuée par cet audit.
