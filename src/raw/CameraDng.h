#ifndef IPR_RAW_CAMERADNG_H
#define IPR_RAW_CAMERADNG_H

#include "CameraProfileEngine.h"

#include <QString>
#include <opencv2/core.hpp>

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
    cv::Mat cameraRgb;      // CV_32FC3, as a decoder's signal: white balance applied, white 1, signed
    QString cameraMake;
    QString cameraModel;
    cv::Vec3d cameraNeutral{1.0, 1.0, 1.0}; // inverse of the applied gains, G = 1
    double whiteBalanceKelvin = 5500.0;
    CameraProfileEngine::DcpRenderOptions renderOptions;
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
