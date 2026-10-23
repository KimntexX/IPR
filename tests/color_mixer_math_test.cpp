#include "ColorMixer.h"
#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
constexpr double DegreesPerRadian = 180.0 / 3.14159265358979323846;
// Band response peaks in Oklab hue (Lightroom measurement; magenta shifted towards IPR).
constexpr double Peaks[ColorMixer::BandCount] = {12, 50, 92, 138, 178, 255, 310, 340};

bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

// Linear Rec.2020 colour of an Oklab lightness, chroma and hue.
cv::Vec3f colour(double lightness, double chroma, double hueDegrees)
{
    const double h = hueDegrees / DegreesPerRadian;
    const cv::Vec3d srgb = WorkingColor::fromOklab({lightness, chroma * std::cos(h), chroma * std::sin(h)});
    return cv::Vec3f(WorkingColor::fromSrgb() * srgb);
}

cv::Vec3d oklab(const cv::Vec3f &rec2020)
{
    return WorkingColor::toOklab(WorkingColor::toSrgb() * cv::Vec3d(rec2020));
}

double hueOf(const cv::Vec3f &rgb)
{
    const cv::Vec3d lab = oklab(rgb);
    return std::atan2(lab[2], lab[1]) * DegreesPerRadian;
}

double rotation(const cv::Vec3f &source, const cv::Vec3f &result)
{
    return std::remainder(hueOf(result) - hueOf(source), 360.0);
}

double chromaRatio(const cv::Vec3f &source, const cv::Vec3f &result)
{
    const cv::Vec3d a = oklab(source), b = oklab(result);
    return std::hypot(b[1], b[2]) / std::hypot(a[1], a[2]);
}

ColorMixer::Parameters single(size_t band, int component, int value)
{
    ColorMixer::Parameters p;
    (component == 0 ? p.hue : component == 1 ? p.saturation : p.luminance)[band] = value;
    return p;
}
}

int main()
{
    ColorMixer::Parameters none;
    for (double h = 0; h < 360; h += 0.5) {
        const cv::Vec3f source = colour(0.6, 0.08, h);
        if (!check(cv::norm(ColorMixer::apply(source, none) - source) == 0, "zero settings must be an exact identity")) return 1;
    }
    for (size_t band = 0; band < ColorMixer::BandCount; ++band) {
        for (int component = 0; component < 3; ++component) {
            for (int value : {-100, 100}) {
                const ColorMixer::Parameters p = single(band, component, value);
                for (float grey : {0.0f, 0.001f, 0.2f, 1.0f, 3.0f}) {
                    const cv::Vec3f source(grey, grey, grey);
                    if (!check(cv::norm(ColorMixer::apply(source, p) - source) == 0,
                               "greys, black and HDR neutrals must stay exact")) return 1;
                }
                const cv::Vec3f peak = colour(0.6, 0.08, Peaks[band]);
                const cv::Vec3f result = ColorMixer::apply(peak, p);
                for (int c = 0; c < 3; ++c)
                    if (!check(std::isfinite(result[c]), "finite result")) return 1;
                if (component == 0 && !check(value > 0 ? rotation(peak, result) > 10 : rotation(peak, result) < -10,
                                             "the hue slider turns its band by more than 10 degrees, in its direction")) return 1;
                if (component == 1 && !check(value > 0 ? chromaRatio(peak, result) > 1.2 : chromaRatio(peak, result) < 0.3,
                                             "the saturation slider scales its band's chroma")) return 1;
                if (component == 2 && !check(value > 0 ? oklab(result)[0] > oklab(peak)[0] : oklab(result)[0] < oklab(peak)[0],
                                             "the luminance slider moves its band's lightness")) return 1;
                for (size_t other = 0; other < ColorMixer::BandCount; ++other) {
                    if (std::abs(std::remainder(Peaks[other] - Peaks[band], 360.0)) < 100) continue;
                    const cv::Vec3f far = colour(0.6, 0.08, Peaks[other]);
                    const cv::Vec3f changed = ColorMixer::apply(far, p);
                    if (!check(std::abs(rotation(far, changed)) < 0.5 && std::abs(std::log(chromaRatio(far, changed))) < 0.01,
                               "a band leaves distant hues unchanged")) return 1;
                }
            }
        }
    }
    // A stronger hue slider never turns less (orange peak).
    double previous = -1e9;
    for (int v = -100; v <= 100; v += 5) {
        const cv::Vec3f peak = colour(0.6, 0.08, Peaks[1]);
        const double r = rotation(peak, ColorMixer::apply(peak, single(1, 0, v)));
        if (!check(r + 1e-6 >= previous, "rotation must not decrease with the hue slider")) return 1;
        previous = r;
    }
    // Every hue slider at +100: the result stays continuous around the hue circle.
    ColorMixer::Parameters all;
    all.hue.fill(100);
    cv::Vec3f last = ColorMixer::apply(colour(0.6, 0.08, -0.25), all);
    for (double h = 0; h < 360; h += 0.25) {
        const cv::Vec3f current = ColorMixer::apply(colour(0.6, 0.08, h), all);
        if (!check(cv::norm(oklab(current) - oklab(last)) < 0.01, "no seam across bands or the hue wrap")) return 1;
        last = current;
    }
    // Near-greys fade continuously to the grey identity.
    const ColorMixer::Parameters blue = single(5, 0, 100);
    const cv::Vec3f nearGrey = colour(0.6, 1e-4, Peaks[5]);
    if (!check(std::abs(rotation(nearGrey, ColorMixer::apply(nearGrey, blue))) < 1.0, "near-greys barely turn")) return 1;
    const cv::Vec3f outOfGamut(-0.03f, 0.2f, 0.1f);
    const cv::Vec3f signedResult = ColorMixer::apply(outOfGamut, single(3, 1, 100));
    for (int c = 0; c < 3; ++c)
        if (!check(std::isfinite(signedResult[c]), "negative working colours stay finite")) return 1;
    // Slider track colours: encoded sRGB at the band's reference colour.
    const auto encodedHue = [](const cv::Vec3f &encoded) {
        cv::Vec3d linear;
        for (int c = 0; c < 3; ++c) linear[c] = WorkingColor::srgbDecode(double(encoded[c]));
        const cv::Vec3d lab = WorkingColor::toOklab(linear);
        return std::atan2(lab[2], lab[1]) * DegreesPerRadian;
    };
    for (int band = 0; band < int(ColorMixer::BandCount); ++band) {
        const cv::Vec3f reference = ColorMixer::trackColour(band, 0, 0);
        for (int component = 0; component < 3; ++component) {
            for (int value : {-100, -50, 0, 50, 100}) {
                const cv::Vec3f c = ColorMixer::trackColour(band, component, value);
                for (int k = 0; k < 3; ++k)
                    if (!check(std::isfinite(c[k]) && c[k] >= 0.0f && c[k] <= 1.0f, "track colours are encoded sRGB")) return 1;
            }
            if (!check(cv::norm(ColorMixer::trackColour(band, component, 0) - reference) == 0,
                       "every track passes through the band's reference colour at zero")) return 1;
        }
        const double low = std::remainder(encodedHue(ColorMixer::trackColour(band, 0, -100)) - encodedHue(reference), 360.0);
        const double high = std::remainder(encodedHue(ColorMixer::trackColour(band, 0, 100)) - encodedHue(reference), 360.0);
        if (!check(low < -5 && high > 5, "the hue track turns both ways from the reference")) return 1;
        const cv::Vec3f grey = ColorMixer::trackColour(band, 1, -100);
        const float spread = std::max({grey[0], grey[1], grey[2]}) - std::min({grey[0], grey[1], grey[2]});
        const float referenceSpread = std::max({reference[0], reference[1], reference[2]}) -
                                      std::min({reference[0], reference[1], reference[2]});
        // Lightroom keeps a residual at -100: 4 to 12 % of the reference here, violet about 25 %.
        if (!check(spread < 0.3f * referenceSpread, "saturation -100 strongly desaturates the track")) return 1;
        for (int other = 0; other < band; ++other)
            if (!check(std::abs(std::remainder(encodedHue(ColorMixer::trackColour(other, 0, 0)) - encodedHue(reference), 360.0)) > 10,
                       "bands have distinct reference hues")) return 1;
    }
    if (!check(ColorMixer::trackColour(8, 0, 0) == cv::Vec3f(0.5f, 0.5f, 0.5f) &&
               ColorMixer::trackColour(0, 3, 0) == cv::Vec3f(0.5f, 0.5f, 0.5f), "invalid track requests give grey")) return 1;
    // Malformed imported values clamp to +-100.
    const cv::Vec3f orange = colour(0.6, 0.08, Peaks[1]);
    return check(cv::norm(ColorMixer::apply(orange, single(1, 0, std::numeric_limits<int>::max())) -
                          ColorMixer::apply(orange, single(1, 0, 100))) == 0,
                 "malformed slider values clamp to +-100") ? 0 : 1;
}
