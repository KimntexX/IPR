#ifndef IPR_IMAGE_FILTERS_H
#define IPR_IMAGE_FILTERS_H

#include "Image.h"

#include <vector>

namespace ipr {

// The half of a normalized gaussian, centre first: the whole has size * 2 - 1 taps. Its radius
// is four sigmas, as OpenCV takes for floats. Empty when sigma is not above zero.
std::vector<float> gaussianHalfKernel(double sigma);

// A new image, blurred by this sigma in pixels, on both axes; beyond the borders the image is
// mirrored without repeating its edge (dcb|abcd|cba). F32 only, any channel count: throws
// std::invalid_argument on another depth. Sigma not above zero, or an empty image, gives a clone.
// threads as in parallelRows. The result does not depend on them.
Image gaussianBlur(const Image &image, double sigma, int threads = 0);

} // namespace ipr

#endif // IPR_IMAGE_FILTERS_H
