# Spécification — Réglages photographiques

**Statut :** partiellement validé, consolidation au 8 octobre 2026.
**Implémentation :** `ImageEditor::renderWorkingImage()` et `ColorPipeline`.

## Contrat commun

- Les réglages sont non destructifs et stockés par image dans le sidecar `.ipr`.
- Un changement d'image restaure ses propres réglages ou les valeurs de référence en l'absence de sidecar valide.
- L'aperçu emploie une image proxy ; l'export repart de l'original.
- Les contrôles numériques et curseurs représentent une seule valeur et doivent rester synchronisés.

## État par groupe

| Groupe | Commandes | Statut de référence |
| --- | --- | --- |
| Tonalité | EV, contraste, ombres, noirs, hautes lumières, blancs | Validé sur le corpus de base ; détails dans la matrice de validation. |
| Balance des blancs | température, nuance, balance automatique | Validé sur corpus ; référence par image et repli 5200 K. |
| Couleur globale | vibrance, saturation | Validé. |
| Détail et atmosphère | texture, clarté, voile, grain, flou, réduction de bruit, netteté | Texture, clarté et voile validés ; bruit, netteté, grain et flou nécessitent une recette dédiée. |
| Couleur sélective | mélangeur 8 bandes (rouge, orange, jaune, vert, cyan, bleu, violet, magenta), color grading 3 zones | Mélangeur : extrêmes et progressivité mesurés ; calibration/recette visuelle ouvertes. Grading : recette photographique à faire. |
| Courbes | luminance, rouge, vert, bleu | Implémenté ; recette de stabilité et de compatibilité HDR à compléter. |

## Organisation du panneau de réglages

L'histogramme et les informations de prise de vue restent en haut, suivis du
bouton de réinitialisation globale. Les commandes sont réparties en dix sections
repliables indépendantes : Développement RAW / Profil, Lumière, Courbe de tonalité, Couleur, Mélangeur de
couleurs, Color grading, Effets, Réduction du bruit, Netteté et Export.

- Au premier lancement, Développement RAW / Profil et Lumière sont dépliées ; les autres sections sont repliées.
  Aux lancements suivants, le dépliage est restauré depuis l’INI utilisateur.
- Cliquer un titre, ou l'activer au clavier, déplie/replie son contenu. Plusieurs
  sections peuvent rester ouvertes en même temps.
- Replier une section ne détruit pas ses contrôles et ne modifie ni leurs
  valeurs, ni les options d'export, ni les réglages de l'image.
- Un changement d'image actualise aussi les contrôles repliés ; l'état de
  dépliage ne change pas. Cet état d'interface n'est pas stocké dans le sidecar.
- Réduction du bruit contient uniquement les bruits de luminance et de couleur.
  Netteté contient gain, rayon et masquage. Export ne contient que format,
  qualité JPEG (visible pour JPEG) et choix de destination.
- Le flou gaussien est regroupé avec Texture, Clarté, Voile et Grain dans Effets.
- Le composant commun `SettingsSection.qml` est enregistré dans le module QML
  CMake. Les titres internes des modules courbe/mélangeur/grading sont masqués
  dans le panneau pour ne pas dupliquer les en-têtes.
- La largeur de défilement est bornée à la largeur utile du panneau ; 8 pixels
  sont réservés à la barre verticale. Les libellés longs se replient ou
  s'abrègent pour rester lisibles à la largeur minimale de l'IHM.

Les protocoles et preuves sont référencés depuis [la matrice de validation](../validation/matrice-validation.md). Les fondements et exemples sont disponibles dans [Scholar](../scholar/README.md).

## Développement RAW et base colorimétrique

Le choix DCP et la normalisation expérimentale sont accessibles sous
l'histogramme. Camera Neutral est accepté visuellement par l'utilisateur,
sans validation de fidélité physique ni parité Lightroom. Le manifeste est
limité à cinq RAW exacts et à LibRaw 0.22.2-Release ; il reste désactivé par défaut.
Le sidecar v6 enregistre les profils DCP/LCP, le manifeste, leurs SHA-256 et les activations.
Ces choix et la WB automatique sont restaurés avant décodage. Camera Neutral
est privilégié à la première ouverture lorsqu'il est disponible. Les ressources
manquantes/modifiées et les changements de version du moteur sont signalés.
Le changement de DCP utilise le tampon pré-DCP de l'image active lorsque possible,
sans vider l'aperçu ni réinitialiser zoom/retouches. La WB de décodage et la
normalisation peuvent nécessiter un redécodage, toujours sans vider l'aperçu.
La réinitialisation des retouches conserve les choix RAW ; une commande distincte
réinitialise profil/calibration. Le LCP automatiquement résolu est figé par image ;
les cases objectif (distorsion/vignettage) et aberration chromatique latérale sont
indépendantes, conservées au reset des retouches et appliquées à l'export.
Sans données LCP correspondantes, le statut indique la non-application.
Les anciens sidecars v1–v5 migrent avec les deux corrections activées pour
préserver la politique antérieure. Voir [ADR-0006](../decisions/ADR-0006-restauration-raw-et-retouche-profil.md).
Voir le [contrat des profils](../camera-profile-engine.md) et le
[rapport unique](../validation/analyses/analyse-base-colorimetrique.md).

### Réduction de bruit : correctif et revalidation

Les deux bruits sont compris entre 0 et 100. À 0/0, le traitement est bypassé
sans modification des pixels. La couleur seule préserve Y Rec.709 linéaire
par pixel à la précision flottante. Le correctif CPU conserve les moyennes
des plans, pas nécessairement celles de chaque région locale, et protège
les frontières de couleur sans imposer un bord de luminance.
Le rendu non nul change mais les valeurs de réglage du sidecar
ne sont pas modifiées par ce correctif. Une image ouverte avec son ancien sidecar utilise le
nouveau moteur : ses anciens TIFF ne constituent pas une validation actuelle.
Voir [modèle](../architecture/denoising-sharpening.md) et
[protocole v2](../validation/protocols/protocole-bruit-v2.md).

## Préférences d’interface, distinctes du développement

Le fichier global `ipr-ui.ini` conserve taille/position/état de fenêtre, visibilité
et dimensions des panneaux, couleurs principales, sections ouvertes, canal de
courbe, bande HSL, zone de grading et format/qualité d’export. La sélection d’une
bande ou d’une zone restaure ses contrôles sans changer les valeurs photo.
Changer d’image ne réinitialise pas ces préférences de présentation ; les
réglages de développement restent restaurés par le sidecar de l’image.

Le menu Affichage permet de masquer les informations, l’histogramme et le ruban,
et affiche le chemin du fichier. Les dimensions et couleurs se personnalisent
dans l’INI, application fermée. Les clés futures sont présentes mais inactives.
Voir [le contrat des préférences](../architecture/ui-preferences.md).

## Mélangeur : contrat de sélection et persistance

La sélection d'un cercle recharge les trois valeurs de sa bande sans modifier les
réglages d'une autre bande. Le mélangeur et le color grading fonctionnent même si
la saturation et la vibrance globales valent zéro.

Les centres de teinte HSV **RGB linéaire** sont 0°, 30°, 60°, 120°, 180°, 240°, 270° et
300°. Chaque masque triangulaire s'annule aux centres voisins ; son poids est
calculé sur la teinte d'origine, avant toute modification. Ces centres ne doivent
pas être assimilés aux teintes mesurées directement dans un JPEG gamma-encodé.

Le sidecar v4 sépare violet (indice 6) et magenta (7). La lecture v1/v2 insère
orange et violet à zéro ; la lecture v3 insère uniquement violet à zéro et
migre l'ancien dernier triplet vers magenta. Les valeurs sont préservées, mais
les transitions et le modèle changent : aucun rendu identique n'est promis pour
les réglages non nuls. Les anciennes versions d'IPR ne lisent pas v4.

Depuis le correctif du 4 octobre, `ColorMixer::apply` protège exactement les
achromatiques et atténue progressivement les faibles chromas avec
smoothstep(0, 0,05, S). Les commandes pondérées sont additionnées avant application.
Teinte et saturation conservent Y en linéaire ; la luminance applique un gain
2^(0,5 × commande pondérée/100), borné entre 1/√2 et √2. Les choix 5% et ±0,5 EV
sont provisoires, à calibrer sur plusieurs scènes. Les couleurs à canal négatif
restent intactes dans ce module. Le gamut mapping de sortie peut modifier Y.

Les défauts relevés dans [le corpus étendu](../validation/analyses/analyse-color-mixer-corpus-etendu-2026-10-04.md)
ont motivé ces corrections ; ce corpus historique ne valide pas le nouveau
moteur. Voir [ADR-0005](../decisions/ADR-0005-melangeur-huit-bandes-luminance-bornee.md)
et le protocole pour les nouveaux exports. Le rendu RAW neutre reste inchangé.

## Export — type de fichier et numérotation

À chaque ouverture du dialogue, `prepareExportDialog()` sélectionne le filtre
du format choisi dans le panneau (JPEG/PNG/TIFF/DNG). Le suffixe par défaut suit
ce filtre. Si l'utilisateur change le type dans le dialogue et valide, le panneau
et l'encodeur adoptent ce type ; une annulation ne change pas le format du panneau.

La première proposition utilise le dossier de la photo source. Après validation
d'un export, les propositions suivantes recherchent les noms disponibles dans
la dernière destination choisie, conservée pendant la session IHM. Le dossier
est conservé même si l'image ou le format change ; le nom de base suit la photo
courante et les collisions sont propres à chaque extension. Une annulation ne
change pas cette destination ; elle n'est pas persistée après redémarrage.

Le nom proposé reste `<source>-IPR.<extension>` lorsqu'il est disponible.
Sinon, IPR recherche `-2`, `-3`, etc., en sautant les noms occupés. Un suffixe
numérique existant est poursuivi (`photo-IPR-5.tiff` → `photo-IPR-6.tiff`) ; les
zéros de tête sont conservés (`photo-009.png` → `photo-010.png`). Les collisions
sont vérifiées sur l'extension réellement écrite, après normalisation du format,
et les alias valides `.jpeg`/`.tif` restent acceptés. Les dossiers et liens
symboliques existants sont également considérés comme des noms occupés.

Les anciens exports et le fichier source ne sont pas remplacés. L'encodeur écrit
d'abord un fichier temporaire voisin ; après succès, le nom final est revérifié
et publié avec `QFile::rename`, qui refuse un nom déjà existant selon son
[contrat Qt](https://doc.qt.io/qt-6/qfile.html#rename). Les collisions apparues
pendant l'export provoquent une nouvelle recherche de nom, avec 32 tentatives
maximum. Les temporaires non publiés sont nettoyés ; le statut indique le chemin
réel écrit. Ce mécanisme ne constitue pas une transaction multi-clients sur
tous les systèmes de fichiers réseau ou sur le repli par copie de Qt.

La confirmation native d'écrasement est désactivée, puisque la politique est
de numéroter une copie, pas de remplacer un fichier. Vérifications automatisées
dans `imageExportTests` et `settingsQmlTests` ; confort du dialogue natif Windows
à confirmer manuellement.

### Cycle de vie du temporaire sous Windows

La réservation `QTemporaryFile` est détruite avant l'encodeur et `QSaveFile` :
`close()` seul conserve un descripteur ouvert en interne, conformément au
[contrat Qt](https://doc.qt.io/qt-6/qtemporaryfile.html#details). Garder cet objet
vivant pendant le remplacement du temporaire provoquait « Accès refusé » sous
Windows, malgré des droits de dossier corrects. Le chemin réservé reste possédé
par l'export et un `QScopeGuard` assure son nettoyage en cas d'échec ; après
publication réussie, ce nettoyage est désarmé. Le fichier final numéroté reste
intact. Le statut d'erreur précise désormais création, écriture, validation ou
publication pour distinguer une permission réelle d'un verrouillage interne.

`imageExportTests` est aussi compilable et enregistré dans CTest sous Windows.
Son argument facultatif est un dossier de destination existant : le test crée
un sous-dossier temporaire isolé, ne lit ni n'écrase les photos réelles et nettoie
uniquement ses propres fichiers en sortie.
