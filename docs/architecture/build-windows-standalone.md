*[Français](build-windows-standalone.md) | [English](build-windows-standalone_en.md)*

# IPR — distributions Windows x64

Mise à jour du 21 octobre 2026. Cette version française décrit le packaging
courant ; sa traduction relève du chantier documentaire.

## ZIP portable et installateur

Les deux distributions utilisent le même contenu standalone vérifié :

- ZIP : extraire toute l’archive et lancer `bin/appIPR.exe`. Les préférences
  restent dans `bin/config/ipr-ui.ini` ; le dossier doit être accessible en écriture.
- Inno Setup : `IPR-<version>-windows-x64-setup.exe`, installation par utilisateur
  dans `%LOCALAPPDATA%/Programs/IPR`, sans élévation par défaut. Raccourci Démarrer,
  bureau facultatif, mise à niveau avec AppId constant et désinstallation Windows.

Cible : Windows 10/11 x64. Windows ARM et 32 bits ne sont pas certifiés.
Qt/QML et les DLL tierces proviennent du packaging existant.
Le mode `system` reste distinct et n’est pas enveloppé dans cet installateur.

## Préférences et profils

L’installateur omet `bin/config/ipr-ui.ini`. Le comportement existant de
`UiPreferences` utilise alors `QStandardPaths::AppConfigLocation/ipr-ui.ini`,
initialisé avec les valeurs embarquées. Le manifeste installé possède son
propre inventaire et l’empreinte du manifeste portable source.

La calibration publique reste dans `bin/config/raw-normalization.json` ;
c’est une ressource distribuée, distincte des préférences utilisateur.
Les profils personnels sont recherchés dans
`QStandardPaths::AppDataLocation/profiles` : `index.json`, `dcp/`, `lcp/`.
Ils ne sont pas gérés par le désinstalleur. Redémarrer après ajout.
Les profils embarqués sont dans `bin/profiles/`, avec un index public vide.

Le chargement DCP/LCP est multiplateforme : racine portable, ressources du
bundle macOS et `share/IPR/profiles/` sur Unix. La découverte Adobe n’est pas
complètement uniforme : automatique sous Windows, cas WSL sous Linux, pas de
racine Adobe macOS implicite. `IPR_ADOBE_CAMERARAW_DIR` fonctionne sur les trois OS.

Aucun DCP/LCP privé n’est inclus. L’inventaire SHA-256 est vérifié avant
compilation ; les fichiers modifiés ou supplémentaires sont refusés.
Cela ne remplace pas l’audit juridique. Il n’existe pas d’option courante
d’empaquetage privé des profils Adobe.

## Construction locale

Préparer un build Release standalone au même libellé de version que le paquet.
Depuis la racine, adapter les chemins et la version de cet exemple :

```powershell
$env:IPR_ISCC = 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe'
./scripts/package-windows.ps1 -BuildDirectory build-ci -OutputDirectory dist `
  -Mode standalone -Version 0.1.20-alpha -QtRoot 'C:/Qt/6.8.3/msvc2022_64' `
  -RuntimeDirectory 'build-ci/vcpkg_installed/x64-windows/bin' -Installer
```

La version est illustrative, pas un jalon livré. Inno Setup 6.3+ est requis ;
la CI installe 6.7.3. Sans `-Installer`, seul le ZIP est demandé.
Le script ne recompile pas le C++, refuse les sorties existantes et n’installe
pas IPR sur le poste développeur. Les sorties restent dans `dist/`, hors Git.

Pour compiler seulement le setup depuis un staging déjà mesuré :

```powershell
python scripts/windows_installer.py --stage 'dist/.staging/IPR-<libelle>-windows-x64-standalone' `
  --version '<libelle>' --output-dir dist --iscc 'CHEMIN/ISCC.exe'
```

Le libellé doit correspondre au manifeste. Ce script ne transforme pas
un dossier de build ou un paquet system en standalone.

## CI et limites de validation

Sur `patch_ci_cd`, le setup est compilé/testé temporairement puis supprimé :
pas de binaire téléchargeable. Sur `dev`/`main`, ZIP, setup et SHA-256 sont livrés
seulement après validation du commit exact par la politique existante.

Le runner Windows GitHub hébergé et jetable teste installation silencieuse,
réinstallation du même paquet, démarrage QML hors environnement développeur et
désinstallation. Un fichier non possédé doit survivre. Une installation IPR
préexistante bloque ce test ; il n’est pas exécuté automatiquement en local.

Restent à vérifier sur une machine utilisateur sans SDK : compte standard,
chemins avec espaces/accents, préférences/profils personnels, mise à niveau
entre deux versions différentes et ouverture RAW réelle. Photos et sidecars
ne doivent pas être altérés. Pas de signature de distribution configurée ;
un avertissement Windows reste possible, sans justifier de désactiver la sécurité.

## Références

- [Inno : mode sans élévation](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm).
- [Inno : identité AppId](https://jrsoftware.org/ishelp/topic_setup_appid.htm).
- [Inno : versions officielles](https://jrsoftware.org/isdl.php).
- [Qt : emplacements applicatifs](https://doc.qt.io/qt-6/qstandardpaths.html).
