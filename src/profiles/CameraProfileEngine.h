#ifndef CAMERAPROFILEENGINE_H
#define CAMERAPROFILEENGINE_H

#include <QString>
#include <QStringList>
#include <opencv2/core.hpp>
#include "PreparedProfileCache.h"

namespace CameraProfileEngine {

struct Selection
{
    QString dcpPath;
    QString lcpPath;
};

struct CaptureSettings
{
    double whiteBalanceKelvin = 5500.0;
    double focalLengthMm = 0.0;
    double focusDistanceM = 0.0;
    double aperture = 0.0;
};

struct DcpRenderOptions
{
    // Negative metadata, distinct from the profile's BaselineExposureOffset.
    // Zero means no additional gain; callers must not reapply an existing gain.
    double cameraBaselineExposureEv = 0.0;
    bool preserveToneCurveHue = false;
    // Audited camera/profile rendering adjustment, composed before DCP LUTs/curve.
    double profileExposureCompensationEv = 0.0;
};

// Immutable camera-space source. LibRaw WB and optical corrections are already
// applied; retain it so edits can precede the nonlinear DCP tables and curve.
struct DcpWorkingContext
{
    cv::Mat cameraRgb;
    QString profilePath;
    double sourceKelvin = 5500.0;
    cv::Vec3d cameraNeutral{0.0, 0.0, 0.0}; // Inverse actual LibRaw multipliers, G=1.
    DcpRenderOptions options;
};

bool whiteBalanceGains(const DcpWorkingContext &source, int temperature, int tint,
                       cv::Vec3d &gains, QString *error = nullptr);
// Scene-referred source edits: white balance gains and exposure scale the
// linear camera signal, then the DCP (tables and tone curve) is applied.
// Exposure therefore precedes the profile tone curve, as in camera RAW editors.
bool renderFromCameraSignal(const DcpWorkingContext &source, int temperature, int tint,
                            double exposureEv, cv::Mat &linearRgb, QString *error = nullptr);

struct LcpRenderOptions
{
    bool lensCorrection = true; // Distortion and vignetting.
    bool chromaticAberration = true; // Lateral red/blue registration.
};

struct LcpApplied
{
    bool geometry = false;
    bool vignetting = false;
    bool chromaticAberration = false;
};

QStringList defaultProfileDirectories();
PreparedProfileCache::Statistics preparedProfileCacheStats();
void configurePreparedProfileCache(quint64 budgetBytes, int entryLimit = 16);
void clearPreparedProfileCache();
QStringList availableDcpProfiles(const QString &cameraMake, const QString &cameraModel,
                                 const QStringList &profileDirectories = {});
bool dcpMatchesCamera(const QString &path, const QString &cameraMake,
                       const QString &cameraModel);
bool lcpMatchesLens(const QString &path, const QString &cameraMake,
                    const QString &cameraModel, const QString &lensMake,
                    const QString &lensModel);
Selection findProfiles(const QString &cameraMake, const QString &cameraModel,
                       const QString &lensMake, const QString &lensModel,
                       const QStringList &profileDirectories = {});
// Camera RGB input -> linear Rec.2020 D65 output (unbounded float).
// renderFromCameraSignal returns this same master domain.
bool applyDcp(const QString &fileName, cv::Mat &cameraRgb,
              double whiteBalanceKelvin = 5500.0, QString *error = nullptr,
              const DcpRenderOptions &options = {});
bool applyLcp(const QString &fileName, cv::Mat &linearRgb,
              const CaptureSettings &capture = {}, QString *error = nullptr,
              const LcpRenderOptions &options = {}, LcpApplied *applied = nullptr);

}

#endif
