<!-- translation-source: docs/decisions/ADR-0015-controle-memoire-et-prechargement.md; sha256: 3d6b1704d57a7babc6f6e12dfcc2f1bcea34f753fd5dfe6f4a801f3e3c01309e -->
*[Français](ADR-0015-controle-memoire-et-prechargement.md) | [English](ADR-0015-controle-memoire-et-prechargement_en.md)*

# ADR-0015 — Memory controller and adaptive image cache

Date: 17 October 2026. Status: user-approved, locally integrated.

## Context

[Opening measurements](../validation/analyses/analyse-performance-ouverture.md)
find 937–944 MiB entries, incompatible with the old fixed 256 MiB editor
budget. A 3 GiB control speeds returns, but forcing that budget without
memory-pressure knowledge would be unsafe.

## Decision

`ImageCacheController` owns LRU on a dedicated `QThread`. No concurrent direct
`QHash` access: queued Qt commands, `QFuture` results, statistics copied under
mutex. Admission waits happen on compute workers, never UI thread.

Protection concerns **global** memory use: 80%, resuming prefetch below 75%.
It neither assigns 80% of RAM to IPR nor guarantees other applications stay
below it. Active images/exports retain references after eviction. Re-read
system data rather than assume eviction immediately frees RAM.

Automatic retention budget: `min(25% effective RAM,8 GiB)`, reducible by
explicit limit. This differs from the global 80% threshold. Pressure can
reduce retention further.

Reserve estimated workspace before interactive decode/render/export.
Concurrent reservations add up, leaving 20% RAM free plus
`max(128 MiB,2% RAM)` margin. Release only when work actually ends, even if
its result is stale. Without system memory data, refuse new controlled
allocations and disable cache/prefetch.

**User-decided amendment, 17 October 2026:** replace immediate foreground
opening refusal with waiting. Failed retained real campaign `latence-ouverture-v003`
showed an ordinary 16 GB session(browser, editor, antivirus) already at
82–85% global use, preventing every RAW from opening. Now:

1. Evict oldest LRU entries and re-read system memory.
2. If still insufficient and IPR holds reservations(render/export/prefetch)
   or cache, **wait** in FIFO, reevaluated every 500 ms and on release/request.
   Report required/available memory.
3. If IPR has nothing left to release, pressure comes from other applications:
   **admit under pressure**, with warning, while Windows commit headroom
   (RAM+pagefile; available RAM elsewhere) covers peak plus safety margin.
   Otherwise continue waiting.
4. Image changes cancel stale waits; closure abandons all waits. Keep current
   image displayed; Information describes it, not the waiting target.
5. Immediately refuse only when estimated peak exceeds idle-machine
   `80% RAM−margin`, or system data cannot be read.

Prefetch/retention remain strictly governed by 80/75% thresholds.

**Second 17 October amendment**, following the user's request to run study steps:

- **Proxy-only retention:** leaving an image keeps its 2048px proxy/camera
  signal, about 73 MiB rather than 940 MiB at 32 MP. Export rebuilds full
  master with current recipe; DCP changes redevelop. Display real source dimensions.
- **Established pressure:** apply 80/75% to global use excluding current
  reservations. Already admitted planned peaks no longer evict retained images;
  sustained pressure still does, external or otherwise.
- **No pointless eviction:** if clearing all cache cannot fit the task,
  evict nothing; use waiting/pressure rules above.
- **Camera preview:** display badged embedded preview on RAW click, then
  replace with IPR rendition. Failure restores previous image.

These pressure-policy rules may be rejected by the user.

One speculative task at a time on a separate queue: next neighbor, then
previous, after 250 ms render inactivity. It writes no sidecar/edits.
Clicking the prefetched file can promote it if development key still matches;
discard other stale results. LibRaw gets cooperative cancellation; a
non-interruptible phase may delay actual release, so keep its reservation counted.

## Memory sources and estimates

- Windows: `GlobalMemoryStatusEx`, available physical RAM and commit headroom.
- Linux: `MemAvailable`, `MemTotal`, accessible cgroup-v2 limits/ancestors.
- macOS: `hw.memsize`, Mach statistics, free/inactive pages; conservative
  estimate, not exact Apple “memory pressure”.
- RAW registry exposes optional dimensions probe. LibRaw reads only header;
  raster uses `QImageReader::size()`.
- Estimate: `80×pixels+128 MiB`, recalibrated 17 October from native peak.
  32.3 MP R6 Mark III/R7 CR3 process peak 2.19–2.21 GiB, retention 0.96 GiB;
  estimate about 2.53 GiB, roughly 15% margin. Old `96×pixels` overestimated
  peak by about 45%. Unknown dimensions: `max(3 GiB,96×file size)`, saturating
  integers. Not mathematical bounds on third-party allocations; only two
  cameras were measured.

## Consequences and limits

Rec.2020 rendering and RAW/DCP constants stay unchanged. Profile/thumbnail
caches remain separate; system measurements see their memory but this
controller does not directly evict them. Offline generator uses its static
compute path, not UI navigation cache.

Sampling every 500 ms, even with admission, cannot guarantee “never 80%”:
external apps, uninstrumented allocations, shared-memory GPU and estimation
errors can exceed target. Never present this as an OS-enforced hardware cap.

Synthetic tests inject deterministic memory and separately test pressure,
eviction, priority, restoration and concurrency. Native Windows sampler has
its own check. Linux/macOS still need native build/execution before validation.

Sources: [Qt threads/objects](https://doc.qt.io/qt-6/threads-qobject.html),
[Windows MEMORYSTATUSEX](https://learn.microsoft.com/windows/win32/api/sysinfoapi/ns-sysinfoapi-memorystatusex),
[Linux /proc](https://docs.kernel.org/filesystems/proc.html).
