#include "CameraDenoise.h"
#include "Demosaic.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
QString hash(const QString &path){QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot hash input");QCryptographicHash h(QCryptographicHash::Sha256);require(h.addData(&f),"Cannot hash input bytes");return QString::fromLatin1(h.result().toHex());}
struct Capture {cv::Mat dn;Demosaic::Pattern pattern{};std::array<int,4> colours{};std::array<double,4> gains{};double divisor=0;QJsonObject metadata;};
Capture read(const QString &path)
{
    QFile json(path);require(json.open(QIODevice::ReadOnly),"Cannot open capture");
    const auto manifest=QJsonDocument::fromJson(json.readAll()).object();
    require(manifest.value("mode")=="signed","Expected signed capture");
    std::vector<QJsonObject> stages;
    for(const auto &v:manifest.value("stages").toArray())if(v.toObject().value("stage")=="black_subtracted_cfa")stages.push_back(v.toObject());
    require(stages.size()==3,"Expected three fixed CFA crops");
    std::sort(stages.begin(),stages.end(),[](const auto &a,const auto &b){return a.value("origin_yx").toArray()[1].toInt()<b.value("origin_yx").toArray()[1].toInt();});
    const auto stage=stages[1];const auto shape=stage.value("shape").toArray();
    require(shape==QJsonArray{256,256,1}&&stage.value("dtype")=="f32"&&stage.value("unit")=="DN"&&stage.value("stride_bytes")==1024,"Invalid CFA crop contract");
    const auto pixels=manifest.value("pixels").toObject();const auto binary=QFileInfo(path).dir().filePath(pixels.value("path").toString());
    require(hash(binary)==pixels.value("sha256").toString(),"Capture binary hash mismatch");
    QFile f(binary);require(f.open(QIODevice::ReadOnly)&&f.seek(qint64(stage.value("offset_bytes").toDouble(-1))),"Invalid capture extent");
    const auto bytes=f.read(256*256*4);require(bytes.size()==256*256*4,"Truncated capture");
    const unsigned short one=1;require(*reinterpret_cast<const unsigned char *>(&one)==1,"Little endian host required");
    Capture result;result.dn=cv::Mat(256,256,CV_32F);std::memcpy(result.dn.data,bytes.constData(),std::size_t(bytes.size()));
    require(cv::checkRange(result.dn),"Nonfinite CFA noise");
    const auto applied=manifest.value("applied").toObject();result.divisor=applied.value("divisor").toDouble();
    const auto gains=applied.value("gains").toArray();require(gains.size()==4&&result.divisor>0&&std::isfinite(result.divisor),"Invalid normalization");
    const auto origin=stage.value("origin_yx").toArray(),pattern=stage.value("context").toObject().value("cfa").toArray();
    require(origin.size()==2&&pattern.size()==2,"Missing CFA pattern");
    for(int c=0;c<4;++c){result.gains[c]=gains[c].toDouble();require(result.gains[c]>0&&std::isfinite(result.gains[c]),"Invalid gain");}
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        const int colour=pattern[(origin[0].toInt()+y)%2].toArray()[(origin[1].toInt()+x)%2].toInt(-1);
        require(colour>=0&&colour<4,"Invalid CFA colour");result.colours[std::size_t(y*2+x)]=colour;
        result.pattern[std::size_t(y)][std::size_t(x)]=colour==3?1:colour;
    }
    result.metadata=QJsonObject{{"capture_sha256",hash(path)},{"source",manifest.value("source")},
        {"applied",applied},{"stage",stage},{"metadata",manifest.value("metadata")}};
    return result;
}
cv::Mat noise(const Capture &capture)
{
    cv::Mat out(capture.dn.size(),CV_32F);
    for(int y=0;y<out.rows;++y)for(int x=0;x<out.cols;++x){const int c=capture.colours[std::size_t((y%2)*2+x%2)];out.at<float>(y,x)=float(capture.dn.at<float>(y,x)*capture.gains[c]/capture.divisor);}
    return out;
}
std::array<double,4> variances(const cv::Mat &source)
{
    std::array<double,4> out{};
    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
        cv::Mat plane(source.rows/2,source.cols/2,CV_64F);
        for(int y=0;y<plane.rows;++y)for(int x=0;x<plane.cols;++x)plane.at<double>(y,x)=source.at<float>(2*y+dy,2*x+dx);
        cv::Scalar mean,sigma;cv::meanStdDev(plane,mean,sigma);out[std::size_t(dy*2+dx)]=sigma[0]*sigma[0]*double(plane.total())/double(plane.total()-1);
    }return out;
}
// Study-only, original implementation of local scalar Wiener shrinkage.
// Four phases stay separate, no signal bound, fixed 3x3 window per phase.
cv::Mat filterCfa(const cv::Mat &source,const std::array<double,4> &variance)
{
    require(source.type()==CV_32F&&source.rows>=6&&source.cols>=6&&source.rows%2==0&&source.cols%2==0&&cv::checkRange(source),"Invalid filter input");
    cv::Mat out=source.clone();
    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
        const double power=variance[std::size_t(dy*2+dx)];require(std::isfinite(power)&&power>=0,"Invalid noise variance");if(power==0)continue;
        cv::Mat plane(source.rows/2,source.cols/2,CV_64F);
        for(int y=0;y<plane.rows;++y)for(int x=0;x<plane.cols;++x)plane.at<double>(y,x)=source.at<float>(2*y+dy,2*x+dx);
        cv::Mat mean,second;cv::boxFilter(plane,mean,-1,{3,3},{-1,-1},true,cv::BORDER_REFLECT_101);
        cv::boxFilter(plane.mul(plane),second,-1,{3,3},{-1,-1},true,cv::BORDER_REFLECT_101);
        for(int y=0;y<plane.rows;++y)for(int x=0;x<plane.cols;++x){
            const double m=mean.at<double>(y,x),v=std::max(0.,(second.at<double>(y,x)-m*m)*9./8.);
            const double gain=v>power?1.-power/v:0.;
            out.at<float>(2*y+dy,2*x+dx)=float(m+gain*(plane.at<double>(y,x)-m));
        }
    }require(cv::checkRange(out),"Nonfinite filter output");return out;
}
cv::Mat truth(const QString &pattern,double level)
{
    constexpr double pi=3.14159265358979323846;cv::Mat out(256,256,CV_32FC3);
    for(int y=0;y<256;++y)for(int x=0;x<256;++x){cv::Vec3f v{float(level),float(level),float(level)};
        if(pattern=="texture")v+=cv::Vec3f::all(float(.0015*std::sin(2*pi*x/16.)*std::cos(2*pi*y/24.)));
        else if(pattern=="colour_edge"){const float c=x<128?.003f:-.003f;v+=cv::Vec3f(c,0,-c);}
        else if(pattern=="line"&&x>=128&&x<130)v+=cv::Vec3f::all(.003f);
        out.at<cv::Vec3f>(y,x)=v;
    }return out;
}
cv::Mat mosaic(const cv::Mat &rgb,const Demosaic::Pattern &pattern)
{
    cv::Mat out(rgb.size(),CV_32F);for(int y=0;y<out.rows;++y)for(int x=0;x<out.cols;++x)out.at<float>(y,x)=rgb.at<cv::Vec3f>(y,x)[pattern[std::size_t(y%2)][std::size_t(x%2)]];return out;
}
cv::Mat demosaic(const cv::Mat &source,const Demosaic::Pattern &pattern)
{
    cv::Mat out;require(Demosaic::interpolate(source,pattern,Demosaic::OpeningMedian,Demosaic::Range::Signed,out,nullptr,{}),"RCD failed");require(cv::checkRange(out),"Nonfinite RCD output");return out;
}
QJsonObject measure(const cv::Mat &estimate,const cv::Mat &target,const cv::Mat &cleanRcd)
{
    const cv::Rect roi(80,80,96,96);double mse=0,relative=0,iError=0,cError=0,numerator=0,denominator=0;cv::Vec3d bias{};
    const auto em=cv::mean(estimate(roi)),tm=cv::mean(target(roi));
    for(int y=roi.y;y<roi.y+roi.height;++y)for(int x=roi.x;x<roi.x+roi.width;++x){
        const auto a=cv::Vec3d(estimate.at<cv::Vec3f>(y,x)),b=cv::Vec3d(target.at<cv::Vec3f>(y,x));const auto d=a-b;
        const auto r=a-cv::Vec3d(cleanRcd.at<cv::Vec3f>(y,x));mse+=d.dot(d);relative+=r.dot(r);bias+=d;
        const double di=.25*d[0]+.5*d[1]+.25*d[2],co=.5*(d[0]-d[2]),cg=d[1]-.5*(d[0]+d[2]);iError+=di*di;cError+=(co*co+cg*cg)/2;
        for(int c=0;c<3;++c){const double t=b[c]-tm[c];numerator+=(a[c]-em[c])*t;denominator+=t*t;}
    }
    const double n=roi.area();return {{"rgb_mse_truth",mse/(3*n)},{"rgb_mse_clean_rcd",relative/(3*n)},
        {"intensity_mse_truth",iError/n},{"chroma_mse_truth",cError/n},
        {"bias_rgb",QJsonArray{bias[0]/n,bias[1]/n,bias[2]/n}},
        {"pattern_gain",denominator>1e-15?QJsonValue(numerator/denominator):QJsonValue(QJsonValue::Null)}};
}
void selfTest()
{
    cv::Mat x(32,32,CV_32F);for(int y=0;y<32;++y)for(int c=0;c<32;++c)x.at<float>(y,c)=float(std::sin(y*.3)+std::cos(c*.2));
    require(cv::norm(x,filterCfa(x,{}),cv::NORM_INF)==0,"Zero variance must be identity");
    cv::Mat constant(32,32,CV_32F,cv::Scalar(-.1));require(cv::norm(constant,filterCfa(constant,{1,1,1,1}),cv::NORM_INF)<1e-7,"Signed constant changed");
    for(int y=0;y<32;++y)for(int c=0;c<32;++c)constant.at<float>(y,c)=float((y%2)*2+c%2)-1;
    require(cv::norm(constant,filterCfa(constant,{1,1,1,1}),cv::NORM_INF)<1e-7,"CFA phases mixed");
    cv::Mat shifted=x+.25f,expected=filterCfa(x,{.1,.1,.1,.1})+.25f;
    require(cv::norm(filterCfa(shifted,{.1,.1,.1,.1}),expected,cv::NORM_INF)<1e-5,"Translation covariance failed");
    bool rejected=false;try{filterCfa(x,{-1,0,0,0});}catch(const std::runtime_error &){rejected=true;}require(rejected,"Invalid variance accepted");
}
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);QCommandLineParser parser;parser.addHelpOption();
    parser.addOption(QCommandLineOption("self-test","Small signed/phase/variance contract tests"));
    for(const auto *key:{"first","second"})parser.addOption(QCommandLineOption(key,key,"path"));parser.process(app);
    try{cv::setNumThreads(1);selfTest();if(parser.isSet("self-test")){std::cout<<"5 contract checks passed\n";return 0;}
        require(parser.isSet("first")&&parser.isSet("second"),"Two black development captures required");
        const std::array<Capture,2> captures{read(parser.value("first")),read(parser.value("second"))};
        require(captures[0].pattern==captures[1].pattern&&captures[0].colours==captures[1].colours&&captures[0].gains==captures[1].gains&&captures[0].divisor==captures[1].divisor,"Capture contexts differ");
        QJsonArray cases;
        for(int shot=0;shot<2;++shot){const auto residual=noise(captures[std::size_t(shot)]);const auto power=variances(noise(captures[std::size_t(1-shot)]));
            for(double level:{.005,.03})for(const auto *name:{"flat","texture","colour_edge","line"}){
                const auto target=truth(name,level),clean=mosaic(target,captures[0].pattern);const auto cleanRcd=demosaic(clean,captures[0].pattern);
                QJsonObject row{{"shot",shot},{"pattern",name},{"level",level},{"reference_variance_by_phase",QJsonArray{power[0],power[1],power[2],power[3]}},
                    {"clean_rcd",measure(cleanRcd,target,cleanRcd)}};
                for(bool noisy:{false,true}){cv::Mat input=noisy?clean+residual:clean.clone();QJsonObject variants;
                    for(int branch=0;branch<4;++branch){const auto start=std::chrono::steady_clock::now();
                        const auto prepared=(branch==1||branch==3)?filterCfa(input,power):input;
                        auto output=demosaic(prepared,captures[0].pattern);
                        if(branch>=2)require(CameraDenoise::apply(output,{100,100},{}),"Post-RCD denoising failed");
                        auto metrics=measure(output,target,cleanRcd);metrics.insert("patch_pipeline_ms",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
                        variants.insert(std::array<const char *,4>{"rcd_only","cfa_wiener","post_current","cfa_plus_post"}[std::size_t(branch)],metrics);
                    }row.insert(noisy?"noisy":"clean_controls",variants);
                }cases.append(row);
            }
        }
        const QJsonObject result{{"captures",QJsonArray{captures[0].metadata,captures[1].metadata}},{"cases",cases},
            {"demosaic_workers",Demosaic::workerThreads()},{"denoise_workers",CameraDenoise::workerThreads()},
            {"scope","256px synthetic balanced camera RGB + real black residual; independent development dark reference per shot; central96px measured; read-noise only; no DCP or VST; not a production latency benchmark"}};
        std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
