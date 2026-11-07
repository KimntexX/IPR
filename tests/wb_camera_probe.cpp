#include "RawDecoder.h"
#include "CameraProfileEngine.h"
#include "Pixels.h"
#include "image_testing.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
QString hash(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&file);
    return QString::fromLatin1(h.result().toHex());
}
bool renderInto(const CameraProfileEngine::DcpWorkingContext &context, int temperature, int tint, ipr::Image &rgb, QString *error)
{
    return CameraProfileEngine::renderFromCameraSignal(context, temperature, tint, 0.0, rgb, error);
}
ipr::Image sampled(const ipr::Image &input) {
    ipr::Image result=ipr::Image::create({(input.width()+7)/8, (input.height()+7)/8}, input.depth(), input.channels());
    const size_t bytes=input.pixelBytes();
    for (int y=0; y<result.height(); ++y)
        for (int x=0; x<result.width(); ++x)
            std::memcpy(result.mutableRow<std::uint8_t>(y)+x*bytes, input.row<std::uint8_t>(y*8)+x*8*bytes, bytes);
    return result;
}
ColorPipeline::WorkingImage rgba(const ipr::Image &rgb) {
    ColorPipeline::WorkingImage result;
    result.linearRgba = ipr::opaque(rgb);
    return result;
}
ipr::Image output(const ColorPipeline::WorkingImage &source) {
    QString error;
    QImage image = ColorPipeline::toSrgb(source,ColorPipeline::OutputFormat::HighBitDepth,&error);
    if (image.isNull()) throw std::runtime_error(error.toStdString());
    ipr::Image rgb=ipr::Image::create({image.width(),image.height()},ipr::Depth::F64,3);
    for (int y=0;y<image.height();++y) {
        const QRgba64 *row = reinterpret_cast<const QRgba64 *>(image.constScanLine(y));
        for (int x=0;x<image.width();++x) {
            const QRgba64 p = row[x];
            ipr::Vec3d v(p.red()/65535.,p.green()/65535.,p.blue()/65535.);
            for(int c=0;c<3;++c) v[c] = v[c]<=.04045 ? v[c]/12.92 : std::pow((v[c]+.055)/1.055,2.4);
            imagetest::at<ipr::Vec3d>(rgb, y,x)=v;
        }
    }
    return rgb;
}
QJsonObject measure(const ipr::Image &base,const ipr::Image &changed) {
    double rb=0,mg=0,lum=0,lum0=0;
    ipr::Vec3d means0(0,0,0), means1(0,0,0);
    int valid=0,clipped=0;
    const double total=double(base.size().area());
    for(int y=0;y<base.height();++y) for(int x=0;x<base.width();++x) {
        const auto a=imagetest::at<ipr::Vec3d>(base, y,x), b=imagetest::at<ipr::Vec3d>(changed, y,x);
        const double hiA=std::max({a[0],a[1],a[2]}),hiB=std::max({b[0],b[1],b[2]});
        lum+=.2126*b[0]+.7152*b[1]+.0722*b[2];
        lum0+=.2126*a[0]+.7152*a[1]+.0722*a[2];
        means0+=a; means1+=b;
        clipped += hiB >= .99996; // Same encoded 65534/65535 clipping threshold.
        if(std::min({a[0],a[1],a[2],b[0],b[1],b[2]})>1e-5 && hiA<.98 && hiB<.98) {
            rb+=std::log(b[0]/b[2])-std::log(a[0]/a[2]);
            mg+=std::log(std::sqrt(b[0]*b[2])/b[1])-std::log(std::sqrt(a[0]*a[2])/a[1]);
            ++valid;
        }
    }
    means0/=total; means1/=total;
    return {{"axis_samples",valid},{"delta_log_R_B",valid?QJsonValue(rb/valid):QJsonValue()},
        {"delta_log_magenta_green",valid?QJsonValue(mg/valid):QJsonValue()},
        {"mean_Y_variant",lum/total},{"Y_ratio",lum0>0?QJsonValue(lum/lum0):QJsonValue()},
        {"channel_mean_RGB_neutral",QJsonArray{means0[0],means0[1],means0[2]}},
        {"channel_mean_RGB_variant",QJsonArray{means1[0],means1[1],means1[2]}},
        {"clipped_percent_variant",100.*clipped/total}};
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTextStream out(stdout);
    if(argc!=3) {out<<"Usage: wbCameraProbe <project-root> <memory-directory>\n";return 2;}
    try {
        QDir root(QString::fromLocal8Bit(argv[1]));
        QJsonArray records;
        bool identity=true;
        for(const QString scene : {"6V1A0007","6V1A0037","6V1A0043","6V1A0327","6V1A9421","6V1A9950"}) {
            const QString path=root.filePath("tests/image/base/"+scene+".CR3");
            const QString before=hash(path);
            RawDecoder::Options options;
            options.automaticWhiteBalance=false;
            options.preferCameraNeutral=true;
            options.useNormalizationEnvironment=false;
            options.lensCorrectionEnabled=false;
            options.chromaticAberrationEnabled=false;
            ImageMetadata metadata;
            QString error;
            const auto decoded=RawDecoder::decode(path,options,&metadata,&error);
            if(decoded.isNull() || !decoded.dcpContext) throw std::runtime_error(error.toStdString());
            auto context=CameraProfileEngine::sharedCopy(*decoded.dcpContext);
            context.cameraRgb=sampled(context.cameraRgb);
            auto neutral=ColorPipeline::sharedCopy(decoded);
            neutral.linearRgba=sampled(decoded.linearRgba);
            const ipr::Image neutralPixels=output(neutral);
            ipr::Image rgb;
            const int ref=int(std::lround(context.sourceKelvin));
            if(!renderInto(context, ref, 0, rgb, &error)) throw std::runtime_error(error.toStdString());
            const bool exact=ipr::largestDifference(rgba(rgb).linearRgba,neutral.linearRgba)==0;
            identity &= exact;
            for(const QString feature : {"temperature","tint"}) {
                const std::vector<int> values=feature=="temperature" ? std::vector<int>{2000,3500,5200,6500,9000,50000} : std::vector<int>{-150,-100,-50,-25,0,25,50,100,150};
                ipr::Image supportRgb;
                if(!renderInto(context, 5200, 0, supportRgb, &error)) throw std::runtime_error(error.toStdString());
                const ipr::Image support=output(rgba(supportRgb));
                for(int value:values) {
                    const int temp=feature=="temperature" ? value:5200;
                    const int tint=feature=="tint"?value:0;
                    ipr::Vec3d gains;
                    if(!CameraProfileEngine::whiteBalanceGains(context,temp,tint,gains,&error) ||
                       !renderInto(context, temp, tint, rgb, &error)) throw std::runtime_error(error.toStdString());
                    const ipr::Image pixels=output(rgba(rgb));
                    records.append(QJsonObject{{"scene",scene},{"feature",feature},{"value",value},
                        {"engine","IPR"},{"settings",QJsonObject{{"temperature",temp},{"tint",tint},
                        {"normalization",false},{"auto_wb",false},{"lens_correction",false},{"chromatic_aberration",false}}},
                        {"source_kelvin",context.sourceKelvin},{"source_camera_neutral",QJsonArray{context.cameraNeutral[0],context.cameraNeutral[1],context.cameraNeutral[2]}},
                        {"camera_gains",QJsonArray{gains[0],gains[1],gains[2]}},
                        {"raw_file",path},{"raw_sha256",before},{"dcp_file",context.profilePath},{"dcp_sha256",hash(context.profilePath)},
                        {"neutral_exact",exact},{"metrics",measure(support,pixels)},
                        {"metadata",QJsonObject{{"dimensions",QJsonArray{decoded.linearRgba.width(),decoded.linearRgba.height()}}}},
                        {"shared_AsShot_metrics",measure(neutralPixels,pixels)}});
                }
            }
            if(before.isEmpty() || before!=hash(path)) throw std::runtime_error("RAW changed during probe");
            out<<scene<<" neutral_exact="<<exact<<Qt::endl;
        }
        QJsonObject hashes;
        for(const QString path : {"src/raw/RawDecoder.cpp","src/editor/imageeditor.cpp","src/profiles/CameraProfileEngine.cpp","src/core/ColorPipeline.cpp","tests/wb_camera_probe.cpp"})
            hashes[path]=hash(root.filePath(path));
        const auto now=QDateTime::currentDateTimeUtc();
        QJsonObject document{{"schema_version",1},{"data_version",3},{"measured_at_utc",now.toString(Qt::ISODateWithMs)},
            {"group","couleur"},{"kind","fresh_RAW_camera_WB_probe"},{"source_hashes",hashes},
            {"executable_sha256",hash(QCoreApplication::applicationFilePath())},{"libraw",RawDecoder::backendVersion()},
            {"sampling_step",8},{"pixel_scope","exact point samples, no resize; all other edits zero"},
            {"all_neutrals_exact",identity},{"records",records}};
        QDir directory(QString::fromLocal8Bit(argv[2]));
        if(!directory.exists() && !QDir().mkpath(directory.absolutePath())) return 1;
        QFile file(directory.filePath("sonde-wb-capteur-v003-"+now.toString("yyyyMMddTHHmmsszzzZ")+".json"));
        if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) || file.write(QJsonDocument(document).toJson())<0) return 1;
        out<<file.fileName()<<Qt::endl;
        return identity?0:1;
    } catch(const std::exception &e) {out<<"ERROR: "<<e.what()<<Qt::endl;return 1;}
}
