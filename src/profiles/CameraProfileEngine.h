#ifndef IPR_PROFILES_CAMERAPROFILEENGINE_H
#define IPR_PROFILES_CAMERAPROFILEENGINE_H

#include "Image.h"
#include "LocalValues.h"
#include <QString>
#include <QStringList>
#include "PreparedProfileCache.h"
#include "Types.h"

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
    double sensorWidthMm = 0.0;     // long side; 0 when unknown, taken as a full frame
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

struct LcpRenderOptions
{
    bool lensCorrection = true; // Distortion and vignetting.
    bool chromaticAberration = true; // Lateral red/blue registration.
};

// Immutable camera-space source. LibRaw WB and optical corrections are already
// applied; retain it so edits can precede the nonlinear DCP tables and curve.
// Its signals are shared, read only, by whoever holds the context: a copy is made
// with sharedCopy, never by assignment.
struct DcpWorkingContext
{
    ipr::Image cameraRgb;
    QString profilePath;
    double sourceKelvin = 5500.0;
    ipr::Vec3d cameraNeutral{0.0, 0.0, 0.0}; // Inverse actual LibRaw multipliers, G=1.
    DcpRenderOptions options;
    // Experimental camera-signal denoise: the signal before the lens corrections and
    // what reapplies them. Empty unless the decode was asked for it and corrected.
    ipr::Image preLensCameraRgb;
    QString lcpPath;
    CaptureSettings capture;
    LcpRenderOptions lensOptions;
};
// The same context on the same pixels, frozen for every holder.
DcpWorkingContext sharedCopy(const DcpWorkingContext &context);

bool whiteBalanceGains(const DcpWorkingContext &source, int temperature, int tint,
                       ipr::Vec3d &gains, QString *error = nullptr);
// Scene-referred source edits: white balance gains and exposure scale the
// linear camera signal, then the DCP (tables and tone curve) is applied.
// Exposure therefore precedes the profile tone curve, as in camera RAW editors.
// local: per-pixel exposure (EV), temperature (kelvins) and tint added to the
// global ones on the camera signal (LocalValues); a map that does not fit the
// signal is an error. Gains and profile illuminant are prepared once per
// combination of temperature and tint present, rounded to 25 K and 1 tint unit.
bool renderFromCameraSignal(const DcpWorkingContext &source, int temperature, int tint,
                            double exposureEv, ipr::Image &linearRgb, QString *error = nullptr,
                            const LocalValues::Maps &local = {});

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
// What a DNG of this camera needs to tell other software about its colours.
struct DcpCameraData
{
    std::array<double, 9> colorMatrix{}; // XYZ to camera; all zero when the profile has none
    int illuminant = 21;                 // EXIF light source of the matrix
};
DcpCameraData dcpCameraData(const QString &path);

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
bool applyDcp(const QString &fileName, ipr::Image &cameraRgb,
              double whiteBalanceKelvin = 5500.0, QString *error = nullptr,
              const DcpRenderOptions &options = {});
bool applyLcp(const QString &fileName, ipr::Image &linearRgb,
              const CaptureSettings &capture = {}, QString *error = nullptr,
              const LcpRenderOptions &options = {}, LcpApplied *applied = nullptr);

}

#endif // IPR_PROFILES_CAMERAPROFILEENGINE_H
