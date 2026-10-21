# Complete Guide — Creating DCP Profiles Independent of Adobe

## Objective

This guide describes the production of IPR-specific DCP profiles from measurements. The DCP is an input profile: after demosaicing, it transforms the sensor RGB to a reference color space. It does not correct distortion, vignetting, or chromatic aberration: this is the responsibility of LCP or a future Lensfun source.

The first target is a **dual-illuminant DCP** per camera model: illuminant A (tungsten) and D50/D65 (daylight). It serves as a general-purpose profile. A single-illuminant DCP is simpler but only reliable near its calibration source.

Camera model names and EXIF are not calibration. A chart with known reference values and controlled shots are required.

```text
Chart + controlled light + unaltered RAW
                  │
                  ▼
Patch measurements ↔ reference values
                  │
                  ▼
Matrices + color LUT + DCP metadata
                  │
                  ▼
Validated IPR DCP profile → profiles/dcp/ → profiles/index.json
```

A base profile seeks measurable reproduction, not the "look" JPEG of a manufacturer. Creative styles must remain separate settings or optional profiles.

## Hardware and Software

| Element | Recommendation | Role |
|---|---|---|
| Chart | ColorChecker 24/Passport matte, or equivalent with reference data | Known colors |
| Camera | A model and firmware recorded | Characterized sensor |
| Lens | Prime or locked zoom, without filter | Reduce optical variations |
| Setup | Tripod, matte black background, level | Sharpness, framing, few reflections |
| Warm Light | illuminant A, unfiltered tungsten | Warm anchor |
| Day Light | Clean midday sun or controlled D50/D65 source | Cool anchor |
| DCamProf | External DCP generation tool | Matrices, LUT, and DCP file |
| ArgyllCMS | External chart reading tool | `.ti3` measurements |

RawPedia recommends matte charts with reference data and the illuminant A + D50/D65 pair for a general-purpose DCP. Glossy charts, flash, fluorescents, and uncharacterized LEDs are to be avoided for this calibration. [RawPedia Guide](https://rawpedia.pixls.us/how_to_create_dcp_color_profiles/)

DCamProf is a GPL-3.0 DCP/ICC generation tool. ArgyllCMS provides measurement tools but is AGPL. For IPR, they must first be used **outside the IPR binary**, in a separate calibration environment; do not link them to the application without a license review. [DCamProf](https://github.com/Beep6581/dcamprof), [ArgyllCMS](https://www.argyllcms.com/)

## Detailed Procedure

### 1. Prepare Traceable Calibration

Create a folder outside the repository, without publishing RAWs by default:

```text
calibration/
  Canon_EOS_R6_Mark_III/2026-10-01/
    README.md
    daylight/RAW/      daylight/exports/
    tungsten/RAW/      tungsten/exports/
    output/
```

In `README`, note model and firmware, chart number/age, lens and focal length, ISO, aperture, exposure, light source, location/time, operator, DCamProf/ArgyllCMS/LibRaw versions, and RAW SHA-256 hashes. This provenance is essential to reproduce or remove a profile.

### 2. Set Camera and Frame Chart

1. Use full-size RAW, never sRAW/mRAW.
2. Use the lowest native ISO; disable processing that affects RAW if possible; remove colored/UV/ND filters.
3. Use a tripod and an aperture between f/5.6 and f/8.
4. Place a sharp chart in the central third: not too small, not full frame, without specular reflection, in front of a matte dark background.
5. Photograph -1/0/+1 EV to retain a view without clipped patches.

The retained view must present neither saturated patches, nor blur, nor shadow, reflection, heterogeneous lighting, mixed sources, or inconsistent metadata. Details of distance, angle, and exposure constraints are given in the [RawPedia protocol](https://rawpedia.pixls.us/how_to_create_dcp_color_profiles/).

### 3. Take Both Series

**Daylight (D50/D65).** Photograph far from walls, trees, cars, and any colored surface. Prefer high/direct sun; avoid dawn, dusk, smog, thick sky, and environmental reflections.

**Tungsten (illuminant A).** Use a genuine unfiltered tungsten lamp. Place it far enough for uniform lighting; orient chart and camera to eliminate reflection. An LED, fluorescent, or halogen light are not substitutes for illuminant A for a universal profile.

Preserve the three exposures of each series. A single scene profile (museum, interior, wedding) can be created with a single real source but must not be presented as a universal DCP.

### 4. Prepare Two Neutral TIFFs

The generator must receive values not already modified by a camera profile, curve, tint correction, or creative white balance.

The reference flow consists of opening each RAW in RawTherapee, applying the *Neutral* profile, rotating the 24-patch chart (brown top left, black bottom right), cropping to markers, then exporting `daylight.tif` and `tungsten.tif` **without applying white balance**. Verify that the chosen flow does not reinject an Adobe profile; the goal is a sensor RGB as neutral as possible. [DCamProf Preparation](https://rawpedia.pixls.us/how_to_create_dcp_color_profiles/)

### 5. Measure and Generate DCP

Example for ColorChecker 24. Adapt paths to `ColorChecker.cht`, the reference file `cc24_ref.cie`, executables, and the actual camera name. Verify options according to the installed DCamProf version.

```sh
# Measure 24 patches: .ti3 and diagnostic image output.
scanin -v -dipn tungsten.tif ColorChecker.cht cc24_ref.cie tungsten-diag.tif
scanin -v -dipn daylight.tif ColorChecker.cht cc24_ref.cie daylight-diag.tif

# Colorimetric description of each illuminant.
dcamprof make-profile -i StdA tungsten.ti3 tungsten.json
dcamprof make-profile -i D50 -C daylight.ti3 daylight.json

# Dual illuminant DCP: A then D50/D65 order is mandatory.
dcamprof make-dcp \
  -n "Canon EOS R6 Mark III" \
  -d "IPR Canon EOS R6 Mark III v1" \
  -c "IPR calibration; see provenance manifest" \
  -t acr -o neutral \
  tungsten.json daylight.json Canon-EOS-R6-Mark-III-IPR-v1.dcp
```

The order `tungsten.json`, then `daylight.json`, is significant. Keep the default LUT smoothing for the first general-purpose profile: it limits the risk of overfitting only chart colors. [Example commands and illuminant order](https://rawpedia.pixls.us/how_to_create_dcp_color_profiles/)

### 6. Validate Before Publication

Validation must use both chart RAWs **and** distinct real RAWs.

- Measure average and maximum error (e.g., ΔE00), especially on grays, skin tones, saturated reds and blues. Archive excluded patches and diagnostics.
- Compare real images under daylight, tungsten, and mixed light: gray neutrality, saturation, white balance continuity, gradients without breaks.
- Do not modify IPR sliders to hide a profile defect.
- Define IPR thresholds after a first inter-camera series; do not invent a universal threshold before having a comparison reference.

An input profile relies on a measured characterization of a device and an independent reference space. This is the same general logic as an ICC profile. [ICC: Input Profile and Connection Space](https://www.color.org/getting-started/)

### 7. Install in IPR

1. Copy the validated DCP to `profiles/dcp/` locally.
2. Declare the camera in `profiles/index.json`.
3. Open a control RAW, verify the selected DCP with diagnostics, then export a reference PNG/TIFF.
4. Run `cameraProfileEngineTests`, `rawDecoderTests`, and a visual comparison before publishing.

```json
{
  "cameraMake": "Canon",
  "cameraModel": "Canon EOS R6 Mark III",
  "dcp": "dcp/Canon-EOS-R6-Mark-III-IPR-v1.dcp"
}
```

## Publication, License, and Evolution

Before publishing, verify rights on chart RAWs, chart reference data, generated DCP, and mentioned brands. Add a provenance manifest with author, date, tool versions, illuminants, DCP hash, license, and known limits. Never include Adobe tables or payloads.

| Phase | Deliverable | Risk |
|---|---|---|
| POC, one camera | Dual illuminant DCP and validated procedure | Low if private |
| 3-5 cameras | Provenance schema, ΔE00, non-regression | Medium |
| IPR Assistant | Guided capture and metadata archiving | Medium |
| Internal Generator | DCamProf replacement | High: color science and LUT |

Do not start with the internal generator. DCamProf must be the external reference: it allows defining required data and comparing a future IPR algorithm without depending on Adobe profiles.

## Main Sources

- [DCamProf — Repository, Compilation, and GPL-3.0](https://github.com/Beep6581/dcamprof)
- [RawPedia — Shooting and DCP Generation](https://rawpedia.pixls.us/how_to_create_dcp_color_profiles/)
- [ArgyllCMS — Profiling Tools and AGPL](https://www.argyllcms.com/)
- [International Color Consortium — Input Profiles](https://www.color.org/getting-started/)
