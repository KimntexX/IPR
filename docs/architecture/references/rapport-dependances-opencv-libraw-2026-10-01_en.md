# Dependency Decision — OpenCV and LibRaw

## Conclusion

**Recommended decision: keep OpenCV and LibRaw.** Their removal does not reduce the observed opening time and would shift a significant volume of proven code into IPR. The functional risk is particularly high for CR2/CR3; no direct replacement covers current decoding, demosaicing, metadata, and rendering with lower cost.

Measurements from this session show that the remaining cost is mainly in LibRaw development, not in DLL loading or the OpenCV layer. Removing a library before replacing this work therefore does not provide measurable performance gain.

## Implementation Status

| Dependency | Current Role in IPR | If Removed |
|---|---|---|
| OpenCV | `cv::Mat`/`CV_32FC4` images, proxy resize, LCP remapping, filters, histogram, and pipeline calculations | Would need to rewrite or replace a large part of pixel pipeline and its tests |
| LibRaw | CR2/CR3/DNG reading, metadata, black level, decompression, and 16-bit RAW development | CR2/CR3/DNG no longer importable until a complete RAW engine is delivered |

Qt does not decode RAW, and OpenCV does not fill this role. LibRaw exposes a loading, decompression, and post-processing API; its `open_file()` also reads metadata. [LibRaw C++ API](https://www.libraw.org/docs/API-CXX.html)

## Option 1 — Keep Both Dependencies (Recommended)

Cost: low. Risk: low, limited to usual updates and Windows bundle validation. Existing code retains its results and tests. OpenCV 4.5+ is Apache-2.0 licensed; compliance consists of including the license notice, not opening IPR code. [OpenCV License](https://opencv.org/license/)

LibRaw is distributed under LGPL 2.1 or CDDL 1.0 depending on project choice; linking mode and notice distribution must be reviewed before commercial distribution. Its documentation also indicates that multiple LibRaw processors can work in parallel, but each consumes memory. [LibRaw License and Presentation](https://www.libraw.org/docs)

## Option 2 — Remove OpenCV Only

Possible benefit: reduce the number of DLLs and deployment size. This gain does not reduce RAW decoding time, and replacement is not free: IPR would need to provide floating images, quality resize, bilinear remapping, LUT/curves, convolutions, and pixel processing with comparable precision and performance.

Engineering estimate: **6 to 12 weeks** for targeted replacement, then visual and performance validation; **high** risk of regressions on LCP edges, formats/channels, and exports. This is an internal estimate, not a promise or publisher data. A replacement library would mostly shift rather than eliminate the dependency.

## Option 3 — Remove LibRaw

This option immediately removes actual CR2/CR3/DNG support. The closest replacement candidate, RawSpeed, describes itself as a first stage: it reads almost no metadata beyond white balance, does not perform color/WB correction, demosaicing, or displayable vignette. It would therefore be necessary to write and validate everything following the RAW mosaic, in addition to integrating camera data. [RawSpeed: Scope and Limits](https://github.com/darktable-org/rawspeed)

Engineering estimate: **12 to 24 weeks minimum** to achieve a comparable CR2/CR3 path, with **very high** risk on camera compatibility, color science, noise, demosaicing artifacts, and format updates. RawSpeed also requires delivering its camera definitions with the exact code version, increasing maintenance cost.

The Adobe DNG SDK is not a substitute for LibRaw for proprietary RAW: it handles the DNG format and flows converted to DNG. [Adobe DNG Presentation](https://helpx.adobe.com/camera-raw/desktop/dng-and-file-formats/digital-negative.html)

## Option 4 — `cv::UMat` / OpenCL (Point D)

`cv::UMat` retains OpenCV: it is neither a dependency removal nor a solution to internal LibRaw development. At the current scale, the OpenCV DCP/LCP part has been reduced to about 0.8 s thanks to multi-core CPU, while LibRaw remains around 2.8 s. An immediate migration would risk host-GPU copies and variable results depending on drivers, for a secondary area.

Decision: do not address D blindly. First, benchmark on the target Windows PC, including rendering and export. If GPU accelerates a complete proxy repeatedly, limit experimentation to a branch and keep the `cv::Mat` path as non-regression reference.

## Higher Yield Performance Plan

1. Verify on a normal Windows installation the persistent DCP/LCP selection cache, which avoids Adobe scan on subsequent startups.
2. If the user must see something within a second, first display the LibRaw embedded thumbnail, then replace with the full render. Quality and settings would only be provisional. LibRaw provides `unpack_thumb()` and `dcraw_make_mem_thumb()`; [LibRaw Thumbnails](https://www.libraw.org/docs/API-CXX.html)
3. Add LibRaw progress and cancellation: its callback is invoked during post-processing and can interrupt work. This does not reduce CPU time but makes waiting controllable. [LibRaw Callback](https://www.libraw.org/docs/API-CXX.html)
4. Only re-evaluate OpenCL after these gains, with measurements on actually deployed hardware.
