#include "StackTurbulence.h"

#include "StackMerge.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace StackTurbulence {
namespace {

cv::Mat rootLuminance(const cv::Mat &rgb)
{
    cv::Mat luminance(rgb.size(), CV_32FC1);
    for (int y = 0; y < rgb.rows; ++y) {
        const cv::Vec3f *in = rgb.ptr<cv::Vec3f>(y);
        float *out = luminance.ptr<float>(y);
        for (int x = 0; x < rgb.cols; ++x)
            out[x] = std::sqrt(std::max(0.f, 0.25f * in[x][0] + 0.5f * in[x][1] + 0.25f * in[x][2]));
    }
    return luminance;
}

// Fine detail of a view, read at a scale above the noise of a single pixel.
double sharpnessOf(const cv::Mat &luminance)
{
    cv::Mat smoothed, detail;
    cv::GaussianBlur(luminance, smoothed, cv::Size(), 1.0);
    cv::Laplacian(smoothed, detail, CV_32F);
    cv::Scalar mean, deviation;
    cv::meanStdDev(detail, mean, deviation);
    return deviation[0] * deviation[0];
}

// 1 inside, falling to 0 at the area's edge.
cv::Mat fadeMask(const cv::Size &size, int feather)
{
    cv::Mat mask(size, CV_32FC1);
    const float reach = float(std::max(1, feather));
    for (int y = 0; y < size.height; ++y) {
        float *out = mask.ptr<float>(y);
        for (int x = 0; x < size.width; ++x) {
            const float edge = float(std::min({x, y, size.width - 1 - x, size.height - 1 - y}));
            const float t = std::clamp(edge / reach, 0.f, 1.f);
            out[x] = t * t * (3.f - 2.f * t);
        }
    }
    return mask;
}

} // namespace

Report refine(cv::Mat &merged, const cv::Rect &subject, const cv::Point &origin, const std::vector<View> &views,
              const Options &options, const std::function<bool()> &cancelled)
{
    Report report;
    report.views = int(views.size());
    const cv::Rect bounds(0, 0, merged.cols, merged.rows);
    if (merged.empty() || merged.type() != CV_32FC3 || subject.empty() || (subject & bounds) != subject) {
        report.reason = QStringLiteral("Zone du sujet invalide.");
        return report;
    }
    if (int(views.size()) < options.minimumKept) {
        report.reason = QStringLiteral("Trop peu de vues pour choisir les plus nettes.");
        return report;
    }

    const cv::Size size = subject.size();
    const cv::Mat reference = rootLuminance(merged(subject));
    // From the subject's own coordinates to the frame's.
    const cv::Matx33d subjectToReference(1, 0, double(origin.x + subject.x), 0, 1, double(origin.y + subject.y), 0, 0, 1);

    std::vector<cv::Mat> registered(views.size());
    std::vector<int> measured;
    for (size_t i = 0; i < views.size(); ++i) {
        if (cancelled && cancelled()) {
            report.reason = QStringLiteral("Annulé.");
            return report;
        }
        const View &view = views[i];
        if (view.pixels.empty() || view.pixels.type() != CV_32FC3) {
            report.sharpness.push_back(0.0);
            continue;
        }
        const cv::Matx33d frameToPixels(1, 0, -double(view.origin.x), 0, 1, -double(view.origin.y), 0, 0, 1);
        const cv::Matx33d subjectToPixels = frameToPixels * view.referenceToFrame * subjectToReference;
        cv::Mat roughly;
        cv::warpPerspective(rootLuminance(view.pixels), roughly, subjectToPixels, size,
                            cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE);
        const StackAlignment::LocalField field = StackAlignment::localField(reference, roughly, options.local);
        if (field.displacements.empty()) {
            report.sharpness.push_back(0.0);
            continue;
        }
        measured.push_back(field.measured);
        report.largestDisplacement = std::max(report.largestDisplacement, field.largest);

        cv::Mat mapX(size, CV_32FC1), mapY(size, CV_32FC1);
        for (int y = 0; y < size.height; ++y) {
            const cv::Vec2f *move = field.displacements.ptr<cv::Vec2f>(y);
            float *outX = mapX.ptr<float>(y), *outY = mapY.ptr<float>(y);
            for (int x = 0; x < size.width; ++x) {
                const double px = x + double(move[x][0]), py = y + double(move[x][1]);
                const double w = subjectToPixels(2, 0) * px + subjectToPixels(2, 1) * py + subjectToPixels(2, 2);
                outX[x] = float((subjectToPixels(0, 0) * px + subjectToPixels(0, 1) * py + subjectToPixels(0, 2)) / w);
                outY[x] = float((subjectToPixels(1, 0) * px + subjectToPixels(1, 1) * py + subjectToPixels(1, 2)) / w);
            }
        }
        cv::Mat output;
        cv::remap(view.pixels, output, mapX, mapY, cv::INTER_LANCZOS4, cv::BORDER_REPLICATE);
        cv::multiply(output, cv::Scalar(view.gains[0], view.gains[1], view.gains[2]), output);
        report.sharpness.push_back(sharpnessOf(rootLuminance(output)));
        registered[i] = std::move(output);
    }

    std::vector<int> order(views.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const double sa = report.sharpness[size_t(a)], sb = report.sharpness[size_t(b)];
        return sa != sb ? sa > sb : a < b;
    });
    const int usable = int(std::count_if(registered.begin(), registered.end(), [](const cv::Mat &m) { return !m.empty(); }));
    const int wanted = std::max(options.minimumKept, int(std::ceil(options.keepShare * usable)));
    const int kept = std::min(wanted, usable);
    if (kept < options.minimumKept) {
        report.reason = QStringLiteral("Trop peu de vues utilisables sur le sujet.");
        return report;
    }
    std::vector<cv::Mat> best;
    for (int i = 0; i < kept; ++i)
        best.push_back(registered[size_t(order[size_t(i)])]);

    cv::Mat refined;
    if (!StackMerge::merge(best, options.rejectionSigma, refined)) {
        report.reason = QStringLiteral("Fusion impossible.");
        return report;
    }
    const cv::Mat mask = fadeMask(size, options.feather);
    cv::Mat area = merged(subject);
    for (int y = 0; y < size.height; ++y) {
        cv::Vec3f *out = area.ptr<cv::Vec3f>(y);
        const cv::Vec3f *in = refined.ptr<cv::Vec3f>(y);
        const float *weight = mask.ptr<float>(y);
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
