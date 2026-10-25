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

namespace {
// Reference colour of the slider tracks: vivid enough to read, Oklab.
constexpr double TrackLightness = 0.68;
constexpr double TrackChroma = 0.14;
constexpr double TrackSaturation = 0.4; // ProPhoto saturation cell used to find the peak

// Oklab hue of each band's strongest hue response at +100, from the tables.
const std::array<double, BandCount> &peakHues()
{
    static const std::array<double, BandCount> peaks = [] {
        std::array<double, BandCount> result{};
        Position p;
        bracket(Model::Saturation, Model::SaturationCount, TrackSaturation, p.s, p.ts);
        bracket(Model::Lightness, Model::LightnessCount, TrackLightness, p.l, p.tl);
        const int plus100 = int(std::find(Model::Values, Model::Values + Model::ValueCount, 100) - Model::Values);
        for (int band = 0; band < int(BandCount); ++band) {
            int best = 0;
            double strongest = -1.0;
            for (int i = 0; i < Model::BandBinCount[band]; ++i) {
                const double rotation = std::abs(cell(Model::BandOffset[band] + i, 0, plus100, 0, p));
                if (rotation > strongest) {
                    strongest = rotation;
                    best = i;
                }
            }
            result[size_t(band)] = std::fmod((Model::BandFirstBin[band] + best + 0.5) * Model::HueStepDegrees, 360.0);
        }
        return result;
    }();
    return peaks;
}

// Encoded sRGB of an Oklab colour, chroma reduced until it fits the gamut.
cv::Vec3f encodedInGamut(const cv::Vec3d &lab)
{
    const double lightness = std::clamp(lab[0], 0.0, 1.0);
    cv::Vec3d srgb(lightness, lightness, lightness);
    for (int step = 100; step >= 0; --step) {
        const double scale = step / 100.0;
        const cv::Vec3d candidate = WorkingColor::fromOklab({lightness, lab[1] * scale, lab[2] * scale});
        if (std::min({candidate[0], candidate[1], candidate[2]}) >= -1e-6 &&
            std::max({candidate[0], candidate[1], candidate[2]}) <= 1.0 + 1e-6) {
            srgb = candidate;
            break;
        }
    }
    cv::Vec3f encoded;
    for (int c = 0; c < 3; ++c)
        encoded[c] = float(std::clamp(WorkingColor::srgbEncode(std::clamp(srgb[c], 0.0, 1.0)), 0.0, 1.0));
    return encoded;
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
    // The tables are measured in Oklab and ProPhoto saturation, both reached from the
    // Rec.2020 master in one matrix, unclipped.
    cv::Vec3d lab = WorkingColor::workingToOklab(cv::Vec3d(rgb));
    const double chroma = std::hypot(lab[1], lab[2]);
    if (chroma < 1e-12) return rgb;
    double hue = std::atan2(lab[2], lab[1]) * DegreesPerRadian;
    if (hue < 0.0) hue += 360.0;
    Position position;
    bracket(Model::Saturation, Model::SaturationCount, WorkingColor::proPhotoSaturation(cv::Vec3d(rgb)), position.s, position.ts);
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
    return cv::Vec3f(WorkingColor::oklabToWorking(lab));
}

cv::Vec3f trackColour(int band, int component, int value)
{
    if (band < 0 || band >= int(BandCount) || component < 0 || component >= Model::ComponentCount)
        return {0.5f, 0.5f, 0.5f};
    const double hue = peakHues()[size_t(band)] / DegreesPerRadian;
    const cv::Vec3d reference = WorkingColor::oklabToWorking(
        {TrackLightness, TrackChroma * std::cos(hue), TrackChroma * std::sin(hue)});
    Parameters parameters;
    (component == 0 ? parameters.hue : component == 1 ? parameters.saturation : parameters.luminance)[size_t(band)] = value;
    const cv::Vec3f reached = apply(cv::Vec3f(reference), parameters);
    return encodedInGamut(WorkingColor::workingToOklab(cv::Vec3d(reached)));
}
}
