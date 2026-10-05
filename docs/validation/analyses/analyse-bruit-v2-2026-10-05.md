# Réduction de bruit v2 — revalidation et corpus étendu

**Date :** 5 octobre 2026. **Décision :** fonctionnement et progressivité
confirmés sur le corpus ; amélioration luminance et stabilité sombre confirmées.
Qualité photographique globale, calibration couleur à haut ISO, parité Lightroom
et couplage à la netteté **non validés**.

## 1. Conclusion

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

## 2. Corpus retenu et traçabilité

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

### Associations de fichiers

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

### Scènes supplémentaires

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

## 3. Méthode et limites

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

## 4. Avant/après sur les cinq scènes historiques

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

## 5. Progressions IPR sur l'ensemble du corpus

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

### Stabilité lumineuse et nouveaux sujets

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

## 6. Activation à luminance 1 / 5 / 10

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

## 7. Comparaison Lightroom : ce qu'on peut conclure

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

## 8. Suite proposée et portée de validation

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

## 9. Reproduction, version et tests

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
# Choisir de nouveaux noms de JSON pour toute réexécution.
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

### Données et références internes

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
