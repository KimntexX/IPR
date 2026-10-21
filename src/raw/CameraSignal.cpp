#include "CameraSignal.h"

#include <QFileInfo>
#include <cmath>
#include <vector>

namespace CameraSignal {
bool validate(const Source &source, QString *error)
{
    if (error) error->clear();
    const auto reject = [&](const char *message) {
        if (error) *error = QString::fromLatin1(message);
        return false;
    };
    if (source.coordinates != Coordinates::CameraRgb || !source.whiteBalanceApplied ||
        source.rgb.empty() || source.rgb.dims != 2 || source.rgb.type() != CV_32FC3 ||
        !cv::checkRange(source.rgb, true))
        return reject("Expected finite linear camera RGB float32, with WB already applied.");
    if (source.cameraMake.trimmed().isEmpty() || source.cameraModel.trimmed().isEmpty())
        return reject("Camera identity is required for DCP rendering.");
    if (source.whiteBalanceKelvin && (!std::isfinite(*source.whiteBalanceKelvin) ||
        *source.whiteBalanceKelvin < 2000 || *source.whiteBalanceKelvin > 50000))
        return reject("Invalid source white balance temperature.");
    const bool unavailable = source.cameraNeutral == cv::Vec3d(0, 0, 0);
    if (!unavailable) {
        for (double channel : source.cameraNeutral.val)
            if (!std::isfinite(channel) || channel <= 0)
                return reject("Invalid applied camera neutral.");
        if (std::abs(source.cameraNeutral[1] - 1.0) > 1e-9)
            return reject("Applied camera neutral must be normalized to green.");
    }
    return true;
}

ColorPipeline::WorkingImage render(Source &&source, const QString &profile,
                                   const CameraProfileEngine::DcpRenderOptions &options,
                                   QString *error)
{
    if (!validate(source, error)) return {};
    if (!CameraProfileEngine::dcpMatchesCamera(profile, source.cameraMake, source.cameraModel)) {
        if (error) *error = QStringLiteral("Profil DCP absent ou incompatible avec le boîtier.");
        return {};
    }
    auto context = std::make_shared<CameraProfileEngine::DcpWorkingContext>();
    context->cameraRgb = std::move(source.rgb);
    context->profilePath = profile;
    context->sourceKelvin = source.whiteBalanceKelvin.value_or(5500.0);
    context->cameraNeutral = source.cameraNeutral;
    context->headroom = source.headroom;
    context->options = options;
    // One working copy: preserve the immutable camera buffer before nonlinear DCP.
    auto rendered = context->cameraRgb.clone();
    if (!CameraProfileEngine::applyDcp(profile, rendered, context->sourceKelvin, error, options))
        return {};
    ColorPipeline::WorkingImage result;
    std::vector<cv::Mat> channels;
    cv::split(rendered, channels);
    channels.push_back(cv::Mat(rendered.size(), CV_32FC1, cv::Scalar(1.f)));
    cv::merge(channels, result.linearRgba);
    result.colorSpace = ColorPipeline::workingColorSpace();
    result.referenceWhiteBalanceKelvin = source.whiteBalanceKelvin;
    result.dcpContext = context;
    result.sourceProfile = QStringLiteral("DCP %1 → Rec.2020 linéaire").arg(QFileInfo(profile).completeBaseName());
    return result;
}
}
