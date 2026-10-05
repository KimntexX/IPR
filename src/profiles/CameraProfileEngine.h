#ifndef CAMERAPROFILEENGINE_H
#define CAMERAPROFILEENGINE_H

#include <QString>
#include <QStringList>
#include <opencv2/core.hpp>

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

QStringList defaultProfileDirectories();
Selection findProfiles(const QString &cameraMake, const QString &cameraModel,
                       const QString &lensMake, const QString &lensModel,
                       const QStringList &profileDirectories = {});
bool applyDcp(const QString &fileName, cv::Mat &cameraRgb,
              double whiteBalanceKelvin = 5500.0, QString *error = nullptr);
bool applyLcp(const QString &fileName, cv::Mat &linearRgb,
              const CaptureSettings &capture = {}, QString *error = nullptr);

}

#endif
