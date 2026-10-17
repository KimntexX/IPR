#pragma once

#include <QString>
#include <array>
#include <optional>

namespace RawNormalization {

struct Context
{
    QString fileName;
    QString profilePath;
    QString cameraMake;
    QString cameraModel;
    QString decoderVersion;
    int iso = 0;
    std::array<unsigned, 4> black{};
    unsigned globalBlack = 0;
    unsigned nativeWhiteLevel = 0;
    bool cameraWhiteBalance = false;
    bool nativeBaselineUnknown = false;
    QString decoderId;
};

struct Calibration
{
    unsigned whiteLevel = 0;
    double baselineExposureEv = 0.0;
    QString provenance;
    // Profile rendering preference, NOT a replacement for measured DNG metadata.
    double profileExposureCompensationEv = 0.0;
    QString profileRenderingProvenance;
};

// Model/profile and sensor-mode rules, no source-file whitelist or RAW hashing.
// Empty manifest: no I/O. Invalid/unsupported: no partial correction.
std::optional<Calibration> resolve(const QString &manifestPath,
                                   const Context &context, QString *reason = nullptr);
QString cacheToken(const QString &manifestPath);
}
