#include "OpenCvBridge.h"

#include <memory>
#include <stdexcept>

namespace ipr {

namespace {

Depth depthOf(const cv::Mat &mat)
{
    switch (mat.depth()) {
    case CV_8U: return Depth::U8;
    case CV_16U: return Depth::U16;
    case CV_32F: return Depth::F32;
    case CV_64F: return Depth::F64;
    default: throw std::invalid_argument("The image layer has no such depth");
    }
}

int cvDepthOf(Depth depth)
{
    switch (depth) {
    case Depth::U8: return CV_8U;
    case Depth::U16: return CV_16U;
    case Depth::F32: return CV_32F;
    case Depth::F64: return CV_64F;
    }
    throw std::invalid_argument("Unknown depth");
}

cv::Mat headerOn(const Image &image)
{
    if (image.empty())
        return {};
    // The header never writes: which pixels may be written is the caller's to ask for.
    return cv::Mat(image.height(), image.width(), CV_MAKETYPE(cvDepthOf(image.depth()), image.channels()),
                   const_cast<std::uint8_t *>(image.row<std::uint8_t>(0)), std::size_t(image.stepBytes()));
}

void checkPlanar(const cv::Mat &mat)
{
    if (mat.dims > 2)
        throw std::invalid_argument("The image layer has two dimensions");
}

} // namespace

Image fromMat(const cv::Mat &mat)
{
    checkPlanar(mat);
    if (mat.empty())
        return {};
    return Image::wrapReadOnly(mat.data, {mat.cols, mat.rows}, depthOf(mat), mat.channels(), std::ptrdiff_t(mat.step[0]),
                               std::make_shared<cv::Mat>(mat));
}

Image fromMutableMat(cv::Mat &mat)
{
    checkPlanar(mat);
    if (mat.empty())
        return {};
    return Image::wrap(mat.data, {mat.cols, mat.rows}, depthOf(mat), mat.channels(), std::ptrdiff_t(mat.step[0]),
                       std::make_shared<cv::Mat>(mat));
}

const cv::Mat asMat(const Image &image)
{
    return headerOn(image);
}

cv::Mat asMutableMat(Image &image)
{
    if (image.frozen())
        throw std::logic_error("These pixels are shared: clone them to change them");
    return headerOn(image);
}

cv::Matx33d toCv(const Matrix33d &matrix)
{
    cv::Matx33d out;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out(r, c) = matrix(std::size_t(r), std::size_t(c));
    return out;
}

Matrix33d fromCv(const cv::Matx33d &matrix)
{
    Matrix33d out;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out(std::size_t(r), std::size_t(c)) = matrix(r, c);
    return out;
}

} // namespace ipr
