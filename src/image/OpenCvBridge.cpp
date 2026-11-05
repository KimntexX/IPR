#include "OpenCvBridge.h"

#include <cmath>
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

bool allFinite(const cv::Mat &mat)
{
    return mat.empty() || allFinite(fromMat(mat));
}

std::vector<cv::Mat> splitChannels(const cv::Mat &mat)
{
    std::vector<cv::Mat> planes;
    if (mat.empty())
        return planes;
    for (int c = 0; c < mat.channels(); ++c)
        planes.push_back(channel(mat, c));
    return planes;
}

cv::Mat merged(const std::vector<cv::Mat> &planes)
{
    if (planes.empty() || planes.front().empty())
        return {};
    cv::Mat out(planes.front().size(), CV_MAKETYPE(planes.front().depth(), int(planes.size())));
    std::vector<Image> images;
    std::vector<const Image *> pointers;
    for (const cv::Mat &plane : planes)
        images.push_back(fromMat(plane));
    for (const Image &image : images)
        pointers.push_back(&image);
    Image target = fromMutableMat(out);
    mergeChannelsInto(pointers, target);
    return out;
}

cv::Mat channel(const cv::Mat &a, int index)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), CV_MAKETYPE(a.depth(), 1));
    Image target = fromMutableMat(out);
    channelInto(fromMat(a), target, index);
    return out;
}

cv::Mat withoutAlpha(const cv::Mat &a)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), CV_MAKETYPE(a.depth(), 3));
    Image target = fromMutableMat(out);
    firstChannelsInto(fromMat(a), target);
    return out;
}

cv::Mat multiplied(const cv::Mat &a, const cv::Mat &b)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    multiplyInto(fromMat(a), fromMat(b), target);
    return out;
}

cv::Mat multiplied(const cv::Mat &a, const std::vector<double> &factors)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    multiplyInto(fromMat(a), factors, target);
    return out;
}

cv::Mat divided(const cv::Mat &a, const cv::Mat &b)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    divideInto(fromMat(a), fromMat(b), target);
    return out;
}

cv::Mat blended(const cv::Mat &a, double alpha, const cv::Mat &b, double beta, double gamma)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    blendInto(fromMat(a), alpha, fromMat(b), beta, gamma, target);
    return out;
}

cv::Mat addScaled(const cv::Mat &a, double scale, const cv::Mat &b)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    addScaledInto(fromMat(a), scale, fromMat(b), target);
    return out;
}

cv::Mat subtractedFrom(double value, const cv::Mat &a)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    subtractFromInto(value, fromMat(a), target);
    return out;
}

cv::Mat magnitude(const cv::Mat &a, const cv::Mat &b)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    magnitudeInto(fromMat(a), fromMat(b), target);
    return out;
}

cv::Mat squareRoot(const cv::Mat &a)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    squareRootInto(fromMat(a), target);
    return out;
}

cv::Mat atLeast(const cv::Mat &a, float bound)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    atLeastInto(fromMat(a), bound, target);
    return out;
}

cv::Mat atLeast(const cv::Mat &a, const std::vector<float> &bounds)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    atLeastInto(fromMat(a), bounds, target);
    return out;
}

cv::Mat atMost(const cv::Mat &a, float bound)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    atMostInto(fromMat(a), bound, target);
    return out;
}

cv::Mat transformed(const cv::Mat &a, const cv::Matx33f &matrix)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Matrix<float, 3, 3> m;
    for (int i = 0; i < 9; ++i)
        m.values[size_t(i)] = matrix.val[i];
    Image target = fromMutableMat(out);
    transformInto(fromMat(a), m, target);
    return out;
}

void replaceNaN(cv::Mat &mat, float value)
{
    if (mat.empty())
        return;
    Image image = fromMutableMat(mat);
    replaceNaN(image, value);
}

cv::Mat bitwiseOr(const cv::Mat &a, const cv::Mat &b)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    bitwiseOrInto(fromMat(a), fromMat(b), target);
    return out;
}

cv::Mat bitwiseAnd(const cv::Mat &a, const cv::Mat &b)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    bitwiseAndInto(fromMat(a), fromMat(b), target);
    return out;
}

cv::Mat bitwiseNot(const cv::Mat &a)
{
    if (a.empty())
        return {};
    cv::Mat out(a.size(), a.type());
    Image target = fromMutableMat(out);
    bitwiseNotInto(fromMat(a), target);
    return out;
}

long long countNonZero(const cv::Mat &mat)
{
    return mat.empty() ? 0 : countNonZero(fromMat(mat));
}

MatExtremes extremesOf(const cv::Mat &mat)
{
    const Extremes found = extremesOf(fromMat(mat));
    return {found.smallest, found.largest, {found.smallestAt.x, found.smallestAt.y}, {found.largestAt.x, found.largestAt.y}};
}

std::vector<double> meanOf(const cv::Mat &mat)
{
    return meanOf(fromMat(mat));
}

void meanAndDeviationOf(const cv::Mat &mat, std::vector<double> &mean, std::vector<double> &deviation)
{
    meanAndDeviationOf(fromMat(mat), mean, deviation);
}

cv::Mat matchedTemplate(const cv::Mat &image, const cv::Mat &templ)
{
    if (image.empty() || templ.empty())
        return {};
    cv::Mat out(image.rows - templ.rows + 1, image.cols - templ.cols + 1, CV_32F);
    Image target = fromMutableMat(out);
    matchTemplateInto(fromMat(image), fromMat(templ), target);
    return out;
}

cv::Mat hannWindow(cv::Size size)
{
    cv::Mat out(size, CV_32F);
    Image target = fromMutableMat(out);
    hannWindowInto(target);
    return out;
}

cv::Point2d phaseCorrelation(const cv::Mat &a, const cv::Mat &b, const cv::Mat &window)
{
    const Point<double> shift = phaseCorrelation(fromMat(a), fromMat(b), window.empty() ? Image() : fromMat(window));
    return {shift.x, shift.y};
}

namespace {

std::vector<double> doublesOf(const cv::Mat &mat)
{
    cv::Mat values;
    mat.convertTo(values, CV_64F);
    return std::vector<double>(values.begin<double>(), values.end<double>());
}

} // namespace

bool symmetricEigen(const cv::Mat &matrix, cv::Mat &values, cv::Mat &vectors)
{
    if (matrix.empty() || matrix.rows != matrix.cols || matrix.channels() != 1)
        return false;
    std::vector<double> foundValues, foundVectors;
    if (!symmetricEigen(doublesOf(matrix), matrix.rows, foundValues, foundVectors))
        return false;
    values = cv::Mat(int(foundValues.size()), 1, CV_64F, foundValues.data()).clone();
    vectors = cv::Mat(matrix.rows, matrix.rows, CV_64F, foundVectors.data()).clone();
    return true;
}

bool solveCholesky(const cv::Mat &a, const cv::Mat &b, cv::Mat &x)
{
    if (a.empty() || a.rows != a.cols || int(b.total()) != a.rows)
        return false;
    std::vector<double> solved;
    if (!solveCholesky(doublesOf(a), doublesOf(b), a.rows, solved))
        return false;
    x = cv::Mat(a.rows, 1, CV_64F, solved.data()).clone();
    return true;
}

bool singularValues(const cv::Mat &a, cv::Mat &w, cv::Mat &vt)
{
    if (a.empty() || a.channels() != 1)
        return false;
    std::vector<double> values, vectors;
    if (!singularValues(doublesOf(a), a.rows, a.cols, values, vectors))
        return false;
    w = cv::Mat(a.cols, 1, CV_64F, values.data()).clone();
    vt = cv::Mat(a.cols, a.cols, CV_64F, vectors.data()).clone();
    return true;
}

double determinant(const cv::Matx33d &m)
{
    return determinant(fromCv(m));
}

cv::Matx33d nearestRotation(const cv::Matx33d &m)
{
    return toCv(nearestRotation(fromCv(m)));
}

double length(const cv::Vec2d &v)
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1]);
}

double length(const cv::Point2d &p)
{
    return std::sqrt(p.x * p.x + p.y * p.y);
}

cv::Vec3d unit(const cv::Vec3d &v)
{
    // As OpenCV's normalize of a Vec: a zero vector stays zero.
    const double norm = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return v * (norm != 0.0 ? 1.0 / norm : 0.0);
}

bool allFinite(const cv::Vec3d &v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
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
