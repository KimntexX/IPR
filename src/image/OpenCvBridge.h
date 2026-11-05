#ifndef IPR_IMAGE_OPENCVBRIDGE_H
#define IPR_IMAGE_OPENCVBRIDGE_H

#include "Arrange.h"
#include "Filters.h"
#include "Image.h"
#include "Resize.h"
#include "Warp.h"

#include <opencv2/core.hpp>

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
cv::Mat resized(const cv::Mat &mat, cv::Size size, Interpolation interpolation);
cv::Mat resizedBy(const cv::Mat &mat, double scaleX, double scaleY, Interpolation interpolation);
// A new Mat read through two maps of single floats, or through a homography from the new Mat to this
// one, as Warp.h. CV_32F of any channel count.
cv::Mat remapped(const cv::Mat &mat, const cv::Mat &mapX, const cv::Mat &mapY, Resampling resampling, Outside outside);
cv::Mat warpedPerspective(const cv::Mat &mat, const cv::Matx33d &outToImage, cv::Size size, Resampling resampling,
                          Outside outside);
// New Mats as Arrange.h gives them: any depth, except the pyramid in CV_32F.
cv::Mat turned(const cv::Mat &mat, int quarterTurns);
cv::Mat mirrored(const cv::Mat &mat, bool leftToRight, bool topToBottom);
cv::Mat transposed(const cv::Mat &mat);
cv::Mat padded(const cv::Mat &mat, int top, int bottom, int left, int right);
// A region of the Mat with margins, its own pixels where they exist, as copyMakeBorder on a region.
cv::Mat paddedRegion(const cv::Mat &whole, cv::Rect region, int top, int bottom, int left, int right);
cv::Mat pyramidDown(const cv::Mat &mat);

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
