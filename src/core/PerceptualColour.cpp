#include "PerceptualColour.h"
#include "ColorGrading.h"
#include "Vibrance.h"
#include "WorkingColorSpace.h"

#include "Pixels.h"
#include "Parallel.h"

#include <algorithm>
#include <cmath>

namespace PerceptualColour {

void apply(ipr::Image &linearRgb, const Parameters &parameters, const LocalValues::Maps &local)
{
    const auto readable = [&](LocalValues::Parameter parameter) {
        const ipr::Image &map = local[parameter];
        return !map.empty() && LocalValues::fits(map, linearRgb.size()) ? &map : nullptr;
    };
    const ipr::Image *localSaturation = readable(LocalValues::Saturation);
    const ipr::Image *localVibrance = readable(LocalValues::Vibrance);
    const bool vibrance = parameters.saturation != 0 || parameters.vibrance != 0;
    const bool localVibrancePossible = localSaturation || localVibrance;
    const bool mixer = ColorMixer::active(parameters.mixer);
    const bool point = PointColor::active(parameters.pointColor);
    const auto nonZero = [](int v) { return v != 0; };
    const bool grading = std::any_of(parameters.gradingSaturation.cbegin(), parameters.gradingSaturation.cend(), nonZero) ||
                         std::any_of(parameters.gradingLuminance.cbegin(), parameters.gradingLuminance.cend(), nonZero);
    if ((!vibrance && !localVibrancePossible && !mixer && !point && !grading) || linearRgb.empty() ||
        !(linearRgb.depth() == ipr::Depth::F32 && linearRgb.channels() == 3)) return;
    const double vibranceAmount = parameters.vibrance / 100.0, saturationAmount = parameters.saturation / 100.0;
    const ColorGrading::Zones zones = ColorGrading::prepare(parameters.gradingHue, parameters.gradingSaturation,
                                                            parameters.gradingLuminance, parameters.gradingBlending,
                                                            parameters.gradingBalance);
    ipr::parallelRows(linearRgb.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            auto *row = linearRgb.mutableRow<ipr::Vec3f>(y);
            const float *saturationRow = localSaturation ? localSaturation->row<float>(y) : nullptr;
            const float *vibranceRow = localVibrance ? localVibrance->row<float>(y) : nullptr;
            for (int x = 0; x < linearRgb.width(); ++x) {
                // Global plus local amounts; where no local value applies, the global path.
                const float extraSaturation = saturationRow ? saturationRow[x] : 0.0f;
                const float extraVibrance = vibranceRow ? vibranceRow[x] : 0.0f;
                const bool pixelVibrance = vibrance || extraSaturation != 0.0f || extraVibrance != 0.0f;
                if (!pixelVibrance && !mixer && !point && !grading) continue;
                const double pixelVibranceAmount = extraVibrance != 0.0f
                    ? (double(parameters.vibrance) + double(extraVibrance)) / 100.0 : vibranceAmount;
                const double pixelSaturationAmount = extraSaturation != 0.0f
                    ? (double(parameters.saturation) + double(extraSaturation)) / 100.0 : saturationAmount;
                const ipr::Vec3f pixel = row[x];
                // Vibrance, the mixer and the point colour leave exact greys alone; only the
                // grading colours them.
                const bool grey = pixel[0] == pixel[1] && pixel[1] == pixel[2];
                if (grey && !grading) continue;
                ipr::Vec3d lab = WorkingColor::workingToOklab(ipr::Vec3d(pixel));
                const bool chromatic = !grey && std::hypot(lab[1], lab[2]) >= 1e-12;
                if (chromatic && (pixelVibrance || mixer)) {
                    const double saturation = WorkingColor::proPhotoSaturation(ipr::Vec3d(pixel));
                    if (pixelVibrance) Vibrance::adjust(lab, saturation, pixelVibranceAmount, pixelSaturationAmount);
                    if (mixer) {
                        // The mixer reads the saturation of the pixel vibrance produced.
                        const double after = pixelVibrance
                            ? WorkingColor::proPhotoSaturation(WorkingColor::oklabToWorking(lab)) : saturation;
                        ColorMixer::adjust(lab, after, parameters.mixer);
                    }
                }
                if (chromatic && point) {
                    // The point colour works in the HSV of linear ProPhoto, from the working colour.
                    ipr::Vec3d working = WorkingColor::oklabToWorking(lab);
                    PointColor::adjust(working, parameters.pointColor);
                    lab = WorkingColor::workingToOklab(working);
                }
                if (grading) ColorGrading::adjust(lab, zones);
                if (chromatic || grading) row[x] = ipr::Vec3f(WorkingColor::oklabToWorking(lab));
            }
        }
    });
}

std::optional<ipr::Vec3d> colourBeforePointColour(const ipr::Image &linearRgb, double x, double y, int radius,
                                                 const Parameters &parameters)
{
    if (linearRgb.empty() || !(linearRgb.depth() == ipr::Depth::F32 && linearRgb.channels() == 3) || !(x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0))
        return std::nullopt;
    const int cx = std::min(int(x * linearRgb.width()), linearRgb.width() - 1);
    const int cy = std::min(int(y * linearRgb.height()), linearRgb.height() - 1);
    const ipr::Recti area = ipr::Recti(cx - radius, cy - radius, 2 * radius + 1, 2 * radius + 1)
                          & ipr::Recti(0, 0, linearRgb.width(), linearRgb.height());
    const std::vector<double> mean = ipr::meanOf(linearRgb.copy(area));
    ipr::Image colour = ipr::Image::create({1, 1}, ipr::Depth::F32, 3);
    ipr::fill(colour, std::vector<double>{mean[0], mean[1], mean[2]});
    Parameters before = parameters;   // the stages ahead of the point colour only
    before.pointColor = {};
    before.gradingSaturation = {};
    before.gradingLuminance = {};
    apply(colour, before);
    return ipr::Vec3d(colour.row<ipr::Vec3f>(0)[0]);
}

ipr::Image pointWeights(const ipr::Image &linearRgb, const Parameters &parameters, int point)
{
    if (linearRgb.empty() || !(linearRgb.depth() == ipr::Depth::F32 && linearRgb.channels() == 3) || point < 0 ||
        point >= int(std::min(parameters.pointColor.count, PointColor::MaxPoints)))
        return {};
    ipr::Image colours = linearRgb.clone();
    Parameters before = parameters;   // the stages ahead of the point colour only
    before.pointColor = {};
    before.gradingSaturation = {};
    before.gradingLuminance = {};
    apply(colours, before);
    const PointColor::Point &selected = parameters.pointColor.points[size_t(point)];
    ipr::Image weights = ipr::Image::create(colours.size(), ipr::Depth::F32, 1);
    ipr::parallelRows(colours.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const auto *in = colours.row<ipr::Vec3f>(y);
            auto *out = weights.mutableRow<float>(y);
            for (int x = 0; x < colours.width(); ++x)
                out[x] = float(PointColor::weight(PointColor::toHsv(ipr::Vec3d(in[x])), selected));
        }
    });
    return weights;
}

} // namespace PerceptualColour
