#include "imageeditor.h"
#include "imageprovider.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QImageReader>
#include <QTimer>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

namespace {

struct ReferenceSettings
{
    static constexpr bool AutomaticWhiteBalance = false;
    static constexpr float ExposureEv = -0.49f;
    static constexpr int Contrast = 41;
    static constexpr int Highlights = -100;
    static constexpr int Shadows = -100;
    static constexpr int Whites = -100;
    static constexpr int Blacks = -100;
};

struct ComparisonMetrics
{
    std::array<double, 3> renderedMean{};
    std::array<double, 3> referenceMean{};
    double meanAbsoluteRgbDifference = 0.0;
    double meanAbsoluteLuminanceDifference = 0.0;
    std::array<int, 3> renderedLuminanceQuantiles{};
    std::array<int, 3> referenceLuminanceQuantiles{};
    QString referenceAlignment;
};

QString awaitExport(ImageEditor &editor, const QUrl &target)
{
    QEventLoop loop;
    QString finalStatus;
    const auto connection = QObject::connect(
        &editor, &ImageEditor::statusMessageChanged, &loop, [&] {
            const QString status = editor.statusMessage();
            if (status.startsWith(QStringLiteral("Image exportée")) ||
                status.startsWith(QStringLiteral("Échec de l'export")) ||
                status.startsWith(QStringLiteral("Échec du rendu avant export"))) {
                finalStatus = status;
                loop.quit();
            }
        });
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    editor.exportImage(target, QStringLiteral("JPEG"), 100);
    loop.exec();
    QObject::disconnect(connection);
    return finalStatus;
}

bool awaitLoad(ImageEditor &editor, const QUrl &fileUrl)
{
    QEventLoop loop;
    const auto connection = QObject::connect(
        &editor, &ImageEditor::hasImageChanged, &loop, &QEventLoop::quit);
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    editor.loadImage(fileUrl);
    if (editor.busy())
        loop.exec();
    QObject::disconnect(connection);
    return editor.hasImage();
}

bool awaitImageInfo(ImageEditor &editor)
{
    if (!editor.busy())
        return true;
    QEventLoop loop;
    const auto connection = QObject::connect(
        &editor, &ImageEditor::imageInfoChanged, &loop, &QEventLoop::quit);
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(connection);
    return true;
}

bool render(const QString &rawPath, bool automaticWhiteBalance,
            const QString &outputPath, QImage *output, QString *error)
{
    ImageProvider provider;
    ImageEditor editor(&provider);
    if (!awaitLoad(editor, QUrl::fromLocalFile(rawPath))) {
        if (error)
            *error = editor.statusMessage();
        return false;
    }

    editor.setAutomaticWhiteBalance(automaticWhiteBalance);
    awaitImageInfo(editor);
    if (qEnvironmentVariableIntValue("IPR_PROBE_DEFAULT_SETTINGS") == 0) {
        editor.setBrightness(ReferenceSettings::ExposureEv);
        editor.setContrast(ReferenceSettings::Contrast);
        editor.setHighlights(ReferenceSettings::Highlights);
        editor.setShadows(ReferenceSettings::Shadows);
        editor.setWhites(ReferenceSettings::Whites);
        editor.setBlacks(ReferenceSettings::Blacks);
    }

    const QString status = awaitExport(editor, QUrl::fromLocalFile(outputPath));
    if (!status.startsWith(QStringLiteral("Image exportée"))) {
        if (error)
            *error = status.isEmpty()
                ? QStringLiteral("Le rendu IPR a expiré.")
                : status;
        return false;
    }

    QImageReader reader(outputPath);
    reader.setAutoTransform(true);
    *output = reader.read();
    if (output->isNull()) {
        if (error)
            *error = reader.errorString();
        return false;
    }
    return true;
}

int luminance(const QRgb pixel)
{
    return qBound(0, qRound(0.2126 * qRed(pixel) + 0.7152 * qGreen(pixel) +
                            0.0722 * qBlue(pixel)),
                  255);
}

struct AlignedReference
{
    QImage image;
    int xOffset = 0;
    int yOffset = 0;
    QString description;
};

double alignmentError(const QImage &rendered, const QImage &reference,
                      int xOffset, int yOffset)
{
    // Sparse sampling is enough to choose which one-pixel edge belongs to
    // the reference frame, without turning alignment into another expensive
    // full-resolution render pass.
    const int xStep = std::max(1, rendered.width() / 256);
    const int yStep = std::max(1, rendered.height() / 256);
    double error = 0.0;
    quint64 count = 0;
    for (int y = 0; y < rendered.height(); y += yStep) {
        const QRgb *renderedRow =
            reinterpret_cast<const QRgb *>(rendered.constScanLine(y));
        const QRgb *referenceRow =
            reinterpret_cast<const QRgb *>(reference.constScanLine(y + yOffset));
        for (int x = 0; x < rendered.width(); x += xStep) {
            const QRgb renderedPixel = renderedRow[x];
            const QRgb referencePixel = referenceRow[x + xOffset];
            error += std::abs(qRed(renderedPixel) - qRed(referencePixel));
            error += std::abs(qGreen(renderedPixel) - qGreen(referencePixel));
            error += std::abs(qBlue(renderedPixel) - qBlue(referencePixel));
            ++count;
        }
    }
    return count == 0 ? std::numeric_limits<double>::infinity()
                      : error / static_cast<double>(count);
}

AlignedReference alignReference(const QImage &rendered,
                                const QImage &reference)
{
    if (reference.size() == rendered.size())
        return {reference, 0, 0, QStringLiteral("identical dimensions")};

    // LibRaw exposes the active area of the tested Canon CR3 files as one
    // pixel narrower and shorter than Lightroom's rendered frame. Cropping
    // the one-pixel reference border preserves every IPR sample. Scaling
    // would interpolate all pixels and make MAE/superposition less accurate.
    if (reference.width() == rendered.width() + 1 &&
        reference.height() == rendered.height() + 1) {
        double bestError = std::numeric_limits<double>::infinity();
        int bestX = 0;
        int bestY = 0;
        for (int yOffset = 0; yOffset <= 1; ++yOffset) {
            for (int xOffset = 0; xOffset <= 1; ++xOffset) {
                const double error = alignmentError(rendered, reference,
                                                    xOffset, yOffset);
                if (error < bestError) {
                    bestError = error;
                    bestX = xOffset;
                    bestY = yOffset;
                }
            }
        }
        return {reference, bestX, bestY,
                QStringLiteral("cropped one-pixel reference border (%1,%2)")
                    .arg(bestX)
                    .arg(bestY)};
    }

    return {reference.scaled(rendered.size(), Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation),
            0, 0, QStringLiteral("scaled because dimensions differ")};
}

std::array<int, 3> quantiles(const std::array<quint64, 256> &histogram,
                             quint64 count)
{
    constexpr std::array<double, 3> probabilities{0.05, 0.50, 0.95};
    std::array<int, 3> result{};
    quint64 cumulative = 0;
    int quantileIndex = 0;
    for (int value = 0; value < static_cast<int>(histogram.size()) &&
                        quantileIndex < static_cast<int>(probabilities.size());
         ++value) {
        cumulative += histogram[static_cast<size_t>(value)];
        while (quantileIndex < static_cast<int>(probabilities.size()) &&
               cumulative >= static_cast<quint64>(
                   std::ceil(probabilities[quantileIndex] * count))) {
            result[static_cast<size_t>(quantileIndex)] = value;
            ++quantileIndex;
        }
    }
    return result;
}

ComparisonMetrics compare(const QImage &rendered, const QImage &reference)
{
    const QImage renderedRgba = rendered.convertToFormat(QImage::Format_RGBA8888);
    const QImage sourceReferenceRgba =
        reference.convertToFormat(QImage::Format_RGBA8888);
    const AlignedReference alignedReference =
        alignReference(renderedRgba, sourceReferenceRgba);

    ComparisonMetrics metrics;
    metrics.referenceAlignment = alignedReference.description;
    std::array<quint64, 256> renderedHistogram{};
    std::array<quint64, 256> referenceHistogram{};
    quint64 sampleCount = 0;
    for (int y = 0; y < renderedRgba.height(); y += 4) {
        const QRgb *renderedRow =
            reinterpret_cast<const QRgb *>(renderedRgba.constScanLine(y));
        const QRgb *referenceRow =
            reinterpret_cast<const QRgb *>(
                alignedReference.image.constScanLine(y + alignedReference.yOffset));
        for (int x = 0; x < renderedRgba.width(); x += 4) {
            const QRgb renderedPixel = renderedRow[x];
            const QRgb referencePixel = referenceRow[x + alignedReference.xOffset];
            for (int channel = 0; channel < 3; ++channel) {
                const int renderedValue =
                    channel == 0 ? qRed(renderedPixel)
                                 : channel == 1 ? qGreen(renderedPixel)
                                                : qBlue(renderedPixel);
                const int referenceValue =
                    channel == 0 ? qRed(referencePixel)
                                 : channel == 1 ? qGreen(referencePixel)
                                                : qBlue(referencePixel);
                metrics.renderedMean[static_cast<size_t>(channel)] +=
                    renderedValue;
                metrics.referenceMean[static_cast<size_t>(channel)] +=
                    referenceValue;
                metrics.meanAbsoluteRgbDifference +=
                    std::abs(renderedValue - referenceValue);
            }
            const int renderedLuminance = luminance(renderedPixel);
            const int referenceLuminance = luminance(referencePixel);
            ++renderedHistogram[static_cast<size_t>(renderedLuminance)];
            ++referenceHistogram[static_cast<size_t>(referenceLuminance)];
            metrics.meanAbsoluteLuminanceDifference +=
                std::abs(renderedLuminance - referenceLuminance);
            ++sampleCount;
        }
    }

    for (int channel = 0; channel < 3; ++channel) {
        metrics.renderedMean[static_cast<size_t>(channel)] /= sampleCount;
        metrics.referenceMean[static_cast<size_t>(channel)] /= sampleCount;
    }
    metrics.meanAbsoluteRgbDifference /= 3.0 * sampleCount;
    metrics.meanAbsoluteLuminanceDifference /= sampleCount;
    metrics.renderedLuminanceQuantiles =
        quantiles(renderedHistogram, sampleCount);
    metrics.referenceLuminanceQuantiles =
        quantiles(referenceHistogram, sampleCount);
    return metrics;
}

void printMetrics(const char *label, const ComparisonMetrics &metrics)
{
    std::cout << label << '\n'
              << "  mean_srgb_ipr=" << metrics.renderedMean[0] << ','
              << metrics.renderedMean[1] << ',' << metrics.renderedMean[2]
              << '\n'
              << "  mean_srgb_reference=" << metrics.referenceMean[0] << ','
              << metrics.referenceMean[1] << ',' << metrics.referenceMean[2]
              << '\n'
              << "  mean_absolute_rgb_difference="
              << metrics.meanAbsoluteRgbDifference << '\n'
              << "  mean_absolute_luminance_difference="
              << metrics.meanAbsoluteLuminanceDifference << '\n'
              << "  reference_alignment="
              << metrics.referenceAlignment.toStdString() << '\n'
              << "  luminance_p05_p50_p95_ipr="
              << metrics.renderedLuminanceQuantiles[0] << ','
              << metrics.renderedLuminanceQuantiles[1] << ','
              << metrics.renderedLuminanceQuantiles[2] << '\n'
              << "  luminance_p05_p50_p95_reference="
              << metrics.referenceLuminanceQuantiles[0] << ','
              << metrics.referenceLuminanceQuantiles[1] << ','
              << metrics.referenceLuminanceQuantiles[2] << '\n';
}

}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 5) {
        std::cerr << "Usage: tonalReferenceComparisonProbe <source.cr3> "
                     "<reference.jpg> <automatic-wb.jpg> <camera-wb.jpg>\n";
        return 2;
    }

    const QString rawPath = QString::fromLocal8Bit(argv[1]);
    const QString referencePath = QString::fromLocal8Bit(argv[2]);
    QImageReader referenceReader(referencePath);
    referenceReader.setAutoTransform(true);
    const QImage reference = referenceReader.read();
    if (reference.isNull()) {
        std::cerr << "Cannot read reference image: "
                  << referenceReader.errorString().toStdString() << '\n';
        return 1;
    }

    QImage automaticWhiteBalance;
    QString error;
    const QString automaticPath = QString::fromLocal8Bit(argv[3]);
    if (!render(rawPath, true, automaticPath, &automaticWhiteBalance, &error)) {
        std::cerr << "Automatic-WB render failed: " << error.toStdString() << '\n';
        return 1;
    }

    QImage cameraWhiteBalance;
    const QString cameraPath = QString::fromLocal8Bit(argv[4]);
    if (!render(rawPath, false, cameraPath, &cameraWhiteBalance, &error)) {
        std::cerr << "Camera-WB render failed: " << error.toStdString() << '\n';
        return 1;
    }

    const bool defaultSettings =
        qEnvironmentVariableIntValue("IPR_PROBE_DEFAULT_SETTINGS") != 0;
    std::cout << "settings="
              << (defaultSettings ? "opening-defaults"
                                  : "white-balance:as-shot,exposure:-0.49,"
                                    "contrast:41,highlights:-100,shadows:-100,"
                                    "whites:-100,blacks:-100")
              << '\n'
              << "reference=" << QFileInfo(referencePath).fileName().toStdString()
              << '\n'
              << "automatic_output=" << automaticPath.toStdString() << '\n'
              << "camera_output=" << cameraPath.toStdString() << '\n';
    const ComparisonMetrics automaticMetrics =
        compare(automaticWhiteBalance, reference);
    const ComparisonMetrics cameraMetrics =
        compare(cameraWhiteBalance, reference);
    printMetrics("automatic_white_balance", automaticMetrics);
    printMetrics("camera_as_shot_white_balance", cameraMetrics);
    if (cameraMetrics.meanAbsoluteRgbDifference > 25.0) {
        std::cerr << "Camera-WB render diverges too far from the reference JPEG.\n";
        return 3;
    }
    return 0;
}
