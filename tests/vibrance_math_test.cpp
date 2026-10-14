#include "Vibrance.h"
#include <opencv2/core.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
}
}

int main()
{
    try {
        double previous = -2;
        for (int v = -1000; v <= 1000; ++v) {
            const double a = v/1000., effective = Vibrance::effectiveAmount(a);
            check(effective >= previous, "A1 effective amount must be monotone");
            if (std::abs(a) <= .75) check(effective == a, "A1 preserves intermediate intensities");
            previous = effective;
        }
        check(std::abs(Vibrance::effectiveAmount(-1)+.965) < 1e-12, "A1 negative endpoint");
        check(std::abs(Vibrance::effectiveAmount(1)-1.065) < 1e-12, "A1 positive endpoint");
        for (const cv::Vec3f rgb : {cv::Vec3f(0,0,0), cv::Vec3f(.2f,.2f,.2f),
                cv::Vec3f(.02f,.7f,.1f), cv::Vec3f(.9f,.03f,.2f),
                cv::Vec3f(.25f,.3f,.35f), cv::Vec3f(4,4,4),
                cv::Vec3f(2,.4f,.1f), cv::Vec3f(-.1f,.2f,.5f)}) {
            const cv::Vec3d original = Vibrance::toOklab(cv::Vec3d(rgb));
            check(cv::norm(Vibrance::apply(rgb,0,0)-rgb) == 0, "Zero is exact identity");
            double previousChroma = -1;
            for (int v = -100; v <= 100; ++v) {
                const auto changed = Vibrance::apply(rgb,v/100.,0);
                for (int channel=0;channel<3;++channel)
                    check(std::isfinite(changed[channel]), "Finite signed/HDR result");
                const auto lab = Vibrance::toOklab(cv::Vec3d(changed));
                const double chroma = std::hypot(lab[1],lab[2]);
                check(std::abs(lab[0]-original[0]) < 2e-6, "Preserve Oklab L before output");
                check(chroma+2e-6 >= previousChroma, "Vibrance chroma progression");
                previousChroma = chroma;
                if (rgb[0]==rgb[1] && rgb[1]==rgb[2])
                    check(cv::norm(changed-rgb)==0, "Exact gray/HDR gray identity");
                for (double s : {-1., -.5, .5, 1.}) {
                    const auto combined = Vibrance::apply(rgb,v/100.,s);
                    const auto result = Vibrance::toOklab(cv::Vec3d(combined));
                    check(std::abs(result[0]-original[0]) < 2e-6, "Combined controls preserve L");
                    for (int c=0;c<3;++c) check(std::isfinite(combined[c]), "Finite combined controls");
                    if (s == -1.) check(std::hypot(result[1],result[2]) < 2e-6, "Saturation -100 is gray");
                }
            }
            const auto left = Vibrance::apply(rgb,-1e-6,0), right = Vibrance::apply(rgb,1e-6,0);
            check(cv::norm(left-right)<1e-5, "Continuous transition at zero");
            check(cv::norm(Vibrance::apply(rgb,5,5)-Vibrance::apply(rgb,1,1))==0, "Clamp normalized controls");
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
