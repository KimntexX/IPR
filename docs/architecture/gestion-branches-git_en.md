# Git Branch Management

Rule adopted on October 10, 2026.

## Organization

`dev` gathers validated feature groups. Each group has a permanent branch derived from `dev`: the existing `patch_light` branch follows Lights (exposure, contrast, shadows, blacks, whites, and highlights). Next groups will each have their branch, for example `patch_color` for color or `patch_noise` for noise; these names are examples, not branches already created.

A group branch is preserved after delivery. Merge commits materialize validations in the Git graph; tags identify frozen states. `main` remains distinct, and its update requires explicit delivery.

## Create or Resume a Group

Execute commands with a working state compatible with branch change. Preserve local modifications unrelated to the group; do not automatically add or delete them.

For a new group, update `dev`, then create its branch:

```bash
git switch dev
git pull --ff-only origin dev
git switch -c patch_group_name
git push -u origin patch_group_name
```

To resume an existing group, update `dev`, then merge it into the group branch. If multiple stations contribute, also update the group branch before merging:

```bash
git switch dev
git pull --ff-only origin dev
git switch patch_light
git pull --ff-only origin patch_light
git merge dev
```

In case of divergence or conflict, examine and resolve differences; do not force a pull, rebase published history, or overwrite a remote branch. Commits specific to the remote `dev` branch must also be preserved.

## Deliver After Validation

### Prototyping Versions

Rule clarified on October 11, 2026: the project is in alpha. Next validated merges into `dev` are identified as `v0.1.X-alpha`. The counter `X` increases by the number of delivered patches, not the number of commits. Examine existing tags and identify delivery patches before choosing the next number; do not create a tag on an unvalidated merge.

After validation of all implemented adjustment features, the first beta series uses `v0.2.X-beta`. The minor version then evolves with each integration of another feature panel: `v0.3.X-beta`, `v0.4.X-beta`, etc.

The transition to stable version **`v1.Y.X`** occurs after validation of all feature panels on the cameras the user currently owns. Document covered models, campaigns, and proofs before declaring this milestone validated by the user. This does not constitute universal validation for other cameras. `Y` denotes the minor version and `X` the patch version; no release schedule is imposed.

Historical tags already published are neither renamed nor moved.

Place the version tag on the validated merge commit of `dev`; publish this tag only within an authorized delivery. This documentation rule does not itself create a tag and does not authorize any merge or publication.

During prototyping, no legacy is preserved, even in tests. A replacement deletes the old implementation, its compatibility options, and tests that only concern it; current implementation tests are maintained. Git history and JSON analysis proof remain preserved: these are traces, not legacy execution paths.

### Delivery Procedure

Validate tests adapted to modified zones, results, and user agreement before delivery. Record proofs and limits in the group's single report, with versioned and timestamped JSON data.

After committing and publishing group changes:

```bash
git push origin patch_light
git switch dev
git pull --ff-only origin dev
git merge --no-ff patch_light -m "merge: integrate validated lighting changes into dev"
git push origin dev
```

Do not delete either the local or remote `patch_light` branch. For the next evolution, resume this same branch and integrate updated `dev` into it. Do not use squash, rebase published commits, or force push: the merge graph serves as tree-based tracking. Validation tags are neither moved nor reused.

## First Integration

The visually approved Lights version, commit `35d7c82`, identified by tag `validation-ombres-hl-2026-10-10`, has been merged into `dev` by commit `8e8165b`. The `patch_light` branch is preserved. This validation is limited to the current corpus; evolutions remain possible after testing with other cameras and/or lenses.
