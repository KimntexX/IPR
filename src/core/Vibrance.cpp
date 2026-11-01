#include "Vibrance.h"

#include "VibranceModel.h"
#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>

namespace Vibrance {
namespace {
constexpr double DegreesPerRadian = 180.0 / 3.14159265358979323846;
// Below this Oklab chroma (the fitting threshold) the lightness gain and the
// hue rotation fade out, so near-greys stay continuous with the exact grey
// identity.
constexpr double NearGreyFade = 0.01;

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

struct Position {
    int s, l, h0, h1;
    double ts, tl, th;
};

Position position(double saturation, double lightness, double hueDegrees)
{
    Position p{};
    bracket(Model::Saturation, Model::SaturationCount, saturation, p.s, p.ts);
    bracket(Model::Lightness, Model::LightnessCount, lightness, p.l, p.tl);
    const double h = hueDegrees / Model::HueStepDegrees - 0.5; // circular over the centres
    const double floor = std::floor(h);
    p.th = h - floor;
    p.h0 = (int(floor) % Model::HueCount + Model::HueCount) % Model::HueCount;
    p.h1 = (p.h0 + 1) % Model::HueCount;
    return p;
}

double lookup(const float (&table)[Model::SaturationCount][Model::LightnessCount][Model::HueCount], const Position &p)
{
    const auto hue = [&](int s, int l) {
        return table[s][l][p.h0] * (1.0 - p.th) + table[s][l][p.h1] * p.th;
    };
    const auto light = [&](int s) { return hue(s, p.l) * (1.0 - p.tl) + hue(s, p.l + 1) * p.tl; };
    return light(p.s) * (1.0 - p.ts) + light(p.s + 1) * p.ts;
}

// Near-grey factor of a measured value at an Oklab chroma.
double nearGrey(int valueIndex, double chroma)
{
    int index = 0;
    double weight = 0.0;
    bracket(Model::NearGreyChroma, Model::NearGreyCount, chroma, index, weight);
    const auto &factor = Model::NearGreyFactor[valueIndex];
    return factor[index] * (1.0 - weight) + factor[index + 1] * weight;
}

struct Response {
    double logChroma = 0.0, logLightness = 0.0, rotationDegrees = 0.0;
};

Response at(int index, const Position &p, double pixelChroma)
{
    return {lookup(Model::LogChromaGain[index], p) * nearGrey(index, pixelChroma),
            lookup(Model::LogLightnessGain[index], p), lookup(Model::HueRotation[index], p)};
}

// Response at a slider value in [-100, 100], linear between the measured
// values on the slider's side, zero at zero; the chroma gain of each measured
// value is scaled by its near-grey factor.
Response response(double value, const Position &p, double pixelChroma)
{
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
    if (farIndex < 0) farIndex = nearIndex; // beyond the last measured value
    if (farIndex < 0) return {};
    const double near = nearIndex < 0 ? 0.0 : Model::Values[nearIndex];
    const double far = Model::Values[farIndex];
    const Response low = nearIndex < 0 ? Response{} : at(nearIndex, p, pixelChroma);
    const Response high = at(farIndex, p, pixelChroma);
    const double t = far == near ? 1.0 : (value - near) / (far - near);
    return {low.logChroma + (high.logChroma - low.logChroma) * t,
            low.logLightness + (high.logLightness - low.logLightness) * t,
            low.rotationDegrees + (high.rotationDegrees - low.rotationDegrees) * t};
}
}

void adjust(cv::Vec3d &lab, double proPhotoSaturation, double vibrance, double saturation)
{
    vibrance = std::clamp(vibrance, -1.0, 1.0);
    saturation = std::clamp(saturation, -1.0, 1.0);
    if (vibrance == 0.0 && saturation == 0.0) return;
    const double chroma = std::hypot(lab[1], lab[2]);
    if (chroma < 1e-12) return;
    double chromaGain = std::max(0.0, 1.0 + saturation);
    double lightnessGain = 1.0, rotation = 0.0;
    if (vibrance != 0.0) {
        double hue = std::atan2(lab[2], lab[1]) * DegreesPerRadian;
        if (hue < 0.0) hue += 360.0;
        const Response r = response(vibrance * 100.0, position(proPhotoSaturation, lab[0], hue), chroma);
        const double fade = std::min(1.0, chroma / NearGreyFade);
        chromaGain *= std::exp(r.logChroma);
        lightnessGain = std::exp(r.logLightness * fade);
        rotation = r.rotationDegrees * fade / DegreesPerRadian;
    }
    lab[0] *= lightnessGain;
    const double a = lab[1] * chromaGain, b = lab[2] * chromaGain;
    const double cosine = std::cos(rotation), sine = std::sin(rotation);
    lab[1] = a * cosine - b * sine;
    lab[2] = a * sine + b * cosine;
}

cv::Vec3f applyPixel(const cv::Vec3f &rgb, double vibrance, double saturation)
{
    if ((vibrance == 0.0 && saturation == 0.0) || (rgb[0] == rgb[1] && rgb[1] == rgb[2]))
        return rgb; // exact zero / grey identity, without matrix round trip
    cv::Vec3d lab = WorkingColor::workingToOklab(cv::Vec3d(rgb));
    if (std::hypot(lab[1], lab[2]) < 1e-12) return rgb;
    adjust(lab, WorkingColor::proPhotoSaturation(cv::Vec3d(rgb)), vibrance, saturation);
    return cv::Vec3f(WorkingColor::oklabToWorking(lab));
}
}
