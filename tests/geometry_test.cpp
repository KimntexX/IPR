#include "Geometry.h"
#include "Histogram.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

bool passed = true;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        passed = false;
    }
}

bool same(const cv::Mat &a, const cv::Mat &b)
{
    if (a.size() != b.size() || a.type() != b.type())
        return false;
    for (int y = 0; y < a.rows; ++y)
        if (std::memcmp(a.ptr(y), b.ptr(y), size_t(a.cols) * a.elemSize()) != 0)
            return false;
    return true;
}

// Distinct and smooth in each channel: position can be read back from the value.
cv::Mat ramp(int width, int height)
{
    cv::Mat image(height, width, CV_32FC3);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.at<cv::Vec3f>(y, x) = {(x + .5f) / width, (y + .5f) / height, .5f};
    return image;
}

// A bright dot, as a known landmark.
cv::Mat dot(int width, int height, double u, double v, double sigma)
{
    cv::Mat image(height, width, CV_32FC3, cv::Scalar::all(0));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const double dx = (x + .5) - u * width, dy = (y + .5) - v * height;
            const float value = float(std::exp(-(dx * dx + dy * dy) / (2 * sigma * sigma)));
            image.at<cv::Vec3f>(y, x) = {value, value, value};
        }
    return image;
}

cv::Point2d centroid(const cv::Mat &image)
{
    double sum = 0, sumX = 0, sumY = 0;
    for (int y = 0; y < image.rows; ++y)
        for (int x = 0; x < image.cols; ++x) {
            const double value = std::max(0.f, image.at<cv::Vec3f>(y, x)[0]);
            sum += value;
            sumX += value * (x + .5);
            sumY += value * (y + .5);
        }
    return {sumX / sum / image.cols, sumY / sum / image.rows};
}

bool close(const cv::Point2d &a, const cv::Point2d &b, double tolerance)
{
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance;
}

Geometry::Settings general()
{
    Geometry::Settings s;
    s.rotation = 7.5f;
    s.vertical = 30;
    s.horizontal = -20;
    s.aspect = 15;
    s.scale = 110;
    s.offsetX = 8;
    s.offsetY = -6;
    return s;
}

const cv::Size Frame(3000, 2000);

// The image's visible position of a source point, after commands.
cv::Point2d seen(const Geometry::Settings &s, cv::Point2d point = {0.3, 0.2})
{
    const auto canvas = Geometry::toCanvas(s, Frame, point);
    return canvas ? *canvas : cv::Point2d(-100, -100);
}

void identity()
{
    const Geometry::Settings neutral;
    check(Geometry::isIdentity(neutral) && !Geometry::transformsPixels(neutral) && !Geometry::hasCrop(neutral),
          "the default settings change nothing");
    const cv::Mat image = ramp(32, 24);
    check(Geometry::warpRgb(image, neutral, Geometry::Quality::Export).data == image.data,
          "neutral settings share the buffer");
    check(Geometry::crop(image, neutral).data == image.data, "no crop shares the buffer");
    check(close(seen(neutral), {0.3, 0.2}, 1e-12), "neutral settings map a point to itself");
    cv::Mat empty;
    check(Geometry::warpRgb(empty, general(), Geometry::Quality::Export).empty() &&
          Geometry::warpCoverage(empty, general(), Geometry::Quality::Export).empty() &&
          Geometry::warpRgba(empty, general(), Geometry::Quality::Export).empty(),
          "an empty image stays empty");
}

void exactOrientation()
{
    const cv::Mat image = ramp(16, 10);
    const cv::Mat reference = image.clone();
    Geometry::Settings s;
    s.quarterTurns = 1;
    cv::Mat turned = Geometry::warpRgb(image, s, Geometry::Quality::Export);
    check(turned.cols == 10 && turned.rows == 16, "a quarter turn swaps the sides");
    check(turned.at<cv::Vec3f>(0, 9) == image.at<cv::Vec3f>(0, 0), "a clockwise turn sends the top left to the top right");
    check(close(*Geometry::toCanvas(s, image.size(), {0, 0}), {1, 0}, 1e-12), "the matrix agrees with the exact turn");

    Geometry::Settings half;
    half.quarterTurns = 2;
    cv::Mat back = Geometry::warpRgb(Geometry::warpRgb(image, half, Geometry::Quality::Export), half, Geometry::Quality::Export);
    check(same(back, image), "two half turns give the exact image");

    Geometry::Settings mirrored;
    mirrored.flipHorizontal = true;
    cv::Mat twice = Geometry::warpRgb(Geometry::warpRgb(image, mirrored, Geometry::Quality::Preview), mirrored, Geometry::Quality::Preview);
    check(same(twice, image), "two mirrors give the exact image");
    check(close(*Geometry::toCanvas(mirrored, image.size(), {0.25, 0.5}), {0.75, 0.5}, 1e-12), "the matrix agrees with the exact mirror");

    for (int turns = 0; turns < 4; ++turns)
        for (int flips = 0; flips < 4; ++flips) {
            Geometry::Settings o;
            o.quarterTurns = turns;
            o.flipHorizontal = flips & 1;
            o.flipVertical = flips & 2;
            const cv::Mat out = Geometry::warpRgb(image, o, Geometry::Quality::Export);
            const auto corner = Geometry::toCanvas(o, image.size(), {(.5 + 3) / 16, (.5 + 2) / 10});
            const int x = int(corner->x * out.cols), y = int(corner->y * out.rows);
            check(out.at<cv::Vec3f>(y, x) == image.at<cv::Vec3f>(2, 3), "matrix and exact copy agree on every orientation");
            check(same(image, reference), "an orientation never writes in its source");
        }
}

void inverses()
{
    const std::vector<cv::Point2d> points{{0.1, 0.1}, {0.9, 0.2}, {0.5, 0.5}, {0.2, 0.85}, {0.7, 0.6}};
    for (const cv::Size &size : {cv::Size(3000, 2000), cv::Size(2000, 3000), cv::Size(1000, 1000)}) {
        Geometry::Settings s = general();
        s.quarterTurns = 1;
        s.flipVertical = true;
        for (const auto &point : points) {
            const auto canvas = Geometry::toCanvas(s, size, point);
            check(bool(canvas), "a source point has a canvas position");
            const auto back = Geometry::toSource(s, size, *canvas);
            check(back && close(*back, point, 1e-10), "the inverse returns the source point");
        }
    }
    // At every strength the image's corners keep a finite position.
    Geometry::Settings strongest;
    for (int vertical : {-100, 100})
        for (int horizontal : {-100, 100}) {
            strongest.vertical = vertical;
            strongest.horizontal = horizontal;
            for (const cv::Point2d &corner : {cv::Point2d(0, 0), cv::Point2d(1, 0), cv::Point2d(1, 1), cv::Point2d(0, 1)})
                check(bool(Geometry::toCanvas(strongest, Frame, corner)), "the strongest perspective stays before the horizon");
        }
    Geometry::Settings view;
    view.cropX = 0.2;
    view.cropY = 0.1;
    view.cropWidth = 0.5;
    view.cropHeight = 0.25;
    check(close(Geometry::viewToCanvas(view, {0.5, 1}), {0.45, 0.35}, 1e-12), "the displayed crop maps to the canvas");
}

void landmarks()
{
    // The resampling puts a known dot where the matrix says, at both qualities and sizes.
    const Geometry::Settings s = general();
    const cv::Point2d source{0.42, 0.55};
    for (const cv::Size &size : {cv::Size(1536, 1024), cv::Size(3072, 2048)})
        for (const auto quality : {Geometry::Quality::Preview, Geometry::Quality::Export}) {
            const cv::Mat image = dot(size.width, size.height, source.x, source.y, size.width / 128.0);
            const cv::Mat out = Geometry::warpRgb(image, s, quality);
            const auto expected = Geometry::toCanvas(s, size, source);
            // Half a pixel of the small image: perspective shifts a centroid a little.
            check(close(centroid(out), *expected, 0.5 / 1024), "a landmark lands where the matrix says");
        }
    // A grating the output cannot hold fades instead of folding back as a false pattern.
    for (const auto quality : {Geometry::Quality::Preview, Geometry::Quality::Export})
        for (const int scale : {50, 75}) {
            cv::Mat grating(512, 512, CV_32FC3);
            for (int y = 0; y < grating.rows; ++y)
                for (int x = 0; x < grating.cols; ++x)
                    grating.at<cv::Vec3f>(y, x) = cv::Vec3f::all(float(.5 + .2 * std::sin(2 * 3.14159265358979 * (x + .5) / 2.5)));
            Geometry::Settings reduced;
            reduced.scale = scale;
            cv::Mat channel;
            cv::extractChannel(Geometry::warpRgb(grating, reduced, quality)(cv::Rect(200, 200, 112, 112)), channel, 0);
            cv::Scalar mean, deviation;
            cv::meanStdDev(channel, mean, deviation);
            check(deviation[0] * std::sqrt(2.0) / .2 < 0.35, "a reduced fine grating does not fold back");
        }
    // Preview and export of the same settings agree in position, despite their filters.
    const cv::Mat image = dot(2048, 1365, source.x, source.y, 16);
    check(close(centroid(Geometry::warpRgb(image, s, Geometry::Quality::Preview)),
                centroid(Geometry::warpRgb(image, s, Geometry::Quality::Export)), 0.1 / 1365),
          "preview and export agree in position");
}

void directions()
{
    Geometry::Settings s;
    s.rotation = 90.f;    // beyond the range: the matrix alone reads the sign
    check(close(seen(s, {0.5, 0.5}), {0.5, 0.5}, 1e-12), "rotation turns around the centre");
    s.rotation = 10.f;
    const auto top = Geometry::toCanvas(s, Frame, {0.5, 0});
    check(top->x > 0.5, "a positive rotation moves the top to the right");

    Geometry::Settings v;
    v.vertical = 50;
    const double topWidth = Geometry::toCanvas(v, Frame, {1, 0})->x - Geometry::toCanvas(v, Frame, {0, 0})->x;
    const double bottomWidth = Geometry::toCanvas(v, Frame, {1, 1})->x - Geometry::toCanvas(v, Frame, {0, 1})->x;
    check(topWidth > bottomWidth, "a positive vertical widens the top");

    Geometry::Settings h;
    h.horizontal = 50;
    const double leftHeight = Geometry::toCanvas(h, Frame, {0, 1})->y - Geometry::toCanvas(h, Frame, {0, 0})->y;
    const double rightHeight = Geometry::toCanvas(h, Frame, {1, 1})->y - Geometry::toCanvas(h, Frame, {1, 0})->y;
    check(rightHeight > leftHeight, "a positive horizontal widens the right");

    Geometry::Settings a;
    a.aspect = 50;
    const auto right = Geometry::toCanvas(a, Frame, {1, 0.5});
    const auto bottom = Geometry::toCanvas(a, Frame, {0.5, 1});
    check(right->x > 1 && bottom->y < 1, "a positive aspect widens and keeps the area");

    Geometry::Settings z;
    z.scale = 150;
    check(close(*Geometry::toCanvas(z, Frame, {0.75, 0.5}), {0.875, 0.5}, 1e-12), "the scale grows around the centre");

    Geometry::Settings o;
    o.offsetX = 50;
    o.offsetY = -50;
    check(close(*Geometry::toCanvas(o, Frame, {0.5, 0.5}), {0.75, 0.25}, 1e-12), "the offsets are percents of half the canvas");
}

void borders()
{
    cv::Mat image(100, 150, CV_32FC3, cv::Scalar(.4, .5, .6));
    cv::Mat alpha(100, 150, CV_32FC1, cv::Scalar(1));
    Geometry::Settings s;
    s.rotation = 20.f;
    for (const auto quality : {Geometry::Quality::Preview, Geometry::Quality::Export}) {
        const cv::Mat rgb = Geometry::warpRgb(image, s, quality);
        const cv::Mat coverage = Geometry::warpCoverage(alpha, s, quality);
        check(rgb.size() == image.size() && coverage.size() == image.size(), "the canvas keeps the source's size");
        // Beyond the source the colour repeats, so a filter sees no false edge.
        check(cv::norm(rgb, cv::Mat(rgb.size(), CV_32FC3, cv::Scalar(.4, .5, .6)), cv::NORM_INF) < 1e-5,
              "a flat image stays flat up to the border and beyond");
        double low = 0, high = 0;
        cv::minMaxIdx(coverage, &low, &high);
        check(low == 0 && high == 1, "the coverage is in [0, 1]");
        check(coverage.at<float>(0, 0) == 0, "a corner beyond the source is uncovered");
        check(coverage.at<float>(50, 75) == 1, "the centre is covered");
    }
    // A sharp edge in a dark area: no filter lobe below the source's minimum.
    cv::Mat edge(64, 64, CV_32FC3, cv::Scalar::all(0));
    edge(cv::Rect(32, 0, 32, 64)).setTo(cv::Scalar::all(4));
    double low = 0;
    cv::minMaxIdx(Geometry::warpRgb(edge, s, Geometry::Quality::Export).reshape(1), &low);
    check(low >= 0, "the filter's lobes stay above the source's minimum");

    cv::Mat rgba(50, 75, CV_32FC4, cv::Scalar(.4, .5, .6, 1));
    const cv::Mat warped = Geometry::warpRgba(rgba, s, Geometry::Quality::Export);
    check(warped.type() == CV_32FC4 && warped.at<cv::Vec4f>(0, 0) == cv::Vec4f(0, 0, 0, 0),
          "a pixel of no coverage is transparent black");
    Geometry::Settings turned;
    turned.quarterTurns = 1;
    check(Geometry::warpRgba(rgba, turned, Geometry::Quality::Export).size() == cv::Size(50, 75),
          "turns of an image with alpha keep every channel");
}

void bounds()
{
    Geometry::Settings s;
    s.quarterTurns = 9;
    s.rotation = 100.f;
    s.vertical = 500;
    s.horizontal = -500;
    s.aspect = 500;
    s.scale = 1;
    s.offsetX = 500;
    s.offsetY = -500;
    s.cropX = 0.9;
    s.cropWidth = 0.5;
    s.cropY = -2;
    s.cropHeight = 0;
    check(Geometry::bound(s), "out-of-range values are clamped");
    check(s.quarterTurns == 3 && s.rotation == 45.f && s.vertical == 100 && s.horizontal == -100 && s.aspect == 100 &&
          s.scale == 50 && s.offsetX == 100 && s.offsetY == -100, "every range holds");
    check(s.cropX + s.cropWidth <= 1 && s.cropY == 0 && s.cropHeight > 0, "the crop stays on the canvas");
    for (const double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        Geometry::Settings bad;
        bad.cropWidth = invalid;
        check(!Geometry::bound(bad), "a non-finite crop is refused");
    }
    Geometry::Settings angle;
    angle.rotation = std::numeric_limits<float>::quiet_NaN();
    check(!Geometry::bound(angle), "a non-finite rotation is refused");
}

void cropping()
{
    Geometry::Settings s;
    s.cropX = 0.25;
    s.cropY = 0.5;
    s.cropWidth = 0.5;
    s.cropHeight = 0.25;
    check(Geometry::hasCrop(s) && !Geometry::transformsPixels(s) && !Geometry::isIdentity(s), "a crop alone is a change");
    check(Geometry::cropRect(s, {400, 200}) == cv::Rect(100, 100, 200, 50), "the rectangle follows the canvas");
    check(Geometry::cropRect(s, {1600, 800}) == cv::Rect(400, 400, 800, 200), "and scales with the resolution");
    cv::Mat image(200, 400, CV_32FC4, cv::Scalar::all(0));
    check(Geometry::crop(image, s).size() == cv::Size(200, 50), "the view has the rectangle's size");
    Geometry::Settings tiny;
    tiny.cropX = 0.999;
    tiny.cropWidth = 0.001;
    tiny.cropHeight = 0.001;
    const cv::Rect smallest = Geometry::cropRect(tiny, {10, 10});
    check(smallest.width >= 1 && smallest.height >= 1 && smallest.x + smallest.width <= 10, "a crop is at least one pixel");

    Geometry::Settings r;
    Geometry::setCropRatio(r, Frame, 1.0);
    const cv::Rect square = Geometry::cropRect(r, Frame);
    check(square.width == square.height && square.height == 2000 && square.x == 500, "a square crop fits around the centre");
    Geometry::setCrop(r, Frame, 0, 0, 0.5, 0.2);
    const cv::Rect kept = Geometry::cropRect(r, Frame);
    check(std::abs(kept.width - kept.height) <= 1, "a hand-drawn crop keeps the ratio");
    Geometry::setCrop(r, Frame, std::numeric_limits<double>::quiet_NaN(), 0, 1, 1);
    check(Geometry::cropRect(r, Frame) == kept, "a non-finite crop changes nothing");
    Geometry::setCropRatio(r, Frame, 0);
    Geometry::setCrop(r, Frame, 0.1, 0.1, 0.5, 0.2);
    check(std::abs(r.cropWidth - 0.5) < 1e-12 && std::abs(r.cropHeight - 0.2) < 1e-12, "a free crop takes any shape");
}

void constraint()
{
    Geometry::Settings s;
    s.rotation = 12.f;
    check(!Geometry::cropInsideImage(s, Frame), "a rotated image leaves empty corners in the whole canvas");
    Geometry::Settings free = s;
    Geometry::applyConstraint(free, Frame);
    check(!Geometry::hasCrop(free), "without the option the empty zones stay");
    s.constrain = true;
    Geometry::applyConstraint(s, Frame);
    check(Geometry::cropInsideImage(s, Frame) && Geometry::hasCrop(s), "the constraint brings the crop on the image");
    check(std::abs(s.cropWidth - s.cropHeight) < 1e-9, "and keeps its proportions");
    check(s.cropWidth > 0.5, "and stays as large as it can");
    // The result has no uncovered pixel, measured on pixels.
    const cv::Mat alpha(1000, 1500, CV_32FC1, cv::Scalar(1));
    const cv::Mat coverage = Geometry::warpCoverage(alpha, s, Geometry::Quality::Export);
    double low = 0;
    cv::minMaxIdx(Geometry::crop(coverage, s), &low);
    check(low > 0.4, "the constrained crop holds no empty pixel");

    Geometry::Settings moved;
    moved.offsetX = 100;
    moved.constrain = true;
    Geometry::applyConstraint(moved, Frame);
    check(Geometry::cropInsideImage(moved, Frame), "a strong offset still ends on the image");
    Geometry::Settings clean;
    clean.constrain = true;
    Geometry::applyConstraint(clean, Frame);
    check(!Geometry::hasCrop(clean), "a whole image needs no constraint");
}

void commands()
{
    // A command moves the picture on the screen as a rigid turn or mirror would,
    // whatever is already set; the source point still shows where it should.
    const std::vector<cv::Point2d> points{{0.3, 0.2}, {0.8, 0.7}};
    for (int mirrors = 0; mirrors < 4; ++mirrors) {
        Geometry::Settings s = general();
        s.flipHorizontal = mirrors & 1;
        s.flipVertical = mirrors & 2;
        s.aspect = 0;  // A rigid turn of an aspect is its opposite only in the isotropic frame.
        for (const auto &point : points) {
            const auto before = Geometry::toCanvas(s, Frame, point);
            Geometry::Settings turned = s;
            Geometry::turn(turned, Frame);
            const auto after = Geometry::toCanvas(turned, Frame, point);
            // The clockwise quarter turn of the frame: (u, v) goes to (1 - v, u).
            check(after && close(*after, {1 - before->y, before->x}, 1e-9), "a turn turns the picture");

            Geometry::Settings mirrored = s;
            Geometry::mirror(mirrored, true, Frame);
            const auto flipped = Geometry::toCanvas(mirrored, Frame, point);
            check(flipped && close(*flipped, {1 - before->x, before->y}, 1e-9), "a mirror mirrors the picture");

            Geometry::Settings upside = s;
            Geometry::mirror(upside, false, Frame);
            const auto vertical = Geometry::toCanvas(upside, Frame, point);
            check(vertical && close(*vertical, {before->x, 1 - before->y}, 1e-9), "a vertical mirror mirrors the picture");
        }
    }
    Geometry::Settings c;
    c.cropX = 0.1;
    c.cropY = 0.2;
    c.cropWidth = 0.3;
    c.cropHeight = 0.4;
    c.cropRatio = 1.5;
    Geometry::turn(c, Frame);
    check(std::abs(c.cropX - 0.4) < 1e-12 && std::abs(c.cropY - 0.1) < 1e-12 &&
          std::abs(c.cropWidth - 0.4) < 1e-12 && std::abs(c.cropHeight - 0.3) < 1e-12,
          "the crop turns with the picture");
    check(std::abs(c.cropRatio - 1 / 1.5) < 1e-12, "and so does its ratio");
    for (int count = 0; count < 3; ++count)
        Geometry::turn(c, Frame);
    check(std::abs(c.cropX - 0.1) < 1e-12 && std::abs(c.cropWidth - 0.3) < 1e-12 && std::abs(c.cropRatio - 1.5) < 1e-12,
          "four turns come back");
    Geometry::mirror(c, true, Frame);
    check(std::abs(c.cropX - 0.6) < 1e-12, "a mirror mirrors the crop");
}

} // namespace

void histogramOfEmptyZones()
{
    const auto count = [](const QVariantList &histogram, int channel, int bin) {
        return histogram.at(channel).toList().at(bin).toDouble();
    };
    const auto total = [](const QVariantList &histogram, int channel) {
        double sum = 0;
        for (const QVariant &value : histogram.at(channel).toList())
            sum += value.toDouble();
        return sum;
    };

    // White colours under a zero alpha, and enough rows to split between threads.
    QImage image(40, 600, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const bool empty = y % 2 == 0;
            const uchar code = empty ? 255 : 128;
            row[4 * x] = row[4 * x + 1] = row[4 * x + 2] = code;
            row[4 * x + 3] = empty ? 0 : 255;
        }
    }
    const QVariantList histogram = Histogram::compute(image);
    check(histogram.size() == 4, "the histogram has its four channels");
    if (histogram.size() != 4)
        return;
    const double opaque = 40 * 300;
    for (int channel = 0; channel < 4; ++channel) {
        check(total(histogram, channel) == opaque, "every channel counts the opaque pixels only");
        check(count(histogram, channel, 255) == 0, "an empty zone leaves no tone behind its zero alpha");
        check(count(histogram, channel, 128) == opaque, "the opaque grey falls in its own bin");
    }

    const QVariantList none = Histogram::compute(QImage());
    check(none.size() == 4 && total(none, 0) == 0, "a null image gives empty channels");
}

int main()
{
    identity();
    exactOrientation();
    inverses();
    landmarks();
    directions();
    borders();
    bounds();
    cropping();
    constraint();
    commands();
    histogramOfEmptyZones();
    if (passed)
        std::cout << "geometry: ok\n";
    return passed ? 0 : 1;
}
