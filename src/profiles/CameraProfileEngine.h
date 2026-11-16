#ifndef IPR_PROFILES_CAMERAPROFILEENGINE_H
#define IPR_PROFILES_CAMERAPROFILEENGINE_H

#include "Image.h"
#include "LocalValues.h"
#include <QString>
#include <QStringList>
#include "PreparedProfileCache.h"
#include "Types.h"

#include <optional>

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

// The black a DCP whose DefaultBlackRender is Auto (0, or the tag absent: Adobe Standard
// and older Camera Matching profiles) subtracts in linear ProPhoto, at the scene level
// before the exposure gain, ahead of the look table and the tone curve, with a quadratic
// toe from zero to twice the black. The DNG
// specification leaves the amount to the converter; 0.005 is the DNG SDK's default
// shadows (5/1000) and fits Lightroom's Camera Neutral exports of five EOS 60D CR2
// (0.003 to 0.0075 each, 15 November 2026). None (1) subtracts nothing.
constexpr double AutoBlackRenderLevel = 0.005;
// Dark scenes take a smaller black: AutoBlackRenderLevel x min(1, sqrt(median Y / reference)),
// the scene's median luminance read once on the camera signal. Fitted on Lightroom's Camera
// Neutral exports of nine night scenes and four day ones of the EOS 60D (15 November 2026):
// nights 0.0005 to 0.0013, day scenes above the reference unchanged.
constexpr double AutoBlackSceneReference = 0.035;

struct DcpRenderOptions
{
    // Negative metadata, distinct from the profile's BaselineExposureOffset.
    // Zero means no additional gain; callers must not reapply an existing gain.
    double cameraBaselineExposureEv = 0.0;
    bool preserveToneCurveHue = false;
    // Audited camera/profile rendering adjustment, composed before DCP LUTs/curve.
    double profileExposureCompensationEv = 0.0;
    // Median Y (XYZ D50, scene level) of the whole camera signal, set by CameraSignal::render
    // for a profile whose DefaultBlackRender is Auto and kept in the working context, so the
    // black stays the same whatever the exposure, white balance or region rendered. Absent:
    // the fixed AutoBlackRenderLevel.
    std::optional<double> autoBlackSceneLuminance;
};

// The automatic black at the scene level for a scene luminance; AutoBlackRenderLevel when
// it is absent or not a finite non-negative number.
double autoBlackLevel(std::optional<double> sceneLuminance);
// The scene luminance the automatic black adapts to: median Y of the camera signal on a
// 1/8 grid, under the profile's matrix at the white balance. Absent for a profile whose
// DefaultBlackRender is None, an unreadable profile or an empty signal.
std::optional<double> autoBlackSceneLuminance(const QString &profilePath, const ipr::Image &cameraRgb,
                                              double whiteBalanceKelvin);

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
