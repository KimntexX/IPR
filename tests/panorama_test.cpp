// The panorama engine on synthetic series whose geometry is known: frames rendered from one
// viewpoint on a textured world, so that what the engine finds can be compared to the truth.
#include "PanoramaBlend.h"
#include "PanoramaFeatures.h"
#include "PanoramaFill.h"
#include "PanoramaGeometry.h"
#include "PanoramaJob.h"
#include "Filters.h"
#include "Pixels.h"
#include "Resize.h"
#include "Warp.h"
#include "image_testing.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

using namespace PanoramaGeometry;

namespace {

int failures = 0;

bool check(bool condition, const char *what)
{
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
        ++failures;
    }
    return condition;
}

constexpr double degree = ipr::pi / 180.0;
const ipr::Size frameSize(900, 600);
constexpr double trueFocal = 1300.0;

ipr::Image textured(const ipr::Size &size, int seed)
{
    ipr::Image image = ipr::filled(size, ipr::Depth::F32, 3, 0.25);
    unsigned layerSeed = unsigned(seed) * 16u;
    for (const double sigma : {1.2, 3.0, 8.0, 24.0}) {
        const ipr::Image layer = ipr::gaussianBlurred(imagetest::normalNoise(size, 1, 0.0, 1.0, layerSeed++), sigma);
        std::vector<double> mean, deviation;
        ipr::meanAndDeviationOf(layer, mean, deviation);
        const float weight = float(0.05 / std::max(1e-6, deviation[0]));
        image = ipr::mapped(image, ipr::merged({&layer, &layer, &layer}), [weight](float a, float b) { return a + b * weight; });
    }
    return ipr::mapped(image, [](float v) { return std::max(v, 0.005f); });
}

// A camera turned by yaw and pitch (positive: to the right, downward) and a little roll.
ipr::Matrix33d camera(double yaw, double pitch, double roll = 0.0)
{
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
    const ipr::Matrix33d turn(cy, 0, -sy, 0, 1, 0, sy, 0, cy);
    const ipr::Matrix33d tilt(1, 0, 0, 0, cp, -sp, 0, sp, cp);
    const ipr::Matrix33d lean(cr, -sr, 0, sr, cr, 0, 0, 0, 1);
    return lean * tilt * turn;
}

// A place of the world mirrored back into it, the edge pixel repeated as in a mirror.
float mirroredInto(double place, int length)
{
    if (place < -0.5)
        place = -1.0 - place;
    if (place > length - 0.5)
        place = 2.0 * length - 1.0 - place;
    return float(place);
}

// The frame such a camera sees on a world mapped by yaw and pitch.
ipr::Image render(const ipr::Image &world, const ipr::Matrix33d &worldToCamera, double gain = 1.0, double distortion = 0.0)
{
    const double inverseCorner = 4.0 / (double(frameSize.width) * frameSize.width + double(frameSize.height) * frameSize.height);
    const double perRadian = world.width() / (160.0 * degree);
    ipr::Image mapX = ipr::Image::create(frameSize, ipr::Depth::F32, 1), mapY = ipr::Image::create(frameSize, ipr::Depth::F32, 1);
    const ipr::Matrix33d back = worldToCamera.t();
    for (int y = 0; y < frameSize.height; ++y) {
        for (int x = 0; x < frameSize.width; ++x) {
            const double seenX = x - (frameSize.width - 1) * 0.5, seenY = y - (frameSize.height - 1) * 0.5;
            double idealX = seenX, idealY = seenY;
            for (int step = 0; step < 6; ++step) {
                const double factor = 1.0 + distortion * inverseCorner * (idealX * idealX + idealY * idealY);
                idealX = seenX / factor;
                idealY = seenY / factor;
            }
            const ipr::Vec3d ray = back * ipr::Vec3d(idealX / trueFocal, idealY / trueFocal, 1.0);
            imagetest::at<float>(mapX, y, x) = mirroredInto(std::atan2(ray[0], ray[2]) * perRadian + world.width() * 0.5, world.width());
            imagetest::at<float>(mapY, y, x) =
                mirroredInto(std::atan2(ray[1], std::hypot(ray[0], ray[2])) * perRadian + world.height() * 0.5, world.height());
        }
    }
    const ipr::Image frame = ipr::remapped(world, mapX, mapY, ipr::Resampling::Cubic, ipr::Outside::LastPixel);
    const float factor = float(gain);
    return ipr::mapped(frame, [factor](float v) { return v * factor; });
}

const ipr::Image &sharedWorld()
{
    static const ipr::Image world = textured(ipr::Size(7000, 4000), 11);
    return world;
}

PanoramaJob::Request requestFor(const std::vector<ipr::Image> &frames)
{
    PanoramaJob::Request request;
    request.frameCount = int(frames.size());
    request.frame = [&frames](int index) {
        PanoramaJob::Frame frame;
        frame.cameraRgb = frames[size_t(index)].clone();
        return frame;
    };
    return request;
}

// Frames for a series: an initializer list would copy them.
template <typename... Images>
std::vector<ipr::Image> framesOf(Images &&...images)
{
    std::vector<ipr::Image> frames;
    (frames.push_back(std::move(images)), ...);
    return frames;
}

ipr::Image greenOf(const ipr::Image &rgb)
{
    return std::move(ipr::splitChannels(rgb)[1]);
}

// One value into an area of the image.
void paint(ipr::Image &image, const ipr::Recti &area, double value)
{
    ipr::Image place = image.mutableView(area);
    ipr::fill(place, value);
}

// The mean of a plane of floats where the mask, bytes, is not zero.
double meanWhere(const ipr::Image &plane, const ipr::Image &mask)
{
    double sum = 0.0;
    long long count = 0;
    for (int y = 0; y < plane.height(); ++y)
        for (int x = 0; x < plane.width(); ++x)
            if (mask.row<std::uint8_t>(y)[x]) {
                sum += plane.row<float>(y)[x];
                ++count;
            }
    return count > 0 ? sum / double(count) : 0.0;
}

// The frames' bottom-centre rows on the canvas, once the job has turned the world.
double footDrift(const Solution &solution, const Surface &surface)
{
    double lowest = 0.0, highest = 0.0;
    bool first = true;
    for (const int m : solution.members) {
        ipr::Point2d seat;
        surface.place(solution.worldToCamera[size_t(m)].t() * ipr::Vec3d(0.0, (frameSize.height - 1) * 0.5 / solution.focal, 1.0), seat);
        lowest = first ? seat.y : std::min(lowest, seat.y);
        highest = first ? seat.y : std::max(highest, seat.y);
        first = false;
    }
    return highest - lowest;
}

Solution solveSeries(const std::vector<ipr::Image> &frames, const LensKnowledge &lens = {})
{
    std::vector<std::vector<PanoramaFeatures::Feature>> features;
    std::vector<ipr::Image> grey;
    for (const ipr::Image &frame : frames) {
        ipr::Image root = ipr::mapped(greenOf(frame), [](float v) { return std::sqrt(std::max(v, 0.0f)); });
        features.push_back(PanoramaFeatures::detect(root));
        grey.push_back(std::move(root));
    }
    std::vector<Pair> pairs;
    for (int a = 0; a < int(frames.size()); ++a) {
        for (int b = a + 1; b < int(frames.size()); ++b) {
            Pair pair;
            pair.a = a;
            pair.b = b;
            if (verifyPair(PanoramaFeatures::match(features[size_t(a)], features[size_t(b)]), frameSize, {}, pair))
                pairs.push_back(pair);
        }
    }
    Solution solution = solve(int(frames.size()), pairs, frameSize, lens);
    refine(solution, grey, 1.0, frameSize, lens);
    return solution;
}

void geometryRecoversTheTruth()
{
    // Two rows, given out of order, with a tilted camera and a little roll.
    const std::vector<ipr::Matrix33d> truth = {
        camera(-20 * degree, 4 * degree, 0.004), camera(12 * degree, -14 * degree, -0.003), camera(0 * degree, 4 * degree, 0.002),
        camera(-20 * degree, -14 * degree, 0.001), camera(20 * degree, 4 * degree, -0.002), camera(-8 * degree, -14 * degree, 0.003)};
    std::vector<ipr::Image> frames;
    for (const ipr::Matrix33d &r : truth)
        frames.push_back(render(sharedWorld(), r));
    const Solution solution = solveSeries(frames);
    if (!check(solution.valid && solution.members.size() == truth.size(), "every frame of two rows joins"))
        return;
    check(std::abs(solution.focal - trueFocal) < 0.02 * trueFocal, "the focal length is found");
    check(solution.residual < 0.5, "the overlaps agree within half a pixel");
    double worst = 0.0;
    for (size_t a = 0; a < truth.size(); ++a) {
        for (size_t b = a + 1; b < truth.size(); ++b) {
            const ipr::Matrix33d found = solution.worldToCamera[b] * solution.worldToCamera[a].t();
            const ipr::Matrix33d known = truth[b] * truth[a].t();
            const ipr::Matrix33d gap = found * known.t();
            const double angle = std::acos(std::clamp((gap(0, 0) + gap(1, 1) + gap(2, 2) - 1.0) * 0.5, -1.0, 1.0));
            worst = std::max(worst, angle);
        }
    }
    check(worst < 0.05 * degree, "the relative rotations are found within 0.05 degree");
}

void aLensIsFoundWithTheFocal()
{
    constexpr double bend = 0.012; // a pixel in the corner moves out by more than 1 %
    std::vector<ipr::Image> frames;
    for (const double yaw : {-30.0, -15.0, 0.0, 15.0, 30.0})
        frames.push_back(render(sharedWorld(), camera(yaw * degree, -3 * degree), 1.0, bend));
    const Solution solution = solveSeries(frames);
    if (!check(solution.valid, "a bent series joins"))
        return;
    check(std::abs(solution.distortion - bend) < 0.002, "the distortion is found");
    check(std::abs(solution.focal - trueFocal) < 0.03 * trueFocal, "the focal length is not misled by it");
    check(solution.residual < 0.5, "a distorted series agrees within half a pixel");

    // What the files know stays: the adjustment cannot trade it for a smaller residual.
    LensKnowledge known;
    known.focalPixels = trueFocal * 1.05;
    known.distortionCorrected = true;
    const Solution held = solveSeries(frames, known);
    if (check(held.valid, "a series with a known lens joins")) {
        check(held.distortion == 0.0, "a corrected lens has no distortion left to find");
        check(std::abs(held.focal / known.focalPixels - 1.0) <= 0.0701, "a known focal length stays close to what the file says");
    }
}

void shapesAreRecognizedAndLevelOnTheBottom()
{
    {
        // A sweep tilted upward: the bottom of the view must come out level.
        std::vector<ipr::Image> frames;
        for (const double yaw : {-48.0, -24.0, 0.0, 24.0, 48.0})
            frames.push_back(render(sharedWorld(), camera(yaw * degree, -9 * degree, yaw * 0.0002)));
        Solution solution = solveSeries(frames);
        if (check(solution.valid, "a row joins")) {
            const Shape shape = straighten(solution, frameSize, Projection::Cylindrical);
            check(shape.layout == Layout::Horizontal && shape.rows == 1, "a row is horizontal, with one row");
            Surface surface;
            surface.projection = shape.projection;
            surface.layout = shape.layout;
            surface.scale = solution.focal;
            check(footDrift(solution, surface) < 1.5, "the bottom of a row runs level");
        }
    }
    {
        std::vector<ipr::Image> frames;
        for (const double pitch : {-36.0, -18.0, 0.0, 18.0})
            frames.push_back(render(sharedWorld(), camera(5 * degree, pitch * degree)));
        Solution solution = solveSeries(frames);
        if (check(solution.valid, "a column joins")) {
            const Shape shape = straighten(solution, frameSize, Projection::Automatic);
            check(shape.layout == Layout::Vertical && shape.rows == 1, "a column is vertical");
            // The lowest frame stands level: its X axis has no vertical part.
            int foot = solution.members.front();
            for (const int m : solution.members)
                if (solution.worldToCamera[size_t(m)](2, 1) > solution.worldToCamera[size_t(foot)](2, 1))
                    foot = m;
            check(foot == 3, "the lowest frame of a column is found");
            check(std::abs(solution.worldToCamera[size_t(foot)](0, 2)) < 1e-6, "the foot of a column stands square");
        }
    }
    {
        std::vector<ipr::Image> frames;
        for (const double pitch : {-12.0, 8.0})
            for (const double yaw : {-22.0, 0.0, 22.0})
                frames.push_back(render(sharedWorld(), camera(yaw * degree, pitch * degree)));
        Solution solution = solveSeries(frames);
        if (check(solution.valid, "two rows join")) {
            const Shape shape = straighten(solution, frameSize, Projection::Automatic);
            check(shape.layout == Layout::Horizontal && shape.rows == 2, "two rows are told apart");
        }
    }
}

void aPairMustReallyOverlap()
{
    const ipr::Image a = render(sharedWorld(), camera(0, 0));
    const ipr::Image unrelated = textured(frameSize, 99);
    const ipr::Image channel = greenOf(a);
    const std::vector<PanoramaFeatures::Feature> fa = PanoramaFeatures::detect(channel);
    const std::vector<PanoramaFeatures::Feature> fb = PanoramaFeatures::detect(greenOf(unrelated));
    check(!fa.empty() && !fb.empty(), "corners are found on a textured image");
    Pair pair;
    check(!verifyPair(PanoramaFeatures::match(fa, fb), frameSize, {}, pair), "two unrelated views do not overlap");
    check(PanoramaFeatures::detect(ipr::filled(frameSize, ipr::Depth::F32, 1, 0.5)).empty(), "a flat image has no corners");
    check(PanoramaFeatures::detect(ipr::Image()).empty(), "an empty image has no corners");
    ipr::Image undefined = channel.clone();
    paint(undefined, ipr::Recti(100, 100, 200, 200), std::numeric_limits<double>::quiet_NaN());
    check(!PanoramaFeatures::detect(undefined).empty(), "undefined pixels do not stop the search");
}

void seamsFollowTheSmallestDifference()
{
    // Two views that agree only on a strip: the cut must run through it.
    const ipr::Size canvas(200, 60);
    std::vector<PanoramaBlend::View> views(2);
    PanoramaBlend::View &left = views[0], &right = views[1];
    left.area = ipr::Recti(0, 0, 130, 60);
    right.area = ipr::Recti(70, 0, 130, 60);
    for (PanoramaBlend::View &view : views) {
        view.whole = ipr::filled(view.area.size(), ipr::Depth::U8, 1, 255);
        view.any = view.whole.clone();
    }
    left.colour = ipr::filled(left.area.size(), ipr::Depth::F32, 3, 0.2);
    right.colour = ipr::filled(right.area.size(), ipr::Depth::F32, 3, 0.6);
    const int strip = 112;
    for (int y = 0; y < 60; ++y) {
        for (int x = strip - 3; x <= strip + 3; ++x) {
            imagetest::at<ipr::Vec3f>(left.colour, y, x) = ipr::Vec3f(0.4f, 0.4f, 0.4f);
            imagetest::at<ipr::Vec3f>(right.colour, y, x - 70) = ipr::Vec3f(0.4f, 0.4f, 0.4f);
        }
    }
    const ipr::Image labels = PanoramaBlend::seams(views, canvas);
    if (!check(!labels.empty() && labels.depth() == ipr::Depth::S16 && labels.channels() == 1, "the seams give a map"))
        return;
    int worst = 0;
    for (int y = 0; y < 60; ++y) {
        int change = -1;
        for (int x = 1; x < 200; ++x)
            if (imagetest::at<short>(labels, y, x) != imagetest::at<short>(labels, y, x - 1))
                change = x;
        worst = std::max(worst, std::abs(change - strip));
    }
    check(worst <= 4, "the cut runs through the strip where the views agree");
    check(imagetest::at<short>(labels, 30, 5) == 0 && imagetest::at<short>(labels, 30, 195) == 1, "each view keeps what only it covers");
    check(PanoramaBlend::seams(views, canvas, [] { return true; }).empty(), "cancelled, the seams stop");
}

void gainsLevelTheFrames()
{
    std::vector<PanoramaBlend::View> views(2);
    views[0].area = ipr::Recti(0, 0, 100, 50);
    views[1].area = ipr::Recti(60, 0, 100, 50);
    views[0].colour = imagetest::filledWith({100, 50}, {0.2, 0.3, 0.4});
    views[1].colour = imagetest::filledWith({100, 50}, {0.2 * 1.25, 0.3 * 1.25, 0.4 * 0.8});
    views[0].whole = ipr::filled({100, 50}, ipr::Depth::U8, 1, 255);
    views[1].whole = views[0].whole.clone();
    PanoramaBlend::levelGains(views);
    for (int c = 0; c < 3; ++c) {
        const double levelledA = imagetest::at<ipr::Vec3f>(std::as_const(views[0].colour), 0, 0)[c] * views[0].gains[c];
        const double levelledB = imagetest::at<ipr::Vec3f>(std::as_const(views[1].colour), 0, 0)[c] * views[1].gains[c];
        check(std::abs(levelledA / levelledB - 1.0) < 0.03, "two frames come to one level");
        check(std::abs(views[0].gains[c] * views[1].gains[c] - 1.0) < 0.03, "the difference is shared out");
    }
}

ipr::Image emptyMask(int side)
{
    return ipr::Image::zeros({side, side}, ipr::Depth::U8, 1);
}

void lowRectangleStaysOnTheBottom()
{
    ipr::Image mask = emptyMask(100);
    paint(mask, ipr::Recti(10, 10, 80, 30), 255);
    check(PanoramaBlend::lowRectangle(mask) == ipr::Recti(10, 10, 80, 30), "a plain rectangle is found exactly");
    check(PanoramaBlend::lowRectangle(emptyMask(10)).empty(), "an empty mask has no rectangle");
    check(PanoramaBlend::lowRectangle(ipr::Image()).empty(), "no mask has no rectangle");

    // The top is a little wider: the largest area is high, but the crop stays low when the
    // loss is small.
    ipr::Image stepped = emptyMask(100);
    paint(stepped, ipr::Recti(0, 0, 100, 60), 255);
    paint(stepped, ipr::Recti(0, 60, 95, 40), 255);
    const ipr::Recti low = PanoramaBlend::lowRectangle(stepped);
    check(low.y + low.height == 100, "the crop keeps the bottom of the view");
    check(low.width == 95, "and the largest rectangle standing there");

    // A long way down, the bottom is too narrow to be worth it.
    ipr::Image narrow = emptyMask(100);
    paint(narrow, ipr::Recti(0, 0, 100, 80), 255);
    paint(narrow, ipr::Recti(45, 80, 10, 20), 255);
    check(PanoramaBlend::lowRectangle(narrow) == ipr::Recti(0, 0, 100, 80), "a useless foot does not cost the image");
}

void blendKeepsADetailAndHidesASlowStep()
{
    // One layer alone gives its pixels back exactly.
    const ipr::Size reduced(20, 10);
    const int cell = 4;
    const ipr::Recti band(0, 0, 80, 40);
    const ipr::Image pixels = textured(band.size(), 5);
    PanoramaBlend::View view;
    view.area = ipr::Recti(ipr::Point2i(0, 0), reduced);
    view.colour = ipr::resized(pixels, reduced, ipr::Interpolation::Area);
    view.whole = ipr::filled(reduced, ipr::Depth::U8, 1, 255);
    view.any = view.whole.clone();
    const ipr::Image labels = ipr::Image::zeros(reduced, ipr::Depth::S16, 1);
    const PanoramaBlend::Layer alone = PanoramaBlend::prepareLayer(view, labels, 0, 1.0, 3.0);
    std::vector<PanoramaBlend::Pixels> one(1);
    one[0].layer = &alone;
    one[0].area = band;
    one[0].colour = pixels.share();
    one[0].valid = ipr::filled(band.size(), ipr::Depth::U8, 1, 255);
    ipr::Image out;
    PanoramaBlend::blendBand(one, cell, band, out);
    check(ipr::largestDifference(out, pixels) < 1e-5, "a single frame comes out untouched");

    // Nothing covers a pixel: it stays empty rather than undefined.
    one[0].valid = ipr::Image::zeros(band.size(), ipr::Depth::U8, 1);
    PanoramaBlend::blendBand(one, cell, band, out);
    check(imagetest::largestAbsolute(out) == 0.0, "an uncovered pixel is empty");

    // Two flat frames with a difference of light: the step spreads out over the transition.
    const ipr::Size wide(800, 40);
    const int wideCell = 4;
    const ipr::Size wideReduced(200, 10);
    ipr::Image split = ipr::Image::zeros(wideReduced, ipr::Depth::S16, 1);
    paint(split, ipr::Recti(100, 0, 100, 10), 1);
    PanoramaBlend::View left, right;
    left.area = right.area = ipr::Recti(ipr::Point2i(0, 0), wideReduced);
    left.colour = ipr::filled(wideReduced, ipr::Depth::F32, 3, 0.3);
    right.colour = ipr::filled(wideReduced, ipr::Depth::F32, 3, 0.36);
    for (PanoramaBlend::View *side : {&left, &right}) {
        side->whole = ipr::filled(wideReduced, ipr::Depth::U8, 1, 255);
        side->any = side->whole.clone();
    }
    const PanoramaBlend::Layer leftLayer = PanoramaBlend::prepareLayer(left, split, 0, 1.0, 20.0);
    const PanoramaBlend::Layer rightLayer = PanoramaBlend::prepareLayer(right, split, 1, 1.0, 20.0);
    std::vector<PanoramaBlend::Pixels> two(2);
    two[0].layer = &leftLayer;
    two[1].layer = &rightLayer;
    two[0].colour = ipr::filled(wide, ipr::Depth::F32, 3, 0.3);
    two[1].colour = ipr::filled(wide, ipr::Depth::F32, 3, 0.36);
    for (PanoramaBlend::Pixels &entry : two) {
        entry.area = ipr::Recti(ipr::Point2i(0, 0), wide);
        entry.valid = ipr::filled(wide, ipr::Depth::U8, 1, 255);
    }
    PanoramaBlend::blendBand(two, wideCell, ipr::Recti(ipr::Point2i(0, 0), wide), out);
    const ipr::Image &blended = out;
    double largestStep = 0.0;
    for (int x = 1; x < wide.width; ++x)
        largestStep = std::max(largestStep, double(std::abs(imagetest::at<ipr::Vec3f>(blended, 20, x)[1] - imagetest::at<ipr::Vec3f>(blended, 20, x - 1)[1])));
    check(largestStep < 0.002, "a difference of light leaves no step");
    check(std::abs(imagetest::at<ipr::Vec3f>(blended, 20, 2)[1] - 0.3f) < 1e-3 && std::abs(imagetest::at<ipr::Vec3f>(blended, 20, 797)[1] - 0.36f) < 1e-3,
          "each end keeps its own level");
}

// What is left in a folder the job was told to use.
int entriesIn(const QString &path)
{
    return int(QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
}

// The image with its hidden pixels at zero, as a canvas leaves them.
ipr::Image hiddenAtZero(const ipr::Image &image, const ipr::Image &hidden)
{
    ipr::Image out = image.clone();
    ipr::fillWhere(out, 0, hidden);
    return out;
}

void fillInventsOnlyWhatIsMissing()
{
    const ipr::Size size(600, 300);
    // A smooth sky with a little grain, and a corner hidden.
    ipr::Image sky = ipr::Image::create(size, ipr::Depth::F32, 3);
    std::mt19937 random(31);
    std::normal_distribution<double> skyGrain(0.0, 0.004);
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            const float level = 0.5f + 0.25f * float(y) / float(size.height) + 0.1f * float(x) / float(size.width);
            const float grain = float(skyGrain(random));
            imagetest::at<ipr::Vec3f>(sky, y, x) = ipr::Vec3f(level * 0.8f + grain, level + grain, level * 0.6f + grain);
        }
    }
    ipr::Image known = ipr::filled(size, ipr::Depth::U8, 1, 255);
    for (int y = 0; y < 90; ++y)
        paint(known, ipr::Recti(0, y, 180 - 2 * y, 1), 0);
    const ipr::Image hidden = ipr::bitwiseNot(known);
    const double hiddenCount = double(ipr::countNonZero(hidden));

    ipr::Image filled = hiddenAtZero(sky, hidden);
    const PanoramaFill::Report report = PanoramaFill::fill(filled, known);
    check(report.outcome == PanoramaFill::Outcome::Filled, "a corner is filled");
    check(std::abs(report.filledShare - hiddenCount / double(size.area())) < 1e-9, "and its share is told");
    check(imagetest::largestDifferenceWhere(filled, sky, known) == 0.0, "a real pixel is never changed");
    check(meanWhere(greenOf(imagetest::absoluteDifference(filled, sky)), hidden) < 0.03, "a smooth sky is continued at its own level");
    const ipr::Image filledGreen = greenOf(filled);
    long long nonEmpty = 0;
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x)
            nonEmpty += imagetest::at<std::uint8_t>(hidden, y, x) && imagetest::at<float>(filledGreen, y, x) > 0.0f;
    check(nonEmpty == ipr::countNonZero(hidden), "no pixel of the corner stays empty");

    // Twice the same.
    ipr::Image again = hiddenAtZero(sky, hidden);
    PanoramaFill::fill(again, known);
    check(ipr::largestDifference(again, filled) == 0.0, "a second fill gives the same pixels");

    // Reduced below whole resolution, the grain comes back from the real pixels.
    PanoramaFill::Options smaller;
    smaller.workingSide = 200;
    ipr::Image reduced = hiddenAtZero(sky, hidden);
    const PanoramaFill::Report reducedReport = PanoramaFill::fill(reduced, known, smaller);
    check(reducedReport.outcome == PanoramaFill::Outcome::Filled, "a fill works on a reduced image");
    const auto grainOf = [&](const ipr::Image &image) {
        const ipr::Image green = greenOf(image);
        return meanWhere(imagetest::absoluteDifference(green, ipr::gaussianBlurred(green, 3.0)), hidden);
    };
    const double truth = grainOf(sky), kept = grainOf(reduced);
    check(kept > 0.5 * truth && kept < 1.5 * truth, "the grain comes back at whole resolution");
    check(imagetest::largestDifferenceWhere(reduced, sky, known) == 0.0, "a real pixel stays real there too");

    // A strong structure stays one: not a grey average of what the patches propose.
    ipr::Image stripes = ipr::Image::create(size, ipr::Depth::F32, 3);
    for (int y = 0; y < size.height; ++y)
        paint(stripes, ipr::Recti(0, y, size.width, 1), y / 20 % 2 ? 0.8 : 0.2);
    ipr::Image striped = hiddenAtZero(stripes, hidden);
    PanoramaFill::fill(striped, known);
    const ipr::Image stripeContrast = ipr::mapped(greenOf(striped), [](float v) { return std::abs(v - 0.5f); });
    check(meanWhere(stripeContrast, hidden) > 0.2, "a strong structure keeps its contrast");

    // A bright object next to the hole, in a plain texture: it is cut by the limit, as the
    // house of a real series. It must not be reflected across the limit, nor found again.
    {
        const ipr::Size field(1000, 500);
        ipr::Image scene = ipr::Image::create(field, ipr::Depth::F32, 3);
        std::mt19937 grainy(77);
        std::normal_distribution<double> sceneGrain(0.0, 0.012);
        for (int y = 0; y < field.height; ++y)
            for (int x = 0; x < field.width; ++x) {
                const float value = 0.09f + float(sceneGrain(grainy));
                imagetest::at<ipr::Vec3f>(scene, y, x) = ipr::Vec3f(value, value, value);
            }
        const int limit = 380;
        const ipr::Recti object(400, limit - 30, 90, 60); // astride the limit: half of it is real
        paint(scene, object, 0.8);
        ipr::Image seen = ipr::filled(field, ipr::Depth::U8, 1, 255);
        paint(seen, ipr::Recti(0, limit, field.width, field.height - limit), 0);
        ipr::Image filledScene = hiddenAtZero(scene, ipr::bitwiseNot(seen));
        const ipr::Image real = filledScene.clone();
        check(PanoramaFill::fill(filledScene, seen).outcome == PanoramaFill::Outcome::Filled, "a scene with an object is filled");
        const ipr::Image green = greenOf(filledScene);
        // Below the limit, right under the object: what a reflection would make bright.
        const ipr::Image reflection = green.copy(ipr::Recti(object.x + 10, limit + 4, object.width - 20, 20));
        check(ipr::meanOf(reflection)[0] < 0.3, "an object cut by the limit is not reflected across it");
        // Anywhere else in what was invented: another copy of it.
        const ipr::Image elsewhere = green.copy(ipr::Recti(0, limit + 40, field.width, field.height - limit - 40));
        check(ipr::extremesOf(elsewhere).largest < 0.4, "an object is not found a second time");
        const ipr::Recti realArea(0, 0, field.width, limit);
        check(ipr::largestDifference(filledScene.copy(realArea), real.copy(realArea)) == 0.0, "and the object itself stays as it was");
    }

    // Nothing to do, too little to do it with, wrong inputs, and a cancel.
    ipr::Image whole = sky.clone();
    const PanoramaFill::Report nothing = PanoramaFill::fill(whole, ipr::filled(size, ipr::Depth::U8, 1, 255));
    check(nothing.outcome == PanoramaFill::Outcome::NothingToFill && nothing.filledShare == 0.0, "a full image needs no fill");
    check(ipr::largestDifference(whole, sky) == 0.0, "and stays as it is");
    ipr::Image littleKnown = ipr::Image::zeros(size, ipr::Depth::U8, 1);
    paint(littleKnown, ipr::Recti(0, 0, 40, 40), 255);
    ipr::Image attempt = sky.clone();
    check(PanoramaFill::fill(attempt, littleKnown).outcome == PanoramaFill::Outcome::TooLittleKnown, "too little known is refused");
    check(PanoramaFill::fill(attempt, ipr::Image()).outcome == PanoramaFill::Outcome::NothingToFill, "without a mask nothing happens");
    ipr::Image stopped = sky.clone();
    check(PanoramaFill::fill(stopped, known, {}, [] { return true; }).outcome == PanoramaFill::Outcome::Cancelled, "a cancel stops it");

    // Through the job: the whole canvas, and what is invented is said.
    QTemporaryDir folder;
    std::vector<ipr::Image> frames;
    for (const double yaw : {-24.0, 0.0, 24.0})
        frames.push_back(render(sharedWorld(), camera(yaw * degree, -5 * degree)));
    PanoramaJob::Request request = requestFor(frames);
    request.scratchRoot = folder.path();
    request.edges = PanoramaJob::Edges::Fill;
    const PanoramaJob::Result result = PanoramaJob::run(request);
    if (check(result.success, "the job fills its edges")) {
        check(result.cameraRgb.size() == result.canvas, "over the whole canvas");
        check(result.filledShare > 0.0 && std::abs(result.filledShare - (1.0 - result.coveredShare)) < 0.03, "and says what it invented");
        check(ipr::countNonZero(greenOf(result.cameraRgb)) == result.canvas.area(), "no pixel stays empty");
    }
    check(entriesIn(folder.path()) == 0, "and nothing stays in the scratch folder");
}

void jobMakesAPanorama()
{
    QTemporaryDir folder;
    std::vector<ipr::Image> frames;
    // In any order, with a difference of exposure, and a stranger in the series.
    for (const double yaw : {24.0, -24.0, 0.0})
        frames.push_back(render(sharedWorld(), camera(yaw * degree, -5 * degree), yaw > 0.0 ? 1.15 : 1.0));
    frames.push_back(textured(frameSize, 123));
    PanoramaJob::Request request = requestFor(frames);
    request.scratchRoot = folder.path();
    int lastDone = 0, lastTotal = 0;
    request.progress = [&](int done, int total) {
        lastDone = done;
        lastTotal = total;
    };
    quint64 reserved = 0;
    request.reserveCanvas = [&](quint64 bytes) {
        reserved = bytes;
        return QString();
    };
    const PanoramaJob::Result result = PanoramaJob::run(request);
    if (!check(result.success, "the job makes a panorama")) {
        std::cerr << result.error.toStdString() << '\n';
        return;
    }
    check(result.usedFrames == 3, "three frames join");
    check(!result.frames[3].used && !result.frames[3].exclusion.isEmpty(), "the stranger is left out, with its reason");
    check(result.shape.layout == Layout::Horizontal && result.shape.rows == 1, "a row is recognized");
    check(result.cameraRgb.depth() == ipr::Depth::F32 && result.cameraRgb.channels() == 3 && result.cameraRgb.size() == result.kept.size(),
          "the result is the kept area");
    check(result.kept.width > frameSize.width * 17 / 10 && result.kept.height > frameSize.height * 9 / 10, "the panorama widens the field");
    check(reserved == quint64(result.canvas.area()) * 12u, "the canvas is announced before it is written");
    check(lastTotal > 0 && lastDone == lastTotal, "the progress reaches its end");
    check(std::abs(result.frames[0].gains[1] / result.frames[1].gains[1] - 1.0 / 1.15) < 0.03, "the brighter frame is brought down");
    check(entriesIn(folder.path()) == 0, "nothing is left in the scratch folder");

    // No hole in a cropped panorama: an uncovered pixel would be exactly 0.
    check(ipr::countNonZero(greenOf(result.cameraRgb)) == result.kept.area(), "a crop holds no empty pixel");

    const PanoramaJob::Result again = PanoramaJob::run(request);
    check(again.success && again.cameraRgb.size() == result.cameraRgb.size() && ipr::largestDifference(again.cameraRgb, result.cameraRgb) == 0.0,
          "the same series gives the same pixels");

    PanoramaJob::Request uncropped = requestFor(frames);
    uncropped.scratchRoot = folder.path();
    uncropped.edges = PanoramaJob::Edges::Keep;
    const PanoramaJob::Result whole = PanoramaJob::run(uncropped);
    check(whole.success && whole.cameraRgb.size() == whole.canvas, "uncropped, the whole canvas comes out");
    check(whole.coveredShare > 0.5 && whole.coveredShare < 1.0, "and says how much of it is covered");
}

void framesThatNearlyCoincideStayCheap()
{
    // A burst chosen by mistake: everything overlaps, and every place costs about the same.
    QTemporaryDir folder;
    std::vector<ipr::Image> frames;
    for (int i = 0; i < 6; ++i)
        frames.push_back(render(sharedWorld(), camera((i - 3) * 0.05 * degree, (i - 3) * -0.04 * degree)));
    PanoramaJob::Request request = requestFor(frames);
    request.scratchRoot = folder.path();
    QElapsedTimer watch;
    watch.start();
    const PanoramaJob::Result result = PanoramaJob::run(request);
    check(result.success && result.usedFrames == 6, "frames that nearly coincide still join");
    std::cout << "nearly coinciding frames: " << watch.elapsed() << " ms\n";
    check(watch.elapsed() < 30000, "and stay cheap");
}

void jobFailsWithItsReason()
{
    QTemporaryDir folder;
    {
        PanoramaJob::Request request;
        request.scratchRoot = folder.path();
        request.frameCount = 1;
        request.frame = [](int) { return PanoramaJob::Frame{}; };
        const PanoramaJob::Result result = PanoramaJob::run(request);
        check(!result.success && !result.error.isEmpty(), "one frame is not a panorama");
        request.frameCount = 3;
        request.frame = nullptr;
        check(!PanoramaJob::run(request).success, "no provider gives no panorama");
    }
    {
        // Frames that cannot be decoded say so, and nothing is left behind.
        PanoramaJob::Request request;
        request.scratchRoot = folder.path();
        request.frameCount = 3;
        request.frame = [](int) {
            PanoramaJob::Frame frame;
            frame.error = QStringLiteral("illisible");
            return frame;
        };
        const PanoramaJob::Result result = PanoramaJob::run(request);
        check(!result.success && !result.error.isEmpty(), "unreadable frames give no panorama");
        check(result.frames[1].exclusion == QStringLiteral("illisible"), "each says why");
    }
    {
        // Nothing in common.
        const std::vector<ipr::Image> frames = framesOf(textured(frameSize, 21), textured(frameSize, 22), textured(frameSize, 23));
        PanoramaJob::Request request = requestFor(frames);
        request.scratchRoot = folder.path();
        const PanoramaJob::Result result = PanoramaJob::run(request);
        check(!result.success && result.error.contains(QStringLiteral("recouvrent")), "unrelated frames fail with the reason");
    }
    {
        // Flat frames.
        std::vector<ipr::Image> frames;
        for (int i = 0; i < 3; ++i)
            frames.push_back(ipr::filled(frameSize, ipr::Depth::F32, 3, 0.4));
        PanoramaJob::Request request = requestFor(frames);
        request.scratchRoot = folder.path();
        check(!PanoramaJob::run(request).success, "flat frames give no panorama");
    }
    {
        // A frame of another size is left out, not stretched.
        const std::vector<ipr::Image> frames = framesOf(render(sharedWorld(), camera(-14 * degree, 0)), render(sharedWorld(), camera(14 * degree, 0)),
                                                        ipr::filled({300, 300}, ipr::Depth::F32, 3, 0.3));
        PanoramaJob::Request request = requestFor(frames);
        request.scratchRoot = folder.path();
        const PanoramaJob::Result result = PanoramaJob::run(request);
        check(result.success && !result.frames[2].used && result.frames[2].exclusion.contains(QStringLiteral("Dimensions")), "another size is left out");
    }
    {
        // Another focal length too.
        const std::vector<ipr::Image> frames = framesOf(render(sharedWorld(), camera(-14 * degree, 0)), render(sharedWorld(), camera(14 * degree, 0)),
                                                        render(sharedWorld(), camera(0, 0)));
        PanoramaJob::Request request;
        request.scratchRoot = folder.path();
        request.frameCount = 3;
        request.frame = [&frames](int index) {
            PanoramaJob::Frame frame;
            frame.cameraRgb = frames[size_t(index)].clone();
            frame.focalMm = index == 2 ? 200.0 : 70.0;
            return frame;
        };
        const PanoramaJob::Result result = PanoramaJob::run(request);
        check(result.success && result.frames[2].exclusion.contains(QStringLiteral("Focale")), "another focal length is left out");
    }
    {
        // The caller can refuse the canvas, and a limit stops a canvas too large.
        const std::vector<ipr::Image> frames = framesOf(render(sharedWorld(), camera(-14 * degree, 0)), render(sharedWorld(), camera(14 * degree, 0)));
        PanoramaJob::Request request = requestFor(frames);
        request.scratchRoot = folder.path();
        request.reserveCanvas = [](quint64) { return QStringLiteral("pas de place"); };
        PanoramaJob::Result result = PanoramaJob::run(request);
        check(!result.success && result.error == QStringLiteral("pas de place"), "a refused canvas stops the job");
        request.reserveCanvas = nullptr;
        request.maximumMegapixels = 0.1;
        result = PanoramaJob::run(request);
        check(!result.success && result.error.contains(QStringLiteral("trop grand")), "a canvas above the limit stops the job");
        request.maximumMegapixels = 400.0;
        request.minimumFreeDiskBytes = quint64(1) << 60;
        result = PanoramaJob::run(request);
        check(!result.success && result.error.contains(QStringLiteral("disque")), "too little disk stops the job");
    }
    check(entriesIn(folder.path()) == 0, "a failure leaves nothing in the scratch folder");
}

void jobStopsWhenCancelled()
{
    QTemporaryDir folder;
    const std::vector<ipr::Image> frames = framesOf(render(sharedWorld(), camera(-14 * degree, 0)), render(sharedWorld(), camera(14 * degree, 0)));
    // At every point the job asks, in turn.
    int asked = 0;
    {
        PanoramaJob::Request request = requestFor(frames);
        request.scratchRoot = folder.path();
        request.cancelled = [&] {
            ++asked;
            return false;
        };
        check(PanoramaJob::run(request).success, "a job that is not cancelled finishes");
    }
    const int questions = asked;
    check(questions > 3, "the job keeps asking whether it is cancelled");
    for (int at : {1, 2, questions / 4, questions / 2, questions - 1}) {
        int count = 0;
        PanoramaJob::Request request = requestFor(frames);
        request.scratchRoot = folder.path();
        request.cancelled = [&] { return ++count >= at; };
        const PanoramaJob::Result result = PanoramaJob::run(request);
        check(result.cancelled && !result.success && result.cameraRgb.empty(), "a cancelled job stops without a result");
        check(entriesIn(folder.path()) == 0, "a cancelled job leaves nothing behind");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    geometryRecoversTheTruth();
    aLensIsFoundWithTheFocal();
    shapesAreRecognizedAndLevelOnTheBottom();
    aPairMustReallyOverlap();
    seamsFollowTheSmallestDifference();
    gainsLevelTheFrames();
    lowRectangleStaysOnTheBottom();
    blendKeepsADetailAndHidesASlowStep();
    jobMakesAPanorama();
    fillInventsOnlyWhatIsMissing();
    framesThatNearlyCoincideStayCheap();
    jobFailsWithItsReason();
    jobStopsWhenCancelled();
    if (failures == 0)
        std::cout << "panorama: ok\n";
    return failures == 0 ? 0 : 1;
}
