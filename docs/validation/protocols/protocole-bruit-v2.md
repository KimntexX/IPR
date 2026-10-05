# Protocole bruit v2 — revalidation après correctif CPU

**Consolidation :** 5 octobre 2026. **État :** exécuté sur douze scènes ;
fonctionnement/progressivité confirmés, qualité globale et recette IHM à poursuivre.
Résultats : [campagne v2](../analyses/analyse-bruit-v2-2026-10-05.md).
Complète le [protocole initial](protocole-validation-bruit-2026-10-04.md) ;
méthode de mesure et limites identiques. [Correctif](../analyses/correctif-bruit-2026-10-05.md).

## Corpus prioritaire : mêmes images

Garder les RAW 0043/0112/0327/0624/9890 et les TIFF Lightroom audités.
**Ouvrir les RAW**, pas un TIFF déjà développé. Le neutre Lightroom 0112
est bien celui à bruit 0/0 ; les réexports luminance LR 9890 −5 à −8 sont
les bons états. Le dernier LR 0624 a été corrigé à luminance 100/couleur 0.

1. Lancer `build-windows-msvc/Release/appIPR.exe` corrigé. Ouvrir un RAW,
   réinitialiser les réglages puis vérifier WB/profil/géométrie identiques
   à la première campagne. Un sidecar peut restaurer le dernier état non nul.
2. Exporter un neutre IPR bruit 0/0 et gain de netteté 0. La nouvelle branche
   de bruit à zéro ne modifie pas les pixels ; vérifier néanmoins l'identité
   du neutre avec l'ancien pour détecter une différence d'autres réglages.
3. Exporter quatre niveaux **25/50/75/100** de luminance avec couleur 0,
   puis quatre niveaux de couleur avec luminance 0. Réinitialiser entre séries.
4. TIFF sRGB 16 bits SDR, pleine résolution, pas de redimensionnement ni
   de netteté de sortie ; EV/texture/clarté/voile/grain et gain de netteté à 0.
5. Examiner à 100 % : ciel sombre 0112, bruit du mur/vêtement 9890,
   pelage/sangle 0624, fleur 0043 et rocher 0327. Surveiller lissage de vraie
   texture, halos, saignement de couleur et perte de saturation.
6. Comparer aperçu stabilisé et export ; rouvrir une image pour vérifier
   la restauration des deux valeurs de bruit. Ne pas tester le couplage
   netteté dans ces premières séries.

## Destination : ne pas écraser la première campagne

Proposition pour les **nouveaux IPR** :

```text
tests/image/bruit-v2/
  neutre/              ID-IPR.tiff
  bruit/luminance/     ID-IPR.tiff, ID-IPR-2.tiff, ID-IPR-3.tiff, ID-IPR-4.tiff
  bruit/couleur/       mêmes suffixes pour les quatre valeurs
```

Le fichier sans suffixe dans une série signifie **25**, pas 0.
`neutre` signifie bruit 0/0. Les références Lightroom de la campagne précédente
restent dans `tests/image/base` ; l'analyste les associera avec `--variant-file`
et `--neutral-file`, sans exiger la recopie de dizaines de gros TIFF.
Si l'export propose déjà −5/−6/…, ne pas déduire les valeurs du suffixe :
noter l'association. Un nouveau dossier évite cette ambiguïté.

Le nouvel analyseur peut travailler avec `--root tests/image/bruit-v2` et les
mappings explicites des références. Les fichiers bruts JSON ne doivent pas
écraser les mesures v1 à v4 du 4 octobre.

## Compléments facultatifs près de zéro

Sur 0043 et 9890, trois IPR de luminance **1/5/10**, nommés clairement
`ID-IPR-L001.tiff`, `ID-IPR-L005.tiff`, `ID-IPR-L010.tiff` dans un dossier
`activation`. Cette petite série sert à examiner le démarrage du curseur,
pas à remplacer les quatre états standards. Le comparateur ne déduit pas ces
valeurs des suffixes standards : utiliser `--extra-state IMAGE_ID luminance
IPR VALEUR CHEMIN` pour les associer explicitement. Les neuf exports de
0043/9890/9421 aux valeurs 1/5/10 ont été mesurés dans la campagne v2.

## Exécution réelle du 5 octobre

Les fichiers reçus sont dans `tests/image/base`, pas dans le dossier proposé
`bruit-v2`. Les nouveaux IPR historiques utilisent −5/−6/−7/−8 pour
25/50/75/100 ; les nouveaux sujets utilisent sans suffixe/−2/−3/−4.
Ne pas réanalyser les anciens IPR par erreur. Le
[lanceur de campagne](../raw/analyse-bruit-v2-2026-10-05.sh) conserve les
associations exactes, notamment le neutre 0112 et les Lightroom 9890 corrigés.
Total : 24 neutres et **136 variantes standards + 9 activations**.
Les sept nouvelles scènes n'ont pas de variantes Lightroom de bruit ;
ce manque limite la comparaison, pas l'analyse interne IPR.

9950 et 1020 ont été confirmées par l'utilisateur comme les identifiants
initialement annoncés 1050/9920. ISO 3200 (1020/9421) et 8000 (9734) sont
désormais couverts avec peau, cheveux, rocher, textile et aplats. L'ISO 6400
exact n'est pas un prérequis pour poursuivre ; ce corpus n'est toutefois pas
une échelle ISO de prises identiques.

## Nouvelles photos utiles, sans multiplier les séries

Les faibles ISO 100/400 et extrêmes 25 600/64 000 sont déjà couverts.
Le manque principal est une **scène à texture réelle entre ISO 1 600 et 12 800**.

- Minimum utile : même scène à **ISO 3 200 et 6 400**.
- Si possible : ajouter **ISO 1 600 et 12 800** pour vérifier la transition.
- Dans le cadre : une plage uniforme grise/peinte, une zone sombre non noire,
  une texture fine (tissu tissé, cheveux/pelage ou feuillage) et un bord net
  clair/foncé. Une seconde scène avec peau/carnation est souhaitable.
- Inclure deux plages colorées voisines pour contrôler le saignement de
  couleur ; ne pas remplacer toute la scène par un motif artificiel répétitif.
- Trépied si disponible, sujet immobile, même lumière/cadrage/ouverture ;
  ajuster le temps de pose pour une luminosité comparable sans bougé.
  Garder les RAW d'origine et éviter une réduction de bruit longue pose qui
  changerait la capture entre prises. Noter boîtier, objectif, ISO et éclairage.

**Pas besoin de nouveaux Lightroom pour les cinq RAW existants.** Pour les
nouvelles scènes seulement, fournir un neutre IPR/LR et, dans un premier
temps, les états 50/100 luminance seule et couleur seule. On complétera
25/75 si un défaut apparaît ; étiqueter ces états explicitement puisque
ce sous-ensemble ne suit pas la numérotation automatique à quatre fichiers.
La qualité ne sera pas attribuée à l'ISO seul : vérifier d'abord le bruit
et la mise au point dans le neutre.
