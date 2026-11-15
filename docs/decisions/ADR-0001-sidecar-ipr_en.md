<!-- translation-source: docs/decisions/ADR-0001-sidecar-ipr.md; sha256: 8f6f3495dbdc7433fe78f840c70ab742a687193f3bc8640b13a5241a8c4b99bd -->
*[Français](ADR-0001-sidecar-ipr.md) | [English](ADR-0001-sidecar-ipr_en.md)*

# ADR-0001 — Non-destructive `.ipr` sidecar

- **Date:** 2 October 2026
- **Status:** Superseded by [ADR-0021](ADR-0021-socle-edition-beta.md) (CBOR format, 1 November 2026); files in this format are still read and converted
- **Decision makers:** IPR project

## Context

Settings and annotations must survive closure without modifying the RAW/image
file. They must also support batch application and invalidation when they no
longer match the source.

## Decision

Write an adjacent `<source>.ipr` file with `IPR1` header, version 3, source size
and modification date. Serialize with `QDataStream` Qt 6.4 and write atomically
through `QSaveFile` after 450 ms. The reader accepts versions 1/2: absent v1
noise/sharpening fields receive neutral values; absent v1/v2 orange is inserted
at zero without shifting the six previous colors. The next save writes v3.
Older application versions cannot read v3.

## Consequences

**4 October 2026:** [ADR-0005](ADR-0005-melangeur-huit-bandes-luminance-bornee.md)
moves writing to v4 with distinct purple/magenta and v1/v2/v3 migrations.
The original v3 decision above is historical, not the current format.

**8 October 2026:** [ADR-0006](ADR-0006-restauration-raw-et-retouche-profil.md)
moves writing to v5 to store per-image DCP/calibration choices and restore
white balance before decoding.

**9 October 2026:** v6 writing, v1–v6 reading: LCP identity/SHA-256, explicit
resolution and lens/lateral-chromatic-aberration switches. See the
[optics contract](../camera-profile-engine.md#persistance-et-commandes-optiques-9-octobre-2026).

- Processing remains non-destructive and independent of RAW/JPEG/TIFF formats.
- Source size/timestamp changes invalidate the sidecar rather than risk wrong settings.
- The binary format must be versioned and have migration for incompatible changes.
- Batch copying preserves existing destination annotations.

## Validation and links

- Remaining recipe: [validation matrix](../validation/matrice-validation.md).
- Design: [engine guide](../scholar/moteur-graphique-ipr.tex) and
  [historical session report](../reports/sessions/rapport-session-2026-10-02.md).
