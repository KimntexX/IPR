#ifndef IPR_IMAGE_RESIZE_H
#define IPR_IMAGE_RESIZE_H

#include "Image.h"

namespace ipr {

// Linear reads the two nearest pixels along each axis, pixel centres aligned. Area averages what
// each new pixel covers when reducing on both axes; otherwise it interpolates as OpenCV does for
// INTER_AREA when enlarging.
enum class Interpolation { Linear, Area };

// A new image of this size, as OpenCV's resize gives it for floats. F32 only, any channel count:
// throws std::invalid_argument on another depth, an empty image or an empty size.
Image resized(const Image &image, Size size, Interpolation interpolation, int threads = 0);

// The size is the image's times the scale, rounded; the pixels follow the scale itself, not the
// rounded size, as OpenCV does when given scales.
Image resizedBy(const Image &image, double scaleX, double scaleY, Interpolation interpolation, int threads = 0);
Size scaledSize(Size size, double scaleX, double scaleY);

// Into a buffer of the depth and channels of the image, at the buffer's size, or at the scales
// given: std::invalid_argument when the buffer does not match. out may not be the image itself.
void resizeInto(const Image &image, Image &out, Interpolation interpolation, int threads = 0);
void resizeInto(const Image &image, Image &out, double scaleX, double scaleY, Interpolation interpolation,
                int threads = 0);

} // namespace ipr

#endif // IPR_IMAGE_RESIZE_H
