#ifndef IPR_IMAGE_FILTERS_H
#define IPR_IMAGE_FILTERS_H

#include "Image.h"

#include <vector>

namespace ipr {

// The half of a normalized gaussian, centre first: the whole has size * 2 - 1 taps. Its radius
// is about four sigmas, the same as OpenCV takes for floats. Empty when sigma is not above zero.
std::vector<float> gaussianHalfKernel(double sigma);

// The normalized gaussian of `size` taps, as OpenCV's getGaussianKernel gives it in floats for a
// sigma above zero: each tap rounded to float, the sum taken in double, then each tap scaled.
std::vector<float> gaussianKernel(int size, double sigma);

// What lies beyond the borders: the image mirrored without repeating its edge (dcb|abcd|cba),
// mirrored repeating it (dcba|abcd|dcba), or its last pixel again (aaa|abcd|ddd).
enum class BlurBorder { Mirror, MirrorEdge, LastPixel };

// A new image, blurred by this sigma in pixels, on both axes. F32 only, any channel count: throws
// std::invalid_argument on another depth. Sigma not above zero, or an empty image, gives a clone.
// threads as in parallelRows. The result does not depend on them.
Image gaussianBlur(const Image &image, double sigma, int threads = 0, BlurBorder border = BlurBorder::Mirror);

// The same into a buffer of the image's size, depth and channel count, or std::invalid_argument.
// out may not be the image itself.
void gaussianBlurInto(const Image &image, Image &out, double sigma, int threads = 0,
                      BlurBorder border = BlurBorder::Mirror);
// With a sigma along each axis; an axis whose sigma is not above zero is left as it is.
void gaussianBlurInto(const Image &image, Image &out, double sigmaX, double sigmaY, int threads = 0,
                      BlurBorder border = BlurBorder::Mirror);

} // namespace ipr

#endif // IPR_IMAGE_FILTERS_H
