// Manual corpus diagnostic: no production switches, TIFF generation or sidecar writes.
#include "RawDecoder.h"
#include "CameraProfileEngine.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <libraw/libraw.h>
#include <cmath>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace {
QString hash(const QString &path) {
    QFile f(path); if(!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot hash input");
    QCryptographicHash h(QCryptographicHash::Sha256); if(!h.addData(&f))throw std::runtime_error("Hash read failed");
    return QString::fromLatin1(h.result().toHex());
}
template<typename T> QJsonArray array(const T *p,int n=4) {
    QJsonArray a; for(int i=0;i<n;++i)a.append(double(p[i]));return a;
}
cv::Mat sample(const cv::Mat &p) {
    cv::Mat q((p.rows+7)/8,(p.cols+7)/8,p.type());
    for(int y=0;y<q.rows;++y)for(int x=0;x<q.cols;++x)
        std::memcpy(q.ptr(y)+x*q.elemSize(),p.ptr(y*8)+x*8*p.elemSize(),q.elemSize());
    return q;
}
ColorPipeline::WorkingImage working(const cv::Mat &rgb) {
    ColorPipeline::WorkingImage result;
    if(rgb.channels()==4) result.linearRgba=rgb;
    else {std::vector<cv::Mat> c;cv::split(rgb,c);c.push_back(cv::Mat(rgb.rows,rgb.cols,CV_32F,cv::Scalar(1)));cv::merge(c,result.linearRgba);}
    return result;
}
double linear(double x){return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);}
struct Accumulator {
    double r=0,g=0,b=0,y=0;int n=0,outside=0,black=0;
    void add(double a,double c,double d) {
        ++n;r+=a;g+=c;b+=d;const double luminance=.2126*a+.7152*c+.0722*d;y+=luminance;
        outside+=(std::min({a,c,d})<0 || std::max({a,c,d})>1);black+=luminance<=.001;
    }
    QJsonObject json()const {
        if(!n)return {{"samples",0}};
        return {{"samples",n},{"mean_Y",y/n},{"mean_RGB",QJsonArray{r/n,g/n,b/n}},
                {"G_over_R_of_means",std::abs(r)>1e-12?QJsonValue(g/r):QJsonValue()},
                {"outside_srgb_percent",100.*outside/n},{"black_percent",100.*black/n}};
    }
};
QJsonObject metrics(const ColorPipeline::WorkingImage &image) {
    QString error;const QImage output=ColorPipeline::toSrgb(image,ColorPipeline::OutputFormat::HighBitDepth,&error);
    if(output.isNull())throw std::runtime_error(error.toStdString());
    Accumulator before,after,roi;int clip=0;QJsonArray warmBands;std::array<Accumulator,12> bands;
    for(int y=0;y<image.linearRgba.rows;++y)for(int x=0;x<image.linearRgba.cols;++x) {
        const auto p=image.linearRgba.at<cv::Vec4f>(y,x);
        if(!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2]))throw std::runtime_error("Nonfinite pixel");
        before.add(p[0],p[1],p[2]);
        const QRgba64 pixel=reinterpret_cast<const QRgba64*>(output.constScanLine(y))[x];
        const double r=linear(pixel.red()/65535.),g=linear(pixel.green()/65535.),b=linear(pixel.blue()/65535.);
        after.add(r,g,b);clip+=pixel.red()>=65534||pixel.green()>=65534||pixel.blue()>=65534;
        if(x>=image.linearRgba.cols*.25 && x<image.linearRgba.cols*.75 && y>=image.linearRgba.rows*.48 && y<image.linearRgba.rows*.76)roi.add(r,g,b);
        const double hi=std::max({r,g,b}),lo=std::min({r,g,b}),delta=hi-lo;
        if(hi>1e-8 && delta/hi>.1) {
            double h=hi==r?(g-b)/delta:hi==g?(b-r)/delta+2:(r-g)/delta+4;
            h=std::fmod(h*60+360,360.);bands[std::clamp(int(h/30),0,11)].add(r,g,b);
        }
    }
    for(int i=0;i<12;++i){auto item=bands[i].json();item["hue_begin_degrees"]=i*30;warmBands.append(item);}
    return {{"pre_output",before.json()},{"output",after.json()},{"flower_ROI",roi.json()},
            {"encoded_clipped_percent",100.*clip/image.linearRgba.total()},{"hue_bands_linear_HSV",warmBands}};
}
QJsonObject metadata(const RawDecoder::SourceMetadata &m) {
    return {{"camera",m.cameraModel},{"lens",m.lensModel},{"ISO",m.iso},{"focal_mm",m.focalLengthMm},
            {"exposure_seconds",m.exposureSeconds},{"dcp",m.dcpProfile},{"lcp",m.lcpProfile},{"lcp_sha256",m.lcpSha256},
            {"lens_geometry",m.lensGeometryApplied},{"lens_vignetting",m.lensVignettingApplied},
            {"CA",m.chromaticAberrationApplied},{"normalization",m.rawNormalizationApplied},
            {"black_estimated",m.opticalBlackLevelEstimated},{"warning",m.developmentWarning}};
}
void write(const QString &path,const QJsonObject &object) {
    QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))throw std::runtime_error("Refuse overwrite / cannot create JSON");
    const QByteArray data=QJsonDocument(object).toJson();if(file.write(data)!=data.size()||!file.flush())throw std::runtime_error("JSON write failed");
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTextStream out(stdout);
    try {
        if(argc!=4)throw std::runtime_error("Usage: rawOpeningProbe REPO_ROOT NEW_RAW_JSON_DIRECTORY NEW_PREVIEW_DIRECTORY");
        const QDir root(app.arguments()[1]);QDir destination(app.arguments()[2]),previews(app.arguments()[3]);
        if(!QDir().mkpath(destination.absolutePath())||previews.exists()||!QDir().mkpath(previews.absolutePath()))throw std::runtime_error("Invalid/new preview directory required");
        QJsonObject hashes;for(const QString path:{"tests/raw_opening_probe.cpp","src/raw/RawDecoder.cpp","src/raw/RawDecoder.h","src/profiles/CameraProfileEngine.cpp","src/profiles/CameraProfileEngine.h","src/core/ColorPipeline.cpp","src/core/ColorPipeline.h","src/editor/imageeditor.cpp"})hashes[path]=hash(root.filePath(path));
        const auto paths=QDir(root.filePath("tests/image/base")).entryInfoList(QDir::Files,QDir::Name);
        QJsonArray records;
        for(const auto &file:paths) {
            if(!RawDecoder::supportedExtensions().contains(file.suffix().toLower()))continue;
            out<<"Decoding "<<file.fileName()<<Qt::endl;QElapsedTimer timer;timer.start();
            QJsonObject record{{"scene",file.completeBaseName()},{"source",file.absoluteFilePath()},{"sha256",hash(file.absoluteFilePath())}};
            try {
                RawDecoder::Options options;options.automaticWhiteBalance=false;options.preferCameraNeutral=true;
                options.useNormalizationEnvironment=false;options.lensCorrectionEnabled=false;options.chromaticAberrationEnabled=false;
                RawDecoder::SourceMetadata meta;QString error;
                auto full=RawDecoder::decode(file.absoluteFilePath(),options,&meta,&error);
                if(full.isNull())throw std::runtime_error(error.toStdString());
                record["metadata"]=metadata(meta);record["width"]=full.linearRgba.cols;record["height"]=full.linearRgba.rows;
                record["reference_kelvin"]=full.referenceWhiteBalanceKelvin.value_or(0);
                auto current=working(sample(full.linearRgba));
                QJsonObject stages{{"current",metrics(current)}};
                auto preview=[&](const ColorPipeline::WorkingImage &image,const QString &name){
                    const QImage view=ColorPipeline::toSrgb(image,ColorPipeline::OutputFormat::Preview8Bit);
                    const QString path=previews.filePath(file.completeBaseName()+"-"+name+".png");
                    if(view.isNull()||QFileInfo::exists(path)||!view.save(path))throw std::runtime_error("Cannot save diagnostic preview");
                };
                preview(current,"current");
                if(full.dcpContext) {
                    auto context=*full.dcpContext;context.cameraRgb=sample(context.cameraRgb);
                    record["camera_neutral"]=array(context.cameraNeutral.val,3);
                    record["profile_path"]=context.profilePath;record["profile_sha256"]=hash(context.profilePath);
                    const auto beforeProfile=context.cameraRgb.clone();
                    // Release full buffers before sample-only ablations.
                    full={};
                    cv::Mat witness=beforeProfile.clone();
                    if(!CameraProfileEngine::applyDcp(context.profilePath,witness,context.sourceKelvin,&error,context.options))throw std::runtime_error(error.toStdString());
                    const auto witnessImage=working(witness);
                    stages["sample_replay_witness"]=metrics(witnessImage);
                    record["sample_replay_max_abs_float_error"]=cv::norm(witnessImage.linearRgba,current.linearRgba,cv::NORM_INF);
                    auto hueOptions=context.options;hueOptions.preserveToneCurveHue=true;
                    cv::Mat stable=beforeProfile.clone();
                    if(!CameraProfileEngine::applyDcp(context.profilePath,stable,context.sourceKelvin,&error,hueOptions))throw std::runtime_error(error.toStdString());
                    stages["hue_preserving_curve"]=metrics(working(stable));preview(working(stable),"hue-curve");
                    cv::Mat changed;
                    if(CameraProfileEngine::renderWhiteBalance(context,5200,5,changed,&error)) {
                        stages["display_5200_tint5_diagnostic"]=metrics(working(changed));preview(working(changed),"5200-5");
                    }else record["WB_diagnostic_error"]=error;
                    stages["camera_RGB_before_DCP_not_sRGB"]=QJsonObject{{"mean_RGB",array(cv::mean(beforeProfile).val,3)}};
                }else{full={};record["DCP_context_missing"]=true;}
                record["stages"]=stages;record["status"]="measured";
                if(record["sha256"].toString()!=hash(file.absoluteFilePath()))throw std::runtime_error("RAW changed");
            }catch(const std::exception &e){record["status"]="diagnostic_error";record["error"]=e.what();}
            record["elapsed_ms"]=double(timer.elapsed());records.append(record);
            const auto now=QDateTime::currentDateTimeUtc();
            const QString id="inspection-ouverture-scene-v001-"+now.toString("yyyyMMddTHHmmsszzzZ")+"-"+file.completeBaseName();
            write(destination.filePath(id+".json"),{{"campaign_id",id},{"schema_version",1},{"data_version",1},{"group","base-colorimetrique"},
                {"measured_at_utc",now.toString(Qt::ISODateWithMs)},{"source_hashes",hashes},{"executable_sha256",hash(app.applicationFilePath())},
                {"LibRaw",RawDecoder::backendVersion()},{"native_sample_step",8},{"record",record},
                {"method","As Shot; no normalization or LCP; sample-only DCP ablations; production output; independent distributions, no pixel LR MAE"}});
            out<<file.fileName()<<" "<<record["status"].toString()<<" "<<timer.elapsed()<<"ms "<<record["error"].toString()<<Qt::endl;
        }
        for(auto i=hashes.begin();i!=hashes.end();++i)if(i.value().toString()!=hash(root.filePath(i.key())))throw std::runtime_error("Code changed during probe");
        out<<"Complete "<<records.size()<<" RAW records; no source images/sidecars changed."<<Qt::endl;
        return 0;
    }catch(const std::exception &e){QTextStream(stderr)<<e.what()<<Qt::endl;return 1;}
}
