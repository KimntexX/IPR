#ifndef IPR_RAW_RAWDECODER_H
#define IPR_RAW_RAWDECODER_H

#include "ColorPipeline.h"
#include "ImageMetadata.h"

#include <QImage>
#include <QSize>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

namespace RawDecoder {


// One immutable, pre-DCP buffer for the active image only (not an unbounded cache).
// LCP and LibRaw WB have already been applied. Auto WB/normalization needs decode;
// manual temperature/tint edits reuse the camera-space working context.
struct DcpSource
{
    cv::Mat cameraRgb;
    std::shared_ptr<const CameraProfileEngine::DcpWorkingContext> workingContext;
    ImageMetadata metadata;
    std::optional<double> whiteBalanceKelvin;
    QString defaultProfile;
    bool automaticWhiteBalance = false;
    bool lensCorrectionEnabled = true;
    bool chromaticAberrationEnabled = true;
};

struct Options
{
    bool automaticWhiteBalance = true;
    QStringList profileDirectories;
    // Model/profile/sensor-mode policy; never selected from camera name alone.
    QString normalizationManifest;
    QString dcpProfileOverride;
    QString lcpProfileOverride;
    bool automaticLcpSelection = true;
    bool lensCorrectionEnabled = true;
    bool chromaticAberrationEnabled = true;
    bool useNormalizationEnvironment = true;
    bool listDcpProfiles = false;
    bool preferCameraNeutral = false;
    std::shared_ptr<DcpSource> retainedDcpSource;
    std::function<bool()> cancellationRequested;
};

ColorPipeline::WorkingImage applyProfile(const DcpSource &source, const QString &profile,
                                        ImageMetadata *metadata, QString *error);

using DecodeFunction = std::function<ColorPipeline::WorkingImage(
    const QString &fileName, const Options &options,
    ImageMetadata *metadata, QString *error)>;

using ThumbnailFunction = std::function<QImage(
    const QString &fileName, const QSize &maximumSize, QString *error)>;

// Extension point for future formats. Register at startup, before folder discovery.
// No wildcard: an extension is advertised only when a decoder actually exists.
struct FormatOptions
{
    ThumbnailFunction thumbnail;
    QString decoderId;
    QString decoderVersion;
    std::function<QSize(const QString &)> dimensions;
};

bool registerFormatHandler(const QStringList &extensions,
                           DecodeFunction decode, QString *error = nullptr,
                           const FormatOptions &format = {});
QStringList supportedExtensions();
QString backendVersion();
QSize sourceDimensions(const QString &fileName);
ColorPipeline::WorkingImage decode(const QString &fileName,
                                   const Options &options = {},
                                   ImageMetadata *metadata = nullptr,
                                   QString *error = nullptr);
QImage decodeEmbeddedThumbnail(const QString &fileName,
                               const QSize &maximumSize,
                               QString *error = nullptr);

}

#endif // IPR_RAW_RAWDECODER_H
