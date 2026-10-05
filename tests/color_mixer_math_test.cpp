#include "ColorMixer.h"
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <iostream>
#include <limits>

namespace {
float y(const cv::Vec3f &rgb) { return rgb.dot(cv::Vec3f(0.2126f, 0.7152f, 0.0722f)); }
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}
bool close(const cv::Vec3f &a, const cv::Vec3f &b, float tolerance = 1e-6f)
{
    return cv::norm(a - b) <= tolerance;
}
cv::Vec3f hsv(float hue, float saturation, float value)
{
    cv::Mat input(1, 1, CV_32FC3, cv::Scalar(hue, saturation, value)), output;
    cv::cvtColor(input, output, cv::COLOR_HSV2RGB);
    return output.at<cv::Vec3f>(0, 0);
}
}

int main()
{
    ColorMixer::Parameters parameters;
    for (float h = 0; h < 360; h += 0.5f) {
        const cv::Vec3f source = hsv(h, 0.8f, 0.4f);
        if (!check(close(source, ColorMixer::apply(source, parameters), 0), "zero settings must be an exact identity")) return 1;
    }
    for (size_t band = 0; band < ColorMixer::BandCount; ++band) {
        for (int component = 0; component < 3; ++component) {
            for (int amount : {-100, 100}) {
                parameters = {};
                auto &values = component == 0 ? parameters.hue : component == 1
                    ? parameters.saturation : parameters.luminance;
                values[band] = amount;
                for (float gray : {0.0f, 0.001f, 0.2f, 1.0f, 3.0f}) {
                    const cv::Vec3f source(gray, gray, gray);
                    if (!check(close(source, ColorMixer::apply(source, parameters), 0),
                               "all bands/components must preserve gray, black and HDR neutrals exactly")) return 1;
                }
                for (size_t other = 0; other < ColorMixer::BandCount; ++other) {
                    const cv::Vec3f source = hsv(ColorMixer::Centers[other], 0.8f, 0.4f);
                    const cv::Vec3f result = ColorMixer::apply(source, parameters);
                    if (!check(other == band ? !close(source, result) : close(source, result),
                               "eight bands must act independently at their centers")) return 1;
                    if (component != 2 && !check(std::abs(y(source) - y(result)) < 1e-6f,
                                               "hue and saturation must preserve linear luminance")) return 1;
                }
            }
        }
    }
    // Dense circular scan, including overlap/wrap: no compound gain or hue seams.
    for (int amount : {-100, 100}) {
        parameters = {};
        parameters.luminance.fill(amount);
        cv::Vec3f previous = ColorMixer::apply(hsv(359.5f, 0.8f, 0.3f), parameters);
        for (float h = 0; h < 360; h += 0.5f) {
            for (float value : {1e-7f, 0.01f, 0.3f, 3.0f}) {
                const cv::Vec3f source = hsv(h, 0.8f, value);
                const cv::Vec3f result = ColorMixer::apply(source, parameters);
                const float ratio = y(result) / y(source);
                if (!check(std::isfinite(ratio) && std::abs(ratio - std::exp2(amount * 0.005f)) < 2e-6f,
                           "selective exposure must stay at +/-0.5 EV, even in dark/HDR overlaps")) return 1;
            }
            const cv::Vec3f current = ColorMixer::apply(hsv(h, 0.8f, 0.3f), parameters);
            if (!check(close(current, previous, 0.004f), "luminance must stay continuous across bands and red wrap")) return 1;
            previous = current;
        }
    }
    // Saturation masks partition unity: adjacent +50 edits give x1.5, not x1.5625.
    parameters = {};
    parameters.saturation.fill(50);
    const cv::Vec3f midpoint = hsv(255, 0.2f, 0.3f);
    cv::Vec3f expected = hsv(255, 0.3f, 0.3f);
    expected *= y(midpoint) / y(expected);
    if (!check(close(ColorMixer::apply(midpoint, parameters), expected), "overlapping saturation must use one combined gain")) return 1;
    // Protection tends smoothly to identity on near-neutrals.
    parameters = {};
    parameters.luminance.fill(100);
    for (float s : {1e-5f, 0.025f, 0.05f}) {
        const cv::Vec3f source = hsv(270, s, 0.2f);
        const float t = std::min(1.0f, s / 0.05f);
        const float expectedGain = std::exp2(0.5f * t * t * (3 - 2 * t));
        if (!check(std::abs(y(ColorMixer::apply(source, parameters)) / y(source) - expectedGain) < 2e-6f,
                   "near-neutral protection must follow the documented continuous ramp")) return 1;
    }
    const cv::Vec3f outOfGamut(-0.03f, 0.2f, 0.1f);
    if (!check(close(ColorMixer::apply(outOfGamut, parameters), outOfGamut, 0), "negative working colors must not be silently clipped")) return 1;
    // Defensive clamping for imported sidecars as well as UI-originated values.
    parameters.luminance.fill(std::numeric_limits<int>::max());
    const cv::Vec3f source = hsv(120, 0.8f, 0.3f);
    return check(std::abs(y(ColorMixer::apply(source, parameters)) / y(source) - std::sqrt(2.0f)) < 2e-6f,
                 "malformed imported commands must not bypass the luminance bound") ? 0 : 1;
}
