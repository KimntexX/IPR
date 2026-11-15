<!-- translation-source: docs/decisions/ADR-0003-profils-dcp-lcp-locaux.md; sha256: 2577d6f698de04912a0ab2deded79b887fd9a0fbefa86f38347391a789223b4c -->
*[Français](ADR-0003-profils-dcp-lcp-locaux.md) | [English](ADR-0003-profils-dcp-lcp-locaux_en.md)*

# ADR-0003 — Local DCP/LCP profile library

- **Date:** 3 October 2026
- **Status:** Accepted subject to provenance/licensing constraints
- **Decision makers:** IPR project

## Context

Recursive profile discovery during loading slows opening. Camera DCP and lens
LCP profiles contribute to RAW-development fidelity, but redistribution may
be legally restricted by their origin.

## Decision

Keep explicitly authorized profiles under `profiles/dcp/` and `profiles/lcp/`,
with `profiles/index.json`. Prefer the index and fall back to configured
directories. Do not commit or redistribute third-party profiles without a
license granting those rights.

## Consequences

- Profile resolution becomes faster and deterministic.
- Update the index when adding/removing a profile.
- Every distributed profile needs verifiable provenance, license and distribution decision.
- Long-term alternatives include IPR-created profiles or a documented open
  profiling workflow without Adobe-profile dependency.

## Validation and links

- [DCP/LCP documentation](../camera-profile-engine.md)
- [RAW and profile course](../scholar/cours-09-raw-profils-appareil.tex)
- [Historical Adobe note](../architecture/references/note-utilisation-profils-adobe-camera-raw-2026-10-01.md)
