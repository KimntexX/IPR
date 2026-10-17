#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

// Source metadata shared by camera RAW and already-developed raster imports.
struct ImageMetadata
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
    double profileExposureCompensationEv = 0.0;
    QString profileRenderingProvenance;
    QString normalizationProvenance;
    QString normalizationReason;
    QStringList availableDcpProfiles;
    QString developmentWarning;
    QString decoderId;
    QString decoderVersion;
    bool cameraRaw = false;
    bool hasEmbeddedColorProfile = false;
};
