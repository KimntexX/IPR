#ifndef IPR_IMAGE_OPENCVBRIDGE_H
#define IPR_IMAGE_OPENCVBRIDGE_H

#include "Image.h"

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
