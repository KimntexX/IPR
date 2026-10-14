// Exercise the exact diagnostic math/immutable writer, without a private corpus.
#define main iprRawOpeningProbeMain
#include "raw_opening_probe.cpp"
#undef main
#include <QTemporaryDir>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    auto check=[](bool ok,const char *message){if(!ok)throw std::runtime_error(message);};
    try {
        cv::Mat pixels(9,10,CV_32FC4);
        for(int y=0;y<9;++y)for(int x=0;x<10;++x)pixels.at<cv::Vec4f>(y,x)={y*.01f,x*.01f,.18f,1};
        const auto grid=sample(pixels);
        check(grid.rows==2&&grid.cols==2,"Native-grid ceil geometry");
        check(cv::norm(grid.at<cv::Vec4f>(1,1)-pixels.at<cv::Vec4f>(8,8))==0,"Sample is exact, not resized");
        cv::Mat gray(8,8,CV_32FC3,cv::Scalar(.18,.18,.18));
        const auto image=working(gray);
        check(image.linearRgba.type()==CV_32FC4,"RGBA conversion");
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
