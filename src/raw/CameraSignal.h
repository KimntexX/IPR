#pragma once

#include "ColorPipeline.h"
#include "CameraProfileEngine.h"

namespace CameraSignal {

enum class Coordinates { CameraRgb, LinearSrgb };

// Post-demosaic, black-subtracted/scaled camera RGB; NOT a sensor mosaic.
// Decoder WB and requested optical corrections have already been applied.
// Ownership of rgb is transferred to render(); any remaining aliases must be
// treated as immutable for the lifetime of the returned DCP working context.
struct Source
{
    cv::Mat rgb;
    QString cameraMake;
    QString cameraModel;
    std::optional<double> whiteBalanceKelvin;
    cv::Vec3d cameraNeutral{0, 0, 0}; // Inverse applied WB gains, G=1; all-zero = unavailable.
    bool whiteBalanceApplied = false;
    Coordinates coordinates = Coordinates::CameraRgb;
};

bool validate(const Source &source, QString *error = nullptr);
ColorPipeline::WorkingImage render(Source &&source, const QString &profile,
                                   const CameraProfileEngine::DcpRenderOptions &options = {},
                                   QString *error = nullptr);
}
