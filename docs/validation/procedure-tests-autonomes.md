# Tester les fonctionnalités d'IPR en autonomie

Procédure alignée le 12 octobre 2026 avec `patch_validation`. Recompiler la
branche qui contient les outils avant de générer. Un build réussi n'est pas
une validation photographique.

## 1. Choisir le périmètre et compiler

Depuis la racine du projet, application fermée :

```bash
bash scripts/build-windows.sh --dry-run
bash scripts/build-windows.sh
# Une zone précise sans reconstruire l'application :
bash scripts/build-windows.sh --tests-only --paths src/core/ColorMixer.cpp
# Changement transversal ou porte d'intégration :
bash scripts/build-windows.sh --full-tests
```

Attendre un code 0 et aucun échec. `--tests-only` ne prépare pas le binaire
à utiliser pour les photos. Le lanceur configure DLL/plugins Qt ; un CTest
lancé sans cet environnement ne prouve pas une régression du moteur.
Voir le [contrat de tests ciblés](../architecture/tests-cibles-et-build-windows.md).

Choisir les scènes et critères du [protocole du groupe](protocols/README.md),
avant mesure. Même RAW et état de développement dans les deux moteurs ;
pas de TIFF Lightroom à la place du RAW pour IPR.

## 2. Préparer et générer

Les sources sont les images à la racine de `tests/image/base`. Les références
et exports sont dans ses sous-dossiers existants. Le générateur ne supprime
ni n'écrase rien : retirer manuellement les anciens exports IPR à régénérer.
Ne pas créer une campagne datée ou un nouveau dossier par photo.

Depuis PowerShell à la racine :

```powershell
./scripts/generate-test-images.ps1 couleur
./scripts/generate-test-images.ps1 lumiere
# Tous les groupes, toutes les sources de base : attention au volume !
./scripts/generate-test-images.ps1 all
```

Seuls `all` ou un des dix groupes sont acceptés. Pas de liste d'images,
réglages/valeurs personnalisés ou dossier de sortie fourni. Les séries et
destinations sont dans le [guide du générateur](tools/generateur-images-tests.md).
Les TIFF contiennent les réglages/provenance ; aucun fichier auxiliaire
de génération n'est produit. Une erreur conserve les exports déjà écrits.

Vérifier la console DCP/LCP : Camera Neutral est préféré, pas garanti si absent.
La normalisation expérimentale est désactivée. Les sources et sidecars restent
inchangés ; le générateur ne restaure pas leurs retouches.

## 3. Préparer les références Lightroom

- Ouvrir les mêmes RAW, sélectionner réellement Camera Neutral compatible.
- Garder WB, corrections optiques et cadrage cohérents avec la série IPR.
- Mettre les traitements hors test à zéro, y compris bruit et netteté de sortie.
  Un reset Lightroom ne suffit pas toujours ; vérifier les valeurs.
- Désactiver automatismes, masques et IA pour comparer les curseurs classiques.
  Un DNG déjà débruité constitue une autre expérience, pas un neutre RAW.
- Exporter TIFF 16 bits, SDR, sRGB ICC embarqué, natif, non compressé, avec XMP.
- Mettre les variantes dans les mêmes dossiers de fonctionnalités que IPR ;
  conserver les vrais neutres sous `base/neutre`.

Pour température/nuance, préparer les supports manuels **5200 K / nuance 0**.
Le neutre LR As Shot peut indiquer 5200/+5 : ce n'est pas une erreur et ce
n'est pas le support manuel. Pour rayon/masquage, quantité de netteté 50 ;
pour grading teinte, saturation 25 ; respecter les supports du générateur.

## 4. Lancer les mesures

Python 3 avec NumPy/Pillow, depuis la racine, sous Windows ou WSL :

```bash
python tests/analyse_groupes.py --list
python tests/analyse_couleur.py --help
python tests/analyse_couleur.py --base tests/image/base --strict --data-version 1
python tests/analyse_lumiere.py --base tests/image/base --feature shadows --data-version 2
```

Sous WSL remplacer `python` par `python3` si nécessaire. Le code lit les
déclarations IPR et XMP LR ; il ne devine pas un état LR d'après un suffixe.
Une association ambiguë ou une ancienne image IPR non déclarée exige une
correction ou un mapping explicite selon le [guide des analyseurs](tools/analyseurs-groupes.md).
Les contrôles LR sans audit automatique demandent également ce mapping.

Les sorties sont des JSON versionnés/horodatés immuables sous
`docs/validation/raw`. Codes : 0 = calcul terminé ; 2 en mode strict =
diagnostics/exclusions conservés ; 1 = erreur empêchant la publication.
**0 ne signifie pas que le réglage est validé.**

## 5. Quelles données regarder ?

Commencer par `coverage`, puis `records[].status`, `issues`, les empreintes,
profils, référence/support et réglages. Un diagnostic n'entre pas dans une
conclusion de conformité. Vérifier les états manquants et la couverture réelle.

| Groupe | Données utiles | Contrôle visuel complémentaire |
| --- | --- | --- |
| Lumières | Y linéaire, deltas/ratios, quantiles, noirs/clips, monotonicité | Ombres, transitions, récupération HL et bruit amplifié |
| Couleur | `delta_log_R_B`, `delta_log_magenta_green`, chroma faible/forte, clipping | Peau, neutres, couleurs vives, extrêmes WB |
| Mélangeur | Delta teinte circulaire, saturation, ratio Y dans les huit bandes, nombre de pixels | Débordement sur bandes voisines, gris, continuité |
| Courbes/grading | Classes de Y, points/supports, teinte/chroma | Transitions et absence de ruptures |
| Bruit/netteté/effets | ROI natives, Haar/MAD, gradients, résidus, ratios de détail | Crops à 100 %, textures, halos, bavures chromatiques |
| RAW/optique | Options réellement appliquées, provenance, dimensions | Géométrie, franges et distorsion ; recalage spécialisé si nécessaire |

Comparer d'abord variante/référence **du même moteur**, puis les réponses
IPR et LR à états/supports compatibles. Pas de MAE inter-moteurs si cadrage
ou géométrie diffèrent. Les ratios indéfinis sont `null`, pas zéro. Une moyenne
globale correcte peut masquer un défaut local ; respecter les seuils définis
avant mesure, ou déclarer le seuil encore à fixer.

## 6. Recette IHM et conclusion

Tester champ ↔ slider, bornes, resets, passage A→B→A, fermeture/restauration
`.ipr`, aperçu/export et comparaison origine/actuel. Le zoom est un aperçu :
vérifier le détail sur l'export natif, pas seulement le proxy.

Consigner la campagne dans le rapport unique du groupe en liant son JSON.
Distinguer fonctionnement, qualité visuelle, proximité LR et fidélité physique
(cette dernière demande une référence mesurée). Ne pas extrapoler le verdict
à d'autres boîtiers/objectifs ou profils.

Après analyse, **l'utilisateur supprime manuellement les images**. Préserver
les RAW et mémoires JSON. CSV/courbes sont dérivés ; les JSON font autorité.
Une empreinte n'est pas une sauvegarde : sans pixels, une nouvelle mesure
nécessite de régénérer les exports.
