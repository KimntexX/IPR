#ifndef IPR_RAW_DEMOSAIC_H
#define IPR_RAW_DEMOSAIC_H

#include <opencv2/core.hpp>

#include <array>
#include <functional>

// Bayer demosaic of IPR: RCD (ratio-corrected demosaicing), written from the
// published description (L. Sanz Rodriguez, 2017), its green estimates blended
// evenly with colour-difference (Laplacian) ones for the pixel-scale sharpness
// of Lightroom's rendering, with an optional 3x3 median on R - G and B - G. It replaces only LibRaw's interpolation step: black level,
// white balance and colour conversion stay upstream. The image is processed in
// tiles on a bounded number of threads. Study and measurements:
// docs/validation/analyses/analyse-dematricage.md.
namespace Demosaic {

// Colour (0 red, 1 green, 2 blue) of the 2x2 Bayer cell, by row then column.
using Pattern = std::array<std::array<int, 2>, 2>;

// 3x3 median on R - G and B - G after the interpolation, off or only where the
// luminance is flat at the scale of the tile's noise: flat colour noise falls
// below Lightroom's while hair and textures keep their colour detail.
enum class ChromaMedian { Off, FlatOnly };

// Median of the opening, at every ISO.
constexpr ChromaMedian OpeningMedian = ChromaMedian::FlatOnly;

// Worker threads: the host's, one left for the interface, at most 8 (the
// demosaic and denoise studies gain nothing beyond).
int workerThreads();

// Interpolates a white-balanced Bayer mosaic (CV_32FC1, white 1, the pattern's
// colour at each site) into camera RGB (CV_32FC3, same size). The median works
// tile by tile; beforeMedian, when given, receives the interpolation before it
// (the development's intermediate capture). Returns false when cancelled or on
// an invalid mosaic; the outputs are then incomplete.
bool interpolate(const cv::Mat &mosaic, const Pattern &pattern, ChromaMedian median, cv::Mat &rgb,
                 cv::Mat *beforeMedian, const std::function<bool()> &cancelled);

} // namespace Demosaic

#endif // IPR_RAW_DEMOSAIC_H
