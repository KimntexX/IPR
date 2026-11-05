#ifndef IPR_IMAGE_PIXELS_H
#define IPR_IMAGE_PIXELS_H

#include "Image.h"
#include "Types.h"

#include <vector>

namespace ipr {

// Arithmetic pixel by pixel and over a whole image, as OpenCV computes it. Images are F32 unless
// said otherwise; a buffer is of the image's size and of the channels the operation gives, or the
// call throws std::invalid_argument. out may be the image itself where the operation reads each
// value once, before writing it: every elementwise operation here.

// No value is NaN or infinite. Integer images always pass.
bool allFinite(const Image &image);

// Channels taken apart and put together; a channel alone; the first channels kept, as RGBA to RGB.
std::vector<Image> splitChannels(const Image &image);
void mergeChannelsInto(const std::vector<const Image *> &planes, Image &out);
void channelInto(const Image &image, Image &out, int channel);
void firstChannelsInto(const Image &image, Image &out);

// a * b, a / b, value by value, of the same channels; or a by one factor per channel.
void multiplyInto(const Image &a, const Image &b, Image &out);
void multiplyInto(const Image &a, const std::vector<double> &factors, Image &out);
void divideInto(const Image &a, const Image &b, Image &out);
// a * alpha + b * beta + gamma, in floats, as addWeighted.
void blendInto(const Image &a, double alpha, const Image &b, double beta, double gamma, Image &out);
// a * scale + b, as scaleAdd.
void addScaledInto(const Image &a, double scale, const Image &b, Image &out);
// value - a.
void subtractFromInto(double value, const Image &a, Image &out);
// sqrt(x * x + y * y); sqrt(a).
void magnitudeInto(const Image &x, const Image &y, Image &out);
void squareRootInto(const Image &a, Image &out);
// The larger, or the smaller, of each value and a bound.
void atLeastInto(const Image &a, float bound, Image &out);
void atLeastInto(const Image &a, const std::vector<float> &bounds, Image &out);
void atMostInto(const Image &a, float bound, Image &out);
// out = matrix * pixel, three channels to three, in floats.
void transformInto(const Image &a, const Matrix<float, 3, 3> &matrix, Image &out);
// Each NaN becomes value, in place.
void replaceNaN(Image &image, float value);

// Bytes: a | b, a & b, ~a.
void bitwiseOrInto(const Image &a, const Image &b, Image &out);
void bitwiseAndInto(const Image &a, const Image &b, Image &out);
void bitwiseNotInto(const Image &a, Image &out);

// Over a whole image of one channel: how many values are not zero (F32 or bytes); the smallest and
// largest and where each first lies, rows first.
long long countNonZero(const Image &image);
struct Extremes
{
    double smallest = 0.0, largest = 0.0;
    Point<int> smallestAt, largestAt;
};
Extremes extremesOf(const Image &image);

// Per channel: the mean, and the standard deviation over all the values, in double.
std::vector<double> meanOf(const Image &image);
void meanAndDeviationOf(const Image &image, std::vector<double> &mean, std::vector<double> &deviation);

} // namespace ipr

#endif // IPR_IMAGE_PIXELS_H
