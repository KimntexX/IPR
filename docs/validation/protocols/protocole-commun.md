# Socle commun des protocoles photographiques

Version 1 — 10 octobre 2026. Statut : prêt pour les prochaines campagnes.
Ce document fixe la procédure, pas un verdict sur les images.

## 1. Préparer une comparaison interprétable

1. Choisir le groupe, le paramètre, les valeurs et les scènes. Définir avant
   mesure les critères et tolérances numériques ; une tolérance non déterminée
   reste à définir, elle ne vaut pas acceptation implicite.
2. Compiler le moteur et les tests ciblés : consulter le plan avec
   `bash scripts/build-windows.sh --dry-run`, puis lancer le build habituel.
   Une modification transversale ou la CI nécessite la suite complète.
3. Partir des mêmes RAW d'origine dans les deux moteurs. Ne jamais remplacer
   l'entrée IPR par le TIFF développé par Lightroom. Utiliser uniquement le
   corpus privé autorisé ; ne pas publier les images ni les profils Adobe.
4. Exporter un neutre frais par scène et par moteur. Profil de comparaison
   courant : **Camera Neutral**, sélection réellement appliquée et identité
   du DCP tracées ; ne pas mélanger les anciens Adobe Standard dans la série.
5. WB du RAW « Telle quelle / As Shot », WB automatique désactivée ; IPR garde
   sa température de référence et une nuance relative nulle. Pour un test WB,
   seule la commande étudiée varie. Des Kelvin identiques entre moteurs ne
   garantissent pas les mêmes gains ; zéro réglage ne signifie pas 0 K.
6. Garder constants normalisation RAW, LCP, options objectif/CA, cadrage et
   orientation. Déclarer toute correction intégrée obligatoire. La normalisation
   expérimentale est désactivée sauf campagne expressément dédiée.
7. Annuler EV, réglages tonals, couleur, mélangeur, grading, effets, bruit,
   quantité de netteté, masques, automatisme/IA et netteté de sortie hors
   paramètre testé. Courbes utilisateur identités. Vérifier les valeurs : un
   reset Lightroom peut conserver netteté/bruit et un sidecar IPR peut restaurer
   des retouches. Les paramètres de support non nuls sont déclarés explicitement.
8. Exporter **TIFF SDR sRGB 16 bits, ICC embarqué, dimensions natives, non
   compressé** pour les lecteurs actuels. Ni HDR ni JPEG pour ces mesures.

## 2. Générer et associer les états

Le contrat ci-dessous correspond à l'alignement de `patch_validation` du
12 octobre 2026. Recompiler cette branche avant de l'utiliser ; un ancien
exécutable ne possède pas les déclarations de provenance TIFF attendues.

Depuis PowerShell à la racine :

```powershell
./scripts/generate-test-images.ps1 couleur
# Ou tous les groupes :
./scripts/generate-test-images.ps1 all
```

Le générateur lit toutes les images à la racine de `tests/image/base`, sans
parcours récursif. Il dépose les TIFF directement dans les dossiers existants
partagés entre logiciels et crée seulement les sous-dossiers manquants.
Aucun dossier daté ni fichier auxiliaire. Les anciens arguments de campagne
ne sont plus acceptés. Voir les chemins, séries fixes et supports dans le
[contrat du générateur](../tools/generateur-images-tests.md).

Chaque nom IPR indique la source, le contrôle et la valeur, par exemple
`6V1A0327-IPR-shadows--50.tiff`. Un neutre frais par source et groupe est
généré dans `neutre`. Pour les axes WB manuels, les ancres sont les variantes
température 5200 et nuance 0 (support 5200 K / 0), pas ce neutre As Shot.
Lightroom est exporté séparément dans les mêmes dossiers, avec les mêmes
supports. Une même valeur affichée entre moteurs ne garantit pas le même blanc.

Les analyseurs acceptent `--base tests/image/base`. Ils lisent les déclarations
embarquées IPR et les contrôles XMP Lightroom, sans manifeste de génération.
Ne pas présenter un numéro de TIFF LR comme une preuve de son état. Si les
références sont ambiguës ou si le contrôle n'a pas d'audit XMP implémenté,
fournir une association explicite à l'analyseur ; ne pas inventer les valeurs.

Auditer ICC, profondeur, dimensions, orientation, transparence, profil, WB,
réglages Lightroom/XMP et provenance IPR. Un état manquant ou ambigu est exclu
et consigné ; jamais remplacé par une interpolation ni présenté comme neutre.
Tracer build/commit et empreinte exécutable, RAW et profils lorsque disponibles ;
une identité de profil commerciale seule n'est pas une empreinte.

## 3. Mesurer sans confondre rendu et erreur

Décoder la fonction de transfert sRGB avant les calculs en lumière linéaire :
`c_lin=c/12,92` pour `c≤0,04045`, sinon `((c+0,055)/1,055)^2,4`.
La luminance relative est `Y=0,2126R+0,7152G+0,0722B`.

Comparer la variante au neutre **du même moteur**, puis comparer les réponses
IPR/LR. Conserver métriques par scène/état/ROI, unités, dénominateurs, nombres
de pixels, exclusions et clipping (noirs, canaux saturés, pixels nouvellement
écrêtés). Les masques/ROI sont fixes pour la série ; les ratios indéfinis restent
indéfinis, notamment sur un noir ou une chroma nulle.

MAE/superposition entre moteurs seulement après vérification géométrique.
Une différence d'un pixel ou de correction LCP ne se résout pas par un simple
redimensionnement. Sans recalage fiable, utiliser des régions homologues ou
des réponses internes ; pas de MAE inter-moteurs présentée comme erreur absolue.
Les analyses de détail/bruit emploient des pixels natifs adjacents ; un aperçu
8 bits ou le proxy de zoom ne valide pas les détails pleine résolution.

Les seuils, échantillonnage, filtres, espace colorimétrique et incertitudes font
partie de la méthode enregistrée. Une moyenne satisfaisante peut masquer un
défaut local ; vérifier crops, transitions, peau et zones écrêtées.

## 4. Recette IHM et décision

Pour chaque commande : champ ↔ slider, bornes et clavier ; reset individuel
et global ; passage A→B→A ; fermeture/réouverture et restauration `.ipr` ;
paramètres de support ; aperçu stabilisé/export ; comparaison origine/actuel.
Le proxy peut différer pour les traitements spatiaux : consigner l'écart à
échelle comparable, ne pas exiger une identité entre résolutions différentes.

Publier séparément : **fonctionnement**, **qualité visuelle**, **proximité LR**,
et, seulement avec référence physique, **fidélité colorimétrique**. Une absence
de référence LR n'empêche pas un test interne mais limite le verdict comparatif.

## 5. Mémoire et clôture

Un protocole évolutif et un rapport évolutif par groupe. Enregistrer chaque
campagne dans un **JSON versionné et horodaté UTC immuable** sous
`docs/validation/raw/`, selon le [contrat de mémoire](../data/README.md).
Conserver les associations d'états, audits, données détaillées, conditions, limitations
et verdicts. CSV/courbes sont dérivés, pas la source de vérité.

Après analyse, l'utilisateur supprime **manuellement** les images. Aucun
nettoyage automatique n'est déclenché par le générateur ou par cette procédure.
Préserver les RAW et les JSON de mémoire ; vérifier les mesures détaillées et
la provenance avant nettoyage. Sans pixels, une nouvelle mesure nécessite une
régénération : un hash n'est pas une sauvegarde.
