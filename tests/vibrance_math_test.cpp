#include "Vibrance.h"
#include "WorkingColorSpace.h"

#include <opencv2/core.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
}

double chroma(const cv::Vec3f &rgb)
{
    const cv::Vec3d lab = Vibrance::toOklab(cv::Vec3d(rgb));
    return std::hypot(lab[1], lab[2]);
}

double gain(const cv::Vec3f &rgb, double vibrance)
{
    return chroma(Vibrance::applyPixel(rgb, vibrance, 0)) / chroma(rgb);
}
}

int main()
{
    try {
        for (const cv::Vec3f rgb : {cv::Vec3f(0, 0, 0), cv::Vec3f(.2f, .2f, .2f), cv::Vec3f(.02f, .7f, .1f),
                 cv::Vec3f(.9f, .03f, .2f), cv::Vec3f(.25f, .3f, .35f), cv::Vec3f(4, 4, 4),
                 cv::Vec3f(2, .4f, .1f), cv::Vec3f(-.1f, .2f, .5f), cv::Vec3f(.45f, .3f, .22f)}) {
            const bool grey = rgb[0] == rgb[1] && rgb[1] == rgb[2];
            check(cv::norm(Vibrance::applyPixel(rgb, 0, 0) - rgb) == 0, "Zero is exact identity");
            double previous = -1;
            for (int v = -100; v <= 100; ++v) {
                const cv::Vec3f changed = Vibrance::applyPixel(rgb, v / 100., 0);
                for (int c = 0; c < 3; ++c) check(std::isfinite(changed[c]), "Finite signed/HDR result");
                if (grey) {
                    check(cv::norm(changed - rgb) == 0, "Exact grey/HDR grey identity");
                    continue;
                }
                const double current = chroma(changed);
                check(current + 1e-6 >= previous, "Chroma must not decrease with vibrance");
                previous = current;
                for (double s : {-1., -.5, .5, 1.}) {
                    const cv::Vec3f combined = Vibrance::applyPixel(rgb, v / 100., s);
                    for (int c = 0; c < 3; ++c) check(std::isfinite(combined[c]), "Finite combined controls");
                    if (s == -1.) check(chroma(combined) < 2e-6, "Saturation -100 is grey");
                }
            }
            if (!grey) {
                const cv::Vec3d before = Vibrance::toOklab(cv::Vec3d(rgb));
                const cv::Vec3d after = Vibrance::toOklab(cv::Vec3d(Vibrance::applyPixel(rgb, 0, .5)));
                check(std::abs(after[0] - before[0]) < 2e-6, "Saturation alone preserves Oklab L");
            }
            const cv::Vec3f left = Vibrance::applyPixel(rgb, -1e-6, 0), right = Vibrance::applyPixel(rgb, 1e-6, 0);
            check(cv::norm(left - right) < 1e-5, "Continuous transition at zero");
            check(cv::norm(Vibrance::applyPixel(rgb, 5, 5) - Vibrance::applyPixel(rgb, 1, 1)) == 0,
                  "Clamp normalised controls");
        }

        // Lightroom response: strongest on moderately saturated mid-tones, saturated
        // colours protected, near-total desaturation of weak colours at -100.
        const cv::Vec3f weak(.24f, .20f, .17f), strong(.60f, .05f, .03f);
        check(gain(weak, 1) > 1.5 && gain(weak, 1) < 4.5, "Vibrance +100 on a moderate colour");
        check(gain(strong, 1) < gain(weak, 1), "Saturated colours are boosted less");
        check(gain(weak, .25) - 1 < (gain(weak, 1) - 1) / 4, "Progressive positive response");
        check(gain(weak, -1) < .25, "Vibrance -100 nearly removes weak colours");
        check(gain(strong, -1) > gain(weak, -1), "Saturated colours keep more chroma at -100");

        // Image path: Rec.2020 greys exact, colours changed, unsupported inputs untouched.
        cv::Mat image(1, 2, CV_32FC3);
        image.at<cv::Vec3f>(0, 0) = {.3f, .3f, .3f};
        image.at<cv::Vec3f>(0, 1) = WorkingColor::fromSrgb() * cv::Vec3d(weak);
        const cv::Mat original = image.clone();
        Vibrance::apply(image, 0, 50);
        check(image.at<cv::Vec3f>(0, 0) == original.at<cv::Vec3f>(0, 0), "Rec.2020 grey unchanged");
        check(cv::norm(image.at<cv::Vec3f>(0, 1) - original.at<cv::Vec3f>(0, 1)) > 1e-3, "Rec.2020 colour changed");
        cv::Mat empty;
        Vibrance::apply(empty, 50, 50);
        cv::Mat wrong(1, 1, CV_32FC4, cv::Scalar(.1, .2, .3, 1));
        Vibrance::apply(wrong, 50, 50);
        check(wrong.at<cv::Vec4f>(0, 0) == cv::Vec4f(.1f, .2f, .3f, 1.f), "Unsupported type untouched");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
