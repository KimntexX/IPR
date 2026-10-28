#include "RawDecoder.h"
#include "Demosaic.h"
#include "CameraProfileEngine.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <opencv2/core.hpp>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {
void require(bool v,const char *message){if(!v)throw std::runtime_error(message);}
QString hash(const QString &path){QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot hash file");QCryptographicHash h(QCryptographicHash::Sha256);require(h.addData(&f),"File hash failed");return QString::fromLatin1(h.result().toHex());}
quint64 peakBytes(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info{};info.cb=sizeof(info);
    require(GetProcessMemoryInfo(GetCurrentProcess(),&info,sizeof(info))!=0,"Cannot measure memory peak");return quint64(info.PeakWorkingSetSize);
#else
    struct rusage info{};require(getrusage(RUSAGE_SELF,&info)==0,"Cannot measure memory peak");
#ifdef __APPLE__
    return quint64(info.ru_maxrss);
#else
    return quint64(info.ru_maxrss)*1024;
#endif
#endif
}
QString imageHash(const cv::Mat &m){
    QCryptographicHash h(QCryptographicHash::Sha256);for(int y=0;y<m.rows;++y)h.addData(QByteArrayView(reinterpret_cast<const char *>(m.ptr(y)),qsizetype(m.cols*m.elemSize())));
    return QString::fromLatin1(h.result().toHex());
}
}

int main(int argc,char **argv){
    QCoreApplication app(argc,argv);app.setApplicationName("iprRawBench");QCommandLineParser p;p.addHelpOption();
    for(auto key:{"input","output","profile-root","profile","manifest"})p.addOption(QCommandLineOption(key,key,"path"));
    p.process(app);
    try{
        require(p.isSet("input")&&p.isSet("output")&&p.isSet("profile-root")&&p.isSet("manifest"),"Need input, output, explicit profile-root and manifest");
        const auto source=QFileInfo(p.value("input")).absoluteFilePath(),output=QFileInfo(p.value("output")).absoluteFilePath();
        require(!QFile::exists(output)&&!QFile::exists(output+".bin"),"Output exists");
        const auto sourceHash=hash(source),manifestHash=hash(p.value("manifest"));
        const auto profile=p.value("profile");const auto profileHash=profile.isEmpty()?QString():hash(profile);
        cv::setNumThreads(1);
        RawDecoder::Options options;options.automaticWhiteBalance=false;options.profileDirectories=QStringList{p.value("profile-root")};
        options.dcpProfileOverride=profile;options.preferCameraNeutral=false;options.automaticLcpSelection=false;
        options.lensCorrectionEnabled=false;options.chromaticAberrationEnabled=false;options.useNormalizationEnvironment=false;options.normalizationManifest=p.value("manifest");
        QString error;ImageMetadata metadata;QElapsedTimer timer;timer.start();
        auto image=RawDecoder::decode(source,options,&metadata,&error);
        const double elapsed=double(timer.nsecsElapsed())/1e6;const auto memory=peakBytes();
        if(image.isNull())throw std::runtime_error(("Decode failed: "+error).toStdString());
        require(image.linearRgba.type()==CV_32FC4&&cv::checkRange(image.linearRgba),"Invalid working image");
        require(metadata.lensGeometryApplied==false&&metadata.lensVignettingApplied==false&&metadata.chromaticAberrationApplied==false,"Unexpected optics");
        if(profile.isEmpty())require(metadata.dcpProfile.isEmpty()&&!image.dcpContext,"Unexpected implicit DCP");
        else require(image.dcpContext&&hash(image.dcpContext->profilePath)==profileHash,"Requested DCP not applied");
        QJsonObject full{{"shape",QJsonArray{image.linearRgba.rows,image.linearRgba.cols,4}},{"dtype","f32"},{"sha256",imageHash(image.linearRgba)}};
        QFile binary(output+".bin");require(binary.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create pixels");QJsonArray stages;
        const quint16 endian=1;require(*reinterpret_cast<const unsigned char *>(&endian)==1,"Float binary requires little-endian host");
        for(double f:{.2,.5,.8}){
            const auto &m=image.linearRgba;const int x=(int(m.cols*f)-128)/2*2,y=(m.rows/2-128)/2*2;
            require(x>=0&&y>=0&&x+256<=m.cols&&y+256<=m.rows,"Image too small");
            const auto offset=binary.pos();for(int yy=0;yy<256;++yy){const char *row=reinterpret_cast<const char *>(m.ptr<float>(y+yy)+x*4);require(binary.write(row,256*4*4)==256*4*4,"Incomplete capture");}
            QJsonObject context{{"profile_sha256",profileHash},{"calibration_applied",metadata.rawNormalizationApplied},{"white",int(metadata.normalizationWhiteLevel)},
                {"baseline_ev",metadata.normalizationBaselineExposureEv},{"profile_compensation_ev",metadata.profileExposureCompensationEv}};
            stages.append(QJsonObject{{"stage","working_rec2020"},{"dtype","f32"},{"shape",QJsonArray{256,256,4}},{"offset_bytes",double(offset)},
                {"stride_bytes",256*4*4},{"origin_yx",QJsonArray{y,x}},{"orientation","display"},{"unit","linear_rec2020_d65"},{"layout","rgba"},{"context",context}});
        }
        binary.close();require(hash(source)==sourceHash&&hash(p.value("manifest"))==manifestHash,"Input changed during measurement");
        QJsonObject data{{"schema_version",1},{"mode","parity"},{"engine_revision",IPR_ENGINE_REVISION},{"measured_at_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"source",QJsonObject{{"path",source},{"sha256",sourceHash}}},{"pixels",QJsonObject{{"path",QFileInfo(binary.fileName()).fileName()},{"sha256",hash(binary.fileName())}}},
            {"stages",stages},{"full_image",full},{"decode_ms",elapsed},{"decode_peak_working_set_bytes",double(memory)},{"rcd_workers",Demosaic::workerThreads()},{"opencv_threads",cv::getNumThreads()},
            {"manifest_sha256",manifestHash},{"profile_sha256",profileHash},{"metadata",QJsonObject{{"model",metadata.cameraModel},{"iso",metadata.iso},{"format",metadata.format},
                {"profile",metadata.dcpProfile},{"normalization_applied",metadata.rawNormalizationApplied},{"white",int(metadata.normalizationWhiteLevel)},
                {"baseline_ev",metadata.normalizationBaselineExposureEv},{"profile_compensation_ev",metadata.profileExposureCompensationEv},{"black_estimated",metadata.opticalBlackLevelEstimated},
                {"white_balance_kelvin",image.referenceWhiteBalanceKelvin.value_or(0.)},{"development_warning",metadata.developmentWarning}}}};
        QFile out(output);require(out.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create manifest");auto bytes=QJsonDocument(data).toJson();require(out.write(bytes)==bytes.size(),"Incomplete manifest");
        std::cout<<"decode_ms="<<elapsed<<" peak_bytes="<<memory<<" revision="<<IPR_ENGINE_REVISION<<'\n';
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
