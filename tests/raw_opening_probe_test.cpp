// Exercise the exact diagnostic math/immutable writer, without a private corpus.
#define main iprRawOpeningProbeMain
#include "raw_opening_probe.cpp"
#include "image_testing.h"
#include "Pixels.h"
#undef main
#include <QTemporaryDir>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    auto check=[](bool ok,const char *message){if(!ok)throw std::runtime_error(message);};
    try {
        ipr::Image pixels = ipr::Image::create({10,9},ipr::Depth::F32,4);
        for(int y=0;y<9;++y)for(int x=0;x<10;++x)imagetest::at<ipr::Vec4f>(pixels, y,x)={y*.01f,x*.01f,.18f,1};
        const auto grid=sample(pixels);
        check(grid.height()==2&&grid.width()==2,"Native-grid ceil geometry");
        check(ipr::length(imagetest::at<ipr::Vec4f>(grid, 1,1)-imagetest::at<ipr::Vec4f>(pixels, 8,8))==0,"Sample is exact, not resized");
        ipr::Image gray = ipr::filled({8,8},ipr::Depth::F32,3,.18);
        const auto image=working(gray);
        check(image.linearRgba.depth()==ipr::Depth::F32 && image.linearRgba.channels()==4,"RGBA conversion");
        const auto stats=metrics(image);
        check(std::abs(stats["pre_output"].toObject()["mean_Y"].toDouble()-.18)<1e-6,"Linear Y on gray");
        check(std::abs(stats["output"].toObject()["mean_Y"].toDouble()-.18)<1e-5,"16-bit output round trip");
        check(stats["encoded_clipped_percent"].toDouble()==0,"Gray cannot clip");
        QTemporaryDir dir;check(dir.isValid(),"Temporary test directory");
        const QString path=dir.filePath("memory.json");write(path,{{"proof",1}});
        bool refused=false;try{write(path,{{"proof",2}});}catch(const std::exception &){refused=true;}
        check(refused,"Immutable JSON must refuse overwrite");
        QFile file(path);check(file.open(QIODevice::ReadOnly),"Read memory");
        check(QJsonDocument::fromJson(file.readAll()).object()["proof"].toInt()==1,"Original evidence preserved");
    }catch(const std::exception &e){QTextStream(stderr)<<e.what()<<Qt::endl;return 1;}
    return 0;
}
