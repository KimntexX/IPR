<!-- translation-source: docs/decisions/ADR-0014-cache-profils-prepares.md; sha256: 8b1f0e96c7d229391785ad73c935839ddbc97e7ff02e3112a7fd31951d78bf57 -->
*[Français](ADR-0014-cache-profils-prepares.md) | [English](ADR-0014-cache-profils-prepares_en.md)*

# ADR-0014 — Bounded shared cache of prepared profiles

Date: 17 October 2026. Status: local implementation requested by the user,
qualified through tests and corpus replay; no implicit Git publication.

## Decision

Share parsed immutable DCP/LCP profiles, not a photo's parameters or pixels.
One `PreparedProfileCache` serves compatibility, profile discovery, DCP
rendering, camera WB edits and optical corrections. Replace previous parsers
with byte-snapshot parsers; retain no legacy option.

Initial shared budget: **64 MiB and 16 entries**, LRU eviction. Configurable
through C++, not a new UI setting. Accounting includes objects, vector/string
capacities and an allowance for auxiliary structures; a conservative estimate,
not exact Qt heap or process RSS measurement.

## Identity and invalidation

Key: parser type/revision + SHA-256 of bytes read. Identical profiles at
different paths can share tables; filename-dependent compatibility rules
still use the requested path.

Read/hash the file on every request. The cache avoids reparsing, **not all I/O**.
Same-size/timestamp modifications are detected; missing/unreadable sources
are not replaced with stale entries. Hashed bytes are exactly the parsed bytes.
Do not cache errors. Per-file reading limit remains 64 MiB.

## Concurrency and lifetime

A mutex protects index, LRU and counters. Reading, hashing and preparation
occur outside the lock; same-key preparation is shared with waiting callers
(single-flight). Different keys can prepare concurrently. Parser failure wakes
waiters. Internal loaders must not recursively request the same key.

Rendering holds a shared const reference. Eviction does not destroy in-use
objects. An over-budget profile is usable but not retained. Zero bytes/entries
disable retention, not rendering. Clearing/reconfiguring changes generation:
old preparation cannot repopulate the new generation.

**The bound covers store-retained references**, not bytes being read, evicted
objects still in use or image buffers. Track active concurrency/render
allocations separately; this is not an application-wide memory bound.

## Per-image state

WB/neutral, source/profile exposures, illuminant interpolation, capture
settings and optical remapping are not stored as mutable profile state.
R6/R7 do not share a rendering coefficient. Tables/matrices/models are read
through `shared_ptr<const ...>`. The cache changes neither rendering math
nor recipes.

Path-selection caches, characterization catalog, decoded-image LRU and active
camera buffer remain separate; this ADR replaces neither their budgets nor
their invalidation rules.

## Checks

Tests: hits, LRU eviction, byte/entry limits, oversized profile, deletion,
same-size/timestamp modification, eight readers, parser failure and clearing
during preparation. DCP fixtures compare no-retention/hot/modified output
and concurrent synthetic R6/R7 alternation. LCP fixtures check single
preparation and concurrent capture settings without contamination. These
are not newly physically characterized cameras.

The 19-RAW native replay stores cache counters in JSON. Overall decode times
cannot isolate cache benefit; claim no speedup percentage without a dedicated
benchmark.
