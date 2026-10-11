# Noise Reduction and Sharpening

**Status:** CPU noise reduction corrected, photographic functionality/progressivity confirmed; quality/calibration to be continued; sharpening not validated.  
**Consolidation Date:** October 5, 2026.

The [October 4 photographic campaign](../validation/analyses/analyse-bruit-2026-10-04.md) measures 80 retained variants and ten neutrals. It confirms the settings' activity but notes plateaus at low noise and significant residual color noise at high ISO. The CPU probe identifies a non-zero activation threshold, edge protection driven by noise, and a possible dark bias during RGB reconstruction. The October 4 measurements remain a reference for the old version. [Corrections now applied](../validation/analyses/correctif-bruit-2026-10-05.md) are synthetically tested. The [v2 revalidation](../validation/analyses/analyse-bruit-v2-2026-10-05.md) covers twelve scenes, 24 neutrals, and 145 variants: the 24 standard IPR series are progressive, and dark bias decreases. Color noise at high ISO remains moderately reduced; overall quality, component coupling, and preview/export remain to be validated. No additional recalibration or sharpening changes have been applied during this analysis. The [protocol](../validation/protocols/protocole-validation-bruit-2026-10-04.md) distinguishes neutral measurements, diagnostic series, and separate AI comparison.

## Decision

IPR does not attempt to imitate a proprietary model from Lightroom or DxO. The first version uses multi-scale noise reduction in linear RGB space, with luminance/chrominance separation, then contrast mask sharpening and halo limitation. The path is deterministic, works on Windows, Linux, and macOS without special GPU drivers, and does not modify the image when sliders are at zero.

Settings are:

- **Luminance Noise:** reduces monochrome grain, with edge protection;
- **Color Noise:** reduces colored blotches in two chrominance axes;
- **Sharpening Gain**, **Radius**, and **Masking:** enhance details with sufficient contrast without systematically reinforcing noise.

Introduced in `.ipr` v2 sidecar, they are preserved in the current v4 format. Versions v1/v2/v3 remain readable; v1 sidecars take default noise and sharpening settings. The fix does not change fields or stored values, but changes the rendering of non-zero noise settings. No user sidecar is rewritten by the build.

## Mathematical Model Implemented

Noise reduction uses Rec.709 linear sRGB luminance and two Co/Cg axes. This is no longer exactly the classic YCoCg transform:

\[
Y=0.2126R+0.7152G+0.0722B,\quad Co=R-B,\quad
Cg=G-\tfrac12(R+B).
\]

Each plane is separated by two successive Gaussian blurs. Fine and medium residuals are contracted by soft thresholding:

\[
S_\lambda(d)=\operatorname{sgn}(d)\max(|d|-\lambda,0).
\]

Gaussian sigmas are 0.8 then 1.8 pixels, the second blur applied to the first result. Y/Co/Cg noises are estimated separately by the MAD of the Haar diagonal detail `(p00-p01-p10+p11)/2`, divided by 0.67448975. Sampling preserves the four adjacent pixels of each block; perfectly constant blocks are ignored so that a large black edge does not disable processing of the rest of the image. Since the distribution after demosaicing is correlated, this estimate is not an exact sensor model.

For `s=clamp(value,0,100)/100`, thresholds are `lambda_fin = sigma*s*m`, with `m=2.55` for Y and `m=3.15` for Co/Cg, then `lambda_moyen=0.52*lambda_fin`. No 0.35*sigma activation threshold. The corrected result is also blended with the original by `s` to maintain progressivity when thresholding saturates. Processing at zero is exactly short-circuited before any transformation, bit-by-bit identical; 1/5/10 have intentionally soft effects.

The edge mask comes from a Gaussian guide at sigma 0.8, not directly from the noisy signal. Sobel is normalized by 1/8; its amplitude crosses a smoothstep from 0.5*sigma to 2*sigma. For Co/Cg, the maximum of the Y guide and the plane's own guide also protects isolated luminescent chromatic boundaries. Threshold reduction by protection remains capped at 86% for Y and 72% for Co/Cg. Maps do not share a mutable output buffer.

Thresholding can change the mean. Positive/negative changes are rebalanced: if their amplitude sums are P/N, multiply positives by `min(1,N/P)` and negatives by `min(1,P/N)`. Zero sum cases are handled explicitly. This preserves the continuous component of the plane, without adding an offset to unchanged blacks or renormalizing exposure. Result Y candidates are non-negative; Co/Cg remain signed.

Reconstruction is `RGB=Y+d`, where `d=(0.4298*Co-0.7152*Cg; -0.0702*Co+0.2848*Cg; -0.5702*Co-0.7152*Cg)`. If a channel would be negative, reduce `d` by `t=min(1,Y/(-min(d)))`. This approach to gray keeps Y constant; no SDR upper clamp is introduced. The color setting alone thus preserves linear luminance per pixel at floating precision. Local means and output render must however be revalidated on TIFFs.

Intensity/protection/scale coefficients are empirical choices, not physical constants or validated Lightroom calibration. `1e-7` is only a numerical safeguard for a plane without measurable noise. The historical floor `0.00025` remains only in the sharpening estimator, unchanged to isolate the two recipes.

Sharpening is an adaptive unsharp mask on luminance:

\[
Y'=Y+a\,(Y-G_\rho(Y))\,M,
\]

where \(G_\rho\) is a Gaussian blur of radius \(\rho\), \(a\) the gain, and \(M\) a mask that only opens above a noise-relative threshold. The result is bounded near local minimum and maximum: this limits bright/dark halos.

## Why This Choice Over Reference Solutions

| Approach | Potential Quality | Cost / Risk | IPR Decision |
| --- | --- | --- | --- |
| Bilateral or Simple Gaussian | Fast, but smooths edges or creates flats | Low | Insufficient alone. |
| NLM / BM3D | Very good classic denoising, preserves repeated patterns | Expensive computation; BM3D absent from base OpenCV, licenses and implementations to audit | Quality candidate for a non-real-time export option. |
| AI RAW (Lightroom, DeepPRIME) | Best result on very strong noise | Model, own RAW corpus, GPU, rights, and multi-camera validation required | Out of scope for first version. |
| Multi-scale with Structure Mask | Good compromise detail, predictability, and portability | CPU only, less performant than AI at very high ISO | Chosen for current preview and export. |
| Richardson-Lucy Deconvolution | Corrects known blur | Sensitive to erroneous PSF, noise, and halos | Propose later in expert mode. |

RawTherapee applies capture sharpening after demosaicing, in linear gamma, and uses a contrast threshold to avoid noise amplification. It also offers unsharp mask and Richardson-Lucy deconvolution. Lightroom exposes gain, radius, detail, and masking, and recommends reducing noise before detail tools. These principles directly guided IPR pipeline order: denoising, local contrast tools, then masked sharpening.

DxO and Lightroom use heavy AI models directly on RAW data; their quality does not come from a single matrix filter but from massive training and GPU execution. A reimplementation without training data would be visually and legally risky.

## GPU: Deferred Decision

Added operations are mainly convolutions, gradients, and pixel-to-pixel passes, thus parallelizable. However, IPR's pipeline currently keeps pixels in `cv::Mat` on CPU. An immediate OpenCL/CUDA/Vulkan pass would add host-GPU copies and a second computation path to compare, while the interactive proxy is limited to 2048 px. The CPU reference therefore serves as non-regression truth.

Before acceleration, measure on target hardware: transfer time, proxy render time, full definition export, and numerical CPU/GPU difference. A future implementation must keep the CPU fallback and be optional via CMake configuration.

## Expected Validation

1. A low ISO RAW, a high ISO RAW with dark flats, a scene with skin, and a scene with fine foliage/texture; export neutral sRGB 16-bit TIFF, noise only, and noise + sharpening.
2. Verify at 100%: noise reduction in flats, edge preservation, absence of color bleed, waxy texture, and halos.
3. Measure residual noise, gradient energy, chrominance of grays, clipping, and MAE compared to neutral. Compare same parameters with Lightroom as a reference, without requiring pixel-by-pixel parity.
4. Verify `0` values, extremes, sidecar reopening, and full definition export.

## Sources

- [RawTherapee — Capture Sharpening](https://rawpedia.rawtherapee.com/Capture_Sharpening): linear processing after demosaicing and contrast threshold.
- [RawTherapee — Sharpening](https://rawpedia.rawtherapee.com/Sharpening): unsharp mask, halo control, and Richardson-Lucy.
- [RawTherapee — About Noise Reduction](https://rawpedia.rawtherapee.com/About_Noise_Reduction): wavelet denoising, DCT, and structure masks.
- [Adobe Lightroom Classic — Detail](https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/retouch-photos.html): radius, detail, and sharpening masking.
- [Adobe Lightroom Classic — Denoise](https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/enhance-details.html): AI denoising on RAW and GPU cost.
- [DxO — HQ, PRIME, and DeepPRIME comparison](https://support.dxo.com/hc/en-us/articles/4432857457041-Which-of-the-different-DxO-denoising-technologies-should-I-use): quality, cost, and GPU trade-offs.
- [Buades, Coll, Morel — Non-Local Means Denoising](https://doi.org/10.5201/ipol.2011.bcm_nlm): foundation of non-local denoising.
