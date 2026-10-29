#include "CameraDenoise.h"
#include "CameraProfileEngine.h"
#include "RawDecoder.h"
#include "WorkingColorSpace.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
QString hash(const QString &path)
{
    QFile f(path); require(f.open(QIODevice::ReadOnly), "Cannot hash source");
    QCryptographicHash h(QCryptographicHash::Sha256); require(h.addData(&f), "Hash read failed");
    return QString::fromLatin1(h.result().toHex());
}
float median(std::vector<float> values)
{
    require(!values.empty(),"Empty median");
    const auto mid=values.begin()+std::ptrdiff_t(values.size()/2);
    std::nth_element(values.begin(),mid,values.end());
    return values.size()%2 ? *mid : .5f*(*mid+*std::max_element(values.begin(),mid));
}
int reflected(int x,int size)
{
    if(size==1)return 0;
    while(x<0||x>=size)x=x<0?-x:2*size-2-x;
    return x;
}
QJsonArray signalSurvey(const cv::Mat &camera,const std::vector<CameraDenoise::NoiseModelSummary> &models)
{
    // Independent coefficient survey, not a second production denoiser.
    constexpr int width=64,halo=65;
    require(camera.cols>=width&&camera.rows>=width,"Survey needs 64px images");
    QJsonArray records;
    for(int gy=0;gy<9;++gy)for(int gx=0;gx<9;++gx) {
        const int ox=(camera.cols-width)*gx/8,oy=(camera.rows-width)*gy/8;
        cv::Mat current(width+2*halo,width+2*halo,CV_32FC3);
        std::vector<float> intensities;
        std::array<std::vector<float>,4> quarters;
        for(int y=0;y<current.rows;++y)for(int x=0;x<current.cols;++x) {
            auto rgb=camera.at<cv::Vec3f>(reflected(oy+y-halo,camera.rows),reflected(ox+x-halo,camera.cols));
            const cv::Vec3f v(.25f*rgb[0]+.5f*rgb[1]+.25f*rgb[2],.5f*(rgb[0]-rgb[2]),rgb[1]-.5f*(rgb[0]+rgb[2]));
            current.at<cv::Vec3f>(y,x)=v;
            if(x>=halo&&x<halo+width&&y>=halo&&y<halo+width) {
                intensities.push_back(v[0]);
                quarters[size_t((x-halo>=width/2)+2*(y-halo>=width/2))].push_back(v[0]);
            }
        }
        float low=median(quarters[0]),high=low;
        for(const auto &q:quarters){const float m=median(q);low=std::min(low,m);high=std::max(high,m);}
        QJsonArray levels; std::vector<float> guide;
        for(int level=0;level<5;++level) {
            const int step=1<<level;
            cv::Mat kernel=cv::Mat::zeros(1,4*step+1,CV_32F);
            for(int k=0;k<5;++k)kernel.at<float>(0,k*step)=std::array<float,5>{1.f/16,4.f/16,6.f/16,4.f/16,1.f/16}[size_t(k)];
            cv::Mat base;cv::sepFilter2D(current,base,-1,kernel,kernel,{-1,-1},0,cv::BORDER_REFLECT_101);
            const cv::Mat detail=current-base;
            if(level==0)for(int y=halo;y<halo+width;++y)for(int x=halo;x<halo+width;++x)
                guide.push_back(base.at<cv::Vec3f>(y,x)[0]);
            QJsonArray sigma,correlationX,correlationY;bool rejected=false;
            for(int c=0;c<3;++c) {
                std::vector<float> values;
                for(int y=halo;y<halo+width;++y)for(int x=halo;x<halo+width;++x)values.push_back(detail.at<cv::Vec3f>(y,x)[c]);
                const float center=median(values);
                for(auto &v:values)v=std::abs(v-center);
                const float s=median(values)/.67448975f;sigma.append(s);
                const auto corr=[&](bool horizontal) {
                    double ab=0,aa=0,bb=0;const int lag=2*step;
                    for(int y=0;y<width-(horizontal?0:lag);++y)for(int x=0;x<width-(horizontal?lag:0);++x) {
                        const double a=detail.at<cv::Vec3f>(halo+y,halo+x)[c]-center;
                        const double b=detail.at<cv::Vec3f>(halo+y+(horizontal?0:lag),halo+x+(horizontal?lag:0))[c]-center;
                        ab+=a*b;aa+=a*a;bb+=b*b;
                    }
                    return aa>0&&bb>0?std::abs(ab/std::sqrt(aa*bb)):0.;
                };
                const double cx=corr(true),cy=corr(false);correlationX.append(cx);correlationY.append(cy);
                rejected=rejected||(s>0&&cx>.9&&cy>.9);
            }
            levels.append(QJsonObject{{"sigma_I_Co_Cg",sigma},{"correlation_x",correlationX},
                {"correlation_y",correlationY},{"coherent_rejected",rejected}});
            current=std::move(base);
        }
        QJsonArray coverage;
        for(const auto &model:models) {
            int below=0,above=0;
            if(model.supported)for(float v:guide){below+=v<model.low;above+=v>model.high;}
            coverage.append(QJsonObject{{"level",model.level},{"supported",model.supported},
                {"below",below},{"above",above},{"pixels",int(guide.size())}});
        }
        records.append(QJsonObject{{"roi",QJsonArray{ox,oy,width,width}},
            {"training_original_5x5",gx%2==0&&gy%2==0},{"signal_I_median",median(intensities)},
            {"quadrant_signal_drift",double(high-low)},{"levels",levels},{"model_coverage",coverage}});
    }
    return records;
}
cv::Mat render(CameraProfileEngine::DcpWorkingContext context, const cv::Mat &camera)
{
    context.cameraRgb = camera;
    cv::Mat out; QString error;
    if (!CameraProfileEngine::renderFromCameraSignal(context, int(std::lround(context.sourceKelvin)), 0, 0, out, &error))
        throw std::runtime_error(error.toStdString());
    require(out.type() == CV_32FC3 && cv::checkRange(out), "Invalid profile output");
    return out;
}
cv::Mat intensity(const cv::Mat &camera)
{
    cv::Mat out(camera.size(), CV_32F);
    for (int y=0; y<out.rows; ++y) for (int x=0; x<out.cols; ++x) {
        const auto v=camera.at<cv::Vec3f>(y,x);
        out.at<float>(y,x)=.25f*v[0]+.5f*v[1]+.25f*v[2];
    }
    return out;
}
cv::Mat luminance(const cv::Mat &working)
{
    cv::Mat out(working.size(),CV_32F);
    for(int y=0;y<out.rows;++y)for(int x=0;x<out.cols;++x)
        out.at<float>(y,x)=WorkingColor::luminance(working.at<cv::Vec3f>(y,x));
    return out;
}
double stddev(const cv::Mat &m)
{
    cv::Scalar mean,sigma; cv::meanStdDev(m,mean,sigma); return sigma[0];
}
double rms(const cv::Mat &m) { return cv::norm(m,cv::NORM_L2)/std::sqrt(double(m.total())); }
QJsonObject measures(const cv::Mat &working,const cv::Rect &roi)
{
    cv::Mat display(working.size(),CV_32FC3);
    for(int y=0;y<working.rows;++y)for(int x=0;x<working.cols;++x) {
        auto v=WorkingColor::toSrgb()*cv::Vec3d(working.at<cv::Vec3f>(y,x));
        auto &out=display.at<cv::Vec3f>(y,x);
        for(int c=0;c<3;++c) out[c]=float(std::lround(255*std::clamp(WorkingColor::srgbEncode(v[c]),0.,1.)));
    }
    cv::Mat y(display.size(),CV_32F),co(y.size(),CV_32F),cg(y.size(),CV_32F);
    for(int row=0;row<y.rows;++row)for(int x=0;x<y.cols;++x) {
        const auto v=display.at<cv::Vec3f>(row,x);
        y.at<float>(row,x)=.2126f*v[0]+.7152f*v[1]+.0722f*v[2];
        co.at<float>(row,x)=.5f*(v[0]-v[2]); cg.at<float>(row,x)=v[1]-.5f*(v[0]+v[2]);
    }
    cv::Mat by,bco,bcg; cv::GaussianBlur(y,by,{},2); cv::GaussianBlur(co,bco,{},2); cv::GaussianBlur(cg,bcg,{},2);
    const auto yy=luminance(working);cv::Mat sy;cv::GaussianBlur(yy,sy,{},2);
    const cv::Mat hpY=y-by,hpCo=co-bco,hpCg=cg-bcg,hpLinear=yy-sy;
    return {{"display_y_mean",cv::mean(y(roi))[0]}, {"display_y_highpass_sigma2_std",stddev(hpY(roi))},
            {"display_chroma_highpass_sigma2_rms_std",std::hypot(stddev(hpCo(roi)),stddev(hpCg(roi)))},
            {"display_chroma_lowpass_sigma2_rms_std",std::hypot(stddev(bco(roi)),stddev(bcg(roi)))},
            {"linear_rec2020_y_mean",cv::mean(yy(roi))[0]}, {"linear_rec2020_y_highpass_sigma2_std",stddev(hpLinear(roi))}};
}
// STUDY ONLY: finite differences of the actual profile, not a production DCP hook.
// Project chroma correction onto the tangent plane of rendered linear Y, along
// the balanced opponent-intensity direction (1,1,1), not a characterized scene gray.
// Clipped/ill-conditioned derivatives abstain.
cv::Mat projected(const CameraProfileEngine::DcpWorkingContext &context,const cv::Mat &source,
                  const cv::Mat &filtered,const cv::Vec3f &gains,QJsonObject &diagnostic)
{
    constexpr float epsilon=.0002f;
    cv::Mat gradient(source.size(),CV_32FC3);
    for(int c=0;c<3;++c) {
        auto plus=source.clone(),minus=source.clone();
        for(int y=0;y<source.rows;++y)for(int x=0;x<source.cols;++x) {
            plus.at<cv::Vec3f>(y,x)[c]+=epsilon*gains[c];
            minus.at<cv::Vec3f>(y,x)[c]-=epsilon*gains[c];
        }
        const auto yp=luminance(render(context,plus)),ym=luminance(render(context,minus));
        for(int y=0;y<source.rows;++y)for(int x=0;x<source.cols;++x)
            gradient.at<cv::Vec3f>(y,x)[c]=(yp.at<float>(y,x)-ym.at<float>(y,x))/(2*epsilon*gains[c]);
    }
    auto out=filtered.clone(); double abstained=0,limited=0;
    for(int y=0;y<source.rows;++y)for(int x=0;x<source.cols;++x) {
        const auto g=gradient.at<cv::Vec3f>(y,x),d=filtered.at<cv::Vec3f>(y,x)-source.at<cv::Vec3f>(y,x);
        const double denominator=g.dot(gains);
        if(!std::isfinite(denominator)||denominator<=1e-6) {out.at<cv::Vec3f>(y,x)=source.at<cv::Vec3f>(y,x);++abstained;continue;}
        const double shift=g.dot(d)/denominator;
        const double bound=std::max({std::abs(d[0]/gains[0]),std::abs(d[1]/gains[1]),std::abs(d[2]/gains[2])});
        const double bounded=std::clamp(shift,-bound,bound); limited+=bounded!=shift;
        out.at<cv::Vec3f>(y,x)-=gains*float(bounded);
    }
    diagnostic={{"abstained_fraction",abstained/source.total()},{"limited_fraction",limited/source.total()},
                {"finite_difference_epsilon_balanced_camera",epsilon}};
    return out;
}
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);QCommandLineParser p;p.addHelpOption();
    for(const auto *key:{"input","manifest","profile-root","label"})p.addOption(QCommandLineOption(key,key,"value"));
    p.addOption(QCommandLineOption("signal-survey","Survey balanced coefficient MAD and actual full-frame models; no image export"));
    p.process(app);
    try {
        for(const auto *key:{"input","manifest","profile-root","label"})require(p.isSet(key),"Missing argument");
        cv::setNumThreads(1);
        RawDecoder::Options options;options.automaticWhiteBalance=false;options.signedDevelopment=true;
        options.retainPreLensSignal=true;options.preferCameraNeutral=true;
        options.automaticLcpSelection=false;options.lensCorrectionEnabled=false;options.chromaticAberrationEnabled=false;
        options.useNormalizationEnvironment=false;options.normalizationManifest=p.value("manifest");
        options.profileDirectories={p.value("profile-root")};
        ImageMetadata meta;QString error;
        auto loaded=RawDecoder::decode(p.value("input"),options,&meta,&error);
        if(loaded.isNull()||!loaded.dcpContext)throw std::runtime_error(error.toStdString());
        auto full=*loaded.dcpContext;loaded={};
        CameraDenoise::Context context;
        // V1: source is already white-balanced; never compensate these axes.
        const cv::Vec3f gains(1,1,1); // Neutral direction for the projection study only.
        if(p.isSet("signal-survey")) {
            QJsonArray modelJson; std::vector<CameraDenoise::NoiseModelSummary> models;
            context.noiseModelObserver=[&](const CameraDenoise::NoiseModelSummary &m) {
                models.push_back(m); QJsonArray sigma,nodes;
                for(int c=0;c<3;++c)sigma.append(m.globalSigma[c]);
                for(const auto &node:m.nodes)nodes.append(QJsonObject{{"signal",node.first},
                    {"sigma",QJsonArray{node.second[0],node.second[1],node.second[2]}}});
                modelJson.append(QJsonObject{{"level",m.level},{"candidates",m.candidates},
                    {"flat_candidates",m.flatCandidates},{"supported",m.supported},
                    {"low",m.low},{"high",m.high},{"global_signal",m.globalSignal},
                    {"global_sigma",sigma},{"nodes",nodes}});
            };
            auto filtered=full.cameraRgb;
            require(CameraDenoise::apply(filtered,{100,100},context),"Model observation failed");
            filtered.release();
            const QJsonObject result{{"created_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {"label",p.value("label")},{"source",p.value("input")},{"source_sha256",hash(p.value("input"))},
                {"manifest_sha256",hash(p.value("manifest"))},{"profile_sha256",hash(full.profilePath)},
                {"engine_base_revision",IPR_ENGINE_REVISION},{"camera_axes","already_white_balanced"},
                {"production_models",modelJson},{"patches",signalSurvey(full.cameraRgb,models)}};
            std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
        }
        QJsonArray patches;
        for(double position:{.25,.5,.75}) {
            const int side=std::min({768,full.cameraRgb.cols,full.cameraRgb.rows});
            require(side>=420,"Image too small for the declared witness geometry");
            const cv::Rect area(int((full.cameraRgb.cols-side)*position),int((full.cameraRgb.rows-side)*position),side,side);
            const int margin=(side-420)/2; const cv::Rect roi(margin,margin,std::min(420,side),std::min(420,side));
            auto source=full.cameraRgb(area).clone();auto filtered=source;
            require(CameraDenoise::apply(filtered,{0,50},context),"Filter failed");
            const auto baseline=render(full,source),rendered=render(full,filtered);
            QJsonObject projection;const auto candidate=projected(full,source,filtered,gains,projection);
            const auto constrained=render(full,candidate);
            const auto inputI=intensity(source),outputI=intensity(filtered);
            const auto baselineY=luminance(baseline),filteredY=luminance(rendered),constrainedY=luminance(constrained);
            const cv::Mat deltaI=outputI-inputI,deltaY=filteredY-baselineY,deltaProjected=constrainedY-baselineY;
            patches.append(QJsonObject{{"camera_roi",QJsonArray{area.x,area.y,area.width,area.height}},
                {"measurement_roi_within_camera_roi",QJsonArray{roi.x,roi.y,roi.width,roi.height}},
                {"B0",measures(baseline,roi)},{"chroma50",measures(rendered,roi)},{"projected_chroma50",measures(constrained,roi)},
                {"camera_intensity_delta_rms",rms(deltaI(roi))},{"rendered_linear_y_delta_rms",rms(deltaY(roi))},
                {"projected_linear_y_delta_rms",rms(deltaProjected(roi))},{"projection",projection}});
        }
        const QJsonObject result{{"created_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"engine_revision",IPR_ENGINE_REVISION},{"label",p.value("label")},{"source",p.value("input")},
            {"source_sha256",hash(p.value("input"))},{"manifest_sha256",hash(p.value("manifest"))},
            {"profile",full.profilePath},{"profile_sha256",hash(full.profilePath)},
            {"camera_denoise_source_sha256",hash(QStringLiteral(IPR_SOURCE_ROOT_PATH "/src/raw/CameraDenoise.cpp"))},
            {"patches",patches}};
        std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
