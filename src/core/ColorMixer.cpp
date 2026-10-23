#include "ColorMixer.h"

#include "ColorMixerModel.h"
#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>

namespace ColorMixer {
namespace {
constexpr double DegreesPerRadian = 180.0 / 3.14159265358979323846;

struct Effect {
    double rotationDegrees = 0.0, logChroma = 0.0, logLightness = 0.0;
};

struct Position {
    int s = 0, l = 0;
    double ts = 0.0, tl = 0.0;
};

// Lower neighbour and weight for linear interpolation over cell centres,
// constant beyond the first and last centres.
void bracket(const float *centres, int count, double x, int &index, double &weight)
{
    if (x <= centres[0]) {
        index = 0;
        weight = 0.0;
    } else if (x >= centres[count - 1]) {
        index = count - 2;
        weight = 1.0;
    } else {
        index = int(std::upper_bound(centres, centres + count, float(x)) - centres) - 1;
        index = std::clamp(index, 0, count - 2);
        weight = std::clamp((x - centres[index]) / (centres[index + 1] - centres[index]), 0.0, 1.0);
    }
}

// Bilinear value over (saturation, lightness) of one row, component, value and quantity.
double cell(int row, int component, int value, int quantity, const Position &p)
{
    constexpr int plane = Model::SaturationCount * Model::LightnessCount;
    const float *r = Model::Response[row] + ((component * Model::ValueCount + value) * Model::QuantityCount + quantity) * plane;
    const auto at = [&](int s, int l) { return double(r[s * Model::LightnessCount + l]); };
    const double low = at(p.s, p.l) * (1.0 - p.tl) + at(p.s, p.l + 1) * p.tl;
    const double high = at(p.s + 1, p.l) * (1.0 - p.tl) + at(p.s + 1, p.l + 1) * p.tl;
    return low * (1.0 - p.ts) + high * p.ts;
}

// Effect of one measured slider value of a band component, interpolated over
// the band's hue window; zero outside it (its edge bins are zero).
Effect measured(int band, int component, int value, double hueDegrees, const Position &p)
{
    const double first = Model::BandFirstBin[band] * Model::HueStepDegrees;
    const double f = std::fmod(hueDegrees - first + 720.0, 360.0) / Model::HueStepDegrees - 0.5;
    const int count = Model::BandBinCount[band];
    if (f < 0.0 || f >= count - 1) return {};
    const int i = int(f);
    const double t = f - i;
    const int row = Model::BandOffset[band] + i;
    const auto blend = [&](int quantity) {
        return cell(row, component, value, quantity, p) * (1.0 - t) + cell(row + 1, component, value, quantity, p) * t;
    };
    return {blend(0), blend(1), blend(2)};
}

// Effect at a slider value in [-100, 100], linear between the measured values
// on the slider's side, zero at zero.
Effect response(int band, int component, int slider, double hueDegrees, const Position &p)
{
    const double value = std::clamp(slider, -100, 100);
    if (value == 0.0) return {};
    int nearIndex = -1, farIndex = -1;
    for (int i = 0; i < Model::ValueCount; ++i) {
        const double v = Model::Values[i];
        if ((v > 0.0) != (value > 0.0)) continue;
        if (std::abs(v) <= std::abs(value) && (nearIndex < 0 || std::abs(v) > std::abs(Model::Values[nearIndex])))
            nearIndex = i;
        if (std::abs(v) >= std::abs(value) && (farIndex < 0 || std::abs(v) < std::abs(Model::Values[farIndex])))
            farIndex = i;
    }
    if (farIndex < 0) farIndex = nearIndex;
    if (farIndex < 0) return {};
    const double near = nearIndex < 0 ? 0.0 : Model::Values[nearIndex];
    const double far = Model::Values[farIndex];
    const Effect low = nearIndex < 0 ? Effect{} : measured(band, component, nearIndex, hueDegrees, p);
    const Effect high = measured(band, component, farIndex, hueDegrees, p);
    const double t = far == near ? 1.0 : (value - near) / (far - near);
    return {low.rotationDegrees + (high.rotationDegrees - low.rotationDegrees) * t,
            low.logChroma + (high.logChroma - low.logChroma) * t,
            low.logLightness + (high.logLightness - low.logLightness) * t};
}
}

cv::Vec3f apply(const cv::Vec3f &rgb, const Parameters &parameters)
{
    const auto active = [](const std::array<int, BandCount> &values) {
        return std::any_of(values.cbegin(), values.cend(), [](int v) { return v != 0; });
    };
    if (!active(parameters.hue) && !active(parameters.saturation) && !active(parameters.luminance))
        return rgb;
    if (rgb[0] == rgb[1] && rgb[1] == rgb[2])
        return rgb; // exact grey identity, without matrix round trip
    // The tables are measured in linear sRGB: unclipped round trip from the master.
    const cv::Vec3d srgb = WorkingColor::toSrgb() * cv::Vec3d(rgb);
    cv::Vec3d lab = WorkingColor::toOklab(srgb);
    const double chroma = std::hypot(lab[1], lab[2]);
    if (chroma < 1e-12) return rgb;
    double hue = std::atan2(lab[2], lab[1]) * DegreesPerRadian;
    if (hue < 0.0) hue += 360.0;
    Position position;
    bracket(Model::Saturation, Model::SaturationCount, WorkingColor::proPhotoSaturation(srgb), position.s, position.ts);
    bracket(Model::Lightness, Model::LightnessCount, lab[0], position.l, position.tl);

    Effect total;
    const std::array<const std::array<int, BandCount> *, 3> components{
        &parameters.hue, &parameters.saturation, &parameters.luminance};
    for (int band = 0; band < int(BandCount); ++band) {
        for (int component = 0; component < Model::ComponentCount; ++component) {
            const int slider = (*components[size_t(component)])[size_t(band)];
            if (slider == 0) continue;
            const Effect e = response(band, component, slider, hue, position);
            total.rotationDegrees += e.rotationDegrees;
            total.logChroma += e.logChroma;
            total.logLightness += e.logLightness;
        }
    }
    // Near-greys fade to the exact grey identity (the tables exclude them).
    const double fade = std::min(1.0, chroma / Model::NearGreyChroma);
    lab[0] *= std::exp(total.logLightness * fade);
    const double gain = std::exp(total.logChroma * fade);
    const double rotation = total.rotationDegrees * fade / DegreesPerRadian;
    const double a = lab[1] * gain, b = lab[2] * gain;
    lab[1] = a * std::cos(rotation) - b * std::sin(rotation);
    lab[2] = a * std::sin(rotation) + b * std::cos(rotation);
    return cv::Vec3f(WorkingColor::fromSrgb() * WorkingColor::fromOklab(lab));
}
}
