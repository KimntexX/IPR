*[Français](note-utilisation-profils-adobe-camera-raw-2026-10-01.md) | [English](note-utilisation-profils-adobe-camera-raw-2026-10-01_en.md)*

# Note de licence — profils Adobe Camera Raw DCP/LCP

## Conclusion prudente

La recherche n'a trouvé **aucune licence Adobe primaire qui autorise
explicitement IPR à redistribuer sur GitHub** les fichiers `.dcp` et `.lcp`
installés dans `C:\ProgramData\Adobe\CameraRaw`. Ne pas les committer, les
inclure dans un installateur, ni fournir un « fork » de ce dossier sans une
autorisation écrite d'Adobe ou une licence accompagnant précisément les
fichiers concernés.

Cette note est une analyse technique de sources publiques, pas un avis
juridique. Une distribution commerciale ou publique doit être validée par un
conseil juridique et, idéalement, par Adobe.

## Ce que les sources Adobe autorisent ou établissent

- Adobe documente l'installation manuelle de DCP dans
  `C:\ProgramData\Adobe\CameraRaw\CameraProfiles` afin qu'ils soient utilisés
  par Camera Raw. Cela établit un usage et une copie locale dans l'écosystème
  Adobe, pas un droit de redistribution par une autre application.
  [Instructions Adobe pour les profils DCP](https://helpx.adobe.com/camera-raw/desktop/edit-and-enhance-images/tone-and-color/adjust-color-rendering-camera-camera.html)
- La licence de brevet de la spécification DNG couvre la mise en œuvre qui lit
  et écrit des fichiers DNG conformes. Elle ne concède pas une licence sur les
  tables créatives ou les fichiers de profils Adobe distribués avec Camera Raw.
  [Spécification et licence DNG Adobe](https://helpx.adobe.com/camera-raw/desktop/dng-and-file-formats/digital-negative.html)
- Adobe publie une licence de *bundling* distincte pour son profil ICC Adobe
  RGB, avec conditions de redistribution précises. Elle ne vise pas les DCP ou
  LCP, mais confirme qu'il ne faut pas présumer un droit de redistribution pour
  un profil Adobe à partir du seul fait qu'il est installé localement.
  [Licence de distribution Adobe RGB ICC](https://www.adobe.com/support/downloads/iccprofiles/icc_eula_win_dist.html)

## Politique IPR appliquée

1. IPR lit les profils Adobe directement à leur emplacement installé : aucune
   copie, modification ou distribution par IPR.
2. `profiles/dcp/` et `profiles/lcp/` sont ignorés par Git. Ils sont réservés
   aux profils dont l'utilisateur ou le projet a vérifié la provenance et le
   droit d'emploi.
3. Le dépôt ne versionne que `profiles/index.json`, des `.gitkeep`, des hashes
   éventuels et des profils synthétiques de test.
4. Pour publier un profil, conserver sa licence, son auteur, son origine et la
   preuve du droit de redistribution dans un manifeste séparé.

Les profils créés par IPR à partir de mesures propres, ou obtenus sous une
licence de redistribution compatible, peuvent être gérés séparément après
vérification de cette licence.
