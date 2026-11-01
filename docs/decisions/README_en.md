*[Français](README.md) | [English](README_en.md)*

# Architecture Decision Records (ADR)

An ADR records a choice that influences compatibility, rendering, data, or maintenance. It is concise, dated, and not rewritten to erase history: a replaced decision receives a new ADR that references it.

| ADR | Decision | Status |
| --- | --- | --- |
| [ADR-0001](ADR-0001-sidecar-ipr.md) | Non-destructive `.ipr` persistence | Accepted |
| [ADR-0002](ADR-0002-pipeline-lineaire-et-calibration.md) | Linear pipeline and rendering calibration | Accepted, to be revalidated on pipeline change |
| [ADR-0003](ADR-0003-profils-dcp-lcp-locaux.md) | Local DCP/LCP library | Accepted under license constraints |
| [ADR-0004](ADR-0004-preferences-ihm-ini.md) | Global UI preferences in user INI | Accepted |
| [ADR-0006](ADR-0006-restauration-raw-et-retouche-profil.md) | Sidecar v5 and DCP change without visual reopening | Accepted; visual recipe to be completed |

Use [the template](../templates/adr-template.md) for any new decision.

[ADR-0005](ADR-0005-melangeur-huit-bandes-luminance-bornee.md) complements ADRs 0001/0002/0004: eight bands, neutral protection, bounded luminance, sidecar v4, and INI schema 2 (October 4, 2026).
