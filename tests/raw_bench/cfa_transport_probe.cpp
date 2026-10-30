#include "Demosaic.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/core.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *reason)
{
    if (!condition) throw std::runtime_error(reason);
}

QJsonValue pixels(const cv::Mat &image)
{
    require(image.type()==CV_32FC3 && image.isContinuous() && cv::checkRange(image), "Invalid output");
    return QString::fromLatin1(QByteArray::fromRawData(reinterpret_cast<const char *>(image.data),
        int(image.total()*image.elemSize())).toBase64());
}

QJsonObject transport(const cv::Mat &mosaic, const Demosaic::Pattern &pattern)
{
    cv::Mat before,after;
    require(Demosaic::interpolate(mosaic,pattern,Demosaic::OpeningMedian,Demosaic::Range::Signed,
        after,&before,{}), "Signed RCD failed");
    return {{"before_median",pixels(before)}, {"after_median",pixels(after)},
        {"shape",QJsonArray{256,256,3}}, {"dtype","f32_le"}, {"workers",Demosaic::workerThreads()}};
}
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    cv::setNumThreads(1);
    try {
        const unsigned short endian = 1;
        require(*reinterpret_cast<const unsigned char *>(&endian)==1,"Little endian required");
        if (app.arguments().contains("--self-test")) {
            cv::Mat input(256,256,CV_32F,cv::Scalar(-.01));
            cv::Mat before,after;
            const Demosaic::Pattern pattern{{{{0,1}},{{1,2}}}};
            require(Demosaic::interpolate(input,pattern,Demosaic::OpeningMedian,Demosaic::Range::Signed,
                after,&before,{}),"Constant signed test failed");
            require(!before.empty() && !after.empty() && cv::checkRange(before) && cv::checkRange(after),"Missing finite output");
            require(cv::norm(before,cv::Mat(256,256,CV_32FC3,cv::Scalar::all(-.01)),cv::NORM_INF)<1e-6,"Signed pre-median changed");
            require(cv::norm(after,cv::Mat(256,256,CV_32FC3,cv::Scalar::all(-.01)),cv::NORM_INF)<1e-6,"Signed post-median changed");
            std::cout<<"Signed constant preserved at both stages\n";
            return 0;
        }
        std::string line;
        require(bool(std::getline(std::cin,line)),"Missing input");
        QJsonParseError error;
        const auto document=QJsonDocument::fromJson(QByteArray::fromStdString(line),&error);
        require(error.error==QJsonParseError::NoError && document.isObject(),"Invalid JSON");
        const auto bytes=QByteArray::fromBase64(document.object().value("cfa").toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
        require(bytes.size()==256*256*4,"Wrong CFA size");
        cv::Mat input(256,256,CV_32F);
        std::memcpy(input.data,bytes.constData(),std::size_t(bytes.size()));
        require(cv::checkRange(input),"Nonfinite CFA");
        const auto value=document.object().value("pattern").toArray();
        require(value.size()==2 && value[0].toArray().size()==2 && value[1].toArray().size()==2,"Wrong pattern");
        Demosaic::Pattern pattern;
        for (int y=0;y<2;++y) for(int x=0;x<2;++x) {
            const int colour=value[y].toArray()[x].toInt(-1);
            require(colour>=0 && colour<=2,"Unknown CFA colour");
            pattern[std::size_t(y)][std::size_t(x)]=colour;
        }
        std::cout<<QJsonDocument(transport(input,pattern)).toJson(QJsonDocument::Compact).constData()<<'\n';
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; }
    return 0;
}
