#include "ColorGrading.h"
#include "GradingWheelProvider.h"
#include "PerceptualColour.h"
#include "WorkingColorSpace.h"

#include <cmath>
#include <iostream>

namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

// Grey ramp in linear Rec.2020 over Oklab lightness 0.02 .. 0.98.
cv::Mat greyRamp()
{
    cv::Mat ramp(1, 49, CV_32FC3);
    for (int x = 0; x < ramp.cols; ++x) {
        const double lightness = 0.02 + 0.02 * x;
        const double y = lightness * lightness * lightness;   // Oklab L of a grey is cbrt(Y)
        ramp.at<cv::Vec3f>(0, x) = cv::Vec3f(float(y), float(y), float(y));
    }
    return ramp;
}

// Oklab change of each ramp pixel.
std::vector<cv::Vec3d> changes(const cv::Mat &before, const cv::Mat &after)
{
    std::vector<cv::Vec3d> out;
    for (int x = 0; x < before.cols; ++x)
        out.push_back(WorkingColor::workingToOklab(cv::Vec3d(after.at<cv::Vec3f>(0, x)))
                      - WorkingColor::workingToOklab(cv::Vec3d(before.at<cv::Vec3f>(0, x))));
    return out;
}

double chroma(const cv::Vec3d &d) { return std::hypot(d[1], d[2]); }

cv::Mat graded(const cv::Mat &ramp, int zone, int hue, int saturation)
{
    cv::Mat out = ramp.clone();
    PerceptualColour::Parameters p;
    p.gradingHue[size_t(zone)] = hue;
    p.gradingSaturation[size_t(zone)] = saturation;
    PerceptualColour::apply(out, p);
    return out;
}

} // namespace

int main()
{
    const cv::Mat ramp = greyRamp();

    cv::Mat same = ramp.clone();
    PerceptualColour::Parameters hueOnly;
    hueOnly.gradingHue = {200, 30, 120};
    PerceptualColour::apply(same, hueOnly);
    if (!check(cv::norm(same, ramp, cv::NORM_INF) == 0, "zero saturation leaves the image unchanged at any hue")) return 1;

    // Each zone acts where Lightroom's does: peak index on the ramp (lightness 0.02 + 0.02 i).
    const int expected[3] = {16, 29, 41};   // lightness about 0.35, 0.60, 0.85
    for (int zone = 0; zone < 3; ++zone) {
        const auto d = changes(ramp, graded(ramp, zone, 30, 50));
        int peak = 0;
        for (int i = 1; i < int(d.size()); ++i)
            if (chroma(d[size_t(i)]) > chroma(d[size_t(peak)])) peak = i;
        if (!check(std::abs(peak - expected[zone]) <= 3, "each zone peaks at its lightness")) return 1;
        const double angle = std::fmod(std::atan2(d[size_t(peak)][2], d[size_t(peak)][1]) * 180 / 3.14159265358979 + 360, 360);
        if (!check(angle > 25 && angle < 60, "an orange hue pushes towards orange in Oklab")) return 1;
        const int far = zone == 2 ? 2 : int(d.size()) - 2;
        if (!check(chroma(d[size_t(far)]) < 0.1 * chroma(d[size_t(peak)]), "a zone leaves the other end of the range alone")) return 1;
        const auto half = changes(ramp, graded(ramp, zone, 30, 25));
        const double ratio = chroma(half[size_t(peak)]) / chroma(d[size_t(peak)]);
        if (!check(std::abs(ratio - 0.5) < 0.05, "saturation 25 gives half the change of 50")) return 1;
    }

    // Blending widens the shadows towards the midtones; a positive balance folds them back
    // towards black (Lightroom: shadow peak at lightness 0.45 at blending 100, 0.20 at balance +50).
    const auto shadowsAt = [&](int blending, int balance) {
        cv::Mat out = ramp.clone();
        PerceptualColour::Parameters p;
        p.gradingHue[0] = 30;
        p.gradingSaturation[0] = 50;
        p.gradingBlending = blending;
        p.gradingBalance = balance;
        PerceptualColour::apply(out, p);
        return changes(ramp, out);
    };
    const auto wide = shadowsAt(100, 0), narrow = shadowsAt(0, 0), folded = shadowsAt(50, 50), plain = shadowsAt(50, 0);
    if (!check(chroma(wide[29]) > 2 * chroma(narrow[29]), "blending 100 carries the shadows into the midtones")) return 1;
    if (!check(chroma(folded[24]) < 0.6 * chroma(plain[24]) && chroma(folded[6]) > 0.8 * chroma(plain[6]),
               "a positive balance folds the shadows towards black")) return 1;

    // Hue is circular: 359 and 0 degrees give almost the same colour.
    const auto a = changes(ramp, graded(ramp, 1, 359, 50)), b = changes(ramp, graded(ramp, 1, 0, 50));
    if (!check(cv::norm(a[29] - b[29]) < 0.01 * chroma(b[29]) + 1e-4, "hue 359 and 0 degrees meet")) return 1;

    cv::Mat strong = graded(ramp, 0, 120, 100);
    bool finite = true;
    for (int x = 0; x < strong.cols; ++x)
        for (int c = 0; c < 3; ++c) finite = finite && std::isfinite(strong.at<cv::Vec3f>(0, x)[c]);
    if (!check(finite, "output stays finite")) return 1;

    // The direct working-space Oklab is the sRGB one for sRGB colours, and round-trips.
    const cv::Matx33d fromSrgb = WorkingColor::fromSrgb();
    double gap = 0, trip = 0;
    for (const cv::Vec3d srgb : {cv::Vec3d(.2, .5, .1), cv::Vec3d(.9, .1, .3), cv::Vec3d(.05, .05, .6), cv::Vec3d(.18, .18, .18)}) {
        const cv::Vec3d working = fromSrgb * srgb;
        gap = std::max(gap, cv::norm(WorkingColor::workingToOklab(working) - WorkingColor::toOklab(srgb)));
        trip = std::max(trip, cv::norm(WorkingColor::oklabToWorking(WorkingColor::workingToOklab(working)) - working));
    }
    const cv::Vec3d grey = WorkingColor::workingToOklab(cv::Vec3d(.18, .18, .18));
    // Ottosson's 10-decimal coefficients leave about 1e-8 (a 16-bit step is 1.5e-5).
    if (!check(gap < 1e-9 && trip < 1e-6 && std::hypot(grey[1], grey[2]) < 1e-6,
               "working-space Oklab matches the sRGB one, keeps greys neutral and round-trips")) return 1;

    // The UI colour follows the measured effect: grey at 0, stronger with saturation, and
    // at 210 degrees the cyan-blue of Lightroom's change (an HSL wheel showed blue).
    const auto displayed = [](int zone, int hue, int saturation) {
        const cv::Vec3f e = ColorGrading::displayColour(zone, hue, saturation);
        cv::Vec3d linear;
        for (int c = 0; c < 3; ++c) linear[c] = WorkingColor::srgbDecode(double(e[c]));
        return WorkingColor::toOklab(linear);
    };
    const cv::Vec3d shownGrey = displayed(1, 210, 0), half = displayed(1, 210, 50), full = displayed(1, 210, 100);
    const double shownAngle = std::fmod(std::atan2(full[2], full[1]) * 180 / 3.14159265358979 + 360, 360);
    const auto effect = changes(ramp, graded(ramp, 1, 210, 50))[29];
    const double effectAngle = std::fmod(std::atan2(effect[2], effect[1]) * 180 / 3.14159265358979 + 360, 360);
    if (!check(chroma(shownGrey) < 2e-3 && chroma(half) < chroma(full) && std::abs(shownAngle - effectAngle) < 6,
               "the UI colour is grey at 0, grows with saturation and shows the effect's hue")) return 1;

    // The QML wheel: each pixel the colour of its hue and intensity, hue 0 at the top,
    // full intensity at 90 % of the radius, transparent outside the wheel.
    GradingWheelProvider wheels;
    QSize wheelSize;
    const QImage wheel = wheels.requestImage(QStringLiteral("2/200"), &wheelSize, QSize());
    const auto shown = [](int zone, int hue, int saturation) {
        const cv::Vec3f e = ColorGrading::displayColour(zone, hue, saturation);
        return qRgb(int(std::lround(e[0] * 255)), int(std::lround(e[1] * 255)), int(std::lround(e[2] * 255)));
    };
    const auto near = [](QRgb a, QRgb b) {
        return std::abs(qRed(a) - qRed(b)) <= 2 && std::abs(qGreen(a) - qGreen(b)) <= 2 && std::abs(qBlue(a) - qBlue(b)) <= 2;
    };
    const QRgb top = wheel.pixel(100, 100 - 85), right = wheel.pixel(100 + 85, 100), centre = wheel.pixel(100, 100);
    if (!check(wheelSize == QSize(200, 200) && qAlpha(wheel.pixel(2, 2)) == 0 && qAlpha(centre) == 255 &&
               near(centre, shown(2, 0, 0)) && near(top, shown(2, 0, 94)) && near(right, shown(2, 90, 95)),
               "the wheel image shows each hue and intensity where the pointer sets them")) return 1;
    if (!check(qAlpha(wheels.requestImage(QStringLiteral("7/50"), nullptr, QSize()).pixel(25, 25)) == 0,
               "an unknown zone gives an empty wheel")) return 1;

    cv::Mat empty;
    PerceptualColour::Parameters strongAll;
    strongAll.gradingHue = {30, 30, 30};
    strongAll.gradingSaturation = {50, 50, 50};
    PerceptualColour::apply(empty, strongAll);
    if (!check(empty.empty(), "an empty image is left alone")) return 1;
    return 0;
}
