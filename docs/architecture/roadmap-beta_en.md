<!-- translation-source: docs/architecture/roadmap-beta.md; sha256: aa83c3e33fec27550cba277b47e30ff449ce15e849c57818bae753f52ec91b55 -->
*[Français](roadmap-beta.md) | [English](roadmap-beta_en.md)*

# Beta action plan — editing panels and neural processing

Planning date: 31 October 2026. Status: **proposed plan, not implemented**.
User-requested scope: geometry, object removal, layers/filters, neural networks
for noise/sharpening and for shapes/objects, stacking, horizontal/vertical
panoramas and HDR merging. “Shapes” means geometric shapes
within the image, as confirmed by the user; this does not concern file-format
identification.

This document owns the roadmap for these work packages. The
[function inventory](roadmap-fonctions-lightroom.md) remains the coverage
inventory, the [matrix](../validation/matrice-validation.md) records validations,
and reports/JSON hold evidence. Planned does not mean available or validated.

## 1. Goal and entry conditions

Keep photo development usable without AI, then add non-destructive local
operations and **local** neural assistants. Lightroom may remain a visual
reference, not a copying target or algorithmic specification.

Before the first beta milestone:

- finish qualifying already implemented adjustments under the project rule;
  stopping noise research is not equivalent to that qualification;
- stabilize current noise reduction without integrating rejected bench variants,
  following the [31 October decision, §54](../validation/analyses/analyse-developpement-raw.md#54-stabilisation-et-orientation-neuronale-pour-la-bêta--31-octobre-2026);
- freeze the reference state, known limits and regression recipes;
- qualify saving/restoration and preview/export behavior.

The first beta series is `v0.2.X-beta`. New panel integrations increment the
minor version according to the [Git rules](gestion-branches-git.md).
Internal technical packages or individual commits do not automatically count
as panels or minor releases. Release numbers are settled upon validation;
this plan creates no tag.

## 2. Work order and dependencies

| Package | Deliverable | Dependencies | Validation gate |
| --- | --- | --- | --- |
| B0 | Editing, recipe and long-task contracts | Qualified alpha reference | Undo/restoration; unchanged rendering without new operations |
| B1 | Manual Geometry panel | B0 | Accurate placement, crop and export consistency |
| B2 | Non-destructive layers, masks and filters | B0, B1 coordinate system | Reproducible composition and persistence |
| B3 | Manual object removal | B2 | Local editing, clean boundaries and undo |
| B4 | Neural restoration: noise first, then sharpening | B0, RAW contract, resource management | Qualified visual/detail benefit on reference machine |
| B5 | Shape detection and object segmentation | B0; B1/B2 connections | Correctable selections/proposals; measured errors |
| B6 | Detection-assisted removal | B3 + B5 | Assisted selection without unconfirmed automatic edits |
| B7 | Multi-image stacking with user-selected reference | B0, multi-source contract and disk staging | Registration, valid crop and benefit without detail destruction |
| B8 | Horizontal/vertical panorama, then multi-row | B7 multi-source foundation, B1 coordinates | Qualified geometry, seams, coverage and resources |
| B9 | HDR exposure merging, then HDR panorama | B7 multi-source foundation; B8 for HDR panorama | Controlled exposures, saturation, motion and dynamic-range rendering |

B4 need not wait for layers for an independent initial study, but integration
must respect the same task, history and provenance system. B5 can be prepared
after the foundation; it does not block B1/B3 manual tools. This plan starts
no training or parallel implementation.

## 3. B0 — shared foundation before panels

Define engine contracts before QML connections:

Include a multi-source recipe and disk assets for B7/B8/B9: reference, input
identities, alignments, exposure, validity masks and crop. Plan disk staging
in the foundation rather than adding it as a late extension.

- immutable source, non-destructive recipe, undoable commands and session
  restoration; canceled/failed operations must not publish partial results;
- explicit coordinate spaces: source, oriented image, document geometry,
  preview and screen. Preserve inverse transforms for brushes, handles,
  masks and selections after zoom/crop;
- documented rendering order and color domains. Keep the Rec.2020 working
  master and explicit model adapters; do not arbitrarily move every module
  into a network's space;
- history/recipe references to masks, operations and large computed results.
  Plan project-associated assets instead of indefinitely embedding serialized
  pixels in sidecars; decide storage through an ADR before implementation;
- invalidate caches by source, recipe, geometry, mask, model and parameters;
  stale asynchronous results must not replace the current image;
- progress, cancellation, task queue and RAM/VRAM reservations; reuse existing
  controllers rather than multiplying isolated budgets;
- export contract: full resolution, alpha limits, same composition order as
  preview, and no accidental export of provisional results.

Deliverables: contract ADRs, recipe schema, engine tests and short interfaces.
`ImageEditor` remains the coordinator; no photographic computation in QML.
Tests: identity without operations, undo/redo, closure during a task, invalid
recipe, missing asset, disk failure and no source-file modification.

## 4. B1 — Geometry settings menu

### Initial manual integration

Group free/aspect-ratio crop, rotation/straightening, horizontal/vertical flip,
vertical/horizontal perspective, scale, aspect and offsets. Provide a grid,
handles, numeric input, reset per action and panel reset. Explicitly handle
empty areas: retain supported transparency or constrain/crop, without silent
invented fill.

Define orientation/optics/geometry order and avoid unnecessary successive
resampling. Qualify interpolation and transform limits; a homography must
not be presented as a universal optical-distortion corrector. LCP profiles
retain their own domain.

The initial version is manual. Line-guided straightening may follow;
automatic B5 suggestions remain a distinct step.

Validation: grids and known landmarks, rotations/flips, inverse transforms,
crop limits, masks after transformations, saving/reloading and full-resolution
export. Freeze tolerances before campaigns; measure edge sharpness, aliasing
and absence of preview/export misalignment.

## 5. B2 — Layers and Filters menu

First increment: groups, adjustment layers, visibility, ordering, opacity,
duplication, deletion and brush/gradient/range masks. Include computed retouch
layers needed by B3 without confusing them with RAW copies. List initial
filters from modules actually present; do not build a second photographic
engine for the panel.

Begin with a properly defined normal blending mode. Add other modes only
with equations, color domain, alpha handling and tests. Document a uniform
premultiplied/straight-alpha choice, especially at mask and geometric-operation
boundaries.

Early camera operations, including possible neural RAW denoising, must not
arbitrarily become filters movable after DCP. The panel must distinguish fixed
operations from reorderable filters; formalize constraints in the rendering graph.

Validation: synthetic compositions, 0/100% opacity, uniform and nested masks,
layer permutations, undo, reopening and identical export rendering. Measure
memory with several masks/layers, not just an unedited image.

## 6. B3 — Object Removal menu

Separate clone stamping with chosen source, local healing/filling and, only
after dedicated qualification, neural reconstruction. Removal does not restore
the truth: replaced pixels are reconstructed and must remain identifiable
in history.

First usable increment without a network: brush/region, size, feathering,
mask visualization, stamp source, preview, apply/cancel and editable retouch
in a dedicated layer. Never modify the original. Define sampling scope:
source only, layer or composition.

B6 adds clicking an object through B5, followed by dilation/manual mask
correction before processing. Generative reconstruction is optional and outside
the first commitment; it requires model selection, rights and separate criteria,
not merely an object detector.

Validation: dust, small elements, textures, objects on lines, fine contours,
photo borders and large regions with insufficient surrounding information.
Keep cases where the system must warn or abstain instead of promising a
credible replacement. Check halos, repetition, seams and color.

## 7. B4 — Noise and Sharpening neural networks

### B4a: define model and data scope

The [31 October B4 preparation study](restauration-neuronale_en.md) specifies
candidate models, observed hardware, RGB/RAW boundaries and bench gates.
Study authorized alongside alpha testing, without concurrent heavy inference;
no model or runtime is qualified yet.

Distinguish denoising, sharpening and deblurring. No single network is mandated:
qualify denoising first, then sharpening, then their order or a joint model
if a benefit is demonstrated. Sharpening does not recover absent information;
test invented detail as carefully as smoothing.

Explicitly choose CFA, signed camera RGB or working-image input. Preserve
upstream data; a model trained on encoded RGB cannot directly process signed
RAW without validation. Define normalization, white balance, negative/>1 values,
subsampling and input/output adapters. Add no implicit u16 round-trip or clipping.

Separate development, tuning and qualification by scene/session/camera;
crops of one burst do not become independent sets. Preserve the unopened
reserve and plan additional data if it does not cover the selected domain.
Trace data rights, weights/runtime licenses, hashes and model version;
“open weight” alone is not sufficient.

### B4b: prototype on the reference machine

Compare with the retained classic engine at identical rendition/framing:
luma/chroma noise, weak detail, skin/hair/textiles, flat regions, saturated
colors, blacks/highlights and artifacts. Use signal-conditioned measurements
and visual review, not one average or Lightroom proximity alone.
For sharpening, test halos, ringing, doubled edges, invented texture and
repeated processing. Compare noise→sharpening and interaction with classic
sharpening to avoid default double enhancement.

### B4c: usable, robust integration

Explicitly triggered local processing, progress/cancellation, before/after
comparison and return to the classic engine. Controls and advanced settings
must stay predictable; avoid rerunning expensive processing for every slider
movement when qualified reuse is possible. Cache result/provenance by model
and recipe without mixing weight versions. Missing/incompatible model:
explicit message, preserved source/edits and no silent download.

## 8. B5 — Shape and object detection networks

Provide a shared assistance layer, but distinct tasks and outputs:

| Task | Expected output | Use |
| --- | --- | --- |
| Geometric shapes | Lines, segments, contours or primitives with confidence | Landmarks and proposed B1 straightening/perspective |
| Object detection | Optional classes and boxes | Candidate selection |
| Segmentation | Pixel masks, boundaries and confidence | Layers/local edits and B6 assisted removal |

A detection box is not enough to cut out an object. Provide manual mask
correction, addition/subtraction and subject/background ambiguity handling.
For shapes, retain a classic geometric comparator; do not impose a network
if a simple method meets the need at lower cost. Networks are planned
candidates, not guaranteed to outperform every alternative on every scene.

Coordinates must remain exact between analysis proxy and full-resolution
source, including after geometry. Detection must not automatically change
rendering; the user selects and confirms the transform/edit. Define covered
classes/shapes before training, without promising recognition of “all objects”
or scene understanding.

Validation: precision/recall, IoU or boundary metrics as appropriate, landmark
geometric error, thin/occluded objects, repeated patterns, noise and low light.
Separate selection quality from B3 reconstruction quality. Freeze confidence
thresholds and abstention before final qualification data.

## 9. Host resources, privacy and deployment

Qualify first on the user's machine, following the 31 October decision.
Freeze CPU/threads, RAM, GPU/VRAM if used, OS, drivers, backend, model and
image size. The 20 logical-thread hint observed on the bench is insufficient
to qualify inference hardware.

Plan capability detection, shared CPU/RAM/VRAM budgets, interface priority,
qualified tiling/overlap, bounded disk temporaries when needed and cleanup
limited to processing temporaries. Do not promise that application controls
prevent all host-wide load overruns. Eight current threads are an existing
ceiling, not the future neural optimum.

Less capable machines: slower, limited or unavailable advanced mode with an
explanation; retain manual functions and the classic engine. No silent
resolution reduction or rendition substitution. Qualify backend/numeric
precision differences before offering them. Memory/latency gates must be
frozen before measurement, not invented here.

Images and inference stay local by default. This plan authorizes no uploads
or collection of photos for training. Any future weights download or external
service requires an explicit decision. Check redistribution rights for models
separately from code licenses.

## 10. Delivery, branches and exit gates

Durable English-named branches start from current `dev`; reuse existing
domain branches. Proposed new groups: `patch_geometry`, `patch_layers`,
`patch_object_removal`, `patch_detection`. Reuse the appropriate noise/sharpening
domain after branch audit; do not create duplicates. Coordinate UI/engine
connections through contracts and isolated worktrees. This document executes
no branching, push, merge or tag.

Each package follows specification/ADR if needed → engine/tests → UI →
qualification on frozen references → user validation → authorized `--no-ff`
delivery. Keep branches after integration. Use targeted development tests,
full tests for major changes and CI. Recipe/render-graph changes require
cross-domain regression tests.

For every delivered panel: beta context help/hover tooltips, user-guide chapter,
non-conflicting shortcuts, documentation-workstream translation, one protocol
and evolving report per group, and canonical measurement JSON. Update the
technical manual when contracts actually change; do not describe this target
as already delivered architecture.

A package is complete only with qualified persistence/undo, preview/export,
errors/resources, stated limits and user validation. Beta is not `v1`:
full stability still requires panel validation on target cameras owned by
the user. This plan commits to no firm schedule or model selection.

## 11. First preparation package — B0, 31 October 2026

Preparation may start during alpha; it is not entry into beta. Working reference:
`dev` at `b19b4c3`. This section delivers no engine change. Local cleanup is
recorded in the [dedicated report](../reports/storage/workspace-cleanup-2026-10-31/README_en.md).

### Verified existing components and gaps to qualify

- `EditRecipe::Settings` and `RawDevelopment` already carry adjustments and the
  RAW recipe: extend them rather than introduce a second recipe for new panels.
- `Sidecar::write` already uses `QSaveFile` and writes format 9; reading accepts
  versions 1 through 9. **User decision of 1 November 2026 (audit C-13): retain
  reading, conversion into the current recipe and migration tests.** Write a
  single current format, without retaining an old photographic engine. The
  header still mentions format 6: correct that declaration and qualify conversion
  failures in the engine package without dropping support for users' files.
- The experimental `denoiseBeforeProfile` switch is explicitly session-only,
  not persisted. Decide whether a future beta recipe must retain it before
  promising identical restoration; alpha behaviour is unchanged here.
- Cancellation already exists for prefetch and batch export. This does not
  establish a shared cancellation contract for every operation or complete
  undo/redo history: both still require an audit.

### Concrete sequence and completion criteria

| Step | Preparation work | Evidence required before integration |
| --- | --- | --- |
| B0.1 | Inventory commands, persistence, tasks and caches from code; identify ownership and lifetime | Existing/missing table, existing tests located, no competing implementation |
| B0.2 | Write the recipe/history and asset-storage ADR; decide coordinates and alpha | Contract reviewed with engine/UI workstreams, schema and errors defined |
| B0.3 | Define task interfaces: state, progress, cancellation, source/recipe revision, atomic publication | Deterministic stale-task, cancellation and closing tests, without unbounded waits |
| B0.4 | Implement the modular foundation, then connect the UI | Unchanged rendering without new operations; qualified persistence and undo/redo |
| B0.5 | Close remaining alpha validations and obtain milestone approval | Referenced matrix and JSON, known limits, user approval before a beta tag |

Start with B0.1, then B0.2/B0.3; do not start all five panels or train a network
in parallel. For initial development, create the foundation branch from current
`dev` in an isolated worktree; do not implement the engine on
`patch_documentation`. Choose the exact branch and connections when that package
starts, without duplicating an existing branch.

## 12. B7/B8/B9 — stacking, panorama and HDR

Added at the user's request on 31 October 2026. Reuse the
[stacking and panorama plan (French)](empilement-et-panorama.md) as the detailed
reference, without introducing separate multi-image engines for each function.
These are distinct modes: horizontal/vertical describes a panorama, not HDR.
An HDR panorama combines both operations after their separate qualification.

- **B7 — stacking:** start with constant-exposure noise reduction, then qualify
  focus stacking separately. User-selected reference image and anchor/region,
  subpixel alignment, confidence and motion rejection; proposed automatic crop
  to common coverage. One point alone cannot determine rotation or perspective:
  add correspondences and report failures. Sky and ground may require separate
  alignments, to be qualified in a dedicated extension.
- **B8 — panorama:** one engine for horizontal and vertical, single-row first,
  then multi-row. Use the union of views rather than their intersection; make
  projection, exposure compensation, seams and transparent areas explicit.
- **B9 — HDR:** align differently exposed captures of one scene, handle clipping
  and moving objects without ghosts. Distinguish visual exposure fusion from
  radiometric HDR reconstruction. For the latter, qualify input before nonlinear
  profile curves; a linear-transfer Rec.2020 buffer is not necessarily scene-linear.
  Separate floating HDR output from tone-mapped SDR rendering; merging is not
  equivalent to HDR display/file export, which needs its own contract.

All modes use a **dedicated per-task disk staging directory**, with tiles, shared
RAM/disk budgets, progress, cancellation and cleanup restricted to owned
temporary files. Keep a persistent recipe independent of staging, intact sources
and atomic publication of the new editable image.

Qualification: known displacements, fine detail, moving subjects, boundaries,
extreme exposures, colors, halos, seams, full disk, cancellation and reopening
after staging removal. Establish one evolving protocol and report per family
with JSON evidence; existing bursts alone do not qualify HDR or panoramas.
No new user exports are requested and no corpus is declared complete at this stage.

Internal order: multi-source foundation → simple stacking → separately qualified
panorama and HDR → HDR panorama. B7/B8/B9 numbering does not require waiting for
all neural networks: these packages depend on B0, not neural training. Proposed
branches, to check when starting: `patch_stacking`, `patch_panorama`, `patch_hdr`.
This plan implements none of these features.
