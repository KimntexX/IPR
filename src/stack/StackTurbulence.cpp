#include "StackTurbulence.h"

#include "StackMerge.h"

#include "Filters.h"
#include "Warp.h"
#include "Neighbourhood.h"
#include "Pixels.h"


#include <algorithm>
#include <cmath>
#include <numeric>

namespace StackTurbulence {
namespace {

ipr::Image rootLuminance(const ipr::Image &rgb)
{
    ipr::Image luminance = ipr::Image::create(rgb.size(), ipr::Depth::F32, 1);
    for (int y = 0; y < rgb.height(); ++y) {
        const ipr::Vec3f *in = rgb.row<ipr::Vec3f>(y);
        float *out = luminance.mutableRow<float>(y);
        for (int x = 0; x < rgb.width(); ++x)
            out[x] = std::sqrt(std::max(0.f, 0.25f * in[x][0] + 0.5f * in[x][1] + 0.25f * in[x][2]));
    }
    return luminance;
}

// Fine detail of a view, read at a scale above the noise of a single pixel.
double sharpnessOf(const ipr::Image &luminance)
{
    ipr::Image smoothed, detail;
    smoothed = ipr::gaussianBlurred(luminance, 1.0, ipr::BlurBorder::Mirror);
    detail = ipr::laplacian(smoothed);
    std::vector<double> mean, deviation;
    ipr::meanAndDeviationOf(detail, mean, deviation);
    return deviation[0] * deviation[0];
}

// 1 inside, falling to 0 at the area's edge.
ipr::Image fadeMask(const ipr::Size &size, int feather)
{
    ipr::Image mask = ipr::Image::create(size, ipr::Depth::F32, 1);
    const float reach = float(std::max(1, feather));
    for (int y = 0; y < size.height; ++y) {
        float *out = mask.mutableRow<float>(y);
        for (int x = 0; x < size.width; ++x) {
            const float edge = float(std::min({x, y, size.width - 1 - x, size.height - 1 - y}));
            const float t = std::clamp(edge / reach, 0.f, 1.f);
            out[x] = t * t * (3.f - 2.f * t);
        }
    }
    return mask;
}

} // namespace

Report refine(ipr::Image &merged, const ipr::Recti &subject, const ipr::Point2i &origin, const std::vector<View> &views,
              const Options &options, const std::function<bool()> &cancelled)
{
    Report report;
    report.views = int(views.size());
    const ipr::Recti bounds(0, 0, merged.width(), merged.height());
    if (merged.empty() || !(merged.depth() == ipr::Depth::F32 && merged.channels() == 3) || subject.empty() || (subject & bounds) != subject) {
        report.reason = QStringLiteral("Zone du sujet invalide.");
        return report;
    }
    if (int(views.size()) < options.minimumKept) {
        report.reason = QStringLiteral("Trop peu de vues pour choisir les plus nettes.");
        return report;
    }

    const ipr::Size size = subject.size();
    const ipr::Image reference = rootLuminance(merged.copy(subject));
    // From the subject's own coordinates to the frame's.
    const ipr::Matrix33d subjectToReference(1, 0, double(origin.x + subject.x), 0, 1, double(origin.y + subject.y), 0, 0, 1);

    std::vector<ipr::Image> registered(views.size());
    std::vector<int> measured;
    for (size_t i = 0; i < views.size(); ++i) {
        if (cancelled && cancelled()) {
            report.reason = QStringLiteral("Annulé.");
            return report;
        }
        const View &view = views[i];
        if (view.pixels.empty() || !(view.pixels.depth() == ipr::Depth::F32 && view.pixels.channels() == 3)) {
            report.sharpness.push_back(0.0);
            continue;
        }
        const ipr::Matrix33d frameToPixels(1, 0, -double(view.origin.x), 0, 1, -double(view.origin.y), 0, 0, 1);
        const ipr::Matrix33d subjectToPixels = frameToPixels * view.referenceToFrame * subjectToReference;
        const ipr::Image roughly = ipr::warpedPerspective(rootLuminance(view.pixels), subjectToPixels, size, ipr::Resampling::Linear, ipr::Outside::LastPixel);
        const StackAlignment::LocalField field = StackAlignment::localField(reference, roughly, options.local);
        if (field.displacements.empty()) {
            report.sharpness.push_back(0.0);
            continue;
        }
        measured.push_back(field.measured);
        report.largestDisplacement = std::max(report.largestDisplacement, field.largest);

        ipr::Image mapX = ipr::Image::create(size, ipr::Depth::F32, 1), mapY = ipr::Image::create(size, ipr::Depth::F32, 1);
        for (int y = 0; y < size.height; ++y) {
            const ipr::Vec2f *move = field.displacements.row<ipr::Vec2f>(y);
            float *outX = mapX.mutableRow<float>(y), *outY = mapY.mutableRow<float>(y);
            for (int x = 0; x < size.width; ++x) {
                const double px = x + double(move[x][0]), py = y + double(move[x][1]);
                const double w = subjectToPixels(2, 0) * px + subjectToPixels(2, 1) * py + subjectToPixels(2, 2);
                outX[x] = float((subjectToPixels(0, 0) * px + subjectToPixels(0, 1) * py + subjectToPixels(0, 2)) / w);
                outY[x] = float((subjectToPixels(1, 0) * px + subjectToPixels(1, 1) * py + subjectToPixels(1, 2)) / w);
            }
        }
        ipr::Image output = ipr::remapped(view.pixels, mapX, mapY, ipr::Resampling::Lanczos4, ipr::Outside::LastPixel);
        output = ipr::multiplied(output, std::vector<double>{view.gains[0], view.gains[1], view.gains[2]});
        report.sharpness.push_back(sharpnessOf(rootLuminance(output)));
        registered[i] = std::move(output);
    }

    std::vector<int> order(views.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const double sa = report.sharpness[size_t(a)], sb = report.sharpness[size_t(b)];
        return sa != sb ? sa > sb : a < b;
    });
    const int usable = int(std::count_if(registered.begin(), registered.end(), [](const ipr::Image &m) { return !m.empty(); }));
    const int wanted = std::max(options.minimumKept, int(std::ceil(options.keepShare * usable)));
    const int kept = std::min(wanted, usable);
    if (kept < options.minimumKept) {
        report.reason = QStringLiteral("Trop peu de vues utilisables sur le sujet.");
        return report;
    }
    std::vector<ipr::Image> best;
    for (int i = 0; i < kept; ++i)
        best.push_back(std::move(registered[size_t(order[size_t(i)])]));

    ipr::Image refined;
    if (!StackMerge::merge(best, options.rejectionSigma, refined)) {
        report.reason = QStringLiteral("Fusion impossible.");
        return report;
    }
    const ipr::Image mask = fadeMask(size, options.feather);
    ipr::Image area = merged.mutableView(subject);
    for (int y = 0; y < size.height; ++y) {
        ipr::Vec3f *out = area.mutableRow<ipr::Vec3f>(y);
        const ipr::Vec3f *in = refined.row<ipr::Vec3f>(y);
        const float *weight = mask.row<float>(y);
        for (int x = 0; x < size.width; ++x)
            out[x] = in[x] * weight[x] + out[x] * (1.f - weight[x]);
    }

    report.applied = true;
    report.kept = kept;
    if (!measured.empty()) {
        std::nth_element(measured.begin(), measured.begin() + std::ptrdiff_t(measured.size() / 2), measured.end());
        report.measuredBlocks = measured[measured.size() / 2];
    }
    return report;
}

} // namespace StackTurbulence
