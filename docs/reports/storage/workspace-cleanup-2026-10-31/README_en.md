<!-- translation-source: docs/reports/storage/workspace-cleanup-2026-10-31/README.md; sha256: 8ed31e4417ae9efc26bd322858a213154b24f62ca6924af9193f5a4b56cd71ed -->
*[Français](README.md) | [English](README_en.md)*

# Local cleanup and beta preparation — 31 October 2026

Cleanup explicitly requested by the user, as a one-off exception to the usual
retention of feature branches. No remote cleanup.

## Completed actions

Three inactive local branches deleted using `git branch -d`, after confirming
zero commits in `dev..branch` and no associated worktree:

| Branch | Previous tip, retained in dev history |
| --- | --- |
| `patch_color` | `27d7293` |
| `patch_in_memory_analysis` | `b6b3f28` |
| `patch_raw_opening` | `49112bf` |

GitHub branches were not deleted. Local references can be recreated from these
commits; no commit was removed from `dev`.

Deleted **413 `.obj` files, 58,608,199 bytes (about56 MiB)**, exclusively under
`.worktrees/patch_raw_bench/build-patch_raw_bench`. Each target's absolute path
was checked before deletion. A rebuild regenerates these compilation artifacts;
the objects were not backed up. Intermediate captures, executables, reports,
sources, images and JSON were retained.

## Deliberately retained workspaces

- `patch_documentation`: translations remain in progress;8 commits outside
  `dev` at inventory time. The documentation package is not declared complete.
- `patch_raw_bench`:23 commits outside `dev`, including bench tools to retain;
  copying some evidence into documentation does not integrate the tools.
- All other agents' active worktrees and branches, and untracked files in the
  main worktree: unchanged.
- Remote branches, tags, `dev` and `main`: no publication or merge.

This was targeted cleanup, not an exhaustive audit of all repository builds.
No automated deletion of image corpora or analysis evidence.

## Next

The [beta plan's B0 package](../../../architecture/roadmap-beta_en.md) describes
the first foundation tasks. Beta preparation does not waive remaining alpha
adjustment qualification; this cleanup declares no beta release.
