#include "PanoramaGeometry.h"

#include "Analysis.h"
#include "Parallel.h"
#include "Pixels.h"
#include "Warp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>

namespace PanoramaGeometry {
namespace {

constexpr int adjustmentIterations = 40;
constexpr double huberPixels = 3.0;
constexpr double largestDistortion = 0.15;
constexpr double knownFocalTolerance = 0.07; // of a focal length the files give: format and breathing
constexpr int refinedHalf = 12;          // of a measured block, pixels of the finer image
constexpr int refinedRadius = 5;
constexpr double refinedScore = 0.7;
constexpr double refinedFlat = 1e-3;
constexpr double rowGapOfField = 0.25;   // between two rows, of the field across
constexpr double bendPull = 0.002;       // against the spread of the bottom edges along their arc
constexpr double spanFlatLimit = 50.0 * ipr::pi / 180.0;    // along, for a flat projection by default
constexpr double spanSphereLimit = 60.0 * ipr::pi / 180.0;  // across, beyond which a sphere is needed
constexpr double spanFlatRefused = 150.0 * ipr::pi / 180.0; // no flat projection can hold more

ipr::Point2d apply(const ipr::Matrix33d &h, const ipr::Point2d &p)
{
    const double w = h(2, 0) * p.x + h(2, 1) * p.y + h(2, 2);
    return {(h(0, 0) * p.x + h(0, 1) * p.y + h(0, 2)) / w, (h(1, 0) * p.x + h(1, 1) * p.y + h(1, 2)) / w};
}

// Least squares through the points, with the conditioning of Hartley's normalization.
bool fitHomography(const std::vector<ipr::Point2d> &a, const std::vector<ipr::Point2d> &b, ipr::Matrix33d &result)
{
    if (a.size() < 4 || a.size() != b.size())
        return false;
    const auto normalizer = [](const std::vector<ipr::Point2d> &points) {
        ipr::Point2d mean(0, 0);
        for (const ipr::Point2d &p : points)
            mean += p;
        mean *= 1.0 / double(points.size());
        double spread = 0.0;
        for (const ipr::Point2d &p : points)
            spread += ipr::length(p - mean);
        spread = std::max(1e-9, spread / double(points.size()));
        const double s = std::sqrt(2.0) / spread;
        return ipr::Matrix33d(s, 0, -s * mean.x, 0, s, -s * mean.y, 0, 0, 1);
    };
    const ipr::Matrix33d ta = normalizer(a), tb = normalizer(b);
    const int rows = int(a.size()) * 2;
    std::vector<double> system(size_t(rows) * 9);
    for (int i = 0; i < int(a.size()); ++i) {
        const ipr::Point2d p = apply(ta, a[size_t(i)]), q = apply(tb, b[size_t(i)]);
        double *u = &system[size_t(2 * i) * 9], *v = &system[size_t(2 * i + 1) * 9];
        const double rowU[9] = {p.x, p.y, 1, 0, 0, 0, -q.x * p.x, -q.x * p.y, -q.x};
        const double rowV[9] = {0, 0, 0, p.x, p.y, 1, -q.y * p.x, -q.y * p.y, -q.y};
        std::copy(rowU, rowU + 9, u);
        std::copy(rowV, rowV + 9, v);
    }
    // The normal equations, system transposed times system.
    std::vector<double> normal(81, 0.0), values, vectors;
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 9; ++c) {
            double sum = 0.0;
            for (int k = 0; k < rows; ++k)
                sum += system[size_t(k) * 9 + size_t(r)] * system[size_t(k) * 9 + size_t(c)];
            normal[size_t(r) * 9 + size_t(c)] = sum;
        }
    if (!ipr::symmetricEigen(normal, 9, values, vectors))
        return false;
    const double *h = &vectors[8 * 9];
    const ipr::Matrix33d fitted(h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8]);
    result = tb.inv() * fitted * ta;
    if (std::abs(result(2, 2)) < 1e-12)
        return false;
    result = result * (1.0 / result(2, 2));
    return true;
}

ipr::Matrix33d exponential(double x, double y, double z)
{
    const double angle = std::sqrt(x * x + y * y + z * z);
    const ipr::Matrix33d skew(0, -z, y, z, 0, -x, -y, x, 0);
    const double a = angle < 1e-8 ? 1.0 : std::sin(angle) / angle;
    const double b = angle < 1e-8 ? 0.5 : (1.0 - std::cos(angle)) / (angle * angle);
    return ipr::Matrix33d::identity() + skew * a + skew * skew * b;
}

// The lens: an ideal point (x, y) lands at (x, y) * (1 + k r^2), r relative to the corner.
struct Lens
{
    double focal = 1.0;
    double distortion = 0.0;
    double inverseCorner = 0.0; // 1 / squared half-diagonal
};

Lens lensOf(const ipr::Size &frame, double focal, double distortion)
{
    return {focal, distortion, 4.0 / (double(frame.width) * frame.width + double(frame.height) * frame.height)};
}

inline ipr::Point2d distorted(const ipr::Point2d &p, const Lens &lens)
{
    const double factor = 1.0 + lens.distortion * lens.inverseCorner * (p.x * p.x + p.y * p.y);
    return {p.x * factor, p.y * factor};
}

inline ipr::Point2d undistorted(const ipr::Point2d &p, const Lens &lens)
{
    ipr::Point2d ideal = p;
    for (int step = 0; step < 4; ++step) {
        const double factor = 1.0 + lens.distortion * lens.inverseCorner * (ideal.x * ideal.x + ideal.y * ideal.y);
        ideal = {p.x / factor, p.y / factor};
    }
    return ideal;
}

inline ipr::Point2d predicted(const ipr::Point2d &p, const ipr::Matrix33d &ra, const ipr::Matrix33d &rb, const Lens &lens)
{
    const ipr::Point2d ideal = undistorted(p, lens);
    const ipr::Vec3d ray = ra.t() * ipr::Vec3d(ideal.x / lens.focal, ideal.y / lens.focal, 1.0);
    const ipr::Vec3d seen = rb * ray;
    return distorted({lens.focal * seen[0] / seen[2], lens.focal * seen[1] / seen[2]}, lens);
}

// What one adjustment works on, with a light share of each pair's points.
struct Bundle
{
    std::vector<int> slot;                    // per frame: its rotation's place in the unknowns, -1 when held
    int unknowns = 0;                         // 3 per free frame + the focal + the distortion
};

double pairError(const Pair &pair, const std::vector<ipr::Matrix33d> &rotations, const Lens &lens)
{
    double sum = 0.0;
    int count = 0;
    for (size_t i = 0; i < pair.pointsA.size(); ++i) {
        const ipr::Point2d d1 = predicted(pair.pointsA[i], rotations[size_t(pair.a)], rotations[size_t(pair.b)], lens) - pair.pointsB[i];
        const ipr::Point2d d2 = predicted(pair.pointsB[i], rotations[size_t(pair.b)], rotations[size_t(pair.a)], lens) - pair.pointsA[i];
        sum += d1.dot(d1) + d2.dot(d2);
        count += 4;
    }
    return count > 0 ? std::sqrt(sum / count) : 0.0;
}

// Levenberg-Marquardt on the rotations, the focal and the distortion; the first member stays
// as it is.
double adjust(const std::vector<Pair> &pairs, const std::vector<int> &members, int frameCount, int maximumPoints,
              std::vector<ipr::Matrix33d> &rotations, Lens &lens, const LensKnowledge &known, const std::function<bool()> &cancelled)
{
    const double focalLow = known.focalPixels > 0.0 ? known.focalPixels * (1.0 - knownFocalTolerance) : 0.0;
    const double focalHigh = known.focalPixels > 0.0 ? known.focalPixels * (1.0 + knownFocalTolerance) : std::numeric_limits<double>::max();
    const double distortionLimit = known.distortionCorrected ? 0.0 : largestDistortion;
    Bundle bundle;
    bundle.slot.assign(size_t(frameCount), -1);
    for (size_t i = 1; i < members.size(); ++i)
        bundle.slot[size_t(members[i])] = int(i - 1) * 3;
    const int focalSlot = int(members.size() - 1) * 3, distortionSlot = focalSlot + 1;
    bundle.unknowns = distortionSlot + 1;

    const auto cost = [&](const std::vector<ipr::Matrix33d> &r, const Lens &l) {
        double sum = 0.0;
        for (const Pair &pair : pairs) {
            const int stride = std::max(1, int(pair.pointsA.size()) / maximumPoints);
            for (size_t i = 0; i < pair.pointsA.size(); i += size_t(stride)) {
                for (const ipr::Point2d &d : {predicted(pair.pointsA[i], r[size_t(pair.a)], r[size_t(pair.b)], l) - pair.pointsB[i],
                                             predicted(pair.pointsB[i], r[size_t(pair.b)], r[size_t(pair.a)], l) - pair.pointsA[i]}) {
                    const double length = std::sqrt(d.dot(d));
                    sum += length <= huberPixels ? length * length : huberPixels * (2.0 * length - huberPixels);
                }
            }
        }
        return sum;
    };

    double damping = 1e-3, current = cost(rotations, lens);
    constexpr double step = 1e-6;
    for (int iteration = 0; iteration < adjustmentIterations; ++iteration) {
        if (cancelled && cancelled())
            break;
        const size_t unknowns = size_t(bundle.unknowns);
        std::vector<double> normal(unknowns * unknowns, 0.0), gradient(unknowns, 0.0);
        Lens longer = lens, bent = lens;
        longer.focal = lens.focal * (1.0 + step);
        bent.distortion = lens.distortion + step;
        for (const Pair &pair : pairs) {
            const int stride = std::max(1, int(pair.pointsA.size()) / maximumPoints);
            const int slotA = bundle.slot[size_t(pair.a)], slotB = bundle.slot[size_t(pair.b)];
            const ipr::Matrix33d ra = rotations[size_t(pair.a)], rb = rotations[size_t(pair.b)];
            for (size_t i = 0; i < pair.pointsA.size(); i += size_t(stride)) {
                for (int direction = 0; direction < 2; ++direction) {
                    const ipr::Point2d &from = direction == 0 ? pair.pointsA[i] : pair.pointsB[i];
                    const ipr::Point2d &to = direction == 0 ? pair.pointsB[i] : pair.pointsA[i];
                    const auto project = [&](const ipr::Matrix33d &a, const ipr::Matrix33d &b, const Lens &l) {
                        return direction == 0 ? predicted(from, a, b, l) : predicted(from, b, a, l);
                    };
                    const ipr::Point2d base = project(ra, rb, lens);
                    const ipr::Point2d residual = base - to;
                    const double length = std::sqrt(residual.dot(residual));
                    const double weight = length <= huberPixels ? 1.0 : huberPixels / length;
                    // Each residual depends on two rotations and the lens only.
                    double dx[8], dy[8];
                    int place[8];
                    int used = 0;
                    const auto record = [&](const ipr::Point2d &moved, int slot) {
                        dx[used] = (moved.x - base.x) / step;
                        dy[used] = (moved.y - base.y) / step;
                        place[used++] = slot;
                    };
                    for (int axis = 0; axis < 3; ++axis) {
                        const ipr::Matrix33d turn = exponential(axis == 0 ? step : 0, axis == 1 ? step : 0, axis == 2 ? step : 0);
                        if (slotA >= 0)
                            record(project(turn * ra, rb, lens), slotA + axis);
                        if (slotB >= 0)
                            record(project(ra, turn * rb, lens), slotB + axis);
                    }
                    record(project(ra, rb, longer), focalSlot); // per relative change of the focal
                    record(project(ra, rb, bent), distortionSlot);
                    for (int m = 0; m < used; ++m) {
                        gradient[size_t(place[m])] += weight * (dx[m] * residual.x + dy[m] * residual.y);
                        for (int n = 0; n < used; ++n)
                            normal[size_t(place[m]) * unknowns + size_t(place[n])] += weight * (dx[m] * dx[n] + dy[m] * dy[n]);
                    }
                }
            }
        }
        bool improved = false;
        const double previous = current;
        for (int attempt = 0; attempt < 8 && !improved; ++attempt) {
            std::vector<double> damped = normal, downhill(unknowns), change;
            for (size_t k = 0; k < unknowns; ++k) {
                damped[k * unknowns + k] += damping * std::max(normal[k * unknowns + k], 1e-9);
                downhill[k] = -gradient[k];
            }
            if (!ipr::solveCholesky(damped, downhill, bundle.unknowns, change)) {
                damping *= 10.0;
                continue;
            }
            std::vector<ipr::Matrix33d> trial = rotations;
            for (size_t i = 1; i < members.size(); ++i) {
                const int slot = bundle.slot[size_t(members[i])];
                trial[size_t(members[i])] = exponential(change[size_t(slot)], change[size_t(slot + 1)], change[size_t(slot + 2)]) * rotations[size_t(members[i])];
            }
            Lens trialLens = lens;
            trialLens.focal = std::clamp(lens.focal * std::exp(std::clamp(change[size_t(focalSlot)], -0.5, 0.5)), focalLow, focalHigh);
            trialLens.distortion = std::clamp(lens.distortion + change[size_t(distortionSlot)], -distortionLimit, distortionLimit);
            const double trialCost = cost(trial, trialLens);
            if (trialCost < current) {
                rotations = std::move(trial);
                lens = trialLens;
                current = trialCost;
                damping = std::max(1e-9, damping * 0.3);
                improved = true;
            } else {
                damping *= 10.0;
            }
        }
        if (!improved || previous - current < 1e-7 * previous)
            break;
    }
    return current;
}

// The frames connected to the best one, each joined to an earlier member by its strongest pair.
std::vector<int> spanningOrder(int frameCount, const std::vector<Pair> &pairs, std::vector<int> &parentPair)
{
    std::vector<int> group(static_cast<size_t>(frameCount));
    std::iota(group.begin(), group.end(), 0);
    const std::function<int(int)> find = [&](int x) { return group[size_t(x)] == x ? x : group[size_t(x)] = find(group[size_t(x)]); };
    for (const Pair &pair : pairs)
        group[size_t(find(pair.a))] = find(pair.b);
    std::vector<int> sizes(size_t(frameCount), 0);
    for (int i = 0; i < frameCount; ++i)
        ++sizes[size_t(find(i))];
    int bestGroup = int(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());

    std::vector<long> strength(size_t(frameCount), 0);
    for (const Pair &pair : pairs) {
        strength[size_t(pair.a)] += long(pair.pointsA.size());
        strength[size_t(pair.b)] += long(pair.pointsA.size());
    }
    int start = -1;
    for (int i = 0; i < frameCount; ++i)
        if (find(i) == bestGroup && (start < 0 || strength[size_t(i)] > strength[size_t(start)]))
            start = i;

    std::vector<int> order;
    parentPair.assign(size_t(frameCount), -1);
    if (start < 0)
        return order;
    std::vector<char> inside(size_t(frameCount), 0);
    inside[size_t(start)] = 1;
    order.push_back(start);
    for (;;) {
        int bestPair = -1;
        size_t bestCount = 0;
        for (size_t p = 0; p < pairs.size(); ++p) {
            if (inside[size_t(pairs[p].a)] == inside[size_t(pairs[p].b)])
                continue;
            if (pairs[p].pointsA.size() > bestCount) {
                bestCount = pairs[p].pointsA.size();
                bestPair = int(p);
            }
        }
        if (bestPair < 0)
            break;
        const int added = inside[size_t(pairs[size_t(bestPair)].a)] ? pairs[size_t(bestPair)].b : pairs[size_t(bestPair)].a;
        inside[size_t(added)] = 1;
        parentPair[size_t(added)] = bestPair;
        order.push_back(added);
    }
    return order;
}

std::vector<double> focalSeeds(const ipr::Size &frame, const LensKnowledge &known)
{
    if (known.focalPixels > 0.0)
        return {known.focalPixels};
    const double side = std::max(frame.width, frame.height);
    return {side * 1.0, side * 2.0, side * 4.0};
}

} // namespace

bool verifyPair(const std::vector<PanoramaFeatures::Correspondence> &correspondences, const ipr::Size &frame,
                const PairOptions &options, Pair &pair)
{
    const int count = int(correspondences.size());
    if (count < options.minimumInliers)
        return false;
    const ipr::Point2d centre((frame.width - 1) * 0.5, (frame.height - 1) * 0.5);
    std::vector<ipr::Point2d> a(static_cast<size_t>(count)), b(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        a[size_t(i)] = ipr::Point2d(correspondences[size_t(i)].a) - centre;
        b[size_t(i)] = ipr::Point2d(correspondences[size_t(i)].b) - centre;
    }
    const auto inliersOf = [&](const ipr::Matrix33d &h, std::vector<int> *kept) {
        int found = 0;
        for (int i = 0; i < count; ++i) {
            const ipr::Point2d d = apply(h, a[size_t(i)]) - b[size_t(i)];
            if (d.dot(d) <= options.inlierPixels * options.inlierPixels) {
                ++found;
                if (kept)
                    kept->push_back(i);
            }
        }
        return found;
    };
    std::mt19937 random(0x1a2b3c);
    ipr::Matrix33d best = ipr::Matrix33d::identity();
    int bestCount = 0;
    for (int iteration = 0; iteration < options.iterations; ++iteration) {
        std::array<int, 4> pick{};
        bool distinct = true;
        for (int k = 0; k < 4; ++k) {
            pick[size_t(k)] = int(random() % unsigned(count));
            for (int l = 0; l < k; ++l)
                distinct = distinct && pick[size_t(l)] != pick[size_t(k)];
        }
        if (!distinct)
            continue;
        std::vector<ipr::Point2d> sampleA, sampleB;
        for (const int index : pick) {
            sampleA.push_back(a[size_t(index)]);
            sampleB.push_back(b[size_t(index)]);
        }
        ipr::Matrix33d h;
        if (!fitHomography(sampleA, sampleB, h))
            continue;
        const int found = inliersOf(h, nullptr);
        if (found > bestCount) {
            bestCount = found;
            best = h;
        }
    }
    if (bestCount < options.minimumInliers)
        return false;
    // A real overlap explains most of what falls inside it (Brown and Lowe's criterion).
    int inOverlap = 0;
    for (int i = 0; i < count; ++i) {
        const ipr::Point2d p = apply(best, a[size_t(i)]);
        if (std::abs(p.x) <= frame.width * 0.5 && std::abs(p.y) <= frame.height * 0.5)
            ++inOverlap;
    }
    if (double(bestCount) < 8.0 + 0.3 * inOverlap)
        return false;

    std::vector<int> kept;
    inliersOf(best, &kept);
    std::vector<ipr::Point2d> inA, inB;
    for (const int i : kept) {
        inA.push_back(a[size_t(i)]);
        inB.push_back(b[size_t(i)]);
    }
    ipr::Matrix33d fitted;
    if (fitHomography(inA, inB, fitted)) {
        std::vector<int> again;
        inliersOf(fitted, &again);
        if (int(again.size()) >= bestCount) {
            best = fitted;
            inA.clear();
            inB.clear();
            for (const int i : again) {
                inA.push_back(a[size_t(i)]);
                inB.push_back(b[size_t(i)]);
            }
        }
    }
    const auto spannedArea = [](const std::vector<ipr::Point2d> &points) {
        double left = points[0].x, right = left, top = points[0].y, bottom = top;
        for (const ipr::Point2d &p : points) {
            left = std::min(left, p.x);
            right = std::max(right, p.x);
            top = std::min(top, p.y);
            bottom = std::max(bottom, p.y);
        }
        return (right - left) * (bottom - top);
    };
    const double wholeArea = double(frame.area());
    if (spannedArea(inA) < options.minimumArea * wholeArea || spannedArea(inB) < options.minimumArea * wholeArea)
        return false;
    pair.aToB = best;
    pair.pointsA = std::move(inA);
    pair.pointsB = std::move(inB);
    return true;
}

Solution solve(int frameCount, std::vector<Pair> pairs, const ipr::Size &frame, const LensKnowledge &known, const SolveOptions &options,
               const std::function<bool()> &cancelled)
{
    Solution best;
    if (frameCount < 2 || pairs.empty())
        return best;
    // A pair that the others contradict leaves, and the rest is adjusted again.
    for (;;) {
        std::vector<int> parentPair;
        const std::vector<int> members = spanningOrder(frameCount, pairs, parentPair);
        if (members.size() < 2)
            return best;
        std::vector<char> inside(size_t(frameCount), 0);
        for (const int m : members)
            inside[size_t(m)] = 1;
        std::vector<Pair> usable;
        for (const Pair &pair : pairs)
            if (inside[size_t(pair.a)] && inside[size_t(pair.b)])
                usable.push_back(pair);

        Solution attempt;
        double bestCost = -1.0;
        for (const double seed : focalSeeds(frame, known)) {
            const ipr::Matrix33d k(seed, 0, 0, 0, seed, 0, 0, 0, 1), inverseK(1.0 / seed, 0, 0, 0, 1.0 / seed, 0, 0, 0, 1);
            std::vector<ipr::Matrix33d> rotations(static_cast<size_t>(frameCount), ipr::Matrix33d::identity());
            for (size_t i = 1; i < members.size(); ++i) {
                const Pair &link = pairs[size_t(parentPair[size_t(members[i])])];
                const ipr::Matrix33d relative = nearestRotation(inverseK * link.aToB * k); // from a to b
                if (link.b == members[i])
                    rotations[size_t(link.b)] = relative * rotations[size_t(link.a)];
                else
                    rotations[size_t(link.a)] = relative.t() * rotations[size_t(link.b)];
            }
            Lens lens = lensOf(frame, seed, 0.0);
            const double cost = adjust(usable, members, frameCount, options.maximumPoints, rotations, lens, known, cancelled);
            if (cancelled && cancelled())
                return best;
            if (bestCost < 0.0 || cost < bestCost) {
                bestCost = cost;
                attempt.focal = lens.focal;
                attempt.distortion = lens.distortion;
                attempt.worldToCamera = std::move(rotations);
            }
        }
        attempt.members = members;
        attempt.pairs = usable;
        double sum = 0.0, weight = 0.0;
        int worst = -1;
        const Lens found = lensOf(frame, attempt.focal, attempt.distortion);
        for (size_t p = 0; p < attempt.pairs.size(); ++p) {
            Pair &pair = attempt.pairs[p];
            pair.residual = pairError(pair, attempt.worldToCamera, found);
            sum += pair.residual * pair.residual * double(pair.pointsA.size());
            weight += double(pair.pointsA.size());
            if (worst < 0 || pair.residual > attempt.pairs[size_t(worst)].residual)
                worst = int(p);
        }
        attempt.residual = weight > 0.0 ? std::sqrt(sum / weight) : 0.0;
        attempt.worstPair = worst >= 0 ? attempt.pairs[size_t(worst)].residual : 0.0;
        if (attempt.worstPair <= options.dropResidual || attempt.pairs.size() < 2) {
            attempt.valid = attempt.worstPair <= options.dropResidual;
            return attempt;
        }
        const Pair &bad = attempt.pairs[size_t(worst)];
        pairs.erase(std::remove_if(pairs.begin(), pairs.end(), [&](const Pair &pair) { return pair.a == bad.a && pair.b == bad.b; }), pairs.end());
        if (pairs.empty())
            return best;
    }
}

void refine(Solution &solution, const std::vector<ipr::Image> &images, double imageScale, const ipr::Size &frame, const LensKnowledge &known,
            const SolveOptions &options, const std::function<bool()> &cancelled)
{
    if (!solution.valid)
        return;
    const Lens lens = lensOf(frame, solution.focal, solution.distortion);
    const ipr::Point2d centre((frame.width - 1) * 0.5, (frame.height - 1) * 0.5);
    // A reduction keeps the pixels' centres, not their indices.
    const auto toImage = [&](const ipr::Point2d &p) {
        return ipr::Point2d((p.x + centre.x + 0.5) * imageScale - 0.5, (p.y + centre.y + 0.5) * imageScale - 0.5);
    };
    const auto fromImage = [&](const ipr::Point2d &p) {
        return ipr::Point2d((p.x + 0.5) / imageScale - 0.5 - centre.x, (p.y + 0.5) / imageScale - 0.5 - centre.y);
    };
    constexpr int templateSide = 2 * refinedHalf + 1;
    constexpr int searchSide = templateSide + 2 * refinedRadius;

    for (Pair &pair : solution.pairs) {
        if (cancelled && cancelled())
            return;
        const ipr::Image &imageA = images[size_t(pair.a)], &imageB = images[size_t(pair.b)];
        if (imageA.empty() || imageB.empty())
            continue;
        const ipr::Matrix33d &ra = solution.worldToCamera[size_t(pair.a)], &rb = solution.worldToCamera[size_t(pair.b)];
        const int total = int(pair.pointsA.size());
        std::vector<ipr::Point2d> refinedA(static_cast<size_t>(total)), refinedB(static_cast<size_t>(total));
        std::vector<char> good(static_cast<size_t>(total), 0);
        ipr::parallelRows(total, [&](int first, int last) {
            ipr::Image mapX = ipr::Image::create({searchSide, searchSide}, ipr::Depth::F32, 1);
            ipr::Image mapY = ipr::Image::create({searchSide, searchSide}, ipr::Depth::F32, 1);
            for (int index = first; index < last; ++index) {
                const ipr::Point2d onA = toImage(pair.pointsA[size_t(index)]);
                const ipr::Point2i seat(int(std::lround(onA.x)), int(std::lround(onA.y)));
                const ipr::Recti block(seat.x - refinedHalf, seat.y - refinedHalf, templateSide, templateSide);
                if ((block & ipr::Recti(0, 0, imageA.width(), imageA.height())) != block)
                    continue;
                const ipr::Image tile = imageA.copy(block);
                std::vector<double> mean, deviation;
                ipr::meanAndDeviationOf(tile, mean, deviation);
                if (deviation[0] < refinedFlat)
                    continue;
                bool inside = true;
                for (int y = 0; y < searchSide && inside; ++y) {
                    float *outX = mapX.mutableRow<float>(y), *outY = mapY.mutableRow<float>(y);
                    for (int x = 0; x < searchSide; ++x) {
                        const ipr::Point2d place(seat.x - refinedHalf - refinedRadius + x, seat.y - refinedHalf - refinedRadius + y);
                        const ipr::Point2d onB = toImage(predicted(fromImage(place), ra, rb, lens));
                        if (onB.x < 1 || onB.y < 1 || onB.x > imageB.width() - 2 || onB.y > imageB.height() - 2) {
                            inside = false;
                            break;
                        }
                        outX[x] = float(onB.x);
                        outY[x] = float(onB.y);
                    }
                }
                if (!inside)
                    continue;
                const ipr::Image search = ipr::remapped(imageB, mapX, mapY, ipr::Resampling::Cubic, ipr::Outside::LastPixel);
                const ipr::Image response = ipr::matchedTemplate(search, tile);
                const ipr::Extremes responsePeak = ipr::extremesOf(response);
                const double score = responsePeak.largest;
                const ipr::Point2i peak = responsePeak.largestAt;
                if (score < refinedScore || peak.x < 1 || peak.y < 1 || peak.x >= response.width() - 1 || peak.y >= response.height() - 1)
                    continue;
                const auto vertex = [](double left, double middle, double right) {
                    const double curve = left - 2.0 * middle + right;
                    return curve < 0.0 ? std::clamp(0.5 * (left - right) / curve, -0.5, 0.5) : 0.0;
                };
                const float *above = response.row<float>(peak.y - 1), *here = response.row<float>(peak.y), *below = response.row<float>(peak.y + 1);
                const double subX = vertex(here[peak.x - 1], here[peak.x], here[peak.x + 1]);
                const double subY = vertex(above[peak.x], here[peak.x], below[peak.x]);
                // The block at this seat of the frame looks like the other frame at that place.
                const ipr::Point2d found(seat.x + peak.x - refinedRadius + subX, seat.y + peak.y - refinedRadius + subY);
                refinedA[size_t(index)] = fromImage(ipr::Point2d(seat));
                refinedB[size_t(index)] = predicted(fromImage(found), ra, rb, lens);
                good[size_t(index)] = 1;
            }
        });
        std::vector<ipr::Point2d> keptA, keptB;
        for (int index = 0; index < total; ++index) {
            if (good[size_t(index)]) {
                keptA.push_back(refinedA[size_t(index)]);
                keptB.push_back(refinedB[size_t(index)]);
            }
        }
        // Too little to trust keeps what the coarser images measured.
        if (int(keptA.size()) >= 8) {
            pair.pointsA = std::move(keptA);
            pair.pointsB = std::move(keptB);
        }
    }
    // Once more on what was measured: a pair the finer images contradict leaves here.
    solution = solve(int(solution.worldToCamera.size()), std::move(solution.pairs), frame, known, options, cancelled);
}

Shape straighten(Solution &solution, const ipr::Size &frame, Projection requested)
{
    Shape shape;
    if (solution.members.empty())
        return shape;
    const auto axisOf = [&](int member, int row) {
        const ipr::Matrix33d &r = solution.worldToCamera[size_t(member)];
        return ipr::Vec3d(r(row, 0), r(row, 1), r(row, 2));
    };
    const auto turnWorld = [&](const ipr::Vec3d &x, const ipr::Vec3d &y, const ipr::Vec3d &z) {
        // The new axes, as columns, in the old world.
        const ipr::Matrix33d turn(x[0], y[0], z[0], x[1], y[1], z[1], x[2], y[2], z[2]);
        for (const int m : solution.members)
            solution.worldToCamera[size_t(m)] = solution.worldToCamera[size_t(m)] * turn;
    };
    const auto meanAxis = [&](int row) {
        ipr::Vec3d sum(0, 0, 0);
        for (const int m : solution.members)
            sum += axisOf(m, row);
        return ipr::unit(sum);
    };

    const ipr::Vec3d meanX = meanAxis(0), meanY = meanAxis(1), meanZ = meanAxis(2);
    double lowX = 0, highX = 0, lowY = 0, highY = 0;
    for (size_t i = 0; i < solution.members.size(); ++i) {
        const ipr::Vec3d z = axisOf(solution.members[i], 2);
        const double angleX = std::asin(std::clamp(z.dot(meanX), -1.0, 1.0)), angleY = std::asin(std::clamp(z.dot(meanY), -1.0, 1.0));
        lowX = i == 0 ? angleX : std::min(lowX, angleX);
        highX = i == 0 ? angleX : std::max(highX, angleX);
        lowY = i == 0 ? angleY : std::min(lowY, angleY);
        highY = i == 0 ? angleY : std::max(highY, angleY);
    }
    shape.layout = (highY - lowY) > 1.2 * (highX - lowX) ? Layout::Vertical : Layout::Horizontal;

    // First level: along a horizontal sweep the frames' X axes lie in the horizon's plane.
    std::vector<double> spread(9, 0.0);
    for (const int m : solution.members) {
        const ipr::Vec3d x = axisOf(m, 0);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                spread[size_t(r * 3 + c)] += x[size_t(r)] * x[size_t(c)];
    }
    std::vector<double> values, vectors;
    ipr::Vec3d axisY = meanY;
    if (ipr::symmetricEigen(spread, 3, values, vectors) && values[1] > 1e-4 * values[0])
        axisY = ipr::Vec3d(vectors[6], vectors[7], vectors[8]);
    if (axisY.dot(meanY) < 0.0)
        axisY = -axisY;
    if (shape.layout == Layout::Horizontal) {
        axisY = ipr::unit(axisY);
        const ipr::Vec3d axisZ = ipr::unit(meanZ - axisY * meanZ.dot(axisY));
        turnWorld(axisY.cross(axisZ), axisY, axisZ);
    } else {
        const ipr::Vec3d axisZ = ipr::unit(meanZ - meanX * meanZ.dot(meanX));
        turnWorld(meanX, axisZ.cross(meanX), axisZ);
    }

    const double fieldX = 2.0 * std::atan2(frame.width * 0.5, solution.focal);
    const double fieldY = 2.0 * std::atan2(frame.height * 0.5, solution.focal);
    const double fieldAlong = shape.layout == Layout::Horizontal ? fieldX : fieldY;
    const double fieldAcross = shape.layout == Layout::Horizontal ? fieldY : fieldX;
    const auto measure = [&](std::vector<double> &along, std::vector<double> &across) {
        along.clear();
        across.clear();
        for (const int m : solution.members) {
            const ipr::Vec3d z = axisOf(m, 2);
            along.push_back(shape.layout == Layout::Horizontal ? std::atan2(z[0], z[2]) : std::atan2(z[1], z[2]));
            across.push_back(shape.layout == Layout::Horizontal ? std::atan2(z[1], std::hypot(z[0], z[2])) : std::atan2(z[0], std::hypot(z[1], z[2])));
        }
    };
    std::vector<double> along, across;
    measure(along, across);

    // A series leans on the bottom of its view: that line is what must run level, the rest
    // of the frames falling where they may. In a sweep, the bottom edges of the lowest row
    // give the world's sideways lean and how far it bends forward. A short arc tells little
    // of the bend, which small changes of tilt during the series would decide: a pull towards
    // no bend keeps it out then, and lets a wide arc through.
    if (shape.layout == Layout::Horizontal) {
        const double lowest = *std::max_element(across.begin(), across.end());
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<ipr::Vec3d> feet; // sideways, forward, height
            ipr::Vec3d mean(0, 0, 0);
            for (size_t i = 0; i < solution.members.size(); ++i) {
                if (lowest - across[i] > rowGapOfField * fieldAcross)
                    continue;
                const ipr::Matrix33d &r = solution.worldToCamera[size_t(solution.members[i])];
                const ipr::Vec3d foot = r.t() * ipr::Vec3d(0.0, frame.height * 0.5 / solution.focal, 1.0);
                const double yaw = std::atan2(foot[0], foot[2]);
                const ipr::Vec3d entry(std::sin(yaw), std::cos(yaw), std::atan2(foot[1], std::hypot(foot[0], foot[2])));
                feet.push_back(entry);
                mean += entry;
            }
            if (feet.size() < 2)
                break;
            mean *= 1.0 / double(feet.size());
            double ss = 1e-9, sc = 0.0, cc = bendPull * double(feet.size()), se = 0.0, ce = 0.0;
            for (const ipr::Vec3d &foot : feet) {
                const ipr::Vec3d d = foot - mean;
                ss += d[0] * d[0];
                sc += d[0] * d[1];
                cc += d[1] * d[1];
                se += d[0] * d[2];
                ce += d[1] * d[2];
            }
            const double determinant = ss * cc - sc * sc;
            if (determinant < 1e-12 || ss < 1e-5)
                break;
            const double lean = (cc * se - sc * ce) / determinant, bend = (ss * ce - sc * se) / determinant;
            const double cl = std::cos(lean), sl = std::sin(lean), cb = std::cos(bend), sb = std::sin(bend);
            turnWorld(ipr::Vec3d(cl, sl, 0.0), ipr::Vec3d(-sl, cl, 0.0), ipr::Vec3d(0.0, 0.0, 1.0));
            turnWorld(ipr::Vec3d(1.0, 0.0, 0.0), ipr::Vec3d(0.0, cb, -sb), ipr::Vec3d(0.0, sb, cb));
        }
    } else {
        int foot = solution.members.front();
        double lowest = -2.0;
        for (const int m : solution.members) {
            const double pitch = axisOf(m, 2)[1];
            if (pitch > lowest) {
                lowest = pitch;
                foot = m;
            }
        }
        const ipr::Vec3d x = axisOf(foot, 0);
        const ipr::Vec3d z = ipr::unit(ipr::Vec3d(0, 0, 1) - x * x[2]);
        turnWorld(x, z.cross(x), z);
    }
    measure(along, across);

    const auto span = [](const std::vector<double> &list) {
        return *std::max_element(list.begin(), list.end()) - *std::min_element(list.begin(), list.end());
    };
    shape.spanAlong = span(along) + fieldAlong;
    shape.spanAcross = span(across) + fieldAcross;
    // The frames stacked above each other somewhere along the panorama: a series is seldom
    // shot on neat rows, and its rows drift across the whole.
    for (size_t i = 0; i < along.size(); ++i) {
        std::vector<double> column;
        for (size_t j = 0; j < along.size(); ++j)
            if (std::abs(along[j] - along[i]) < 0.5 * fieldAlong)
                column.push_back(across[j]);
        std::sort(column.begin(), column.end());
        int rows = 1;
        for (size_t k = 1; k < column.size(); ++k)
            if (column[k] - column[k - 1] > rowGapOfField * fieldAcross)
                ++rows;
        shape.rows = std::max(shape.rows, rows);
    }

    shape.projection = requested;
    if (requested == Projection::Automatic) {
        shape.projection = shape.spanAcross > spanSphereLimit ? Projection::Spherical
            : shape.spanAlong <= spanFlatLimit ? Projection::Rectilinear : Projection::Cylindrical;
    }
    if (shape.projection == Projection::Rectilinear && std::max(shape.spanAlong, shape.spanAcross) > spanFlatRefused)
        shape.projection = Projection::Cylindrical;
    return shape;
}

ipr::Vec3d Surface::ray(double x, double y) const
{
    const double u = (x + origin.x) / scale, v = (y + origin.y) / scale;
    switch (projection) {
    case Projection::Spherical:
        if (layout == Layout::Horizontal)
            return {std::sin(u) * std::cos(v), std::sin(v), std::cos(u) * std::cos(v)};
        return {std::sin(u), std::cos(u) * std::sin(v), std::cos(u) * std::cos(v)};
    case Projection::Cylindrical:
        if (layout == Layout::Horizontal)
            return {std::sin(u), v, std::cos(u)};
        return {u, std::sin(v), std::cos(v)};
    case Projection::Automatic:
    case Projection::Rectilinear:
        break;
    }
    return {u, v, 1.0};
}

bool Surface::place(const ipr::Vec3d &r, ipr::Point2d &canvas) const
{
    double u = 0.0, v = 0.0;
    switch (projection) {
    case Projection::Spherical:
        if (layout == Layout::Horizontal) {
            u = std::atan2(r[0], r[2]);
            v = std::atan2(r[1], std::hypot(r[0], r[2]));
        } else {
            v = std::atan2(r[1], r[2]);
            u = std::atan2(r[0], std::hypot(r[1], r[2]));
        }
        break;
    case Projection::Cylindrical:
        if (layout == Layout::Horizontal) {
            u = std::atan2(r[0], r[2]);
            v = r[1] / std::hypot(r[0], r[2]);
        } else {
            v = std::atan2(r[1], r[2]);
            u = r[0] / std::hypot(r[1], r[2]);
        }
        break;
    case Projection::Automatic:
    case Projection::Rectilinear:
        if (r[2] <= 1e-6)
            return false;
        u = r[0] / r[2];
        v = r[1] / r[2];
        break;
    }
    canvas = {u * scale - origin.x, v * scale - origin.y};
    return true;
}

bool outline(const Surface &surface, const FrameView &view, ipr::Rectd &area)
{
    constexpr int steps = 32;
    double left = 0, right = 0, top = 0, bottom = 0;
    bool first = true;
    const double halfWidth = (view.frame.width - 1) * 0.5, halfHeight = (view.frame.height - 1) * 0.5;
    const ipr::Matrix33d cameraToWorld = view.worldToCamera.t();
    const Lens lens = lensOf(view.frame, view.focal, view.distortion);
    for (int side = 0; side < 4; ++side) {
        for (int step = 0; step <= steps; ++step) {
            const double t = double(step) / steps;
            const double x = side == 0 ? -halfWidth + t * 2.0 * halfWidth : side == 1 ? halfWidth : side == 2 ? halfWidth - t * 2.0 * halfWidth : -halfWidth;
            const double y = side == 0 ? -halfHeight : side == 1 ? -halfHeight + t * 2.0 * halfHeight : side == 2 ? halfHeight : halfHeight - t * 2.0 * halfHeight;
            ipr::Point2d canvas;
            const ipr::Point2d ideal = undistorted({x, y}, lens);
            if (!surface.place(cameraToWorld * ipr::Vec3d(ideal.x / view.focal, ideal.y / view.focal, 1.0), canvas))
                return false;
            left = first ? canvas.x : std::min(left, canvas.x);
            right = first ? canvas.x : std::max(right, canvas.x);
            top = first ? canvas.y : std::min(top, canvas.y);
            bottom = first ? canvas.y : std::max(bottom, canvas.y);
            first = false;
        }
    }
    area = ipr::Rectd(left, top, right - left, bottom - top);
    return true;
}

void sourceMaps(const Surface &surface, const FrameView &view, const ipr::Recti &area, double margin,
                ipr::Image &mapX, ipr::Image &mapY, ipr::Image &valid)
{
    mapX = ipr::Image::create(area.size(), ipr::Depth::F32, 1);
    mapY = ipr::Image::create(area.size(), ipr::Depth::F32, 1);
    valid = ipr::Image::create(area.size(), ipr::Depth::U8, 1);
    const double centreX = (view.frame.width - 1) * 0.5, centreY = (view.frame.height - 1) * 0.5;
    const double right = view.frame.width - 1 - margin, bottom = view.frame.height - 1 - margin;
    const ipr::Matrix33d r = view.worldToCamera;
    const Lens lens = lensOf(view.frame, view.focal, view.distortion);
    ipr::parallelRows(area.height, [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            float *outX = mapX.mutableRow<float>(y), *outY = mapY.mutableRow<float>(y);
            std::uint8_t *inside = valid.mutableRow<std::uint8_t>(y);
            for (int x = 0; x < area.width; ++x) {
                const ipr::Vec3d seen = r * surface.ray(area.x + x, area.y + y);
                outX[x] = -1.0f;
                outY[x] = -1.0f;
                inside[x] = 0;
                if (seen[2] <= 1e-9)
                    continue;
                const ipr::Point2d source = distorted({view.focal * seen[0] / seen[2], view.focal * seen[1] / seen[2]}, lens);
                const double u = source.x + centreX, v = source.y + centreY;
                if (u >= margin && u <= right && v >= margin && v <= bottom) {
                    outX[x] = float(u);
                    outY[x] = float(v);
                    inside[x] = 255;
                }
            }
        }
    });
}

} // namespace PanoramaGeometry
