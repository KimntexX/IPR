#include "StackAlignment.h"
#include "image_testing.h"
#include "StackJob.h"
#include "StackMerge.h"
#include "Filters.h"
#include "Warp.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
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

// Texture at several scales, in the range of a linear camera image.
ipr::Image scene(const ipr::Size &size, unsigned seed = 7)
{
    ipr::Image image = ipr::Image::zeros(size, ipr::Depth::F32, 3);
    for (const double sigma : {1.5, 5.0, 18.0}) {
        const ipr::Image layer = ipr::gaussianBlurred(imagetest::normalNoise(size, 3, 0.0, 1.0, seed++), sigma);
        std::vector<double> mean, deviation;
        ipr::meanAndDeviationOf(layer, mean, deviation);
        const float weight = float(0.05 / std::max(1e-6, deviation[0]));
        image = ipr::mapped(image, layer, [weight](float a, float b) { return a + b * weight; });
    }
    return ipr::mapped(image, [](float v) { return v + 0.3f; });
}

ipr::Image plus(const ipr::Image &a, const ipr::Image &b)
{
    return ipr::mapped(a, b, [](float x, float y) { return x + y; });
}

ipr::Image minus(const ipr::Image &a, const ipr::Image &b)
{
    return ipr::mapped(a, b, [](float x, float y) { return x - y; });
}

ipr::Image times(const ipr::Image &image, float factor)
{
    return ipr::mapped(image, [factor](float v) { return v * factor; });
}

ipr::Image noisy(const ipr::Image &image, double sigma, unsigned seed)
{
    return plus(image, imagetest::normalNoise(image.size(), image.channels(), 0.0, sigma, seed));
}

// from's pixels into an area of to.
void paste(const ipr::Image &from, ipr::Image &to, const ipr::Recti &area)
{
    ipr::Image place = to.mutableView(area);
    ipr::copyInto(from, place);
}

// One value into an area of the image.
void paint(ipr::Image &image, const ipr::Recti &area, double value)
{
    ipr::Image place = image.mutableView(area);
    ipr::fill(place, value);
}

// The root mean square of the difference between two images of one layout.
double rms(const ipr::Image &a, const ipr::Image &b)
{
    return imagetest::distance(a, b) / std::sqrt(double(a.size().area()) * a.channels());
}

ipr::Matrix33d translation(double dx, double dy)
{
    return {1, 0, dx, 0, 1, dy, 0, 0, 1};
}

// The frame a camera moved by referenceToFrame would have seen of the scene.
ipr::Image seenFrom(const ipr::Image &base, const ipr::Matrix33d &referenceToFrame)
{
    return imagetest::moved(base, referenceToFrame, base.size());
}

double cornerError(const ipr::Matrix33d &found, const ipr::Matrix33d &truth, const ipr::Size &size)
{
    double worst = 0.0;
    const double margin = 0.1;
    for (const double u : {margin, 1.0 - margin})
        for (const double v : {margin, 1.0 - margin}) {
            const ipr::Vec3d p(u * size.width, v * size.height, 1.0);
            const ipr::Vec3d a = found * p, b = truth * p;
            worst = std::max(worst, std::hypot(a[0] / a[2] - b[0] / b[2], a[1] / a[2] - b[1] / b[2]));
        }
    return worst;
}

ipr::Matrix33d rotation(double degrees, const ipr::Size &size, double dx, double dy)
{
    const double r = degrees * ipr::pi / 180.0, c = std::cos(r), s = std::sin(r);
    const double cx = size.width / 2.0, cy = size.height / 2.0;
    return {c, -s, cx - c * cx + s * cy + dx, s, c, cy - s * cx - c * cy + dy, 0, 0, 1};
}

bool sameBytes(const ipr::Image &a, const ipr::Image &b)
{
    if (a.size() != b.size() || a.depth() != b.depth() || a.channels() != b.channels())
        return false;
    for (int y = 0; y < a.height(); ++y)
        if (std::memcmp(a.row<std::uint8_t>(y), b.row<std::uint8_t>(y), size_t(a.width()) * a.pixelBytes()) != 0)
            return false;
    return true;
}

void rectangles()
{
    using StackJob::largestRectangle;
    check(largestRectangle({}).empty(), "no rows, no rectangle");
    check(largestRectangle({ipr::Vec2i(5, 5), ipr::Vec2i(9, 2)}).empty(), "empty spans, no rectangle");
    check(largestRectangle({ipr::Vec2i(2, 8)}) == ipr::Recti(2, 0, 6, 1), "a single row");
    // A diamond: the best rectangle holds its middle rows.
    std::vector<ipr::Vec2i> diamond;
    for (int y = 0; y < 21; ++y) {
        const int half = 10 - std::abs(y - 10);
        diamond.emplace_back(10 - half, 10 + half + 1);
    }
    const ipr::Recti best = largestRectangle(diamond);
    check(best.area() >= 110 && best.area() <= 121, "a diamond keeps about half its area");
    for (int y = best.y; y < best.y + best.height; ++y)
        check(best.x >= diamond[size_t(y)][0] && best.x + best.width <= diamond[size_t(y)][1], "the rectangle stays inside");
    check(largestRectangle({ipr::Vec2i(0, 0), ipr::Vec2i(1, 4), ipr::Vec2i(1, 4), ipr::Vec2i(0, 0)}) == ipr::Recti(1, 1, 3, 2),
          "empty rows at both ends are left out");
}

// Frames for a merge: an initializer list would copy them.
template <typename... Images>
std::vector<ipr::Image> framesOf(Images &&...images)
{
    std::vector<ipr::Image> frames;
    (frames.push_back(std::move(images)), ...);
    return frames;
}

void merging()
{
    ipr::Image merged;
    check(!StackMerge::merge({}, 3.0, merged), "no frames, no merge");
    check(!StackMerge::merge(framesOf(ipr::Image()), 3.0, merged), "an empty frame is refused");
    check(!StackMerge::merge(framesOf(ipr::filled({2, 2}, ipr::Depth::U8, 3, 0)), 3.0, merged), "a wrong type is refused");
    check(!StackMerge::merge(framesOf(ipr::filled({2, 2}, ipr::Depth::F32, 3, 0), ipr::filled({3, 2}, ipr::Depth::F32, 3, 0)), 3.0, merged),
          "different sizes are refused");

    // Below the minimum: a plain average, even of an outlier.
    std::vector<ipr::Image> few;
    for (const float value : {0.1f, 0.2f, 0.9f})
        few.push_back(ipr::filled({2, 2}, ipr::Depth::F32, 3, value));
    check(StackMerge::merge(few, 3.0, merged) && std::abs(imagetest::at<ipr::Vec3f>(merged, 0, 0)[1] - 0.4f) < 1e-6f, "three frames average");

    // Enough frames: what flashed in one of them goes, the rest averages.
    std::vector<ipr::Image> many;
    for (int i = 0; i < 9; ++i)
        many.push_back(ipr::filled({4, 4}, ipr::Depth::F32, 3, 0.2 + 0.001 * i));
    imagetest::at<ipr::Vec3f>(many[3], 1, 1) = ipr::Vec3f(0.9f, 0.1f, 0.9f);
    StackMerge::Statistics statistics;
    check(StackMerge::merge(many, 3.0, merged, &statistics), "nine frames merge");
    const float others = float((0.2 * 9 + 0.001 * 36 - (0.2 + 0.003)) / 8.0);
    const ipr::Vec3f mergedPixel = imagetest::at<ipr::Vec3f>(merged, 1, 1);
    check(std::abs(mergedPixel[0] - others) < 1e-5f && std::abs(mergedPixel[1] - others) < 1e-5f, "the outlier is out, colour included");
    check(statistics.samples == 16 * 9 && statistics.rejected >= 1, "the statistics count it");

    // A contrasted edge that wavers by one pixel between frames stays a clean line: the
    // decisions read a neighbourhood, and a sample fades instead of flipping.
    std::vector<ipr::Image> wavering;
    for (int i = 0; i < 12; ++i) {
        ipr::Image frame = ipr::filled({64, 64}, ipr::Depth::F32, 3, 0.05);
        const int edge = 32 + (i % 2);
        paint(frame, ipr::Recti(edge, 0, 64 - edge, 64), 0.6);
        wavering.push_back(noisy(frame, 0.01, unsigned(1100 + i)));
    }
    check(StackMerge::merge(wavering, 3.0, merged), "a wavering edge merges");
    double teeth = 0.0;
    for (int y = 1; y < 63; ++y) {
        const float here = imagetest::at<ipr::Vec3f>(merged, y, 32)[1], next = imagetest::at<ipr::Vec3f>(merged, y + 1, 32)[1];
        teeth = std::max(teeth, double(std::abs(here - next)));
    }
    if (teeth > 0.1)
        std::cerr << "teeth " << teeth << '\n';
    check(teeth <= 0.1, "the edge has no teeth along it");

    // Signed values stay signed; rejection off keeps the outlier.
    std::vector<ipr::Image> negative;
    for (int i = 0; i < 6; ++i)
        negative.push_back(ipr::filled({2, 2}, ipr::Depth::F32, 3, -0.01));
    check(StackMerge::merge(negative, 3.0, merged) && imagetest::at<ipr::Vec3f>(merged, 0, 0)[0] < -0.009f, "values below the black stay");
    check(StackMerge::merge(many, 0.0, merged, &statistics) && statistics.rejected == 0 && imagetest::at<ipr::Vec3f>(merged, 1, 1)[0] > others, "sigma 0 keeps all");
}

void alignment()
{
    const ipr::Size size(900, 600);
    const ipr::Image base = scene(size);
    const StackAlignment::Pyramid reference = StackAlignment::buildPyramid(noisy(base, 0.004, 1));
    check(reference.size() >= 2 && reference[0].size() == size, "the pyramid starts at the whole size");
    check(StackAlignment::buildPyramid(ipr::Image()).empty(), "no image, no pyramid");
    check(StackAlignment::buildPyramid(ipr::Image::create({4, 4}, ipr::Depth::U8, 3)).empty(), "a wrong type gives no pyramid");

    const std::vector<std::pair<const char *, ipr::Matrix33d>> cases{
        {"identity", ipr::Matrix33d::identity()},
        {"sub-pixel shift", {1, 0, 0.37, 0, 1, -0.62, 0, 0, 1}},
        {"a large shift", {1, 0, 37.3, 0, 1, -21.7, 0, 0, 1}},
        {"a rotation", rotation(1.5, size, 6.2, -3.4)},
        {"a perspective", {1.004, 0.006, 11.0, -0.005, 0.998, -7.5, 1.2e-5, -0.9e-5, 1}},
    };
    for (const auto &[name, truth] : cases) {
        const ipr::Image frame = noisy(seenFrom(base, truth), 0.004, 2);
        const StackAlignment::Result found = StackAlignment::estimate(reference, StackAlignment::buildPyramid(frame));
        if (!found.valid)
            std::cerr << name << ": " << found.error.toStdString() << '\n';
        check(found.valid, "a textured frame registers");
        const double error = cornerError(found.referenceToFrame, truth, size);
        if (error > 0.2)
            std::cerr << name << ": error " << error << " px\n";
        check(error <= 0.2, "the transform is found within a fifth of a pixel");
        check(found.model == StackAlignment::Model::Homography, "matches across the image give a homography");
        check(found.inliers >= 12 && found.support > 0.35, "with its support reported");
    }

    // A moving object over a tenth of the frame does not pull the transform.
    {
        const ipr::Matrix33d truth(1, 0, 14.0, 0, 1, 9.0, 0, 0, 1);
        ipr::Image frame = seenFrom(base, truth);
        paste(scene(ipr::Size(280, 200), 99), frame, ipr::Recti(300, 200, 280, 200));
        const StackAlignment::Result found = StackAlignment::estimate(reference, StackAlignment::buildPyramid(noisy(frame, 0.004, 3)));
        check(found.valid && cornerError(found.referenceToFrame, truth, size) <= 0.2, "a moving object is outvoted");
    }

    // Texture in a small area only: a homography would be a guess there.
    {
        ipr::Image dark = ipr::filled(size, ipr::Depth::F32, 3, 0.02);
        paste(scene(ipr::Size(220, 180), 5), dark, ipr::Recti(340, 210, 220, 180));
        const ipr::Matrix33d truth(1, 0, 8.0, 0, 1, -5.0, 0, 0, 1);
        const StackAlignment::Result found = StackAlignment::estimate(
            StackAlignment::buildPyramid(noisy(dark, 0.002, 4)), StackAlignment::buildPyramid(noisy(seenFrom(dark, truth), 0.002, 5)));
        check(found.valid, "a small textured area registers");
        check(found.model == StackAlignment::Model::Similarity, "with a transform it can hold");
        ipr::Vec3d centre = found.referenceToFrame * ipr::Vec3d(450, 300, 1);
        check(std::hypot(centre[0] / centre[2] - 458, centre[1] / centre[2] - 295) <= 0.3, "and the right shift on that area");
    }

    // A small subject on an empty background, as the Moon: a regular grid hardly touches it.
    {
        const ipr::Size wide(1800, 1200);
        ipr::Image sky = ipr::filled(wide, ipr::Depth::F32, 3, 0.002);
        paste(scene(ipr::Size(150, 150), 13), sky, ipr::Recti(1000, 300, 150, 150));
        const ipr::Matrix33d truth(1, 0, 45.0, 0, 1, 18.0, 0, 0, 1);
        const StackAlignment::Result found = StackAlignment::estimate(
            StackAlignment::buildPyramid(noisy(sky, 0.002, 8)), StackAlignment::buildPyramid(noisy(seenFrom(sky, truth), 0.002, 9)));
        if (!found.valid)
            std::cerr << "small subject: " << found.error.toStdString() << '\n';
        check(found.valid, "a small subject registers");
        check(found.model == StackAlignment::Model::Similarity, "with what so small a support can hold");
        const ipr::Vec3d at = found.referenceToFrame * ipr::Vec3d(1075, 375, 1);
        const double error = std::hypot(at[0] / at[2] - 1120, at[1] / at[2] - 393);
        if (error > 0.3)
            std::cerr << "small subject error " << error << '\n';
        check(error <= 0.3, "and it lies where it should");
    }

    // No detail, or another scene: a reason, never a guess.
    {
        ipr::Image flat = ipr::filled(size, ipr::Depth::F32, 3, 0.4);
        const StackAlignment::Result found = StackAlignment::estimate(reference, StackAlignment::buildPyramid(noisy(flat, 0.004, 6)));
        check(!found.valid && !found.error.isEmpty(), "a uniform frame fails with its reason");
        const StackAlignment::Result other = StackAlignment::estimate(reference, StackAlignment::buildPyramid(scene(size, 1234)));
        check(!other.valid, "another scene fails");
        const StackAlignment::Result small = StackAlignment::estimate(reference, StackAlignment::buildPyramid(scene(ipr::Size(600, 400))));
        check(!small.valid && !small.error.isEmpty(), "another size fails");
        const StackAlignment::Result stoppedEarly = StackAlignment::estimate(reference, reference, {}, [] { return true; });
        check(!stoppedEarly.valid, "cancellation stops the registration");
    }
}

// A near object over a far wall: the object lies elsewhere than one transform says.
ipr::Image withParallax(const ipr::Image &base, const ipr::Point2d &shift)
{
    const ipr::Recti object(180, 90, 280, 240);
    ipr::Image frame = base.clone();
    const ipr::Image near = scene(base.size(), 21);
    const ipr::Image moved = imagetest::moved(near, translation(shift.x, shift.y), base.size());
    paste(moved.copy(object), frame, object);
    return frame;
}

void localFields()
{
    using StackAlignment::localField;
    check(localField(ipr::Image(), ipr::Image()).displacements.empty(), "no image, no field");
    check(localField(ipr::Image::create({8, 8}, ipr::Depth::F32, 1), ipr::Image::create({8, 9}, ipr::Depth::F32, 1)).displacements.empty(), "different sizes, no field");
    check(localField(ipr::Image::create({8, 8}, ipr::Depth::U8, 1), ipr::Image::create({8, 8}, ipr::Depth::U8, 1)).displacements.empty(), "a wrong type, no field");

    const ipr::Size size(1200, 800);
    const ipr::Image base = scene(size);
    const auto luminance = [](const ipr::Image &rgb) { StackAlignment::Pyramid levels = StackAlignment::buildPyramid(rgb); return std::move(levels[0]); };
    const ipr::Image reference = luminance(noisy(base, 0.004, 1));

    // Nothing moved: the field stays at zero.
    StackAlignment::LocalField still = localField(reference, luminance(noisy(base, 0.004, 2)));
    check(still.displacements.size() == size && still.measured > still.sites / 2, "a textured frame measures its blocks");
    check(still.largest < 0.3, "nothing moved, nothing to correct");

    // A shift everywhere: found within a tenth of a pixel in the middle of the frame.
    const ipr::Image shifted = imagetest::moved(base, translation(3.4, -2.2), size);
    const StackAlignment::LocalField all = localField(reference, luminance(noisy(shifted, 0.004, 3)));
    const ipr::Vec2f middle = imagetest::at<ipr::Vec2f>(all.displacements, 400, 600);
    if (std::abs(middle[0] - 3.4f) > 0.1f || std::abs(middle[1] + 2.2f) > 0.1f)
        std::cerr << "middle " << middle[0] << ' ' << middle[1] << '\n';
    check(std::abs(middle[0] - 3.4f) <= 0.1f && std::abs(middle[1] + 2.2f) <= 0.1f, "a shift is measured");

    // No texture: nothing to measure, and no invented motion.
    ipr::Image flat = ipr::filled(size, ipr::Depth::F32, 3, 0.4);
    const StackAlignment::LocalField none = localField(luminance(noisy(flat, 0.004, 4)), luminance(noisy(flat, 0.004, 5)));
    check(none.measured <= none.sites / 50 && none.largest < 1.0, "a flat frame stays at zero");
    const StackAlignment::LocalField beyond = localField(reference, luminance(noisy(scene(size, 77), 0.004, 6)));
    check(beyond.measured <= beyond.sites / 10, "another scene measures almost nothing");
}

void parallax()
{
    // Nine views where a near object moves against the wall, as with a moving hand.
    const ipr::Size size(640, 420);
    const int count = 9;
    const ipr::Image base = scene(size);
    const ipr::Image clean = withParallax(base, ipr::Point2d(0, 0));
    QTemporaryDir root;
    const auto stack = [&](bool local) {
        StackJob::Request request;
        request.frameCount = count;
        request.referenceIndex = count / 2;
        request.scratchRoot = root.path();
        request.minimumFreeDiskBytes = 0;
        request.localRefinement = local;
        request.local.spacing = 48;
        request.local.blockHalf = 16;
        request.frame = [&](int index) {
            const double step = index - count / 2;
            StackJob::Frame frame;
            frame.cameraRgb = noisy(withParallax(base, ipr::Point2d(step * 0.8, step * -0.5)), 0.02, unsigned(300 + index));
            return frame;
        };
        return StackJob::run(request);
    };
    const StackJob::Result global = stack(false);
    const StackJob::Result refined = stack(true);
    check(global.success && refined.success, "both stacks succeed");
    if (!global.success || !refined.success)
        return;
    const ipr::Recti middle(260, 150, 120, 120);
    const auto errorAt = [&](const StackJob::Result &result) {
        const ipr::Recti inResult = middle - result.commonArea.tl();
        return rms(result.cameraRgb.copy(inResult), clean.copy(middle));
    };
    const double before = errorAt(global), after = errorAt(refined);
    if (after > before * 0.6)
        std::cerr << "parallax error before " << before << " after " << after << '\n';
    check(after <= before * 0.6, "the local field recovers the near object");
    check(refined.frames[0].localBlocks > 0 && refined.frames[0].localLargest > 0.5, "the report tells the field");
    check(global.frames[0].localBlocks == 0, "without it, no field");
}

// A smooth random deformation, as the atmosphere gives each view of a subject.
ipr::Image deformed(const ipr::Image &image, double amplitude, unsigned seed)
{
    // Cells much smaller than the subject: no single transform follows them.
    const ipr::Size cells(image.width() / 60, image.height() / 60);
    const ipr::Image coarseX = imagetest::normalNoise(cells, 1, 0.0, amplitude, seed);
    const ipr::Image coarseY = imagetest::normalNoise(cells, 1, 0.0, amplitude, seed + 1000);
    // The cells spread over the image, read between them as a cubic resize reads.
    ipr::Image cellX = ipr::Image::create(image.size(), ipr::Depth::F32, 1), cellY = ipr::Image::create(image.size(), ipr::Depth::F32, 1);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            imagetest::at<float>(cellX, y, x) = float((x + 0.5) * cells.width / image.width() - 0.5);
            imagetest::at<float>(cellY, y, x) = float((y + 0.5) * cells.height / image.height() - 0.5);
        }
    const ipr::Image moveX = ipr::remapped(coarseX, cellX, cellY, ipr::Resampling::Cubic, ipr::Outside::LastPixel);
    const ipr::Image moveY = ipr::remapped(coarseY, cellX, cellY, ipr::Resampling::Cubic, ipr::Outside::LastPixel);
    ipr::Image mapX = ipr::Image::create(image.size(), ipr::Depth::F32, 1), mapY = ipr::Image::create(image.size(), ipr::Depth::F32, 1);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            imagetest::at<float>(mapX, y, x) = float(x) + imagetest::at<float>(moveX, y, x);
            imagetest::at<float>(mapY, y, x) = float(y) + imagetest::at<float>(moveY, y, x);
        }
    return ipr::remapped(image, mapX, mapY, ipr::Resampling::Lanczos4, ipr::Outside::LastPixel);
}

bool scratchEmpty(const QTemporaryDir &root);

void turbulence()
{
    // A disc of detail on a black sky, drifting, deformed differently in each view.
    const ipr::Size size(1400, 1000);
    const ipr::Recti disc(600, 380, 240, 240);
    const ipr::Image detail = times(scene(disc.size(), 31), 1.5f);
    ipr::Image sky = ipr::filled(size, ipr::Depth::F32, 3, 0.001);
    paste(detail, sky, disc);
    const int count = 12;
    QTemporaryDir root;
    const auto stack = [&](bool correct, int blurred) {
        StackJob::Request request;
        request.frameCount = count;
        request.referenceIndex = count / 2;
        request.scratchRoot = root.path();
        request.minimumFreeDiskBytes = 0;
        request.correctTurbulence = correct;
        request.frame = [&, blurred](int index) {
            const ipr::Matrix33d drift = translation((index - count / 2) * 3.1, (index - count / 2) * 1.9);
            ipr::Image moved = imagetest::moved(deformed(sky, 0.8, unsigned(40 + index)), drift, size);
            // The first views are badly seen: blurred.
            if (index < blurred)
                moved = ipr::gaussianBlurred(moved, 1.6);
            StackJob::Frame frame;
            frame.cameraRgb = noisy(moved, 0.004, unsigned(600 + index));
            return frame;
        };
        return StackJob::run(request);
    };
    const StackJob::Result plain = stack(false, 4);
    const StackJob::Result corrected = stack(true, 4);
    check(plain.success && corrected.success, "both stacks of a small subject succeed");
    if (!plain.success || !corrected.success)
        return;
    check(plain.frames[0].alignment.model == StackAlignment::Model::Similarity, "a small subject holds a similarity");
    check(!plain.turbulence.applied, "without the pass, nothing changes");
    if (!corrected.turbulence.applied)
        std::cerr << "not applied: " << corrected.turbulence.reason.toStdString() << '\n';
    check(corrected.turbulence.applied && corrected.turbulence.views == count, "the pass sees every view");
    check(corrected.turbulence.kept == count / 2, "and keeps the sharpest half");
    check(corrected.turbulence.measuredBlocks > 30, "measured by many small blocks");
    check((corrected.subject & disc) == disc, "its area holds the whole subject");

    // The blurred views measure less sharp than the others; the reference comes first.
    const std::vector<double> &sharpness = corrected.turbulence.sharpness;
    double blurredSum = 0.0, sharpSum = 0.0;
    int blurredCount = 0, sharpCount = 0;
    for (size_t i = 0; i < sharpness.size(); ++i) {
        if (i >= 1 && i <= 4) {
            blurredSum += sharpness[i];
            ++blurredCount;
        } else {
            sharpSum += sharpness[i];
            ++sharpCount;
        }
    }
    check(blurredCount == 4 && blurredSum / blurredCount < sharpSum / sharpCount, "a blurred view measures less sharp");

    // Closer to the true subject than the plain stack, which averages the deformations.
    const ipr::Recti middle(disc.x + 20, disc.y + 20, disc.width - 40, disc.height - 40);
    const auto errorOf = [&](const StackJob::Result &result) {
        const ipr::Recti inResult = middle - result.commonArea.tl();
        return rms(result.cameraRgb.copy(inResult), sky.copy(middle));
    };
    const double before = errorOf(plain), after = errorOf(corrected);
    if (after > before * 0.95)
        std::cerr << "turbulence error before " << before << " after " << after << '\n';
    check(after <= before * 0.95, "the pass recovers detail the first stack lost");

    // The sky away from the subject stays that of the first stack.
    const ipr::Recti far(60, 60, 200, 200);
    const auto at = [](const StackJob::Result &result, const ipr::Recti &area) { return result.cameraRgb.copy(area - result.commonArea.tl()); };
    check(sameBytes(at(plain, far), at(corrected, far)), "the rest of the image is untouched");
    check(scratchEmpty(root), "and the scratch folder is gone");

    // Direct refusals.
    ipr::Image merged = ipr::filled({100, 100}, ipr::Depth::F32, 3, 0.2);
    const auto views = [](int count) {
        std::vector<StackTurbulence::View> made(static_cast<size_t>(count));
        for (StackTurbulence::View &view : made)
            view.pixels = ipr::filled({100, 100}, ipr::Depth::F32, 3, 0.2);
        return made;
    };
    StackTurbulence::Report report = StackTurbulence::refine(merged, ipr::Recti(0, 0, 200, 200), ipr::Point2i(), views(3));
    check(!report.applied && !report.reason.isEmpty(), "an area outside the image is refused");
    report = StackTurbulence::refine(merged, ipr::Recti(0, 0, 50, 50), ipr::Point2i(), views(1));
    check(!report.applied && !report.reason.isEmpty(), "too few views are refused");
    report = StackTurbulence::refine(merged, ipr::Recti(0, 0, 50, 50), ipr::Point2i(), views(3), {}, [] { return true; });
    check(!report.applied, "cancellation stops the pass");
    check(imagetest::largestAbsolute(ipr::mapped(merged, [](float v) { return v - 0.2f; })) < 1e-6, "and a refused pass leaves the stack");
}

// A series of noisy views of one scene; `extra` adds the frames that must be left out.
struct Series
{
    ipr::Image base;
    std::vector<ipr::Matrix33d> moves;
    double noise = 0.02;
    int calls = 0;

    StackJob::Frame frame(int index)
    {
        ++calls;
        StackJob::Frame result;
        result.cameraRgb = noisy(seenFrom(base, moves[size_t(index)]), noise, unsigned(100 + index));
        return result;
    }
};

Series makeSeries(const ipr::Size &size, int count)
{
    Series series;
    series.base = scene(size);
    std::mt19937 random(42);
    const auto uniform = [&random](double low, double high) { return std::uniform_real_distribution<double>(low, high)(random); };
    for (int i = 0; i < count; ++i) {
        if (i == count / 2) {
            series.moves.push_back(ipr::Matrix33d::identity());
            continue;
        }
        const double degrees = uniform(-0.4, 0.4), dx = uniform(-12.0, 12.0), dy = uniform(-9.0, 9.0);
        series.moves.push_back(rotation(degrees, size, dx, dy));
    }
    return series;
}

double noiseOf(const ipr::Image &merged, const ipr::Image &truth)
{
    std::vector<double> mean, deviation;
    ipr::meanAndDeviationOf(minus(merged, truth), mean, deviation);
    return (deviation[0] + deviation[1] + deviation[2]) / 3.0;
}

bool scratchEmpty(const QTemporaryDir &root)
{
    return QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();
}

void job()
{
    const ipr::Size size(600, 400);
    const int count = 9;
    QTemporaryDir root;
    check(root.isValid(), "a scratch root for the test");

    const auto request = [&](Series &series) {
        StackJob::Request r;
        r.frameCount = count;
        r.referenceIndex = count / 2;
        r.frame = [&series](int index) { return series.frame(index); };
        r.scratchRoot = root.path();
        r.minimumFreeDiskBytes = 0;
        return r;
    };

    // The whole path: registered, merged, cleaned.
    Series series = makeSeries(size, count);
    int lastDone = 0, lastTotal = 0;
    StackJob::Request whole = request(series);
    whole.progress = [&](int done, int total) { lastDone = done; lastTotal = total; };
    const StackJob::Result result = StackJob::run(whole);
    check(result.success, "the job succeeds");
    if (!result.success) {
        std::cerr << result.error.toStdString() << '\n';
        return;
    }
    check(result.usedFrames == count && series.calls == count, "each frame is decoded once");
    check(result.frames[size_t(count / 2)].reference && result.frames[0].used, "the report says which frame did what");
    check(scratchEmpty(root), "the scratch folder is gone");
    check(result.scratchBytes == StackJob::scratchBytesNeeded(count, size), "the frames were written");
    check(lastDone == lastTotal && lastTotal > count, "the progress ends complete");
    check(result.cameraRgb.size() == result.commonArea.size() && result.commonArea.area() > size.area() / 2, "the common area stays large");
    const ipr::Recti bounds(0, 0, size.width, size.height);
    check((result.commonArea & bounds) == result.commonArea, "the common area lies in the frame");

    // The gain: towards 1/sqrt(N) against one frame, measured on the known scene.
    const ipr::Image truth = series.base.copy(result.commonArea);
    const double single = noiseOf(noisy(truth, series.noise, 500), truth);
    const double stacked = noiseOf(result.cameraRgb, truth);
    if (stacked > single / 2.2 || stacked < single / 4.5)
        std::cerr << "single " << single << " stacked " << stacked << '\n';
    check(stacked <= single / 2.2, "nine frames divide the noise by nearly three");
    check(stacked >= single / 4.5, "and no more than resampling explains: nothing was blurred away");
    check(result.frames[0].localBlocks > 0, "the local field is measured by default");

    // The result does not depend on how much memory the bands had.
    Series second = makeSeries(size, count);
    StackJob::Request tight = request(second);
    tight.memoryBudgetBytes = 1;
    const StackJob::Result small = StackJob::run(tight);
    check(small.success && small.bandRows == 16, "a tight budget works by small bands");
    check(sameBytes(small.cameraRgb, result.cameraRgb), "the same pixels, whatever the bands");

    // Without the reference: independent of its noise, the same area for the others.
    Series apart = makeSeries(size, count);
    StackJob::Request withoutReference = request(apart);
    withoutReference.mergeReference = false;
    const StackJob::Result independent = StackJob::run(withoutReference);
    check(independent.success && independent.usedFrames == count - 1 && !independent.frames[size_t(count / 2)].used, "the reference can stay out");
    check(independent.commonArea == result.commonArea, "with the same common area");
    check(!sameBytes(independent.cameraRgb, result.cameraRgb), "and other pixels");

    // A lamp's flicker and a passing object.
    Series moved = makeSeries(size, count);
    StackJob::Request lit = request(moved);
    lit.frame = [&moved](int index) {
        StackJob::Frame frame = moved.frame(index);
        if (index == 1)
            frame.cameraRgb = times(frame.cameraRgb, 1.08f);
        if (index == 2)
            paint(frame.cameraRgb, ipr::Recti(280, 180, 60, 60), 0.95);
        return frame;
    };
    const StackJob::Result corrected = StackJob::run(lit);
    check(corrected.success && std::abs(corrected.frames[1].gains[1] - 1.0 / 1.08) < 0.01, "a flicker is measured and undone");
    if (corrected.success) {
        const ipr::Image clean = moved.base.copy(corrected.commonArea);
        check(noiseOf(corrected.cameraRgb, clean) <= single / 2.0, "neither the flicker nor the object remains");
    }

    // Frames that cannot belong are left out, with their reason, and the stack goes on.
    Series mixed = makeSeries(size, count);
    StackJob::Request excluded = request(mixed);
    excluded.frame = [&mixed, size](int index) {
        StackJob::Frame frame = mixed.frame(index);
        if (index == 0)
            frame.cameraRgb = ipr::filled(size, ipr::Depth::F32, 3, 0.4);
        if (index == 1)
            frame.cameraRgb = ipr::filled(ipr::Size(300, 200), ipr::Depth::F32, 3, 0.4);
        if (index == 2)
            frame.cameraRgb = ipr::Image();
        if (index == 3)
            frame.cameraRgb = times(frame.cameraRgb, 2.0f);
        return frame;
    };
    const StackJob::Result partial = StackJob::run(excluded);
    check(partial.success && partial.usedFrames == count - 4, "the others still stack");
    for (const int index : {0, 1, 2, 3})
        check(!partial.frames[size_t(index)].used && !partial.frames[size_t(index)].exclusion.isEmpty(), "a left-out frame says why");
    check(scratchEmpty(root), "nothing stays after a partial stack");

    // Too little to stack.
    Series alone = makeSeries(size, count);
    StackJob::Request lonely = request(alone);
    lonely.frame = [&alone, size](int index) {
        StackJob::Frame frame;
        if (index == alone.moves.size() / 2)
            frame = alone.frame(index);
        else
            frame.error = QStringLiteral("unreadable");
        return frame;
    };
    const StackJob::Result none = StackJob::run(lonely);
    check(!none.success && !none.error.isEmpty() && none.cameraRgb.empty(), "one frame is no stack");
    check(none.frames[0].exclusion == QStringLiteral("unreadable"), "a provider's error is kept");
    check(scratchEmpty(root), "nothing stays after a refused stack");

    // Cancelled while decoding, and while merging: nothing stays, nothing is returned.
    Series first = makeSeries(size, count);
    StackJob::Request stopping = request(first);
    stopping.cancelled = [&first] { return first.calls >= 4; };
    const StackJob::Result stopped = StackJob::run(stopping);
    check(!stopped.success && stopped.cancelled && stopped.cameraRgb.empty(), "a cancelled job says so");
    check(first.calls == 4, "and stops decoding");
    check(scratchEmpty(root), "nothing stays after a cancellation");
    Series last = makeSeries(size, count);
    StackJob::Request lateStop = request(last);
    bool lateFlag = false;
    lateStop.progress = [&](int done, int) { lateFlag = done > count; };
    lateStop.memoryBudgetBytes = 1;
    lateStop.cancelled = [&] { return lateFlag; };
    const StackJob::Result late = StackJob::run(lateStop);
    check(!late.success && late.cancelled && late.cameraRgb.empty(), "a merge can be cancelled too");
    check(scratchEmpty(root), "nothing stays after a cancelled merge");

    // Refused before any writing.
    Series full = makeSeries(size, count);
    StackJob::Request noRoom = request(full);
    noRoom.minimumFreeDiskBytes = quint64(1) << 60;
    const StackJob::Result refused = StackJob::run(noRoom);
    check(!refused.success && refused.error.contains(QStringLiteral("disque")) && full.calls == 1, "not enough disk is refused at once");
    Series absent = makeSeries(size, count);
    StackJob::Request badRoot = request(absent);
    badRoot.scratchRoot = QDir(root.path()).filePath(QStringLiteral("does/not/exist"));
    check(!StackJob::run(badRoot).success, "a missing scratch root is refused");

    // Invalid requests.
    StackJob::Request invalid;
    check(!StackJob::run(invalid).success, "an empty request is refused");
    Series outOfRange = makeSeries(size, count);
    StackJob::Request wrongReference = request(outOfRange);
    wrongReference.referenceIndex = count;
    check(!StackJob::run(wrongReference).success && outOfRange.calls == 0, "a reference out of the series is refused");
    Series unreadable = makeSeries(size, count);
    StackJob::Request noReference = request(unreadable);
    noReference.frame = [](int) { StackJob::Frame frame; frame.error = QStringLiteral("broken"); return frame; };
    const StackJob::Result failed = StackJob::run(noReference);
    check(!failed.success && failed.error == QStringLiteral("broken"), "an unreadable reference stops with its reason");
    check(scratchEmpty(root), "the scratch root stays empty");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    rectangles();
    merging();
    alignment();
    localFields();
    parallax();
    job();
    turbulence();
    if (passed)
        std::cout << "stack: ok\n";
    return passed ? 0 : 1;
}
