*[Français](INDEX.md) | [English](INDEX_en.md)*

# IPR Documentation Index

This file is the single entry point for versioned documentation. It distinguishes between documentation that defines the product, evidence that validates it, and logs that explain its evolution.

| Domain | Role | Entry Point |
| --- | --- | --- |
| Architecture | Durable structure, contracts, and diagrams | [architecture/README.md](architecture/README.md) |
| Specifications | Expected behavior, independent of any work session | [specifications/README.md](specifications/README.md) |
| Decisions | Stable architectural choices and their consequences | [decisions/README.md](decisions/README.md) |
| Validation | Protocols, status matrix, and reproducible measurements | [validation/README.md](validation/README.md) |
| Reports | Session logs and dated histories | [reports/README.md](reports/README.md) |
| Technical | LaTeX reference manual and compilable sources | [technical/latex/README.md](technical/latex/README.md) |
| Scholar | Courses on models used by IPR | [scholar/README.md](scholar/README.md) |

## Governance Rules

Document formatting: [common LaTeX conventions](AGENTS.md).

Background courses: [digital image architecture — PDF](scholar/cours-11-architecture-images.pdf)
and [associated practical work](scholar/README.md#cours-11--architecture-des-images).

Practical guide: [testing features autonomously](validation/procedure-tests-autonomes.md).

Tracking: [CSV and curves](validation/metrics/README.md),
[prioritized backlog](reports/sessions/session-2026-10-08.md).

1. A session report records dated facts; it does not become a specification through accumulation.
2. A completed feature updates its specification, the validation matrix, and if necessary, an ADR.
3. Raw measurement files remain immutable and are separated from their conclusions.
4. A decision that impacts compatibility, rendering, or data format receives a numbered ADR.
5. Markdown/LaTeX sources are versioned. Outputs in `build/`, caches, and synchronized files are generated locally and ignored by Git. Scholar PDFs are the exception: they are produced at the root of `scholar/` for direct consultation.

## Migration Completed

Historical content, previously grouped in `docs/rapport/`, was distributed on October 3, 2026 into appropriate responsibility areas. This old folder was then deleted. The registry preserves the source-destination correspondence: [reports/migration-registry.md](reports/migration-registry.md).
