#ifndef IPR_IMAGE_NEIGHBOURHOOD_H
#define IPR_IMAGE_NEIGHBOURHOOD_H

#include "Filters.h"
#include "Image.h"

namespace ipr {

// Small filters over each pixel's neighbours, as OpenCV computes them for floats. Each writes into a
// buffer of the image's size, depth and channels, or throws std::invalid_argument; out may not be
// the image. F32 unless said otherwise. threads as in parallelRows; the result does not depend on it.

// The 3 x 3 Sobel derivative along x or along y, times scale; mirrored without repeating the edge.
void sobelInto(const Image &image, Image &out, bool alongX, double scale = 1.0, int threads = 0);

// The four-neighbour Laplacian, 0 1 0 / 1 -4 1 / 0 1 0; mirrored without repeating the edge.
void laplacianInto(const Image &image, Image &out, int threads = 0);

// The difference of the next and the previous pixel, halved, along x or y; mirrored repeating the edge.
void centralDifferenceInto(const Image &image, Image &out, bool alongX, int threads = 0);

// The median of each 3 x 3 or 5 x 5 window, the last pixel repeated beyond the edge.
void medianInto(const Image &image, Image &out, int side, int threads = 0);

// The mean of each width x height window, beyond the edge as border says.
void boxMeanInto(const Image &image, Image &out, int width, int height, BlurBorder border, int threads = 0);

// The smallest or largest value of each width x height window. Beyond the edge: nothing, so the
// window keeps what lies inside, or zero. F32 or U8.
enum class Beyond { Nothing, Zero };
void minimumInto(const Image &image, Image &out, int width, int height, Beyond beyond, int threads = 0);
void maximumInto(const Image &image, Image &out, int width, int height, Beyond beyond, int threads = 0);

// The same into new images. An empty image gives an empty one.
Image sobel(const Image &image, bool alongX, double scale = 1.0);
Image laplacian(const Image &image);
Image centralDifference(const Image &image, bool alongX);
Image median(const Image &image, int side);
Image boxMean(const Image &image, Size window, BlurBorder border);
Image minimum(const Image &image, Size window, Beyond beyond);
Image maximum(const Image &image, Size window, Beyond beyond);

} // namespace ipr

#endif // IPR_IMAGE_NEIGHBOURHOOD_H
