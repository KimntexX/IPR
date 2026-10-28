#include "RawInput.h"
#include "LibRawInput.h"
#include "RawDevelopment.h"
#include "Demosaic.h"
#include <libraw/libraw.h>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <opencv2/core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
QString hash(const QString &path){QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot hash file");QCryptographicHash h(QCryptographicHash::Sha256);require(h.addData(&f),"Hash failed");return QString::fromLatin1(h.result().toHex());}
QJsonArray four(const std::array<double,4> &v){QJsonArray a;for(auto x:v)a.append(x);return a;}
const char *name(RawDevelopment::Stage s){
    switch(s){
    case RawDevelopment::Stage::UnpackedCfa:return "unpacked_cfa";
    case RawDevelopment::Stage::BlackSubtractedCfa:return "black_subtracted_cfa";
    case RawDevelopment::Stage::NormalizedCfa:return "normalized_cfa";
    case RawDevelopment::Stage::PreDemosaicCfa:return "pre_demosaic_cfa";
    case RawDevelopment::Stage::PostDemosaicCameraRgb:return "post_demosaic_camera_rgb";
    case RawDevelopment::Stage::PostMedianCameraRgb:return "post_median_camera_rgb";
    }throw std::runtime_error("Unknown capture stage");
}
const char *unit(RawDevelopment::Stage s){
    if(s==RawDevelopment::Stage::UnpackedCfa||s==RawDevelopment::Stage::BlackSubtractedCfa)return "DN";
    if(s==RawDevelopment::Stage::PreDemosaicCfa||s==RawDevelopment::Stage::PostMedianCameraRgb)return "linear_camera_grid_1_65535";
    return "linear_camera_white_1";
}

class Writer {
public:
    explicit Writer(const QString &output):pixels(output+".bin"){
        require(!QFile::exists(output),"Manifest exists");require(pixels.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Binary exists or inaccessible");
    }
    void save(const char *stage,const cv::Mat &image,const char *units,const RawInput::Input &input){
        require(!image.empty()&&(image.depth()==CV_16U||image.depth()==CV_32F)&&cv::checkRange(image),"Invalid capture image");
        QJsonArray pattern;for(auto row:input.pattern){QJsonArray r;for(auto c:row)r.append(c);pattern.append(r);}
        for(double fraction:{.2,.5,.8}){
            const int x=(int(image.cols*fraction)-128)/2*2,y=(image.rows/2-128)/2*2;
            require(x>=0&&y>=0&&x+256<=image.cols&&y+256<=image.rows,"Image too small for bench ROIs");
            cv::Mat crop=image(cv::Rect(x,y,256,256)).clone();const auto offset=pixels.pos();
            QByteArray bytes(int(crop.total()*crop.elemSize()),Qt::Uninitialized);
            if(crop.depth()==CV_16U){for(size_t i=0;i<crop.total()*crop.channels();++i)qToLittleEndian<quint16>(crop.ptr<quint16>()[i],bytes.data()+i*2);}
            else {const quint16 endian=1;require(*reinterpret_cast<const unsigned char *>(&endian)==1,"Float capture currently requires little-endian host");std::copy(crop.datastart,crop.dataend,reinterpret_cast<unsigned char *>(bytes.data()));}
            require(pixels.write(bytes)==bytes.size(),"Incomplete stage capture");
            QJsonObject context{{"cfa",pattern},{"visible_size",QJsonArray{image.rows,image.cols}},
                {"sensor_origin_xy",QJsonArray{input.origin.x,input.origin.y}},{"input_black",four(input.black)},
                {"input_white",input.white},{"input_as_shot_multipliers",four(input.asShotMultipliers)}};
            stages.append(QJsonObject{{"stage",stage},{"shape",QJsonArray{256,256,image.channels()}},{"dtype",image.depth()==CV_16U?"u16":"f32"},
                {"offset_bytes",double(offset)},{"stride_bytes",int(crop.step)},{"origin_yx",QJsonArray{y,x}},{"layout",image.channels()==1?"cfa":"rgb"},
                {"unit",units},{"orientation","sensor"},{"context",context}});
        }
    }
    QFile pixels;
    QJsonArray stages;
};

// A test-only adapter around LibRaw's public callback mechanism. It invokes IPR's
// current RCD, not any demosaic implementation copied from LibRaw.
class Reference : public LibRaw {
public:
    Writer *writer=nullptr;
    const RawInput::Input *input=nullptr;
    bool failed=false;
    void instrument(){imgdata.params.user_qual=0;callbacks.interpolate_bayer_cb=&interpolate;}
private:
    static void interpolate(void *p) noexcept{
        auto *self=static_cast<Reference *>(static_cast<LibRaw *>(p));
        try{
            require(self->writer&&self->input&&self->imgdata.image,"Missing reference capture state");
            auto &d=self->imgdata;Demosaic::Pattern pattern{};
            for(int y=0;y<2;++y)for(int x=0;x<2;++x){int c=self->FC(y,x);pattern[size_t(y)][size_t(x)]=c==3?1:c;}
            cv::Mat mosaic(d.sizes.iheight,d.sizes.iwidth,CV_32FC1);
            for(int y=0;y<mosaic.rows;++y)for(int x=0;x<mosaic.cols;++x)mosaic.at<float>(y,x)=d.image[size_t(y)*mosaic.cols+x][pattern[size_t(y&1)][size_t(x&1)]]/65535.f;
            self->writer->save("pre_demosaic_cfa",mosaic,"linear_camera_grid_1_65535",*self->input);
            cv::Mat rgb,before;
            require(Demosaic::interpolate(mosaic,pattern,Demosaic::OpeningMedian,rgb,&before,{}),"Reference RCD failed");
            self->writer->save("post_demosaic_camera_rgb",before,"linear_camera_white_1",*self->input);
            rgb.forEach<cv::Vec3f>([](cv::Vec3f &v,const int *){for(int c=0;c<3;++c)v[c]=float(std::clamp(std::lround(v[c]*65535.f),0L,65535L))/65535.f;});
            self->writer->save("post_median_camera_rgb",rgb,"linear_camera_grid_1_65535",*self->input);
            for(int y=0;y<rgb.rows;++y)for(int x=0;x<rgb.cols;++x)for(int c=0;c<3;++c)d.image[size_t(y)*rgb.cols+x][c]=std::uint16_t(std::lround(rgb.at<cv::Vec3f>(y,x)[c]*65535.f));
        }catch(...){self->failed=true;}
    }
};
}

int main(int argc,char **argv){
    QCoreApplication app(argc,argv);QCommandLineParser parser;parser.addHelpOption();
    for(auto key:{"input","output","path"})parser.addOption(QCommandLineOption(key,key,"value"));
    parser.addOption(QCommandLineOption("mode","Only parity mode is implemented","mode","parity"));
    parser.addOption(QCommandLineOption("black","Explicit DN levels R,G,B,G2 for both paths","levels"));
    parser.addOption(QCommandLineOption("white","Explicit white DN for both paths, disables automatic adjustment","DN"));
    parser.process(app);
    try{
        const auto source=QFileInfo(parser.value("input")).absoluteFilePath(),output=QFileInfo(parser.value("output")).absoluteFilePath(),path=parser.value("path");
        require(parser.isSet("input")&&parser.isSet("output")&&(path=="reference"||path=="candidate")&&parser.value("mode")=="parity","Need input/output/path and parity mode");
        const auto sourceHash=hash(source);auto raw=std::make_unique<Reference>();
#ifdef _WIN32
        require(raw->open_file(source.toStdWString().c_str())==LIBRAW_SUCCESS,"RAW open failed");
#else
        require(raw->open_file(source.toUtf8().constData())==LIBRAW_SUCCESS,"RAW open failed");
#endif
        raw->imgdata.params.use_camera_wb=1;raw->imgdata.params.use_auto_wb=0;raw->imgdata.params.use_camera_matrix=0;
        raw->imgdata.params.no_auto_bright=1;raw->imgdata.params.output_bps=16;raw->imgdata.params.output_color=0;
        raw->imgdata.params.gamm[0]=raw->imgdata.params.gamm[1]=1.;raw->imgdata.params.user_flip=0;
        require(raw->unpack()==LIBRAW_SUCCESS,"RAW unpack failed");
        if(parser.isSet("black")){
            auto levels=parser.value("black").split(',');require(levels.size()==4,"Need four black levels");
            raw->imgdata.color.black=raw->imgdata.rawdata.color.black=0;
            for(int c=0;c<4;++c){bool ok=false;auto level=levels[c].toUInt(&ok);require(ok&&level<65535,"Invalid black DN");raw->imgdata.color.cblack[c]=raw->imgdata.rawdata.color.cblack[c]=level;}
        }
        if(parser.isSet("white")){
            bool ok=false;auto white=parser.value("white").toUInt(&ok);require(ok&&white>0&&white<=65535,"Invalid white DN");
            raw->imgdata.color.maximum=raw->imgdata.rawdata.color.maximum=white;raw->imgdata.params.adjust_maximum_thr=0.f;
        }
        auto input=LibRawInput::read(*raw);
        require(RawInput::valid(input),"Input outside C2 coverage; no silent fallback in parity bench");
        Writer writer(output);QJsonObject applied;
        if(path=="candidate"){
            RawDevelopment::Options options;options.adjustWhiteThreshold=raw->imgdata.params.adjust_maximum_thr;
            options.observer=[&](const RawDevelopment::Capture &c){writer.save(name(c.stage),c.pixels,unit(c.stage),input);
                applied=QJsonObject{{"black",four(c.context.black)},{"white",c.context.white},{"divisor",c.context.divisor},{"gains",four(c.context.gains)},{"policy",c.context.policy}};};
            auto result=RawDevelopment::develop(input,options);require(!result.cameraRgb.empty(),"Candidate development failed");
            writer.save("pre_dcp_camera_rgb",result.cameraRgb,"linear_camera_grid_1_65535",input);
        }else{
            writer.save("unpacked_cfa",input.mosaic,"DN",input);raw->writer=&writer;raw->input=&input;raw->instrument();
            require(raw->dcraw_process()==LIBRAW_SUCCESS&&!raw->failed,"Reference processing failed");
            int error=0;std::unique_ptr<libraw_processed_image_t,void(*)(libraw_processed_image_t *)> memory(raw->dcraw_make_mem_image(&error),LibRaw::dcraw_clear_mem);
            require(memory&&error==LIBRAW_SUCCESS&&memory->bits==16&&memory->colors==3,"Invalid reference output");
            // Canonical I2 representation: divide each integer in float. OpenCV's
            // convertTo reciprocal multiplication can differ by one float ulp.
            cv::Mat integers(memory->height,memory->width,CV_16UC3,memory->data),rgb(memory->height,memory->width,CV_32FC3);
            for(int y=0;y<rgb.rows;++y)for(int x=0;x<rgb.cols;++x)for(int c=0;c<3;++c)rgb.at<cv::Vec3f>(y,x)[c]=float(integers.at<cv::Vec<std::uint16_t,3>>(y,x)[c])/65535.f;
            writer.save("pre_dcp_camera_rgb",rgb,"linear_camera_grid_1_65535",input);
        }
        writer.pixels.close();require(hash(source)==sourceHash,"Source changed during capture");
        QJsonObject manifest{{"schema_version",1},{"mode","parity"},{"path",path},{"measured_at_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"source",QJsonObject{{"path",source},{"sha256",sourceHash}}},{"engine_revision",IPR_BENCH_REVISION},{"tool_revision",IPR_BENCH_REVISION},
            {"scope",parser.isSet("black")||parser.isSet("white")?"C2_module_explicit_levels_not_calibration_selection_or_DCP":"C2_module_native_decoder_levels_not_IPR_calibration_or_DCP"},{"libraw_version",LibRaw::version()},
            {"applied",applied},{"metadata",QJsonObject{{"model",input.model},{"iso",input.iso},{"orientation_flip",input.orientation}}},
            {"pixels",QJsonObject{{"path",QFileInfo(writer.pixels.fileName()).fileName()},{"sha256",hash(writer.pixels.fileName())}}},{"stages",writer.stages}};
        QFile out(output);require(out.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Manifest exists");auto bytes=QJsonDocument(manifest).toJson();require(out.write(bytes)==bytes.size(),"Incomplete manifest");
        std::cout<<path.toStdString()<<" captured\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
