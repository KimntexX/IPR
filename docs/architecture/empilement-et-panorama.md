# Empilement et assemblage panoramique

État : **plan de préparation**, approuvé comme base par l'utilisateur le
29 octobre 2026. Aucun moteur multi-images ni dossier tampon implémenté par
ce document. Chantier distinct du prototype de débruitage d'une image.

## 1. Périmètre et ordre de réalisation

| Palier | Fonction | Critère de passage |
| --- | --- | --- |
| M0 | Contrat multi-source, tâches annulables, budget RAM/disque, recette | Sources intactes, erreurs et ressources maîtrisées |
| M1 | Empilement antibruit à exposition constante, scène statique | Alignement sous-pixel et gain mesuré sans perte de détail inexpliquée |
| M2 | Repère utilisateur, suivi du support choisi, recadrage automatique | Déplacements connus retrouvés, échecs signalés, bords valides |
| M3 | Panorama horizontal ou vertical, une rangée | Géométrie cohérente, raccords et couleurs contrôlés |
| M4 | Panorama multirangée | Optimisation globale, canvas et mémoire bornés |
| M5 | Empilement de netteté et fusion d'expositions | Qualification propre à chaque mode, pas une moyenne générique |

Horizontal et vertical sont deux dispositions du même moteur panorama, pas
deux implémentations. Un collage sans chevauchement reste une opération
distincte : il ne réalise ni recalage photographique ni raccord invisible.

Le corpus `tests/image/colorimétrie/rafale` contient42 CR3 lors de
l'inventaire du29 octobre ; il est candidat pour M1, pas déjà qualifié pour
tous les modes. Les sources ne sont ni déplacées ni supprimées ici.

## 2. Socle partagé et frontières couleur

Réutiliser l'[import commun](import-images.md), les aperçus, les métadonnées
et les réservations du [contrôleur mémoire](controle-memoire-images.md).
`ImageEditor` reste un cadenceur ; alignement et fusion vivent dans des modules
à interfaces courtes, hors du QML. Les objets avec état portent la tâche,
ses ressources et sa durée de vie ; les calculs restent des fonctions libres.

```text
Sélection + recette multi-source
        ↓
Audit / référence / aperçus / budget RAM et disque
        ↓
Alignement / cartes de validité et confiance
        ↓
Lecture et fusion par tuiles via dossier tampon
        ↓
Recadrage proposé / nouvelle image éditable / export explicite
```

- Panorama visuel : images développées avec des réglages cohérents dans le
  master Rec.2020 linéaire D65, avant encodage d'affichage/export.
- Empilement antibruit et HDR physique : privilégier une frontière avant les
  courbes de profil et les retouches non linéaires. Le développement RAW
  flottant et signé prépare cette possibilité ; son usage multi-image reste
  à qualifier. Une approche CFA future doit respecter la phase du motif Bayer,
  pas interpoler naïvement la mosaïque comme une image RGB.
- Le master stocké en transfert linéaire peut déjà contenir la courbe du DCP :
  il n'est pas automatiquement un signal radiométrique scene-linear.
- Même balance de référence et même traitement couleur pour une série ;
  conserver niveaux d'exposition et saturations utiles. Ne pas appliquer
  une BdB automatique différente à chaque image sans contrôle.
- Géométrie estimée sur des proxies, raffinée au besoin à pleine définition ;
  composer autant que possible les déformations pour limiter les rééchantillonnages.

Les modules d'OpenCV peuvent servir de référence ou d'adaptateur expérimental,
pas de contrat public du nouveau moteur : préserver la
[roadmap d'indépendance de la couche image](roadmap-couche-image-sans-opencv.md).
Auditer les types acceptés par chaque brique ; aucun passage entier8 bits ne
doit être imposé aux pixels de fusion sous prétexte que le proxy géométrique
utilise une représentation réduite.

## 3. Repère utilisateur et image de référence

**Demande utilisateur :** permettre de désigner un point de référence pour
recaler la pile, malgré le déplacement de l'appareil ou du ciel, puis tronquer
automatiquement les bords non communs. Ce comportement est retenu comme cible.

Prévoir une image de référence choisie par l'utilisateur ou proposée selon
netteté, exposition et couverture. Sur cette image, un clic pose une ancre ;
une petite région autour du point sert au suivi. Un point sans texture ne
suffit pas : afficher la qualité du suivi et permettre d'agrandir/déplacer
la région, d'ajouter des repères ou d'exclure une image.

Un seul point correspondant fournit deux contraintes : il suffit à déterminer
une translation2D, pas une rotation, une échelle ou une perspective arbitraire.
Le clic est donc une **ancre et un choix de support**, pas une calibration
géométrique complète. Le moteur recherche d'autres correspondances autour
ou sur le support choisi. Pour une homographie générale, au moins quatre
correspondances en configuration non dégénérée sont nécessaires ; en pratique,
en chercher davantage et rejeter les fausses correspondances.

Le point est fixé dans les coordonnées de la référence, puis les transformations
de chaque source sont estimées vers ce repère. Ne pas déplacer successivement
les images uniquement les unes par rapport aux autres : cela accumulerait les
erreurs. Enregistrer coordonnées, convention de pixels, transformations,
référence et confiance dans la recette pour reproduire l'alignement.

### Ciel et premier plan : deux mouvements possibles

- **Ancrage terrestre** : privilégier paysage/bâtiment/sol. Le ciel mobile
  ne peut pas être supposé immobile dans cet alignement.
- **Ancrage céleste** : utiliser une étoile ou une région contenant plusieurs
  étoiles ; prévoir translation et rotation selon la séquence. Les étoiles
  peuvent se recaler, tandis que le premier plan se décale.
- Les nuages changent aussi de forme : un clic ne garantit pas leur suivi
  par une transformation rigide. Refuser un suivi incertain plutôt que créer
  des détails artificiels ou des traînées non signalées.
- **Évolution ultérieure** : masques ciel/sol et alignements séparés, puis
  raccord contrôlé à l'horizon. Ce n'est pas promis dans le premier prototype.

Le recalage géométrique global ne corrige pas tous les objets mobiles ni la
parallaxe. La fusion doit utiliser une confiance locale, rejeter les contributions
incompatibles et revenir à la référence lorsque le support est insuffisant.

## 4. Troncature et couverture

Après transformation, calculer les masques de pixels réellement valides,
en tenant compte du support du noyau d'interpolation et des corrections optiques.
Ne pas détecter les bords par leur couleur : un pixel noir peut être valide.

- **Pile :** proposer par défaut le plus grand rectangle inscrit dans
  l'intersection valide des images retenues. Afficher le recadrage et la part
  de surface perdue avant confirmation. Une image très décalée doit pouvoir
  être exclue, plutôt que réduire silencieusement toute la pile.
- Prévoir ensuite une couverture minimale configurable : au moins K images
  valides par pixel, avec carte du nombre de contributions et de confiance.
  Les coins moins couverts n'ont pas le même niveau de réduction de bruit.
- **Panorama :** utiliser l'union des vues retenues, puis un rectangle intérieur
  ou une sortie à bords transparents. L'intersection de toutes les vues détruirait
  précisément l'élargissement de champ recherché.
- Une intersection vide, un canvas excessif ou un suivi perdu produit un
  diagnostic exploitable, pas une sortie vide présentée comme un succès.

## 5. Dossier tampon disque : exigence utilisateur

Les empilements et panoramas doivent utiliser un **dossier tampon dédié sur
disque**, en complément de la RAM bornée. Il ne s'agit ni du dossier des sources,
ni des dossiers partagés de références photographiques LR/IPR.

Prévoir un emplacement configurable et un sous-dossier unique appartenant à
chaque tâche. Les chemins par défaut devront suivre les emplacements temporaires
appropriés à Windows, Linux et macOS ; ne pas coder un chemin Windows en dur.
Un volume rapide est souhaitable, sans imposer un matériel particulier.

Règles à implémenter :

- Estimer espace disque, canvas, pyramides, masques et buffers avant lancement ;
  conserver une marge et revérifier pendant le calcul. Plusieurs tâches peuvent
  consommer le même espace : l'estimation n'est pas une garantie de disponibilité.
- Définir quotas de RAM, de tampon disque et de dimensions ; réduire la
  concurrence ou refuser explicitement une tâche trop grande. Le seuil RAM80 %
  du contrôleur actuel est un objectif de pression, pas une garantie absolue.
- Lire/développer progressivement, écrire des tuiles flottantes sans perte,
  puis ne charger que les tuiles et halos nécessaires. Ne pas garder toutes
  les images pleine résolution en RAM. Une pyramide ou un fichier mappé en
  mémoire consomme encore des ressources à comptabiliser.
- Versionner le format interne et son index : espace couleur, étape pipeline,
  précision, dimensions, canaux, alpha, coordonnées, transformations et intégrité.
  Les temporaires numériques ne sont pas des exports TIFF de revue visuelle.
- Conserver une recette multi-source persistante distincte du tampon : identités
  des entrées, réglages, référence, ancre, mode, versions et recadrage. La disparition
  du tampon ne doit pas effacer les informations nécessaires à une régénération.
- Publication atomique des unités complètes et du résultat final ; ne jamais
  écraser une source ou exposer un fichier partiel comme une image valide.
- Gérer disque plein, droits refusés, annulation, erreur de lecture et arrêt
  brutal ; fermer fichiers, libérer réservations et joindre les tâches sur tous
  les chemins. Une reprise exige des sources et paramètres toujours compatibles.
- Nettoyer seulement les fichiers temporaires explicitement possédés par la
  tâche. Après crash, proposer les travaux récupérables et le nettoyage des
  orphelins identifiés. Vérifier les chemins résolus, sans suivre de lien vers
  un dossier extérieur. Aucune suppression des RAW, références ou exports.

Ordre de grandeur :42 images RGB float32 de32 Mpx représentent environ16,1 Go
en unités décimales, avant masques, pyramides, source RAW et résultat ; ce n'est
pas un dimensionnement suffisant du dossier tampon ni de la RAM.

## 6. Algorithmes spécifiques et validation

### Empilement antibruit

Commencer à exposition constante sur scène statique : moyenne robuste pondérée
par variance et confiance, avec exclusion des mouvements. La réduction idéale
d'écart-type en1/sqrt(N) suppose du bruit indépendant ; le bruit fixe commun,
les corrélations et les erreurs de recalage limitent le gain. Ne pas promouvoir
une baisse de variance obtenue par flou ou disparition des détails.

### Panorama

Points d'intérêt, correspondances robustes, optimisation globale des positions,
projection choisie (rectilinéaire/cylindrique/sphérique), compensation d'exposition,
recherche des raccords et fusion multibande. Détecter insuffisance de chevauchement,
parallaxe, reflets et mouvement. Une seule homographie ne résout pas toutes les
profondeurs d'une scène photographiée depuis des positions différentes.

### Netteté et exposition

Focus stacking : traiter le changement de grossissement avec la mise au point,
les contours d'occlusion et les halos. Fusion d'exposition : distinguer la fusion
visuelle d'images bien exposées d'un vrai HDR radiométrique ; qualifier expositions,
saturations et anti-fantômes. Ne pas réutiliser aveuglément les poids antibruit.

### Critères communs

Tests synthétiques de translations/rotations connues, géométrie sous-pixel,
masques/recadrages exacts, déterminisme, images uniformes ou sans texture,
suivi perdu, scènes mobiles, cohérence couleur et conservation des sources.
Tests d'erreurs RAM/disque, tailles extrêmes, fichiers tronqués et annulation.
Mesurer temps, RAM et stockage temporaire par phase. Revue à100 % des contours,
étoiles, matière et raccords ; mesurer aussi la couverture et le nombre effectif
de contributions. Les preuves restent des JSON versionnés et horodatés ; un seul
rapport évolutif par famille sera créé au lancement des campagnes, pas ici.

L'IHM proposera sélection, mode, référence/ancre, aperçu d'alignement, couverture,
recadrage, progression et annulation ; les commandes moteur nécessaires seront
décrites au [suivi des raccordements](suivi-raccordements-ihm.md) avant intégration.
Prévoir une branche durable par groupe depuis `dev` à jour lors du lancement,
sans mélanger ce chantier aux modifications non validées de `patch_raw_bench`.

## 7. Références de conception

- [HDR+ : Burst photography for high dynamic range and low-light imaging](https://research.google/pubs/burst-photography-for-high-dynamic-range-and-low-light-imaging-on-mobile-cameras/) : alignement et fusion RAW de rafales, référence d'architecture, pas validation d'IPR.
- [OpenCV — chaîne d'assemblage](https://docs.opencv.org/4.x/d1/d46/group__stitching.html) et [exemple détaillé](https://docs.opencv.org/4.x/d9/dd8/samples_2cpp_2stitching_detailed_8cpp-example.html) : modèles géométriques, projections, raccords et fusion.
- [Hugin — align_image_stack](https://hugin.sourceforge.io/docs/manual/Align_image_stack.html) : alignement des piles, réglage du champ pour les changements de grossissement.
- [Enfuse](https://hugin.sourceforge.io/docs/manual/Enfuse.html) : distinction fusion d'expositions, HDR et empilement de mise au point.

Ces références ne décident ni de la licence d'IPR ni de l'incorporation de code
tiers. Vérifier licences et attribution avant toute réutilisation concrète.
