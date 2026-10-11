# Réduction de bruit — rapport consolidé

Consolidation du 8 octobre 2026. Les campagnes restent séparées : un résultat historique ne valide pas une révision ultérieure. La première campagne ci-dessous est la plus récente.

État actuel : [matrice de validation](../matrice-validation.md). Méthodes communes : [protocoles](../protocols/README.md).

<a id="campagne-1"></a>

## Campagne : analyse-bruit-v2-2026-10-05.md

## Réduction de bruit v2 — revalidation et corpus étendu

**Date :** 5 octobre 2026. **Décision :** fonctionnement et progressivité
confirmés sur le corpus ; amélioration luminance et stabilité sombre confirmées.
Qualité photographique globale, calibration couleur à haut ISO, parité Lightroom
et couplage à la netteté **non validés**.

### 1. Conclusion

Le correctif CPU produit le résultat recherché sur deux points importants :
les curseurs ne donnent plus les mêmes plateaux aux quatre niveaux standards,
et le relèvement indésirable des tons sombres est nettement réduit.
Les **24 séries IPR** (douze images × luminance/couleur) ont un proxy de bruit
médian strictement décroissant entre **25, 50, 75 et 100**.

À luminance 100, le proxy σY restant sur 0112 passe de **0,830 à 0,445**,
avec une dérive de Y médiane des aplats passant de **+2,674 % à −0,289 %**.
Sur 0327 et 0624, la réduction augmente sans dégradation importante du
proxy de contours déjà mesuré. La réduction couleur préserve mieux les
contours de luminance des scènes à bruit élevé.

En revanche, à couleur 100 il reste environ **77 à 84 % du proxy chromatique**
sur les nouvelles scènes ISO 3200/8000 : c'est une réduction réelle, mais
encore modérée et visible comme insuffisante dans certains aplats sombres.
Sur 9890, le proxy chromatique à 100 remonte de 0,806 à 0,835 par rapport
à l'ancien moteur : **le correctif n'améliore pas tous les indicateurs**.
La meilleure séparation luminance/couleur et la protection de contours ont
un coût en puissance de lissage chromatique.

**Recommandation :** conserver ces corrections ; traiter ensuite le bruit
couleur résiduel dans une action ciblée, en protégeant la luminance et les
frontières colorées. Ne pas augmenter simplement tous les seuils ni utiliser
Lightroom comme une vérité terrain. Aucun nouveau correctif de rendu n'est
appliqué dans cette campagne d'analyse.

### 2. Corpus retenu et traçabilité

| Élément | Nombre / couverture |
| --- | --- |
| Scènes | 12 : cinq historiques, sept nouvelles |
| Neutres | 24 : un IPR et un Lightroom par scène |
| Variantes standards | 136 : 96 IPR et 40 Lightroom |
| Activation luminance 1/5/10 | 9 IPR : 0043, 9890, 9421 |
| Total de variantes mesurées | **145**, hors neutres |
| États Lightroom absents | 56 : huit par nouvelle scène |
| Autres anomalies d'audit des fichiers retenus | 0 |

Les deux identifiants annoncés 1050/9920 ont été **confirmés par l'utilisateur
comme étant 9950/1020**. Aucune image n'est renommée ni substituée silencieusement.
Les anciennes variantes restent en place ; seuls les mappings explicites
ci-dessous sont utilisés, sans écrasement des photographies.

#### Associations de fichiers

- Anciennes scènes 0043/0112/0327/0624/9890 : nouveaux IPR `-5/-6/-7/-8`
  dans chaque série = 25/50/75/100 ; anciens IPR sans suffixe et `-2/-3/-4`
  exclus de la mesure v2 mais conservés.
- Lightroom historique : sans suffixe, `-2/-3/-4`, sauf luminance 9890 où
  **`-5/-6/-7/-8`** sont les réexports à netteté zéro audités précédemment.
- Neutre IPR 0112 : `neutre/6V1A0112-IPR-5.tiff`. Les dix neutres historiques
  sont réutilisés, pas présentés comme dix nouveaux exports à zéro.
- Sept nouvelles scènes : sans suffixe, `-2/-3/-4` = 25/50/75/100.
  Neutres dans `tests/image/base/neutre`, y compris 9734 sans suffixe,
  pas son ancien fichier EV0 `-3`.
- Faible activation : `ID-IPR-L001.tiff`, `-L005.tiff`, `-L010.tiff`
  dans `bruit/luminance`, associés explicitement par `--extra-state`.

Les fichiers sélectionnés sont TIFF natifs **sRGB, 16 bits, orientation 1**.
Les géométries sont identiques au sein de chaque moteur. Le TIFF IPR n'a pas
de métadonnées de réglages embarquées permettant de certifier ses curseurs
ou son build : les états viennent du protocole déclaré par l'utilisateur.
Les contrôles Lightroom sont vérifiés dans le XMP : bruit opposé et netteté
à zéro, autres réglages inchangés par rapport au neutre de la même scène,
aucun indicateur d'IA actif détecté. Cette absence ne prouve pas qu'aucune
amélioration IA n'a été utilisée en amont.

#### Scènes supplémentaires

ISO et exposition lus dans l'EXIF des **neutres Lightroom** ; les TIFF IPR
ne portent pas ces informations. Les noms de sujets viennent de la lecture
des aperçus, pas d'une classification automatique.

| Image | ISO | Pose / ouverture | Intérêt du sujet |
| --- | ---: | --- | --- |
| 6V1A9950 | 100 | 1/2000 s, f/2,8 | Ciel, végétation et ombres |
| 6V1A0106 | 100 | 1/1600 s, f/5,6 | Ciel, pont, eau et flou de premier plan |
| 6V1A1020 | 3200 | 1/2500 s, f/5,6 | Grimpeur, texture du rocher, contre-jour |
| 6V1A9734 | 8000 | 1/4000 s, f/2,8 | Peau, cheveux, textile, mur et vitres |
| 6V1A9421 | 3200 | 1/1000 s, f/2,8 | Mur peint, aplats colorés et motifs de textile |
| 6V1A9377 | 1250 | 1/800 s, f/2,8 | Ombres intérieures, volumes et surfaces peintes |
| 6V1A8988 | 400 | 1/200 s, f/4,5 | Intérieur sombre, plafond, mobilier et contre-jour |

Les scènes initiales couvrent ISO 100 (0043/0327), 400 (0624),
25600 (9890) et 64000 (0112). **ISO 6400 n'est pas indispensable** ici :
8000 couvre déjà un cas intermédiaire fortement bruité. En revanche, ce
corpus n'est pas une échelle ISO de la même scène sous lumière constante :
les écarts entre images ne peuvent pas être attribués à l'ISO seul.

### 3. Méthode et limites

Chaque variante est comparée à **son propre neutre**, par régions contiguës
de 256 × 256 pixels natifs. Six régions peu structurées et quatre régions
de détail sont retenues automatiquement sur une grille de candidats.
Ces catégories sont des heuristiques, pas une annotation de terrain.
À ISO 64000, une région « détail » peut mesurer principalement du bruit.

Après décodage de la courbe sRGB, on utilise :

```text
Y = 0,2126 R + 0,7152 G + 0,0722 B
Co = R − B ; Cg = G − (R+B)/2
HH = (p00 − p01 − p10 + p11)/2
σ = médiane(|HH − médiane(HH)|)/0,67448975
σC = sqrt((σCo² + σCg²)/2)
rY = σY(variante)/σY(neutre) ; rC = σC(variante)/σC(neutre)
δY = 100 × (moyenne_Y(variante)/moyenne_Y(neutre) − 1)
```

Les tableaux indiquent les **médianes des ratios des six aplats**, pas un
ratio de variance global ni un pourcentage de pixels bruités. Plus le ratio
est bas, moins le proxy de haute fréquence demeure ; cela ne distingue pas
parfaitement le bruit de la vraie texture. Les gradients sont mesurés après
deux filtrages binomiaux, sur les pixels forts du neutre ; leur ratio ne
garantit ni l'absence de halo ni la conservation de tous les détails fins.

Les TIFF complets ne sont pas convertis en 8 bits pour les statistiques.
Les PNG d'extraits sont uniquement des aides visuelles 8 bits, à un pixel
source par pixel de l'extrait. Pas de MAE/PSNR entre Lightroom et IPR :
géométrie et rendu de base différents, régions sélectionnées indépendamment.
La MAE vers le propre neutre décrit une modification, pas une erreur de
reconstruction faute de photographie propre de référence.

Les dix SHA-256 des neutres historiques sont identiques au JSON du 4 octobre,
et les régions avant/après sont exactement les mêmes dans les vingt groupes
historiques. L'analyseur contrôle taille/date des fichiers avant/après lecture
et à la fin de chaque passe. Les JSON consignent leurs mappings et son hash.

### 4. Avant/après sur les cinq scènes historiques

Valeurs à **100** seulement ; les autres niveaux sont dans les JSON.

| Image | rY luminance ancien → v2 | rC couleur ancien → v2 | δY luminance ancien → v2 | Gradient détail luminance ancien → v2 |
| --- | --- | --- | --- | --- |
| 0043 | 0,746 → 0,702 | 0,447 → 0,448 | +0,019 % → −0,048 % | 0,998 → 0,999 |
| 0112 | 0,830 → 0,445 | 0,660 → 0,661 | +2,674 % → −0,289 % | 0,833 → 0,733 |
| 0327 | 0,808 → 0,499 | 0,677 → 0,716 | +0,002 % → −0,020 % | 0,995 → 0,996 |
| 0624 | 0,832 → 0,566 | 0,802 → 0,806 | +0,004 % → −0,013 % | 0,987 → 0,987 |
| 9890 | 0,867 → 0,736 | 0,806 → 0,835 | +0,028 % → −0,013 % | 0,868 → 0,718 |

Sur 0112/9890, la baisse du gradient luminance est également sensible :
elle accompagne le lissage mais **ne doit pas être décrite comme 27/28 %
de détails photographiques détruits**. Les zones contiennent beaucoup de
grain ; une expertise de vraie texture reste nécessaire. Pour couleur seule,
le ratio de gradient sur 0112 passe de **0,975 à 1,000**, et sur 9890 de
**0,976 à 1,000**, résultat cohérent avec la séparation Rec.709 corrigée.

### 5. Progressions IPR sur l'ensemble du corpus

Chaque cellule contient le ratio aux niveaux **25 / 50 / 75 / 100**.
Le zéro de référence vaut 1 ; pas d'égalité sur les quatre médianes standards.

| Image | rY, luminance seule | rC, couleur seule |
| --- | --- | --- |
| 0043 | 0,876 / 0,874 / 0,870 / 0,702 | 1,000 / 0,996 / 0,722 / 0,448 |
| 0112 | 0,958 / 0,842 / 0,663 / 0,445 | 0,971 / 0,884 / 0,779 / 0,661 |
| 0327 | 0,955 / 0,845 / 0,667 / 0,499 | 0,998 / 0,993 / 0,982 / 0,716 |
| 0624 | 0,960 / 0,861 / 0,715 / 0,566 | 0,998 / 0,991 / 0,830 / 0,806 |
| 9890 | 0,978 / 0,918 / 0,824 / 0,736 | 0,988 / 0,953 / 0,895 / 0,835 |
| 9950 | 0,985 / 0,939 / 0,887 / 0,838 | 0,998 / 0,992 / 0,908 / 0,851 |
| 0106 | 0,934 / 0,748 / 0,508 / 0,268 | 0,978 / 0,876 / 0,783 / 0,723 |
| 1020 | 0,959 / 0,848 / 0,716 / 0,594 | 0,979 / 0,922 / 0,844 / 0,772 |
| 9734 | 0,980 / 0,921 / 0,835 / 0,747 | 0,975 / 0,941 / 0,899 / 0,836 |
| 9421 | 0,971 / 0,901 / 0,799 / 0,711 | 0,996 / 0,945 / 0,863 / 0,793 |
| 9377 | 0,936 / 0,771 / 0,591 / 0,482 | 0,997 / 0,930 / 0,817 / 0,788 |
| 8988 | 0,931 / 0,745 / 0,494 / 0,238 | 0,997 / 0,944 / 0,763 / 0,706 |

Un arrondi à 1,000 ne signifie pas fichier identique : toutes les variantes
diffèrent du neutre dans au moins une région échantillonnée. La réponse
reste nettement non linéaire, notamment couleur 0327/0624 et faible bruit
0043. Progressivité de la médiane n'implique pas monotonicité de chaque
pixel, de chaque région ou perceptuelle à chaque cran du curseur.

#### Stabilité lumineuse et nouveaux sujets

Sur tous les niveaux IPR retenus, le maximum absolu de la **dérive médiane
des aplats** est **0,289 %** (0112, luminance 100). Cette borne n'est pas
une borne pixel : les moyennes des régions individuelles, aplats et détails
confondus, vont approximativement de **−0,479 % à +0,829 %**.
Pour couleur seule, le maximum absolu des dérives médianes est environ
**0,033 %** (9377, niveau 50). Des ratios σY peuvent pourtant bouger sur les
zones très peu bruitées : MAD, quantification et faible dénominateur rendent
ce proxy plus sensible qu'une moyenne de luminance.

À luminance 100, gradients de détail restants : 1020 **0,963**, 9734 **0,949**,
9421 **0,989**. Couleur seule : environ **1,000** sur ces trois scènes.
Sur les extraits examinés, rocher, pelage, coutures et motif du tee-shirt
restent lisibles. Sur 9734, les cheveux et la peau sont lissés à 100 mais
du bruit coloré reste présent ; pas de halo large évident dans ces seuls
extraits. Cela ne vaut pas inspection exhaustive des douze images ni
validation de tous les bords colorés.

Extraits natifs reproductibles, colonnes **0/50/100**, trois premières lignes
luminance et trois dernières couleur :
[ciel 0112](../raw/extraits-bruit-v2-6V1A0112-2026-10-05.png),
[pelage/sangle 0624](../raw/extraits-bruit-v2-6V1A0624-2026-10-05.png),
[mur/ombres 9890](../raw/extraits-bruit-v2-6V1A9890-2026-10-05.png),
[rocher 1020](../raw/extraits-bruit-v2-6V1A1020-2026-10-05.png),
[mur/fenêtre 9734](../raw/extraits-bruit-v2-6V1A9734-2026-10-05.png),
[textile 9421](../raw/extraits-bruit-v2-6V1A9421-2026-10-05.png),
[peau/cheveux/textile 9734](../raw/extraits-bruit-v2-6V1A9734-carnation-2026-10-05.png).
Le dernier utilise des coordonnées explicites pour l'examen visuel,
distinctes des régions statistiques ; ses observations ne sont pas
introduites comme de nouvelles médianes dans les tableaux.

### 6. Activation à luminance 1 / 5 / 10

| Image | rY à 1 / 5 / 10 | δY médiane à 10 |
| --- | --- | --- |
| 0043 | 1,00000 / 1,00000 / 1,00000 | −0,0007 % |
| 9890 | 1,00000 / 0,99991 / 0,99696 | −0,0021 % |
| 9421 | 0,99995 / 0,99883 / 0,99502 | −0,0003 % |

Les neuf états sont différents du neutre sur les régions mesurées.
La réponse est faible près de zéro, comme attendu du mélange progressif.
Sur 0043, la médiane MAD inchangée ne démontre **pas** que le moteur est
inactif : cette photo très peu bruitée et quantifiée est peu discriminante.
Les données confirment une activation sans le saut massif précédent ;
elles ne valident pas une linéarité perceptuelle ni tous les crans 0–100.

### 7. Comparaison Lightroom : ce qu'on peut conclure

Les quarante références standards des cinq scènes initiales sont réutilisées
et redonnent les mesures précédentes. À luminance 100, leurs rY sont
0043 **0,461**, 0112 **0,215**, 0327 **0,238**, 0624 **0,167**, 9890 **0,192**.
À couleur 100, rC : **0,457 / 0,043 / 0,331 / 0,169 / 0,183** respectivement.

Le filtrage Lightroom mesuré est donc souvent plus marqué, surtout pour
la couleur ; **cela ne suffit pas à classer la qualité des logiciels**.
Les rendus/ROI diffèrent et, sur 0112, les aplats Lightroom de la série
luminance s'assombrissent fortement par rapport à leur propre neutre
(jusqu'à environ −61 % dans l'ancienne mesure) : une partie de la baisse
de σ est liée à la luminosité. Ne pas compenser IPR par une exposition ou
forcer son rY à rejoindre ce chiffre.

Les sept nouvelles scènes disposent de neutres Lightroom, mais **pas de
variantes Lightroom de bruit**. Les 56 absences sont un manque de couverture
comparative, pas un rejet des 56 variantes IPR correspondantes. Pour une
comparaison étendue utile, commencer seulement par **1020/9421 (ISO 3200)
et 9734 (ISO 8000)**, à 50/100 de chaque composante, netteté zéro, autre
composante zéro. Pas besoin de demander tous les exports absents maintenant.

### 8. Suite proposée et portée de validation

1. **Accepter le fonctionnement CPU actuel** : activation, progressivité
   standard et diminution du biais sombre confirmées. Ne pas fermer la
   recette de qualité globale ni celle de l'aperçu/export et des sidecars.
2. **Priorité suivante : bruit chromatique à haut ISO.** Instrumenter les
   estimations σ par canal, la carte de protection et les résidus par échelle
   sur 9421/9734/9890. Le code utilise un σ global et deux petites échelles
   gaussiennes ; un bruit local dépendant du signal et des taches plus larges
   peuvent être mal traités. C'est une hypothèse à vérifier, pas une cause
   unique démontrée par les TIFF. Évaluer seuils locaux robustes et une échelle
   chromatique supplémentaire, avec non-régression des bords/peau/saturation.
3. **Préserver Y et les noirs.** Garder les tests de reconstruction et la
   mesure δY ; ne pas revenir à un clamp RGB indépendant qui relève le sombre.
4. **Tester le couplage luminance/couleur**, puis la netteté gain/rayon/masquage.
   Les séries actuelles isolent les composantes et utilisent gain de netteté
   zéro : elles ne valident pas simultanément les deux débruitages activés.
5. **Recette utilisateur** : comparaison à 100 %, aperçu stabilisé/export,
   restauration `.ipr`, peau, couleurs saturées et halos ; durée/pic mémoire
   à mesurer séparément. Une acquisition à ISO 6400 reste facultative.

### 9. Reproduction, version et tests

Moteur analysé par lecture du code : `src/core/DetailProcessing.cpp`, SHA-256
`a3af2978884728da45edf2903f35959ba926bfbab7f629b9728a4d85bf0f0b23`,
inchangé depuis le correctif. Le build précédent est détaillé dans le
[rapport du correctif](correctif-bruit-2026-10-05.md). Les TIFF fournis ne
portent pas de hash de build : leur attribution au correctif est déclarative.
Aucune recompilation ni modification du moteur n'a lieu pour cette analyse.

L'analyseur prend maintenant des états supplémentaires explicites avec
`--extra-state IMAGE_ID COMPONENT ENGINE VALUE PATH`, en complément des
quatre états standards. Deux tests ajoutés vérifient leur association et
le rejet d'un doublon d'état standard. Suites exécutées sous WSL :
**17 tests bruit, 13 comparateur partagé, 8 sélection de tests = 38 réussis**.
[Commandes et relevé des tests](../raw/tests-analyse-bruit-v2-2026-10-05.txt).
Ce résultat ne remplace pas le checkpoint de build Windows précédent.

Depuis la racine du dépôt, sous WSL/Bash avec NumPy/Pillow :

```bash
## Choisir de nouveaux noms de JSON pour toute réexécution.
bash docs/validation/raw/analyse-bruit-v2-2026-10-05.sh old \
  docs/validation/raw/mesure-bruit-v2-initial-2026-10-05-v2.json --analyse
bash docs/validation/raw/analyse-bruit-v2-2026-10-05.sh new \
  docs/validation/raw/mesure-bruit-v2-etendu-2026-10-05-v2.json --analyse
python3 -m unittest discover -s tests -p test_analyse_noise.py
```

Les mesures existantes ne sont jamais écrasées. Les scripts d'extraits et
de synthèse refusent eux aussi d'écraser leur sortie ; leurs noms v1 doivent
être versionnés pour produire une nouvelle synthèse. La synthèse conserve
les SHA-256 des JSON sources. Les photographies ne sont ni modifiées,
ni déplacées, ni archivées dans cette action.

#### Données et références internes

- [Inventaire audité](../raw/inventaire-bruit-v2-2026-10-05-v1.json).
- [Mesures sur les cinq scènes historiques](../raw/mesure-bruit-v2-initial-2026-10-05-v1.json).
- [Mesures sur les sept nouvelles scènes](../raw/mesure-bruit-v2-etendu-2026-10-05-v1.json).
- [Synthèse vérifiable](../raw/synthese-bruit-v2-2026-10-05-v1.json) et
  [script de consolidation](../raw/consolider-bruit-v2-2026-10-05.py).
- [EXIF des nouvelles scènes](../raw/exif-bruit-v2-2026-10-05-v1.json).
- [Mappings et commandes de campagne](../raw/analyse-bruit-v2-2026-10-05.sh),
  [script d'extraits](../raw/extraits-bruit-v2-2026-10-05.py),
  [analyseur](../../../tests/analyse_noise.py).
- [Mesures avant correction](../raw/mesure-bruit-2026-10-04-v4.json),
  [analyse historique](analyse-bruit-2026-10-04.md),
  [protocole v2](../protocols/protocole-bruit-v2.md),
  [équations et constantes du moteur](../../architecture/denoising-sharpening.md).

Il s'agit d'une campagne expérimentale locale, pas d'une nouvelle recherche
bibliographique ni d'un benchmark des versions commerciales actuelles.

<a id="campagne-2"></a>

## Campagne : correctif-bruit-2026-10-05.md

## Correctif du débruitage CPU — consolidation du 5 octobre 2026

**État :** implémentation et régressions automatisées terminées ;
[revalidation photographique v2](analyse-bruit-v2-2026-10-05.md) désormais disponible.
Ce document conserve les résultats de l'implémentation ; les chiffres des
TIFF réexportés et la portée de leur validation sont consignés dans la campagne v2.

Référence avant correction : [analyse du 4 octobre](analyse-bruit-2026-10-04.md).
Équations et constantes courantes : [architecture](../../architecture/denoising-sharpening.md).
Réexport : [protocole v2](../protocols/protocole-bruit-v2.md).

### Modifications

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

### Tests et résultat du build

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

### Limites et suite

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

<a id="campagne-3"></a>

## Campagne : analyse-bruit-2026-10-04.md

## Réduction de bruit — analyse photographique et diagnostic du moteur

**Date :** 4 octobre 2026. **Décision :** activité des réglages confirmée sur les
séries exploitables ; calibration et recette complète **non validées**.
Le moteur n'est pas modifié par cette analyse. Aucun RAW, TIFF, sidecar ou INI
utilisateur n'a été déplacé, corrigé ou écrasé.

**Suite au 5 octobre :** les [correctifs CPU](correctif-bruit-2026-10-05.md)
sont maintenant appliqués et testés automatiquement. Les mesures ci-dessous
restent celles **avant correction** ; elles ne valident pas le nouveau rendu.
La [campagne v2](../protocols/protocole-bruit-v2.md) doit encore être exécutée.

[Protocole](../protocols/protocole-validation-bruit-2026-10-04.md) ·
[Architecture](../../architecture/denoising-sharpening.md).

### 1. Conclusion opérationnelle

Les deux commandes IPR ont un effet mesurable. Le réglage de luminance conserve
bien les contours examinés à ISO faible/modéré, mais sa réponse peut plafonner
très tôt. À très haut ISO, la chrominance résiduelle reste importante : la
scène 9890 à ISO 25 600 conserve environ **81 % du proxy de bruit couleur**
de son neutre, au réglage 100. Les crops montrent encore des taches colorées.

Le diagnostic synthétique identifie trois pistes prioritaires dans le code :

1. rendre le seuillage continu à l'approche de zéro et revoir le plancher de bruit ;
2. empêcher le masque de contours de protéger le bruit comme un détail réel ;
3. contrôler la dérive lumineuse lors du seuillage et de la reconstruction RGB.

Ces recommandations ne signifient pas qu'il faut copier les valeurs Lightroom,
changer immédiatement d'algorithme ou intégrer une IA/GPU. Commencer par corriger
et mesurer la référence CPU actuelle. L'efficacité absolue, les halos, la
conservation de toutes les textures et la cohérence aperçu/export ne sont pas
encore validés. La netteté n'est pas testée dans cette campagne.

### 2. Corpus réellement reçu et audit

Quatre niveaux par composante : **25, 50, 75, 100**, pour luminance seule puis
couleur seule. Ce n'est pas la convention −50/−25/+25/+50 du mélangeur.

| Scène | ISO EXIF | Sujet observé | IPR | Lightroom retenu |
| --- | ---: | --- | --- | --- |
| 6V1A0043 | 100 | Fleur, fond flou | Deux séries avec neutre | Deux séries conformes |
| 6V1A0112 | 64 000 | Lune/ciel nocturne très bruité | Deux séries avec neutre explicitement associé | Deux séries conformes |
| 6V1A0327 | 100 | Escalade extérieure, rocher | Deux séries avec neutre | Deux séries conformes |
| 6V1A0624 | 400 | Chien noir, pelage et sangle | Deux séries avec neutre | Deux séries conformes après correction du dernier TIFF |
| 6V1A9890 | 25 600 | Escalade intérieure, mur et vêtement | Deux séries avec neutre | Deux séries conformes après mapping explicite des réexports |

Les métadonnées indiquent « Canon EOS R6 Mark III » ; ce libellé est transcrit
du corpus, pas vérifié comme identification indépendante du matériel.

Le dossier bruit contient **84 TIFF** : 40 IPR et 44 Lightroom. Quatre Lightroom
9890 sont d'anciens exports doublons à netteté 40 ; leurs réexports propres
portent les suffixes −5 à −8. La campagne retenue comporte donc **80 variantes
distinctes**, dix neutres disponibles et aucun état manquant.

Après sélection des bons réexports, **80 variantes** satisfont l'audit de
campagne : 40 IPR avec leur neutre déclaré et 40 Lightroom avec neutre conforme.
Les vingt séries attendues sont complètes. L'audit des fichiers n'est pas
une validation de la qualité photographique de l'algorithme.

#### Compléments reçus pendant l'analyse

- Neutre IPR de 0112 reçu sous `tests/image/base/neutre/6V1A0112-IPR-5.tiff` :
  référence opérateur explicitement associée avec `--neutral-file`.
- Dans `bruit/luminance`, les quatre IPR de 9890 reçus :
  `6V1A9890-IPR.tiff`, `6V1A9890-IPR-2.tiff`, `6V1A9890-IPR-3.tiff`,
  `6V1A9890-IPR-4.tiff`, aux valeurs 25/50/75/100.
- Lightroom `bruit/luminance/6V1A0624-4.tif` réexporté : luminance 100,
  **couleur remise à 0**, XMP vérifié. La version précédente activait couleur 25.

Les Lightroom 9890 −5/−6/−7/−8 indiquent bien luminance 25/50/75/100,
couleur 0, netteté 0. Ils sont explicitement associés aux quatre états,
sans renommer ni supprimer les fichiers de l'utilisateur.

#### Autres contrôles

Les TIFF retenus sont sRGB 16 bits SDR, orientation déjà appliquée.
Les dimensions IPR et Lightroom diffèrent d'un pixel par axe ; aucune
superposition interlogiciels n'est imposée. La géométrie est constante
à l'intérieur de chaque série mesurée.

Les paramètres secondaires Lightroom sont constants dans les états valides :
détail luminance 50, contraste 0 ; détail couleur 50 et lissage 50 lorsque
les contrôles couleur sont actifs. Les autres paramètres XMP sont comparés
au neutre. Aucun indicateur IA actif n'est détecté dans ces exports ; l'absence
de balise ne prouve pas l'absence d'un traitement amont. Aucune série IA
séparée n'a été fournie. Les valeurs IPR sont déclarées par l'opérateur :
leurs TIFF n'offrent pas une preuve XMP équivalente des réglages appliqués.

### 3. Méthode et portée des nombres

Lecture de **pixels natifs adjacents**, sans passage en 8 bits pour les mesures.
Chaque référence fournit six régions peu structurées et quatre régions de
détail de 256×256 pixels, coordonnées figées pour ses quatre variantes.
Les coordonnées et toutes les statistiques par région sont dans les JSON.

Après décodage sRGB, le proxy haute fréquence est le MAD du coefficient
diagonal Haar normalisé :

`HH = (p00 − p01 − p10 + p11)/2`,
`sigma = median(abs(HH − median(HH)))/0,67448975`.

Y est la luminance linéaire Rec.709 ; le proxy couleur combine Co et Cg
de YCoCg. Un ratio de 0,80 signifie 80 % du proxy initial restant,
**pas 80 % de bruit capteur ni 20 % de gain de qualité perceptuelle**.
Un proxy faible peut aussi provenir de l'effacement d'une texture,
d'un changement de corrélation ou d'un assombrissement.

Les tableaux sont des médianes sur six régions, sélectionnées séparément
par logiciel. Ils permettent des tendances internes, pas un classement
quantitatif sur des zones strictement équivalentes IPR/Lightroom. Ni MAE
interlogiciels, ni PSNR de reconstruction : aucune vérité sans bruit n'existe.
Le protocole précise les filtres, critères et limites.

### 4. Résultats IPR

Ratios au **neutre propre**, pour les dix séries IPR complètes.
Les valeurs sont arrondies à trois décimales ; un plateau arrondi n'implique
pas l'identité des pixels ou un blocage de l'interface.

| Scène et composante mesurée | 25 | 50 | 75 | 100 | Référence |
| --- | ---: | ---: | ---: | ---: | --- |
| 0043 — σY, luminance | 0,748 | 0,748 | 0,746 | 0,746 | 0 |
| 0043 — σC, couleur | 0,447 | 0,447 | 0,447 | 0,447 | 0 |
| 0327 — σY, luminance | 0,910 | 0,876 | 0,835 | 0,808 | 0 |
| 0327 — σC, couleur | 0,989 | 0,981 | 0,841 | 0,677 | 0 |
| 0624 — σY, luminance | 0,927 | 0,890 | 0,860 | 0,832 | 0 |
| 0624 — σC, couleur | 0,889 | 0,816 | 0,808 | 0,802 | 0 |
| 9890 — σY, luminance | 0,946 | 0,919 | 0,893 | 0,867 | 0 |
| 9890 — σC, couleur | 0,919 | 0,880 | 0,843 | 0,806 | 0 |
| 0112 — σY, luminance | 0,919 | 0,897 | 0,866 | 0,830 | 0 |
| 0112 — σC, couleur | 0,866 | 0,794 | 0,722 | 0,660 | 0 |

#### Interprétation

- **0043 :** la fleur a très peu de bruit. Une grande partie du changement
  arrive avant ou à 25, puis le proxy varie peu ; ce n'est pas une bonne scène
  pour calibrer seule l'extrémité forte du curseur.
- **0327/0624 :** progression de luminance mesurée. Les ratios de gradient
  de contour à 100 sont respectivement 0,995 et 0,987. Les crops du pelage
  0624 conservent visuellement les poils examinés, avec lissage modéré des
  aplats ; cela ne valide pas toutes les textures ni tous les halos.
- **9890 :** le bruit couleur baisse d'environ 8 % à 25 à 19 % à 100,
  et le proxy de luminance baisse d'environ 13 % à 100. Les crops à un pixel affiché par pixel natif montrent
  beaucoup de bruit résiduel, notamment dans le vêtement et sur le mur.
- **0112 :** le ciel permet d'étudier le bruit, pas de prétendre valider
  la texture par ses gradients. Avec le neutre reçu, le proxy de luminance
  baisse d'environ 17 % à 100 et le proxy couleur d'environ 34 %.
  La moyenne Y monte d'environ **2,67 %** dans les aplats de la série
  luminance à 100 par rapport au zéro, et de 0,38 % dans la série couleur.
  Cette dérive confirme la nécessité de contrôler les tons très sombres.

Sur les huit séries IPR hors 0112, les médianes de dérive Y sont très
faibles (moins de 0,03 % en valeur absolue). Cela ne garantit pas l'absence
de dérive locale ; les mesures par région sont conservées.

### 5. Lightroom classique : repère, pas vérité absolue

| Scène et composante mesurée | 25 | 50 | 75 | 100 |
| --- | ---: | ---: | ---: | ---: |
| 0043 — σY, luminance | 0,623 | 0,535 | 0,473 | 0,461 |
| 0043 — σC, couleur | 0,452 | 0,454 | 0,459 | 0,457 |
| 0112 — σY, luminance | 0,505 | 0,374 | 0,257 | 0,215 |
| 0112 — σC, couleur | 0,043 | 0,043 | 0,043 | 0,043 |
| 0327 — σY, luminance | 0,584 | 0,452 | 0,333 | 0,238 |
| 0327 — σC, couleur | 0,337 | 0,334 | 0,332 | 0,331 |
| 0624 — σY, luminance | 0,532 | 0,383 | 0,249 | 0,167 |
| 0624 — σC, couleur | 0,172 | 0,170 | 0,171 | 0,169 |
| 9890 — σY, luminance, réexports −5 à −8 | 0,560 | 0,414 | 0,282 | 0,192 |
| 9890 — σC, couleur | 0,183 | 0,184 | 0,183 | 0,183 |

Les tendances et les crops montrent un débruitage couleur Lightroom beaucoup
plus marqué sur 9890, dès 25. Ce constat porte sur ce corpus, ces rendus
et ces zones ; les valeurs numériques des curseurs ne sont pas des unités
physiques communes. La baisse forte des gradients peut enlever du bruit
autant que des détails : aucune perte de texture absolue n'est déduite
du seul ratio de gradient.

Attention à **0112** : la luminance moyenne des aplats baisse d'environ
39 à 61 % dans les variantes Lightroom de luminance, et d'environ 41 %
dans celles de couleur. Les contrôles XMP audités restent isolés, mais
l'assombrissement contribue à la baisse du proxy linéaire. Le chiffre de
0,215 ne doit donc pas être interprété seul comme une réduction de bruit
à luminosité égale. Il faut des crops et une analyse locale tenant compte
du niveau de signal avant une comparaison qualitative définitive.

Le réexport corrigé Lightroom 0624 luminance 100 donne un ratio de **0,167**.
L'ancienne version (~0,018) activait également couleur 25 : historique
conservé dans la mesure v2, mais remplacé et exclu des conclusions finales.

### 6. Diagnostic reproductible des constantes du moteur

Le code étudié est [`DetailProcessing.cpp`](../../../src/core/DetailProcessing.cpp),
SHA-256 `1af77486a369b6c265d93bae97c9c24f313a4b26f7917fe67d512e59165c3048`.
Les versions exactes des exécutables ayant produit les TIFF ne sont pas
encodées dans ces fichiers : ce hash identifie le **code audité**, pas une
preuve de provenance de chacun des exports.

[Source de la sonde](../raw/sonde-bruit-2026-10-04.cpp) et
[sortie brute](../raw/sonde-bruit-2026-10-04.txt). Compilation et exécution
WSL/OpenCV réussies, sans changement du moteur. Trois aplats synthétiques,
graine fixe 812, 512×512 pixels ; niveaux 1/5/10/25/50/75/100.

Cette sonde historique inclut le fichier moteur à la compilation. Pour reproduire
ses anciens chiffres il faut le code au hash ci-dessus, pas le moteur corrigé
actuel. La nouvelle référence synthétique est `detailProcessingTests`.

#### A. Seuil non nul dès l'activation

Le chemin à 0 ne filtre pas. Dès que la valeur est positive :

`lambda_fin = sigma * (0,35 + multiplicateur * valeur/100)`.

Le multiplicateur est 2,2 en luminance et 2,8 en couleur ; le seuil moyen
vaut 0,52 fois le seuil fin. La limite du seuil lorsque la valeur tend
vers zéro reste donc **0,35 sigma**, au lieu de zéro.

Le plancher `kNoiseFloor=0,00025` domine aussi une scène synthétique
à σ≈0,0001. À valeur 1, son proxy résiduel tombe déjà à **0,153** ;
à 5, à **0,031** ; à 25–100, à environ **0,009**. Cela démontre une
activation brutale et un plateau sur cet aplat de faible bruit.

**Recommandation :** seuil nul à force nulle, calibration séparée des
échelles et du plancher. Tester aussi une interpolation continue entre
l'image originale et filtrée ; ne pas choisir ses constantes uniquement
pour faire correspondre un seul TIFF Lightroom.

#### B. Le bruit est protégé comme un contour

Le Sobel qui pilote la protection travaille directement sur la luminance
bruitée. Sur un aplat sans texture à σ=0,005, **70,25 %** des pixels ont
un poids de protection au moins 0,5 et **43,94 %** au moins 0,99.
Ce sont des structures dues au bruit, pas des bords à conserver.

**Recommandation :** guide de structure préfiltré ou gradient normalisé
par le bruit attendu, puis comparaison sur véritables bords et textures.
Ne pas simplement supprimer toute protection pour augmenter la baisse
de σ : cela risquerait d'effacer les poils et motifs fins.

#### C. Relèvement possible des tons très sombres

La reconstruction YCoCg borne séparément les composantes RGB négatives à
zéro. Sur l'aplat sombre à bruit couleur de la sonde, la moyenne YCoCg
monte de **1,89 %** à luminance 100, contre **0,71 %** avant ce bornage.
Le seuillage est lui aussi contributeur : le clamp n'explique pas tout.

**Recommandation :** mesurer le biais sombre avant/après reconstruction,
étudier une contrainte de gamut préservant mieux la luminance, sans supprimer
aveuglément les protections de validité numérique. La sonde démontre un
mécanisme possible ; elle n'attribue pas à elle seule la totalité de la
dérive photographique de 0112.

#### D. Coût, IA et GPU

Cette campagne ne chronomètre pas le rendu et ne mesure aucune accélération
GPU. Corriger le masque, les seuils et la reconstruction ne nécessite pas
immédiatement une IA. Si la qualité reste insuffisante à haut ISO après
calibration, comparer une option classique plus élaborée sur les mêmes
neutres et ROI, avec licence, coût CPU et préservation des détails audités.
Une IA nécessite une campagne et un budget propres, pas une équivalence
supposée à partir d'une seule valeur de curseur Lightroom.

### 7. Traçabilité et reproduction

- [Inventaire v5](../raw/inventaire-bruit-2026-10-04-v5.json) : audit final
  des 80 variantes et dix neutres, mappings explicites ; **aucune anomalie**.
- [Mesure v4](../raw/mesure-bruit-2026-10-04-v4.json) : passe finale complète,
  **seule référence des résultats consolidés**.
- [Mesure v2](../raw/mesure-bruit-2026-10-04-v2.json) : 76 variantes mesurées,
  dont anciennes Lightroom 9890 de luminance à netteté 40 ; historique seulement.
- [Mesure 9890 v3](../raw/mesure-bruit-9890-2026-10-04-v3.json) : sélection
  explicite −5 à −8 avant réception des IPR luminance ; historique seulement.
- Les inventaires v1/v2/v3/v4 et mesure v1 restent des instantanés historiques
  pendant les remplacements. Ils ne définissent pas le corpus final ;
  notamment les anciennes étiquettes Lightroom 0327 couleur étaient erronées.

La mesure v4 emploie les métriques de l'analyseur version 2, dont le SHA-256 est enregistré
dans chaque JSON. Le lecteur conserve les vrais niveaux XMP, vérifie tailles
et dates avant/après lecture et à la fin de la campagne, et refuse d'écraser
un fichier de mesure existant. Les neutres disponibles ont leur SHA-256.

Reproduction complète avec les noms réellement reçus :

```bash
python3 tests/analyse_noise.py --analyse \
  --neutral-file 6V1A0112 IPR tests/image/base/neutre/6V1A0112-IPR-5.tiff \
  --variant-file 6V1A9890 luminance LR 1 tests/image/base/bruit/luminance/6V1A9890-5.tif \
  --variant-file 6V1A9890 luminance LR 2 tests/image/base/bruit/luminance/6V1A9890-6.tif \
  --variant-file 6V1A9890 luminance LR 3 tests/image/base/bruit/luminance/6V1A9890-7.tif \
  --variant-file 6V1A9890 luminance LR 4 tests/image/base/bruit/luminance/6V1A9890-8.tif \
  --output NOUVELLE-MESURE-BRUIT.json
```

Les aperçus des ROI et crops diagnostiques sont dans `build-wsl-libraw` :
images de consultation 8 bits uniquement, statistiques natives 16 bits.
Inspection à taille native effectuée pour le pelage 0624 et le bruit couleur
9890. Les autres scènes demandent encore une recette visuelle plus exhaustive.

**Tests ciblés :** 15 tests bruit, 13 tests du lecteur/comparateur partagé,
7 tests de sélection de build réussis. La modification du seul analyseur
bruit sélectionne sa seule suite Python, aucun CTest. Le changement de
cartographie `scripts/test-rules.json` reste transversal et entraînera une
suite complète au prochain build automatique ; aucun checkpoint n'a été
avancé par cette campagne. Aucune recompilation Windows n'était nécessaire
pour cette analyse sans changement de l'application.

### 8. Suite proposée

1. Ajouter des régressions CPU pour identité à 0, activation 1/5/10,
   progression, aplats, marches, fins motifs et biais sombre.
2. Appliquer les corrections de seuil/masque/reconstruction dans une action
   distincte ; conserver ce corpus comme référence de l'ancienne version.
3. Réexporter les séries corrigées et comparer aux mêmes neutres, puis valider
   à 100 % peau/pelage/feuillage, halos, frontières de couleur et aperçu/export.
4. Tester ensuite le couplage bruit + netteté, ainsi que restauration sidecar.

### Bibliographie

- D. L. Donoho et I. M. Johnstone, *Ideal spatial adaptation by wavelet shrinkage*,
  Biometrika 81(3), 1994, 425–455 : [DOI](https://doi.org/10.1093/biomet/81.3.425),
  [rapport Stanford](https://statistics.stanford.edu/technical-reports/ideal-spatial-adaptation-wavelet-shrinkage).
  Référence du seuillage ; la mesure Haar est un proxy, pas une implémentation
  complète de cet algorithme.
- [Adobe — high ISO et outils manuels/IA](https://blog.adobe.com/en/publish/2023/04/20/lowdown-using-high-iso-settings-in-photography)
  et [Enhance/Denoise](https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/enhance-details.html).
  Les hypothèses expérimentales sont détaillées dans le protocole.
- Sources d'implémentation comparées : bibliographie de
  [l'architecture bruit/netteté](../../architecture/denoising-sharpening.md).
  Cette passe mesure les exports fournis ; elle ne renouvelle pas un benchmark
  de RawTherapee, DxO, IA ou GPU.

