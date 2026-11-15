<!-- translation-source: docs/decisions/README.md; sha256: 6ce2f4b81f90a038d92908a49c3f33e089fdddc91f7b4b7da44fa465513fa7d6 -->
*[Français](README.md) | [English](README_en.md)*

# Architecture Decision Records (ADR)

An ADR records a choice that influences compatibility, rendering, data, or maintenance. It is concise, dated, and not rewritten to erase history: a replaced decision receives a new ADR that references it.

| ADR | Decision | Status |
| --- | --- | --- |
| [ADR-0001](ADR-0001-sidecar-ipr.md) | Non-destructive `.ipr` persistence | Superseded by ADR-0021 (CBOR format) |
| [ADR-0002](ADR-0002-pipeline-lineaire-et-calibration.md) | Linear pipeline and rendering calibration | Accepted, to be revalidated on pipeline change |
| [ADR-0003](ADR-0003-profils-dcp-lcp-locaux.md) | Local DCP/LCP library | Accepted under license constraints |
| [ADR-0004](ADR-0004-preferences-ihm-ini.md) | Global UI preferences in user INI | Accepted |
| [ADR-0006](ADR-0006-restauration-raw-et-retouche-profil.md) | Sidecar v5 and DCP change without visual reopening | Accepted; visual recipe to be completed |
| [ADR-0007](ADR-0007-calibration-raw-par-defaut_en.md) | RAW calibration enabled by default, advanced disablement | Default/per-image panel integrated in dev; 22 October review |
| [ADR-0008](ADR-0008-separation-travail-affichage-export_en.md) | Screen-independent working space, separate ICC display/export | Accepted directive; implementation pending |
| [ADR-0009](ADR-0009-espace-travail-rec2020-lineaire_en.md) | Floating linear D65 Rec.2020 master | Integrated in dev; photographic/multi-OS campaign qualification |
| [ADR-0010](ADR-0010-compensation-rendu-par-profil-dcp_en.md) | Camera/DCP rendering compensation separate from source baseline | Integrated; scope extended by ADR-0011 |
| [ADR-0011](ADR-0011-regles-raw-par-mode-et-configuration-vivante_en.md) | Model/sensor-mode rules without photo lists; live configuration | Integrated; other ISO on trial, not universal validation |
| [ADR-0012](ADR-0012-formats-extensibles-et-caracterisations_en.md) | Format/thumbnail adapters and characterization JSON separate from DCP | Adapters, CameraSignal and common import integrated; independent physical characterization pending |

Use [the template](../templates/adr-template.md) for any new decision.

[ADR-0014](ADR-0014-cache-profils-prepares_en.md): shared DCP/LCP LRU,
content identity, concurrent preparation and memory bounds.

[ADR-0015](ADR-0015-controle-memoire-et-prechargement_en.md): dedicated controller,
global 80/75% thresholds, reservations, adaptive image cache and prefetch.

[ADR-0016](ADR-0016-minimum-qt-6-8_en.md): Qt6.8 minimum throughout, Linux SDK6.8.3,
Debian13 `.deb`, no Qt<6.8 path.

[ADR-0017](ADR-0017-imageeditor-cadenceur-modules_en.md): `ImageEditor` module
coordinator, `.h`/`.cpp`, free functions and standard guards(19 October2026).

[ADR-0020](ADR-0020-lecture-permanente-des-noirs_en.md): per-RAW optical-black
measurement compared with LibRaw, replacing missing/deviating values(20 October2026).

[ADR-0019](ADR-0019-analyses-ipr-en-memoire_en.md): in-memory IPR analysis states,
TIFFs reserved for visual review(20 October2026).

[ADR-0018](ADR-0018-independance-decodage-raw-et-couche-image_en.md): IPR RAW
decoders then image layer; initial20October direction revised31October:
floating development integrated, Canon decompression to qualify, LibRaw retained
for other brands.

[ADR-0013](ADR-0013-contrat-import-commun_en.md): shared RAW/developed-image
import boundary, independent metadata and common checks.

[ADR-0005](ADR-0005-melangeur-huit-bandes-luminance-bornee.md) complements ADRs 0001/0002/0004: eight bands, neutral protection, bounded luminance, sidecar v4, and INI schema 2 (October 4, 2026).

[ADR-0021](ADR-0021-socle-edition-beta.md), in French: editing foundation of the beta — edits described in the `.ipr`, heavy results in `<photo>.ipr-data/`, a saved history of 100 steps, geometry after the optics and layers after the settings, a format with a header and sections, evolution without breaks from the stable version (1 November 2026).
