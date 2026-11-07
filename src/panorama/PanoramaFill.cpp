#include "PanoramaFill.h"

#include "Filters.h"
#include "Neighbourhood.h"
#include "Parallel.h"
#include "Pixels.h"
#include "Resize.h"

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

// One pixel, (y, x) or a point, to read or to write.
template <typename T>
inline const T &readAt(const ipr::Image &image, int y, int x)
{
    return image.row<T>(y)[x];
}

template <typename T>
inline const T &readAt(const ipr::Image &image, const ipr::Point2i &p)
{
    return image.row<T>(p.y)[p.x];
}

template <typename T>
inline T &writeAt(ipr::Image &image, int y, int x)
{
    return image.mutableRow<T>(y)[x];
}

template <typename T>
inline T &writeAt(ipr::Image &image, const ipr::Point2i &p)
{
    return image.mutableRow<T>(p.y)[p.x];
}

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
    ipr::Image colour;              // F32, three channels, perceptual; missing cells hold the current estimate
    ipr::Image known;               // bytes of one channel
    ipr::Image usable;              // the same: where a whole patch of known pixels is centred
    std::vector<ipr::Point2i> sources; // the same, as a list
    std::vector<ipr::Point2i> holes;
    ipr::Image holeIndex;           // S32 of one channel, -1 on a known cell
    std::vector<ipr::Point2i> field;   // per hole: the centre of its source patch
};

// Known cells that stand out from what surrounds them, widened by what a patch reaches.
ipr::Image objectsOf(const Level &level)
{
    ipr::Image luminance = ipr::Image::create(level.colour.size(), ipr::Depth::F32, 1);
    ipr::Image weight = ipr::Image::create(level.known.size(), ipr::Depth::F32, 1);
    ipr::convertInto(level.known, weight, 1.0 / 255.0);
    for (int y = 0; y < luminance.height(); ++y) {
        const ipr::Vec3f *in = level.colour.row<ipr::Vec3f>(y);
        const float *present = weight.row<float>(y);
        float *out = luminance.mutableRow<float>(y);
        for (int x = 0; x < luminance.width(); ++x)
            out[x] = (0.25f * in[x][0] + 0.5f * in[x][1] + 0.25f * in[x][2]) * present[x];
    }
    const int side = std::max(3, int(std::max(level.colour.width(), level.colour.height()) * standingOutRadius)) | 1;
    const ipr::Image sum = ipr::boxMean(luminance, ipr::Size(side, side), ipr::BlurBorder::LastPixel);
    const ipr::Image count = ipr::boxMean(weight, ipr::Size(side, side), ipr::BlurBorder::LastPixel);
    ipr::Image objects = ipr::Image::zeros(level.colour.size(), ipr::Depth::U8, 1);
    for (int y = 0; y < objects.height(); ++y) {
        const float *value = luminance.row<float>(y), *around = sum.row<float>(y), *amount = count.row<float>(y), *present = weight.row<float>(y);
        std::uint8_t *out = objects.mutableRow<std::uint8_t>(y);
        for (int x = 0; x < objects.width(); ++x)
            if (present[x] > 0.0f && amount[x] > 1e-3f && std::abs(value[x] - around[x] / amount[x]) > standingOut)
                out[x] = 255;
    }
    objects = ipr::maximum(objects, ipr::Size(4 * patchHalf + 1, 4 * patchHalf + 1), ipr::Beyond::Nothing);
    // An object stands alone. Details close enough to join up are the texture of a slope, and
    // without them it could only be continued by what is left, such as a plain patch of earth.
    const int largest = int(largestObject * double(ipr::countNonZero(level.known)));
    const int smallest = int(smallestObject * double(ipr::countNonZero(level.known)));
    ipr::Image seen = ipr::Image::zeros(objects.size(), ipr::Depth::U8, 1);
    std::vector<ipr::Point2i> stack, group;
    for (int y = 0; y < objects.height(); ++y) {
        for (int x = 0; x < objects.width(); ++x) {
            if (!readAt<std::uint8_t>(objects, y, x) || readAt<std::uint8_t>(seen, y, x))
                continue;
            group.clear();
            stack.assign(1, ipr::Point2i(x, y));
            writeAt<std::uint8_t>(seen, y, x) = 255;
            while (!stack.empty()) {
                const ipr::Point2i p = stack.back();
                stack.pop_back();
                group.push_back(p);
                for (const ipr::Point2i &step : {ipr::Point2i(1, 0), ipr::Point2i(-1, 0), ipr::Point2i(0, 1), ipr::Point2i(0, -1)}) {
                    const ipr::Point2i q = p + step;
                    if (q.x >= 0 && q.y >= 0 && q.x < objects.width() && q.y < objects.height() && readAt<std::uint8_t>(objects, q) && !readAt<std::uint8_t>(seen, q)) {
                        writeAt<std::uint8_t>(seen, q) = 255;
                        stack.push_back(q);
                    }
                }
            }
            if (int(group.size()) > largest || int(group.size()) < smallest)
                for (const ipr::Point2i &p : group)
                    writeAt<std::uint8_t>(objects, p) = 0;
        }
    }
    return objects;
}

// banned: cells no patch may be centred on, of this level's size, or empty.
void describe(Level &level, const ipr::Image &banned)
{
    level.usable = ipr::minimum(level.known, ipr::Size(2 * patchHalf + 1, 2 * patchHalf + 1), ipr::Beyond::Zero);
    if (!banned.empty())
        ipr::fillWhere(level.usable, 0, banned);
    level.holeIndex = ipr::Image::create(level.known.size(), ipr::Depth::S32, 1);
    level.holes.clear();
    level.sources.clear();
    for (int y = 0; y < level.known.height(); ++y) {
        const std::uint8_t *known = level.known.row<std::uint8_t>(y), *usable = level.usable.row<std::uint8_t>(y);
        int *index = level.holeIndex.mutableRow<int>(y);
        for (int x = 0; x < level.known.width(); ++x) {
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
void reduced(const ipr::Image &colour, const ipr::Image &known, int factor, ipr::Image &outColour, ipr::Image &outKnown)
{
    const ipr::Size size((colour.width() + factor - 1) / factor, (colour.height() + factor - 1) / factor);
    outColour = ipr::Image::create(size, ipr::Depth::F32, 3);
    outKnown = ipr::Image::create(size, ipr::Depth::U8, 1);
    ipr::parallelRows(size.height, [&](int first, int last) {
        for (int cy = first; cy < last; ++cy) {
            ipr::Vec3f *targetColour = outColour.mutableRow<ipr::Vec3f>(cy);
            std::uint8_t *targetKnown = outKnown.mutableRow<std::uint8_t>(cy);
            const int top = cy * factor, bottom = std::min(colour.height(), top + factor);
            for (int cx = 0; cx < size.width; ++cx) {
                const int left = cx * factor, right = std::min(colour.width(), left + factor);
                ipr::Vec3f sum(0, 0, 0);
                int count = 0;
                for (int y = top; y < bottom; ++y) {
                    const ipr::Vec3f *inColour = colour.row<ipr::Vec3f>(y);
                    const std::uint8_t *inKnown = known.row<std::uint8_t>(y);
                    for (int x = left; x < right; ++x) {
                        if (inKnown[x]) {
                            sum += inColour[x];
                            ++count;
                        }
                    }
                }
                targetColour[cx] = count > 0 ? sum * (1.0f / float(count)) : ipr::Vec3f(0, 0, 0);
                targetKnown[cx] = count == (bottom - top) * (right - left) ? 255 : 0;
            }
        }
    });
}

// A first estimate on the coarsest level: layer after layer from the known edge inwards.
void growFromEdges(Level &level)
{
    ipr::Image present = level.known.clone();
    std::vector<ipr::Point2i> remaining = level.holes;
    while (!remaining.empty()) {
        std::vector<ipr::Point2i> next;
        std::vector<std::pair<ipr::Point2i, ipr::Vec3f>> layer;
        for (const ipr::Point2i &p : remaining) {
            ipr::Vec3f sum(0, 0, 0);
            int count = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = p.x + dx, y = p.y + dy;
                    if (x >= 0 && y >= 0 && x < present.width() && y < present.height() && readAt<std::uint8_t>(present, y, x)) {
                        sum += readAt<ipr::Vec3f>(level.colour, y, x);
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
            writeAt<ipr::Vec3f>(level.colour, entry.first) = entry.second;
            writeAt<std::uint8_t>(present, entry.first) = 255;
        }
        remaining = std::move(next);
    }
}

inline float patchCost(const ipr::Image &colour, const ipr::Point2i &target, const ipr::Point2i &source)
{
    float sum = 0.0f;
    int count = 0;
    for (int dy = -patchHalf; dy <= patchHalf; ++dy) {
        const int ty = target.y + dy;
        if (ty < 0 || ty >= colour.height())
            continue;
        const ipr::Vec3f *targetRow = colour.row<ipr::Vec3f>(ty);
        const ipr::Vec3f *sourceRow = colour.row<ipr::Vec3f>(source.y + dy);
        for (int dx = -patchHalf; dx <= patchHalf; ++dx) {
            const int tx = target.x + dx;
            if (tx < 0 || tx >= colour.width())
                continue;
            const ipr::Vec3f difference = targetRow[tx] - sourceRow[source.x + dx];
            sum += difference.dot(difference);
            ++count;
        }
    }
    const float rows = float(source.y - target.y) / float(colour.height()), columns = float(source.x - target.x) / float(colour.width());
    return sum / float(std::max(1, count)) + rowPull * rows * rows + columnPull * columns * columns;
}

// One pass over the level: each hole improves its source, then takes the mean of what the
// patches around it propose. Both read the previous pass only, so the threads change nothing.
void searchAndVote(Level &level, int levelIndex, int iteration)
{
    const int holeCount = int(level.holes.size());
    std::vector<ipr::Point2i> improved(static_cast<size_t>(holeCount));
    const int widest = std::max(level.colour.width(), level.colour.height());
    const int sourceCount = int(level.sources.size());
    ipr::parallelRows(holeCount, [&](int first, int last) {
        for (int i = first; i < last; ++i) {
            const ipr::Point2i p = level.holes[size_t(i)];
            ipr::Point2i best = level.field[size_t(i)];
            float bestCost = patchCost(level.colour, p, best);
            const auto attempt = [&](const ipr::Point2i &candidate) {
                if (candidate.x < 0 || candidate.y < 0 || candidate.x >= level.usable.width() || candidate.y >= level.usable.height() ||
                    !readAt<std::uint8_t>(level.usable, candidate))
                    return;
                const float cost = patchCost(level.colour, p, candidate);
                if (cost < bestCost) {
                    bestCost = cost;
                    best = candidate;
                }
            };
            // What a neighbour found, shifted by the distance to it.
            for (const int step : {1, 2, 4}) {
                for (const ipr::Point2i &offset : {ipr::Point2i(step, 0), ipr::Point2i(-step, 0), ipr::Point2i(0, step), ipr::Point2i(0, -step)}) {
                    const ipr::Point2i neighbour = p + offset;
                    if (neighbour.x < 0 || neighbour.y < 0 || neighbour.x >= level.holeIndex.width() || neighbour.y >= level.holeIndex.height())
                        continue;
                    const int index = readAt<int>(level.holeIndex, neighbour);
                    if (index >= 0)
                        attempt(level.field[size_t(index)] - offset);
                }
            }
            std::uint32_t state = std::uint32_t(i) * 2654435761u + std::uint32_t(iteration) * 40503u + std::uint32_t(levelIndex) * 97u + 1u;
            nextRandom(state);
            // Around the best so far, closer and closer; and anywhere, in case it is a poor place.
            for (int radius = widest / 2, tries = 0; radius >= 1 && tries < randomTries; radius /= 2, ++tries) {
                const int span = 2 * radius + 1;
                attempt(best + ipr::Point2i(int(nextRandom(state) % unsigned(span)) - radius, int(nextRandom(state) % unsigned(span)) - radius));
            }
            attempt(level.sources[size_t(nextRandom(state) % unsigned(sourceCount))]);
            // And at its own height: what runs across the image, as a horizon, carries on.
            attempt(ipr::Point2i(best.x, p.y));
            attempt(ipr::Point2i(int(nextRandom(state) % unsigned(level.usable.width())), p.y));
            improved[size_t(i)] = best;
        }
    });
    level.field = std::move(improved);

    std::vector<ipr::Vec3f> voted(static_cast<size_t>(holeCount));
    ipr::parallelRows(holeCount, [&](int first, int last) {
        for (int i = first; i < last; ++i) {
            const ipr::Point2i p = level.holes[size_t(i)];
            ipr::Vec3f sum(0, 0, 0);
            int count = 0;
            for (int dy = -patchHalf; dy <= patchHalf; ++dy) {
                for (int dx = -patchHalf; dx <= patchHalf; ++dx) {
                    const ipr::Point2i q = p + ipr::Point2i(dx, dy);
                    if (q.x < 0 || q.y < 0 || q.x >= level.holeIndex.width() || q.y >= level.holeIndex.height())
                        continue;
                    const int index = readAt<int>(level.holeIndex, q);
                    if (index < 0)
                        continue;
                    // The pixel of that patch that falls on p; a source's patch is entirely known.
                    sum += readAt<ipr::Vec3f>(level.colour, level.field[size_t(index)] - ipr::Point2i(dx, dy));
                    ++count;
                }
            }
            voted[size_t(i)] = count > 0 ? sum * (1.0f / float(count)) : readAt<ipr::Vec3f>(level.colour, p);
        }
    });
    for (int i = 0; i < holeCount; ++i)
        writeAt<ipr::Vec3f>(level.colour, level.holes[size_t(i)]) = voted[size_t(i)];
}

// The coarser level's result as a start for this one.
void inherit(Level &fine, const Level &coarse)
{
    const ipr::Image enlarged = ipr::resized(coarse.colour, fine.colour.size(), ipr::Interpolation::Linear);
    fine.field.resize(fine.holes.size());
    const int sourceCount = int(fine.sources.size());
    for (size_t i = 0; i < fine.holes.size(); ++i) {
        const ipr::Point2i p = fine.holes[i];
        writeAt<ipr::Vec3f>(fine.colour, p) = readAt<ipr::Vec3f>(enlarged, p);
        const ipr::Point2i parent(p.x / 2, p.y / 2);
        ipr::Point2i candidate(-1, -1);
        const int index = readAt<int>(coarse.holeIndex, parent);
        if (index >= 0)
            candidate = coarse.field[size_t(index)] * 2 + (p - parent * 2);
        const bool valid = candidate.x >= 0 && candidate.y >= 0 && candidate.x < fine.usable.width() && candidate.y < fine.usable.height() &&
                           readAt<std::uint8_t>(fine.usable, candidate);
        std::uint32_t state = std::uint32_t(i) * 2654435761u + 7u;
        fine.field[i] = valid ? candidate : fine.sources[size_t(nextRandom(state) % unsigned(sourceCount))];
    }
}

// For each cell, the way to the nearest known cell, by two sweeps of the sequential signed
// Euclidean distance: exact enough for a transition, and linear in the image's size.
ipr::Image waysToKnown(const ipr::Image &known)
{
    constexpr int far = 16000;
    ipr::Image ways = ipr::Image::create(known.size(), ipr::Depth::S16, 2);
    for (int y = 0; y < known.height(); ++y) {
        const std::uint8_t *in = known.row<std::uint8_t>(y);
        ipr::Vec2s *out = ways.mutableRow<ipr::Vec2s>(y);
        for (int x = 0; x < known.width(); ++x)
            out[x] = in[x] ? ipr::Vec2s(0, 0) : ipr::Vec2s(far, far);
    }
    const auto length = [](const ipr::Vec2s &way) { return int(way[0]) * way[0] + int(way[1]) * way[1]; };
    const auto compare = [&](ipr::Vec2s &here, int x, int y, int dx, int dy) {
        if (x + dx < 0 || y + dy < 0 || x + dx >= ways.width() || y + dy >= ways.height())
            return;
        const ipr::Vec2s &there = readAt<ipr::Vec2s>(ways, y + dy, x + dx);
        const ipr::Vec2s candidate(short(std::clamp(there[0] + dx, -far, far)), short(std::clamp(there[1] + dy, -far, far)));
        if (length(candidate) < length(here))
            here = candidate;
    };
    for (int y = 0; y < ways.height(); ++y) {
        ipr::Vec2s *row = ways.mutableRow<ipr::Vec2s>(y);
        for (int x = 0; x < ways.width(); ++x) {
            compare(row[x], x, y, -1, 0);
            compare(row[x], x, y, 0, -1);
            compare(row[x], x, y, -1, -1);
            compare(row[x], x, y, 1, -1);
        }
        for (int x = ways.width() - 1; x >= 0; --x)
            compare(row[x], x, y, 1, 0);
    }
    for (int y = ways.height() - 1; y >= 0; --y) {
        ipr::Vec2s *row = ways.mutableRow<ipr::Vec2s>(y);
        for (int x = ways.width() - 1; x >= 0; --x) {
            compare(row[x], x, y, 1, 0);
            compare(row[x], x, y, 0, 1);
            compare(row[x], x, y, 1, 1);
            compare(row[x], x, y, -1, 1);
        }
        for (int x = 0; x < ways.width(); ++x)
            compare(row[x], x, y, -1, 0);
    }
    return ways;
}

inline ipr::Vec3f bilinearAt(const ipr::Image &image, float x, float y)
{
    const float cx = std::clamp(x, 0.0f, float(image.width() - 1)), cy = std::clamp(y, 0.0f, float(image.height() - 1));
    const int x0 = int(cx), y0 = int(cy), x1 = std::min(x0 + 1, image.width() - 1), y1 = std::min(y0 + 1, image.height() - 1);
    const float fx = cx - float(x0), fy = cy - float(y0);
    const ipr::Vec3f top = readAt<ipr::Vec3f>(image, y0, x0) * (1.0f - fx) + readAt<ipr::Vec3f>(image, y0, x1) * fx;
    const ipr::Vec3f bottom = readAt<ipr::Vec3f>(image, y1, x0) * (1.0f - fx) + readAt<ipr::Vec3f>(image, y1, x1) * fx;
    return top * (1.0f - fy) + bottom * fy;
}

} // namespace

Report fill(ipr::Image &image, const ipr::Image &known, const Options &options, const std::function<bool()> &cancelled,
            const std::function<void(int done, int total)> &progress)
{
    Report report;
    if (image.empty() || image.depth() != ipr::Depth::F32 || image.channels() != 3 || known.size() != image.size() ||
        known.depth() != ipr::Depth::U8 || known.channels() != 1)
        return report;
    const double wholeArea = double(image.size().area());
    const double knownCount = double(ipr::countNonZero(known));
    report.filledShare = 1.0 - knownCount / wholeArea;
    if (knownCount >= wholeArea)
        return report;
    if (knownCount < options.minimumKnown * wholeArea) {
        report.outcome = Outcome::TooLittleKnown;
        return report;
    }
    const auto stopped = [&] { return cancelled && cancelled(); };

    // The pyramid, from the working size down; each level's colours keep to known pixels.
    ipr::Image perceptual = ipr::Image::create(image.size(), ipr::Depth::F32, 3);
    ipr::parallelRows(image.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const ipr::Vec3f *in = image.row<ipr::Vec3f>(y);
            ipr::Vec3f *out = perceptual.mutableRow<ipr::Vec3f>(y);
            for (int x = 0; x < image.width(); ++x)
                out[x] = ipr::Vec3f(encoded(in[x][0]), encoded(in[x][1]), encoded(in[x][2]));
        }
    });
    const int factor = std::max(1, int(std::ceil(double(std::max(image.width(), image.height())) / options.workingSide)));
    std::vector<Level> levels(1);
    if (factor > 1) {
        reduced(perceptual, known, factor, levels[0].colour, levels[0].known);
    } else {
        levels[0].colour = std::move(perceptual);
        levels[0].known = known.clone();
    }
    while (std::min(levels.back().colour.height(), levels.back().colour.width()) / 2 >= smallestSide) {
        Level next;
        reduced(levels.back().colour, levels.back().known, 2, next.colour, next.known);
        levels.push_back(std::move(next));
    }
    // What stands out is no source, as long as that leaves enough to take from.
    ipr::Image banned = objectsOf(levels.front());
    {
        const double knownCells = double(ipr::countNonZero(levels.front().known));
        const ipr::Image allowed = ipr::bitwiseAnd(ipr::bitwiseNot(banned), levels.front().known);
        if (double(ipr::countNonZero(allowed)) < fewestSourcesKept * knownCells)
            banned = {};
    }
    const ipr::Image objectCells = banned.empty() ? ipr::Image() : banned.clone();
    // A level without a patch to take cannot search: the pyramid stops above it.
    for (Level &level : levels) {
        describe(level, banned);
        // What a coarser cell holds of a banned one bans it too.
        if (!banned.empty()) {
            ipr::Image coarser = ipr::Image::zeros(ipr::Size((banned.width() + 1) / 2, (banned.height() + 1) / 2), ipr::Depth::U8, 1);
            for (int y = 0; y < banned.height(); ++y) {
                const std::uint8_t *in = banned.row<std::uint8_t>(y);
                std::uint8_t *out = coarser.mutableRow<std::uint8_t>(y / 2);
                for (int x = 0; x < banned.width(); ++x)
                    if (in[x])
                        out[x / 2] = 255;
            }
            banned = std::move(coarser);
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
    ipr::Image workingLinear = ipr::Image::create(working.colour.size(), ipr::Depth::F32, 3);
    for (int y = 0; y < workingLinear.height(); ++y) {
        const ipr::Vec3f *in = working.colour.row<ipr::Vec3f>(y);
        ipr::Vec3f *out = workingLinear.mutableRow<ipr::Vec3f>(y);
        for (int x = 0; x < workingLinear.width(); ++x)
            out[x] = ipr::Vec3f(decoded(in[x][0]), decoded(in[x][1]), decoded(in[x][2]));
    }
    const float toCell = 1.0f / float(factor);
    // Across the limit with the real image, its texture carries on, mirrored, and fades into
    // what was found: an invented texture meeting a real one would draw the limit as a line.
    constexpr int waysDivisor = 2;
    ipr::Image reducedKnown = ipr::filled(ipr::Size(known.width() / waysDivisor + 1, known.height() / waysDivisor + 1), ipr::Depth::U8, 1, 255);
    for (int y = 0; y < known.height(); ++y) {
        const std::uint8_t *in = known.row<std::uint8_t>(y);
        std::uint8_t *out = reducedKnown.mutableRow<std::uint8_t>(y / waysDivisor);
        for (int x = 0; x < known.width(); ++x)
            if (!in[x])
                out[x / waysDivisor] = 0;
    }
    const ipr::Image ways = waysToKnown(reducedKnown);
    const float transition = float(std::max(1, options.transitionPixels));
    // The way to the nearest pixel jumps from one to the next: mirrored as it is, the texture
    // would break up into a grid. Smoothed, it folds the image in one piece.
    ipr::Image smoothWays = ipr::Image::create(ways.size(), ipr::Depth::F32, 2);
    ipr::convertInto(ways, smoothWays);
    const float longestWay = 2.0f * transition / float(waysDivisor);
    for (int y = 0; y < smoothWays.height(); ++y) {
        ipr::Vec2f *row = smoothWays.mutableRow<ipr::Vec2f>(y);
        for (int x = 0; x < smoothWays.width(); ++x) {
            const float length = std::hypot(row[x][0], row[x][1]);
            if (length > longestWay)
                row[x] *= longestWay / length;
        }
    }
    const double waySmoothing = std::max(1.0, double(transition) / 8.0);
    smoothWays = ipr::gaussianBlurred(smoothWays, waySmoothing, ipr::BlurBorder::LastPixel);
    // Written through these rows, read through the image: only the missing pixels change, and
    // only known ones are read.
    std::vector<ipr::Vec3f *> outRows(static_cast<size_t>(image.height()));
    for (int y = 0; y < image.height(); ++y)
        outRows[size_t(y)] = image.mutableRow<ipr::Vec3f>(y);
    const ipr::Image &source = image;
    ipr::parallelRows(image.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const std::uint8_t *inKnown = known.row<std::uint8_t>(y);
            ipr::Vec3f *out = outRows[size_t(y)];
            const float cellY = (float(y) + 0.5f) * toCell - 0.5f;
            for (int x = 0; x < image.width(); ++x) {
                if (inKnown[x])
                    continue;
                const float cellX = (float(x) + 0.5f) * toCell - 0.5f;
                ipr::Vec3f value = bilinearAt(workingLinear, cellX, cellY);
                if (factor > 1) {
                    const int cx0 = std::clamp(int(std::floor(cellX)), 0, workingLinear.width() - 1), cy0 = std::clamp(int(std::floor(cellY)), 0, workingLinear.height() - 1);
                    const float fx = std::clamp(cellX - float(cx0), 0.0f, 1.0f), fy = std::clamp(cellY - float(cy0), 0.0f, 1.0f);
                    ipr::Vec3f detail(0, 0, 0);
                    float weightSum = 0.0f;
                    for (int j = 0; j < 2; ++j) {
                        for (int i = 0; i < 2; ++i) {
                            const int cx = std::min(cx0 + i, workingLinear.width() - 1), cy = std::min(cy0 + j, workingLinear.height() - 1);
                            const int index = readAt<int>(working.holeIndex, cy, cx);
                            if (index < 0)
                                continue;
                            const ipr::Point2i from = working.field[size_t(index)];
                            const int sx = x + (from.x - cx) * factor, sy = y + (from.y - cy) * factor;
                            if (sx < 0 || sy < 0 || sx >= image.width() || sy >= image.height() || !readAt<std::uint8_t>(known, sy, sx))
                                continue;
                            const float weight = (i == 0 ? 1.0f - fx : fx) * (j == 0 ? 1.0f - fy : fy);
                            const ipr::Vec3f here = readAt<ipr::Vec3f>(source, sy, sx) - bilinearAt(workingLinear, (float(sx) + 0.5f) * toCell - 0.5f, (float(sy) + 0.5f) * toCell - 0.5f);
                            detail += here * weight;
                            weightSum += weight;
                        }
                    }
                    if (weightSum > 1e-3f)
                        value += detail * (1.0f / weightSum);
                }
                const ipr::Vec2s way = readAt<ipr::Vec2s>(ways, y / waysDivisor, x / waysDivisor);
                const float distance = std::hypot(float(way[0]), float(way[1])) * waysDivisor;
                if (distance < transition) {
                    const ipr::Vec2f fold = readAt<ipr::Vec2f>(smoothWays, y / waysDivisor, x / waysDivisor) * float(waysDivisor);
                    const int mirrorX = x + int(std::lround(2.0f * fold[0])), mirrorY = y + int(std::lround(2.0f * fold[1]));
                    const bool onObject = !objectCells.empty() && mirrorX >= 0 && mirrorY >= 0 && mirrorX < image.width() && mirrorY < image.height() &&
                                          readAt<std::uint8_t>(objectCells, mirrorY / factor, mirrorX / factor);
                    if (!onObject && mirrorX >= 0 && mirrorY >= 0 && mirrorX < image.width() && mirrorY < image.height() && readAt<std::uint8_t>(known, mirrorY, mirrorX)) {
                        const float t = distance / transition;
                        const float invented = t * t * (3.0f - 2.0f * t);
                        // Measured on the slow variations of both places.
                        const ipr::Vec3f here = bilinearAt(working.colour, cellX, cellY);
                        const ipr::Vec3f there = bilinearAt(working.colour, (float(mirrorX) + 0.5f) * toCell - 0.5f, (float(mirrorY) + 0.5f) * toCell - 0.5f);
                        const ipr::Vec3f gap = here - there;
                        const float unlike = std::clamp((std::sqrt(gap.dot(gap) / 3.0f) - mirrorLikeFrom) / (mirrorLikeTo - mirrorLikeFrom), 0.0f, 1.0f);
                        const float mirrored = (1.0f - invented) * (1.0f - unlike * unlike * (3.0f - 2.0f * unlike));
                        value = value * (1.0f - mirrored) + readAt<ipr::Vec3f>(source, mirrorY, mirrorX) * mirrored;
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
