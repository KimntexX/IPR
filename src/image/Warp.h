#ifndef IPR_IMAGE_WARP_H
#define IPR_IMAGE_WARP_H

#include "Image.h"
#include "Types.h"

namespace ipr {

// How a pixel between pixels is read: from 2 x 2 neighbours, 4 x 4 or 8 x 8. Cubic and Lanczos read
// their weights at a 1/32 of a pixel, as OpenCV does.
enum class Resampling { Linear, Cubic, Lanczos4 };

// What lies beyond the image: its last pixel again, or zero.
enum class Outside { LastPixel, Zero };

// out(x, y) reads the image at (mapX(x, y), mapY(x, y)). The maps are F32 with one channel, of out's
// size; the image and out are F32 of the same channel count. std::invalid_argument otherwise.
void remapInto(const Image &image, Image &out, const Image &mapX, const Image &mapY, Resampling resampling,
               Outside outside, int threads = 0);

// out(x, y) reads the image where outToImage sends (x, y), a homography, as OpenCV's warpPerspective
// with WARP_INVERSE_MAP. A point sent to infinity reads at the origin, as there.
void warpPerspectiveInto(const Image &image, Image &out, const Matrix33d &outToImage, Resampling resampling,
                         Outside outside, int threads = 0);

// The same into new images, of the maps' size or of this size. Empty for an empty image or map.
Image remapped(const Image &image, const Image &mapX, const Image &mapY, Resampling resampling, Outside outside);
Image warpedPerspective(const Image &image, const Matrix33d &outToImage, Size size, Resampling resampling,
                        Outside outside);

} // namespace ipr

#endif // IPR_IMAGE_WARP_H
