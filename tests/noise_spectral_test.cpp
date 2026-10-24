#include "noise_spectral_signature.h"
#include <QJsonObject>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("spectral assertion failed"); }
}
int main()
{
    try {
        check(!NoiseSpectralStudy::signature(cv::Mat(32,32,CV_32F,cv::Scalar(.25)))["valid"].toBool());
        cv::Mat noise(32,32,CV_32F),texture(32,32,CV_32F);
        cv::RNG rng(733);rng.fill(noise,cv::RNG::NORMAL,0.,.01);
        for (int y=0;y<32;++y) for (int x=0;x<32;++x)
            texture.at<float>(y,x)=float(.07*std::sin(x*.9)*std::cos(y*.7));
        const auto n=NoiseSpectralStudy::signature(noise)["features"].toObject();
        const auto t=NoiseSpectralStudy::signature(texture)["features"].toObject();
        const auto scaled=NoiseSpectralStudy::signature(noise*.02)["features"].toObject();
        check(t["top8_power_fraction"].toDouble()>n["top8_power_fraction"].toDouble());
        check(t["spectral_flatness"].toDouble()<n["spectral_flatness"].toDouble());
        for (auto it=n.begin();it!=n.end();++it) {
            check(std::isfinite(it.value().toDouble()));
            check(std::abs(it.value().toDouble()-scaled[it.key()].toDouble())<1e-4);
        }
        for (int invalid=0;invalid<4;++invalid) {
            cv::Mat bad=invalid==0?cv::Mat():noise.clone();
            if (invalid==1) bad.at<float>(0,0)=std::numeric_limits<float>::quiet_NaN();
            if (invalid==2) bad=cv::Mat(32,32,CV_32FC3);
            if (invalid==3) bad=noise*1e30;
            bool threw=false;try {NoiseSpectralStudy::signature(bad);}catch(const std::invalid_argument &){threw=true;}
            check(threw);
        }
        const auto injected=NoiseSpectralStudy::injectedControls(noise);
        check(injected.size()==22);
        check(injected[0].toObject()["signature"].toObject()==NoiseSpectralStudy::signature(noise));
        check(NoiseSpectralStudy::injectedControls(cv::Mat(32,32,CV_32F,cv::Scalar(0))).isEmpty());
        std::cout<<"spectral signature controls passed\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
