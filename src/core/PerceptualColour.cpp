#include "PerceptualColour.h"
#include "ColorGrading.h"
#include "Vibrance.h"
#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>

namespace PerceptualColour {

void apply(cv::Mat &linearRgb, const Parameters &parameters)
{
    const bool vibrance = parameters.saturation != 0 || parameters.vibrance != 0;
    const bool mixer = ColorMixer::active(parameters.mixer);
    const auto nonZero = [](int v) { return v != 0; };
    const bool grading = std::any_of(parameters.gradingSaturation.cbegin(), parameters.gradingSaturation.cend(), nonZero) ||
                         std::any_of(parameters.gradingLuminance.cbegin(), parameters.gradingLuminance.cend(), nonZero);
    if ((!vibrance && !mixer && !grading) || linearRgb.empty() || linearRgb.type() != CV_32FC3) return;
    const double vibranceAmount = parameters.vibrance / 100.0, saturationAmount = parameters.saturation / 100.0;
    const ColorGrading::Zones zones = ColorGrading::prepare(parameters.gradingHue, parameters.gradingSaturation,
                                                            parameters.gradingLuminance, parameters.gradingBlending,
                                                            parameters.gradingBalance);
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            auto *row = linearRgb.ptr<cv::Vec3f>(y);
            for (int x = 0; x < linearRgb.cols; ++x) {
                const cv::Vec3f pixel = row[x];
                // Vibrance and the mixer leave exact greys alone; only the grading colours them.
                const bool grey = pixel[0] == pixel[1] && pixel[1] == pixel[2];
                if (grey && !grading) continue;
                cv::Vec3d lab = WorkingColor::workingToOklab(cv::Vec3d(pixel));
                const bool chromatic = !grey && std::hypot(lab[1], lab[2]) >= 1e-12;
                if (chromatic && (vibrance || mixer)) {
                    const double saturation = WorkingColor::proPhotoSaturation(cv::Vec3d(pixel));
                    if (vibrance) Vibrance::adjust(lab, saturation, vibranceAmount, saturationAmount);
                    if (mixer) {
                        // The mixer reads the saturation of the pixel vibrance produced.
                        const double after = vibrance
                            ? WorkingColor::proPhotoSaturation(WorkingColor::oklabToWorking(lab)) : saturation;
                        ColorMixer::adjust(lab, after, parameters.mixer);
                    }
                }
                if (grading) ColorGrading::adjust(lab, zones);
                if (chromatic || grading) row[x] = cv::Vec3f(WorkingColor::oklabToWorking(lab));
            }
        }
    });
}

} // namespace PerceptualColour
