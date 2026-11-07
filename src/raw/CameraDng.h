#ifndef IPR_RAW_CAMERADNG_H
#define IPR_RAW_CAMERADNG_H

#include "CameraProfileEngine.h"
#include "Image.h"
#include "Types.h"

#include <QDateTime>
#include <QString>

#include <array>

// A camera signal as a linear DNG: three channels per pixel, already demosaicked and
// corrected for the lens, in the camera's own colours. Other software reads an ordinary
// LinearRaw file with the camera's model, matrix and baseline exposure, and applies its own
// profile. IPR reads back the same signal, with its values below the black and its
// calibration, which LibRaw would clip and lose. An existing target is overwritten: staging
// and publishing belong to ImageExporter.
namespace CameraDng {

struct Content
{
    ipr::Image cameraRgb;   // F32, three channels, as a decoder's signal: white balance applied, white 1, signed
    QString cameraMake;
    QString cameraModel;
    // Kept for IPR alone, in the file's description: another reader must not take the lens
    // for one still to correct.
    QString lensMake;
    QString lensModel;
    int iso = 0;
    double exposureSeconds = 0.0;
    double focalLengthMm = 0.0;
    QDateTime captureDate;
    ipr::Vec3d cameraNeutral{1.0, 1.0, 1.0}; // inverse of the applied gains, G = 1
    double whiteBalanceKelvin = 5500.0;
    CameraProfileEngine::DcpRenderOptions renderOptions;
    unsigned calibratedWhite = 0;           // of the source's calibration, for the panel; 0 without one
    std::array<double, 9> colorMatrix{};    // XYZ to camera, at the illuminant below
    int illuminant = 21;                    // EXIF light source; 21 is D65
    QString provenance;                     // what made the file, in words
};

bool write(const Content &content, const QString &fileName, QString *error = nullptr);

// Whether IPR wrote this file as a camera signal: a plain DNG answers false.
bool isCameraDng(const QString &fileName);

bool read(const QString &fileName, Content &content, QString *error = nullptr);

} // namespace CameraDng

#endif // IPR_RAW_CAMERADNG_H
