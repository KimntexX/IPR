# Gestion des lumières en photographie numérique

**Date :** 2 octobre 2026  
**Objet :** fondements physiques et mathématiques, modèles de rendu et recommandations pour IPR.

## Résumé décisionnel

La gestion des lumières relie trois problèmes distincts : mesurer la scène par le capteur, modifier ses valeurs sans détruire les rapports physiques, puis les rendre visibles sur un écran ou dans un JPEG à dynamique plus faible.

Pour IPR, la base à conserver est le calcul d'exposition en espace linéaire, les contrôles de plages tonales continus et une transformation de sortie explicite. Les opérateurs locaux puissants (Retinex, CLAHE, domaine des gradients) sont utiles dans certaines scènes extrêmes, mais ils peuvent produire halos, bruit et un rendu artificiel. Ils ne doivent pas devenir le rendu implicite par défaut.

La prochaine amélioration ne requiert pas encore un nouvel algorithme : il faut d'abord valider le pipeline sur un corpus RAW. Après cette mesure, la priorité recommandée est une courbe monotone par morceaux (PCHIP/Hermite monotone) et une séparation explicite entre rendu de scène et rendu d'affichage. Un opérateur local préservant les contours peut ensuite être prototypé comme réglage optionnel.

## 1. Les trois domaines à ne pas confondre

| Domaine | Grandeur | Question | Exemple IPR |
|---|---|---|---|
| Capture / scène | radiance, exposition, réponse capteur | quelle lumière a atteint le capteur ? | RAW, niveaux noir/blanc, WB, DCP |
| Travail / édition | RGB linéaire flottant | comment modifier la lumière sans déformer ses rapports ? | exposition, tons, courbe |
| Sortie / affichage | luminance écran, gamut, encodage | comment afficher une scène plus dynamique que l'écran ? | tone mapping, gamut mapping, sRGB/JPEG |

Un RAW est une mesure mosaïquée dépendante du capteur ; ce n'est ni une image sRGB ni une image « sombre » par nature. Après traitement du noir et du blanc, dématriçage, balance des blancs et transformation colorimétrique, l'image devient exploitable dans un espace de travail. Les opérations d'exposition et de mélange doivent précéder l'encodage non linéaire de sortie.

Une valeur de travail supérieure à `1` peut encore porter de l'information dans une haute lumière. Un écrêtage précoce la détruit avant qu'une compression tonale puisse la restituer.

## 2. Fondements physiques, mathématiques et perceptuels

### Exposition, stops et dynamique

À ISO et scène constants, l'exposition au capteur est proportionnelle au temps de pose `t` et inversement proportionnelle au carré du nombre d'ouverture `N` :

```text
H ∝ t / N²
```

Un stop (`+1 EV`) double la lumière. Dans un tampon linéaire :

```text
RGB' = 2^EV × RGB
```

Cette multiplication conserve les rapports entre pixels, contrairement à l'addition d'une constante. L'ISO est une sensibilité/indice d'exposition normalisé, pas une réserve de lumière récupérable après la prise de vue. La dynamique peut s'exprimer en stops : `DR = log2(H_max / H_min)`.

### Linéarité, gamma et luminance

Après normalisation, le signal RAW est approximativement linéaire à la lumière. sRGB, au contraire, possède une fonction de transfert non linéaire : doubler une valeur sRGB encodée ne correspond pas à `+1 EV`. Exposition, fusion HDR, flous physiques et mélange alpha doivent donc se faire en linéaire.

Pour sRGB/D65, la luminance relative usuelle est :

```text
Y = 0,2126 R + 0,7152 G + 0,0722 B
```

où les composantes sont linéaires. Elle est utile pour des masques de tons ou une courbe qui préserve approximativement la chromaticité. Elle n'est toutefois pas synonyme de clarté perçue : l'œil s'adapte à son environnement et sa sensibilité varie avec la luminance et la fréquence spatiale.

### Adaptation et contraste local

La vision est très sensible aux rapports locaux. Une courbe globale peut donc sembler plate dans une scène à forte dynamique, même avec une exposition correcte. Inversement, une correction locale peut supprimer les gradients d'éclairage qui donnent volume et ambiance. Les modèles d'apparence (CIECAM16) et de contraste visible apportent un cadre pour l'affichage, mais ils ne remplacent pas les contrôles photographiques usuels.

## 3. Modèles existants

### 3.1 Exposition, contraste et courbes globales

Chaque pixel reçoit la même fonction `T(Y)`. L'exposition est multiplicative ; le contraste est une pente autour d'un gris de référence (souvent 18 %) ; une courbe en S, logarithmique, sigmoïde ou filmique compresse progressivement les extrêmes.

Cette famille est rapide, prévisible et sans halos. Son défaut est de ne pas pouvoir protéger simultanément un sujet sombre et une fenêtre très lumineuse sans compromis. Une courbe RGB par canal peut déplacer les teintes ; une courbe appliquée à la luminance, avec reconstruction chromatique prudente, est généralement plus stable.

**Courbes à points.** Une interpolation globale de Lagrange passe exactement par les nœuds mais peut osciller entre eux et dépasser les bornes quand le nombre de points ou leur espacement augmente (phénomène de Runge). IPR utilise actuellement une interpolation barycentrique de Lagrange. Pour une courbe utilisateur, une interpolation linéaire ou une spline cubique Hermite monotone (PCHIP/Fritsch--Carlson) offre un meilleur contrat : segments locaux, monotonie contrôlable et absence de dépassement. C'est l'amélioration prioritaire recommandée.

### 3.2 Compression globale photographique (Reinhard)

Un opérateur classique compresse une luminance positive selon une forme de type :

```text
L_d = L / (1 + L)
```

Il est généralement adapté au log-moyen de la scène et à un blanc de référence. Le modèle de Reinhard et collaborateurs formalise une analogie avec la reproduction photographique.

**Forces :** très faible coût, hautes lumières progressives, comportement lisible.  
**Limites :** rendu parfois plat ou gris si le point blanc et l'adaptation ne sont pas maîtrisés. Il doit être présenté comme rendu de base/sortie, non caché dans le curseur d'exposition.

### 3.3 Masques de plages tonales

Au lieu de corriger toute l'image, des poids continus `w_i(Y)` ciblent chaque plage :

```text
Y' = Y + Σ w_i(Y) × Δ_i(Y)
```

Les poids se recouvrent et se raccordent avec une transition lisse (`smoothstep`, gaussienne ou sigmoïde). Une baisse des hautes lumières devient ainsi une compression progressive plutôt qu'un assombrissement uniforme.

**Forces :** meilleur compromis pour les curseurs noirs, ombres, hautes lumières et blancs.  
**Limites :** une mauvaise composition des poids crée un double effet, une discontinuité ou un voile global. IPR possède déjà cette famille : la validation doit maintenant confirmer son comportement sur des scènes réelles.

### 3.4 Base/détail : bilatéral, guidé et pyramides

On sépare une base d'éclairage douce `B` et les détails `D`, souvent dans le logarithme de luminance :

```text
ℓ = log(Y + ε),   D = ℓ - B,   ℓ' = k × B + D
```

Un filtre bilatéral ou guidé lisse au sein des régions tout en respectant les bords ; une pyramide laplacienne fait une séparation analogue à plusieurs échelles. On compresse la base et l'on préserve, ou dose, les détails.

**Forces :** excellente lisibilité des contre-jours, conservation des textures. Durand et Dorsey ont popularisé cette compression de base préservant les détails ; les pyramides de Burt et Adelson en donnent une base multi-échelle.  
**Limites :** halos, bruit remonté, micro-contraste excessif et coût mémoire/calcul. Candidat raisonnable à un prototype optionnel, jamais à un automatisme opaque.

### 3.5 Domaine des gradients et reconstruction de Poisson

On atténue les grands gradients de `log(Y)` puis on reconstruit l'image la plus proche du nouveau champ de gradients, typiquement par :

```text
∇²ℓ' = div(g(∇ℓ))
```

Fattal, Lischinski et Werman proposent cette approche pour réduire fortement la dynamique tout en conservant les détails locaux.

**Forces :** puissant sur HDR extrême.  
**Limites :** solveur itératif, conditions aux bords, coût élevé et rendu qui peut devenir artificiel. À réserver à de la recherche ou à un module expert ultérieur.

### 3.6 Retinex : illumination × réflectance

Retinex repose sur l'hypothèse :

```text
I(x) = R(x) × L(x)        donc        log I = log R + log L
```

`R` représente la réflectance et `L` l'illumination. Une illumination estimée comme lente est retirée partiellement afin de stabiliser clarté et couleur face à des éclairages variés. Land et McCann ont posé ce cadre ; les variantes Multi-Scale Retinex emploient plusieurs rayons de filtrage.

**Forces :** relèvement d'ombres et compensation d'éclairage non uniforme.  
**Limites :** la séparation n'est jamais parfaite dans une photo réelle (spéculaires, brume, éclairages colorés). Les variantes naïves créent halos, désaturation et dérive chromatique. À considérer comme inspiration pour une correction locale contrôlée, pas comme correction universelle.

### 3.7 Histogrammes locaux et CLAHE

L'égalisation d'histogramme redistribue les niveaux par leur fonction de répartition cumulative. CLAHE le fait par tuiles et limite le gain pour éviter une amplification sans borne.

**Forces :** révèle très efficacement une information peu visible.  
**Limites :** ne respecte ni l'éclairage ni une intention photographique ; il amplifie bruit et micro-contraste et peut faire apparaître les tuiles. Pertinent comme effet spécialisé, pas comme réglage Lumière général d'IPR.

### 3.8 Fusion multi-exposition et HDR

Plusieurs poses sont recalées et combinées. Debevec et Malik estiment la réponse de l'appareil puis reconstruisent une carte de radiance à partir de différentes expositions ; un tone mapping reste nécessaire pour l'écran SDR.

**Forces :** récupère réellement une information absente d'une seule pose écrêtée.  
**Limites :** mouvement, fantômes, recalage, stockage et interface spécifique. C'est une fonctionnalité future distincte des curseurs d'un RAW isolé.

### 3.9 Rendu dépendant de l'écran et modèles d'apparence

La transformation finale tient compte du blanc, du noir, de la luminance maximale, du gamut et parfois de l'éclairage ambiant. Les transformations de sortie ACES séparent la transformation de rendu de l'encodage propre au dispositif ; CIECAM16 modélise l'apparence dans un contexte de visualisation.

**Forces :** cohérence entre écrans et exports, base propre pour large gamut et HDR.  
**Limites :** profils ICC, calibration, choix de l'espace de travail et tests plus complexes. C'est une évolution d'architecture colorimétrique, pas un simple filtre de luminosité.

## 4. Comparaison

| Approche | Portée | Coût | Risques | Position IPR |
|---|---:|---:|---|---|
| Exposition `2^EV` | globale/scène | très faible | clipping sans compression finale | indispensable, en linéaire |
| Courbe monotone | globale | très faible | noirs bouchés, blancs écrêtés | indispensable ; migrer vers PCHIP |
| Masques de tons | plages | faible | jonctions mal composées | conserver et valider |
| Reinhard/filmique | globale/sortie | faible | rendu plat | rendu de base explicite |
| Bilatéral/guidé/pyramides | local | moyen | halos, bruit | prototype optionnel |
| Gradients/Poisson | local fort | élevé | aspect artificiel | recherche, pas V1 |
| Retinex | illumination locale | moyen | halos, dérive chromatique | expérimental, désactivé par défaut |
| CLAHE | histogramme local | faible à moyen | bruit, aspect clinique | effet spécialisé seulement |
| Fusion HDR | multi-images | élevé | ghosting, recalage | fonctionnalité séparée |
| ACES/CIECAM/ICC | chaîne de sortie | moyen à élevé | complexité profils | feuille de route colorimétrique |

## 5. Implications pour le pipeline actuel

Les documents IPR décrivent un tampon `CV_32FC4` RGB sRGB linéaire, une exposition `2^EV`, un contraste au pivot 18 % et des masques continus pour noirs, ombres, hautes lumières et blancs. Cette direction est cohérente avec les fondamentaux.

À préserver :

- ne pas ramener les valeurs flottantes dans `[0,1]` avant la transformation de sortie ;
- garder exactement le même ordre de traitement pour aperçu et export ;
- séparer courbe DCP, courbe créative utilisateur et transformation de sortie pour éviter les doubles compressions cachées ;
- placer toute correction locale de lumière avant le grain créatif et l'évaluer avec le futur débruitage.

À vérifier avec le corpus :

1. Le décodage RAW respecte-t-il effectivement le contrat linéaire, avec niveaux noir/blanc et gamma explicitement contrôlés ?
2. Dans une zone non saturée, le rapport de luminance est-il proche de deux entre `0` et `+1 EV` avant la sortie ?
3. Les quatre tons sont-ils neutres à zéro et continus à la jonction des masques ?
4. Une courbe identité donne-t-elle le même aperçu et export, sans dérive de teinte ni dépassement ?
5. Les couleurs saturées restent-elles plausibles lors de la compression des hautes lumières ?

## 6. Protocole de validation des futures images

### Corpus minimal

Conserver les RAW originaux et, pour chaque fichier, relever boîtier, objectif, ISO, ouverture, vitesse, WB, profil DCP/LCP et éclairage.

| Scène | Vérification principale |
|---|---|
| charte grise / ColorChecker homogène | gris, exposition, teinte, dérive couleur |
| scène low-key détaillée | noirs, ombres, bruit remonté |
| scène high-key avec matière blanche | blancs, épaule, clipping |
| contre-jour fenêtre ou ciel/sujet | hautes lumières, ombres, halos |
| soleil dur et ombres nettes | transitions et contraste local |
| tungstène et jour | WB, DCP et tonalité de lumière |
| peau, ciel, feuillage | teinte sous compression tonale |
| même sujet à ISO croissants | interaction lumière, bruit, débruitage |

### Mesures

Pour chaque image, archiver une exportation neutre puis modifier un seul curseur à la fois. Comparer :

- proportion de pixels écrêtés bas/haut par canal et en luminance ;
- luminance moyenne, médiane et log-moyenne ;
- rapport de luminance d'une même zone à `EV = -1, 0, +1` ;
- profil traversant une transition ciel/sujet afin de révéler halos et inversions de pente ;
- chromaticité de patches neutres et de charte, si disponible ;
- temps aperçu/export, mémoire maximale et déterminisme.

`ΔE00` est utile avec une charte et un référentiel connus ; il ne mesure pas l'agrément d'une image ni le naturel de son contraste local. L'évaluation visuelle doit donc compléter ces mesures, sur un écran connu et avec une référence côte à côte.

### Recette par réglage

| Réglage | Positions | Attendu |
|---|---|---|
| Exposition | `-2`, `-1`, `0`, `+1`, `+2 EV` | facteur lumineux prévisible hors saturation |
| Contraste | minimum, `0`, maximum | pivot stable, pas de dominante imprévue |
| Noirs/Ombres | minimum, `0`, maximum | action surtout basse luminance, sans voile global |
| HL/Blancs | minimum, `0`, maximum | texture ciel/blanc préservée, pas de gris sale global |
| Courbe luminance | identité, S douce, points rapprochés | monotonie, aucune ondulation ou cassure |
| Courbe RGB | une variation par canal | effet documenté, pas de changement involontaire |
| Interaction | EV + ombres ; HL + blancs ; WB + tons | pas de double application/clipping précoce |

## 7. Feuille de route recommandée

1. Valider le comportement existant sur le corpus et archiver RAW, paramètres, captures et exports.
2. Remplacer ou encadrer Lagrange par PCHIP/Hermite monotone ; tester identité, monotonie, bornes et absence de dépassement.
3. Définir explicitement le rôle de la courbe DCP, de l'exposition de base, de la courbe utilisateur et de la compression de sortie. Le rendu neutre doit être reproductible.
4. Prototyper, sans l'imposer, un filtre guidé ou bilatéral sur les seuls contre-jours. Comparer halo, bruit, temps et préférence visuelle.
5. Cadrer profils ICC, espace de travail et transformation de sortie dans une architecture séparant *scene-referred*, *view transform* et encodage écran, inspirée d'ACES.

## 8. Bibliographie et sources

Liens consultés le 2 octobre 2026 ; priorité donnée aux organismes, éditeurs et auteurs.

1. ISO, *ISO 12232:2019 — Photography — Digital still cameras — Determination of exposure index, ISO speed ratings, standard output sensitivity, and recommended exposure index*. [Fiche officielle ISO](https://www.iso.org/standard/73758.html).
2. P. Debevec et J. Malik, *Recovering High Dynamic Range Radiance Maps from Photographs*, SIGGRAPH 1997. [Article PDF](https://people.eecs.berkeley.edu/~malik/papers/debevec-malik97.pdf) ; [page du projet](https://www.pauldebevec.com/Research/HDR/).
3. E. Reinhard, M. Stark, P. Shirley et J. Ferwerda, *Photographic Tone Reproduction for Digital Images*, ACM TOG, 2002. [Rapport PDF](https://www-old.cs.utah.edu/docs/techreports/2002/pdf/UUCS-02-001.pdf).
4. F. Durand et J. Dorsey, *Fast Bilateral Filtering for the Display of High-Dynamic-Range Images*, SIGGRAPH 2002. [Page et PDF des auteurs](https://graphics.cs.yale.edu/publications/fast-bilateral-filtering-display-high-dynamic-range-images).
5. R. Fattal, D. Lischinski et M. Werman, *Gradient Domain High Dynamic Range Compression*, SIGGRAPH 2002. [Notice ACM / DOI](https://dl.acm.org/doi/10.1145/566570.566573).
6. E. Land et J. McCann, *Lightness and Retinex Theory*, JOSA, 1971, DOI 10.1364/JOSA.61.000001. [Article PDF](https://mccannimaging.com/Retinex/Retinex_files/L%26M1971.pdf) ; [notice OSA](https://www.wip.osapublishing.org/josa/abstract.cfm?uri=josa-61-1-1).
7. P. Pizer et al., *Adaptive Histogram Equalization and Its Variations*, CVGIP, 1987. [Rapport UNC PDF](https://techreports.cs.unc.edu/papers/86-013.pdf) ; [notice bibliographique](https://isgwww.cs.uni-magdeburg.de/~stefans/npr/entry-Pizer-1987-AHE.html).
8. P. Burt et E. Adelson, *The Laplacian Pyramid as a Compact Image Code*, IEEE Transactions on Communications, 1983, DOI 10.1109/TCOM.1983.1095851. [Article PDF](https://www.rctn.org/bruno/public/papers/Laplacian-pyramid-Burt%2BAdelson1983.pdf) ; [notice DOI](https://dblp.org/rec/journals/tcom/BurtA83.html).
9. R. Mantiuk, S. Daly et L. Kerofsky, *Display Adaptive Tone Mapping*, ACM TOG, 2008, DOI 10.1145/1360612.1360667. [Prépublication PDF](https://www.cl.cam.ac.uk/~rkm38/pdfs/mantiuk08datm.pdf) ; [résultats](https://resources.mpi-inf.mpg.de/hdr/datmo/results/index.html).
10. CIE, *Colorimetry, 4th Edition (CIE 015:2018).* [Publication CIE](https://www.cie.co.at/publications/colorimetry-4th-edition).
11. CIE, *CIE 2016 Colour Appearance Model for Colour Management Systems: CIECAM16.* [Publication CIE](https://www.cie.co.at/publications/cie-2016-colour-appearance-model-colour-management-systems-ciecam16).
12. Academy of Motion Picture Arts and Sciences, documentation ACES, *Output Transforms*. [Documentation ACES](https://docs.acescentral.com/system-components/output-transforms/).

## Annexe — glossaire

- **Luminance :** grandeur photométrique pondérée par la sensibilité visuelle ; ici `Y` est une luminance relative calculée en RGB linéaire.
- **Tone mapping :** projection de la dynamique/du contraste de scène vers un support affichable ; globale ou locale.
- **Gamut mapping :** adaptation des couleurs qui dépassent le gamut de sortie.
- **Scene-referred :** valeurs qui décrivent la scène ; **display-referred :** valeurs déjà adaptées à un écran précis.
- **PCHIP :** *Piecewise Cubic Hermite Interpolating Polynomial*, interpolation cubique locale pouvant préserver la monotonie.
