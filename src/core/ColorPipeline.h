#ifndef COLORPIPELINE_H
#define COLORPIPELINE_H

#include <QColorSpace>
#include <QImage>
#include <QString>
#include <opencv2/core.hpp>
#include <optional>
#include <memory>

namespace CameraProfileEngine { struct DcpWorkingContext; }

namespace ColorPipeline {

struct WorkingImage
{
    // CV_32FC4, straight-alpha RGBA in linear sRGB. RAW/profile transforms and
    // edits may generate values outside [0, 1]; output encoding clips to sRGB.
    cv::Mat linearRgba;
    QColorSpace colorSpace = QColorSpace(QColorSpace::SRgbLinear);
    QString sourceProfile;
    std::optional<double> referenceWhiteBalanceKelvin;
    std::shared_ptr<const CameraProfileEngine::DcpWorkingContext> dcpContext;

    bool isNull() const { return linearRgba.empty(); }
};

enum class OutputFormat
{
    Preview8Bit,
    Jpeg8Bit,
    HighBitDepth
};

WorkingImage fromSourceImage(const QImage &source, QString *error = nullptr);
WorkingImage fromLinearRgb16(const quint16 *data, int width, int height,
                             QString *error = nullptr);
WorkingImage mapToSrgbGamut(const WorkingImage &source,
                            QString *error = nullptr);
bool applyTemperatureTint(cv::Mat &linearRgb, int temperature,
                          double referenceTemperature, int tint,
                          QString *error = nullptr);
QImage toSrgb(const WorkingImage &source, OutputFormat outputFormat,
              QString *error = nullptr);

}

#endif
