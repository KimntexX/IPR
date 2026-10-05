#include "RawDecoder.h"
#include "CameraProfileEngine.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <opencv2/core.hpp>

namespace {

std::array<double, 3> channelMean(const ColorPipeline::WorkingImage &image)
{
    std::array<double, 3> means{};
    for (int channel = 0; channel < 3; ++channel) {
        cv::Mat plane;
        cv::extractChannel(image.linearRgba, plane, channel);
        means[channel] = cv::mean(plane)[0];
    }
    return means;
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const bool useDefaultDirectories =
        argc >= 3 && QString::fromLocal8Bit(argv[2]) == QStringLiteral("--default");
    const int optionalArgumentStart = useDefaultDirectories ? 3 : 4;
    bool automaticWhiteBalance = true;
    QString outputPath;
    bool validArguments = useDefaultDirectories ? argc >= 3 && argc <= 5
                                                : argc >= 4 && argc <= 6;
    for (int argument = optionalArgumentStart;
         validArguments && argument < argc; ++argument) {
        const QString value = QString::fromLocal8Bit(argv[argument]);
        if (value == QStringLiteral("--camera-wb")) {
            automaticWhiteBalance = false;
        } else if (outputPath.isEmpty() && !value.startsWith(QLatin1Char('-'))) {
            outputPath = value;
        } else {
            validArguments = false;
        }
    }
    if (!validArguments) {
        out << "Usage: rawProfileIntegrationProbe <image.raw> --default "
               "[output.png] [--camera-wb]\n"
               "   or: rawProfileIntegrationProbe <image.raw> "
               "<Adobe CameraProfiles dir> <Adobe LensProfiles dir> "
               "[output.png] [--camera-wb]\n";
        return 2;
    }

    const QString rawPath = QString::fromLocal8Bit(argv[1]);
    const QStringList profileDirectories =
        useDefaultDirectories
            ? CameraProfileEngine::defaultProfileDirectories()
            : QStringList{QString::fromLocal8Bit(argv[2]),
                          QString::fromLocal8Bit(argv[3])};
    out << "profile_search_roots=" << profileDirectories.join(QLatin1Char(';'))
        << Qt::endl;
    RawDecoder::SourceMetadata metadata;
    QString error;
    RawDecoder::Options options;
    options.automaticWhiteBalance = automaticWhiteBalance;
    if (!useDefaultDirectories)
        options.profileDirectories = profileDirectories;
    const ColorPipeline::WorkingImage image =
        RawDecoder::decode(rawPath, options, &metadata, &error);
    if (image.isNull()) {
        out << "ERROR: decode failed: " << error << Qt::endl;
        return 1;
    }

    const auto means = channelMean(image);
    if (metadata.dcpProfile.isEmpty() || metadata.lcpProfile.isEmpty()) {
        out << "ERROR: expected matching DCP and LCP were not selected."
            << Qt::endl;
    }
    out << "file=" << QFileInfo(rawPath).fileName() << Qt::endl
        << "camera_make=" << metadata.cameraMake << Qt::endl
        << "camera_model=" << metadata.cameraModel << Qt::endl
        << "lens_make=" << metadata.lensMake << Qt::endl
        << "lens_model=" << metadata.lensModel << Qt::endl
        << "dcp=" << (metadata.dcpProfile.isEmpty()
                          ? QStringLiteral("<none>")
                          : metadata.dcpProfile)
        << Qt::endl
        << "white_balance="
        << (automaticWhiteBalance ? QStringLiteral("automatic")
                                  : QStringLiteral("camera-as-shot"))
        << Qt::endl
        << "source_white_balance_kelvin="
        << (image.referenceWhiteBalanceKelvin
                ? QString::number(*image.referenceWhiteBalanceKelvin, 'f', 1)
                : QStringLiteral("<none>"))
        << Qt::endl
        << "color_transform=" << image.sourceProfile << Qt::endl
        << "optical_black_level="
        << (metadata.opticalBlackLevelEstimated
                ? QStringLiteral("estimated from sensor margins")
                : QStringLiteral("LibRaw metadata"))
        << Qt::endl
        << "lcp=" << (metadata.lcpProfile.isEmpty()
                          ? QStringLiteral("<none>")
                          : metadata.lcpProfile)
        << Qt::endl
        << "dimensions=" << image.linearRgba.cols << 'x'
        << image.linearRgba.rows << Qt::endl
        << "linear_rgb_mean=" << means[0] << ',' << means[1] << ','
        << means[2] << Qt::endl;

    QTemporaryDir emptyProfiles;
    if (!emptyProfiles.isValid()) {
        out << "ERROR: cannot create empty profile directory." << Qt::endl;
        return 5;
    }
    RawDecoder::SourceMetadata baselineMetadata;
    const QStringList emptyProfileDirectories{
        emptyProfiles.filePath(QStringLiteral("dcp")),
        emptyProfiles.filePath(QStringLiteral("lcp"))};
    const ColorPipeline::WorkingImage baseline =
        RawDecoder::decode(rawPath,
                           RawDecoder::Options{automaticWhiteBalance,
                                               emptyProfileDirectories},
                           &baselineMetadata, &error);
    if (baseline.isNull()) {
        out << "ERROR: unprofiled baseline decode failed: " << error << Qt::endl;
        return 6;
    }
    const auto baselineMeans = channelMean(baseline);
    out << "unprofiled_color_transform=" << baseline.sourceProfile << Qt::endl;
    cv::Mat pixelDelta;
    cv::absdiff(image.linearRgba, baseline.linearRgba, pixelDelta);
    const cv::Scalar deltaMean = cv::mean(pixelDelta);
    out << "unprofiled_rgb_mean=" << baselineMeans[0] << ','
        << baselineMeans[1] << ',' << baselineMeans[2] << Qt::endl
        << "profiled_unprofiled_mean_abs_rgb_delta="
        << (deltaMean[0] + deltaMean[1] + deltaMean[2]) / 3.0 << Qt::endl
        << "editor_adjustments=not applied (decoder-only comparison)" << Qt::endl;
    if (!std::isfinite(deltaMean[0]) || !std::isfinite(deltaMean[1]) ||
        !std::isfinite(deltaMean[2]) ||
        deltaMean[0] + deltaMean[1] + deltaMean[2] < 0.003) {
        out << "ERROR: profile application produced no measurable pixel change."
            << Qt::endl;
        return 7;
    }
    if (!outputPath.isEmpty()) {
        const QImage rendered = ColorPipeline::toSrgb(
            image, ColorPipeline::OutputFormat::Preview8Bit, &error);
        if (rendered.isNull() ||
            !rendered.save(outputPath)) {
            out << "ERROR: cannot write preview: " << error << Qt::endl;
            return 4;
        }
        out << "preview=" << outputPath << Qt::endl;
    }

    if (metadata.dcpProfile.isEmpty()) {
        for (const QString &root : profileDirectories) {
            QDirIterator profiles(root, {QStringLiteral("*R6 Mark III*.dcp")},
                                  QDir::Files, QDirIterator::Subdirectories);
            while (profiles.hasNext()) {
                const QString path = profiles.next();
                cv::Mat sample(1, 1, CV_32FC3, cv::Scalar(0.2f, 0.3f, 0.4f));
                QString profileError;
                const bool loaded = CameraProfileEngine::applyDcp(
                    path, sample, 5500.0, &profileError);
                out << "dcp_probe=" << QFileInfo(path).fileName()
                    << " loaded=" << loaded;
                if (!loaded)
                    out << " error=" << profileError;
                out << Qt::endl;
            }
        }
    }
    if (metadata.lcpProfile.isEmpty()) {
        for (const QString &root : profileDirectories) {
            QDirIterator profiles(
                root, {QStringLiteral("*70-200mm F2.8 DG OS HSM S018*.lcp")},
                QDir::Files, QDirIterator::Subdirectories);
            while (profiles.hasNext()) {
                const QString path = profiles.next();
                cv::Mat sample(32, 32, CV_32FC3, cv::Scalar(0.2f, 0.3f, 0.4f));
                CameraProfileEngine::CaptureSettings capture;
                capture.focalLengthMm = 135.0;
                capture.aperture = 2.8;
                QString profileError;
                const bool loaded = CameraProfileEngine::applyLcp(
                    path, sample, capture, &profileError);
                out << "lcp_probe=" << QFileInfo(path).fileName()
                    << " loaded=" << loaded;
                if (!loaded)
                    out << " error=" << profileError;
                out << Qt::endl;
            }
        }
    }

    return metadata.dcpProfile.isEmpty() || metadata.lcpProfile.isEmpty() ? 3 : 0;
}
