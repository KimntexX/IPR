# B5 — préparation de la détection de formes et d'objets

État : **préparation, aucun code ni modèle**, 5 novembre 2026. Précise le
[lot B5 de la feuille de route](roadmap-beta.md#8-b5--réseaux-de-détection-des-formes-et-des-objets).
Branche proposée : `patch_detection`, moteur d'abord, panneau sur une branche enfant.

## 1. Ce que B5 doit apporter à l'utilisateur

Trois gestes, par ordre de valeur :

| Geste | Sert à | Dépend d'un réseau ? |
| --- | --- | --- |
| **Cliquer sur un objet pour obtenir son masque** | Masques (B2), Retouche (B3), suppression assistée (B6) | Oui |
| **Redresser d'après les lignes de l'image** | Géométrie (B1) : proposition de rotation et de perspective | Non, à essayer d'abord sans |
| **Sélectionner le ciel ou le sujet** | Masques, comme les sélections automatiques de Lightroom | Oui, ou déduit du premier |

La détection d'objets avec classes (« voiture », « personne ») n'a pas de geste qui l'exige à ce
stade : **je propose de la reporter**. Le clic répond déjà au besoin sans nommer ce qui est cliqué, et
cela évite de promettre la reconnaissance de « tous les objets ».

Règle commune : une détection ne modifie jamais le rendu. Elle propose, l'utilisateur confirme et
corrige, et le résultat est un masque ordinaire, éditable et enregistré dans la recette.

## 2. Découpage proposé

| Palier | Contenu | Critère de passage |
| --- | --- | --- |
| D0 | Corpus, annotations, plans gelés, licences lues à la source | Manifeste figé, droits écrits. **Licences de code lues, poids en partie** |
| D1 | Lignes par méthode classique, sans réseau | Erreur d'angle mesurée sur horizons et verticales connus ; proposition utile sur le corpus. **Moteur fait, [résultats](../validation/analyses/analyse-detection.md) ; pas encore raccordé** |
| D2 | Banc Python : segmentation au clic, modèles existants, hors IPR | IoU et qualité de bord par classe de difficulté ; temps sur 8 threads |
| D3 | Moteur C++ : exécution, cache de l'analyse, coordonnées | Parité auteur/ONNX, coordonnées exactes après géométrie, annulation, mémoire bornée |
| D4 | Masque issu d'un modèle dans la recette et l'interface | Enregistrement et réouverture sans modèle, correction manuelle au pinceau |
| D5 | Ciel et sujet, si D2 montre qu'un clic ne suffit pas | Mesure dédiée |

D1 est utile même si le reste échoue, et précède tout réseau, comme le demande la feuille de route :
ne pas imposer un réseau si la méthode simple suffit. Il ne doit pas s'appuyer sur OpenCV, dont la
[sortie est planifiée](roadmap-couche-image-sans-opencv.md) : gradients, regroupement de segments et
points de fuite s'écrivent avec les noyaux déjà nécessaires.

## 3. Modèles à essayer, sans entraînement au départ

| Candidat | Rôle | Taille annoncée | Licence annoncée |
| --- | --- | --- | --- |
| MobileSAM | Segmentation au clic, léger | environ 40 Mo | Code Apache-2.0, poids non établis |
| SAM 2.1 tiny | Même usage, plus lourd | environ 78 Mo | Code et poids Apache-2.0 |
| EfficientSAM | Autre compromis | à relever | Code Apache-2.0, poids non établis |

Tailles relevées dans un [tableau comparatif d'Ultralytics](https://docs.ultralytics.com/models/mobile-sam), de
seconde main. Un [paquet ONNX communautaire](https://huggingface.co/Heliosoph/sam-onnx) annonce un fonctionnement
quasi temps réel sur CPU : **à mesurer chez nous**. Licences [lues à la source](../validation/analyses/analyse-detection.md#d0--licences-lues-à-la-source)
le 5 novembre 2026, sans téléchargement de poids.

Pour la détection reportée : les modèles YOLO d'Ultralytics sont annoncés sous AGPL-3.0, celle d'IPR, ce qui
ne les exclut pas ; RT-DETR est annoncé sous Apache-2.0. Une éventuelle extension des conditions aux poids
entraînés reste à clarifier à la source, de même que les droits du corpus d'entraînement (COCO) : je n'ai pas
trouvé de réponse fiable.

**Forme de ces modèles :** un encodeur lourd analyse l'image une fois, puis un décodeur léger répond à chaque
clic. C'est ce qui rend le geste possible : l'analyse peut prendre plusieurs secondes en tâche de fond, le clic
doit rester sous la cible de 250 ms.

## 4. Socle à mettre en commun

- **Exécution :** même moteur que B4. Le choix est à prendre **avec Codex**, dont le §5 propose ONNX Runtime
  avec CPU et CUDA. Pas de second moteur pour la détection.
- **Tâches, mémoire, annulation :** `TaskRunner`, contrôleur mémoire. Une analyse se réserve comme un rendu.
- **GPU :** facultatif ici aussi. Le fonctionnement sur CPU doit rester utilisable, comme pour l'idée de
  comportement adaptatif de l'[issue #22](https://github.com/KimntexX/IPR/issues/22).

## 5. Contrats à définir avec le moteur

**Le masque dans la recette.** Aujourd'hui, un masque est vectoriel et se recalcule à toute résolution. Un
masque de modèle est un champ de pixels. Deux possibilités :

| Option | Avantage | Inconvénient |
| --- | --- | --- |
| Polygones simplifiés dans la recette | Pas de fichier associé, se rouvre sans modèle, reste éditable | Perd les bords fins, cheveux et feuillage |
| Carte de pixels dans un actif associé | Fidèle au modèle | Introduit les actifs `.ipr-data`, prévus mais pas encore réalisés ([issue #21](https://github.com/KimntexX/IPR/issues/21)) |

**Je recommande de commencer par l'actif associé**, parce qu'un bord fin est tout l'intérêt d'un modèle, et
parce que B5 et B6 en auront besoin de toute façon. Il faudra alors un comportement défini si l'actif manque :
signaler et ignorer la forme, sans casser la recette.

**Les coordonnées.** L'analyse porte sur la source neutre, réduite. Le masque est porté par le canevas, après
géométrie. `LocalAdjustments::toSource` montre le sens inverse existant. Mieux vaut que le masque suive la
**photo**, pas le canevas, ce qui règlerait aussi la limite actuelle des masques et de la Retouche quand la
géométrie change après coup. À trancher en D3.

**Le cache.** Le résultat de l'encodeur dépend de la source, pas des réglages. Une clé par empreinte de
source, un plafond, et une perte sans conséquence : il se recalcule.

## 6. Validation

Gardée séparée de la qualité de reconstruction de B3, comme le demande la feuille de route.

- **Corpus :** les photos de l'utilisateur, séparées par séance et par boîtier, jamais des recadrages d'une
  même rafale. Une centaine d'objets annotés au pinceau dans IPR suffirait pour D2, répartis en classes de
  difficulté : objet net sur fond simple, feuillage et cheveux, objet fin, occlusion, faible lumière, bruit.
- **Mesures :** IoU et qualité de bord par classe, nombre de clics pour atteindre un seuil, erreur d'angle pour
  les lignes, temps de l'analyse et du clic, mémoire.
- **Témoin :** une sélection par plage de couleur et de luminance. Si le modèle ne le bat pas, il ne se justifie pas.
- **Abstention :** définir avant les données finales quand le système refuse de proposer.

## 7. Ce qu'il me faut de l'utilisateur

1. **Accord sur le périmètre** : clic et lignes d'abord, détection avec classes reportée. **Donné le 5 novembre 2026.**
2. **Annotation :** une séance pour peindre une centaine d'objets, quand un outil sera prêt. Pas tout de suite.
3. **Pour finir D1 :** des photos de bâtiments, façades et intérieurs, prises en contre-plongée, et un avis sur les
   propositions une fois raccordées à l'interface.
