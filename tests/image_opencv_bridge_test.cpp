#include "OpenCvBridge.h"

#include <opencv2/imgproc.hpp>

#include <iostream>
#include <stdexcept>

using namespace ipr;

namespace {

void check(bool condition, const char *what)
{
    if (!condition) throw std::runtime_error(what);
}

template <typename Exception, typename Call>
void throws(const Call &call, const char *what)
{
    try {
        call();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error(what);
}

cv::Mat scene(int width, int height)
{
    cv::Mat mat(height, width, CV_32FC3);
    cv::RNG random(5);
    random.fill(mat, cv::RNG::UNIFORM, 0.0f, 1.0f);
    return mat;
}

bool same(const cv::Mat &a, const cv::Mat &b)
{
    return a.size() == b.size() && a.type() == b.type() && cv::norm(a, b, cv::NORM_INF) == 0;
}

void noPixelIsCopiedEitherWay()
{
    const cv::Mat mat = scene(32, 24);
    const Image image = fromMat(mat);
    check(image.row<float>(0) == mat.ptr<float>(0), "from a Mat, same pixels");
    check(image.width() == 32 && image.height() == 24 && image.channels() == 3 && image.depth() == Depth::F32, "layout");
    check(image.stepBytes() == std::ptrdiff_t(mat.step[0]), "step");

    const cv::Mat header = asMat(image);
    check(header.data == mat.data && header.type() == CV_32FC3 && header.step[0] == mat.step[0], "and back");
    check(same(header, mat), "same values");

    Image own = Image::create({8, 6}, Depth::U16, 1);
    const cv::Mat onOwn = asMutableMat(own);
    check(onOwn.ptr<std::uint16_t>(0) == own.row<std::uint16_t>(0) && onOwn.type() == CV_16UC1, "from our pixels");
}

void everyDepthAndChannelCrosses()
{
    for (const int depth : {CV_8U, CV_16U, CV_32F, CV_64F})
        for (int channels = 1; channels <= 4; ++channels) {
            const cv::Mat mat(3, 5, CV_MAKETYPE(depth, channels), cv::Scalar::all(1));
            const cv::Mat back = asMat(fromMat(mat));
            check(back.type() == mat.type() && back.data == mat.data, "the type crosses");
        }
    throws<std::invalid_argument>([] { fromMat(cv::Mat(3, 3, CV_16SC1)); }, "a depth we do not have");
    throws<std::invalid_argument>([] { fromMat(cv::Mat(3, 3, CV_8UC(5))); }, "too many channels");
    const int sizes[] = {2, 2, 2};
    throws<std::invalid_argument>([&] { fromMat(cv::Mat(3, sizes, CV_8UC1)); }, "three dimensions");
}

void emptyStaysEmpty()
{
    check(fromMat(cv::Mat()).empty(), "from an empty Mat");
    cv::Mat none;
    check(fromMutableMat(none).empty(), "the same to write");
    check(asMat(Image()).empty(), "to an empty Mat");
    Image empty;
    check(asMutableMat(empty).empty(), "the same to write");
}

void aMatStaysAliveWithItsImage()
{
    Image image;
    float expected = 0;
    {
        cv::Mat mat = scene(16, 16);
        expected = mat.at<cv::Vec3f>(3, 2)[1];
        image = fromMat(mat);
    }
    check(image.row<Vec3f>(3)[2][1] == expected, "the image keeps the pixels");
    const Image part = image.view({2, 3, 4, 4});
    image = Image();
    check(part.row<Vec3f>(0)[0][1] == expected, "and so does a view");
}

void whatComesFromAMatIsReadOnly()
{
    const cv::Mat mat = scene(8, 8);
    Image image = fromMat(mat);
    check(image.frozen(), "frozen");
    throws<std::logic_error>([&] { image.mutableRow<float>(0); }, "no writing through it");
    throws<std::logic_error>([&] { asMutableMat(image); }, "nor by the way back");
    Image own = image.clone();
    own.mutableRow<Vec3f>(0)[0] = Vec3f{{9.0f, 9.0f, 9.0f}};
    check(mat.at<cv::Vec3f>(0, 0)[0] != 9.0f, "a clone leaves the Mat");
}

void aMutableMatIsWrittenInPlace()
{
    cv::Mat mat = scene(8, 8);
    Image image = fromMutableMat(mat);
    check(!image.frozen(), "writable");
    image.mutableRow<Vec3f>(2)[3] = Vec3f{{4.0f, 5.0f, 6.0f}};
    check(mat.at<cv::Vec3f>(2, 3)[1] == 5.0f, "the Mat sees it");
    cv::Mat back = asMutableMat(image);
    back.at<cv::Vec3f>(1, 1)[0] = 7.0f;
    check(mat.at<cv::Vec3f>(1, 1)[0] == 7.0f, "and the way back too");
    const Image shared = image.share();
    throws<std::logic_error>([&] { asMutableMat(image); }, "shared, it is frozen");
}

void aPartOfAMatKeepsItsLayout()
{
    const cv::Mat mat = scene(20, 20);
    const cv::Mat part = mat(cv::Rect(3, 4, 6, 5));
    const Image image = fromMat(part);
    check(!image.continuous() && image.stepBytes() == std::ptrdiff_t(mat.step[0]), "the rows of the whole");
    check(same(asMat(image), part), "same values");
    check(same(asMat(fromMat(mat).view({3, 4, 6, 5})), part), "a view is the same as a Mat's part");
}

void anAlgorithmGivesTheSameThroughTheBridge()
{
    const cv::Mat mat = scene(200, 150);
    cv::Mat direct, bridged;
    cv::GaussianBlur(mat, direct, cv::Size(0, 0), 2.5);
    const Image image = fromMat(mat);
    cv::GaussianBlur(asMat(image), bridged, cv::Size(0, 0), 2.5);
    check(same(direct, bridged), "bit for bit");

    // Into our pixels: OpenCV must not reallocate behind the header.
    Image target = Image::create({200, 150}, Depth::F32, 3);
    cv::Mat header = asMutableMat(target);
    const uchar *before = header.data;
    cv::GaussianBlur(mat, header, cv::Size(0, 0), 2.5);
    check(header.data == before, "written in place");
    check(same(asMat(target), direct), "same pixels");
}

void smallValuesCross()
{
    check(fromCv(toCv(Size{3, 4})) == Size{3, 4}, "size");
    check(fromCv(toCv(Point2f{1.5f, 2.5f})) == Point2f{1.5f, 2.5f}, "point");
    check(fromCv(toCv(Recti{1, 2, 3, 4})) == Recti{1, 2, 3, 4}, "rectangle");
    check(toCv(Recti{1, 2, 3, 4}) == cv::Rect(1, 2, 3, 4), "same fields");
    Matrix33d matrix = Matrix33d::identity();
    matrix(0, 2) = 7.0;
    matrix(2, 1) = -3.0;
    const cv::Matx33d converted = toCv(matrix);
    check(converted(0, 2) == 7.0 && converted(2, 1) == -3.0 && converted(1, 1) == 1.0, "by rows");
    check(fromCv(converted) == matrix, "and back");
}

} // namespace

int main()
{
    try {
        noPixelIsCopiedEitherWay();
        everyDepthAndChannelCrosses();
        emptyStaysEmpty();
        aMatStaysAliveWithItsImage();
        whatComesFromAMatIsReadOnly();
        aMutableMatIsWrittenInPlace();
        aPartOfAMatKeepsItsLayout();
        anAlgorithmGivesTheSameThroughTheBridge();
        smallValuesCross();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "image bridge: all passed\n";
    return 0;
}
