#ifndef IPR_RAW_DEMOSAIC_H
#define IPR_RAW_DEMOSAIC_H

#include <array>
#include <cstdint>
#include <functional>

// Bayer demosaic of IPR: RCD (ratio-corrected demosaicing), written from the
// published description (L. Sanz Rodriguez, 2017), with an optional 3x3 median
// on R - G and B - G. It replaces only LibRaw's interpolation step: black level,
// white balance and colour conversion stay upstream. The image is processed in
// tiles on a bounded number of threads. Study and measurements:
// docs/validation/analyses/analyse-dematricage.md.
namespace Demosaic {

// Colour (0 red, 1 green, 2 blue) of the 2x2 Bayer cell, by row then column.
using Pattern = std::array<std::array<int, 2>, 2>;

// One pixel of LibRaw's image: four 16-bit channels.
using Pixel = std::uint16_t[4];

// 3x3 median on R - G and B - G after the interpolation, off or only where the
// luminance is flat at the scale of the tile's noise: flat colour noise falls
// below Lightroom's while hair and textures keep their colour detail.
enum class ChromaMedian { Off, FlatOnly };

// Median of the opening, at every ISO.
constexpr ChromaMedian OpeningMedian = ChromaMedian::FlatOnly;

// Worker threads: the host's, one left for the interface, at most 8 (the
// demosaic and denoise studies gain nothing beyond).
int workerThreads();

// Interpolates in place a width x height image of four 16-bit channels in which
// only the pattern's colour of each pixel is set (LibRaw's layout before
// interpolation, white-balanced); fills channels 0 to 2. Returns false when
// cancelled; the image is then incomplete.
bool interpolate(Pixel *image, int width, int height, const Pattern &pattern,
                 ChromaMedian median, const std::function<bool()> &cancelled = {});

} // namespace Demosaic

#endif // IPR_RAW_DEMOSAIC_H
