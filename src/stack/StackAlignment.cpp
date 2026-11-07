#include "StackAlignment.h"

#include "Filters.h"
#include "Resize.h"
#include "Arrange.h"
#include "Neighbourhood.h"
#include "Pixels.h"
#include "Analysis.h"
#include "Parallel.h"
#include "Random.h"


#include <algorithm>
#include <cmath>
#include <limits>

namespace StackAlignment {
namespace {

struct Match
{
    ipr::Point2d reference;
    ipr::Point2d frame;
};

ipr::Point2d applied(const ipr::Matrix33d &h, const ipr::Point2d &p)
{
    const double w = h(2, 0) * p.x + h(2, 1) * p.y + h(2, 2);
    if (std::abs(w) < 1e-12)
        return {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    return {(h(0, 0) * p.x + h(0, 1) * p.y + h(0, 2)) / w,
            (h(1, 0) * p.x + h(1, 1) * p.y + h(1, 2)) / w};
}

double distance(const ipr::Matrix33d &h, const Match &match)
{
    const ipr::Point2d d = applied(h, match.reference) - match.frame;
    return std::hypot(d.x, d.y);
}

// Moves the points to mean 0 and mean distance sqrt(2), which conditions the fit.
ipr::Matrix33d conditioning(const std::vector<ipr::Point2d> &points)
{
    ipr::Point2d mean(0, 0);
    for (const auto &p : points)
        mean += p;
    mean *= 1.0 / double(points.size());
    double spread = 0.0;
    for (const auto &p : points)
        spread += std::hypot(p.x - mean.x, p.y - mean.y);
    spread /= double(points.size());
    const double s = spread > 1e-9 ? std::sqrt(2.0) / spread : 1.0;
    return {s, 0, -s * mean.x, 0, s, -s * mean.y, 0, 0, 1};
}

// Direct linear fit of a homography; false when the points do not determine one.
bool fitHomography(const std::vector<Match> &matches, ipr::Matrix33d &h)
{
    if (matches.size() < 4)
        return false;
    std::vector<ipr::Point2d> from, to;
    for (const Match &m : matches) {
        from.push_back(m.reference);
        to.push_back(m.frame);
    }
    const ipr::Matrix33d a = conditioning(from), b = conditioning(to);
    const int rows = int(matches.size()) * 2;
    std::vector<double> system(size_t(rows) * 9, 0.0);
    for (int i = 0; i < int(matches.size()); ++i) {
        const ipr::Point2d p = applied(a, from[size_t(i)]), q = applied(b, to[size_t(i)]);
        double *r0 = &system[size_t(2 * i) * 9], *r1 = &system[size_t(2 * i + 1) * 9];
        r0[0] = p.x; r0[1] = p.y; r0[2] = 1;
        r0[6] = -q.x * p.x; r0[7] = -q.x * p.y; r0[8] = -q.x;
        r1[3] = p.x; r1[4] = p.y; r1[5] = 1;
        r1[6] = -q.y * p.x; r1[7] = -q.y * p.y; r1[8] = -q.y;
    }
    std::vector<double> w, vt;
    if (!ipr::singularValues(system, rows, 9, w, vt) || vt.size() < 81 || w.size() < 8 || w[7] < 1e-9 * w[0])
        return false;
    ipr::Matrix33d conditioned;
    for (int i = 0; i < 9; ++i)
        conditioned(i / 3, i % 3) = vt[size_t(8 * 9 + i)];
    h = b.inv() * conditioned * a;
    if (std::abs(h(2, 2)) < 1e-12)
        return false;
    h *= 1.0 / h(2, 2);
    return true;
}

// Least-squares shift, rotation and uniform scale.
bool fitSimilarity(const std::vector<Match> &matches, ipr::Matrix33d &h)
{
    if (matches.size() < 2)
        return false;
    ipr::Point2d meanFrom(0, 0), meanTo(0, 0);
    for (const Match &m : matches) {
        meanFrom += m.reference;
        meanTo += m.frame;
    }
    meanFrom *= 1.0 / double(matches.size());
    meanTo *= 1.0 / double(matches.size());
    double c = 0.0, s = 0.0, norm = 0.0;
    for (const Match &m : matches) {
        const ipr::Point2d p = m.reference - meanFrom, q = m.frame - meanTo;
        c += p.x * q.x + p.y * q.y;
        s += p.x * q.y - p.y * q.x;
        norm += p.x * p.x + p.y * p.y;
    }
    if (norm < 1e-9)
        return false;
    c /= norm;
    s /= norm;
    h = ipr::Matrix33d(c, -s, meanTo.x - c * meanFrom.x + s * meanFrom.y,
                    s, c, meanTo.y - s * meanFrom.x - c * meanFrom.y,
                    0, 0, 1);
    return true;
}

bool fit(Model model, const std::vector<Match> &matches, ipr::Matrix33d &h)
{
    return model == Model::Homography ? fitHomography(matches, h) : fitSimilarity(matches, h);
}

// Consensus of a seeded random sampling, then refits on the inliers.
bool robustFit(Model model, const std::vector<Match> &matches, double tolerance, ipr::Matrix33d &best,
               std::vector<Match> &inliers)
{
    const int sampleSize = model == Model::Homography ? 4 : 2;
    if (int(matches.size()) < sampleSize)
        return false;
    ipr::Random random(0x1b5d);
    int bestCount = 0;
    std::vector<Match> sample(static_cast<size_t>(sampleSize));
    for (int attempt = 0; attempt < 500; ++attempt) {
        for (Match &chosen : sample)
            chosen = matches[size_t(random.uniform(0, int(matches.size())))];
        ipr::Matrix33d candidate;
        if (!fit(model, sample, candidate))
            continue;
        int count = 0;
        for (const Match &m : matches)
            count += distance(candidate, m) <= tolerance;
        if (count > bestCount) {
            bestCount = count;
            best = candidate;
        }
    }
    if (bestCount < sampleSize)
        return false;
    for (int pass = 0; pass < 3; ++pass) {
        inliers.clear();
        for (const Match &m : matches)
            if (distance(best, m) <= tolerance)
                inliers.push_back(m);
        ipr::Matrix33d refined;
        if (!fit(model, inliers, refined))
            return false;
        best = refined;
    }
    inliers.clear();
    for (const Match &m : matches)
        if (distance(best, m) <= tolerance)
            inliers.push_back(m);
    return !inliers.empty();
}

// How much detail a block holds, and in how many directions: an edge alone slides along
// itself. 0 for a block that cannot locate itself.
double cornerEnergy(const ipr::Image &block)
{
    double xx = 0.0, yy = 0.0, xy = 0.0;
    for (int y = 1; y < block.height() - 1; ++y) {
        const float *above = block.row<float>(y - 1), *row = block.row<float>(y), *below = block.row<float>(y + 1);
        for (int x = 1; x < block.width() - 1; ++x) {
            const double dx = row[x + 1] - row[x - 1], dy = below[x] - above[x];
            xx += dx * dx;
            yy += dy * dy;
            xy += dx * dy;
        }
    }
    const double trace = xx + yy;
    const double smaller = 0.5 * (trace - std::sqrt(std::max(0.0, (xx - yy) * (xx - yy) + 4.0 * xy * xy)));
    return trace > 1e-9 && smaller > 0.08 * trace ? trace : 0.0;
}

bool hasCorners(const ipr::Image &block)
{
    return cornerEnergy(block) > 0.0;
}

// The peak with its sub-pixel place, by a parabola along each axis.
bool peak(const ipr::Image &scores, ipr::Point2d &place, double &value)
{
    const ipr::Extremes found = ipr::extremesOf(scores);
    const double maximum = found.largest;
    const ipr::Point2i at = found.largestAt;
    if (at.x <= 0 || at.y <= 0 || at.x >= scores.width() - 1 || at.y >= scores.height() - 1)
        return false; // on the edge of the search: the true peak may lie beyond it
    const auto offset = [](double before, double middle, double after) {
        const double curve = before - 2.0 * middle + after;
        return std::abs(curve) > 1e-12 ? std::clamp(0.5 * (before - after) / curve, -0.5, 0.5) : 0.0;
    };
    const float *row = scores.row<float>(at.y);
    place = {at.x + offset(row[at.x - 1], maximum, row[at.x + 1]),
             at.y + offset(scores.row<float>(at.y - 1)[at.x], maximum, scores.row<float>(at.y + 1)[at.x])};
    value = maximum;
    return true;
}

// A regular grid, which spreads the matches over the image.
std::vector<ipr::Point2i> gridSites(const ipr::Image &reference, const Options &options)
{
    const int half = options.blockHalf;
    const int spacing = std::max(2 * half + 1, int(std::ceil(std::sqrt(
        double(reference.width()) * double(reference.height()) / std::max(1, options.maximumBlocks)))));
    std::vector<ipr::Point2i> sites;
    for (int y = half; y + half < reference.height(); y += spacing)
        for (int x = half; x + half < reference.width(); x += spacing)
            sites.emplace_back(x, y);
    return sites;
}

// The most detailed blocks of a fine grid, for a small subject on an empty background,
// which a regular grid hardly touches.
std::vector<ipr::Point2i> detailedSites(const ipr::Image &reference, const Options &options)
{
    const int half = options.blockHalf;
    const int spacing = half + 1;
    std::vector<ipr::Point2i> sites;
    for (int y = half; y + half < reference.height(); y += spacing)
        for (int x = half; x + half < reference.width(); x += spacing)
            sites.emplace_back(x, y);
    std::vector<double> energy(sites.size());
    ipr::parallelRows(int(sites.size()), [&](int first, int last) {
        for (int i = first; i < last; ++i) {
            const ipr::Point2i site = sites[size_t(i)];
            energy[size_t(i)] = cornerEnergy(reference.view(ipr::Recti(site.x - half, site.y - half, 2 * half + 1, 2 * half + 1)));
        }
    });
    std::vector<int> order(sites.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = int(i);
    const size_t kept = std::min(order.size(), size_t(std::max(1, options.maximumBlocks)));
    // The site breaks a tie, so that the choice does not depend on the sort.
    std::partial_sort(order.begin(), order.begin() + std::ptrdiff_t(kept), order.end(), [&](int a, int b) {
        return energy[size_t(a)] != energy[size_t(b)] ? energy[size_t(a)] > energy[size_t(b)] : a < b;
    });
    std::vector<ipr::Point2i> best;
    for (size_t i = 0; i < kept; ++i)
        if (energy[size_t(order[i])] > 0.0)
            best.push_back(sites[size_t(order[i])]);
    return best;
}

std::vector<Match> matchBlocks(const ipr::Image &reference, const ipr::Image &frame, const std::vector<ipr::Point2i> &sites,
                               const ipr::Matrix33d &guess, int radius, const Options &options)
{
    const int half = options.blockHalf;
    std::vector<Match> found(sites.size());
    std::vector<char> kept(sites.size(), 0);
    ipr::parallelRows(int(sites.size()), [&](int first, int last) {
        ipr::Image scores;
        for (int i = first; i < last; ++i) {
            const ipr::Point2i site = sites[size_t(i)];
            const ipr::Recti blockArea(site.x - half, site.y - half, 2 * half + 1, 2 * half + 1);
            const ipr::Image block = reference.view(blockArea);
            if (!hasCorners(block))
                continue;
            const ipr::Point2d predicted = applied(guess, ipr::Point2d(site));
            if (!std::isfinite(predicted.x) || !std::isfinite(predicted.y))
                continue;
            const ipr::Point2i centre(int(std::lround(predicted.x)), int(std::lround(predicted.y)));
            const ipr::Recti searchArea(centre.x - half - radius, centre.y - half - radius,
                                      2 * (half + radius) + 1, 2 * (half + radius) + 1);
            if ((searchArea & ipr::Recti(0, 0, frame.width(), frame.height())) != searchArea)
                continue;
            scores = ipr::matchedTemplate(frame.view(searchArea), block);
            ipr::Point2d place;
            double score = 0.0;
            if (!peak(scores, place, score) || score < options.minimumScore)
                continue;
            found[size_t(i)] = {ipr::Point2d(site),
                                ipr::Point2d(searchArea.x + place.x + half, searchArea.y + place.y + half)};
            kept[size_t(i)] = 1;
        }
    });
    std::vector<Match> matches;
    for (size_t i = 0; i < found.size(); ++i)
        if (kept[i])
            matches.push_back(found[i]);
    return matches;
}

double supportOf(const std::vector<Match> &inliers, const ipr::Size &size)
{
    if (inliers.empty())
        return 0.0;
    double left = inliers.front().reference.x, right = left, top = inliers.front().reference.y, bottom = top;
    for (const Match &m : inliers) {
        left = std::min(left, m.reference.x);
        right = std::max(right, m.reference.x);
        top = std::min(top, m.reference.y);
        bottom = std::max(bottom, m.reference.y);
    }
    return std::min((right - left) / std::max(1, size.width), (bottom - top) / std::max(1, size.height));
}

// A transform that sends a corner of the image past half its size is no hand tremor,
// and one that flips it is no transform of a same scene.
bool plausible(const ipr::Matrix33d &h, const ipr::Size &size)
{
    const double determinant = ipr::determinant(h);
    if (!(determinant > 0.0) || !std::isfinite(determinant))
        return false;
    const double reach = 0.5 * std::max(size.width, size.height);
    for (const ipr::Point2d corner : {ipr::Point2d(0, 0), ipr::Point2d(size.width - 1, 0),
                                     ipr::Point2d(0, size.height - 1), ipr::Point2d(size.width - 1, size.height - 1)}) {
        const ipr::Point2d moved = applied(h, corner);
        if (!std::isfinite(moved.x) || !std::isfinite(moved.y) || std::hypot(moved.x - corner.x, moved.y - corner.y) > reach)
            return false;
    }
    return true;
}

ipr::Matrix33d rescaled(const ipr::Matrix33d &h, double factor)
{
    const ipr::Matrix33d up(factor, 0, 0, 0, factor, 0, 0, 0, 1);
    const ipr::Matrix33d down(1.0 / factor, 0, 0, 0, 1.0 / factor, 0, 0, 0, 1);
    return up * h * down;
}

} // namespace

LocalField localField(const ipr::Image &reference, const ipr::Image &registered, const LocalOptions &options)
{
    LocalField field;
    if (reference.empty() || reference.size() != registered.size() || !(reference.depth() == ipr::Depth::F32 && reference.channels() == 1) ||
        !(registered.depth() == ipr::Depth::F32 && registered.channels() == 1))
        return field;
    const int half = options.blockHalf, reach = half + options.radius;
    const ipr::Size grid(std::max(1, reference.width() / options.spacing), std::max(1, reference.height() / options.spacing));
    field.sites = grid.area();
    ipr::Image measured = ipr::filled(grid, ipr::Depth::F32, 2, 0.0), weight = ipr::filled(grid, ipr::Depth::F32, 1, 0.0);
    ipr::parallelRows(grid.height, [&](int first, int last) {
        ipr::Image scores;
        for (int row = first; row < last; ++row) {
            for (int column = 0; column < grid.width; ++column) {
                const ipr::Point2i site(int((column + 0.5) * reference.width() / grid.width),
                                     int((row + 0.5) * reference.height() / grid.height));
                if (site.x < reach || site.y < reach || site.x + reach >= reference.width() || site.y + reach >= reference.height())
                    continue;
                const ipr::Image block = reference.view(ipr::Recti(site.x - half, site.y - half, 2 * half + 1, 2 * half + 1));
                if (!hasCorners(block))
                    continue;
                scores = ipr::matchedTemplate(registered.view(ipr::Recti(site.x - reach, site.y - reach, 2 * reach + 1, 2 * reach + 1)), block);
                ipr::Point2d place;
                double score = 0.0;
                if (!peak(scores, place, score) || score < options.minimumScore)
                    continue;
                measured.mutableRow<ipr::Vec2f>(row)[column] = ipr::Vec2f(float(place.x - options.radius), float(place.y - options.radius));
                weight.mutableRow<float>(row)[column] = 1.f;
            }
        }
    });
    field.measured = int(ipr::countNonZero(weight));

    // A median outvotes the odd block; the weights then fill the gaps, the field falls to
    // zero away from any measure, and it stays smooth: a sharp step would tear an image at
    // an object's edge.
    std::vector<ipr::Image> axes = ipr::splitChannels(measured);
    for (ipr::Image &axis : axes) {
        const ipr::Image filtered = ipr::median(axis, 3);
        for (int row = 0; row < axis.height(); ++row) {
            float *out = axis.mutableRow<float>(row);
            const float *in = filtered.row<float>(row), *measuredHere = weight.row<float>(row);
            for (int column = 0; column < axis.width(); ++column)
                if (measuredHere[column] > 0.f)
                    out[column] = in[column];
        }
    }
    const ipr::Image smoothWeight = ipr::gaussianBlurred(weight, options.smoothing, ipr::BlurBorder::Mirror);
    constexpr float wholeWeight = 0.25f; // this much measure nearby is trusted fully
    for (ipr::Image &axis : axes) {
        const ipr::Image smooth = ipr::gaussianBlurred(ipr::multiplied(axis, weight), options.smoothing, ipr::BlurBorder::Mirror);
        for (int row = 0; row < axis.height(); ++row) {
            float *out = axis.mutableRow<float>(row);
            const float *value = smooth.row<float>(row), *share = smoothWeight.row<float>(row);
            for (int column = 0; column < axis.width(); ++column)
                out[column] = share[column] > 1e-3f
                    ? value[column] / share[column] * std::min(1.f, share[column] / wholeWeight) : 0.f;
        }
    }
    field.displacements = ipr::resized(ipr::merged(axes), reference.size(), ipr::Interpolation::Linear);
    double largest = 0.0;
    for (const ipr::Image &axis : axes) {
        const ipr::Extremes range = ipr::extremesOf(axis);
        const double low = range.smallest, high = range.largest;
        largest = std::max({largest, std::abs(low), std::abs(high)});
    }
    field.largest = largest * std::sqrt(2.0);
    return field;
}

Pyramid buildPyramid(const ipr::Image &cameraRgb, const Options &options)
{
    Pyramid pyramid;
    if (cameraRgb.empty() || !(cameraRgb.depth() == ipr::Depth::F32 && cameraRgb.channels() == 3))
        return pyramid;
    ipr::Image luminance = ipr::Image::create(cameraRgb.size(), ipr::Depth::F32, 1);
    ipr::parallelRows(cameraRgb.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const ipr::Vec3f *in = cameraRgb.row<ipr::Vec3f>(y);
            float *out = luminance.mutableRow<float>(y);
            for (int x = 0; x < cameraRgb.width(); ++x)
                out[x] = std::sqrt(std::max(0.f, 0.25f * in[x][0] + 0.5f * in[x][1] + 0.25f * in[x][2]));
        }
    });
    pyramid.push_back(std::move(luminance));
    while (std::max(pyramid.back().width(), pyramid.back().height()) > options.coarsestSide) {
        pyramid.push_back(ipr::pyramidDown(pyramid.back()));
    }
    return pyramid;
}

Result estimate(const Pyramid &reference, const Pyramid &frame, const Options &options,
                const std::function<bool()> &cancelled)
{
    Result result;
    if (reference.empty() || reference.size() != frame.size() || reference[0].size() != frame[0].size()) {
        result.error = QStringLiteral("Dimensions différentes de l'image de référence.");
        return result;
    }
    const ipr::Size size = reference[0].size();
    const int coarsest = int(reference.size()) - 1;

    // A hand's shift can exceed the block search; the coarsest level recovers it whole.
    const ipr::Image window = ipr::hannWindow(reference[size_t(coarsest)].size());
    const ipr::Point2d shift = ipr::phaseCorrelation(reference[size_t(coarsest)], frame[size_t(coarsest)], window);
    ipr::Matrix33d guess(1, 0, std::isfinite(shift.x) ? shift.x : 0.0,
                      0, 1, std::isfinite(shift.y) ? shift.y : 0.0,
                      0, 0, 1);

    std::vector<Match> inliers;
    for (int level = coarsest; level >= 0; --level) {
        if (cancelled && cancelled()) {
            result.error = QStringLiteral("Annulé.");
            return result;
        }
        const int radius = level <= 1 ? options.fineRadius : options.coarseRadius;
        const ipr::Image &view = reference[size_t(level)];
        ipr::Matrix33d candidate;
        std::vector<Match> agreeing;
        Model model = Model::Homography;
        bool fitted = false;
        // A homography needs matches across the image; a small support holds only a similarity.
        const auto attempt = [&](const std::vector<ipr::Point2i> &sites) {
            const std::vector<Match> matches = matchBlocks(view, frame[size_t(level)], sites, guess, radius, options);
            result.blocks = int(matches.size());
            model = Model::Homography;
            fitted = robustFit(model, matches, options.inlierPixels, candidate, agreeing);
            if (!fitted || supportOf(agreeing, view.size()) < options.minimumSupport) {
                model = Model::Similarity;
                fitted = robustFit(model, matches, options.inlierPixels, candidate, agreeing);
            }
            return fitted && int(agreeing.size()) >= options.minimumInliers;
        };
        fitted = attempt(gridSites(view, options));
        // A similarity means a small subject, which the grid hardly touches: its most detailed
        // blocks say more, if they agree. Otherwise what the grid found stands.
        if (!fitted || model == Model::Similarity) {
            const bool fromGrid = fitted;
            const ipr::Matrix33d gridCandidate = candidate;
            const std::vector<Match> gridAgreeing = agreeing;
            const Model gridModel = model;
            const int gridBlocks = result.blocks;
            fitted = attempt(detailedSites(view, options));
            if (!fitted && fromGrid) {
                candidate = gridCandidate;
                agreeing = gridAgreeing;
                model = gridModel;
                result.blocks = gridBlocks;
                fitted = true;
            }
        }
        if (fitted) {
            guess = candidate;
            inliers = agreeing;
            result.model = model;
        } else {
            inliers.clear(); // too few matches here: the finer level decides from the same guess
        }
        if (level > 0)
            guess = rescaled(guess, 2.0);
    }

    result.inliers = int(inliers.size());
    if (result.inliers < options.minimumInliers) {
        result.error = QStringLiteral("Pas assez de détails communs pour recaler cette image.");
        return result;
    }
    if (!plausible(guess, size)) {
        result.error = QStringLiteral("Le recalage trouvé n'est pas plausible pour une même scène.");
        return result;
    }
    double squares = 0.0;
    for (const Match &m : inliers) {
        const double d = distance(guess, m);
        squares += d * d;
    }
    result.residual = std::sqrt(squares / double(inliers.size()));
    result.support = supportOf(inliers, size);
    ipr::Point2d lowest = inliers.front().reference, highest = lowest;
    for (const Match &m : inliers) {
        lowest = {std::min(lowest.x, m.reference.x), std::min(lowest.y, m.reference.y)};
        highest = {std::max(highest.x, m.reference.x), std::max(highest.y, m.reference.y)};
    }
    result.inlierArea = ipr::Rectd(lowest, highest);
    result.referenceToFrame = guess;
    result.valid = true;
    return result;
}

} // namespace StackAlignment
