#include "ToneCurve.h"
#include "WorkingColorSpace.h"

#include <cmath>
#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

QVector<ToneCurve::Point> curve(std::initializer_list<ToneCurve::Point> points) { return QVector<ToneCurve::Point>(points); }

bool evaluation()
{
    const auto s = curve({{0, 0}, {0.25f, 0.4f}, {0.75f, 0.7f}, {1, 1}});
    bool ok = check(std::abs(ToneCurve::evaluate(s, 0.25f) - 0.4f) < 1e-6f &&
                    std::abs(ToneCurve::evaluate(s, 0.75f) - 0.7f) < 1e-6f, "the spline passes through its points");
    // Natural spline of (0,0), (.5,.75), (1,1): second derivative -3 at the middle.
    const float expected = float(-0.25 * 0.25 * 0.25 + (1.5 + 0.25) * 0.25);   // -x^3 + 1.75 x
    ok &= check(std::abs(ToneCurve::evaluate(curve({{0, 0}, {0.5f, 0.75f}, {1, 1}}), 0.25f) - expected) < 1e-5f,
                "three points give the natural spline");
    const auto clipped = curve({{32 / 255.f, 0}, {223 / 255.f, 1}});
    ok &= check(ToneCurve::evaluate(clipped, 0.05f) == 0.0f && ToneCurve::evaluate(clipped, 0.95f) == 1.0f,
                "flat before the first point and after a last point below 1");
    ok &= check(std::abs(ToneCurve::evaluate(curve({{0, 0}, {1, 1}}), 1.5f) - 1.5f) < 1e-6f,
                "past a last point at 1 the curve follows its end slope");
    ok &= check(ToneCurve::isIdentity(ToneCurve::Curves{{curve({{0, 0}, {1, 1}}), {}, {}, {}}}) &&
                !ToneCurve::isIdentity(ToneCurve::Curves{{{}, clipped, {}, {}}}), "a linear curve is the identity");
    return ok;
}

bool application()
{
    cv::Mat image(1, 2, CV_32FC3);
    image.at<cv::Vec3f>(0, 0) = {0.2f, 0.3f, 0.1f};
    image.at<cv::Vec3f>(0, 1) = {0.5f, 0.5f, 0.5f};
    const cv::Mat before = image.clone();
    const ToneCurve::Curves identity{{curve({{0, 0}, {1, 1}}), curve({{0, 0}, {1, 1}}), {}, {}}};
    ToneCurve::apply(image, identity);
    bool ok = check(cv::norm(image, before, cv::NORM_INF) == 0, "linear curves leave the image untouched");

    // A blue curve changes the blue of ProPhoto only.
    const ToneCurve::Curves blue{{{}, {}, {}, curve({{0, 0}, {0.5f, 0.75f}, {1, 1}})}};
    ToneCurve::apply(image, blue);
    const cv::Vec3d p0 = WorkingColor::toProPhoto() * cv::Vec3d(before.at<cv::Vec3f>(0, 0));
    const cv::Vec3d p1 = WorkingColor::toProPhoto() * cv::Vec3d(image.at<cv::Vec3f>(0, 0));
    ok &= check(std::abs(p1[0] - p0[0]) < 1e-5 && std::abs(p1[1] - p0[1]) < 1e-5 && p1[2] > p0[2] * 1.2,
                "a channel curve acts on its ProPhoto channel alone");
    // The RGB curve acts on every channel: a grey stays grey and brightens.
    image = before.clone();
    ToneCurve::apply(image, ToneCurve::Curves{{curve({{0, 0}, {0.5f, 0.75f}, {1, 1}}), {}, {}, {}}});
    const cv::Vec3f grey = image.at<cv::Vec3f>(0, 1);
    // The published matrices map D65 white to D50 white within about 1e-4.
    ok &= check(grey[0] > 0.6f && std::abs(grey[0] - grey[1]) < 1e-3f && std::abs(grey[1] - grey[2]) < 1e-3f,
                "the RGB curve brightens a grey and keeps it grey");
    return ok;
}
} // namespace

int main()
{
    bool ok = evaluation();
    ok &= application();
    if (ok) std::cout << "toneCurveTests passed\n";
    return ok ? 0 : 1;
}
