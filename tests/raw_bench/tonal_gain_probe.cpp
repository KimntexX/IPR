#include "CameraDenoise.h"
#include "Contrast.h"
#include "ToneRecovery.h"
#include "WorkingColorSpace.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/core.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
struct Recipe {const char *name;double ev;int contrast;float shadows;};
cv::Mat transform(const cv::Mat &source,const Recipe &r)
{
    cv::Mat result=source*float(std::exp2(r.ev));
    Contrast::apply(result,r.contrast);
    if(r.shadows!=0)ToneRecovery::apply(result,{r.shadows,0,0,0},ToneRecovery::typicalStatistics());
    require(cv::checkRange(result),"Nonfinite tone output");return result;
}
double display(const cv::Vec3f &rgb)
{
    const auto s=WorkingColor::toSrgb()*cv::Vec3d(rgb);
    return .2126*std::clamp(WorkingColor::srgbEncode(s[0]),0.,1.)+
           .7152*std::clamp(WorkingColor::srgbEncode(s[1]),0.,1.)+
           .0722*std::clamp(WorkingColor::srgbEncode(s[2]),0.,1.);
}
cv::Vec3f sample(double y,const Recipe &r)
{
    cv::Mat source(1,1,CV_32FC3,cv::Scalar(y,y,y));return transform(source,r).at<cv::Vec3f>(0,0);
}
QJsonObject residualTest()
{
    cv::Mat source(128,128,CV_32FC3);cv::RNG rng(20261030);
    for(int y=0;y<128;++y)for(int x=0;x<128;++x){auto &v=source.at<cv::Vec3f>(y,x);
        for(int c=0;c<3;++c)v[c]=float(.01*(c+1)+rng.gaussian(.003));}
    auto maximum=source;require(CameraDenoise::apply(maximum,{100,100},{}),"Maximum denoise failed");
    QJsonArray results;
    for(int amount:{1,25,50,85,100}){
        CameraDenoise::Settings settings;require(CameraDenoise::settingsFromAmount(amount,settings),"Invalid master mapping");
        auto actual=source;require(CameraDenoise::apply(actual,settings,{}),"Mapped denoise failed");
        auto expected=source.clone();
        for(int y=0;y<128;++y)for(int x=0;x<128;++x){
            const auto d=maximum.at<cv::Vec3f>(y,x)-source.at<cv::Vec3f>(y,x);
            const float i=(.25f*d[0]+.5f*d[1]+.25f*d[2])*settings.luminance/100.f;
            const float co=.5f*(d[0]-d[2])*settings.chrominance/100.f;
            const float cg=(d[1]-.5f*(d[0]+d[2]))*settings.chrominance/100.f;
            expected.at<cv::Vec3f>(y,x)+=cv::Vec3f(i+co-.5f*cg,i+.5f*cg,i-co-.5f*cg);
        }
        const double error=cv::norm(expected,actual,cv::NORM_INF);
        require(error<1e-6,"Residual rescaling is not sufficiently close in this witness");
        results.append(QJsonObject{{"master",amount},{"luminance",settings.luminance},
            {"chrominance",settings.chrominance},{"max_abs_error",error}});
    }
    return {{"witness","128px Gaussian camera RGB, same active five scales, global strengths only"},
        {"cases",results},{"limits","Not bit-exact, not real image qualification, no guarantee for advanced colour=0 or spatial weights; no cache implemented"}};
}
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);cv::setNumThreads(1);
    try{
        const Recipe neutral{"neutral",0,0,0};
        const std::array<Recipe,6> recipes{{neutral,{"exposure2",2,0,0},{"exposure3",3,0,0},
            {"contrast50",0,50,0},{"shadows100",0,0,1},{"exposure2_shadows100",2,0,1}}};
        QJsonArray all;
        for(const auto &r:recipes){QJsonArray points,monteCarlo;
            for(int k=0;k<64;++k){
                const double y=std::exp2(-16.+15.9*k/63.);
                const double eps=std::max(1e-8,y*1e-3);
                const auto value=sample(y,r),plus=sample(y+eps,r),minus=sample(y-eps,r);
                const double linear=(WorkingColor::luminance(plus)-WorkingColor::luminance(minus))/(2*eps);
                const double encoded=(display(plus)-display(minus))/(2*eps);
                const double reference=(display(sample(y+eps,neutral))-display(sample(y-eps,neutral)))/(2*eps);
                points.append(QJsonObject{{"input_y",y},{"output_y",WorkingColor::luminance(value)},
                    {"brightness_ratio",WorkingColor::luminance(value)/y},{"linear_derivative",linear},
                    {"display_derivative",encoded},{"display_noise_gain_to_neutral",reference>1e-8?QJsonValue(std::abs(encoded/reference)):QJsonValue(QJsonValue::Null)}});
            }
            for(double y:{.001,.01,.1,.3}){
                const double sigma=y*.005,eps=std::max(1e-8,y*1e-3);
                cv::Mat source(1,2048,CV_32FC3);cv::RNG rng(43821);
                for(int k=0;k<1024;++k){const double n=rng.gaussian(sigma);
                    source.at<cv::Vec3f>(0,2*k)=cv::Vec3f::all(float(y+n));
                    source.at<cv::Vec3f>(0,2*k+1)=cv::Vec3f::all(float(y-n));}
                double inputSum=0,inputSquared=0;
                for(int k=0;k<2048;++k){const double v=source.at<cv::Vec3f>(0,k)[0];inputSum+=v;inputSquared+=v*v;}
                const double sampleSigma=std::sqrt(std::max(0.,(inputSquared-inputSum*inputSum/2048)/2047));
                const auto out=transform(source,r);double sum=0,squared=0;
                for(int k=0;k<2048;++k){const double v=display(out.at<cv::Vec3f>(0,k));sum+=v;squared+=v*v;}
                const double actual=std::sqrt(std::max(0.,(squared-sum*sum/2048)/2047));
                const double derivative=(display(sample(y+eps,r))-display(sample(y-eps,r)))/(2*eps);
                const double predicted=std::abs(derivative)*sigma;
                const double samplePredicted=std::abs(derivative)*sampleSigma;
                monteCarlo.append(QJsonObject{{"input_y",y},{"input_sigma",sigma},{"measured_input_sigma",sampleSigma},
                    {"sample_predicted_display_sigma",samplePredicted},
                    {"ratio_measured_to_sample_prediction",samplePredicted>1e-9?QJsonValue(actual/samplePredicted):QJsonValue(QJsonValue::Null)},
                    {"predicted_display_sigma",predicted},
                    {"measured_display_sigma",actual},{"ratio_measured_to_predicted",predicted>1e-9?QJsonValue(actual/predicted):QJsonValue(QJsonValue::Null)}});
            }
            all.append(QJsonObject{{"recipe",r.name},{"ev",r.ev},{"contrast",r.contrast},{"shadows",r.shadows},
                {"points",points},{"small_noise_checks",monteCarlo}});
        }
        const QJsonObject result{{"tonal_transfer",all},{"residual_scaling",residualTest()},
            {"scope","Neutral grey Rec2020, exposure AFTER profile (raster-like), contrast and shadows actual modules with typical statistics; NOT full RAW DCP chain or chroma/correlated-noise model"}};
        std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
