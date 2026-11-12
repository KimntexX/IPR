# Audit documentaire — 22 octobre 2026

## État courant

**Branche :** `patch_documentation`, worktree dédié.
**Référence intégrée :** `origin/dev` à `a15f70308c7e610d7af35dc42e3b88899f7501cf`.
Fusion de synchronisation : `e4813a8c338bf6b94551837828617fcba900d5b9`.

**État après reprise : relecture du socle français de production effectuée,
contrats et documents corrigés ; voir les contrôles et limites au §6.**
Cette relecture appartient à l'assistant, pas à l'utilisateur. La traduction
anglaise n'a pas été exécutée pendant cette passe. Les versions anglaises
existantes ne sont pas certifiées alignées.

Les sections 1 à 5 retracent la première passe ; elles ne doivent pas être
prises pour la liste actuelle des actions encore ouvertes.

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

## 4. Écarts identifiés lors de la première passe

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

## 5. Plan proposé à la fin de la première passe

Terminer la relecture des contrats d'architecture et des protocoles courants,
puis du manuel utilisateur et des diagrammes. Réconcilier les livraisons des
branches séparées lorsqu'elles sont intégrées dans `dev`. Geler alors une
référence française identifiée par commit, traduire par famille en conservant
les liens et bascules de langue, puis compiler les documents anglais et
contrôler leur parité. Ne pas attendre une validation de chaque fonction
photographique pour traduire : il faut décrire correctement les limites,
pas les supprimer artificiellement.

Aucune publication GitHub ni fusion dans `dev` n'est effectuée par cet audit.

## 6. Relecture de reprise — 22 octobre 2026

### Référence et méthode

`origin/dev` a été actualisée : elle reste à `a15f703`. Les modifications
sont isolées sur `patch_documentation`. Les contrats ont été confrontés aux
en-têtes et implémentations de l'import, du rendu, des profils, de la
calibration, du contrôleur mémoire, des recettes de test et des actions QML.
L'aide réelle de `tests/analyse_couleur.py --help` a été exécutée.

La relecture a porté sur le manuel utilisateur (familles et annexe clavier),
les chapitres techniques et diagrammes, les protocoles photographiques et
leurs guides, ainsi que les applications IPR décrites dans Scholar.
Les cours théoriques ne deviennent pas une spécification normative du moteur.
Les résultats historiques et leurs JSON n'ont pas été réécrits.

### Écarts corrigés pendant la reprise

- **Contrats d'import et de profils** : état intégré dans dev, catalogue v4,
  calibration active selon compatibilité, paramètres avancés par image,
  optique dans son groupe, chemins Qt/bundle/installation et LCP persisté.
- **Ordre de traitement** : exposition/WB avant DCP quand le contexte caméra
  existe ; repli RGB distinct. Les poids Rec.709 restent légitimes pour les
  sorties sRGB et les axes empiriques conjugués vers Rec.2020. Ils ne sont pas
  remplacés aveuglément par les poids du master.
- **Tonalité courante** : modèle adaptatif, opérations Y versus canaux sRGB,
  statistiques du neutre et réserve HL du 21 octobre. Retrait des anciennes
  fenêtres présentées comme l'implémentation actuelle.
- **Cache** : contrôleur propriétaire du LRU sur son thread, réservation,
  admission sous pression, proxy/signal caméra retenus et reconstruction export ;
  retrait du contrat obsolète « unique tampon 384 Mio hors LRU ».
- **Préférences et IHM** : onze sections avec optique, séparateurs réglables,
  palettes et texte d'information ; raccourcis relus contre QML. Sur cette
  référence, Enregistrer et Dupliquer restent désactivés et le ruban utilise
  les cases ; les évolutions non fusionnées ne sont pas décrites comme livrées.
- **Protocoles et autonomie** : analyse en mémoire avec `iprRenderServer`,
  génération TIFF facultative pour la revue visuelle, défaut de calibration,
  supports fixes sans options CLI inventées, séries Grain/Flou/Bruit exactes,
  état optique combiné à compléter manuellement et ROI de l'analyseur unifié.
- **Diagrammes** : responsabilités de cache/profils actualisées, sidecar v6,
  fournisseur de vignettes asynchrone, cas utilisateur sans diagonales traversant
  les blocs et séquences aérées. Un titre n'est plus séparé de son contenu
  faute d'espace suffisant.
- **Scholar** : applications actualisées, anciens coefficients de reconstruction
  remplacés par l'inverse Rec.2020, guide moteur réécrit sur le code courant,
  préambule commun 12 pt/interligne 1,5 et sections-chapitres sur nouvelle page.
  Les exemples sRGB scientifiquement valides restent explicitement des exemples.
- **Versionnement PDF** : attribut Git binaire explicite ; un filtre global
  `astextplain` faisait sinon traiter un PDF comme un diff textuel.

### Vérifications finales exécutées

| Contrôle | Résultat |
| --- | --- |
| `scripts/ci_documents.py` | 0 erreur, 0 avertissement de lien local |
| `git diff --check` | Aucune erreur ; attribut binaire PDF appliqué |
| Manuel technique `build.ps1` | Compilation réussie, 41 pages |
| Livre utilisateur `compile.ps1` | Compilation réussie, 46 pages |
| Scholar `build.ps1` | Les 12 PDF compilent à la racine de Scholar |
| Journaux LaTeX finaux | Pas de `Overfull`, citation/référence indéfinie ni avertissement de police relevé |
| TP images `images-exemples.py` | 10 tests réussis, sans écriture d'images |
| TP Haar `haar-exemples.py` | Tous les contrôles pédagogiques réussis |
| Sources moteur / données photo | Aucune modification de production, de RAW ou de JSON de mesures |

Une dépendance de pagination supplémentaire a provoqué une attente MiKTeX :
la compilation concernée a été arrêtée et la dépendance retirée. La version
finale réserve l'espace avec les primitives LaTeX déjà disponibles.
Des pages de diagrammes ont été rendues et inspectées ; Cairo a permis
de distinguer un artefact de titres tronqués du moteur Splash de Poppler
d'une perte réelle dans le PDF. Ce contrôle ciblé n'est pas une inspection
visuelle exhaustive de toutes les pages des douze cours.

### Conclusion courante et suite

Le socle français relu est désormais exploitable pour une traduction alignée
sur **cette référence de dev**, sans attendre que chaque fonction photo soit
déclarée validée. La réserve HL, les limites DNG, le proxy de zoom et les
commandes en attente restent explicitement documentés.

Ne pas confondre cette conclusion avec une certification de chacun des
211 candidats de l'inventaire initial, une réévaluation de tous les articles
scientifiques, une vérification des liens externes ou une nouvelle campagne
photographique. Les journaux et rapports anciens gardent leur date.

Les travaux non fusionnés de CI/installation, Couleur, Bruit et IHM restent
hors du comportement décrit de dev. Ils doivent être réconciliés après
intégration autorisée ; **ils n'imposent pas de bloquer la traduction du socle
déjà relu**. Traduire ensuite par famille à partir des sources françaises
identifiées, contrôler les liens et compiler les versions anglaises.
Aucune traduction globale, publication GitHub, fusion dans dev/main ou
création de tag n'est annoncée par cette passe.
