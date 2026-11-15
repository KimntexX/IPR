# Gestion des couleurs : travail, écran et export

**Directive du 15 octobre 2026 :** [ADR-0008](../decisions/ADR-0008-separation-travail-affichage-export.md).
Relecture du 22 octobre 2026 : master Rec.2020 intégré dans `dev` ; affichage ICC écran encore cible. Ce document ne remplace pas les résultats
du [rapport évolutif RAW/profils](../validation/analyses/analyse-base-colorimetrique.md).

## État réel et séparation cible

Aujourd'hui `WorkingImage` contient du RGBA flottant 32 bits en Rec.2020 linéaire D65.
Le DCP utilise ProPhoto pour ses tables et sa courbe, puis rejoint Rec.2020
linéaire. `toSrgb()` vérifie ce contrat, convertit les valeurs puis effectue le gamut mapping sRGB
avant encodage. Une partie de la réduction de gamut peut déjà intervenir
avant les derniers réglages ; ajouter un profil écran ne récupère pas ces pertes.
L'affichage ICC par écran n'est pas déclaré fonctionnel par cette directive.

**Précision issue de l'étude du 15 octobre :** une conversion vers des
coordonnées sRGB flottantes ne borne pas, à elle seule, le master RAW.
`RawDecoder` conserve des valeurs étendues et `toSrgb()` mappe la sortie
dérivée. Le bornage des imports entiers QImage, des tables/courbes DCP et
des différents opérateurs doit être distingué. L'[étude comparative, §43](../validation/analyses/analyse-base-colorimetrique.md#43-étude-préalable-prophoto-et-espaces-de-calcul-comparables--15-octobre-2026)
retient **Rec.2020 linéaire** comme alternative prioritaire à ProPhoto et
**ACEScg** comme piste secondaire ; au stade de cette étude initiale, aucune migration ni supériorité mesurée n'était actée. Le choix d'espace et la politique de bornage doivent être évalués
séparément. Le reste de ce document décrit l'option ProPhoto, pas son adoption.

Le [comparatif réel du §44](../validation/analyses/analyse-base-colorimetrique.md#44-comparatif-exécuté-srgb-étendu-prophoto-et-rec2020--15-octobre-2026)
est désormais disponible : sept scènes et 126 variantes. Les opérations
invariantes concordent ; ProPhoto et Rec.2020 contiennent tous les échantillons
de départ du corpus dans leur cube nominal. Les différences d'opérateurs RGB
ne prouvent pas une supériorité. L'utilisateur a ensuite retenu Rec.2020
linéaire D65 : [ADR-0009](../decisions/ADR-0009-espace-travail-rec2020-lineaire.md).
Le [plan d'intégration](integration-rec2020.md) distingue l'étude initiale du
port intégré dans `dev` ; les neutres sont rejoués dans le rapport §46.

```text
RAW → calibration/WB/DCP → image de travail + retouches
                                  ├→ transformation ICC écran → aperçu
                                  └→ transformation espace d'export → fichier
```

Le profil écran est une caractérisation de destination, pas un profil caméra.
Détection système par écran en mode ordinaire ; sélection manuelle avancée
à prévoir. Un écran mal caractérisé reste une limite : charger un fichier ICC
ne mesure ni ne calibre physiquement le moniteur. Le gamut seul ne décrit pas
toute la transformation (réponse tonale, blanc et caractérisation comptent aussi).

## Piste ProPhoto étudiée avant le choix Rec.2020

**Oui, techniquement ; cette option n'est pas retenue pour le master**, en conservant des valeurs
flottantes avec les primaires ProPhoto et le blanc D50, et une fonction de
transfert linéaire pour le calcul. Cette représentation n'est pas l'encodage
de fichier ROMM RGB standard, qui possède sa propre fonction de transfert.
Nommer explicitement primaires, blanc et transfert ; ne pas simplement
étiqueter les valeurs actuelles « ProPhoto ». Les valeurs étendues ne sont
pas bornées à `[-1,1]` ; leur conservation est une décision de calcul, pas
une propriété de tous les formats ProPhoto.

Intérêts attendus : conserver davantage de couleurs avant la sortie, éviter
des passages sRGB bornés prématurés, et partager un master indépendant de
l'écran entre affichages et exports. Cela ne reconstruit pas un signal perdu
au décodeur ou dans une table et ne garantit pas la fidélité colorimétrique.
Un espace large ne suffit pas non plus à rendre un traitement « scene-referred » :
la courbe de rendu DCP déjà appliquée reste une opération tonale.

**Non, pas comme destination physique universelle de l'affichage.** L'écran
ne devient pas ProPhoto ; l'aperçu dérivé doit toujours être converti vers
son profil réel. Sur un écran sRGB, les couleurs conservées en interne mais
non reproductibles doivent être adaptées pour cet aperçu, sans les supprimer
du master. L'export suit sa propre destination, indépendamment du moniteur.

## Audit requis avant migration

1. Définir le contrat `WorkingImage` : primaires, point blanc, transfert,
   valeurs étendues, alpha et version moteur. Importer TIFF/JPEG en convertissant
   leur profil réel, sans leur attribuer arbitrairement celui du RAW.
2. Recalculer les conversions et la luminance depuis l'espace choisi : les
   poids sRGB `0,2126/0,7152/0,0722` ne sont pas ceux de ProPhoto. Auditer gris,
   tons, courbes, mélangeur, grading, bruit et netteté.
3. Vérifier chaque passage HSV/Lab/Oklab, son blanc et son encodage. Les
   opérations perceptuelles peuvent nécessiter un autre espace intermédiaire ;
   « travail en ProPhoto » ne signifie pas « toutes les opérations en RGB ».
4. Séparer aperçu, export, histogrammes et épreuvage. Définir explicitement
   le domaine de chaque histogramme ; ne pas le rendre dépendant du moniteur
   à l'insu de l'utilisateur.
5. Comparer le rendu au témoin : neutralité, gradients, couleurs saturées,
   noirs/HL, interactions, restauration et sorties sRGB. Les tests d'invariants
   restent utiles ; les validations photographiques sensibles doivent être
   reprises sans modifier leurs anciennes preuves.

Le volume du tampon principal ne change pas du seul fait des primaires si
son format reste `CV_32FC4`. En revanche, tampons temporaires et caches peuvent
augmenter la mémoire. Préparer matrices/transformations ICC hors boucle pixel,
mettre en cache par identité/version, mesurer CPU et mémoire ; un chemin GPU
est une possibilité future, pas un gain acquis.

## Contrats d'affichage à spécifier

- Préférences d'affichage globales/machine, distinctes des recettes `.ipr`.
- Réaction au déplacement de la fenêtre entre écrans et politique si elle en
  couvre plusieurs ; invalidation de l'aperçu, pas du RAW ni du master retouché.
- Repli explicite en cas de profil absent/invalide et diagnostic du profil actif.
- Intention de rendu et gestion du noir à définir selon les profils disponibles ;
  ne pas promettre une conversion perceptuelle universelle.
- Responsabilité unique de conversion avec Qt et l'OS, y compris la séparation
  future SDR/HDR. Changer un profil ICC n'active pas à lui seul un pipeline HDR.
- Tests sur plusieurs OS : export identique avant/après changement d'écran,
  bon profil par moniteur, sortie non gérée détectable, caches bornés et invalidés.

## Choix du master après comparaison — 15 octobre 2026

L'utilisateur a choisi Rec.2020 linéaire D65, après le comparatif post-DCP du
§44 du rapport RAW/profils. [ADR-0009](../decisions/ADR-0009-espace-travail-rec2020-lineaire.md)
consigne la décision ; le [plan de préparation](integration-rec2020.md) précise
les conversions, opérateurs sensibles, sorties, caches et tests.
Les développements ProPhoto ci-dessus décrivent la piste étudiée auparavant,
pas une seconde cible de production. Le master actuel est Rec.2020 linéaire.
Rec.2020 ne signifie ni affichage HDR ni récupération des valeurs perdues dans
les tables DCP. L'affichage ICC écran reste une étape séparée ; la première
migration conserve les destinations sRGB pour isoler les effets.

## Références

Consultées le 15 octobre 2026 :

- [ICC, spécification ROMM RGB](https://www.color.org/chardata/rgb/ROMMRGB.pdf) :
  primaires, blanc D50, calcul linéaire puis encodage.
- [Adobe, espaces de travail](https://helpx.adobe.com/photoshop/desktop/adjust-color/color-effects-techniques/color-working-spaces.html) :
  distinguer espace de travail et profil spécifique de moniteur.
- [ICC, profils d'entrée et de destination](https://www.color.org/profile/) :
  séparation des transformations dans une chaîne gérée en couleur.
- Code actuel : [contrat WorkingImage](../../src/core/ColorPipeline.h),
  [conversion de sortie](../../src/core/ColorPipeline.cpp) et
  [rendu des réglages](../../src/editor/imageeditor.cpp).
