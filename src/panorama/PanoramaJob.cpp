#include "PanoramaJob.h"

#include "FrameSpool.h"
#include "PanoramaBlend.h"
#include "PanoramaFill.h"

#include <QDir>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace PanoramaJob {
namespace {

constexpr int detectionSide = 1600;       // long side the corners are looked for on, pixels
constexpr int refinementSide = 3000;      // long side the matches are measured again on
constexpr int reducedCanvasSide = 2400;   // long side of the canvas seams are decided on
constexpr double reducedCanvasCells = 600000.0; // and its size: frames that nearly coincide must stay cheap
constexpr double narrowSigmaPixels = 8.0; // transition of the detail at a seam, canvas pixels
constexpr double wideSigmaPixels = 120.0; // transition of the slow variations
constexpr int interpolationMargin = 4;    // half the support of the resampling kernel, pixels
constexpr double largestFocalGap = 0.02;  // a zoomed frame is not of this series
constexpr int minimumBandRows = 16;
constexpr int largestCanvasSide = 65000;  // of what a DNG reader is asked to open
constexpr int largestWarp = 32000;        // of an area resampled at once

// Square-root luminance, reduced by whole cells so that it stays faithful to the frame.
cv::Mat perceptualCopy(const cv::Mat &cameraRgb, double scale)
{
    cv::Mat reduced;
    cv::resize(cameraRgb, reduced, cv::Size(), scale, scale, scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);
    cv::Mat out(reduced.size(), CV_32FC1);
    cv::parallel_for_(cv::Range(0, reduced.rows), [&](const cv::Range &range) {
        for (int y = range.start; y < range.end; ++y) {
            const cv::Vec3f *in = reduced.ptr<cv::Vec3f>(y);
            float *target = out.ptr<float>(y);
            for (int x = 0; x < reduced.cols; ++x)
                target[x] = std::sqrt(std::max(0.0f, 0.25f * in[x][0] + 0.5f * in[x][1] + 0.25f * in[x][2]));
        }
    });
    return out;
}

// The reduced copy of a warped frame: the mean of the valid pixels of each cell.
PanoramaBlend::View reducedView(const cv::Mat &warped, const cv::Mat &valid, const cv::Rect &area, int cell)
{
    PanoramaBlend::View view;
    const int left = area.x / cell, top = area.y / cell;
    const int right = (area.x + area.width + cell - 1) / cell, bottom = (area.y + area.height + cell - 1) / cell;
    view.area = cv::Rect(left, top, right - left, bottom - top);
    cv::Mat sums = cv::Mat::zeros(view.area.size(), CV_32FC3);
    cv::Mat counts = cv::Mat::zeros(view.area.size(), CV_32SC1);
    for (int y = 0; y < warped.rows; ++y) {
        const cv::Vec3f *colour = warped.ptr<cv::Vec3f>(y);
        const uchar *inside = valid.ptr<uchar>(y);
        cv::Vec3f *sumRow = sums.ptr<cv::Vec3f>((y + area.y) / cell - top);
        int *countRow = counts.ptr<int>((y + area.y) / cell - top);
        for (int x = 0; x < warped.cols; ++x) {
            if (!inside[x])
                continue;
            const int at = (x + area.x) / cell - left;
            sumRow[at] += colour[x];
            ++countRow[at];
        }
    }
    view.colour.create(view.area.size(), CV_32FC3);
    view.whole.create(view.area.size(), CV_8UC1);
    view.any.create(view.area.size(), CV_8UC1);
    for (int y = 0; y < view.area.height; ++y) {
        const cv::Vec3f *sumRow = sums.ptr<cv::Vec3f>(y);
        const int *countRow = counts.ptr<int>(y);
        cv::Vec3f *colourRow = view.colour.ptr<cv::Vec3f>(y);
        uchar *wholeRow = view.whole.ptr<uchar>(y), *anyRow = view.any.ptr<uchar>(y);
        for (int x = 0; x < view.area.width; ++x) {
            colourRow[x] = countRow[x] > 0 ? sumRow[x] * (1.0f / float(countRow[x])) : cv::Vec3f(0, 0, 0);
            wholeRow[x] = countRow[x] == cell * cell ? 255 : 0;
            anyRow[x] = countRow[x] > 0 ? 255 : 0;
        }
    }
    return view;
}

} // namespace

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
        result.error = QStringLiteral("Il faut au moins deux images pour un panorama.");
        return result;
    }
    if (stopped())
        return result;

    int done = 0;
    // Frames, pairs, the geometry, frames on the canvas, the seams, the first band.
    int total = request.frameCount * 2 + request.frameCount * (request.frameCount - 1) / 2 + 3;
    const auto advance = [&] {
        if (request.progress)
            request.progress(++done, total);
    };

    const QString root = request.scratchRoot.isEmpty() ? QDir::tempPath() : request.scratchRoot;
    // Declared before the spools: the files close before their folder goes.
    QTemporaryDir scratch(QDir(root).filePath(QStringLiteral("ipr-panorama-XXXXXX")));
    if (!scratch.isValid()) {
        result.error = QStringLiteral("Impossible de créer le dossier tampon dans %1.").arg(root);
        return result;
    }
    const auto freeBytes = [&] {
        const QStorageInfo storage(scratch.path());
        return storage.isValid() ? quint64(std::max<qint64>(0, storage.bytesAvailable())) : quint64(0);
    };

    // Every frame once: kept on disk, its corners and a finer copy kept in memory.
    result.frames.resize(size_t(request.frameCount));
    std::vector<std::unique_ptr<FrameSpool>> sources(static_cast<size_t>(request.frameCount));
    std::vector<std::vector<PanoramaFeatures::Feature>> features(static_cast<size_t>(request.frameCount));
    std::vector<cv::Mat> finer(static_cast<size_t>(request.frameCount));
    cv::Size size;
    double focalMm = 0.0, finerScale = 1.0, detectionScale = 1.0;
    PanoramaGeometry::LensKnowledge lens;
    for (int index = 0; index < request.frameCount; ++index) {
        if (stopped())
            return result;
        FrameReport &report = result.frames[size_t(index)];
        report.index = index;
        Frame frame = request.frame(index);
        if (frame.cameraRgb.empty() || frame.cameraRgb.type() != CV_32FC3) {
            report.exclusion = frame.error.isEmpty() ? QStringLiteral("Image illisible.") : frame.error;
            advance();
            continue;
        }
        if (size.empty()) {
            size = frame.cameraRgb.size();
            result.frameSize = size;
            focalMm = frame.focalMm;
            if (frame.focalMm > 0.0 && frame.sensorWidthMm > 0.0)
                lens.focalPixels = frame.focalMm * std::max(size.width, size.height) / frame.sensorWidthMm;
            lens.distortionCorrected = frame.lensGeometryCorrected;
            finerScale = std::min(1.0, double(refinementSide) / std::max(size.width, size.height));
            detectionScale = std::min(1.0, double(detectionSide) / std::max(size.width, size.height));
            const quint64 frameBytes = quint64(size.area()) * quint64(3 * sizeof(float));
            // The frames, then a resampled copy of each: the sources leave as they are used.
            const quint64 needed = frameBytes * quint64(request.frameCount) * 2u;
            if (freeBytes() < needed + request.minimumFreeDiskBytes) {
                result.error = QStringLiteral("Espace disque insuffisant dans %1 : %2 Go nécessaires pour le tampon.")
                    .arg(root).arg(double(needed + request.minimumFreeDiskBytes) / 1e9, 0, 'f', 1);
                return result;
            }
        }
        if (frame.cameraRgb.size() != size) {
            report.exclusion = QStringLiteral("Dimensions différentes du reste de la série.");
            advance();
            continue;
        }
        if (focalMm > 0.0 && frame.focalMm > 0.0 && std::abs(frame.focalMm - focalMm) > largestFocalGap * focalMm) {
            report.exclusion = QStringLiteral("Focale différente du reste de la série.");
            advance();
            continue;
        }
        const cv::Mat detection = perceptualCopy(frame.cameraRgb, detectionScale);
        features[size_t(index)] = PanoramaFeatures::detect(detection);
        report.features = int(features[size_t(index)].size());
        for (PanoramaFeatures::Feature &feature : features[size_t(index)])
            feature.position = (feature.position + cv::Point2f(0.5f, 0.5f)) * float(1.0 / detectionScale) - cv::Point2f(0.5f, 0.5f);
        finer[size_t(index)] = perceptualCopy(frame.cameraRgb, finerScale);
        auto spool = std::make_unique<FrameSpool>(scratch.filePath(QStringLiteral("%1.source").arg(index)));
        if (!spool->write(frame.cameraRgb, result.scratchBytes)) {
            result.error = QStringLiteral("Écriture impossible dans le dossier tampon (disque plein ?).");
            return result;
        }
        sources[size_t(index)] = std::move(spool);
        advance();
    }
    if (size.empty()) {
        result.error = QStringLiteral("Aucune image de la série n'a pu être décodée.");
        return result;
    }

    // Which frames overlap: every pair, the order of a series says nothing about its layout.
    std::vector<PanoramaGeometry::Pair> pairs;
    for (int a = 0; a < request.frameCount; ++a) {
        for (int b = a + 1; b < request.frameCount; ++b) {
            if (stopped())
                return result;
            advance();
            if (features[size_t(a)].empty() || features[size_t(b)].empty())
                continue;
            PanoramaGeometry::Pair pair;
            pair.a = a;
            pair.b = b;
            const auto matches = PanoramaFeatures::match(features[size_t(a)], features[size_t(b)]);
            if (PanoramaGeometry::verifyPair(matches, size, {}, pair))
                pairs.push_back(std::move(pair));
        }
    }
    features.clear();

    PanoramaGeometry::Solution solution = PanoramaGeometry::solve(request.frameCount, std::move(pairs), size, lens, {}, request.cancelled);
    if (stopped())
        return result;
    if (!solution.valid) {
        result.error = QStringLiteral("Ces photos ne se recouvrent pas assez pour former un panorama.");
        return result;
    }
    PanoramaGeometry::refine(solution, finer, finerScale, size, lens, {}, request.cancelled);
    finer.clear();
    if (stopped())
        return result;
    if (!solution.valid) {
        result.error = QStringLiteral("Ces photos ne s'accordent pas sur une même géométrie : ont-elles été prises du même point ?");
        return result;
    }
    advance();

    for (FrameReport &report : result.frames)
        if (report.exclusion.isEmpty())
            report.exclusion = QStringLiteral("Aucun recouvrement fiable avec le reste de la série.");
    for (const int member : solution.members) {
        result.frames[size_t(member)].used = true;
        result.frames[size_t(member)].exclusion.clear();
    }
    for (const PanoramaGeometry::Pair &pair : solution.pairs)
        result.pairs.push_back({pair.a, pair.b, int(pair.pointsA.size()), pair.residual});
    result.usedFrames = int(solution.members.size());
    result.focal = solution.focal;
    result.distortion = solution.distortion;
    result.residual = solution.residual;
    result.worstPair = solution.worstPair;
    result.shape = PanoramaGeometry::straighten(solution, size, request.projection);

    // The canvas.
    PanoramaGeometry::Surface surface;
    surface.projection = result.shape.projection;
    surface.layout = result.shape.layout;
    surface.scale = solution.focal;
    std::vector<cv::Rect2d> outlines;
    cv::Rect2d whole;
    for (const int member : solution.members) {
        PanoramaGeometry::FrameView view{solution.worldToCamera[size_t(member)], solution.focal, solution.distortion, size};
        cv::Rect2d area;
        if (!PanoramaGeometry::outline(surface, view, area)) {
            result.error = QStringLiteral("Ce champ dépasse ce qu'une projection plane peut tenir : choisissez une projection cylindrique ou sphérique.");
            return result;
        }
        whole = outlines.empty() ? area : (area | whole);
        outlines.push_back(area);
    }
    surface.origin = cv::Point2d(std::floor(whole.x) - 1.0, std::floor(whole.y) - 1.0);
    result.canvas = cv::Size(int(std::ceil(whole.width)) + 3, int(std::ceil(whole.height)) + 3);
    const double megapixels = double(result.canvas.width) * double(result.canvas.height) / 1e6;
    if (megapixels > request.maximumMegapixels || std::max(result.canvas.width, result.canvas.height) > largestCanvasSide) {
        result.error = QStringLiteral("Panorama trop grand : %1 × %2 pixels, %3 Mpx au plus.")
            .arg(result.canvas.width).arg(result.canvas.height).arg(request.maximumMegapixels, 0, 'f', 0);
        return result;
    }
    if (request.reserveCanvas) {
        // A fill holds a mask, a perceptual copy and a small pyramid on top of the canvas.
        const quint64 pixels = quint64(result.canvas.width) * quint64(result.canvas.height);
        const quint64 perPixel = request.edges == Edges::Fill ? 2u * 3u * sizeof(float) + 2u : 3u * sizeof(float);
        const QString refusal = request.reserveCanvas(pixels * perPixel);
        if (!refusal.isEmpty()) {
            result.error = refusal;
            return result;
        }
    }

    // Each frame on the canvas, to disk, with a reduced copy kept in memory.
    const int cell = std::max({1, int(std::ceil(double(std::max(result.canvas.width, result.canvas.height)) / reducedCanvasSide)),
                               int(std::ceil(std::sqrt(double(result.canvas.width) * result.canvas.height / reducedCanvasCells)))});
    result.seamCellPixels = cell;
    const cv::Size reducedCanvas((result.canvas.width + cell - 1) / cell, (result.canvas.height + cell - 1) / cell);
    std::vector<PanoramaBlend::View> views;
    std::vector<cv::Rect> areas;
    std::vector<std::unique_ptr<FrameSpool>> warpedColour, warpedValid;
    const cv::Rect canvasArea(0, 0, result.canvas.width, result.canvas.height);
    for (size_t m = 0; m < solution.members.size(); ++m) {
        if (stopped())
            return result;
        const int member = solution.members[m];
        PanoramaGeometry::FrameView frameView{solution.worldToCamera[size_t(member)], solution.focal, solution.distortion, size};
        cv::Rect2d placed;
        PanoramaGeometry::outline(surface, frameView, placed);
        const cv::Rect area = cv::Rect(int(std::floor(placed.x)) - 1, int(std::floor(placed.y)) - 1,
                                       int(std::ceil(placed.width)) + 3, int(std::ceil(placed.height)) + 3) & canvasArea;
        if (area.width >= largestWarp || area.height >= largestWarp) {
            result.error = QStringLiteral("Une photo occupe une trop grande partie de cette projection : choisissez-en une autre.");
            return result;
        }
        cv::Mat source;
        if (!sources[size_t(member)]->read(size, CV_32FC3, 0, size.height, source)) {
            result.error = QStringLiteral("Lecture impossible dans le dossier tampon.");
            return result;
        }
        sources[size_t(member)]->remove();
        cv::Mat mapX, mapY, valid, warped;
        PanoramaGeometry::sourceMaps(surface, frameView, area, interpolationMargin, mapX, mapY, valid);
        cv::remap(source, warped, mapX, mapY, cv::INTER_LANCZOS4, cv::BORDER_REPLICATE);
        source.release();
        mapX.release();
        mapY.release();
        views.push_back(reducedView(warped, valid, area, cell));
        areas.push_back(area);
        result.frames[size_t(member)].area = area;

        auto colourSpool = std::make_unique<FrameSpool>(scratch.filePath(QStringLiteral("%1.warped").arg(member)));
        auto validSpool = std::make_unique<FrameSpool>(scratch.filePath(QStringLiteral("%1.valid").arg(member)));
        if (!colourSpool->write(warped, result.scratchBytes) || !validSpool->write(valid, result.scratchBytes)) {
            result.error = QStringLiteral("Écriture impossible dans le dossier tampon (disque plein ?).");
            return result;
        }
        warpedColour.push_back(std::move(colourSpool));
        warpedValid.push_back(std::move(validSpool));
        advance();
    }
    sources.clear();

    PanoramaBlend::levelGains(views);
    for (size_t m = 0; m < views.size(); ++m)
        result.frames[size_t(solution.members[m])].gains = views[m].gains;

    const cv::Mat labels = PanoramaBlend::seams(views, reducedCanvas, request.cancelled);
    if (stopped())
        return result;
    if (labels.empty()) {
        result.error = QStringLiteral("Calcul des raccords impossible.");
        return result;
    }
    advance();
    std::vector<PanoramaBlend::Layer> layers;
    const double narrowSigma = std::max(0.7, narrowSigmaPixels / cell), wideSigma = std::max(1.5, wideSigmaPixels / cell);
    for (size_t m = 0; m < views.size(); ++m)
        layers.push_back(PanoramaBlend::prepareLayer(views[m], labels, int(m), narrowSigma, wideSigma));

    // Whole cells only: the rectangle has no gap, at the price of a few pixels at its edge.
    cv::Mat covered = cv::Mat::zeros(reducedCanvas, CV_8UC1);
    for (const PanoramaBlend::View &view : views) {
        const cv::Rect inCanvas = view.area & cv::Rect(0, 0, reducedCanvas.width, reducedCanvas.height);
        cv::Mat seat = covered(inCanvas);
        cv::bitwise_or(seat, view.whole(inCanvas - view.area.tl()), seat);
    }
    result.coveredShare = double(cv::countNonZero(covered)) / std::max(1.0, double(reducedCanvas.area()));
    views.clear();
    if (request.edges == Edges::Crop) {
        const cv::Rect best = PanoramaBlend::lowRectangle(covered);
        if (best.empty()) {
            result.error = QStringLiteral("Aucun rectangle plein dans ce panorama.");
            return result;
        }
        result.kept = cv::Rect(best.x * cell, best.y * cell, best.width * cell, best.height * cell) & canvasArea;
    } else {
        result.kept = canvasArea;
    }

    // Written by bands: only the rows being joined are in memory.
    qint64 rowBytes = 0;
    for (const cv::Rect &area : areas)
        rowBytes += qint64(area.width) * qint64(3 * sizeof(float) + 1);
    const int bandRows = int(std::clamp<qint64>(qint64(request.memoryBudgetBytes) / std::max<qint64>(1, rowBytes),
                                                minimumBandRows, std::max(minimumBandRows, result.kept.height)));
    result.bandRows = bandRows;
    const int bands = (result.kept.height + bandRows - 1) / bandRows;
    total = done + bands;
    result.cameraRgb.create(result.kept.size(), CV_32FC3);
    // Pixel by pixel, what the frames really cover: only a fill needs it.
    cv::Mat real;
    if (request.edges == Edges::Fill)
        real = cv::Mat::zeros(result.kept.size(), CV_8UC1);
    std::vector<cv::Mat> colourBands(areas.size()), validBands(areas.size());
    for (int band = 0; band < bands; ++band) {
        if (stopped()) {
            result.cameraRgb.release();
            return result;
        }
        const cv::Rect region(result.kept.x, result.kept.y + band * bandRows, result.kept.width,
                              std::min(bandRows, result.kept.height - band * bandRows));
        std::vector<PanoramaBlend::Pixels> pixels;
        for (size_t m = 0; m < areas.size(); ++m) {
            const cv::Rect part = areas[m] & region;
            if (part.empty())
                continue;
            const int top = part.y, rows = part.height;
            if (!warpedColour[m]->read(areas[m].size(), CV_32FC3, top - areas[m].y, rows, colourBands[m]) ||
                !warpedValid[m]->read(areas[m].size(), CV_8UC1, top - areas[m].y, rows, validBands[m])) {
                result.cameraRgb.release();
                result.error = QStringLiteral("Lecture impossible dans le dossier tampon.");
                return result;
            }
            PanoramaBlend::Pixels entry;
            entry.layer = &layers[m];
            entry.area = cv::Rect(areas[m].x, top, areas[m].width, rows);
            entry.colour = colourBands[m];
            entry.valid = validBands[m];
            pixels.push_back(entry);
            if (!real.empty()) {
                const cv::Rect inBand(part.x - areas[m].x, 0, part.width, part.height);
                cv::Mat seat = real(part - result.kept.tl());
                cv::bitwise_or(seat, validBands[m](inBand), seat);
            }
        }
        cv::Mat blended;
        PanoramaBlend::blendBand(pixels, cell, region, blended);
        blended.copyTo(result.cameraRgb(cv::Rect(0, band * bandRows, region.width, region.height)));
        advance();
    }
    for (const std::unique_ptr<FrameSpool> &spool : warpedColour)
        spool->remove();
    for (const std::unique_ptr<FrameSpool> &spool : warpedValid)
        spool->remove();
    if (request.edges == Edges::Fill) {
        int base = done;
        const PanoramaFill::Report filling = PanoramaFill::fill(result.cameraRgb, real, {}, request.cancelled, [&](int stepDone, int stepTotal) {
            done = base + stepDone;
            if (request.progress)
                request.progress(done, base + stepTotal);
        });
        if (filling.outcome == PanoramaFill::Outcome::Cancelled || stopped()) {
            result.cancelled = true;
            result.error = QStringLiteral("Annulé.");
            result.cameraRgb.release();
            return result;
        }
        if (filling.outcome == PanoramaFill::Outcome::TooLittleKnown) {
            result.cameraRgb.release();
            result.error = QStringLiteral("Trop peu d'image réelle pour remplir les bords : recadrez plutôt.");
            return result;
        }
        result.filledShare = filling.filledShare;
    }
    result.success = true;
    return result;
}

} // namespace PanoramaJob
