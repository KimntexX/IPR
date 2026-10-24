#include "noise_spectral_signature.h"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {
double energy(const cv::Mat &p) { return cv::mean(p.mul(p))[0]; }
double correlation(const cv::Mat &p, int dx, int dy)
{
    const cv::Rect r(0,0,p.cols-dx,p.rows-dy);
    const cv::Mat a=p(r),b=p(r+cv::Point(dx,dy));
    const double ma=cv::mean(a)[0],mb=cv::mean(b)[0];
    const double va=std::max(0.,energy(a)-ma*ma),vb=std::max(0.,energy(b)-mb*mb);
    if (va*vb<1e-40) return 0.;
    return std::clamp((cv::mean(a.mul(b))[0]-ma*mb)/std::sqrt(va*vb),-1.,1.);
}
}

namespace NoiseSpectralStudy {
QJsonObject signature(const cv::Mat &tile)
{
    if (tile.dims!=2 || tile.type()!=CV_32FC1 || tile.size()!=cv::Size(32,32) || !cv::checkRange(tile))
        throw std::invalid_argument("spectral signature needs a finite 32x32 CV_32FC1 tile");
    const cv::Mat centered=tile-cv::mean(tile)[0];
    const double totalEnergy=energy(centered);
    if (!std::isfinite(totalEnergy)) throw std::invalid_argument("spectral energy exceeds float range");
    if (totalEnergy<=1e-20) return {{"valid",false},{"reason","constant or numerically negligible plane"}};
    cv::Mat lh(16,16,CV_32F),hl(16,16,CV_32F),hh(16,16,CV_32F);
    for (int y=0;y<16;++y) for (int x=0;x<16;++x) {
        const float a=tile.at<float>(2*y,2*x),b=tile.at<float>(2*y,2*x+1);
        const float c=tile.at<float>(2*y+1,2*x),d=tile.at<float>(2*y+1,2*x+1);
        lh.at<float>(y,x)=(a+b-c-d)*.5f;
        hl.at<float>(y,x)=(a-b+c-d)*.5f;
        hh.at<float>(y,x)=(a-b-c+d)*.5f;
    }
    const double ehh=energy(hh),floor=totalEnergy*1e-12;
    cv::Mat g1,g4;
    cv::GaussianBlur(tile,g1,cv::Size(0,0),1.,1.,cv::BORDER_REFLECT);
    cv::GaussianBlur(tile,g4,cv::Size(0,0),4.,4.,cv::BORDER_REFLECT);
    cv::Mat window,fft;
    cv::createHanningWindow(window,tile.size(),CV_32F);
    cv::dft(centered.mul(window),fft,cv::DFT_COMPLEX_OUTPUT);
    std::vector<double> powers;
    for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
        const int fx=std::min(x,32-x),fy=std::min(y,32-y);
        if (fx*fx+fy*fy<=4) continue; // omit DC and very broad trends
        const cv::Vec2f z=fft.at<cv::Vec2f>(y,x);
        powers.push_back(double(z[0])*z[0]+double(z[1])*z[1]);
    }
    double sum=0.;
    for (double p:powers) sum+=p;
    if (sum<=0.) return {{"valid",false},{"reason","no non-DC spectral energy"}};
    const double avg=sum/powers.size();
    double logSum=0.;
    for (double p:powers) logSum+=std::log(std::max(p,avg*1e-12));
    std::sort(powers.begin(),powers.end(),std::greater<double>());
    double top=0.; for (int i=0;i<8;++i) top+=powers[size_t(i)];
    return {{"valid",true},{"features",QJsonObject{
        {"log_lh_hh",std::log((energy(lh)+floor)/(ehh+floor))},
        {"log_hl_hh",std::log((energy(hl)+floor)/(ehh+floor))},
        {"spectral_flatness",std::exp(logSum/powers.size())/avg},
        {"top8_power_fraction",top/sum},
        {"hh_corr_x",correlation(hh,1,0)},{"hh_corr_y",correlation(hh,0,1)},
        {"log_fine_hh",std::log((energy(tile-g1)+floor)/(ehh+floor))},
        {"log_mid_hh",std::log((energy(g1-g4)+floor)/(ehh+floor))}}}};
}

QJsonArray controls()
{
    QJsonArray records;
    for (const QString &scene : {QStringLiteral("flat"),QStringLiteral("fine-texture"),QStringLiteral("coarse-texture"),
        QStringLiteral("checker"),QStringLiteral("edge"),QStringLiteral("point"),QStringLiteral("star"),QStringLiteral("ramp")})
        for (int seed : {733,947}) for (int noiseType=0;noiseType<3;++noiseType) {
            cv::Mat clean(32,32,CV_32F);
            for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
                double v=.25;
                if (scene=="fine-texture") v+=.07*std::sin(x*.9)*std::cos(y*.7);
                if (scene=="coarse-texture") v+=.07*std::sin(x*.3)*std::cos(y*.25);
                if (scene=="checker") v+=((x+y)%2?.03:-.03);
                if (scene=="edge") v=x>16?.7:.15;
                if (scene=="point" && x==16 && y==16) v=.8;
                if (scene=="star") v+=.55*std::exp(-((x-16.)*(x-16.)+(y-16.)*(y-16.))/(2.*.7*.7));
                if (scene=="ramp") v=.02+.9*x/31.;
                clean.at<float>(y,x)=float(v);
            }
            cv::Mat observed=clean.clone();
            if (noiseType) {
                cv::Mat noise(32,32,CV_32F);cv::RNG rng(seed);rng.fill(noise,cv::RNG::NORMAL,0.,.01);
                if (noiseType==2) {
                    cv::GaussianBlur(noise,noise,cv::Size(0,0),.7,.7,cv::BORDER_REFLECT);
                    cv::Scalar mean,sd;cv::meanStdDev(noise,mean,sd);
                    noise=(noise-mean[0])*(.01/std::max(sd[0],1e-12));
                }
                observed+=noise;
            }
            records.append(QJsonObject{{"scene",scene},{"seed",seed},{"noise_type",noiseType==0?"clean":noiseType==1?"white-gaussian":"blurred-gaussian"},
                {"sigma",noiseType?.01:0.},{"plane_scope","single scalar plane, not an RGB/RAW simulation"},
                {"signature",signature(observed)}});
        }
    return records;
}

QJsonArray injectedControls(const cv::Mat &noise)
{
    // Validate first, including shape/finite values. No distribution is invented.
    signature(noise);
    cv::Scalar mean,sd;cv::meanStdDev(noise,mean,sd);
    QJsonArray records;
    if (sd[0]<=1e-10) return records;
    for (const QString &scene : {QStringLiteral("flat"),QStringLiteral("fine-texture"),QStringLiteral("coarse-texture"),
        QStringLiteral("checker"),QStringLiteral("edge"),QStringLiteral("point"),QStringLiteral("star"),QStringLiteral("ramp")}) {
        cv::Mat shape(32,32,CV_32F,cv::Scalar(0));
        for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
            double v=0.;
            if (scene=="fine-texture") v=std::sin(x*.9)*std::cos(y*.7);
            if (scene=="coarse-texture") v=std::sin(x*.3)*std::cos(y*.25);
            if (scene=="checker") v=(x+y)%2?1.:-1.;
            if (scene=="edge") v=x>16?1.:0.;
            if (scene=="point" && x==16 && y==16) v=1.;
            if (scene=="star") v=std::exp(-((x-16.)*(x-16.)+(y-16.)*(y-16.))/(2.*.7*.7));
            if (scene=="ramp") v=2.*x/31.-1.;
            shape.at<float>(y,x)=float(v);
        }
        const double peak=cv::norm(shape,cv::NORM_INF);
        for (double amplitude : {1.,3.,10.}) {
            if (scene=="flat" && amplitude!=1.) continue;
            const cv::Mat signal=shape*(amplitude*sd[0]/std::max(peak,1e-12));
            records.append(QJsonObject{{"scene",scene},{"peak_to_noise_sd",scene=="flat"?0.:amplitude},
                {"noise_sd",sd[0]},{"noise_mean",mean[0]},
                {"signature",signature(noise+signal)}});
        }
    }
    return records;
}
}
