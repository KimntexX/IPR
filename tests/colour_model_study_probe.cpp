#define IPR_GENERATOR_UNIT_TEST
#include "../src/tools/generate_test_images.cpp"
#include "colour_model_study.h"
#include "CameraProfileEngine.h"
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTextStream>
#include <QDateTime>
#include <array>

namespace {
using ColourStudy::RGB;
const std::array<std::string,4> models{"current","A1_endpoint_rgb","A2_adaptive_oklch","B_pr598_prophoto"};
const std::array<int,7> levels{-100,-50,-25,0,25,50,100};
QString digest(const QString &path) {
    QFile f(path); if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot hash source");
    QCryptographicHash h(QCryptographicHash::Sha256);h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}
double linear(double value) { return value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4); }
cv::Mat output(const ColorPipeline::WorkingImage &image) {
    QString error; const QImage encoded=ColorPipeline::toSrgb(image,ColorPipeline::OutputFormat::HighBitDepth,&error);
    if(encoded.isNull())throw std::runtime_error(error.toStdString());
    cv::Mat result(encoded.height(),encoded.width(),CV_64FC3);
    for(int y=0;y<encoded.height();++y) {
        const auto row=reinterpret_cast<const QRgba64 *>(encoded.constScanLine(y));
        for(int x=0;x<encoded.width();++x)result.at<RGB>(y,x)=
            RGB(linear(row[x].red()/65535.),linear(row[x].green()/65535.),linear(row[x].blue()/65535.));
    }
    return result;
}
ColorPipeline::WorkingImage sample(const ColorPipeline::WorkingImage &source) {
    auto result=source;result.dcpContext.reset();
    result.linearRgba=cv::Mat((source.linearRgba.rows+7)/8,(source.linearRgba.cols+7)/8,CV_32FC4);
    for(int y=0;y<result.linearRgba.rows;++y)for(int x=0;x<result.linearRgba.cols;++x)
        result.linearRgba.at<cv::Vec4f>(y,x)=source.linearRgba.at<cv::Vec4f>(y*8,x*8);
    return result;
}
ColorPipeline::WorkingImage transform(const ColorPipeline::WorkingImage &source,const std::string &model,double v,double s) {
    auto result=source;result.linearRgba=source.linearRgba.clone();
    for(int y=0;y<result.linearRgba.rows;++y)for(int x=0;x<result.linearRgba.cols;++x) {
        auto &p=result.linearRgba.at<cv::Vec4f>(y,x);
        const auto q=ColourStudy::apply(RGB(p[0],p[1],p[2]),model,v,s);
        for(int c=0;c<3;++c)p[c]=float(q[c]);
    }
    if(!cv::checkRange(result.linearRgba,true))throw std::runtime_error("Non-finite candidate output");
    return result;
}
struct Group {
    qint64 n=0,hueN=0;double dc=0,relative=0,hueAbs=0,okDc=0;
    void add(const RGB &a,const RGB &b) {
        const auto h0=ColourStudy::hsv(a),h1=ColourStudy::hsv(b);
        ++n;dc+=h1[1]-h0[1];relative+=(h1[1]-h0[1])/std::max(h0[1],1e-12);
        const auto l0=ColourStudy::lab(a),l1=ColourStudy::lab(b);
        const double c0=std::hypot(l0[1],l0[2]),c1=std::hypot(l1[1],l1[2]);okDc+=c1-c0;
        if(c0>1e-4 && c1>1e-4) {
            hueAbs+=std::abs(ColourStudy::wrap((std::atan2(l1[2],l1[1])-std::atan2(l0[2],l0[1]))*180/ColourStudy::pi));++hueN;
        }
    }
    QJsonObject json() const {
        const auto mean=[&](double v){return n?QJsonValue(v/n):QJsonValue();};
        return {{"samples",double(n)},{"mean_chroma_delta",mean(dc)},
            {"mean_relative_chroma_delta",mean(relative)},{"mean_OK_chroma_delta",mean(okDc)},
            {"OK_hue_samples",double(hueN)},{"mean_abs_OK_hue_delta_degrees",hueN?QJsonValue(hueAbs/hueN):QJsonValue()}};
    }
};
QJsonObject measure(const cv::Mat &base,const cv::Mat &changed,
                    const cv::Mat &raw0,const cv::Mat &raw1) {
    double y0=0,y1=0,rawY0=0,rawY1=0,maxRawDy=0;int clip0=0,clip1=0,newClip=0,outside=0;
    const double threshold=linear(65534./65535.);
    std::array<Group,2> groups;std::array<Group,12> hues;
    for(int y=0;y<base.rows;++y)for(int x=0;x<base.cols;++x) {
        const RGB a=base.at<RGB>(y,x),b=changed.at<RGB>(y,x);
        const auto a4=raw0.at<cv::Vec4f>(y,x),b4=raw1.at<cv::Vec4f>(y,x);
        const RGB ar(a4[0],a4[1],a4[2]),br(b4[0],b4[1],b4[2]);
        y0+=ColourStudy::y(a);y1+=ColourStudy::y(b);
        rawY0+=ColourStudy::y(ar);rawY1+=ColourStudy::y(br);
        maxRawDy=std::max(maxRawDy,std::abs(ColourStudy::y(ar)-ColourStudy::y(br)));
        const double ha=std::max({a[0],a[1],a[2]}),hb=std::max({b[0],b[1],b[2]});
        const bool ca=ha>=threshold,cb=hb>=threshold;clip0+=ca;clip1+=cb;newClip+=cb&&!ca;
        outside+=std::min({br[0],br[1],br[2]})<0 || std::max({br[0],br[1],br[2]})>1;
        const RGB h=ColourStudy::hsv(a);
        if(std::min({a[0],a[1],a[2]})>1e-5 && ha<.98 && hb<.98) {
            if(h[1]>=.02 && h[1]<.25)groups[0].add(a,b);
            if(h[1]>=.5)groups[1].add(a,b);
            if(h[1]>=.02)hues[std::clamp(int(h[0]/30),0,11)].add(a,b);
        }
    }
    QJsonArray bands;for(int i=0;i<12;++i) {auto b=hues[i].json();b["hue_begin_degrees"]=30*i;b["hue_end_degrees"]=30*(i+1);bands.append(b);}
    const double n=double(base.total());
    return {{"samples",n},{"mean_Y_neutral",y0/n},{"mean_Y_variant",y1/n},
        {"Y_ratio",y0>0?QJsonValue(y1/y0):QJsonValue()},
        {"pre_output_Y_ratio",rawY0>0?QJsonValue(rawY1/rawY0):QJsonValue()},
        {"pre_output_max_abs_Y_delta",maxRawDy},{"pre_output_outside_srgb_percent",100.*outside/n},
        {"clipped_samples_neutral",clip0},{"clipped_samples_variant",clip1},
        {"newly_clipped_samples",newClip},{"clipped_percent_variant",100.*clip1/n},
        {"chroma_groups",QJsonObject{{"low_chroma",groups[0].json()},{"high_chroma",groups[1].json()}}},
        {"hue_bands",bands}};
}
QJsonArray synthetic() {
    const std::array<RGB,11> colors{RGB(0,0,0),RGB(.18,.18,.18),RGB(1,1,1),
        RGB(.180001,.18,.18),RGB(.5,.2,.1),RGB(.02,.3,.5),RGB(.7,.65,.02),
        RGB(.6,.03,.01),RGB(1.8,.3,.1),RGB(-.1,.2,.5),RGB(4,4,4)};
    QJsonArray all;
    for(const auto &model:models)for(const auto &p:colors) {
        bool finite=true,monotone=true;double previous=-1e10,maxGreyError=0,maxDy=0;
        for(int i=-100;i<=100;++i) {
            const RGB q=ColourStudy::apply(p,model,i/100.,0);
            for(int c=0;c<3;++c)finite &= std::isfinite(q[c]);
            const double sat=ColourStudy::hsv(q)[1];monotone &= sat+1e-6>=previous;previous=sat;
            maxDy=std::max(maxDy,std::abs(ColourStudy::y(q)-ColourStudy::y(p)));
            if(p[0]==p[1]&&p[1]==p[2])maxGreyError=std::max(maxGreyError,cv::norm(q-p));
        }
        const RGB zero=ColourStudy::apply(p,model,0,0);
        all.append(QJsonObject{{"model",QString::fromStdString(model)},{"input_linear_srgb",QJsonArray{p[0],p[1],p[2]}},
            {"zero_exact",cv::norm(zero-p)==0},{"finite",finite},{"HSV_saturation_monotone",monotone},
            {"max_grey_error",maxGreyError},{"max_pre_output_Y_delta",maxDy},
            {"scope","201 vibrance positions, pre-output; synthetic coordinates, not observed human perception"}});
    }
    return all;
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTextStream out(stdout);
    try {
        if(argc!=3)throw std::runtime_error("Usage: colourModelStudyProbe PROJECT_ROOT MEMORY_DIRECTORY");
        const QDir root(QString::fromLocal8Bit(argv[1]));
        const QString destination=QString::fromLocal8Bit(argv[2]);
        QJsonArray records,sources;QJsonObject hashes;
        for(const QString relative:{"tests/colour_model_study_probe.cpp","tests/colour_model_study.h",
            "src/core/Vibrance.h","src/core/ColorPipeline.cpp","src/tools/generate_test_images.cpp",
            "src/editor/imageeditor.cpp","src/raw/RawDecoder.cpp","src/profiles/CameraProfileEngine.cpp"})
            hashes[relative]=digest(root.filePath(relative));
        for(const QString scene:{"6V1A0007","6V1A0037","6V1A0043","6V1A0327","6V1A9421","6V1A9890","6V1A9950"}) {
            const QString path=root.filePath("tests/image/base/"+scene+".CR3");
            const QString before=digest(path);
            auto loaded=IprTestImageGenerator::load(path);
            auto working=sample(loaded.image);const cv::Mat base=output(working);
            QString profilePath;
            if(loaded.image.dcpContext)profilePath=loaded.image.dcpContext->profilePath;
            sources.append(QJsonObject{{"scene",scene},{"file",path},{"sha256",before},
                {"dimensions",QJsonArray{loaded.image.linearRgba.cols,loaded.image.linearRgba.rows}},
                {"dcp",loaded.metadata.dcpProfile},{"dcp_file",profilePath},
                {"dcp_sha256",profilePath.isEmpty()?QString():digest(profilePath)},
                {"lcp",loaded.metadata.lcpProfile},{"lcp_file",loaded.metadata.lcpPath},{"lcp_sha256",loaded.metadata.lcpSha256},
                {"lens_geometry",loaded.metadata.lensGeometryApplied},{"lens_vignetting",loaded.metadata.lensVignettingApplied},
                {"CA",loaded.metadata.chromaticAberrationApplied},{"normalization",loaded.metadata.rawNormalizationApplied},
                {"reference_kelvin",loaded.image.referenceWhiteBalanceKelvin.value_or(5200.)}});
            for(const auto &model:models)for(const QString feature:{"vibrance","saturation"})for(int value:levels) {
                QElapsedTimer timer;timer.start();
                const auto adjusted=transform(working,model,feature=="vibrance"?value/100.:0,feature=="saturation"?value/100.:0);
                const double ms=timer.nsecsElapsed()/1e6;
                const auto pixels=output(adjusted);
                records.append(QJsonObject{{"scene",scene},{"model",QString::fromStdString(model)},
                    {"feature",feature},{"value",value},{"transformation_ms",ms},
                    {"metrics",measure(base,pixels,working.linearRgba,adjusted.linearRgba)}});
            }
            if(before!=digest(path))throw std::runtime_error("RAW changed during study");
            out<<scene<<" four candidates measured"<<Qt::endl;
        }
        for(auto it=hashes.begin();it!=hashes.end();++it)
            if(it.value()!=digest(root.filePath(it.key())))throw std::runtime_error("Study source changed");
        for(const auto entry:sources) {
            const auto s=entry.toObject();
            if(s["sha256"]!=digest(s["file"].toString()))throw std::runtime_error("RAW changed at closure");
            for(const QString prefix:{"dcp","lcp"})
                if(!s[prefix+"_file"].toString().isEmpty() &&
                   s[prefix+"_sha256"]!=digest(s[prefix+"_file"].toString()))
                    throw std::runtime_error("Profile changed at closure");
        }
        const auto now=QDateTime::currentDateTimeUtc();const QString id="sonde-modeles-couleur-v001-"+now.toString("yyyyMMddTHHmmsszzzZ");
        QJsonObject result{{"campaign_id",id},{"schema_version",1},{"data_version",1},
            {"measured_at_utc",now.toString(Qt::ISODateWithMs)},{"group","couleur"},
            {"sources",sources},{"source_hashes",hashes},{"executable_sha256",digest(app.applicationFilePath())},
            {"LibRaw",RawDecoder::backendVersion()},{"records",records},{"synthetic",synthetic()},
            {"sampling",QJsonObject{{"native_step",8},{"before_output_encoding",true},{"no_resizing",true}}},
            {"units",QJsonObject{{"Y","relative linear luminance"},{"chroma","HSV saturation in linear RGB"},
                {"OK_chroma","sqrt(a*a+b*b) in Oklab"},{"angle","degrees"},
                {"ratio","dimensionless"},{"percent","100*count/sampled pixels"},{"transformation_ms","milliseconds, sample only"}}},
            {"parameters",QJsonObject{{"endpoint_start",.75},{"negative_endpoint_reduction",.035},
                {"positive_endpoint_boost",.065},{"A2_relative_chroma","C/max(abs(L),0.05), clamp 0..1"},
                {"A2_skin_hue_degrees",50},{"A2_skin_width_degrees",25},{"A2_skin_strength",.4},
                {"A2_skin_lightness_support","smooth(0.10,0.35,L)*(1-smooth(0.85,1.10,L))"},
                {"A2_skin_chroma_fade","smooth(0.005,0.025,C)"},
                {"pr598_head","b9e312a748fde0b0e5b0eeb070a69b7ecf2df59f"},
                {"PR598_positive",QJsonArray{.751,.736,.15,.391}},
                {"PR598_negative",QJsonArray{1.288,.323,.608,.263}},
                {"PR598_skin_hue_width",QJsonArray{20,35}},
                {"PR598_fade",.3},{"PR598_saturation_positive",QJsonArray{.927,.076}}}},
            {"decision",QJsonObject{{"production_modified",false},{"visual","not_evaluated"},{"LR_is_a_target",false}}},
            {"limitations",QJsonArray{"Diagnostic candidate equations, not a port of all LightCraft stages",
                "Same IPR gamut/output for all candidates; no spatial tests or full-photo visual validation",
                "Warm hue/lightness proxy is not semantic detection of skin",
                "A2 heuristic parameters are uncalibrated; PR598 fitted constants are not physical constants",
                "Transformation times are on sampled pixels, not end-to-end IHM performance"}}};
        if(!QDir().mkpath(destination))throw std::runtime_error("Cannot create memory directory");
        QFile file(QDir(destination).filePath(id+".json"));
        if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))throw std::runtime_error("Cannot publish immutable study");
        const auto bytes=QJsonDocument(result).toJson(QJsonDocument::Indented);
        if(file.write(bytes)!=bytes.size() || !file.flush()) {
            file.close();file.remove(); // only the incomplete file just created with NewOnly
            throw std::runtime_error("Study write failed");
        }
        out<<file.fileName()<<Qt::endl;
    }catch(const std::exception &error){out<<"Study failed: "<<error.what()<<Qt::endl;return 1;}
    return 0;
}
