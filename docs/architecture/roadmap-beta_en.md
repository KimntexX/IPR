<!-- translation-source: docs/architecture/roadmap-beta.md; sha256: f876f4fac3ed48584105ba44e5e508c4ae6a4a47a5ebc0339d383b18135630c4 -->
*[Français](roadmap-beta.md) | [English](roadmap-beta_en.md)*

# Beta action plan — editing panels and neural processing

Planning date: 31 October 2026. Status: **proposed plan, not implemented**.
User-requested scope: geometry, object removal, layers/filters, neural networks
for noise/sharpening and for shapes/objects. “Shapes” means geometric shapes
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

B4 need not wait for layers for an independent initial study, but integration
must respect the same task, history and provenance system. B5 can be prepared
after the foundation; it does not block B1/B3 manual tools. This plan starts
no training or parallel implementation.

## 3. B0 — shared foundation before panels

Define engine contracts before QML connections:

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
