#include "StackAlignment.h"
#include "StackJob.h"
#include "StackMerge.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstring>
#include <iostream>
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
cv::Mat scene(const cv::Size &size, uint64_t seed = 7)
{
    cv::RNG random(seed);
    cv::Mat image = cv::Mat::zeros(size, CV_32FC3);
    for (const double sigma : {1.5, 5.0, 18.0}) {
        cv::Mat layer(size, CV_32FC3);
        random.fill(layer, cv::RNG::NORMAL, cv::Scalar::all(0), cv::Scalar::all(1));
        cv::GaussianBlur(layer, layer, cv::Size(), sigma);
        cv::Scalar mean, deviation;
        cv::meanStdDev(layer, mean, deviation);
        image += layer * (0.05 / std::max(1e-6, deviation[0]));
    }
    image += cv::Scalar::all(0.3);
    return image;
}

cv::Mat noisy(const cv::Mat &image, double sigma, uint64_t seed)
{
    cv::RNG random(seed);
    cv::Mat noise(image.size(), CV_32FC3);
    random.fill(noise, cv::RNG::NORMAL, cv::Scalar::all(0), cv::Scalar::all(sigma));
    return image + noise;
}

// The frame a camera moved by referenceToFrame would have seen of the scene.
cv::Mat seenFrom(const cv::Mat &base, const cv::Matx33d &referenceToFrame)
{
    cv::Mat frame;
    cv::warpPerspective(base, frame, referenceToFrame, base.size(), cv::INTER_LANCZOS4, cv::BORDER_REPLICATE);
    return frame;
}

double cornerError(const cv::Matx33d &found, const cv::Matx33d &truth, const cv::Size &size)
{
    double worst = 0.0;
    const double margin = 0.1;
    for (const double u : {margin, 1.0 - margin})
        for (const double v : {margin, 1.0 - margin}) {
            const cv::Vec3d p(u * size.width, v * size.height, 1.0);
            const cv::Vec3d a = found * p, b = truth * p;
            worst = std::max(worst, std::hypot(a[0] / a[2] - b[0] / b[2], a[1] / a[2] - b[1] / b[2]));
        }
    return worst;
}

cv::Matx33d rotation(double degrees, const cv::Size &size, double dx, double dy)
{
    const double r = degrees * CV_PI / 180.0, c = std::cos(r), s = std::sin(r);
    const double cx = size.width / 2.0, cy = size.height / 2.0;
    return {c, -s, cx - c * cx + s * cy + dx, s, c, cy - s * cx - c * cy + dy, 0, 0, 1};
}

bool sameBytes(const cv::Mat &a, const cv::Mat &b)
{
    if (a.size() != b.size() || a.type() != b.type())
        return false;
    for (int y = 0; y < a.rows; ++y)
        if (std::memcmp(a.ptr(y), b.ptr(y), size_t(a.cols) * a.elemSize()) != 0)
            return false;
    return true;
}

void rectangles()
{
    using StackJob::largestRectangle;
    check(largestRectangle({}).empty(), "no rows, no rectangle");
    check(largestRectangle({cv::Vec2i(5, 5), cv::Vec2i(9, 2)}).empty(), "empty spans, no rectangle");
    check(largestRectangle({cv::Vec2i(2, 8)}) == cv::Rect(2, 0, 6, 1), "a single row");
    // A diamond: the best rectangle holds its middle rows.
    std::vector<cv::Vec2i> diamond;
    for (int y = 0; y < 21; ++y) {
        const int half = 10 - std::abs(y - 10);
        diamond.emplace_back(10 - half, 10 + half + 1);
    }
    const cv::Rect best = largestRectangle(diamond);
    check(best.area() >= 110 && best.area() <= 121, "a diamond keeps about half its area");
    for (int y = best.y; y < best.y + best.height; ++y)
        check(best.x >= diamond[size_t(y)][0] && best.x + best.width <= diamond[size_t(y)][1], "the rectangle stays inside");
    check(largestRectangle({cv::Vec2i(0, 0), cv::Vec2i(1, 4), cv::Vec2i(1, 4), cv::Vec2i(0, 0)}) == cv::Rect(1, 1, 3, 2),
          "empty rows at both ends are left out");
}

void merging()
{
    cv::Mat merged;
    check(!StackMerge::merge({}, 3.0, merged), "no frames, no merge");
    check(!StackMerge::merge({cv::Mat()}, 3.0, merged), "an empty frame is refused");
    check(!StackMerge::merge({cv::Mat(2, 2, CV_8UC3, cv::Scalar::all(0))}, 3.0, merged), "a wrong type is refused");
    check(!StackMerge::merge({cv::Mat(2, 2, CV_32FC3, cv::Scalar::all(0)), cv::Mat(2, 3, CV_32FC3, cv::Scalar::all(0))}, 3.0, merged),
          "different sizes are refused");

    // Below the minimum: a plain average, even of an outlier.
    std::vector<cv::Mat> few;
    for (const float value : {0.1f, 0.2f, 0.9f})
        few.emplace_back(2, 2, CV_32FC3, cv::Scalar::all(value));
    check(StackMerge::merge(few, 3.0, merged) && std::abs(merged.at<cv::Vec3f>(0, 0)[1] - 0.4f) < 1e-6f, "three frames average");

    // Enough frames: what flashed in one of them goes, the rest averages.
    std::vector<cv::Mat> many;
    for (int i = 0; i < 9; ++i)
        many.emplace_back(4, 4, CV_32FC3, cv::Scalar::all(0.2 + 0.001 * i));
    many[3].at<cv::Vec3f>(1, 1) = cv::Vec3f(0.9f, 0.1f, 0.9f);
    StackMerge::Statistics statistics;
    check(StackMerge::merge(many, 3.0, merged, &statistics), "nine frames merge");
    const float others = float((0.2 * 9 + 0.001 * 36 - (0.2 + 0.003)) / 8.0);
    const cv::Vec3f mergedPixel = merged.at<cv::Vec3f>(1, 1);
    check(std::abs(mergedPixel[0] - others) < 1e-5f && std::abs(mergedPixel[1] - others) < 1e-5f, "the outlier is out, colour included");
    check(statistics.samples == 16 * 9 && statistics.rejected >= 1, "the statistics count it");

    // A contrasted edge that wavers by one pixel between frames stays a clean line: the
    // decisions read a neighbourhood, and a sample fades instead of flipping.
    std::vector<cv::Mat> wavering;
    cv::RNG waver(11);
    for (int i = 0; i < 12; ++i) {
        cv::Mat frame(64, 64, CV_32FC3, cv::Scalar::all(0.05));
        const int edge = 32 + (i % 2);
        frame(cv::Rect(edge, 0, 64 - edge, 64)).setTo(cv::Scalar::all(0.6));
        cv::Mat noise(frame.size(), CV_32FC3);
        waver.fill(noise, cv::RNG::NORMAL, cv::Scalar::all(0), cv::Scalar::all(0.01));
        wavering.push_back(frame + noise);
    }
    check(StackMerge::merge(wavering, 3.0, merged), "a wavering edge merges");
    double teeth = 0.0;
    for (int y = 1; y < 63; ++y) {
        const float here = merged.at<cv::Vec3f>(y, 32)[1], next = merged.at<cv::Vec3f>(y + 1, 32)[1];
        teeth = std::max(teeth, double(std::abs(here - next)));
    }
    if (teeth > 0.1)
        std::cerr << "teeth " << teeth << '\n';
    check(teeth <= 0.1, "the edge has no teeth along it");

    // Signed values stay signed; rejection off keeps the outlier.
    std::vector<cv::Mat> negative(6, cv::Mat(2, 2, CV_32FC3, cv::Scalar::all(-0.01)));
    check(StackMerge::merge(negative, 3.0, merged) && merged.at<cv::Vec3f>(0, 0)[0] < -0.009f, "values below the black stay");
    check(StackMerge::merge(many, 0.0, merged, &statistics) && statistics.rejected == 0 && merged.at<cv::Vec3f>(1, 1)[0] > others, "sigma 0 keeps all");
}

void alignment()
{
    const cv::Size size(900, 600);
    const cv::Mat base = scene(size);
    const StackAlignment::Pyramid reference = StackAlignment::buildPyramid(noisy(base, 0.004, 1));
    check(reference.size() >= 2 && reference[0].size() == size, "the pyramid starts at the whole size");
    check(StackAlignment::buildPyramid(cv::Mat()).empty(), "no image, no pyramid");
    check(StackAlignment::buildPyramid(cv::Mat(4, 4, CV_8UC3)).empty(), "a wrong type gives no pyramid");

    const std::vector<std::pair<const char *, cv::Matx33d>> cases{
        {"identity", cv::Matx33d::eye()},
        {"sub-pixel shift", {1, 0, 0.37, 0, 1, -0.62, 0, 0, 1}},
        {"a large shift", {1, 0, 37.3, 0, 1, -21.7, 0, 0, 1}},
        {"a rotation", rotation(1.5, size, 6.2, -3.4)},
        {"a perspective", {1.004, 0.006, 11.0, -0.005, 0.998, -7.5, 1.2e-5, -0.9e-5, 1}},
    };
    for (const auto &[name, truth] : cases) {
        const cv::Mat frame = noisy(seenFrom(base, truth), 0.004, 2);
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
        const cv::Matx33d truth(1, 0, 14.0, 0, 1, 9.0, 0, 0, 1);
        cv::Mat frame = seenFrom(base, truth);
        scene(cv::Size(280, 200), 99).copyTo(frame(cv::Rect(300, 200, 280, 200)));
        const StackAlignment::Result found = StackAlignment::estimate(reference, StackAlignment::buildPyramid(noisy(frame, 0.004, 3)));
        check(found.valid && cornerError(found.referenceToFrame, truth, size) <= 0.2, "a moving object is outvoted");
    }

    // Texture in a small area only: a homography would be a guess there.
    {
        cv::Mat dark(size, CV_32FC3, cv::Scalar::all(0.02));
        scene(cv::Size(220, 180), 5).copyTo(dark(cv::Rect(340, 210, 220, 180)));
        const cv::Matx33d truth(1, 0, 8.0, 0, 1, -5.0, 0, 0, 1);
        const StackAlignment::Result found = StackAlignment::estimate(
            StackAlignment::buildPyramid(noisy(dark, 0.002, 4)), StackAlignment::buildPyramid(noisy(seenFrom(dark, truth), 0.002, 5)));
        check(found.valid, "a small textured area registers");
        check(found.model == StackAlignment::Model::Similarity, "with a transform it can hold");
        cv::Vec3d centre = found.referenceToFrame * cv::Vec3d(450, 300, 1);
        check(std::hypot(centre[0] / centre[2] - 458, centre[1] / centre[2] - 295) <= 0.3, "and the right shift on that area");
    }

    // A small subject on an empty background, as the Moon: a regular grid hardly touches it.
    {
        const cv::Size wide(1800, 1200);
        cv::Mat sky(wide, CV_32FC3, cv::Scalar::all(0.002));
        scene(cv::Size(150, 150), 13).copyTo(sky(cv::Rect(1000, 300, 150, 150)));
        const cv::Matx33d truth(1, 0, 45.0, 0, 1, 18.0, 0, 0, 1);
        const StackAlignment::Result found = StackAlignment::estimate(
            StackAlignment::buildPyramid(noisy(sky, 0.002, 8)), StackAlignment::buildPyramid(noisy(seenFrom(sky, truth), 0.002, 9)));
        if (!found.valid)
            std::cerr << "small subject: " << found.error.toStdString() << '\n';
        check(found.valid, "a small subject registers");
        check(found.model == StackAlignment::Model::Similarity, "with what so small a support can hold");
        const cv::Vec3d at = found.referenceToFrame * cv::Vec3d(1075, 375, 1);
        const double error = std::hypot(at[0] / at[2] - 1120, at[1] / at[2] - 393);
        if (error > 0.3)
            std::cerr << "small subject error " << error << '\n';
        check(error <= 0.3, "and it lies where it should");
    }

    // No detail, or another scene: a reason, never a guess.
    {
        const cv::Mat flat(size, CV_32FC3, cv::Scalar::all(0.4));
        const StackAlignment::Result found = StackAlignment::estimate(reference, StackAlignment::buildPyramid(noisy(flat, 0.004, 6)));
        check(!found.valid && !found.error.isEmpty(), "a uniform frame fails with its reason");
        const StackAlignment::Result other = StackAlignment::estimate(reference, StackAlignment::buildPyramid(scene(size, 1234)));
        check(!other.valid, "another scene fails");
        const StackAlignment::Result small = StackAlignment::estimate(reference, StackAlignment::buildPyramid(scene(cv::Size(600, 400))));
        check(!small.valid && !small.error.isEmpty(), "another size fails");
        const StackAlignment::Result stoppedEarly = StackAlignment::estimate(reference, reference, {}, [] { return true; });
        check(!stoppedEarly.valid, "cancellation stops the registration");
    }
}

// A near object over a far wall: the object lies elsewhere than one transform says.
cv::Mat withParallax(const cv::Mat &base, const cv::Point2d &shift)
{
    const cv::Rect object(180, 90, 280, 240);
    cv::Mat frame = base.clone();
    const cv::Mat near = scene(base.size(), 21);
    cv::Mat moved;
    const cv::Matx23d translation(1, 0, shift.x, 0, 1, shift.y);
    cv::warpAffine(near, moved, translation, base.size(), cv::INTER_LANCZOS4, cv::BORDER_REPLICATE);
    moved(object).copyTo(frame(object));
    return frame;
}

void localFields()
{
    using StackAlignment::localField;
    check(localField(cv::Mat(), cv::Mat()).displacements.empty(), "no image, no field");
    check(localField(cv::Mat(8, 8, CV_32FC1), cv::Mat(9, 8, CV_32FC1)).displacements.empty(), "different sizes, no field");
    check(localField(cv::Mat(8, 8, CV_8UC1), cv::Mat(8, 8, CV_8UC1)).displacements.empty(), "a wrong type, no field");

    const cv::Size size(1200, 800);
    const cv::Mat base = scene(size);
    const auto luminance = [](const cv::Mat &rgb) { return StackAlignment::buildPyramid(rgb)[0]; };
    const cv::Mat reference = luminance(noisy(base, 0.004, 1));

    // Nothing moved: the field stays at zero.
    StackAlignment::LocalField still = localField(reference, luminance(noisy(base, 0.004, 2)));
    check(still.displacements.size() == size && still.measured > still.sites / 2, "a textured frame measures its blocks");
    check(still.largest < 0.3, "nothing moved, nothing to correct");

    // A shift everywhere: found within a tenth of a pixel in the middle of the frame.
    const cv::Matx23d everywhere(1, 0, 3.4, 0, 1, -2.2);
    cv::Mat shifted;
    cv::warpAffine(base, shifted, everywhere, size, cv::INTER_LANCZOS4, cv::BORDER_REPLICATE);
    const StackAlignment::LocalField all = localField(reference, luminance(noisy(shifted, 0.004, 3)));
    const cv::Vec2f middle = all.displacements.at<cv::Vec2f>(400, 600);
    if (std::abs(middle[0] - 3.4f) > 0.1f || std::abs(middle[1] + 2.2f) > 0.1f)
        std::cerr << "middle " << middle[0] << ' ' << middle[1] << '\n';
    check(std::abs(middle[0] - 3.4f) <= 0.1f && std::abs(middle[1] + 2.2f) <= 0.1f, "a shift is measured");

    // No texture: nothing to measure, and no invented motion.
    const cv::Mat flat(size, CV_32FC3, cv::Scalar::all(0.4));
    const StackAlignment::LocalField none = localField(luminance(noisy(flat, 0.004, 4)), luminance(noisy(flat, 0.004, 5)));
    check(none.measured <= none.sites / 50 && none.largest < 1.0, "a flat frame stays at zero");
    const StackAlignment::LocalField beyond = localField(reference, luminance(noisy(scene(size, 77), 0.004, 6)));
    check(beyond.measured <= beyond.sites / 10, "another scene measures almost nothing");
}

void parallax()
{
    // Nine views where a near object moves against the wall, as with a moving hand.
    const cv::Size size(640, 420);
    const int count = 9;
    const cv::Mat base = scene(size);
    const cv::Mat clean = withParallax(base, cv::Point2d(0, 0));
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
            frame.cameraRgb = noisy(withParallax(base, cv::Point2d(step * 0.8, step * -0.5)), 0.02, uint64_t(300 + index));
            return frame;
        };
        return StackJob::run(request);
    };
    const StackJob::Result global = stack(false);
    const StackJob::Result refined = stack(true);
    check(global.success && refined.success, "both stacks succeed");
    if (!global.success || !refined.success)
        return;
    const cv::Rect middle(260, 150, 120, 120);
    const auto errorAt = [&](const StackJob::Result &result) {
        const cv::Rect inResult = middle - result.commonArea.tl();
        cv::Mat difference = result.cameraRgb(inResult) - clean(middle);
        return cv::norm(difference, cv::NORM_L2) / std::sqrt(double(difference.total() * 3));
    };
    const double before = errorAt(global), after = errorAt(refined);
    if (after > before * 0.6)
        std::cerr << "parallax error before " << before << " after " << after << '\n';
    check(after <= before * 0.6, "the local field recovers the near object");
    check(refined.frames[0].localBlocks > 0 && refined.frames[0].localLargest > 0.5, "the report tells the field");
    check(global.frames[0].localBlocks == 0, "without it, no field");
}

// A series of noisy views of one scene; `extra` adds the frames that must be left out.
struct Series
{
    cv::Mat base;
    std::vector<cv::Matx33d> moves;
    double noise = 0.02;
    int calls = 0;

    StackJob::Frame frame(int index)
    {
        ++calls;
        StackJob::Frame result;
        result.cameraRgb = noisy(seenFrom(base, moves[size_t(index)]), noise, uint64_t(100 + index));
        return result;
    }
};

Series makeSeries(const cv::Size &size, int count)
{
    Series series;
    series.base = scene(size);
    cv::RNG random(42);
    for (int i = 0; i < count; ++i)
        series.moves.push_back(i == count / 2 ? cv::Matx33d::eye()
                                              : rotation(random.uniform(-0.4, 0.4), size, random.uniform(-12.0, 12.0), random.uniform(-9.0, 9.0)));
    return series;
}

double noiseOf(const cv::Mat &merged, const cv::Mat &truth)
{
    cv::Mat difference = merged - truth;
    cv::Scalar mean, deviation;
    cv::meanStdDev(difference, mean, deviation);
    return (deviation[0] + deviation[1] + deviation[2]) / 3.0;
}

bool scratchEmpty(const QTemporaryDir &root)
{
    return QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();
}

void job()
{
    const cv::Size size(600, 400);
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
    const cv::Rect bounds(0, 0, size.width, size.height);
    check((result.commonArea & bounds) == result.commonArea, "the common area lies in the frame");

    // The gain: towards 1/sqrt(N) against one frame, measured on the known scene.
    const cv::Mat truth = series.base(result.commonArea);
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
            frame.cameraRgb *= 1.08;
        if (index == 2)
            frame.cameraRgb(cv::Rect(280, 180, 60, 60)).setTo(cv::Scalar::all(0.95));
        return frame;
    };
    const StackJob::Result corrected = StackJob::run(lit);
    check(corrected.success && std::abs(corrected.frames[1].gains[1] - 1.0 / 1.08) < 0.01, "a flicker is measured and undone");
    if (corrected.success) {
        const cv::Mat clean = moved.base(corrected.commonArea);
        check(noiseOf(corrected.cameraRgb, clean) <= single / 2.0, "neither the flicker nor the object remains");
    }

    // Frames that cannot belong are left out, with their reason, and the stack goes on.
    Series mixed = makeSeries(size, count);
    StackJob::Request excluded = request(mixed);
    excluded.frame = [&mixed, size](int index) {
        StackJob::Frame frame = mixed.frame(index);
        if (index == 0)
            frame.cameraRgb = cv::Mat(size, CV_32FC3, cv::Scalar::all(0.4));
        if (index == 1)
            frame.cameraRgb = cv::Mat(cv::Size(300, 200), CV_32FC3, cv::Scalar::all(0.4));
        if (index == 2)
            frame.cameraRgb = cv::Mat();
        if (index == 3)
            frame.cameraRgb *= 2.0;
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
    if (passed)
        std::cout << "stack: ok\n";
    return passed ? 0 : 1;
}
