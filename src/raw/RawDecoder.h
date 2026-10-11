#ifndef RAWDECODER_H
#define RAWDECODER_H

#include "ColorPipeline.h"

#include <QImage>
#include <QSize>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

namespace RawDecoder {

struct SourceMetadata
{
    QString format;
    QString cameraMake;
    QString cameraModel;
    QString lensMake;
    QString lensModel;
    QString embeddedColorProfile;
    QString dcpProfile;
    QString lcpProfile;
    QString lcpPath;
    QString lcpSha256;
    bool lensGeometryApplied = false;
    bool lensVignettingApplied = false;
    bool chromaticAberrationApplied = false;
    QDateTime captureDate;
    QString location;
    int iso = 0;
    double focalLengthMm = 0.0;
    double exposureSeconds = 0.0;
    int rating = 0;
    QString colorLabel;
    bool opticalBlackLevelEstimated = false;
    bool rawNormalizationApplied = false;
    unsigned normalizationWhiteLevel = 0;
    double normalizationBaselineExposureEv = 0.0;
    QString normalizationProvenance;
    QString normalizationReason;
    QStringList availableDcpProfiles;
    QString developmentWarning;
};

// One immutable, pre-DCP buffer for the active image only (not an unbounded cache).
// LCP and LibRaw WB have already been applied. Auto WB/normalization needs decode;
// manual temperature/tint edits reuse the camera-space working context.
struct DcpSource
{
    cv::Mat cameraRgb;
    std::shared_ptr<const CameraProfileEngine::DcpWorkingContext> workingContext;
    SourceMetadata metadata;
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
    // Optional audited per-file manifest, never inferred from camera name alone.
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
};

ColorPipeline::WorkingImage applyProfile(const DcpSource &source, const QString &profile,
                                        SourceMetadata *metadata, QString *error);

using DecodeFunction = std::function<ColorPipeline::WorkingImage(
    const QString &fileName, const Options &options,
    SourceMetadata *metadata, QString *error)>;

bool registerFormatHandler(const QStringList &extensions,
                           DecodeFunction decode, QString *error = nullptr);
QStringList supportedExtensions();
QString backendVersion();
ColorPipeline::WorkingImage decode(const QString &fileName,
                                   const Options &options = {},
                                   SourceMetadata *metadata = nullptr,
                                   QString *error = nullptr);
QImage decodeEmbeddedThumbnail(const QString &fileName,
                               const QSize &maximumSize,
                               QString *error = nullptr);

}

#endif
