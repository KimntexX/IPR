# Correctif du débruitage CPU — consolidation du 5 octobre 2026

**État :** implémentation et régressions automatisées terminées ;
[revalidation photographique v2](analyse-bruit-v2-2026-10-05.md) désormais disponible.
Ce document conserve les résultats de l'implémentation ; les chiffres des
TIFF réexportés et la portée de leur validation sont consignés dans la campagne v2.

Référence avant correction : [analyse du 4 octobre](analyse-bruit-2026-10-04.md).
Équations et constantes courantes : [architecture](../../architecture/denoising-sharpening.md).
Réexport : [protocole v2](../protocols/protocole-bruit-v2.md).

## Modifications

- Seuil proportionnel au curseur, sans terme d'activation 0,35*sigma.
- Estimation robuste sur blocs Haar adjacents, sans plancher absolu 0,00025
  pour le bruit ; exclusions des blocs exactement constants.
- Masque Sobel normalisé calculé sur un guide préfiltré ; les guides Co/Cg
  protègent aussi les frontières de couleur sans différence de luminance.
- Mélange progressif avec l'original pour limiter les plateaux aux valeurs
  intermédiaires. Les courbes 1/5/10 restent volontairement très douces.
- Passage à Y Rec.709 avec Co/Cg, conservation de moyenne par rééquilibrage
  des changements, puis compression vers le gris en cas de canaux négatifs.
  Pas d'offset global ajouté aux noirs, pas de clamp supérieur HDR.

Les coefficients maximaux 2,55/3,15 reprennent les totaux historiques des
seuils à 100, mais l'estimateur, le guide et le mélange ont changé : cela
ne signifie pas que les anciens rendus non nuls sont conservés.
La netteté garde son estimateur et ses paramètres précédents ; le format
sidecar reste v4, les valeurs enregistrées sont conservées.

## Tests et résultat du build

Nouvelle cible portable CMake `detailProcessingTests` ; source
[`detail_processing_test.cpp`](../../../tests/detail_processing_test.cpp).
Les assertions vérifient zéro bit-exact, faible/fort bruit, progressivité
1/5/10/25/50/75/100, stabilité lumineuse sombre, préservation des noirs,
bord lumineux et frontière chromatique isoluminante, constante HDR,
entrées vides et mauvais type. Le gain de netteté zéro reste inchangé.

| Mesure synthétique | Résultat Windows Release |
| --- | --- |
| Proxy σY restant à 1 sur un aplat peu bruité | ~0,9998 |
| Proxy σY à 25/50/75/100 sur cet aplat | 0,831 / 0,523 / 0,253 / 0,009 |
| Proxy σC à 100 sur un aplat couleur | ~0,0091 |
| Dérive relative Y moyenne sur l'aplat sombre, bruit luminance 100 | ~−2,7×10⁻⁹ |
| Contraste du bord lumineux restant | 99,34 % |
| Contraste du bord chromatique isoluminant restant | 98,69 % |

[Sortie brute et empreintes](../raw/regressions-bruit-2026-10-05.txt).
Les faibles proxies à 100 décrivent des **aplats synthétiques**, pas une
qualité photographique garantie ni le bruit capteur restant. Ce réglage
fort doit notamment être contrôlé pour les textures fines et les halos.

Le premier build a détecté que la fixture d'export était un dégradé/damier
propre, pas une image bruitée. Le nouveau MAD ne devait pas être forcé à
confondre ce motif avec du bruit : la fixture contient désormais un bruit
luminance/couleur déterministe, en gardant ses motifs. L'assertion que
chaque effet change l'export n'a pas été supprimée ou affaiblie.

Build Windows MSVC x64 Release réussi, **12 CTest et 36 tests Python**
(13 comparateur partagé, 15 analyseur bruit, 8 sélecteur) réussis. Checkpoint
de validation enregistré. Binaire : `build-windows-msvc/Release/appIPR.exe`,
SHA-256 `28e3779fcc2aee85751ca48d7c78a04cefd8bf0e6fdf3bf22328b71405b00b46`.
Les avertissements Qt/Vulkan déjà présents ne bloquent pas la compilation.
Ce build ne fournit pas un nouveau packaging autonome.

Le build CMake ciblé Linux/WSL et les trois CTest `detailProcessingTests`,
`colorPipelineTests`, `imageExportTests` réussissent également. Cela confirme
le chemin CPU sur les deux configurations disponibles, pas sur macOS ni
sur une installation Windows vierge.

La règle ciblée pour `DetailProcessing.*` sélectionne les tests détail,
pipeline et export, plus l'analyseur Python bruit. Le seul fichier de test
détail ne sélectionne que sa cible. La modification CMake/cartographie
étant transversale, une suite complète a été utilisée ici.

## Limites et suite

- Moyenne globale préservée ne signifie pas moyenne locale invariante :
  remesurer les régions sombres de 0112, les carnations et les aplats.
- La compression en RGB non négatif peut réduire la saturation de certains
  pixels. Surveiller les couleurs fortes et les frontières à 100 %.
- Les unités restent des pixels natifs. Le proxy et la pleine résolution
  peuvent donc répondre différemment : recette aperçu/export nécessaire.
- Le MAD Haar ignore les blocs exactement constants ; ce n'est pas un
  modèle Poisson-Gaussien spatial, ni un correcteur de pixels chauds.
- Aucune IA, nouvelle dépendance, modification GPU ou promesse de performance
  à ISO élevé ajoutée. La latence et le pic mémoire doivent encore être mesurés.
- Aucun RAW, TIFF, sidecar ou INI utilisateur modifié ou archivé par cette action.

La campagne v2 a conservé les cinq RAW et les références Lightroom, puis
ajouté sept scènes dont deux ISO 3 200 et une ISO 8 000. Progressivité et
amélioration du biais sombre sont confirmées, mais pas la qualité globale.
La prochaine priorité est la calibration du bruit couleur résiduel à haut ISO ;
la netteté et son couplage restent une campagne distincte. Voir
[les conclusions et données photographiques](analyse-bruit-v2-2026-10-05.md).
