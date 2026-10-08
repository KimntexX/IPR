#ifndef RAWDECODER_H
#define RAWDECODER_H

#include "ColorPipeline.h"

#include <QImage>
#include <QSize>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <functional>

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
};

struct Options
{
    bool automaticWhiteBalance = true;
    QStringList profileDirectories;
    // Optional audited per-file manifest, never inferred from camera name alone.
    QString normalizationManifest;
    QString dcpProfileOverride;
    bool useNormalizationEnvironment = true;
    bool listDcpProfiles = false;
};

using DecodeFunction = std::function<ColorPipeline::WorkingImage(
    const QString &fileName, const Options &options,
    SourceMetadata *metadata, QString *error)>;

bool registerFormatHandler(const QStringList &extensions,
                           DecodeFunction decode, QString *error = nullptr);
QStringList supportedExtensions();
ColorPipeline::WorkingImage decode(const QString &fileName,
                                   const Options &options = {},
                                   SourceMetadata *metadata = nullptr,
                                   QString *error = nullptr);
QImage decodeEmbeddedThumbnail(const QString &fileName,
                               const QSize &maximumSize,
                               QString *error = nullptr);

}

#endif
