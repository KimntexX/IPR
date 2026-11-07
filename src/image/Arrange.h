#ifndef IPR_IMAGE_ARRANGE_H
#define IPR_IMAGE_ARRANGE_H

#include "Image.h"
#include "Types.h"

namespace ipr {

// Pixels moved, not computed: any depth and channel count. Each writes into a buffer of the image's
// depth and channels at the size it gives, or throws std::invalid_argument; out may not be the image.

// Turned clockwise by quarter turns, 0 to 3; odd turns swap width and height.
void turnInto(const Image &image, Image &out, int quarterTurns);
// Mirrored left to right, top to bottom, or both.
void mirrorInto(const Image &image, Image &out, bool leftToRight, bool topToBottom);
// Rows become columns.
void transposeInto(const Image &image, Image &out);
// Extended by these margins, mirrored without repeating the edge (dcb|abcd|cba).
void padInto(const Image &image, Image &out, int top, int bottom, int left, int right);
// A region of the image extended by these margins: the image's own pixels where the margins fall
// inside it, mirrored only beyond its edges, as OpenCV's copyMakeBorder does on a region of a Mat.
void padRegionInto(const Image &image, Image &out, Rect<int> region, int top, int bottom, int left, int right);

// Halved as OpenCV's pyrDown: weights 1 4 6 4 1 on each axis, mirrored without repeating the edge,
// every other pixel kept. F32 only; out is ((width + 1) / 2, (height + 1) / 2).
void pyramidDownInto(const Image &image, Image &out, int threads = 0);
Size pyramidDownSize(Size size);

// The same into new images. An empty image gives an empty one.
Image turned(const Image &image, int quarterTurns);
Image mirrored(const Image &image, bool leftToRight, bool topToBottom);
Image transposed(const Image &image);
Image padded(const Image &image, int top, int bottom, int left, int right);
Image paddedRegion(const Image &image, Rect<int> region, int top, int bottom, int left, int right);
Image pyramidDown(const Image &image);

} // namespace ipr

#endif // IPR_IMAGE_ARRANGE_H
