#include "PanoramaFill.h"

#include "OpenCvBridge.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace PanoramaFill {
namespace {

constexpr int patchHalf = 3;              // a patch is 2 * half + 1 pixels wide
constexpr int smallestSide = 40;          // the pyramid stops above this
constexpr int coarsestIterations = 16;
constexpr int levelIterations = 6;
constexpr int randomTries = 8;
// A patch from the same height, then from nearby, is likelier to belong: sky comes from the
// sky, and the light of a part of it from that part.
constexpr float rowPull = 0.02f;
constexpr float columnPull = 0.02f;
// What stands out from its surroundings is an object, not a texture: a house is not to be
// found twice. Perceptual difference of a cell's luminance with the mean around it.
constexpr float standingOut = 0.25f;
constexpr double standingOutRadius = 1.0 / 40.0;   // of the long side
constexpr double largestObject = 0.01;             // of the known cells: beyond it, the texture of a slope or a cliff
constexpr double smallestObject = 0.0005;          // and below it, a stone among others, with its patch's reach
constexpr double fewestSourcesKept = 0.2;          // of the known cells, or the ban is lifted
// The real texture is mirrored across the limit only where it resembles what was found there:
// an object cut by the limit must not meet its own reflection. Perceptual difference, from
// which the mirror starts to fade, and at which it is gone.
constexpr float mirrorLikeFrom = 0.05f;
constexpr float mirrorLikeTo = 0.14f;

inline float encoded(float value) { return value >= 0.0f ? std::sqrt(value) : -std::sqrt(-value); }
inline float decoded(float value) { return value * std::abs(value); }

// One step of a small generator per pixel, so that the threads do not change the result.
inline std::uint32_t nextRandom(std::uint32_t &state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

struct Level
{
    cv::Mat colour;                 // CV_32FC3, perceptual; missing cells hold the current estimate
    cv::Mat known;                  // CV_8UC1
    cv::Mat usable;                 // CV_8UC1: where a whole patch of known pixels is centred
    std::vector<cv::Point> sources; // the same, as a list
    std::vector<cv::Point> holes;
    cv::Mat holeIndex;              // CV_32SC1, -1 on a known cell
    std::vector<cv::Point> field;   // per hole: the centre of its source patch
};

// Known cells that stand out from what surrounds them, widened by what a patch reaches.
cv::Mat objectsOf(const Level &level)
{
    cv::Mat luminance(level.colour.size(), CV_32FC1), weight;
    level.known.convertTo(weight, CV_32FC1, 1.0 / 255.0);
    for (int y = 0; y < luminance.rows; ++y) {
        const cv::Vec3f *in = level.colour.ptr<cv::Vec3f>(y);
        const float *present = weight.ptr<float>(y);
        float *out = luminance.ptr<float>(y);
        for (int x = 0; x < luminance.cols; ++x)
            out[x] = (0.25f * in[x][0] + 0.5f * in[x][1] + 0.25f * in[x][2]) * present[x];
    }
    const int side = std::max(3, int(std::max(level.colour.cols, level.colour.rows) * standingOutRadius)) | 1;
    cv::Mat sum, count;
    cv::blur(luminance, sum, cv::Size(side, side), cv::Point(-1, -1), cv::BORDER_REPLICATE);
    cv::blur(weight, count, cv::Size(side, side), cv::Point(-1, -1), cv::BORDER_REPLICATE);
    cv::Mat objects = cv::Mat::zeros(level.colour.size(), CV_8UC1);
    for (int y = 0; y < objects.rows; ++y) {
        const float *value = luminance.ptr<float>(y), *around = sum.ptr<float>(y), *amount = count.ptr<float>(y), *present = weight.ptr<float>(y);
        uchar *out = objects.ptr<uchar>(y);
        for (int x = 0; x < objects.cols; ++x)
            if (present[x] > 0.0f && amount[x] > 1e-3f && std::abs(value[x] - around[x] / amount[x]) > standingOut)
                out[x] = 255;
    }
    const cv::Mat reach = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(4 * patchHalf + 1, 4 * patchHalf + 1));
    cv::dilate(objects, objects, reach);
    // An object stands alone. Details close enough to join up are the texture of a slope, and
    // without them it could only be continued by what is left, such as a plain patch of earth.
    const int largest = int(largestObject * double(cv::countNonZero(level.known)));
    const int smallest = int(smallestObject * double(cv::countNonZero(level.known)));
    cv::Mat seen = cv::Mat::zeros(objects.size(), CV_8UC1);
    std::vector<cv::Point> stack, group;
    for (int y = 0; y < objects.rows; ++y) {
        for (int x = 0; x < objects.cols; ++x) {
            if (!objects.at<uchar>(y, x) || seen.at<uchar>(y, x))
                continue;
            group.clear();
            stack.assign(1, cv::Point(x, y));
            seen.at<uchar>(y, x) = 255;
            while (!stack.empty()) {
                const cv::Point p = stack.back();
                stack.pop_back();
                group.push_back(p);
                for (const cv::Point &step : {cv::Point(1, 0), cv::Point(-1, 0), cv::Point(0, 1), cv::Point(0, -1)}) {
                    const cv::Point q = p + step;
                    if (q.x >= 0 && q.y >= 0 && q.x < objects.cols && q.y < objects.rows && objects.at<uchar>(q) && !seen.at<uchar>(q)) {
                        seen.at<uchar>(q) = 255;
                        stack.push_back(q);
                    }
                }
            }
            if (int(group.size()) > largest || int(group.size()) < smallest)
                for (const cv::Point &p : group)
                    objects.at<uchar>(p) = 0;
        }
    }
    return objects;
}

// banned: cells no patch may be centred on, of this level's size, or empty.
void describe(Level &level, const cv::Mat &banned)
{
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2 * patchHalf + 1, 2 * patchHalf + 1));
    cv::erode(level.known, level.usable, kernel, cv::Point(-1, -1), 1, cv::BORDER_CONSTANT, cv::Scalar(0));
    if (!banned.empty())
        level.usable.setTo(cv::Scalar(0), banned);
    level.holeIndex.create(level.known.size(), CV_32SC1);
    level.holes.clear();
    level.sources.clear();
    for (int y = 0; y < level.known.rows; ++y) {
        const uchar *known = level.known.ptr<uchar>(y), *usable = level.usable.ptr<uchar>(y);
        int *index = level.holeIndex.ptr<int>(y);
        for (int x = 0; x < level.known.cols; ++x) {
            index[x] = -1;
            if (!known[x]) {
                index[x] = int(level.holes.size());
                level.holes.emplace_back(x, y);
            }
            if (usable[x])
                level.sources.emplace_back(x, y);
        }
    }
}

// The mean of each cell's known pixels; a cell is known only when all of its pixels are.
void reduced(const cv::Mat &colour, const cv::Mat &known, int factor, cv::Mat &outColour, cv::Mat &outKnown)
{
    const cv::Size size((colour.cols + factor - 1) / factor, (colour.rows + factor - 1) / factor);
    outColour.create(size, CV_32FC3);
    outKnown.create(size, CV_8UC1);
    cv::parallel_for_(cv::Range(0, size.height), [&](const cv::Range &range) {
        for (int cy = range.start; cy < range.end; ++cy) {
            cv::Vec3f *targetColour = outColour.ptr<cv::Vec3f>(cy);
            uchar *targetKnown = outKnown.ptr<uchar>(cy);
            const int top = cy * factor, bottom = std::min(colour.rows, top + factor);
            for (int cx = 0; cx < size.width; ++cx) {
                const int left = cx * factor, right = std::min(colour.cols, left + factor);
                cv::Vec3f sum(0, 0, 0);
                int count = 0;
                for (int y = top; y < bottom; ++y) {
                    const cv::Vec3f *inColour = colour.ptr<cv::Vec3f>(y);
                    const uchar *inKnown = known.ptr<uchar>(y);
                    for (int x = left; x < right; ++x) {
                        if (inKnown[x]) {
                            sum += inColour[x];
                            ++count;
                        }
                    }
                }
                targetColour[cx] = count > 0 ? sum * (1.0f / float(count)) : cv::Vec3f(0, 0, 0);
                targetKnown[cx] = count == (bottom - top) * (right - left) ? 255 : 0;
            }
        }
    });
}

// A first estimate on the coarsest level: layer after layer from the known edge inwards.
void growFromEdges(Level &level)
{
    cv::Mat present = level.known.clone();
    std::vector<cv::Point> remaining = level.holes;
    while (!remaining.empty()) {
        std::vector<cv::Point> next;
        std::vector<std::pair<cv::Point, cv::Vec3f>> layer;
        for (const cv::Point &p : remaining) {
            cv::Vec3f sum(0, 0, 0);
            int count = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = p.x + dx, y = p.y + dy;
                    if (x >= 0 && y >= 0 && x < present.cols && y < present.rows && present.at<uchar>(y, x)) {
                        sum += level.colour.at<cv::Vec3f>(y, x);
                        ++count;
                    }
                }
            }
            if (count > 0)
                layer.emplace_back(p, sum * (1.0f / float(count)));
            else
                next.push_back(p);
        }
        if (layer.empty())
            break; // nothing known at all: the caller has refused that already
        for (const auto &entry : layer) {
            level.colour.at<cv::Vec3f>(entry.first) = entry.second;
            present.at<uchar>(entry.first) = 255;
        }
        remaining = std::move(next);
    }
}

inline float patchCost(const cv::Mat &colour, const cv::Point &target, const cv::Point &source)
{
    float sum = 0.0f;
    int count = 0;
    for (int dy = -patchHalf; dy <= patchHalf; ++dy) {
        const int ty = target.y + dy;
        if (ty < 0 || ty >= colour.rows)
            continue;
        const cv::Vec3f *targetRow = colour.ptr<cv::Vec3f>(ty);
        const cv::Vec3f *sourceRow = colour.ptr<cv::Vec3f>(source.y + dy);
        for (int dx = -patchHalf; dx <= patchHalf; ++dx) {
            const int tx = target.x + dx;
            if (tx < 0 || tx >= colour.cols)
                continue;
            const cv::Vec3f difference = targetRow[tx] - sourceRow[source.x + dx];
            sum += difference.dot(difference);
            ++count;
        }
    }
    const float rows = float(source.y - target.y) / float(colour.rows), columns = float(source.x - target.x) / float(colour.cols);
    return sum / float(std::max(1, count)) + rowPull * rows * rows + columnPull * columns * columns;
}

// One pass over the level: each hole improves its source, then takes the mean of what the
// patches around it propose. Both read the previous pass only, so the threads change nothing.
void searchAndVote(Level &level, int levelIndex, int iteration)
{
    const int holeCount = int(level.holes.size());
    std::vector<cv::Point> improved(static_cast<size_t>(holeCount));
    const int widest = std::max(level.colour.cols, level.colour.rows);
    const int sourceCount = int(level.sources.size());
    cv::parallel_for_(cv::Range(0, holeCount), [&](const cv::Range &range) {
        for (int i = range.start; i < range.end; ++i) {
            const cv::Point p = level.holes[size_t(i)];
            cv::Point best = level.field[size_t(i)];
            float bestCost = patchCost(level.colour, p, best);
            const auto attempt = [&](const cv::Point &candidate) {
                if (candidate.x < 0 || candidate.y < 0 || candidate.x >= level.usable.cols || candidate.y >= level.usable.rows ||
                    !level.usable.at<uchar>(candidate))
                    return;
                const float cost = patchCost(level.colour, p, candidate);
                if (cost < bestCost) {
                    bestCost = cost;
                    best = candidate;
                }
            };
            // What a neighbour found, shifted by the distance to it.
            for (const int step : {1, 2, 4}) {
                for (const cv::Point &offset : {cv::Point(step, 0), cv::Point(-step, 0), cv::Point(0, step), cv::Point(0, -step)}) {
                    const cv::Point neighbour = p + offset;
                    if (neighbour.x < 0 || neighbour.y < 0 || neighbour.x >= level.holeIndex.cols || neighbour.y >= level.holeIndex.rows)
                        continue;
                    const int index = level.holeIndex.at<int>(neighbour);
                    if (index >= 0)
                        attempt(level.field[size_t(index)] - offset);
                }
            }
            std::uint32_t state = std::uint32_t(i) * 2654435761u + std::uint32_t(iteration) * 40503u + std::uint32_t(levelIndex) * 97u + 1u;
            nextRandom(state);
            // Around the best so far, closer and closer; and anywhere, in case it is a poor place.
            for (int radius = widest / 2, tries = 0; radius >= 1 && tries < randomTries; radius /= 2, ++tries) {
                const int span = 2 * radius + 1;
                attempt(best + cv::Point(int(nextRandom(state) % unsigned(span)) - radius, int(nextRandom(state) % unsigned(span)) - radius));
            }
            attempt(level.sources[size_t(nextRandom(state) % unsigned(sourceCount))]);
            // And at its own height: what runs across the image, as a horizon, carries on.
            attempt(cv::Point(best.x, p.y));
            attempt(cv::Point(int(nextRandom(state) % unsigned(level.usable.cols)), p.y));
            improved[size_t(i)] = best;
        }
    });
    level.field = std::move(improved);

    std::vector<cv::Vec3f> voted(static_cast<size_t>(holeCount));
    cv::parallel_for_(cv::Range(0, holeCount), [&](const cv::Range &range) {
        for (int i = range.start; i < range.end; ++i) {
            const cv::Point p = level.holes[size_t(i)];
            cv::Vec3f sum(0, 0, 0);
            int count = 0;
            for (int dy = -patchHalf; dy <= patchHalf; ++dy) {
                for (int dx = -patchHalf; dx <= patchHalf; ++dx) {
                    const cv::Point q = p + cv::Point(dx, dy);
                    if (q.x < 0 || q.y < 0 || q.x >= level.holeIndex.cols || q.y >= level.holeIndex.rows)
                        continue;
                    const int index = level.holeIndex.at<int>(q);
                    if (index < 0)
                        continue;
                    // The pixel of that patch that falls on p; a source's patch is entirely known.
                    sum += level.colour.at<cv::Vec3f>(level.field[size_t(index)] - cv::Point(dx, dy));
                    ++count;
                }
            }
            voted[size_t(i)] = count > 0 ? sum * (1.0f / float(count)) : level.colour.at<cv::Vec3f>(p);
        }
    });
    for (int i = 0; i < holeCount; ++i)
        level.colour.at<cv::Vec3f>(level.holes[size_t(i)]) = voted[size_t(i)];
}

// The coarser level's result as a start for this one.
void inherit(Level &fine, const Level &coarse)
{
    cv::Mat enlarged;
    enlarged = ipr::resized(coarse.colour, fine.colour.size(), ipr::Interpolation::Linear);
    fine.field.resize(fine.holes.size());
    const int sourceCount = int(fine.sources.size());
    for (size_t i = 0; i < fine.holes.size(); ++i) {
        const cv::Point p = fine.holes[i];
        fine.colour.at<cv::Vec3f>(p) = enlarged.at<cv::Vec3f>(p);
        const cv::Point parent(p.x / 2, p.y / 2);
        cv::Point candidate(-1, -1);
        const int index = coarse.holeIndex.at<int>(parent);
        if (index >= 0)
            candidate = coarse.field[size_t(index)] * 2 + (p - parent * 2);
        const bool valid = candidate.x >= 0 && candidate.y >= 0 && candidate.x < fine.usable.cols && candidate.y < fine.usable.rows &&
                           fine.usable.at<uchar>(candidate);
        std::uint32_t state = std::uint32_t(i) * 2654435761u + 7u;
        fine.field[i] = valid ? candidate : fine.sources[size_t(nextRandom(state) % unsigned(sourceCount))];
    }
}

// For each cell, the way to the nearest known cell, by two sweeps of the sequential signed
// Euclidean distance: exact enough for a transition, and linear in the image's size.
cv::Mat waysToKnown(const cv::Mat &known)
{
    constexpr int far = 16000;
    cv::Mat ways(known.size(), CV_16SC2);
    for (int y = 0; y < known.rows; ++y) {
        const uchar *in = known.ptr<uchar>(y);
        cv::Vec2s *out = ways.ptr<cv::Vec2s>(y);
        for (int x = 0; x < known.cols; ++x)
            out[x] = in[x] ? cv::Vec2s(0, 0) : cv::Vec2s(far, far);
    }
    const auto length = [](const cv::Vec2s &way) { return int(way[0]) * way[0] + int(way[1]) * way[1]; };
    const auto compare = [&](cv::Vec2s &here, int x, int y, int dx, int dy) {
        if (x + dx < 0 || y + dy < 0 || x + dx >= ways.cols || y + dy >= ways.rows)
            return;
        const cv::Vec2s &there = ways.at<cv::Vec2s>(y + dy, x + dx);
        const cv::Vec2s candidate(short(std::clamp(there[0] + dx, -far, far)), short(std::clamp(there[1] + dy, -far, far)));
        if (length(candidate) < length(here))
            here = candidate;
    };
    for (int y = 0; y < ways.rows; ++y) {
        cv::Vec2s *row = ways.ptr<cv::Vec2s>(y);
        for (int x = 0; x < ways.cols; ++x) {
            compare(row[x], x, y, -1, 0);
            compare(row[x], x, y, 0, -1);
            compare(row[x], x, y, -1, -1);
            compare(row[x], x, y, 1, -1);
        }
        for (int x = ways.cols - 1; x >= 0; --x)
            compare(row[x], x, y, 1, 0);
    }
    for (int y = ways.rows - 1; y >= 0; --y) {
        cv::Vec2s *row = ways.ptr<cv::Vec2s>(y);
        for (int x = ways.cols - 1; x >= 0; --x) {
            compare(row[x], x, y, 1, 0);
            compare(row[x], x, y, 0, 1);
            compare(row[x], x, y, 1, 1);
            compare(row[x], x, y, -1, 1);
        }
        for (int x = 0; x < ways.cols; ++x)
            compare(row[x], x, y, -1, 0);
    }
    return ways;
}

inline cv::Vec3f bilinearAt(const cv::Mat &image, float x, float y)
{
    const float cx = std::clamp(x, 0.0f, float(image.cols - 1)), cy = std::clamp(y, 0.0f, float(image.rows - 1));
    const int x0 = int(cx), y0 = int(cy), x1 = std::min(x0 + 1, image.cols - 1), y1 = std::min(y0 + 1, image.rows - 1);
    const float fx = cx - float(x0), fy = cy - float(y0);
    const cv::Vec3f top = image.at<cv::Vec3f>(y0, x0) * (1.0f - fx) + image.at<cv::Vec3f>(y0, x1) * fx;
    const cv::Vec3f bottom = image.at<cv::Vec3f>(y1, x0) * (1.0f - fx) + image.at<cv::Vec3f>(y1, x1) * fx;
    return top * (1.0f - fy) + bottom * fy;
}

} // namespace

Report fill(cv::Mat &image, const cv::Mat &known, const Options &options, const std::function<bool()> &cancelled,
            const std::function<void(int done, int total)> &progress)
{
    Report report;
    if (image.empty() || image.type() != CV_32FC3 || known.size() != image.size() || known.type() != CV_8UC1)
        return report;
    const double wholeArea = double(image.total());
    const double knownCount = double(cv::countNonZero(known));
    report.filledShare = 1.0 - knownCount / wholeArea;
    if (knownCount >= wholeArea)
        return report;
    if (knownCount < options.minimumKnown * wholeArea) {
        report.outcome = Outcome::TooLittleKnown;
        return report;
    }
    const auto stopped = [&] { return cancelled && cancelled(); };

    // The pyramid, from the working size down; each level's colours keep to known pixels.
    cv::Mat perceptual(image.size(), CV_32FC3);
    cv::parallel_for_(cv::Range(0, image.rows), [&](const cv::Range &range) {
        for (int y = range.start; y < range.end; ++y) {
            const cv::Vec3f *in = image.ptr<cv::Vec3f>(y);
            cv::Vec3f *out = perceptual.ptr<cv::Vec3f>(y);
            for (int x = 0; x < image.cols; ++x)
                out[x] = cv::Vec3f(encoded(in[x][0]), encoded(in[x][1]), encoded(in[x][2]));
        }
    });
    const int factor = std::max(1, int(std::ceil(double(std::max(image.cols, image.rows)) / options.workingSide)));
    std::vector<Level> levels(1);
    if (factor > 1) {
        reduced(perceptual, known, factor, levels[0].colour, levels[0].known);
    } else {
        levels[0].colour = perceptual;
        levels[0].known = known.clone();
    }
    while (std::min(levels.back().colour.rows, levels.back().colour.cols) / 2 >= smallestSide) {
        Level next;
        reduced(levels.back().colour, levels.back().known, 2, next.colour, next.known);
        levels.push_back(std::move(next));
    }
    // What stands out is no source, as long as that leaves enough to take from.
    cv::Mat banned = objectsOf(levels.front());
    {
        const double knownCells = double(cv::countNonZero(levels.front().known));
        cv::Mat allowed;
        cv::bitwise_not(banned, allowed);
        cv::bitwise_and(allowed, levels.front().known, allowed);
        if (double(cv::countNonZero(allowed)) < fewestSourcesKept * knownCells)
            banned.release();
    }
    const cv::Mat objectCells = banned.clone();
    // A level without a patch to take cannot search: the pyramid stops above it.
    for (Level &level : levels) {
        describe(level, banned);
        // What a coarser cell holds of a banned one bans it too.
        if (!banned.empty()) {
            cv::Mat coarser = cv::Mat::zeros((banned.rows + 1) / 2, (banned.cols + 1) / 2, CV_8UC1);
            for (int y = 0; y < banned.rows; ++y) {
                const uchar *in = banned.ptr<uchar>(y);
                uchar *out = coarser.ptr<uchar>(y / 2);
                for (int x = 0; x < banned.cols; ++x)
                    if (in[x])
                        out[x / 2] = 255;
            }
            banned = coarser;
        }
    }
    while (levels.size() > 1 && levels.back().sources.empty())
        levels.pop_back();
    if (levels.back().sources.empty()) {
        report.outcome = Outcome::TooLittleKnown;
        return report;
    }
    report.levels = int(levels.size());

    int done = 0;
    const int total = coarsestIterations + levelIterations * (int(levels.size()) - 1) + 1;
    const auto advance = [&] {
        if (progress)
            progress(++done, total);
    };

    Level &coarsest = levels.back();
    growFromEdges(coarsest);
    coarsest.field.resize(coarsest.holes.size());
    for (size_t i = 0; i < coarsest.holes.size(); ++i) {
        std::uint32_t state = std::uint32_t(i) * 2654435761u + 13u;
        coarsest.field[i] = coarsest.sources[size_t(nextRandom(state) % unsigned(coarsest.sources.size()))];
    }
    for (int index = int(levels.size()) - 1; index >= 0; --index) {
        Level &level = levels[size_t(index)];
        if (index < int(levels.size()) - 1) {
            if (level.sources.empty()) {
                report.outcome = Outcome::TooLittleKnown;
                return report;
            }
            inherit(level, levels[size_t(index) + 1]);
        }
        const int iterations = index == int(levels.size()) - 1 ? coarsestIterations : levelIterations;
        for (int iteration = 0; iteration < iterations; ++iteration) {
            if (stopped()) {
                report.outcome = Outcome::Cancelled;
                return report;
            }
            if (!level.holes.empty())
                searchAndVote(level, index, iteration);
            advance();
        }
    }

    // Back to whole resolution: the slow variations of the result, and the fine detail of the
    // places each pixel's cell takes its patch from, weighted across the four nearest cells so
    // that the cells do not show.
    const Level &working = levels.front();
    cv::Mat workingLinear(working.colour.size(), CV_32FC3);
    for (int y = 0; y < workingLinear.rows; ++y) {
        const cv::Vec3f *in = working.colour.ptr<cv::Vec3f>(y);
        cv::Vec3f *out = workingLinear.ptr<cv::Vec3f>(y);
        for (int x = 0; x < workingLinear.cols; ++x)
            out[x] = cv::Vec3f(decoded(in[x][0]), decoded(in[x][1]), decoded(in[x][2]));
    }
    const float toCell = 1.0f / float(factor);
    // Across the limit with the real image, its texture carries on, mirrored, and fades into
    // what was found: an invented texture meeting a real one would draw the limit as a line.
    constexpr int waysDivisor = 2;
    cv::Mat reducedKnown(known.rows / waysDivisor + 1, known.cols / waysDivisor + 1, CV_8UC1, cv::Scalar(255));
    for (int y = 0; y < known.rows; ++y) {
        const uchar *in = known.ptr<uchar>(y);
        uchar *out = reducedKnown.ptr<uchar>(y / waysDivisor);
        for (int x = 0; x < known.cols; ++x)
            if (!in[x])
                out[x / waysDivisor] = 0;
    }
    const cv::Mat ways = waysToKnown(reducedKnown);
    const float transition = float(std::max(1, options.transitionPixels));
    // The way to the nearest pixel jumps from one to the next: mirrored as it is, the texture
    // would break up into a grid. Smoothed, it folds the image in one piece.
    cv::Mat smoothWays;
    ways.convertTo(smoothWays, CV_32FC2);
    const float longestWay = 2.0f * transition / float(waysDivisor);
    for (int y = 0; y < smoothWays.rows; ++y) {
        cv::Vec2f *row = smoothWays.ptr<cv::Vec2f>(y);
        for (int x = 0; x < smoothWays.cols; ++x) {
            const float length = std::hypot(row[x][0], row[x][1]);
            if (length > longestWay)
                row[x] *= longestWay / length;
        }
    }
    const double waySmoothing = std::max(1.0, double(transition) / 8.0);
    smoothWays = ipr::gaussianBlurred(smoothWays, waySmoothing, ipr::BlurBorder::LastPixel);
    cv::parallel_for_(cv::Range(0, image.rows), [&](const cv::Range &range) {
        for (int y = range.start; y < range.end; ++y) {
            const uchar *inKnown = known.ptr<uchar>(y);
            cv::Vec3f *out = image.ptr<cv::Vec3f>(y);
            const float cellY = (float(y) + 0.5f) * toCell - 0.5f;
            for (int x = 0; x < image.cols; ++x) {
                if (inKnown[x])
                    continue;
                const float cellX = (float(x) + 0.5f) * toCell - 0.5f;
                cv::Vec3f value = bilinearAt(workingLinear, cellX, cellY);
                if (factor > 1) {
                    const int cx0 = std::clamp(int(std::floor(cellX)), 0, workingLinear.cols - 1), cy0 = std::clamp(int(std::floor(cellY)), 0, workingLinear.rows - 1);
                    const float fx = std::clamp(cellX - float(cx0), 0.0f, 1.0f), fy = std::clamp(cellY - float(cy0), 0.0f, 1.0f);
                    cv::Vec3f detail(0, 0, 0);
                    float weightSum = 0.0f;
                    for (int j = 0; j < 2; ++j) {
                        for (int i = 0; i < 2; ++i) {
                            const int cx = std::min(cx0 + i, workingLinear.cols - 1), cy = std::min(cy0 + j, workingLinear.rows - 1);
                            const int index = working.holeIndex.at<int>(cy, cx);
                            if (index < 0)
                                continue;
                            const cv::Point from = working.field[size_t(index)];
                            const int sx = x + (from.x - cx) * factor, sy = y + (from.y - cy) * factor;
                            if (sx < 0 || sy < 0 || sx >= image.cols || sy >= image.rows || !known.at<uchar>(sy, sx))
                                continue;
                            const float weight = (i == 0 ? 1.0f - fx : fx) * (j == 0 ? 1.0f - fy : fy);
                            const cv::Vec3f here = image.at<cv::Vec3f>(sy, sx) - bilinearAt(workingLinear, (float(sx) + 0.5f) * toCell - 0.5f, (float(sy) + 0.5f) * toCell - 0.5f);
                            detail += here * weight;
                            weightSum += weight;
                        }
                    }
                    if (weightSum > 1e-3f)
                        value += detail * (1.0f / weightSum);
                }
                const cv::Vec2s way = ways.at<cv::Vec2s>(y / waysDivisor, x / waysDivisor);
                const float distance = std::hypot(float(way[0]), float(way[1])) * waysDivisor;
                if (distance < transition) {
                    const cv::Vec2f fold = smoothWays.at<cv::Vec2f>(y / waysDivisor, x / waysDivisor) * float(waysDivisor);
                    const int mirrorX = x + int(std::lround(2.0f * fold[0])), mirrorY = y + int(std::lround(2.0f * fold[1]));
                    const bool onObject = !objectCells.empty() && mirrorX >= 0 && mirrorY >= 0 && mirrorX < image.cols && mirrorY < image.rows &&
                                          objectCells.at<uchar>(mirrorY / factor, mirrorX / factor);
                    if (!onObject && mirrorX >= 0 && mirrorY >= 0 && mirrorX < image.cols && mirrorY < image.rows && known.at<uchar>(mirrorY, mirrorX)) {
                        const float t = distance / transition;
                        const float invented = t * t * (3.0f - 2.0f * t);
                        // Measured on the slow variations of both places.
                        const cv::Vec3f here = bilinearAt(working.colour, cellX, cellY);
                        const cv::Vec3f there = bilinearAt(working.colour, (float(mirrorX) + 0.5f) * toCell - 0.5f, (float(mirrorY) + 0.5f) * toCell - 0.5f);
                        const cv::Vec3f gap = here - there;
                        const float unlike = std::clamp((std::sqrt(gap.dot(gap) / 3.0f) - mirrorLikeFrom) / (mirrorLikeTo - mirrorLikeFrom), 0.0f, 1.0f);
                        const float mirrored = (1.0f - invented) * (1.0f - unlike * unlike * (3.0f - 2.0f * unlike));
                        value = value * (1.0f - mirrored) + image.at<cv::Vec3f>(mirrorY, mirrorX) * mirrored;
                    }
                }
                out[x] = value;
            }
        }
    });
    advance();
    report.outcome = stopped() ? Outcome::Cancelled : Outcome::Filled;
    return report;
}

} // namespace PanoramaFill
