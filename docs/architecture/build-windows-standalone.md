*[Français](build-windows-standalone.md) | [English](build-windows-standalone_en.md)*

# IPR — version autonome Windows x64

**Référence relue le 22 octobre 2026 :** scripts présents dans `dev`
(`a15f703`). La traduction anglaise existante n'est pas encore réalignée.

## Utilisation

Extraire toute l'archive puis lancer `bin/appIPR.exe`. Ne pas déplacer
l'exécutable seul. Le mode `standalone` embarque les bibliothèques nécessaires ;
le mode `system` suppose les dépendances installées et découvrables.
Un paquet compilé ne constitue pas une validation sur Windows 10/11 vierge.

Le paquet autonome Windows contient notamment :

- `bin/config` et `bin/profiles` : ressources copiées près de l'exécutable ;
- `share/IPR/config`, `share/IPR/profiles` et `share/IPR/licenses` :
  ressources installées et notices ;
- `package-manifest.json` : version, commit, état des sources, contrôle de
  lancement et empreintes des fichiers à l'empaquetage.

`UiPreferences` utilise `bin/config/ipr-ui.ini` si ce fichier portable existe.
Sinon, il utilise le répertoire utilisateur `QStandardPaths::AppConfigLocation`.
Les profils personnels et leurs chemins multiplateformes sont décrits dans
[le moteur de profils](../camera-profile-engine.md).

## Calibration et profils

La calibration RAW est active par défaut. Son application effective dépend
des règles compatibles avec le boîtier, le profil et le mode :
une case cochée ne signifie pas qu'un boîtier inconnu est caractérisé.
Le développement et l'optique sont persistés dans le sidecar v6.
L'ancienne restriction à cinq fichiers exacts n'est plus le contrat courant.

Le script public refuse les profils privés DCP/LCP et les calibrations privées.
Il ne propose plus `-IncludeLocalAdobeProfiles` ni `-Destination`.
Les profils Camera Neutral personnels ne font donc pas partie d'un paquet
public. L'acceptation visuelle d'un rendu ne vaut ni fidélité physique ni
autorisation de redistribution des profils.

## Reproduction depuis le dépôt

Après compilation Release, lancer depuis la racine, en adaptant les chemins
et la version validée :

```powershell
./scripts/package-windows.ps1 -BuildDirectory build-ci -OutputDirectory dist -Mode standalone -Version v0.1.X-alpha -QtRoot C:/Qt/6.8.3/msvc2022_64 -RuntimeDirectory C:/vcpkg/installed/x64-windows/bin
```

`v0.1.X-alpha` est un exemple à remplacer, pas un numéro publié.
Le script appelle `scripts/ci.py --action package`, installe via CMake,
copie les DLL tierces, vérifie le lancement, puis produit le manifeste et le ZIP.
Une destination de paquet déjà existante est refusée.
Ne pas committer les sorties `dist/`.

Les contrôles CI des branches de patch ne doivent pas publier de builds
téléchargeables : la livraison est réservée à `dev` et `main`
selon [le contrat CI](ci-et-build-multiplateforme.md).

## Installateur : travail séparé en attente d'intégration

L'installateur Windows par utilisateur est préparé sur `patch_ci_cd`
(commits `4a46701` et `5701fb3`), pas dans la référence `dev` ci-dessus.
Ne pas utiliser ses nouvelles options comme si elles étaient déjà livrées.
Sa documentation, sa compilation Inno Setup et son cycle installation /
mise à niveau / désinstallation devront être contrôlés lors de la fusion.
Ce document ne certifie ni les résultats GitHub de cette branche ni une
installation vierge.
