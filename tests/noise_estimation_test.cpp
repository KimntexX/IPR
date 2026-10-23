#include "NoiseEstimation.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
bool check(bool passed, const char *message)
{
    if (!passed) std::cerr << "FAILED: " << message << '\n';
    return passed;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool passed=true;
    cv::Mat flat(256,256,CV_32F,cv::Scalar(.2));
    auto result=NoiseEstimation::estimate(flat,flat,flat);
    passed &= check(result.valid && NoiseEstimation::sigma(result.planes[0],.2f)==0, "constant field has no noise");
    cv::RNG rng(1948);
    cv::Mat signal(512,512,CV_32F), noisy(512,512,CV_32F), chroma(512,512,CV_32F);
    for (int y=0;y<512;++y) for (int x=0;x<512;++x) {
        const float level=.05f+.8f*x/511.f;
        signal.at<float>(y,x)=level;
        noisy.at<float>(y,x)=level+static_cast<float>(rng.gaussian(std::sqrt(.0004*level+.000025)));
        chroma.at<float>(y,x)=static_cast<float>(rng.gaussian(.01));
    }
    result=NoiseEstimation::estimate(noisy,chroma,chroma);
    std::cout << "slope=" << result.planes[0].slope << " intercept=" << result.planes[0].intercept << '\n';
    passed &= check(result.valid && result.planes[0].affine, "signal-dependent variance can be fitted");
    for (float level : {.1f,.4f,.8f}) {
        const float expected=std::sqrt(.0004f*level+.000025f);
        const float actual=NoiseEstimation::sigma(result.planes[0],level);
        passed &= check(std::abs(actual/expected-1)<.3f, "effective sigma recovers synthetic level within 30 percent");
    }
    passed &= check(result.planes[0].patches<=576, "sampling is bounded");
    const auto cached=NoiseEstimation::estimate(noisy,chroma,chroma);
    passed &= check(cached.planes[0].slope==result.planes[0].slope, "repeat estimation is deterministic");
    const cv::Mat twice=chroma*2;
    const auto changed=NoiseEstimation::estimate(noisy,twice,twice);
    passed &= check(NoiseEstimation::sigma(changed.planes[1],.4f)>
                    1.7f*NoiseEstimation::sigma(cached.planes[1],.4f), "changed samples invalidate cached statistics");
    cv::Mat bad=noisy.clone(); bad.at<float>(0,0)=std::numeric_limits<float>::quiet_NaN();
    passed &= check(!NoiseEstimation::estimate(bad,chroma,chroma).valid, "nonfinite samples rejected");
    passed &= check(!NoiseEstimation::estimate({},chroma,chroma).valid, "empty inputs rejected");
    passed &= check(!NoiseEstimation::estimate(noisy,flat,chroma).valid, "mismatched geometry rejected");
    cv::Mat small(4,4,CV_32F,cv::Scalar(1));
    passed &= check(!NoiseEstimation::estimate(small,small,small).valid, "insufficient spatial support explicit");
    return passed ? 0 : 1;
}
