# ADR-0015 — Contrôleur mémoire et cache d’images adaptatif

Date : 17 octobre 2026. Statut : accepté par l’utilisateur, intégration locale.

## Contexte

La [mesure d’ouverture](../validation/analyses/analyse-performance-ouverture.md)
identifie des entrées de 937–944 Mio, incompatibles avec l’ancien budget fixe
de l’éditeur à 256 Mio. Le témoin à 3 Gio accélère les retours, mais imposer
ce budget sans connaître la pression mémoire serait dangereux.

## Décision

`ImageCacheController` possède le LRU sur un `QThread` dédié. Aucun accès
direct concurrent au `QHash` du cache : commandes en file Qt, résultats par
`QFuture`, statistiques copiées sous mutex. Les attentes d’admission se font
sur les travailleurs de calcul, jamais sur le thread de l’IHM.

Le seuil de protection concerne l’utilisation **globale** de la mémoire :
80 %, avec reprise du préchargement sous 75 %. Il ne réserve pas 80 % de la
RAM à IPR et ne garantit pas que les autres applications resteront sous ce seuil.
Une image active ou un export gardent leurs références même après éviction
du cache. Le contrôleur ne suppose pas qu’une éviction libère immédiatement
de la RAM : il relit les informations système.

Le budget de rétention automatique est `min(25 % RAM effective, 8 Gio)` ;
une limite explicite peut le réduire. Cette borne est distincte du seuil
global de 80 %. La pression mémoire peut entraîner une rétention moindre.

Avant un décodage, rendu ou export interactif, réserver le coût de travail
estimé. Les réservations simultanées s’additionnent, avec 20 % de RAM laissée
disponible et une marge supplémentaire `max(128 Mio, 2 % RAM)`.
Les réservations sont rendues à la fin effective du travail, même si son
résultat est devenu obsolète. Si les données système manquent, refuser les nouvelles
allocations contrôlées et désactiver le cache/préchargement.

**Amendement du 17 octobre 2026, décidé par l’utilisateur.** Le refus immédiat
d’une ouverture au premier plan est remplacé par une attente. La première
campagne réelle (`latence-ouverture-v003`, en échec conservé) l’a montré :
une session ordinaire à 16 Go (navigateur, éditeur, antivirus) atteint déjà
82–85 % d’utilisation globale, donc aucun RAW ne s’ouvrait. Désormais :

1. Évincer les entrées LRU les plus anciennes, puis relire la mémoire système.
2. Si la place manque encore et qu’IPR détient des réservations (rendu, export,
   préchargement) ou du cache, mettre la demande **en attente** (file FIFO
   réévaluée toutes les 500 ms, à chaque restitution et à chaque demande).
   Un message indique la mémoire requise et la mémoire disponible.
3. Si IPR n’a plus rien à libérer, la pression vient des autres applications :
   admettre la demande **sous pression**, avec avertissement, tant que la marge de
   commit Windows (RAM + fichier d’échange ; RAM disponible hors Windows) couvre
   le pic plus la marge de sécurité. Sinon, l’attente continue.
4. Changer d’image annule l’attente périmée (jeton d’annulation) ; la fermeture
   abandonne toutes les attentes. L’image affichée reste en place pendant l’attente,
   et le panneau d’informations décrit l’image affichée, non la cible en attente.
5. Refus immédiat seulement si le pic estimé dépasse `80 % RAM − marge` d’une
   machine inactive, ou si la mémoire système est illisible.

Le préchargement et la rétention restent strictement soumis aux seuils 80/75 %.

**Second amendement du 17 octobre 2026**, après demande de l’utilisateur
de « lancer les étapes » de l’étude :

- **Rétention du proxy seul.** Une image quittée ne garde que son proxy 2048 px
  et le signal caméra de ce proxy, soit ≈ 73 Mio au lieu de ≈ 940 Mio en 32 MP.
  L’export reconstruit le master pleine résolution avec la recette courante ;
  un changement de DCP redéveloppe. Les dimensions affichées restent celles de la source.
- **Pression « établie ».** Les seuils 80/75 % s’appliquent à l’utilisation globale
  hors réservations en cours. Le pic planifié d’un traitement déjà admis n’évince
  donc plus les images retenues. Une pression durable, extérieure ou non, les évince toujours.
- **Pas d’éviction inutile.** Si vider tout le cache ne permettrait pas de loger
  le traitement, rien n’est évincé ; l’admission suit alors les règles d’attente
  ou de pression ci-dessus.
- **Aperçu du boîtier** affiché dès le clic sur un RAW, signalé comme tel, puis
  remplacé par le rendu IPR. Un échec restaure l’image précédente.

Ces deux règles changent la politique de pression : l’utilisateur peut les refuser.

Un seul travail anticipé à la fois, sur une file de calcul séparée : voisin
suivant puis précédent, après 250 ms d’inactivité du rendu. Aucun sidecar ni
retouche n’est écrit par ce travail. Un clic sur le fichier en cours de
préchargement peut promouvoir son résultat si la clé de développement reste
identique. Les autres résultats obsolètes sont abandonnés. LibRaw reçoit un
callback d’annulation coopérative ; une phase non interruptible peut retarder
la libération effective, sa réservation reste alors comptabilisée.

## Sources de mémoire et estimation

- Windows : `GlobalMemoryStatusEx`, RAM physique disponible et marge de commit.
- Linux : `MemAvailable`, `MemTotal`, limites cgroup v2 accessibles et ancêtres.
- macOS : `hw.memsize`, statistiques Mach, pages libres et inactives ; estimation
  prudente mais pas un équivalent exact de l’indicateur « pression mémoire » d’Apple.
- Le registre RAW expose un probe optionnel de dimensions. LibRaw ne lit que
  l’en-tête ; les formats raster utilisent `QImageReader::size()`.
- Estimation : `80 × pixels + 128 Mio`, recalibrée le 17 octobre 2026 sur
  la mesure du pic natif. Pour des CR3 de 32,3 MP (R6 Mark III et R7),
  le pic du processus atteint 2,19–2,21 Gio et la rétention 0,96 Gio ;
  l’estimation donne ≈ 2,53 Gio, soit environ 15 % de marge. L’ancienne
  provision forfaitaire `96 × pixels` surestimait le pic d’environ 45 %.
  Si les dimensions sont inconnues : `max(3 Gio, 96 × taille du fichier)`,
  avec saturation des entiers. Ce ne sont pas des bornes mathématiques des
  allocations internes de bibliothèques tierces ; deux boîtiers seulement sont mesurés.

## Conséquences et limites

Le rendu Rec.2020 et les constantes RAW/DCP ne changent pas. Les caches de
profils et de vignettes restent distincts ; leur consommation est visible
dans les mesures système, mais ce contrôleur ne les évince pas directement.
Le générateur hors ligne utilise son chemin de calcul statique, pas le cache
de navigation de l’IHM.

Un échantillonnage à 500 ms, même avec admission préalable, ne peut garantir
« jamais 80 % » : applications externes, allocations non instrumentées,
GPU à mémoire partagée et erreurs d’estimation peuvent faire dépasser la cible.
Ne jamais présenter cette politique comme un plafond matériel imposé au système.

Les tests synthétiques injectent une mémoire déterministe ; ils testent
séparément pression, évictions, priorités, restauration et concurrence. Le
sampler natif Windows a son contrôle propre. Les chemins Linux/macOS doivent
encore être compilés et exécutés sur leurs plateformes avant validation.

Sources : [Qt — threads et objets](https://doc.qt.io/qt-6/threads-qobject.html),
[Windows — MEMORYSTATUSEX](https://learn.microsoft.com/windows/win32/api/sysinfoapi/ns-sysinfoapi-memorystatusex),
[Linux — /proc](https://docs.kernel.org/filesystems/proc.html).
