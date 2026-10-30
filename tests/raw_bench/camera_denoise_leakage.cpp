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
                {"below",below},{"above",above},{"pixels",int(guide.size())},
                {"global_fallback_pixels",model.globalFallbackAvailable?
                    (model.supported?below+above:int(guide.size())):0}});
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
QJsonObject cameraMoments(const cv::Mat &camera)
{
    require(!camera.empty()&&camera.type()==CV_32FC3,"Invalid camera moments input");
    const auto mean=cv::mean(camera);
    cv::Vec3d negative{},positivePart{};
    for(int y=0;y<camera.rows;++y)for(int x=0;x<camera.cols;++x) {
        const auto v=camera.at<cv::Vec3f>(y,x);
        for(int c=0;c<3;++c) {
            negative[c]+=v[c]<0;
            positivePart[c]+=std::max(0.,double(v[c]));
        }
    }
    QJsonArray means,negatives,positive;
    for(int c=0;c<3;++c) {
        means.append(mean[c]);negatives.append(negative[c]/double(camera.total()));
        positive.append(positivePart[c]/double(camera.total()));
    }
    return {{"mean_rgb",means},{"negative_fraction_rgb",negatives},
            {"positive_part_mean_rgb",positive}};
}
cv::Mat selectPixels(const cv::Mat &camera,const cv::Mat &mask)
{
    require(mask.size()==camera.size()&&mask.type()==CV_8UC1,"Mask shape mismatch");
    const int count=cv::countNonZero(mask);require(count>0,"Empty dark mask");
    cv::Mat selected(1,count,CV_32FC3);int at=0;
    for(int y=0;y<mask.rows;++y)for(int x=0;x<mask.cols;++x)
        if(mask.at<unsigned char>(y,x))selected.at<cv::Vec3f>(0,at++)=camera.at<cv::Vec3f>(y,x);
    return selected;
}
QJsonObject renderedMoments(const cv::Mat &working)
{
    double linear=0,signedEncoded=0,clippedEncoded=0,roundedEncoded=0;
    cv::Vec3d below{},above{};const cv::Vec3d weights(.2126,.7152,.0722);
    for(int y=0;y<working.rows;++y)for(int x=0;x<working.cols;++x) {
        const auto value=working.at<cv::Vec3f>(y,x);
        linear+=WorkingColor::luminance(value);
        const auto srgb=WorkingColor::toSrgb()*cv::Vec3d(value);
        for(int c=0;c<3;++c) {
            const double encoded=WorkingColor::srgbEncode(srgb[c])*255;
            const double clipped=std::clamp(encoded,0.,255.);
            signedEncoded+=weights[c]*encoded;clippedEncoded+=weights[c]*clipped;
            roundedEncoded+=weights[c]*std::lround(clipped);
            below[c]+=srgb[c]<0;above[c]+=srgb[c]>1;
        }
    }
    const double n=double(working.total());
    return {{"linear_rec2020_y_mean",linear/n},{"signed_encoded_y_mean_255",signedEncoded/n},
        {"clipped_encoded_y_mean_255",clippedEncoded/n},{"rounded_encoded_y_mean_255",roundedEncoded/n},
        {"srgb_below_zero_fraction",QJsonArray{below[0]/n,below[1]/n,below[2]/n}},
        {"srgb_above_one_fraction",QJsonArray{above[0]/n,above[1]/n,above[2]/n}}};
}
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
    p.addOption(QCommandLineOption("mean-audit","Audit full-frame 100/100 filtering, local/global camera means and rendered patch means"));
    p.addOption(QCommandLineOption("dark-mask","Bench-only full camera-size uint8 mask, bytes 0/1","path"));
    p.addOption(QCommandLineOption("cell-rects","Bench-only JSON array of camera rectangles","path"));
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
        require(!(p.isSet("signal-survey")&&p.isSet("mean-audit")),"Choose a single study mode");
        if(p.isSet("mean-audit")) {
            auto filtered=full.cameraRgb;
            require(CameraDenoise::apply(filtered,{100,100},context),"Full-frame mean audit failed");
            if(p.isSet("dark-mask")) {
                QFile file(p.value("dark-mask"));require(file.open(QIODevice::ReadOnly),"Cannot open dark mask");
                auto bytes=file.readAll();require(std::size_t(bytes.size())==full.cameraRgb.total(),"Invalid mask byte count");
                require(std::all_of(bytes.begin(),bytes.end(),[](char v){return v==0||v==1;}),"Mask must contain only 0/1");
                cv::Mat mask(full.cameraRgb.size(),CV_8UC1,bytes.data());
                const auto source=selectPixels(full.cameraRgb,mask),candidate=selectPixels(filtered,mask);
                const cv::Scalar shift=cv::mean(source)-cv::mean(candidate);
                auto matched=candidate.clone();
                for(int x=0;x<matched.cols;++x)for(int c=0;c<3;++c)matched.at<cv::Vec3f>(0,x)[c]+=float(shift[c]);
                // DCP rendering is pointwise here (no LCP/spatial operation).
                // Check that changing row layout does not change a real patch.
                const auto check=full.cameraRgb(cv::Rect(0,0,16,16)).clone();
                const auto direct=render(full,check),packed=render(full,check.reshape(3,1));
                const double layoutError=cv::norm(direct.reshape(3,1),packed,cv::NORM_INF);
                require(layoutError<=1e-6,"DCP packing changes rendering");
                QJsonArray cells;
                if(p.isSet("cell-rects")) {
                    QFile rectangles(p.value("cell-rects"));require(rectangles.open(QIODevice::ReadOnly),"Cannot open cell rectangles");
                    const auto document=QJsonDocument::fromJson(rectangles.readAll());
                    require(document.isArray()&&document.array().size()<=4096,"Invalid cell array");
                    for(const auto &item:document.array()) {
                        require(item.isArray()&&item.toArray().size()==4,"Invalid cell rectangle");
                        const auto a=item.toArray();const int x=a[0].toInt(-1),y=a[1].toInt(-1),w=a[2].toInt(-1),h=a[3].toInt(-1);
                        require(x>=0&&y>=0&&w>0&&h>0&&x<full.cameraRgb.cols&&y<full.cameraRgb.rows&&
                            w<=full.cameraRgb.cols-x&&h<=full.cameraRgb.rows-y,"Cell outside camera image");
                        const cv::Rect cell(x,y,w,h);
                        cells.append(QJsonObject{{"camera_roi",a},
                            {"B0_camera",cameraMoments(full.cameraRgb(cell))},
                            {"B100_camera",cameraMoments(filtered(cell))},
                            {"B0_rendered",renderedMoments(render(full,full.cameraRgb(cell)))},
                            {"B100_rendered",renderedMoments(render(full,filtered(cell)))}});
                    }
                }
                const QJsonObject result{{"created_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                    {"label",p.value("label")},{"source",p.value("input")},{"source_sha256",hash(p.value("input"))},
                    {"manifest_sha256",hash(p.value("manifest"))},{"profile_sha256",hash(full.profilePath)},
                    {"engine_base_revision",IPR_ENGINE_REVISION},{"mask_sha256",hash(p.value("dark-mask"))},
                    {"mask_pixels",source.cols},{"camera_size",QJsonArray{full.cameraRgb.cols,full.cameraRgb.rows}},
                    {"layout_check_max_abs",layoutError},
                    {"B0_camera_global",cameraMoments(full.cameraRgb)},
                    {"B100_camera_global",cameraMoments(filtered)},
                    {"B0_camera_dark",cameraMoments(source)},{"B100_camera_dark",cameraMoments(candidate)},
                    {"B100_mean_matched_camera_dark",cameraMoments(matched)},
                    {"B0_rendered_dark",renderedMoments(render(full,source))},
                    {"B100_rendered_dark",renderedMoments(render(full,candidate))},
                    {"B100_mean_matched_rendered_dark",renderedMoments(render(full,matched))},
                    {"mean_matching_shift_rgb",QJsonArray{shift[0],shift[1],shift[2]}},
                    {"cells",cells},
                    {"limits","Full-frame filter; fixed external dark mask; pointwise DCP; mean-matching is a diagnostic counterfactual, not a proposed correction; unchanged DCP; optics off"}};
                std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
            }
            QJsonArray records;
            std::vector<cv::Rect> areas;
            for(double position:{.25,.5,.75}) {
                const int side=std::min({420,full.cameraRgb.cols,full.cameraRgb.rows});
                areas.emplace_back(int((full.cameraRgb.cols-side)*position),
                    int((full.cameraRgb.rows-side)*position),side,side);
            }
            // Declared independent witness, not Claude's frozen full dark mask.
            if(full.cameraRgb.cols>=3360&&full.cameraRgb.rows>=3360&&
               p.value("input").contains("7Y6A7599"))areas.emplace_back(2940,2940,420,420);
            for(const auto &area:areas) {
                const auto sourcePatch=full.cameraRgb(area),filteredPatch=filtered(area);
                const auto baseline=render(full,sourcePatch),candidate=render(full,filteredPatch);
                const cv::Rect local(0,0,area.width,area.height);
                records.append(QJsonObject{{"camera_roi",QJsonArray{area.x,area.y,area.width,area.height}},
                    {"B0_camera",cameraMoments(sourcePatch)},{"B100_camera",cameraMoments(filteredPatch)},
                    {"B0_rendered",measures(baseline,local)},{"B100_rendered",measures(candidate,local)}});
            }
            const QJsonObject result{{"created_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {"label",p.value("label")},{"source",p.value("input")},
                {"source_sha256",hash(p.value("input"))},{"manifest_sha256",hash(p.value("manifest"))},
                {"profile_sha256",hash(full.profilePath)},{"engine_base_revision",IPR_ENGINE_REVISION},
                {"camera_size",QJsonArray{full.cameraRgb.cols,full.cameraRgb.rows}},
                {"B0_camera_global",cameraMoments(full.cameraRgb)},
                {"B100_camera_global",cameraMoments(filtered)},
                {"patches",records},{"settings",QJsonArray{100,100}},
                {"limits","Full-frame noise model; independent rectangular witnesses, not frozen dark masks; optics off; unchanged DCP; no CFA oracle"}};
            std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
        }
        if(p.isSet("signal-survey")) {
            QJsonArray modelJson; std::vector<CameraDenoise::NoiseModelSummary> models;
            context.noiseModelObserver=[&](const CameraDenoise::NoiseModelSummary &m) {
                models.push_back(m); QJsonArray sigma,nodes;
                for(int c=0;c<3;++c)sigma.append(m.globalSigma[c]);
                for(const auto &node:m.nodes)nodes.append(QJsonObject{{"signal",node.first},
                    {"sigma",QJsonArray{node.second[0],node.second[1],node.second[2]}}});
                modelJson.append(QJsonObject{{"level",m.level},{"candidates",m.candidates},
                    {"flat_candidates",m.flatCandidates},{"supported",m.supported},
                    {"dark_floor_applied",m.darkFloorApplied},
                    {"global_fallback_available",m.globalFallbackAvailable},
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
