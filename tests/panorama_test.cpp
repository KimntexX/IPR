// The panorama engine on synthetic series whose geometry is known: frames rendered from one
// viewpoint on a textured world, so that what the engine finds can be compared to the truth.
#include "PanoramaBlend.h"
#include "PanoramaFeatures.h"
#include "PanoramaFill.h"
#include "PanoramaGeometry.h"
#include "PanoramaJob.h"
#include "OpenCvBridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
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

constexpr double degree = CV_PI / 180.0;
const ipr::Size frameSize(900, 600);
constexpr double trueFocal = 1300.0;

cv::Mat textured(const ipr::Size &size, int seed)
{
    cv::RNG random{uint64_t(seed)};
    cv::Mat image(ipr::toCv(size), CV_32FC3, cv::Scalar::all(0.25));
    for (const double sigma : {1.2, 3.0, 8.0, 24.0}) {
        cv::Mat layer(ipr::toCv(size), CV_32FC1);
        random.fill(layer, cv::RNG::NORMAL, cv::Scalar(0), cv::Scalar(1));
        cv::GaussianBlur(layer, layer, cv::Size(), sigma);
        cv::Scalar mean, deviation;
        cv::meanStdDev(layer, mean, deviation);
        layer *= 0.05 / std::max(1e-6, deviation[0]);
        const cv::Mat parts[3] = {layer, layer, layer};
        cv::Mat colour;
        cv::merge(parts, 3, colour);
        image += colour;
    }
    cv::max(image, 0.005, image);
    return image;
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

// The frame such a camera sees on a world mapped by yaw and pitch.
cv::Mat render(const cv::Mat &world, const ipr::Matrix33d &worldToCamera, double gain = 1.0, double distortion = 0.0)
{
    const double inverseCorner = 4.0 / (double(frameSize.width) * frameSize.width + double(frameSize.height) * frameSize.height);
    const double perRadian = world.cols / (160.0 * degree);
    cv::Mat mapX(ipr::toCv(frameSize), CV_32FC1), mapY(ipr::toCv(frameSize), CV_32FC1);
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
            mapX.at<float>(y, x) = float(std::atan2(ray[0], ray[2]) * perRadian + world.cols * 0.5);
            mapY.at<float>(y, x) = float(std::atan2(ray[1], std::hypot(ray[0], ray[2])) * perRadian + world.rows * 0.5);
        }
    }
    cv::Mat frame;
    cv::remap(world, frame, mapX, mapY, cv::INTER_CUBIC, cv::BORDER_REFLECT);
    return frame * gain;
}

const cv::Mat &sharedWorld()
{
    static const cv::Mat world = textured(ipr::Size(7000, 4000), 11);
    return world;
}

PanoramaJob::Request requestFor(const std::vector<cv::Mat> &frames)
{
    PanoramaJob::Request request;
    request.frameCount = int(frames.size());
    request.frame = [&frames](int index) {
        PanoramaJob::Frame frame;
        frame.cameraRgb = frames[size_t(index)];
        return frame;
    };
    return request;
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

Solution solveSeries(const std::vector<cv::Mat> &frames, const LensKnowledge &lens = {})
{
    std::vector<std::vector<PanoramaFeatures::Feature>> features;
    std::vector<cv::Mat> grey;
    for (const cv::Mat &frame : frames) {
        cv::Mat channels[3];
        cv::split(frame, channels);
        cv::Mat root;
        cv::sqrt(cv::max(channels[1], 0.0), root);
        grey.push_back(root);
        features.push_back(PanoramaFeatures::detect(root));
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
    std::vector<cv::Mat> frames;
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
    std::vector<cv::Mat> frames;
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
        std::vector<cv::Mat> frames;
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
        std::vector<cv::Mat> frames;
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
        std::vector<cv::Mat> frames;
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
    const cv::Mat a = render(sharedWorld(), camera(0, 0));
    const cv::Mat unrelated = textured(frameSize, 99);
    std::vector<PanoramaFeatures::Feature> fa, fb;
    cv::Mat channel;
    cv::extractChannel(a, channel, 1);
    fa = PanoramaFeatures::detect(channel);
    cv::extractChannel(unrelated, channel, 1);
    fb = PanoramaFeatures::detect(channel);
    check(!fa.empty() && !fb.empty(), "corners are found on a textured image");
    Pair pair;
    check(!verifyPair(PanoramaFeatures::match(fa, fb), frameSize, {}, pair), "two unrelated views do not overlap");
    check(PanoramaFeatures::detect(cv::Mat(ipr::toCv(frameSize), CV_32FC1, cv::Scalar(0.5))).empty(), "a flat image has no corners");
    check(PanoramaFeatures::detect(cv::Mat()).empty(), "an empty image has no corners");
    cv::Mat undefined = channel.clone();
    undefined(ipr::toCv(ipr::Recti(100, 100, 200, 200))).setTo(std::numeric_limits<float>::quiet_NaN());
    check(!PanoramaFeatures::detect(undefined).empty(), "undefined pixels do not stop the search");
}

void seamsFollowTheSmallestDifference()
{
    // Two views that agree only on a strip: the cut must run through it.
    const ipr::Size canvas(200, 60);
    PanoramaBlend::View left, right;
    left.area = ipr::Recti(0, 0, 130, 60);
    right.area = ipr::Recti(70, 0, 130, 60);
    for (PanoramaBlend::View *view : {&left, &right}) {
        view->whole = cv::Mat(ipr::toCv(view->area.size()), CV_8UC1, cv::Scalar(255));
        view->any = view->whole.clone();
    }
    left.colour = cv::Mat(ipr::toCv(left.area.size()), CV_32FC3, cv::Scalar::all(0.2));
    right.colour = cv::Mat(ipr::toCv(right.area.size()), CV_32FC3, cv::Scalar::all(0.6));
    const int strip = 112;
    for (int y = 0; y < 60; ++y) {
        for (int x = strip - 3; x <= strip + 3; ++x) {
            ipr::at<ipr::Vec3f>(left.colour, y, x) = ipr::Vec3f(0.4f, 0.4f, 0.4f);
            ipr::at<ipr::Vec3f>(right.colour, y, x - 70) = ipr::Vec3f(0.4f, 0.4f, 0.4f);
        }
    }
    const cv::Mat labels = PanoramaBlend::seams({left, right}, canvas);
    if (!check(!labels.empty() && labels.type() == CV_16SC1, "the seams give a map"))
        return;
    int worst = 0;
    for (int y = 0; y < 60; ++y) {
        int change = -1;
        for (int x = 1; x < 200; ++x)
            if (labels.at<short>(y, x) != labels.at<short>(y, x - 1))
                change = x;
        worst = std::max(worst, std::abs(change - strip));
    }
    check(worst <= 4, "the cut runs through the strip where the views agree");
    check(labels.at<short>(30, 5) == 0 && labels.at<short>(30, 195) == 1, "each view keeps what only it covers");
    check(PanoramaBlend::seams({left, right}, canvas, [] { return true; }).empty(), "cancelled, the seams stop");
}

void gainsLevelTheFrames()
{
    PanoramaBlend::View a, b;
    a.area = ipr::Recti(0, 0, 100, 50);
    b.area = ipr::Recti(60, 0, 100, 50);
    a.colour = cv::Mat(50, 100, CV_32FC3, cv::Scalar(0.2, 0.3, 0.4));
    b.colour = cv::Mat(50, 100, CV_32FC3, cv::Scalar(0.2 * 1.25, 0.3 * 1.25, 0.4 * 0.8));
    a.whole = cv::Mat(50, 100, CV_8UC1, cv::Scalar(255));
    b.whole = a.whole.clone();
    std::vector<PanoramaBlend::View> views{a, b};
    PanoramaBlend::levelGains(views);
    for (int c = 0; c < 3; ++c) {
        const double levelledA = ipr::at<ipr::Vec3f>(a.colour, 0, 0)[c] * views[0].gains[c];
        const double levelledB = ipr::at<ipr::Vec3f>(b.colour, 0, 0)[c] * views[1].gains[c];
        check(std::abs(levelledA / levelledB - 1.0) < 0.03, "two frames come to one level");
        check(std::abs(views[0].gains[c] * views[1].gains[c] - 1.0) < 0.03, "the difference is shared out");
    }
}

void lowRectangleStaysOnTheBottom()
{
    cv::Mat mask = cv::Mat::zeros(100, 100, CV_8UC1);
    mask(ipr::toCv(ipr::Recti(10, 10, 80, 30))).setTo(255);
    check(PanoramaBlend::lowRectangle(mask) == ipr::Recti(10, 10, 80, 30), "a plain rectangle is found exactly");
    check(PanoramaBlend::lowRectangle(cv::Mat::zeros(10, 10, CV_8UC1)).empty(), "an empty mask has no rectangle");
    check(PanoramaBlend::lowRectangle(cv::Mat()).empty(), "no mask has no rectangle");

    // The top is a little wider: the largest area is high, but the crop stays low when the
    // loss is small.
    cv::Mat stepped = cv::Mat::zeros(100, 100, CV_8UC1);
    stepped(ipr::toCv(ipr::Recti(0, 0, 100, 60))).setTo(255);
    stepped(ipr::toCv(ipr::Recti(0, 60, 95, 40))).setTo(255);
    const ipr::Recti low = PanoramaBlend::lowRectangle(stepped);
    check(low.y + low.height == 100, "the crop keeps the bottom of the view");
    check(low.width == 95, "and the largest rectangle standing there");

    // A long way down, the bottom is too narrow to be worth it.
    cv::Mat narrow = cv::Mat::zeros(100, 100, CV_8UC1);
    narrow(ipr::toCv(ipr::Recti(0, 0, 100, 80))).setTo(255);
    narrow(ipr::toCv(ipr::Recti(45, 80, 10, 20))).setTo(255);
    check(PanoramaBlend::lowRectangle(narrow) == ipr::Recti(0, 0, 100, 80), "a useless foot does not cost the image");
}

void blendKeepsADetailAndHidesASlowStep()
{
    // One layer alone gives its pixels back exactly.
    const ipr::Size reduced(20, 10);
    const int cell = 4;
    const ipr::Recti band(0, 0, 80, 40);
    cv::Mat pixels = textured(band.size(), 5);
    cv::Mat valid(ipr::toCv(band.size()), CV_8UC1, cv::Scalar(255));
    PanoramaBlend::View view;
    view.area = ipr::Recti(ipr::Point2i(0, 0), reduced);
    cv::resize(pixels, view.colour, ipr::toCv(reduced), 0, 0, cv::INTER_AREA);
    view.whole = cv::Mat(ipr::toCv(reduced), CV_8UC1, cv::Scalar(255));
    view.any = view.whole.clone();
    const cv::Mat labels = cv::Mat::zeros(ipr::toCv(reduced), CV_16SC1);
    const PanoramaBlend::Layer alone = PanoramaBlend::prepareLayer(view, labels, 0, 1.0, 3.0);
    PanoramaBlend::Pixels entry;
    entry.layer = &alone;
    entry.area = band;
    entry.colour = pixels;
    entry.valid = valid;
    cv::Mat out;
    PanoramaBlend::blendBand({entry}, cell, band, out);
    check(cv::norm(out, pixels, cv::NORM_INF) < 1e-5, "a single frame comes out untouched");

    // Nothing covers a pixel: it stays empty rather than undefined.
    cv::Mat none = cv::Mat::zeros(ipr::toCv(band.size()), CV_8UC1);
    entry.valid = none;
    PanoramaBlend::blendBand({entry}, cell, band, out);
    check(cv::countNonZero(out.reshape(1)) == 0, "an uncovered pixel is empty");

    // Two flat frames with a difference of light: the step spreads out over the transition.
    const ipr::Size wide(800, 40);
    const int wideCell = 4;
    const ipr::Size wideReduced(200, 10);
    cv::Mat leftPixels(ipr::toCv(wide), CV_32FC3, cv::Scalar::all(0.3)), rightPixels(ipr::toCv(wide), CV_32FC3, cv::Scalar::all(0.36));
    cv::Mat allValid(ipr::toCv(wide), CV_8UC1, cv::Scalar(255));
    cv::Mat split(ipr::toCv(wideReduced), CV_16SC1, cv::Scalar(0));
    split(ipr::toCv(ipr::Recti(100, 0, 100, 10))).setTo(1);
    PanoramaBlend::View left, right;
    left.area = right.area = ipr::Recti(ipr::Point2i(0, 0), wideReduced);
    left.colour = cv::Mat(ipr::toCv(wideReduced), CV_32FC3, cv::Scalar::all(0.3));
    right.colour = cv::Mat(ipr::toCv(wideReduced), CV_32FC3, cv::Scalar::all(0.36));
    left.whole = right.whole = cv::Mat(ipr::toCv(wideReduced), CV_8UC1, cv::Scalar(255));
    left.any = right.any = left.whole;
    const PanoramaBlend::Layer leftLayer = PanoramaBlend::prepareLayer(left, split, 0, 1.0, 20.0);
    const PanoramaBlend::Layer rightLayer = PanoramaBlend::prepareLayer(right, split, 1, 1.0, 20.0);
    PanoramaBlend::Pixels a, b;
    a.layer = &leftLayer;
    b.layer = &rightLayer;
    a.area = b.area = ipr::Recti(ipr::Point2i(0, 0), wide);
    a.colour = leftPixels;
    b.colour = rightPixels;
    a.valid = b.valid = allValid;
    PanoramaBlend::blendBand({a, b}, wideCell, ipr::Recti(ipr::Point2i(0, 0), wide), out);
    double largestStep = 0.0;
    for (int x = 1; x < wide.width; ++x)
        largestStep = std::max(largestStep, double(std::abs(ipr::at<ipr::Vec3f>(out, 20, x)[1] - ipr::at<ipr::Vec3f>(out, 20, x - 1)[1])));
    check(largestStep < 0.002, "a difference of light leaves no step");
    check(std::abs(ipr::at<ipr::Vec3f>(out, 20, 2)[1] - 0.3f) < 1e-3 && std::abs(ipr::at<ipr::Vec3f>(out, 20, 797)[1] - 0.36f) < 1e-3, "each end keeps its own level");
}

// What is left in a folder the job was told to use.
int entriesIn(const QString &path)
{
    return int(QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
}

void fillInventsOnlyWhatIsMissing()
{
    const ipr::Size size(600, 300);
    // A smooth sky with a little grain, and a corner hidden.
    cv::Mat sky(ipr::toCv(size), CV_32FC3);
    cv::RNG random{uint64_t(31)};
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            const float level = 0.5f + 0.25f * float(y) / float(size.height) + 0.1f * float(x) / float(size.width);
            const float grain = float(random.gaussian(0.004));
            ipr::at<ipr::Vec3f>(sky, y, x) = ipr::Vec3f(level * 0.8f + grain, level + grain, level * 0.6f + grain);
        }
    }
    cv::Mat known(ipr::toCv(size), CV_8UC1, cv::Scalar(255));
    for (int y = 0; y < 90; ++y)
        known(ipr::toCv(ipr::Recti(0, y, 180 - 2 * y, 1))).setTo(0);
    const double hiddenCount = double(known.total()) - cv::countNonZero(known);

    cv::Mat filled = sky.clone();
    filled.setTo(cv::Scalar::all(0), known == 0);
    const PanoramaFill::Report report = PanoramaFill::fill(filled, known);
    check(report.outcome == PanoramaFill::Outcome::Filled, "a corner is filled");
    check(std::abs(report.filledShare - hiddenCount / double(known.total())) < 1e-9, "and its share is told");
    cv::Mat changes;
    cv::absdiff(filled, sky, changes);
    cv::Mat knownChanges = changes.clone();
    knownChanges.setTo(cv::Scalar::all(0), known == 0);
    check(cv::norm(knownChanges, cv::NORM_INF) == 0.0, "a real pixel is never changed");
    const cv::Scalar error = cv::mean(changes, known == 0);
    check(error[1] < 0.03, "a smooth sky is continued at its own level");
    cv::Mat channels[3];
    cv::split(filled, channels);
    cv::Mat hidden = known == 0;
    check(cv::countNonZero(channels[1] > 0.0f & hidden) == cv::countNonZero(hidden), "no pixel of the corner stays empty");

    // The same on any number of threads, and twice.
    cv::Mat again = sky.clone();
    again.setTo(cv::Scalar::all(0), known == 0);
    const int threads = cv::getNumThreads();
    cv::setNumThreads(1);
    PanoramaFill::fill(again, known);
    cv::setNumThreads(threads);
    check(cv::norm(again, filled, cv::NORM_INF) == 0.0, "the threads change no pixel");

    // Reduced below whole resolution, the grain comes back from the real pixels.
    PanoramaFill::Options smaller;
    smaller.workingSide = 200;
    cv::Mat reduced = sky.clone();
    reduced.setTo(cv::Scalar::all(0), known == 0);
    const PanoramaFill::Report reducedReport = PanoramaFill::fill(reduced, known, smaller);
    check(reducedReport.outcome == PanoramaFill::Outcome::Filled, "a fill works on a reduced image");
    const auto grainOf = [&](const cv::Mat &image) {
        cv::Mat green, smooth, fine;
        cv::extractChannel(image, green, 1);
        cv::GaussianBlur(green, smooth, cv::Size(), 3.0);
        cv::absdiff(green, smooth, fine);
        return cv::mean(fine, hidden)[0];
    };
    const double truth = grainOf(sky), kept = grainOf(reduced);
    check(kept > 0.5 * truth && kept < 1.5 * truth, "the grain comes back at whole resolution");
    cv::Mat reducedChanges;
    cv::absdiff(reduced, sky, reducedChanges);
    reducedChanges.setTo(cv::Scalar::all(0), known == 0);
    check(cv::norm(reducedChanges, cv::NORM_INF) == 0.0, "a real pixel stays real there too");

    // A strong structure stays one: not a grey average of what the patches propose.
    cv::Mat stripes(ipr::toCv(size), CV_32FC3);
    for (int y = 0; y < size.height; ++y)
        stripes.row(y).setTo(cv::Scalar::all(y / 20 % 2 ? 0.8 : 0.2));
    cv::Mat striped = stripes.clone();
    striped.setTo(cv::Scalar::all(0), known == 0);
    PanoramaFill::fill(striped, known);
    cv::Mat stripeGreen, stripeContrast;
    cv::extractChannel(striped, stripeGreen, 1);
    cv::absdiff(stripeGreen, cv::Scalar(0.5), stripeContrast);
    check(cv::mean(stripeContrast, known == 0)[0] > 0.2, "a strong structure keeps its contrast");

    // A bright object next to the hole, in a plain texture: it is cut by the limit, as the
    // house of a real series. It must not be reflected across the limit, nor found again.
    {
        const ipr::Size field(1000, 500);
        cv::Mat scene(ipr::toCv(field), CV_32FC3);
        cv::RNG grainy{uint64_t(77)};
        for (int y = 0; y < field.height; ++y)
            for (int x = 0; x < field.width; ++x) {
                const float value = 0.09f + float(grainy.gaussian(0.012));
                ipr::at<ipr::Vec3f>(scene, y, x) = ipr::Vec3f(value, value, value);
            }
        const int limit = 380;
        const ipr::Recti object(400, limit - 30, 90, 60); // astride the limit: half of it is real
        scene(ipr::toCv(object)).setTo(cv::Scalar::all(0.8));
        cv::Mat seen(ipr::toCv(field), CV_8UC1, cv::Scalar(255));
        seen(ipr::toCv(ipr::Recti(0, limit, field.width, field.height - limit))).setTo(0);
        cv::Mat filledScene = scene.clone();
        filledScene.setTo(cv::Scalar::all(0), seen == 0);
        const cv::Mat real = filledScene.clone();
        check(PanoramaFill::fill(filledScene, seen).outcome == PanoramaFill::Outcome::Filled, "a scene with an object is filled");
        cv::Mat green;
        cv::extractChannel(filledScene, green, 1);
        // Below the limit, right under the object: what a reflection would make bright.
        const cv::Mat reflection = green(ipr::toCv(ipr::Recti(object.x + 10, limit + 4, object.width - 20, 20)));
        check(cv::mean(reflection)[0] < 0.3, "an object cut by the limit is not reflected across it");
        // Anywhere else in what was invented: another copy of it.
        cv::Mat elsewhere = green(ipr::toCv(ipr::Recti(0, limit + 40, field.width, field.height - limit - 40))).clone();
        double brightest = 0.0;
        cv::minMaxLoc(elsewhere, nullptr, &brightest);
        check(brightest < 0.4, "an object is not found a second time");
        cv::Mat realChanges;
        cv::absdiff(filledScene(ipr::toCv(ipr::Recti(0, 0, field.width, limit))), real(ipr::toCv(ipr::Recti(0, 0, field.width, limit))), realChanges);
        check(cv::norm(realChanges, cv::NORM_INF) == 0.0, "and the object itself stays as it was");
    }

    // Nothing to do, too little to do it with, wrong inputs, and a cancel.
    cv::Mat whole = sky.clone();
    const PanoramaFill::Report nothing = PanoramaFill::fill(whole, cv::Mat(ipr::toCv(size), CV_8UC1, cv::Scalar(255)));
    check(nothing.outcome == PanoramaFill::Outcome::NothingToFill && nothing.filledShare == 0.0, "a full image needs no fill");
    check(cv::norm(whole, sky, cv::NORM_INF) == 0.0, "and stays as it is");
    cv::Mat littleKnown = cv::Mat::zeros(ipr::toCv(size), CV_8UC1);
    littleKnown(ipr::toCv(ipr::Recti(0, 0, 40, 40))).setTo(255);
    cv::Mat attempt = sky.clone();
    check(PanoramaFill::fill(attempt, littleKnown).outcome == PanoramaFill::Outcome::TooLittleKnown, "too little known is refused");
    cv::Mat noMask;
    check(PanoramaFill::fill(attempt, noMask).outcome == PanoramaFill::Outcome::NothingToFill, "without a mask nothing happens");
    cv::Mat stopped = sky.clone();
    check(PanoramaFill::fill(stopped, known, {}, [] { return true; }).outcome == PanoramaFill::Outcome::Cancelled, "a cancel stops it");

    // Through the job: the whole canvas, and what is invented is said.
    QTemporaryDir folder;
    std::vector<cv::Mat> frames;
    for (const double yaw : {-24.0, 0.0, 24.0})
        frames.push_back(render(sharedWorld(), camera(yaw * degree, -5 * degree)));
    PanoramaJob::Request request = requestFor(frames);
    request.scratchRoot = folder.path();
    request.edges = PanoramaJob::Edges::Fill;
    const PanoramaJob::Result result = PanoramaJob::run(request);
    if (check(result.success, "the job fills its edges")) {
        check(ipr::fromCv(result.cameraRgb.size()) == result.canvas, "over the whole canvas");
        check(result.filledShare > 0.0 && std::abs(result.filledShare - (1.0 - result.coveredShare)) < 0.03, "and says what it invented");
        cv::Mat green;
        cv::extractChannel(result.cameraRgb, green, 1);
        check(cv::countNonZero(green) == result.canvas.area(), "no pixel stays empty");
    }
    check(entriesIn(folder.path()) == 0, "and nothing stays in the scratch folder");
}

void jobMakesAPanorama()
{
    QTemporaryDir folder;
    std::vector<cv::Mat> frames;
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
    check(result.cameraRgb.type() == CV_32FC3 && ipr::fromCv(result.cameraRgb.size()) == result.kept.size(), "the result is the kept area");
    check(result.kept.width > frameSize.width * 17 / 10 && result.kept.height > frameSize.height * 9 / 10, "the panorama widens the field");
    check(reserved == quint64(result.canvas.area()) * 12u, "the canvas is announced before it is written");
    check(lastTotal > 0 && lastDone == lastTotal, "the progress reaches its end");
    check(std::abs(result.frames[0].gains[1] / result.frames[1].gains[1] - 1.0 / 1.15) < 0.03, "the brighter frame is brought down");
    check(entriesIn(folder.path()) == 0, "nothing is left in the scratch folder");

    // No hole in a cropped panorama: an uncovered pixel would be exactly 0.
    cv::Mat channels[3];
    cv::split(result.cameraRgb, channels);
    check(cv::countNonZero(channels[1]) == result.kept.area(), "a crop holds no empty pixel");

    const PanoramaJob::Result again = PanoramaJob::run(request);
    check(again.success && again.cameraRgb.size() == result.cameraRgb.size() && cv::norm(again.cameraRgb, result.cameraRgb, cv::NORM_INF) == 0.0,
          "the same series gives the same pixels");

    PanoramaJob::Request uncropped = requestFor(frames);
    uncropped.scratchRoot = folder.path();
    uncropped.edges = PanoramaJob::Edges::Keep;
    const PanoramaJob::Result whole = PanoramaJob::run(uncropped);
    check(whole.success && ipr::fromCv(whole.cameraRgb.size()) == whole.canvas, "uncropped, the whole canvas comes out");
    check(whole.coveredShare > 0.5 && whole.coveredShare < 1.0, "and says how much of it is covered");
}

void framesThatNearlyCoincideStayCheap()
{
    // A burst chosen by mistake: everything overlaps, and every place costs about the same.
    QTemporaryDir folder;
    std::vector<cv::Mat> frames;
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
        const std::vector<cv::Mat> frames{textured(frameSize, 21), textured(frameSize, 22), textured(frameSize, 23)};
        PanoramaJob::Request request = requestFor(frames);
        request.scratchRoot = folder.path();
        const PanoramaJob::Result result = PanoramaJob::run(request);
        check(!result.success && result.error.contains(QStringLiteral("recouvrent")), "unrelated frames fail with the reason");
    }
    {
        // Flat frames.
        const std::vector<cv::Mat> frames(3, cv::Mat(ipr::toCv(frameSize), CV_32FC3, cv::Scalar::all(0.4)));
        PanoramaJob::Request request = requestFor(frames);
        request.scratchRoot = folder.path();
        check(!PanoramaJob::run(request).success, "flat frames give no panorama");
    }
    {
        // A frame of another size is left out, not stretched.
        std::vector<cv::Mat> frames{render(sharedWorld(), camera(-14 * degree, 0)), render(sharedWorld(), camera(14 * degree, 0)), cv::Mat(300, 300, CV_32FC3, cv::Scalar::all(0.3))};
        PanoramaJob::Request request = requestFor(frames);
        request.scratchRoot = folder.path();
        const PanoramaJob::Result result = PanoramaJob::run(request);
        check(result.success && !result.frames[2].used && result.frames[2].exclusion.contains(QStringLiteral("Dimensions")), "another size is left out");
    }
    {
        // Another focal length too.
        std::vector<cv::Mat> frames{render(sharedWorld(), camera(-14 * degree, 0)), render(sharedWorld(), camera(14 * degree, 0)), render(sharedWorld(), camera(0, 0))};
        PanoramaJob::Request request;
        request.scratchRoot = folder.path();
        request.frameCount = 3;
        request.frame = [&frames](int index) {
            PanoramaJob::Frame frame;
            frame.cameraRgb = frames[size_t(index)];
            frame.focalMm = index == 2 ? 200.0 : 70.0;
            return frame;
        };
        const PanoramaJob::Result result = PanoramaJob::run(request);
        check(result.success && result.frames[2].exclusion.contains(QStringLiteral("Focale")), "another focal length is left out");
    }
    {
        // The caller can refuse the canvas, and a limit stops a canvas too large.
        std::vector<cv::Mat> frames{render(sharedWorld(), camera(-14 * degree, 0)), render(sharedWorld(), camera(14 * degree, 0))};
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
    std::vector<cv::Mat> frames{render(sharedWorld(), camera(-14 * degree, 0)), render(sharedWorld(), camera(14 * degree, 0))};
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
