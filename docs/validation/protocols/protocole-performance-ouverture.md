# Performance — ouverture et changement d’image

## Objectif

Mesurer la latence du chemin courant sans modifier ses algorithmes ni ses
réglages de rendu. Une mesure de temps ne constitue pas une validation couleur.
Les JSON versionnés sous `../raw/` sont la mémoire canonique ; le rapport
évolutif est [Performance d’ouverture](../analyses/analyse-performance-ouverture.md).

## Procédure reproductible

1. Fermer les générations/exports concurrents pour une mesure contrôlée.
2. Compiler la sonde manuelle (exclue du build par défaut et de CTest) :

   ```powershell
   cmake --build build-windows-msvc --config Release --parallel 4 --target openingLatencyProbe
   python tests/measure_opening_latency.py
   ```

3. Le lanceur mesure trois imports successifs de `6V1A0081`, `6V1A0043` et
   `7Y6A7599`, avec Camera Neutral, calibration active et optique désactivée.
   Chaque scène démarre dans un processus distinct ; les deuxième/troisième
   imports réutilisent les caches de ce processus. Aucun cache disque système
   n’est vidé : ne pas qualifier le premier import de « disque froid ».
4. Mesurer séparément import complet, construction du proxy, rendu du couple
   origine/actuel avec histogramme, puis coût du contrôle d’un master déjà chargé.
   `IPR_LOG_RAW_TIMING` détaille l’import : ouverture LibRaw, sélection des profils,
   décompression, niveaux noirs/WB/calibration, traitement LibRaw, image mémoire,
   couleur/optique, fin de décodage.
5. Le lanceur copie seulement ces trois RAW dans un dossier privé sous
   `build-latency-investigation/`. La navigation **réelle de l’éditeur** y suit
   `A → B → A → C → A → B → A`, dossier privé ouvert pour activer le
   préchargement réel. Trois modes, chacun dans un processus distinct :
   limite `0` (témoin sans rétention ni préchargement), `auto` enchaîné
   sans pause, puis `auto` avec pause jusqu’à la fin du préchargement
   (au plus 20 s par étape). Les `.ipr` temporaires sont initialisés avec
   des recettes connues ; les originaux et leurs `.ipr` ne sont pas ouverts
   en écriture. Les copies sont conservées.
6. Comparer temps de publication de l’aperçu, état prêt, hits/misses,
   évictions, refus, budget effectif, réservations, pression, compteurs
   mémoire Windows du processus (working set, commit et leurs pics) et
   utilisation globale maximale échantillonnée toutes les 50 ms.
   `auto` vaut `min(25 % RAM, 8 Gio)` **sur la machine mesurée** seulement.
7. Vérifier les empreintes des entrées avant/après. Toute erreur interrompt la
   campagne ; une mémoire en échec est conservée, sans en tirer de validation.

Qualifier aussi le cache de sélection persistant : `status=0` dans les logs
QSettings signifie une écriture réussie ; `status=1` indique un refus d’accès.
Une mesure sous sandbox refusant l’écriture dans le cache applicatif n’est pas
équivalente à une session utilisateur normale. Conserver cette première preuve,
puis refaire une campagne autorisée avec les permissions normales, sans effacer
le cache utilisateur. La navigation contrôlée restaure ici une recette à
5200 K / nuance 0 ; la décomposition directe utilise la température As Shot
estimée par l’importeur. Ne pas comparer leurs temps de rendu comme des pixels
strictement identiques.

## Interprétation et limites

## Contrôleur mémoire et préchargement — intégration

Après compilation Release complète, lancer :

```powershell
python tests/verify_memory_cache_integration.py
```

Ce contrôle crée une preuve JSON versionnée sans ouvrir les photos utilisateur.
Les tests injectent la RAM pour rendre les cas reproductibles : pression
80/75 %, réservations concurrentes, limite de commit, éviction et conservation
des références actives. L’intégration éditeur teste promotion du préchargement,
annulation, retouches restaurées, absence de sidecars spéculatifs et refus
d’ouverture sans perte de l’image/recette courante. Un contrôle natif distinct
vérifie le sampler Windows dans la campagne actuelle.

Pour une validation manuelle : ouvrir un dossier, attendre le chargement d’un
voisin, puis naviguer suivant/précédent et observer le compteur au pied de
fenêtre. Vérifier un dossier mixte boîtier/objectif et le maintien des réglages
propres à chaque photo. Ne pas provoquer volontairement un épuisement de RAM
sur les photos de travail ; les cas extrêmes sont couverts par injection.
Si une ouverture attend de la mémoire, l’image précédente doit rester affichée,
avec ses informations, et la cible ne doit pas recevoir ses réglages. Changer
d’image pendant l’attente doit l’annuler. Une ouverture admise sous pression
doit l’indiquer dans le message « Image prête ».

Mesurer sur corpus les pics mémoire réels, le budget effectif, les attentes,
les admissions sous pression et la latence, sans confondre un ancien aperçu
conservé avec une ouverture réussie. La sonde vérifie l’identité de l’image
affichée. Relever l’utilisation globale de départ : elle conditionne le résultat.

## Interprétation et limites de temps

Le temps « prêt » s’arrête au moteur/provider CPU. La première présentation QML,
l’upload GPU, le rafraîchissement de l’écran, les vignettes et le parcours d’un
dossier restent hors de cette sonde. La première ouverture de l’application et
la contention avec un générateur peuvent donc être plus lentes. Mesurer ces
éléments séparément avant d’annoncer un temps utilisateur exhaustif.

Ne pas confondre cache des **profils préparés** et cache des **pixels décodés**.
Un profil en cache évite sa lecture/interprétation répétée partiellement, mais
n’évite ni le dématriçage ni son application sur tous les pixels. Un RAW trop
gros pour le budget n’est pas inséré dans le cache de pixels.

Les durées sont en ms, les tailles en octets ; Mio = 2^20 octets, Gio = 2^30.
Trois répétitions et trois scènes ne constituent pas une étude statistique
exhaustive. Conserver les mesures individuelles, les logs, la configuration,
les SHA-256 du binaire, des sources et des ressources. Aucun nettoyage automatique.
