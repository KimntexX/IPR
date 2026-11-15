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
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
QString digest(const QString &path){QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot hash input");QCryptographicHash h(QCryptographicHash::Sha256);require(h.addData(&f),"Hash read failed");return QString::fromLatin1(h.result().toHex());}
}

int main(int argc,char **argv){
    QCoreApplication app(argc,argv);QCommandLineParser parser;parser.addHelpOption();
    parser.addOption(QCommandLineOption("input","Source Bayer RAW","path"));
    parser.addOption(QCommandLineOption("output","New capture JSON","path"));
    parser.addOption(QCommandLineOption("mode","parity only at unpack boundary","mode","parity"));
    parser.addOption(QCommandLineOption("roi","Visible x,y,width,height; repeatable","rect"));
    parser.addOption(QCommandLineOption("full","Capture entire visible mosaic"));
    parser.process(app);
    try{
        require(parser.isSet("input")&&parser.isSet("output")&&parser.value("mode")=="parity","Need --input --output --mode parity");
        require(!(parser.isSet("full")&&parser.isSet("roi")),"Choose full or ROIs");
        const auto input=QFileInfo(parser.value("input")).absoluteFilePath(),output=QFileInfo(parser.value("output")).absoluteFilePath();
        require(!QFile::exists(output)&&!QFile::exists(output+".bin"),"Output exists");const auto sourceHash=digest(input);
        auto storage=std::make_unique<LibRaw>();auto &raw=*storage;
#ifdef _WIN32
        require(raw.open_file(input.toStdWString().c_str())==LIBRAW_SUCCESS,"RAW open failed");
#else
        require(raw.open_file(input.toUtf8().constData())==LIBRAW_SUCCESS,"RAW open failed");
#endif
        require(raw.unpack()==LIBRAW_SUCCESS&&raw.imgdata.rawdata.raw_image,"RAW unpack failed or non-mosaic input");
        const auto &s=raw.imgdata.sizes;require(raw.imgdata.idata.filters!=0&&raw.imgdata.idata.filters!=9,"Only Bayer mosaic supported");
        require(s.width>0&&s.height>0&&s.raw_width>=s.left_margin+s.width&&s.raw_height>=s.top_margin+s.height,"Invalid visible extent");
        const size_t pitch=s.raw_pitch?s.raw_pitch:size_t(s.raw_width)*sizeof(ushort);
        require(pitch>=size_t(s.raw_width)*sizeof(ushort)&&pitch%sizeof(ushort)==0,"Invalid RAW stride");
        QJsonArray pattern;for(int y=0;y<2;++y){QJsonArray row;for(int x=0;x<2;++x)row.append(raw.COLOR(y+s.top_margin,x+s.left_margin));pattern.append(row);}
        QList<std::array<int,4>> regions;
        if(parser.isSet("full"))regions.append({0,0,int(s.width),int(s.height)});
        else if(parser.isSet("roi"))for(auto text:parser.values("roi")){
            const auto parts=text.split(',');require(parts.size()==4,"ROI needs four integers");std::array<int,4> r{};
            for(int i=0;i<4;++i){bool ok=false;r[size_t(i)]=parts[i].toInt(&ok);require(ok,"Invalid ROI integer");}regions.append(r);
        }else{
            require(s.width>=256&&s.height>=256,"Default ROIs require at least256 pixels");
            for(double f:{.2,.5,.8})regions.append({(int(s.width*f)-128)/2*2,(int(s.height)/2-128)/2*2,256,256});
        }
        QFile pixels(output+".bin");require(pixels.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create binary capture");QJsonArray stages;
        for(auto r:regions){
            const int x=r[0],y=r[1],w=r[2],h=r[3];require(x>=0&&y>=0&&w>0&&h>0&&qint64(x)+w<=s.width&&qint64(y)+h<=s.height,"ROI outside visible mosaic");
            const auto offset=pixels.pos();QByteArray row(w*2,Qt::Uninitialized);
            for(int yy=0;yy<h;++yy){
                const auto *sourceRow=reinterpret_cast<const ushort *>(reinterpret_cast<const unsigned char *>(raw.imgdata.rawdata.raw_image)+size_t(y+yy+s.top_margin)*pitch);
                for(int xx=0;xx<w;++xx){const auto value=sourceRow[x+xx+s.left_margin];qToLittleEndian<quint16>(value,row.data()+xx*2);}
                require(pixels.write(row)==row.size(),"Incomplete binary capture");
            }
            stages.append(QJsonObject{{"stage","unpacked_cfa"},{"shape",QJsonArray{h,w,1}},{"dtype","u16"},{"offset_bytes",double(offset)},{"stride_bytes",w*2},{"origin_yx",QJsonArray{y,x}},{"unit","DN"},{"layout","cfa"},{"orientation","sensor"},{"context",QJsonObject{{"cfa",pattern},{"visible_size",QJsonArray{int(s.height),int(s.width)}}}}});
        }
        pixels.close();require(digest(input)==sourceHash,"Source changed during capture");
        QJsonObject manifest{{"schema_version",1},{"mode","parity"},{"capture_id",QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzzZ")},{"measured_at_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"source",QJsonObject{{"path",input},{"sha256",sourceHash}}},{"engine_revision",IPR_BENCH_REVISION},{"tool_revision",IPR_BENCH_REVISION},
            {"scope","unpack_reference_only_not_full_production_opening"},{"libraw_version",LibRaw::version()},
            {"metadata",QJsonObject{{"make",raw.imgdata.idata.make},{"model",raw.imgdata.idata.model},{"iso",double(raw.imgdata.other.iso_speed)},{"orientation_flip",int(s.flip)},{"raw_size",QJsonArray{int(s.raw_height),int(s.raw_width)}},{"visible_origin_yx",QJsonArray{int(s.top_margin),int(s.left_margin)}}}},
            {"pixels",QJsonObject{{"path",QFileInfo(pixels.fileName()).fileName()},{"sha256",digest(pixels.fileName())}}},{"stages",stages}};
        QFile json(output);require(json.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create manifest");auto bytes=QJsonDocument(manifest).toJson();require(json.write(bytes)==bytes.size(),"Incomplete manifest");
        std::cout<<"Captured unpacked CFA; downstream stages unavailable\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
