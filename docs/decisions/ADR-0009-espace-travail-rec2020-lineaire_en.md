<!-- translation-source: docs/decisions/ADR-0009-espace-travail-rec2020-lineaire.md; sha256: 4d813a015cac905e0b29cbddf234fc7ea833177b3a5e1a22bbd78763db7ae61a -->
*[Français](ADR-0009-espace-travail-rec2020-lineaire.md) | [English](ADR-0009-espace-travail-rec2020-lineaire_en.md)*

# ADR-0009 — Linear Rec.2020 working master

- Date: 15 October 2026.
- Status: accepted; master port implemented locally on 15 October 2026;
  visual/multi-OS qualification still required before delivery.
- Complements [ADR-0008](ADR-0008-separation-travail-affichage-export.md) and
  specifies the pipeline space in [ADR-0002](ADR-0002-pipeline-lineaire-et-calibration_en.md).

## Context

The seven-scene post-DCP comparison separates coordinate changes,
primary-dependent operators and earlier DCP-table losses. Neutral renditions
and invariant operations remain numerically equivalent. Rec.2020 is chosen
for the photographic master, not because it alone eliminates clipping or
reproduces Lightroom.

Evidence: [immutable v001 campaign](../validation/raw/comparatif-espaces-travail-v001-20261015T100527950069Z.json).

## Decision

- One Rec.2020-primary, D65, linear-transfer `CV_32FC4` master with straight
  alpha. Retain finite negative and>1 coordinates.
- Convert pixels; never merely replace their tag.
- Keep DCP tables in their intended ProPhoto domain. Rec.2020 concerns their
  output to the master, not reinterpretation of their coordinates.
- Initially retain current sRGB destinations. ADR-0008 display ICC remains
  separate; this decision enables neither PQ nor HLG.
- Requalify primary-dependent operators, restoration and outputs. Do not
  keep a selectable historical production engine. Preserve historical JSON
  evidence and diagnostic comparisons.

## Consequences and limits

The current code master is now linear Rec.2020. This port does not activate
default calibration, replace LibRaw or validate highlight trials. It requires
explicit conversions, renewed engine/cache identity and all photographic-group
tests before delivery. Float RGBA buffer size does not change; application
temporaries and compute time still need measurement.

The [integration plan](../architecture/integration-rec2020.md) retains packages,
risks and stop criteria, separating initial study from completed port.
No Git delivery or overall visual validation is declared here.
