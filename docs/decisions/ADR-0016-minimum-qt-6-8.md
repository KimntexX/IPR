# ADR-0016 — Minimum Qt 6.8 pour tous les modes

- **Date :** 2026-10-17
- **Statut :** Accepté
- **Décideurs :** utilisateur ; application sur `patch_light`

## Contexte

Le 15 octobre 2026, l'étude multiplateforme a retenu Qt 6.8 comme minimum
de distribution, sans l'appliquer : CMake exigeait encore Qt 6.4 et la CI
Linux système compilait avec le Qt 6.4.2 d'Ubuntu 24.04. Cet écart a eu un
coût réel le 17 octobre : Qt 6.4.2 convertit mal `RGBA64` vers `RGBA32FPx4`
(couleur divisée deux fois par l'alpha). Un PNG 16 bits relu à
10000/17000 d'alpha donnait 0,588 de rouge au lieu de 0,153. Qt 6.8.3 donne
la valeur exacte. Maintenir Qt 6.4 imposait donc des contournements.

## Décision

Qt **6.8** est le minimum de compilation et d'exécution, en mode système
comme en mode standalone :

- `find_package(Qt6 6.8 …)` ; aucun chemin de code propre à Qt < 6.8.
- CI Linux sur le SDK Qt 6.8.3 dans les deux modes. Ubuntu 24.04, limité à
  Qt 6.4.2, ne reçoit plus d'archive système : le standalone y reste possible.
- Le paquet `.deb` système est construit sur Debian 13 (Qt 6.8.2) ;
  Fedora et Arch gardent leurs dépôts, déjà au-delà de 6.8.
- macOS : Homebrew (système) et SDK officiel 6.8.3 (standalone), inchangés.

## Conséquences

- Bénéfices : une seule version minimale, plus de contournement de version ;
  les primaires nommées `Bt2020` deviennent disponibles partout.
- Coûts et risques : plus de paquet système pour Ubuntu 24.04 / Debian 12 ;
  une compilation locale Linux sur ces systèmes exige un SDK Qt ≥ 6.8.
- Compatibilité : la version de flux `QDataStream::Qt_6_4` des sidecars `.ipr`
  (ADR-0001) est un format de fichier, inchangé ; les anciens sidecars restent lisibles.
- Validation requise : CI complète sur les quatre plateformes avec Qt ≥ 6.8 ;
  build Linux local sur SDK 6.8.3 ; nouvelle génération des paquets Debian 13 au
  prochain workflow de distribution.

## Liens

- Spécification : [CI et build multiplateforme](../architecture/ci-et-build-multiplateforme.md)
- Code : `CMakeLists.txt`, `cmake/Deployment.cmake`, `.github/actions/setup/action.yml`,
  `.github/workflows/{ci,delivery,linux-distributions}.yml`
- ADR lié : [ADR-0001](ADR-0001-sidecar-ipr.md) (format des sidecars)
