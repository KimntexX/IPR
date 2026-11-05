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

cv::Mat gaussianBlurred(const cv::Mat &mat, double sigma, BlurBorder border)
{
    return gaussianBlurred(mat, sigma, sigma, border);
}

cv::Mat gaussianBlurred(const cv::Mat &mat, double sigmaX, double sigmaY, BlurBorder border)
{
    if (mat.empty())
        return {};
    cv::Mat blurred(mat.size(), mat.type());
    Image target = fromMutableMat(blurred);
    gaussianBlurInto(fromMat(mat), target, sigmaX, sigmaY, 0, border);
    return blurred;
}

cv::Mat resized(const cv::Mat &mat, cv::Size size, Interpolation interpolation)
{
    if (mat.empty())
        return {};
    cv::Mat out(size, mat.type());
    Image target = fromMutableMat(out);
    resizeInto(fromMat(mat), target, interpolation);
    return out;
}

cv::Mat resizedBy(const cv::Mat &mat, double scaleX, double scaleY, Interpolation interpolation)
{
    if (mat.empty())
        return {};
    cv::Mat out(toCv(scaledSize(fromCv(mat.size()), scaleX, scaleY)), mat.type());
    Image target = fromMutableMat(out);
    resizeInto(fromMat(mat), target, scaleX, scaleY, interpolation);
    return out;
}

cv::Mat remapped(const cv::Mat &mat, const cv::Mat &mapX, const cv::Mat &mapY, Resampling resampling, Outside outside)
{
    if (mat.empty() || mapX.empty())
        return {};
    cv::Mat out(mapX.size(), mat.type());
    Image target = fromMutableMat(out);
    remapInto(fromMat(mat), target, fromMat(mapX), fromMat(mapY), resampling, outside);
    return out;
}

cv::Mat warpedPerspective(const cv::Mat &mat, const cv::Matx33d &outToImage, cv::Size size, Resampling resampling,
                          Outside outside)
{
    if (mat.empty() || size.empty())
        return {};
    cv::Mat out(size, mat.type());
    Image target = fromMutableMat(out);
    warpPerspectiveInto(fromMat(mat), target, fromCv(outToImage), resampling, outside);
    return out;
}

cv::Mat turned(const cv::Mat &mat, int quarterTurns)
{
    if (mat.empty())
        return {};
    cv::Mat out(quarterTurns % 2 ? cv::Size(mat.rows, mat.cols) : mat.size(), mat.type());
    Image target = fromMutableMat(out);
    turnInto(fromMat(mat), target, quarterTurns);
    return out;
}

cv::Mat mirrored(const cv::Mat &mat, bool leftToRight, bool topToBottom)
{
    if (mat.empty())
        return {};
    cv::Mat out(mat.size(), mat.type());
    Image target = fromMutableMat(out);
    mirrorInto(fromMat(mat), target, leftToRight, topToBottom);
    return out;
}

cv::Mat transposed(const cv::Mat &mat)
{
    if (mat.empty())
        return {};
    cv::Mat out(mat.cols, mat.rows, mat.type());
    Image target = fromMutableMat(out);
    transposeInto(fromMat(mat), target);
    return out;
}

cv::Mat padded(const cv::Mat &mat, int top, int bottom, int left, int right)
{
    if (mat.empty())
        return {};
    cv::Mat out(mat.rows + top + bottom, mat.cols + left + right, mat.type());
    Image target = fromMutableMat(out);
    padInto(fromMat(mat), target, top, bottom, left, right);
    return out;
}

cv::Mat paddedRegion(const cv::Mat &whole, cv::Rect region, int top, int bottom, int left, int right)
{
    if (whole.empty())
        return {};
    cv::Mat out(region.height + top + bottom, region.width + left + right, whole.type());
    Image target = fromMutableMat(out);
    padRegionInto(fromMat(whole), target, fromCv(region), top, bottom, left, right);
    return out;
}

cv::Mat pyramidDown(const cv::Mat &mat)
{
    if (mat.empty())
        return {};
    cv::Mat out(toCv(pyramidDownSize(fromCv(mat.size()))), mat.type());
    Image target = fromMutableMat(out);
    pyramidDownInto(fromMat(mat), target);
    return out;
}

cv::Mat sobel(const cv::Mat &mat, bool alongX, double scale)
{
    if (mat.empty())
        return {};
    cv::Mat out(mat.size(), mat.type());
    Image target = fromMutableMat(out);
    sobelInto(fromMat(mat), target, alongX, scale);
    return out;
}

cv::Mat laplacian(const cv::Mat &mat)
{
    if (mat.empty())
        return {};
    cv::Mat out(mat.size(), mat.type());
    Image target = fromMutableMat(out);
    laplacianInto(fromMat(mat), target);
    return out;
}

cv::Mat centralDifference(const cv::Mat &mat, bool alongX)
{
    if (mat.empty())
        return {};
    cv::Mat out(mat.size(), mat.type());
    Image target = fromMutableMat(out);
    centralDifferenceInto(fromMat(mat), target, alongX);
    return out;
}

cv::Mat median(const cv::Mat &mat, int side)
{
    if (mat.empty())
        return {};
    cv::Mat out(mat.size(), mat.type());
    Image target = fromMutableMat(out);
    medianInto(fromMat(mat), target, side);
    return out;
}

cv::Mat boxMean(const cv::Mat &mat, cv::Size window, BlurBorder border)
{
    if (mat.empty())
        return {};
    cv::Mat out(mat.size(), mat.type());
    Image target = fromMutableMat(out);
    boxMeanInto(fromMat(mat), target, window.width, window.height, border);
    return out;
}

cv::Mat minimum(const cv::Mat &mat, cv::Size window, Beyond beyond)
{
    if (mat.empty())
        return {};
    cv::Mat out(mat.size(), mat.type());
    Image target = fromMutableMat(out);
    minimumInto(fromMat(mat), target, window.width, window.height, beyond);
    return out;
}

cv::Mat maximum(const cv::Mat &mat, cv::Size window, Beyond beyond)
{
    if (mat.empty())
        return {};
    cv::Mat out(mat.size(), mat.type());
    Image target = fromMutableMat(out);
    maximumInto(fromMat(mat), target, window.width, window.height, beyond);
    return out;
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
