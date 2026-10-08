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
    QString librawVersion;
    int iso = 0;
    std::array<unsigned, 4> black{};
    bool cameraWhiteBalance = false;
    bool nativeBaselineUnknown = false;
};

struct Calibration
{
    unsigned whiteLevel = 0;
    double baselineExposureEv = 0.0;
    QString provenance;
};

// Explicit opt-in, file/profile hashes and measured black/ISO/version bound.
// Empty manifest: no I/O. Invalid/unsupported: no partial correction.
std::optional<Calibration> resolve(const QString &manifestPath,
                                   const Context &context, QString *reason = nullptr);
QString cacheToken(const QString &manifestPath);
}
