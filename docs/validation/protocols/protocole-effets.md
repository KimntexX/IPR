# Protocole — Effets

Version 1 — 10 octobre 2026. Fusion Texture/Clarté/Voile ; ajout Grain/Flou.
Statut : recette prête ; nouvelles campagnes à effectuer sur le pipeline courant.
Appliquer le [socle commun](protocole-commun.md).

## Corpus et états

0327 : rocher, peau et tissus ; 0043 : fleur/feuillage ; 0007 : aplats et ciel ;
9944 ou 9947 : contre-jour et contours. Ajouter une scène avec voile réellement
visible et une sans voile comme contrôle négatif. Réduction de bruit, netteté
et effets voisins à zéro ; une commande à la fois.

| Contrôle | Série |
| --- | --- |
| `texture`, `clarity`, `dehaze` | −100, −50, −25, 0, +25, +50, +100 |
| `grain` | 0, 25, 50, 75, 100 |
| `blur` | 0, 1, 5, 10, 20 |

Le groupe générateur `effets` couvre ces cinq commandes. Utiliser leurs valeurs
propres par défaut, pas une série commune incompatible avec grain/flou.

## Mesures et recette

- Texture : ROI natives fixes, par exemple 512×512 ; énergie du résidu à un
  flou 3×3 et des gradients de Y. Attendre une action opposée selon le signe,
  contrôler la conservation de vraie texture et l'amplification du bruit.
- Clarté : contraste local à plus grande échelle ; résidu à lissage 17×17,
  gradients, Y et chroma. Examiner halos sur bords clair/foncé et contre-jour,
  ombres profondes/HL et différence avec Texture.
- Voile : P05/P50/P95 de Y, étendue P95−P05, chroma et clipping. Signe cohérent
  du contraste sur la scène voilée ; contrôler faux positifs sur scène claire.
  Le curseur ne peut pas être validé par la seule hausse du contraste.
- Grain : écart au neutre, variance/structure haute fréquence par ROI et
  dépendance à Y ; répétitions avec même état pour documenter déterminisme ou
  variabilité. Un grain artistique ne se juge pas comme un défaut de bruit.
- Flou : baisse des hautes fréquences et gradients, profil de bord, comportement
  aux limites ; vérifier absence de décalage, bord noir et contamination alpha.
- Tous : identité à zéro, progressivité, ratios Y/chroma, nouveaux clips,
  absence de bandes, rendu peau/textile et aperçu/export à échelle comparable.
  Définir avant mesure les tolérances et le choix esthétique Grain/Flou.

## Outils et limites

L'[analyseur de groupe](../tools/analyseurs-groupes.md), `tests/analyse_effets.py`,
mesure les cinq contrôles en natif 16 bits/JSON : contraste, clips et détail
dans des ROI adjacentes. Les scripts de curseurs remplacés et leur chemin de
compatibilité ont été supprimés. Les résidus binomiaux nouveaux diffèrent des filtres
historiques : comparer seulement des méthodes compatibles. Halos, profils
de bord et déterminisme du grain restent des recettes complémentaires.

Rapport Effets unique à ouvrir lors de la prochaine campagne, avec sous-sections
par commande ; anciennes analyses Texture/Clarté/Voile conservées comme preuves
historiques. Suivre le socle pour IHM, verdict et nettoyage.
