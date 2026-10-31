<!-- translation-source: docs/architecture/restauration-neuronale.md; sha256: 67858c65819a90ed151f7919afd4ac2be4d464b3b59a5daf2cacbcd421594ae5 -->
*[Français](restauration-neuronale.md) | [English](restauration-neuronale_en.md)*

# B4 — preparation study for local neural restoration

## State on 31 October 2026

**Preparation, not integration or a denoising result.** The user authorizes this
study during alpha qualification. The classic engine is unchanged; no training,
weight download or inference has started. Alpha tests take priority and must not
share machine load with a second benchmark.

This reference expands [beta package B4](roadmap-beta_en.md). Local observations
and limitations are preserved in the
[v001 preparation JSON](../validation/raw/preparation-b4-v001-20261031T132231Z.json),
not a competing campaign report. The
[evolving RAW report (French)](../validation/analyses/analyse-developpement-raw.md)
continues tracking existing results. Documentation is on `patch_documentation`;
local prototype branch `patch_neural_restoration` was created from `dev`
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
