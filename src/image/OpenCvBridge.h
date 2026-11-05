#ifndef IPR_IMAGE_OPENCVBRIDGE_H
#define IPR_IMAGE_OPENCVBRIDGE_H

#include "Analysis.h"
#include "Arrange.h"
#include "Filters.h"
#include "Image.h"
#include "Neighbourhood.h"
#include "Pixels.h"
#include "Resize.h"
#include "Warp.h"

#include <opencv2/core.hpp>

#include <vector>

// The way between the image layer and cv::Mat while modules move from one to the other, without
// copying pixels. It goes away with OpenCV. Only this file knows both.
namespace ipr {

// The Mat's pixels, read only, kept alive by a reference to the Mat. Throws std::invalid_argument
// on a type the image layer does not have.
Image fromMat(const cv::Mat &mat);
// The Mat's pixels to write. Whoever else holds this Mat sees what is written: this is for a
// buffer the caller owns alone.
Image fromMutableMat(cv::Mat &mat);

// A header on the pixels, valid as long as the image is. cv::Mat has no read-only form: a frozen
// image comes out as a const Mat, and asMutableMat refuses it (std::logic_error).
const cv::Mat asMat(const Image &image);
cv::Mat asMutableMat(Image &image);

// A new Mat, blurred by this sigma in pixels. CV_32F of any channel count: throws
// std::invalid_argument on another depth. While the modules still hold Mats.
cv::Mat gaussianBlurred(const cv::Mat &mat, double sigma, BlurBorder border = BlurBorder::Mirror);
cv::Mat gaussianBlurred(const cv::Mat &mat, double sigmaX, double sigmaY, BlurBorder border);
// A new Mat at this size, or at these scales, as Resize.h. CV_32F of any channel count.
cv::Mat resized(const cv::Mat &mat, Size size, Interpolation interpolation);
cv::Mat resizedBy(const cv::Mat &mat, double scaleX, double scaleY, Interpolation interpolation);
// A new Mat read through two maps of single floats, or through a homography from the new Mat to this
// one, as Warp.h. CV_32F of any channel count.
cv::Mat remapped(const cv::Mat &mat, const cv::Mat &mapX, const cv::Mat &mapY, Resampling resampling, Outside outside);
cv::Mat warpedPerspective(const cv::Mat &mat, const Matrix33d &outToImage, Size size, Resampling resampling,
                          Outside outside);
// New Mats as Arrange.h gives them: any depth, except the pyramid in CV_32F.
cv::Mat turned(const cv::Mat &mat, int quarterTurns);
cv::Mat mirrored(const cv::Mat &mat, bool leftToRight, bool topToBottom);
cv::Mat transposed(const cv::Mat &mat);
cv::Mat padded(const cv::Mat &mat, int top, int bottom, int left, int right);
// A region of the Mat with margins, its own pixels where they exist, as copyMakeBorder on a region.
cv::Mat paddedRegion(const cv::Mat &whole, Recti region, int top, int bottom, int left, int right);
cv::Mat pyramidDown(const cv::Mat &mat);
// New Mats filtered as Neighbourhood.h does: CV_32F, and CV_8U too for the minimum and maximum.
cv::Mat sobel(const cv::Mat &mat, bool alongX, double scale = 1.0);
cv::Mat laplacian(const cv::Mat &mat);
cv::Mat centralDifference(const cv::Mat &mat, bool alongX);
cv::Mat median(const cv::Mat &mat, int side);
cv::Mat boxMean(const cv::Mat &mat, Size window, BlurBorder border);
cv::Mat minimum(const cv::Mat &mat, Size window, Beyond beyond);
cv::Mat maximum(const cv::Mat &mat, Size window, Beyond beyond);
// Pixel arithmetic as Pixels.h gives it.
bool allFinite(const cv::Mat &mat);
std::vector<cv::Mat> splitChannels(const cv::Mat &mat);
cv::Mat merged(const std::vector<cv::Mat> &planes);
cv::Mat channel(const cv::Mat &mat, int index);
cv::Mat withoutAlpha(const cv::Mat &rgba);
cv::Mat multiplied(const cv::Mat &a, const cv::Mat &b);
cv::Mat multiplied(const cv::Mat &a, const std::vector<double> &factors);
cv::Mat divided(const cv::Mat &a, const cv::Mat &b);
cv::Mat blended(const cv::Mat &a, double alpha, const cv::Mat &b, double beta, double gamma);
cv::Mat addScaled(const cv::Mat &a, double scale, const cv::Mat &b);
cv::Mat subtractedFrom(double value, const cv::Mat &a);
cv::Mat magnitude(const cv::Mat &x, const cv::Mat &y);
cv::Mat squareRoot(const cv::Mat &a);
cv::Mat atLeast(const cv::Mat &a, float bound);
cv::Mat atLeast(const cv::Mat &a, const std::vector<float> &bounds);
cv::Mat atMost(const cv::Mat &a, float bound);
cv::Mat transformed(const cv::Mat &a, const Matrix33f &matrix);
void replaceNaN(cv::Mat &mat, float value);
cv::Mat bitwiseOr(const cv::Mat &a, const cv::Mat &b);
cv::Mat bitwiseAnd(const cv::Mat &a, const cv::Mat &b);
cv::Mat bitwiseNot(const cv::Mat &a);
long long countNonZero(const cv::Mat &mat);
struct MatExtremes
{
    double smallest = 0.0, largest = 0.0;
    Point2i smallestAt, largestAt;
};
MatExtremes extremesOf(const cv::Mat &mat);
std::vector<double> meanOf(const cv::Mat &mat);
void meanAndDeviationOf(const cv::Mat &mat, std::vector<double> &mean, std::vector<double> &deviation);
// Analysis as Analysis.h gives it, on Mats of doubles for the linear algebra.
cv::Mat matchedTemplate(const cv::Mat &image, const cv::Mat &templ);
cv::Mat hannWindow(Size size);
Point2d phaseCorrelation(const cv::Mat &a, const cv::Mat &b, const cv::Mat &window);
bool symmetricEigen(const cv::Mat &matrix, cv::Mat &values, cv::Mat &vectors);
bool solveCholesky(const cv::Mat &a, const cv::Mat &b, cv::Mat &x);
// w: one singular value per column, largest first, as a column; vt: the right singular vectors as rows.
bool singularValues(const cv::Mat &a, cv::Mat &w, cv::Mat &vt);
// A pixel of a Mat seen as a small type of the layer, while modules still hold Mats.
template <typename T>
T &at(cv::Mat &mat, int y, int x) { return mat.ptr<T>(y)[x]; }
template <typename T>
const T &at(const cv::Mat &mat, int y, int x) { return mat.ptr<T>(y)[x]; }
template <typename T>
T &at(cv::Mat &mat, const Point2i &p) { return mat.ptr<T>(p.y)[p.x]; }
template <typename T>
const T &at(const cv::Mat &mat, const Point2i &p) { return mat.ptr<T>(p.y)[p.x]; }

inline cv::Size toCv(const Size &size) { return {size.width, size.height}; }
inline Size fromCv(const cv::Size &size) { return {size.width, size.height}; }
template <typename T>
cv::Point_<T> toCv(const Point<T> &p) { return {p.x, p.y}; }
template <typename T>
Point<T> fromCv(const cv::Point_<T> &p) { return {p.x, p.y}; }
template <typename T>
cv::Rect_<T> toCv(const Rect<T> &r) { return {r.x, r.y, r.width, r.height}; }
template <typename T>
Rect<T> fromCv(const cv::Rect_<T> &r) { return {r.x, r.y, r.width, r.height}; }

cv::Matx33d toCv(const Matrix33d &matrix);
Matrix33d fromCv(const cv::Matx33d &matrix);

} // namespace ipr

#endif // IPR_IMAGE_OPENCVBRIDGE_H
