*[Français](build-windows-standalone.md) | [English](build-windows-standalone_en.md)*

# IPR — version autonome Windows x64

## Utilisation

Extraire l'archive entière dans un dossier accessible en écriture, puis lancer
`appIPR.exe`. Ne pas déplacer l'exécutable seul. Le kit Qt, OpenCV et LibRaw
n'ont pas besoin d'être installés séparément : leurs dépendances sont incluses.
La cible est Windows 10/11 x64 ; les essais actuels ne remplacent pas un test
sur une machine propre et sur chaque version de Windows.

- `config/ipr-ui.ini` : état de l'IHM portable, lu et mis à jour dans ce dossier.
- `config/raw-normalization.json` : calibration expérimentale, désactivée par défaut.
- `profiles/dcp` et `profiles/lcp` : profils locaux ; redémarrer après ajout.
- `profiles/index.json` : sélection explicite facultative par appareil/objectif.
- `licenses` : textes et notices des dépendances ; audit complet avant publication.
- `package-manifest.json` : inventaire et SHA-256 des fichiers à l'empaquetage.

Pour reproduire le rendu testé, ouvrir un RAW, sélectionner **Camera Neutral**
dans « Développement RAW / Profil », désactiver la WB automatique et activer
la normalisation uniquement pour les RAW audités. Le manifeste est détecté
dans `config`. Vérifier le statut d'application. Le profil et l'activation RAW
restent des choix de session, indépendants de la persistance de l'IHM.
La calibration concerne uniquement 0007, 0043, 0327, 9421 et 9950 du corpus
exact (empreintes SHA-256), avec LibRaw 0.22.2-Release. Aucun autre RAW n'est
calibré automatiquement. Camera Neutral peut toutefois être sélectionné
pour d'autres RAW compatibles, sans cette calibration spécifique.

## Profils et distribution

Le script n'inclut aucun profil Adobe par défaut. L'option explicite
`-IncludeLocalAdobeProfiles` produit une **copie locale privée de test** avec
Camera Neutral pour le Canon EOS R6 Mark III et les LCP Canon installés.
Cette copie ne doit pas être publiée, committée ou redistribuée : aucun droit
de redistribution de ces profils n'a été établi. Voir
`licenses/ADOBE-PROFILES-NOTICE.md`. Les droits de toutes les dépendances
doivent également être vérifiés avant une distribution publique.

## Reproduction depuis le dépôt

Compiler la version Release, puis depuis PowerShell à la racine :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-windows.ps1
# Copie locale de test seulement, avec les profils Adobe installés :
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-windows.ps1 -Destination dist/IPR-Windows-x64-local -IncludeLocalAdobeProfiles
```

Le script refuse toute destination existante et ne supprime rien. Ses chemins
Qt/OpenCV/vcpkg sont paramétrables. Les dossiers générés et archives `dist/`
ne doivent pas entrer dans le dépôt. Ils ne contiennent pas les photographies.

L'accord visuel Camera Neutral a été accepté par l'utilisateur ; la parité
exacte Lightroom et la fidélité colorimétrique physique ne sont pas validées.
Le rendu Adobe Standard reste à traiter et le test JPEG 9933 reste en échec.
