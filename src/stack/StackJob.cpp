#include "StackJob.h"

#include "FrameSpool.h"

#include "OpenCvBridge.h"
#include "Parallel.h"

#include <QDir>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace StackJob {
namespace {

constexpr int proxyDivisor = 16;          // of the reference's side, to compare the levels of a frame
constexpr int interpolationMargin = 4;    // half the support of the resampling kernel, pixels
constexpr float darkestForGain = 0.005f;  // of white: below it, the noise decides the ratio
constexpr float brightestForGain = 0.9f;  // of white: above it, clipping does
constexpr double largestGainGap = 0.25;   // a frame further from the reference is another exposure
constexpr int minimumBandRows = 16;
constexpr double subjectMargin = 0.15;    // around what the matches span, of its size

// What a small subject needs around what the matches span.
ipr::Recti subjectArea(const ipr::Rectd &inliers, const ipr::Size &size)
{
    const double marginX = subjectMargin * inliers.width + 32.0, marginY = subjectMargin * inliers.height + 32.0;
    const ipr::Recti area(int(inliers.x - marginX), int(inliers.y - marginY),
                        int(inliers.width + 2.0 * marginX), int(inliers.height + 2.0 * marginY));
    return area & ipr::Recti(0, 0, size.width, size.height);
}

// The pixels of a frame that the subject's area can reach, kept as decoded.
StackTurbulence::View subjectView(const cv::Mat &frame, const ipr::Recti &subject, const ipr::Matrix33d &referenceToFrame, int reach)
{
    ipr::Point2d lowest(frame.cols, frame.rows), highest(0, 0);
    for (const ipr::Point2i corner : {subject.tl(), ipr::Point2i(subject.x + subject.width, subject.y),
                                   ipr::Point2i(subject.x, subject.y + subject.height), subject.br()}) {
        const ipr::Vec3d mapped = referenceToFrame * ipr::Vec3d(corner.x, corner.y, 1.0);
        const ipr::Point2d place(mapped[0] / mapped[2], mapped[1] / mapped[2]);
        lowest = {std::min(lowest.x, place.x), std::min(lowest.y, place.y)};
        highest = {std::max(highest.x, place.x), std::max(highest.y, place.y)};
    }
    const ipr::Recti wanted(int(lowest.x) - reach, int(lowest.y) - reach,
                          int(highest.x - lowest.x) + 2 * reach + 1, int(highest.y - lowest.y) + 2 * reach + 1);
    const ipr::Recti area = wanted & ipr::Recti(0, 0, frame.cols, frame.rows);
    StackTurbulence::View view;
    view.origin = area.tl();
    view.referenceToFrame = referenceToFrame;
    if (!area.empty())
        view.pixels = frame(ipr::toCv(area)).clone();
    return view;
}

cv::Mat proxyOf(const cv::Mat &image)
{
    cv::Mat proxy;
    proxy = ipr::resized(image, ipr::Size(std::max(1, image.cols / proxyDivisor), std::max(1, image.rows / proxyDivisor)),
                         ipr::Interpolation::Area);
    return proxy;
}

// Per channel, the median ratio of the reference to a registered frame, where both are
// well exposed. Corrects the flicker of a lamp and a different as-shot balance.
bool levelGains(const cv::Mat &referenceProxy, const cv::Mat &frameProxy, ipr::Vec3d &gains)
{
    std::array<std::vector<float>, 3> ratios;
    for (int y = 0; y < referenceProxy.rows; ++y) {
        const ipr::Vec3f *a = referenceProxy.ptr<ipr::Vec3f>(y), *b = frameProxy.ptr<ipr::Vec3f>(y);
        for (int x = 0; x < referenceProxy.cols; ++x)
            for (int c = 0; c < 3; ++c)
                if (a[x][c] > darkestForGain && b[x][c] > darkestForGain &&
                    a[x][c] < brightestForGain && b[x][c] < brightestForGain)
                    ratios[size_t(c)].push_back(a[x][c] / b[x][c]);
    }
    for (int c = 0; c < 3; ++c) {
        std::vector<float> &values = ratios[size_t(c)];
        if (values.empty())
            return false;
        const auto middle = values.begin() + std::ptrdiff_t(values.size() / 2);
        std::nth_element(values.begin(), middle, values.end());
        gains[c] = *middle;
    }
    return true;
}

// The source coordinates of each reference pixel: the local field, then the transform.
void sourceMaps(const ipr::Matrix33d &referenceToFrame, const cv::Mat &displacements, cv::Mat &mapX, cv::Mat &mapY)
{
    mapX.create(displacements.size(), CV_32FC1);
    mapY.create(displacements.size(), CV_32FC1);
    ipr::parallelRows(displacements.rows, [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const ipr::Vec2f *field = displacements.ptr<ipr::Vec2f>(y);
            float *outX = mapX.ptr<float>(y), *outY = mapY.ptr<float>(y);
            for (int x = 0; x < displacements.cols; ++x) {
                const double px = x + double(field[x][0]), py = y + double(field[x][1]);
                const double w = referenceToFrame(2, 0) * px + referenceToFrame(2, 1) * py + referenceToFrame(2, 2);
                outX[x] = float((referenceToFrame(0, 0) * px + referenceToFrame(0, 1) * py + referenceToFrame(0, 2)) / w);
                outY[x] = float((referenceToFrame(1, 0) * px + referenceToFrame(1, 1) * py + referenceToFrame(1, 2)) / w);
            }
        }
    });
}

// Where the frame's pixels, with the kernel's whole support, fall inside it. The preimage
// of a rectangle under a transform is convex: one span per row. A local field moves pixels
// by at most its longest displacement, which the margin covers.
void narrowSpans(const ipr::Matrix33d &referenceToFrame, const ipr::Size &size, double fieldReach, std::vector<ipr::Vec2i> &spans)
{
    const double margin = interpolationMargin + std::ceil(fieldReach);
    const double right = size.width - 1 - margin, bottom = size.height - 1 - margin;
    ipr::parallelRows(size.height, [&](int from, int to) {
        for (int y = from; y < to; ++y) {
            int first = size.width, last = -1;
            for (int x = 0; x < size.width; ++x) {
                const double w = referenceToFrame(2, 0) * x + referenceToFrame(2, 1) * y + referenceToFrame(2, 2);
                if (w <= 0.0)
                    continue;
                const double u = (referenceToFrame(0, 0) * x + referenceToFrame(0, 1) * y + referenceToFrame(0, 2)) / w;
                const double v = (referenceToFrame(1, 0) * x + referenceToFrame(1, 1) * y + referenceToFrame(1, 2)) / w;
                if (u >= margin && u <= right && v >= margin && v <= bottom) {
                    first = std::min(first, x);
                    last = x;
                }
            }
            ipr::Vec2i &span = spans[size_t(y)];
            span[0] = std::max(span[0], first);
            span[1] = std::min(span[1], last + 1);
        }
    });
}

} // namespace

ipr::Recti largestRectangle(const std::vector<ipr::Vec2i> &spans)
{
    ipr::Recti best;
    qint64 bestArea = 0;
    const int rows = int(spans.size());
    // In a convex area the narrowest row of a range is one of its two ends.
    for (int top = 0; top < rows; ++top) {
        if (spans[size_t(top)][1] <= spans[size_t(top)][0])
            continue;
        for (int bottom = rows - 1; bottom >= top; --bottom) {
            const int height = bottom - top + 1;
            if (spans[size_t(bottom)][1] <= spans[size_t(bottom)][0])
                continue;
            const int left = std::max(spans[size_t(top)][0], spans[size_t(bottom)][0]);
            const int right = std::min(spans[size_t(top)][1], spans[size_t(bottom)][1]);
            const qint64 area = qint64(std::max(0, right - left)) * height;
            if (area > bestArea) {
                bestArea = area;
                best = ipr::Recti(left, top, right - left, height);
            }
        }
    }
    return best;
}

quint64 scratchBytesNeeded(int frameCount, const ipr::Size &size)
{
    return quint64(std::max(0, frameCount)) * quint64(std::max(0, size.width)) * quint64(std::max(0, size.height)) *
           quint64(3 * sizeof(float));
}

Result run(const Request &request)
{
    Result result;
    const auto stopped = [&] {
        if (request.cancelled && request.cancelled()) {
            result.cancelled = true;
            result.error = QStringLiteral("Annulé.");
            return true;
        }
        return false;
    };
    if (request.frameCount < 2 || !request.frame) {
        result.error = QStringLiteral("Il faut au moins deux images pour un empilement.");
        return result;
    }
    if (request.referenceIndex < 0 || request.referenceIndex >= request.frameCount) {
        result.error = QStringLiteral("L'image de référence n'appartient pas à la série.");
        return result;
    }
    if (stopped())
        return result;

    int done = 0;
    int total = request.frameCount + 1;
    const auto advance = [&] {
        if (request.progress)
            request.progress(++done, total);
    };

    Frame reference = request.frame(request.referenceIndex);
    if (reference.cameraRgb.empty() || reference.cameraRgb.type() != CV_32FC3) {
        result.error = reference.error.isEmpty()
            ? QStringLiteral("L'image de référence n'a pas pu être décodée.") : reference.error;
        return result;
    }
    const ipr::Size size = ipr::fromCv(reference.cameraRgb.size());
    result.frameSize = size;

    const QString root = request.scratchRoot.isEmpty() ? QDir::tempPath() : request.scratchRoot;
    const QStorageInfo storage(root);
    const quint64 needed = scratchBytesNeeded(request.frameCount, size);
    if (!storage.isValid() || quint64(std::max<qint64>(0, storage.bytesAvailable())) < needed + request.minimumFreeDiskBytes) {
        result.error = QStringLiteral("Espace disque insuffisant dans %1 : %2 Go nécessaires pour le tampon.")
            .arg(root).arg(double(needed + request.minimumFreeDiskBytes) / 1e9, 0, 'f', 1);
        return result;
    }
    // Declared before the spool: the files close before their folder goes.
    QTemporaryDir scratch(QDir(root).filePath(QStringLiteral("ipr-stack-XXXXXX")));
    if (!scratch.isValid()) {
        result.error = QStringLiteral("Impossible de créer le dossier tampon dans %1.").arg(root);
        return result;
    }
    std::vector<std::unique_ptr<FrameSpool>> spool;

    std::vector<ipr::Vec2i> spans(size_t(size.height), ipr::Vec2i(0, size.width));
    ipr::Recti subject;
    std::vector<StackTurbulence::View> views;
    StackTurbulence::View pendingView;
    bool hasPendingView = false;
    // The reference waits for the first frame that tells whether the subject is small.
    cv::Mat referencePixels;
    const StackAlignment::Pyramid referencePyramid = StackAlignment::buildPyramid(reference.cameraRgb, request.alignment);
    const cv::Mat referenceProxy = proxyOf(reference.cameraRgb);

    const auto spoolFrame = [&](const cv::Mat &image) {
        auto file = std::make_unique<FrameSpool>(scratch.filePath(QStringLiteral("%1.frame").arg(spool.size())));
        if (!file->write(image, result.scratchBytes))
            return false;
        spool.push_back(std::move(file));
        return true;
    };

    result.frames.resize(size_t(request.frameCount));
    FrameReport &referenceReport = result.frames[size_t(request.referenceIndex)];
    referenceReport.index = request.referenceIndex;
    referenceReport.reference = true;
    referenceReport.used = request.mergeReference;
    referenceReport.alignment.valid = true;
    if (request.mergeReference && !spoolFrame(reference.cameraRgb)) {
        result.error = QStringLiteral("Écriture impossible dans le dossier tampon (disque plein ?).");
        return result;
    }
    if (request.correctTurbulence && request.mergeReference)
        referencePixels = reference.cameraRgb;
    reference.cameraRgb.release();
    advance();

    for (int index = 0; index < request.frameCount; ++index) {
        if (index == request.referenceIndex)
            continue;
        if (stopped())
            return result;
        FrameReport &report = result.frames[size_t(index)];
        report.index = index;
        hasPendingView = false;
        Frame frame = request.frame(index);
        if (frame.cameraRgb.empty() || frame.cameraRgb.type() != CV_32FC3 || ipr::fromCv(frame.cameraRgb.size()) != size) {
            report.exclusion = !frame.error.isEmpty() ? frame.error
                : frame.cameraRgb.empty() ? QStringLiteral("Image illisible.")
                                          : QStringLiteral("Dimensions différentes de l'image de référence.");
            advance();
            continue;
        }
        const StackAlignment::Pyramid framePyramid = StackAlignment::buildPyramid(frame.cameraRgb, request.alignment);
        report.alignment = StackAlignment::estimate(referencePyramid, framePyramid, request.alignment, request.cancelled);
        if (stopped())
            return result;
        if (!report.alignment.valid) {
            report.exclusion = report.alignment.error;
            advance();
            continue;
        }
        // A small subject: its original pixels wait for the second pass.
        if (request.correctTurbulence && report.alignment.model == StackAlignment::Model::Similarity) {
            if (subject.empty())
                subject = subjectArea(report.alignment.inlierArea, size);
            const quint64 viewBytes = quint64(subject.area()) * quint64(3 * sizeof(float));
            const int reach = request.turbulence.local.radius + request.turbulence.local.blockHalf + interpolationMargin;
            if (viewBytes * quint64(request.frameCount) <= request.memoryBudgetBytes) {
                if (!referencePixels.empty())
                    views.push_back(subjectView(referencePixels, subject, ipr::Matrix33d::identity(), reach));
                pendingView = subjectView(frame.cameraRgb, subject, report.alignment.referenceToFrame, reach);
                hasPendingView = true;
            } else {
                result.turbulence.reason = QStringLiteral("Sujet trop grand pour la seconde passe.");
            }
        }
        referencePixels.release();
        cv::Mat registered;
        double fieldReach = 0.0;
        if (request.localRefinement) {
            // Measured on a cheap resampling, so that the pixels are resampled only once.
            const cv::Mat roughly = ipr::warpedPerspective(framePyramid[0], report.alignment.referenceToFrame, size, ipr::Resampling::Linear, ipr::Outside::LastPixel);
            const StackAlignment::LocalField field = StackAlignment::localField(referencePyramid[0], roughly, request.local);
            report.localBlocks = field.measured;
            report.localLargest = field.largest;
            fieldReach = field.largest;
            cv::Mat mapX, mapY;
            sourceMaps(report.alignment.referenceToFrame, field.displacements, mapX, mapY);
            registered = ipr::remapped(frame.cameraRgb, mapX, mapY, ipr::Resampling::Lanczos4, ipr::Outside::LastPixel);
        } else {
            registered = ipr::warpedPerspective(frame.cameraRgb, report.alignment.referenceToFrame, size, ipr::Resampling::Lanczos4, ipr::Outside::LastPixel);
        }
        frame.cameraRgb.release();
        if (!levelGains(referenceProxy, proxyOf(registered), report.gains)) {
            report.exclusion = QStringLiteral("Aucune zone bien exposée à comparer à la référence.");
            advance();
            continue;
        }
        const double gap = std::max({std::abs(report.gains[0] - 1.0), std::abs(report.gains[1] - 1.0), std::abs(report.gains[2] - 1.0)});
        if (gap > largestGainGap) {
            report.exclusion = QStringLiteral("Exposition trop différente de la référence.");
            advance();
            continue;
        }
        registered = ipr::multiplied(registered, std::vector<double>{report.gains[0], report.gains[1], report.gains[2]});
        if (!spoolFrame(registered)) {
            result.error = QStringLiteral("Écriture impossible dans le dossier tampon (disque plein ?).");
            return result;
        }
        if (hasPendingView) {
            pendingView.gains = report.gains;
            views.push_back(std::move(pendingView));
            hasPendingView = false;
        }
        narrowSpans(report.alignment.referenceToFrame, size, fieldReach, spans);
        report.used = true;
        advance();
    }

    result.usedFrames = int(spool.size());
    if (result.usedFrames < (request.mergeReference ? 2 : 1)) {
        result.error = QStringLiteral("Aucune autre image n'a pu être recalée sur la référence.");
        return result;
    }
    result.commonArea = largestRectangle(spans);
    if (result.commonArea.empty()) {
        result.error = QStringLiteral("Les images recalées n'ont aucune zone commune.");
        return result;
    }

    const quint64 rowBytes = quint64(size.width) * quint64(3 * sizeof(float));
    const int bandRows = int(std::clamp<quint64>(request.memoryBudgetBytes / std::max<quint64>(1, rowBytes * quint64(result.usedFrames)),
                                                 quint64(minimumBandRows), quint64(std::max(minimumBandRows, result.commonArea.height))));
    result.bandRows = bandRows;
    const int bands = (result.commonArea.height + bandRows - 1) / bandRows;
    total = request.frameCount + bands;
    result.cameraRgb.create(ipr::toCv(result.commonArea.size()), CV_32FC3);
    std::vector<cv::Mat> rows(spool.size()), cut(spool.size());
    for (int band = 0; band < bands; ++band) {
        if (stopped()) {
            result.cameraRgb.release();
            return result;
        }
        const int top = band * bandRows;
        const int height = std::min(bandRows, result.commonArea.height - top);
        // The decisions look around a pixel: each part reads that much more, the same
        // context whatever the bands. The common area keeps a margin inside the frame.
        const int context = (StackMerge::decisionWindow - 1) / 2;
        const ipr::Recti wanted(result.commonArea.x - context, result.commonArea.y + top - context,
                              result.commonArea.width + 2 * context, height + 2 * context);
        const ipr::Recti readable = wanted & ipr::Recti(0, 0, size.width, size.height);
        for (size_t i = 0; i < spool.size(); ++i) {
            if (!spool[i]->read(size, CV_32FC3, readable.y, readable.height, rows[i])) {
                result.cameraRgb.release();
                result.error = QStringLiteral("Lecture impossible dans le dossier tampon.");
                return result;
            }
            cut[i] = rows[i](ipr::toCv(ipr::Recti(readable.x, 0, readable.width, readable.height)));
        }
        cv::Mat merged;
        StackMerge::Statistics statistics;
        if (!StackMerge::merge(cut, request.rejectionSigma, merged, &statistics)) {
            result.cameraRgb.release();
            result.error = QStringLiteral("Fusion impossible.");
            return result;
        }
        merged(ipr::toCv(ipr::Recti(result.commonArea.x - readable.x, result.commonArea.y + top - readable.y, result.commonArea.width, height)))
            .copyTo(result.cameraRgb(ipr::toCv(ipr::Recti(0, top, result.commonArea.width, height))));
        result.merge.samples += statistics.samples;
        result.merge.rejected += statistics.rejected;
        advance();
    }
    if (!views.empty()) {
        const ipr::Recti inMerged = (subject & result.commonArea) - result.commonArea.tl();
        result.subject = inMerged + result.commonArea.tl();
        StackTurbulence::Options options = request.turbulence;
        options.rejectionSigma = request.rejectionSigma;
        result.turbulence = StackTurbulence::refine(result.cameraRgb, inMerged, result.commonArea.tl(), views, options, request.cancelled);
        if (stopped()) {
            result.cameraRgb.release();
            return result;
        }
    }
    result.success = true;
    return result;
}

} // namespace StackJob
