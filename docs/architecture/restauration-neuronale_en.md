<!-- translation-source: docs/architecture/restauration-neuronale.md; sha256: 38a1ddbb948aeead0261db67b67bbb5121d285dec8456ac00859839d88c4ca70 -->
*[Français](restauration-neuronale.md) | [English](restauration-neuronale_en.md)*

# B4 — preparation study for local neural restoration

## State on 31 October 2026

**First synthetic bench passed (§10), not integration or a photo result.**
The user authorizes initial tests and PyTorch installation in an isolated
environment. The classic engine is unchanged; alpha tests retain priority.
Sections 1 through 9 record scoping before neural installation/calculation.

Subsequent audit (§11): settings of 115 lunar CR3 files checked, three previews
measured and historical movement evidence reused. No clean reference created.

While awaiting new captures (§12), normalization/mask/partition contracts were
tested and a multi-scene synthetic pipeline run. No photographic benefit or
usable weights claimed.

Subsequent user direction (§9): proven architecture, prioritizing **weights
trained for IPR and signed camera data**; existing RGB models remain comparators,
not a mandatory target.

This reference expands [beta package B4](roadmap-beta_en.md). Local observations
and limitations are preserved in the
[v001 preparation JSON](../validation/raw/preparation-b4-v001-20261031T132231Z.json),
not a competing campaign report. The
[evolving RAW report (French)](../validation/analyses/analyse-developpement-raw.md)
continues tracking existing results. At initial scoping, documentation was on
`patch_documentation`; local prototype branch `patch_neural_restoration` came from `dev`
`b19b4c3`, without a worktree, implementation or publication at this stage.

## 1. Scoping conclusion

Keep two routes distinct:

1. **Developed-RGB pilot** for comparing available weights quickly: NAFNet SIDD
   width32, color DRUNet and Restormer Real Denoising. This evaluates photographic
   usefulness; it does not validate replacing RAW denoising.
2. **RAW target:** noise-conditioned restoration on CFA or camera RGB, adapted
   to our cameras and signed signal. None of the selected weights justifies
   direct insertion at that boundary. Decide subsequent adaptation or training
   from pilot benefit and the necessary data.

Recommendation: start with NAFNet width32 and DRUNet, then Restormer if that pair
does not provide the expected trade-off. This is a proposed testing order, not
a measured ranking or an exhaustive state-of-the-art survey. No LLM, generative diffusion or mandatory combined
noise/sharpening network: faithful restoration is the first goal.

## 2. Reference host and consequences

| Local observation | Value | Scope |
| --- | --- | --- |
| CPU | Intel Core i7-12700K, 20 reported logical threads | Effective affinity and shared budget still to qualify |
| Physical RAM | 16,933,507,072 bytes, about 15.77 GiB | About 5.65 GiB available at query time; variable, not reserved |
| GPU | NVIDIA RTX 3060, 12,288 MiB reported | Free VRAM, CUDA runtime and ONNX compatibility not tested |
| GPU driver | 610.88 | Observed with `nvidia-smi`, not dependency validation |
| OS | Windows 11 AMD64, version 10.0.26300 | This measurement does not qualify Linux/macOS |

The GPU makes a local pilot plausible; it proves neither full-frame capacity
nor a 10-second latency. System RAM requires bounding CPU copies as well as GPU
activations. At 32 MP, one float32 RGBA buffer alone occupies 512 decimal MB,
before camera source, caches, tensors and temporaries.

Initial proposal: one inference at a time, 256- then 512-pixel tiles, batch 1,
FP32 first. Try 1024 only after measuring peaks. These sizes are unqualified and
need defined halos. Reserve resources through the existing controller; do not
use all VRAM or multiply independent CPU pools. The classic filter's current
8-thread ceiling is not a measurement of the best neural budget.

## 3. Examined models and unresolved rights

| Candidate | Pilot role | Key limitation |
| --- | --- | --- |
| NAFNet-SIDD-width32 | RGB CNN, initial real-noise comparator | Smartphone SIDD domain, not signed camera RGB; spatial tile context matters |
| Color DRUNet | RGB CNN with noise-level map, useful for strength studies | Gaussian reference noise differs from demosaiced Canon noise |
| Restormer Real Denoising | Attention-based RGB comparator | Cost and spatial context need measurement; not inherently superior |
| Learning to See in the Dark (SID) | RAW architecture reference | Sony/Fuji weights are not interchangeable with Canon; not a ready-to-use pilot |

[NAFNet](https://github.com/megvii-research/NAFNet),
[DPIR/DRUNet](https://github.com/cszn/DPIR) and
[Restormer](https://github.com/swz30/Restormer) provide architectures and weight
references. Denoising and deblurring variants are distinct: do not substitute
GoPro weights for SIDD. No weight or exact parameter count was verified locally.
Weight size is not peak inference memory; PSNR rankings do not establish latency.

Consulted source licenses declare MIT for
[NAFNet](https://raw.githubusercontent.com/megvii-research/NAFNet/main/LICENSE)
(with BasicSR Apache-2.0 notices),
[DPIR](https://raw.githubusercontent.com/cszn/DPIR/master/LICENSE) and
[Restormer](https://raw.githubusercontent.com/swz30/Restormer/main/LICENSE.md).
This does not finish package legal clearance: before redistribution, identify
the exact artifact, its rights, notices and training data. Record source commit,
official URL, SHA-256, architecture/configuration, export version and weight
license. A mirror or an “open weight” tag is insufficient. No third party was
contacted and no user content was uploaded.

The [SIDD site](https://abdokamel.github.io/sidd/) describes smartphone data and
RAW/sRGB versions; availability does not demonstrate Canon generalization.
The [SID README](https://github.com/cchen156/Learning-to-See-in-the-Dark) itself
warns that weights depend on the sensor. No external dataset was downloaded.

## 4. Input contracts: protect the current pipeline

Reference code exposes two different boundaries:

- `CameraDenoise.h`: `CV_32FC3`, **already white-balanced** camera RGB, white 1,
  finite negative and above-one values allowed; after RCD/median, before LCP/DCP
  on the retained pre-lens path. Not universal photometric luminance.
- `ColorPipeline::WorkingImage`: `CV_32FC4`, linear-transfer Rec.2020 D65,
  straight alpha, extended range. DCP may already have applied its curve, so this
  buffer is not necessarily radiometric scene-linear data.

For the **RGB pilot**, create only a neutral-render copy in the encoded-RGB
domain expected by the weights, retaining floats where the adapter allows.
Compare model and controls on **the same copy/adapter**. Record every clamp and
out-of-domain pixel; do not credit adapter gamut losses or transfer changes to
the network. This SDR bench route is not generally invertible and is not a new
master proposal. Measure in-memory buffers, not IPR TIFF round trips.

For the **RAW target**, define CFA/RGB, Bayer plane order, per-channel black/white,
gains, known/unknown ISO, signal-dependent noise, orientation and demosaic stage.
Train/qualify the network in that domain. Simply offsetting negatives or clipping
at zero does not create valid compatibility. Preserve immutable input, floating
output and extended values according to the contract; do not predict alpha or
metadata.

Strength 0 must bypass the network exactly. For a model without strength
conditioning, `output = input + t × (prediction − input)` is an ergonomic control
to test, not proof of monotonic noise reduction or LR equivalence. DRUNet's noise
map is not directly our 0–100 slider. Future sharpening needs its own weights,
data and false-detail tests.

## 5. Runtime and tiling

Calculation reference: an isolated, pinned author-framework environment, never
installation into alpha campaign environments. Proposed deployment:
[ONNX Runtime](https://onnxruntime.ai/docs/execution-providers/), C++ API, CPU
control and CUDA on the first host. The
[runtime license](https://github.com/microsoft/onnxruntime/blob/main/LICENSE)
is separate from models and GPU libraries. Export/operator parity must pass
before drawing quality conclusions.

[ncnn](https://github.com/Tencent/ncnn), C++ CPU/Vulkan, remains a deployment
alternative if conversion and package size warrant it; audit its
[licenses and components](https://raw.githubusercontent.com/Tencent/ncnn/master/LICENSE.txt)
too. Do not multiply backends in the first pilot. CoreML/macOS and other GPUs
require later qualification, not covered by present hardware.
[DirectML documentation](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html)
states sustained engineering and directs new development toward WinML: do not
automatically choose that older path for every Windows host.

Qualify NAFNet global pooling and Restormer spatial reductions: a finite halo
does not ensure full-frame identity. `NAFNetLocal` is a configuration variant to
identify, not an invisible checkpoint fix. Compare two offset grids, boundaries,
corners and small full images; detect rendering differences, not just visible
seams. Include padding, overlap, tile blending and precision in the recipe.

CPU: explicitly bound internal/external pools; measure 1/4/8 threads after alpha
tests release the host. The
[ONNX threading documentation](https://onnxruntime.ai/docs/performance/tune-performance/threading.html)
explains active-wait consumption; disable spinning in the first control to avoid
burning CPU between commands. Cancellation prevents publication and finishes
work at the next safe point; GPU tiles may not be instantly interruptible.

## 6. Available data and bias prevention

Filename/extension inventory, **without new decoding**: 28 base RAW files
(14 CR3, 14 CR2), 76 outside-protocol RAW files, 42 burst CR3 files and 241 CR3
files in the colorimetric protocol, including 77 in `blacks`. Groups overlap;
neither their sum nor file count measures independent scenes. Thousands of TIFF
files are not declared valid LR neutrals by this inventory.

The historical CFA inventory retains a 14/14/14 development/validation/sealed
partition of the burst. Historical controls and the 76 outside-protocol files
have already been examined: renaming or recropping does **not** create a new
holdout. Verify provenance/history before freezing the final set; do not choose
its examples according to network results.

No new photographs are needed for the first contract/resource checks and
exploratory visual pilot. Training or fidelity conclusions need qualified clean
references: aligned static bursts with stable exposure/WB, controlled clipping,
fine detail, skin, hair, textiles and flat regions. Burst averaging retains shared
fixed noise; capped-lens dark frames are not clean truth for illuminated textures.
Split sessions/scenes/cameras, not just crops. Audit current burst motion before
using its mean as truth. LR references compare behavior, not radiometric truth
or implicitly authorized training data.

## 7. Bench to prepare, then execute after alpha tests

| Gate | Work | Expected output |
| --- | --- | --- |
| Q0 | Freeze sources, weights/rights, adapters, sets and masks before testing | Internal bench manifest and hashes; no IPR measurement TIFF |
| Q1 | Local loading and contracts on small synthetic images | Shape/channels, RGB/BGR, finitude, exact zero bypass, immutable source, explicit errors |
| Q2 | Author FP32 against ONNX FP32 export on identical buffers | Mean/max/channel errors; tolerances fixed before the campaign |
| Q3 | 256/512 tiles, two origins, borders and alpha mask | Seams, context variation and peak memory; not only a resized-image test |
| Q4 | Three to six development scenes, neutral/classic/neural controls | Detail/color at 100%, identical strengths/masks, blind comparison where possible |
| Q5 | Frozen qualification, independent scenes, full resolution | User approval, failures/limits and host budget before integration |

Do not score “better” from reduced high-pass energy alone. Measure signal-binned
luma/chroma, detail contrast, color shifts, blacks/highlights and false edges.
Use PSNR/SSIM only with an admissible aligned independent reference; otherwise
report residual indicators and perceptual review, not error to truth. Avoid
masks independently recomputed per output to favor a candidate.

Resources: separate cold loading, warm-up then at least 3 repetitions, median/
worst value; include CPU/GPU transfer, preparation and reconstruction in total
time, synchronizing the GPU for timing. Measure peak RAM/VRAM, not just weights.
The user's approximate 10-second exploratory goal is not a commitment before
full-resolution measurements. Do not benchmark alongside LR or alpha tests;
record any interference.

Blocking defects: obvious color drift, invented detail, seams, silently accepted
unsupported inputs, changed source, NaN/Inf, cancellation publishing stale output,
uncontrolled resource excess or crash. Missing/incorrect-hash model: explicit
message, classic mode available, no silent download/backend substitution. Do not
execute third-party scripts or load unverified pickles; plan tensor-restricted
weight loading and provenance-controlled artifacts.

## 8. Next decision

Finish artifact clearance and freeze Q0, then run Q1/Q2 only once alpha campaigns
finish and release the host budget. Select the first checkpoint and authorize
acquisition before download; redistribution is not approved. This work delivers
proposed contracts, inventory and bench protocol, **not a demonstrated performant
model**.

## 9. Selected direction and image requirements — 31 October 2026

The user selects a **proven architecture with our own weights** and controlled
color contract. Signed pre-LCP/DCP camera RGB becomes the priority; existing RGB
weights remain optional comparators, not a prerequisite to our training. Factual
details are preserved in the [v001 requirements JSON](../validation/raw/besoins-corpus-b4-v001-20261031T134511Z.json).

### What is actually missing

- Multiple **different scenes with noisy data and comparable clean references**,
  not simply more isolated photographs. The historical verified inventory covers
  one 42-frame ISO 4000 burst plus capped-lens dark/dark-room captures, all from
  the R6 Mark III; it is not a diverse supervised dataset.
- Weak textures: fine textiles, stationary hair or a wig, rough paper, wood;
  strong colors: orange/red, blue, green and gray. Retain the real luminance
  sand texture on climbing panels.
- Separate sessions and stable lighting, with independent scenes reserved before
  tuning. Existing dark frames are useful; **do not retake them all** before
  auditing unclassified series.
- Qualify the 60D later. Do not automatically transfer R6 Mark III ISO settings,
  noise, weights or conclusions.

### First requested capture: three pilot scenes

Start with only three static arrangements:

1. Fine fabric, stationary hair strands and a sharp-edged object, in soft light.
2. Orange/red, blue and green objects with a gray patch, without clipped channels.
3. Matte gray area, low-contrast detail and a dark object with readable shadows.

For **each scene**, tripod-mounted R6 Mark III, full-resolution RAW rather than
C-RAW for the first package, fixed shutter mode, aperture and manual focus.
Fixed WB, no Auto ISO or automatic exposure compensation. Disable long-exposure
noise reduction and record other camera noise settings. Do not move camera,
subject or lighting between series. Avoid screens, flickering LEDs, foliage,
people and moving fabric.

- **Reference: 16 frames at ISO 100**, or 400 if that provides a more stable
  sequence; lengthen exposure for correct brightness without clipping.
- **Noise: 16 frames at each of ISO 3200, 12800 and 51200.** Shorten shutter time
  as ISO increases to preserve comparable brightness; do not leave every exposure
  parameter unchanged at once.
- Same framing, aperture and lighting. Check blacks, grays and colors, without
  trying to expose the entire scene to the right. Record shutter times and
  verify stability between reference and noisy captures.

This is **64 RAW files per scene, 192 in total**. Reference frames are not noisy
training inputs. After alignment/inspection, aggregation reduces temporal noise
without automatically removing FPN, hot pixels or lighting changes. Low-ISO
references and high-ISO images need measured radiometric matching; changing ISO
does not make their pixels identical. Do not produce LR TIFFs.

Audit this package before taking more. If acceptable, a starting proposal is
**12 scenes total: 8 development / 2 validation / 2 sealed test**, or 768 RAW files
with this recipe. The three pilots stay in development; all ISO settings and
crops from a scene stay together. This prepares an initial study, not a guarantee
of a general-purpose model: training from scratch may need much more diversity.

### Lunar bursts on SD card D:

The user confirms tripod capture. Read-only listing found 115 CR3 files in
`D:/DCIM/101EOSR6`, from `6V1A0016.CR3` through `6V1A0130.CR3`, grouped by write
time on 30 October around 22:44, 22:46 and 22:47. These are not EXIF-proven burst
boundaries. The first embedded preview shows the Moon; other files/settings
still need auditing.

Proposed use: exploratory craters, halos, dark sky and false-detail checks,
initially outside training. A tripod does not eliminate lunar drift, turbulence
or vibration. Do not declare an average “clean” without alignment/checks; do not
select only views favoring the network. This already-previewed series is not
described as a sealed blind test.

### Public datasets: complements, not replacements for Canon captures

| Source | Potential contribution | Preparation decision |
| --- | --- | --- |
| [SIDD](https://abdokamel.github.io/sidd/) | Noisy/reference pairs, RAW and sRGB; declared MIT license | First public candidate, begin with a subset; smartphone/normalized RAW do not qualify Canon negatives |
| [raw.pixls.us](https://raw.pixls.us/) | Scene/format diversity | Individually verified CC0 files only; not paired clean truth, label synthesized noise |
| [RENOIR](https://ani.stat.fsu.edu/~abarbu/Renoir.html) | Includes Canon T3i/S90 data | Potential interest, image reuse rights not established here: not selected for training yet |
| [ELD](https://github.com/Vandermode/ELD) | Noise modeling and low-light comparison | MIT code but dataset described for evaluation: do not treat it as unrestricted training data |
| [FiveK](https://data.csail.mit.edu/graphics/fivek/) | Diverse RAWs and expert edits | Research licenses restrict commercial-purpose use; not selected as an unrestricted foundation |
| [HDR+](https://hdrplusdata.org/dataset.html) | Smartphone bursts and merged results, declared CC-BY-SA | Possible later B7 study; no full download or assumed automatic rights over our weights |

The [FiveK license](https://data.csail.mit.edu/graphics/fivek/legal/LicenseAdobeMIT.txt)
is a research license despite MIT in the dataset name, not the MIT software
license. Future acquisitions must track image rights separately from code and
weight rights. Nothing was downloaded.

### 500 GB SSD: proposed organization, no disk operation

At the median 38,026,452-byte size of the 14 base CR3 files, 192 new RAW files
occupy about **7.3 GB**, 768 about **29.2 GB**; future sizes vary, excluding copies
and caches. The 115 lunar candidates occupy about 2.62 GB. A nominal 500 GB SSD
helps substantially, but is no reason to materialize every full-frame tensor:
one 32 MP float32 RGB already occupies 384 MB.

After user mounting, keep training data separate from `base` and disposable
exports, identifying camera/session/scene/series and source hashes. Canon names
repeat: never copy lunar frames into `base` over an existing namesake. Backed-up
immutable sources, on-demand crops and bounded regenerable cache; proposed
initial cache budget 100 GB, reassessed after measurement, with free-space margin.
A compute SSD is not a backup. Do not apply analysis-TIFF cleanup to training RAW
sources needed for reproducibility.

Formatting, volume selection and corpus relocation still need user decisions.
This preparation performs no formatting, copying, deletion or partitioning.

## 10. First synthetic bench and course — 31 October 2026

Dedicated `patch_neural_restoration` worktree and `.venv-neural` environment:
PyTorch **2.9.1+cpu**, NumPy **2.3.5**, Python 3.14.3. Packages came from official
PyTorch/PyPI indexes, not pretrained weights. Global Python and alpha dependencies
were unchanged. No GPU calculation.

Local commit `b6ad93d` provides a two-level width-8 residual U-Net with
**29,995 parameters** and CPU bench. Camera RGB/per-channel sigma inputs use
NCHW float32; random body, initially zero head. This is a contract witness,
not a production architecture or weight selection.

[JSON evidence](../validation/raw/neural-contracts-v001-20261031T190924Z.json):
**14 tests passed**, no failures/errors/skips, about 1.36 s for the internal suite,
two intra-op threads and one inter-op thread. Checks cover odd/even dimensions,
identity extended range, zero bypass, immutable source, invalid inputs/outputs,
before/after cancellation and gradients. One test performs 16 small Adam steps
on one artificial patch: wiring verification, **not photographic training or
generalization**. No weights saved.

This time is not photo latency. CUDA, ONNX, full-frame tiling, peak RAM/VRAM,
Canon noise and photographic quality are not qualified. Next gate: freeze
adapter/dataset and test admissible references, not automatic integration or
large-scale training.

[French course13](../scholar/cours-13-reseaux-neuronaux-de-zero.pdf), its
[LaTeX source](../scholar/cours-13-reseaux-neuronaux-de-zero.tex) and
[executable lab](../scholar/references/reseaux-exemples.py) explain fundamentals,
manual gradients, CNN/U-Net, data and qualification. The lab is separate from the
bench: teaching demonstration, not another validation campaign.

## 11. Audit of available bursts — 31 October 2026

The user authorizes this audit while preparing new scenes. Dedicated tool on
`patch_neural_restoration`, commit `1aec51c`, seven synthetic reader tests passed.
Measurements are preserved in the
[v001 lunar JSON](../validation/raw/moon-audit-v001-20261031T194552Z.json).
Bounded CMT1/CMT2 reading, not full MakerNotes/CTMD interpretation. Sequential
processing, one OpenCV thread; no production change.

### Confirmed lunar settings

All 115 files in `D:/DCIM/101EOSR6` identify Canon EOS R6 Mark III, 200 mm,
`70-200mm F2.8 DG OS HSM | Sports 018`, orientation1. Tripod use is the user's
statement, not an EXIF measurement.

| Files | Count | ISO | Shutter | Aperture |
| --- | --- | --- | --- | --- |
| `6V1A0016.CR3` through `6V1A0049.CR3` | 34 | 1600 | 1/200 s | f/5.6 |
| `6V1A0050.CR3` through `6V1A0130.CR3` | 81 | 1000 | 1/200 s | f/9 |

Capture timestamps confirm 30 October around22:44–22:47. The second range
contains two time groups; matching settings do not establish one continuous
burst. Do not average all 115 together: gain/aperture differ, and framing moved
between the previews.

Embedded JPEGs from0016/0073/0130 measure6960×4640 pixels. The detected bright
region is about300 pixels across: useful local lunar detail, not 115 scenes or
diverse texture/color examples. Encoded medians in that region are159/75/78.
The first preview has about1.17% of region channels at255, the other two0%.
This is **not RAW clipping diagnosis**: the JPEG curve, balance and processing
can saturate a channel.

The Laplacian proxy varies but is not used to rank sharpness: exposure and
processing change it too. The selection box uses JPEG coordinates, not a
certified sensor ROI. Full RAW hashes were retained for the three selected
views; others have metadata-block hashes and size/date only, not whole-file hashes.

**Decision:** candidate for exploratory detail/halo checks, initially outside
training. Later RAW qualification must cover saturation, registration per
subseries, coverage, turbulence and texture stability. No “clean” average or
supervised target created from the JPEGs.

### Reuse of the historical 42-frame burst

This bench rereads no development/validation/sealed pixels. It reuses the
27 October movement JSON, retaining its path/hash in the new evidence. That
audit covered only the first14 development frames, using an 800-pixel-wide CFA
preview. It reported up to5.19 preview pixels of global translation,0.427°
absolute rotation and scale1–1.00458. Do not translate those values into native
RAW displacements without the original bench's exact conventions.

These results rule out a naive mean as texture truth. They do not rule out a
registered reference: global affine fit, parallax and local movement still need
checks on authorized pixels. The holdout remains sealed; previously examined
images do not become a new untouched test.

### Next

Audit the first new capture package before expanding, retaining scene/session/
settings and independent groups. For existing sources, freeze the RAW domain
and registration treatment before constructing an averaged target. No training
or weight adjustment from preview metrics. No RAW copied, exported, deleted
or modified.

## 12. Data pipeline and synthetic pilot — 31 October 2026

The user announces new captures for the next day and authorizes preparation.
On `patch_neural_restoration`, commits `cbe1770` and `041af0b`, with no engine
connection. The bench contract requires:

- one explicit positive scalar shared by input, reference and sigma; no inferred
  per-image range, gamma, clamp or white-balance change;
- immutable source and preserved negative/extended values. Inverse scaling is
  subject to float32 roundoff, not general bitwise identity. Reject overflow and
  underflow destroying a nonzero value;
- Boolean N1HW validity mask; loss over valid channels only, rejecting empty
  masks and nonfinite buffers. Masking pixels must not artificially lower loss
  by retaining the original denominator;
- all variants of one scene in one group; reject identical source or reference
  content shared across train/validation/test. This still depends on honestly
  assigned scene identities.

**14 data tests pass.** The first run had13; the additional check covers shared
clean targets. Evidence remains separate: [first run](../validation/raw/neural-data-pilot-v001-20261031T204455Z.json)
and [guarded replay](../validation/raw/neural-data-pilot-v001-20261031T204714Z.json).
Same architecture, seeds,64 Adam steps and learning rate0.001; no settings chosen
from validation/test and no checkpoint selection. Per-scene metrics and losses
are identical across both runs.

The pilot uses **8/4/4 artificial scenes**, meaning16 seeds of one generator,
not16 independent photographic scenes. Ramps, weak patterns and signed extended
patches,32×48 pixels, two CPU intra-op threads. Gaussian signal-dependent
variance; sigma computed from clean targets: ideal conditioning, not a usable
Canon noise estimate.

| Group | Mean input MSE | Mean output MSE |
| --- | --- | --- |
| Train,8 cases | 0.000788827 | 0.000788840 |
| Validation,4 cases | 0.000784167 | 0.000784299 |
| Synthetic test,4 cases | 0.000718287 | 0.000718276 |

**Verdict:** operational plumbing/checks, near-identity output, no demonstrated
useful denoising benefit. Do not repeatedly tune on these16 cases for a cosmetic
improvement. The latest internal1.73-second run is not photo latency or qualified
memory budget. No weights saved, no RAW/sealed/LR-reference pixels read, no GPU/ONNX.

### First checks when captures arrive

Before learning: noncolliding identities, integrity and camera/RAW mode,
ISO/shutter/aperture, framing/lighting stability, unclipped reference, comparable
focus and registration quality. Verify pre-LCP/DCP signal units and masks in that
signal, not the JPEG. Beige is accepted; a neutral-gray flat area is unnecessary.
Fabric weave is detail to protect, not noise to erase.

Fix tolerances and partitions before looking at network outputs. If admissible,
construct an initial reference with validity/provenance, then qualify real
training. Otherwise identify files and reasons to correct without silently
including them.
