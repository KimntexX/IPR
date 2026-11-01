#ifndef IPR_CORE_COLORPIPELINE_H
#define IPR_CORE_COLORPIPELINE_H

#include "ToneStatistics.h"
#include <QColorSpace>
#include <QImage>
#include <QString>
#include <opencv2/core.hpp>
#include <optional>
#include <memory>
#include "WorkingColorSpace.h"

namespace CameraProfileEngine { struct DcpWorkingContext; }

namespace ColorPipeline {

inline QColorSpace workingColorSpace()
{
    // Explicit xy definition also works with the project's Qt 6.4 minimum;
    // the named Bt2020 primaries were introduced only in Qt 6.8.
    static const QColorSpace space(QPointF(.3127, .3290), QPointF(.708, .292),
        QPointF(.170, .797), QPointF(.131, .046), QColorSpace::TransferFunction::Linear);
    return space;
}
inline QString engineIdentity()
{
    return QStringLiteral("ipr-render-rec2020-linear-d65-v1|profile-render=2");
}

struct WorkingImage
{
    // CV_32FC4, straight-alpha RGBA in linear Rec.2020 D65. RAW/profile transforms and
    // edits may generate values outside [0, 1]; output encoding clips to sRGB.
    cv::Mat linearRgba;
    QColorSpace colorSpace = workingColorSpace();
    QString sourceProfile;
    std::optional<double> referenceWhiteBalanceKelvin;
    std::shared_ptr<const CameraProfileEngine::DcpWorkingContext> dcpContext;
    // Luminance statistics at neutral settings, measured once on the edit
    // proxy at load and shared by the master: per-image tone-control weights.
    std::optional<ToneRecovery::Statistics> toneStatistics;

    bool isNull() const { return linearRgba.empty(); }
};

enum class OutputFormat
{
    Preview8Bit,
    Jpeg8Bit,
    HighBitDepth
};

WorkingImage fromSourceImage(const QImage &source, QString *error = nullptr);
// A RAW decoder's linear sRGB (CV_32FC3, white 1, finite, extended range kept) -> Rec.2020 master.
WorkingImage fromLinearSrgb(const cv::Mat &rgb, QString *error = nullptr);
// Output-only derivative: returned pixels AND tag are linear sRGB, not a master.
WorkingImage mapToSrgbGamut(const WorkingImage &source,
                            QString *error = nullptr);
bool applyTemperatureTint(cv::Mat &linearRgb, int temperature,
                          double referenceTemperature, int tint,
                          QString *error = nullptr);
QImage toSrgb(const WorkingImage &source, OutputFormat outputFormat,
              QString *error = nullptr);

}

#endif // IPR_CORE_COLORPIPELINE_H
