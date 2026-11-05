#ifndef IPR_IMAGE_FILTERS_H
#define IPR_IMAGE_FILTERS_H

#include "Image.h"

#include <vector>

namespace ipr {

// The half of a normalized gaussian, centre first: the whole has size * 2 - 1 taps. Its radius
// is four sigmas, as OpenCV takes for floats. Empty when sigma is not above zero.
std::vector<float> gaussianHalfKernel(double sigma);

// What lies beyond the borders: the image mirrored without repeating its edge (dcb|abcd|cba), or
// repeating it (dcba|abcd|dcba).
enum class BlurBorder { Mirror, MirrorEdge };

// A new image, blurred by this sigma in pixels, on both axes. F32 only, any channel count: throws
// std::invalid_argument on another depth. Sigma not above zero, or an empty image, gives a clone.
// threads as in parallelRows. The result does not depend on them.
Image gaussianBlur(const Image &image, double sigma, int threads = 0, BlurBorder border = BlurBorder::Mirror);

// The same into a buffer of the image's size, depth and channel count, or std::invalid_argument.
// out may not be the image itself.
void gaussianBlurInto(const Image &image, Image &out, double sigma, int threads = 0,
                      BlurBorder border = BlurBorder::Mirror);

} // namespace ipr

#endif // IPR_IMAGE_FILTERS_H
