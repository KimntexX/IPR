#include "CameraDenoise.h"
#include "CameraProfileEngine.h"
#include "RawDecoder.h"
#include "Pixels.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
// One level of the B3 spline with holes, step pixels between its taps, across then down,
// the image reflected without repeating its edge.
ipr::Image atrous(const ipr::Image &in,int step)
{
    static constexpr std::array<float,5> weights{1.f/16,4.f/16,6.f/16,4.f/16,1.f/16};
    const int width=in.width(),height=in.height();
    ipr::Image across=ipr::Image::create(in.size(),ipr::Depth::F32,3),out=ipr::Image::create(in.size(),ipr::Depth::F32,3);
    for(int y=0;y<height;++y) {
        const ipr::Vec3f *row=in.row<ipr::Vec3f>(y);ipr::Vec3f *target=across.mutableRow<ipr::Vec3f>(y);
        for(int x=0;x<width;++x) {
            ipr::Vec3f sum(0,0,0);
            for(int k=0;k<5;++k)sum+=row[reflected(x+(k-2)*step,width)]*weights[size_t(k)];
            target[x]=sum;
        }
    }
    for(int y=0;y<height;++y) {
        ipr::Vec3f *target=out.mutableRow<ipr::Vec3f>(y);
        for(int x=0;x<width;++x) {
            ipr::Vec3f sum(0,0,0);
            for(int k=0;k<5;++k)sum+=across.row<ipr::Vec3f>(reflected(y+(k-2)*step,height))[x]*weights[size_t(k)];
            target[x]=sum;
        }
    }
    return out;
}
QJsonArray signalSurvey(const ipr::Image &camera,const std::vector<CameraDenoise::NoiseModelSummary> &models)
{
    // Independent coefficient survey, not a second production denoiser.
    constexpr int width=64,halo=65;
    require(camera.width()>=width&&camera.height()>=width,"Survey needs 64px images");
    const auto at=[](const ipr::Image &image,int y,int x){return image.row<ipr::Vec3f>(y)[x];};
    QJsonArray records;
    for(int gy=0;gy<9;++gy)for(int gx=0;gx<9;++gx) {
        const int ox=(camera.width()-width)*gx/8,oy=(camera.height()-width)*gy/8;
        ipr::Image current=ipr::Image::create({width+2*halo,width+2*halo},ipr::Depth::F32,3);
        std::vector<float> intensities;
        std::array<std::vector<float>,4> quarters;
        for(int y=0;y<current.height();++y)for(int x=0;x<current.width();++x) {
            auto rgb=at(camera,reflected(oy+y-halo,camera.height()),reflected(ox+x-halo,camera.width()));
            const ipr::Vec3f v(.25f*rgb[0]+.5f*rgb[1]+.25f*rgb[2],.5f*(rgb[0]-rgb[2]),rgb[1]-.5f*(rgb[0]+rgb[2]));
            current.mutableRow<ipr::Vec3f>(y)[x]=v;
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
            ipr::Image base=atrous(current,step);
            const ipr::Image detail=ipr::mapped(current,base,[](float a,float b){return a-b;});
            if(level==0)for(int y=halo;y<halo+width;++y)for(int x=halo;x<halo+width;++x)
                guide.push_back(at(base,y,x)[0]);
            QJsonArray sigma,correlationX,correlationY;bool rejected=false;
            for(int c=0;c<3;++c) {
                std::vector<float> values;
                for(int y=halo;y<halo+width;++y)for(int x=halo;x<halo+width;++x)values.push_back(at(detail,y,x)[c]);
                const float center=median(values);
                for(auto &v:values)v=std::abs(v-center);
                const float s=median(values)/.67448975f;sigma.append(s);
                const auto corr=[&](bool horizontal) {
                    double ab=0,aa=0,bb=0;const int lag=2*step;
                    for(int y=0;y<width-(horizontal?0:lag);++y)for(int x=0;x<width-(horizontal?lag:0);++x) {
                        const double a=at(detail,halo+y,halo+x)[c]-center;
                        const double b=at(detail,halo+y+(horizontal?0:lag),halo+x+(horizontal?lag:0))[c]-center;
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
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);QCommandLineParser p;p.addHelpOption();
    for(const auto *key:{"input","manifest","profile-root","label"})p.addOption(QCommandLineOption(key,key,"value"));
    p.process(app);
    try {
        for(const auto *key:{"input","manifest","profile-root","label"})require(p.isSet(key),"Missing argument");
        RawDecoder::Options options;options.automaticWhiteBalance=false;options.signedDevelopment=true;
        options.retainPreLensSignal=true;options.preferCameraNeutral=true;
        options.automaticLcpSelection=false;options.lensCorrectionEnabled=false;options.chromaticAberrationEnabled=false;
        options.useNormalizationEnvironment=false;options.normalizationManifest=p.value("manifest");
        options.profileDirectories={p.value("profile-root")};
        ImageMetadata meta;QString error;
        auto loaded=RawDecoder::decode(p.value("input"),options,&meta,&error);
        if(loaded.isNull()||!loaded.dcpContext)throw std::runtime_error(error.toStdString());
        auto full=CameraProfileEngine::sharedCopy(*loaded.dcpContext);loaded={};
        CameraDenoise::Context context;
        // The source is already white-balanced; never compensate these axes.
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
        auto filtered=full.cameraRgb.clone();
        require(CameraDenoise::apply(filtered,100,context),"Model observation failed");
        filtered={};
        const QJsonObject result{{"created_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"label",p.value("label")},{"source",p.value("input")},{"source_sha256",hash(p.value("input"))},
            {"manifest_sha256",hash(p.value("manifest"))},{"profile_sha256",hash(full.profilePath)},
            {"engine_base_revision",IPR_ENGINE_REVISION},{"camera_axes","already_white_balanced"},
            {"production_models",modelJson},{"patches",signalSurvey(full.cameraRgb,models)}};
        std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
