#pragma once
// Offline DCP experiment. No production option, source/profile mutation or TIFF export.
#include "RawDecoder.h"
#include "CameraProfileEngine.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
// Explicit analysis boundary: historical metrics/experiments use extended
// linear sRGB, while the only production renderer now returns Rec.2020.
bool applyDcpForSrgbAnalysis(const QString &path, cv::Mat &rgb, double kelvin,
                            QString *error, const CameraProfileEngine::DcpRenderOptions &options = {}) {
    if (!CameraProfileEngine::applyDcp(path, rgb, kelvin, error, options)) return false;
    WorkingColor::transform(rgb, WorkingColor::toSrgb());
    return true;
}
// Hide only IFD tag identifiers in disposable profile copies; never alter payloads.
QByteArray hideDcpTags(QByteArray bytes,const std::vector<int> &tags) {
    if(bytes.size()<8)throw std::runtime_error("Short DCP");
    const bool little=bytes.left(2)=="II";
    if(!little && bytes.left(2)!="MM")throw std::runtime_error("Invalid DCP byte order");
    auto u16=[&](qsizetype p){return little?qFromLittleEndian<quint16>(bytes.constData()+p):qFromBigEndian<quint16>(bytes.constData()+p);};
    auto u32=[&](qsizetype p){return little?qFromLittleEndian<quint32>(bytes.constData()+p):qFromBigEndian<quint32>(bytes.constData()+p);};
    if(u16(2)!=0x4352)throw std::runtime_error("Not DCP");
    const quint64 offset=u32(4);
    if(offset+2>quint64(bytes.size()))throw std::runtime_error("Invalid DCP IFD");
    const auto count=u16(qsizetype(offset));
    if(count>536)throw std::runtime_error("Diagnostic tag space exhausted");
    if(offset+2+12*quint64(count)+4>quint64(bytes.size()))throw std::runtime_error("Truncated DCP IFD");
    for(int i=0;i<count;++i)if(u16(qsizetype(offset)+2+12*i)>=65000)throw std::runtime_error("Reserved diagnostic tag");
    for(int i=0;i<count;++i) {
        const qsizetype p=qsizetype(offset)+2+12*i;
        if(std::find(tags.begin(),tags.end(),u16(p))==tags.end())continue;
        if(little)qToLittleEndian<quint16>(quint16(65000+i),bytes.data()+p);
        else qToBigEndian<quint16>(quint16(65000+i),bytes.data()+p);
    }
    return bytes;
}

QJsonObject linearStageStats(const cv::Mat &rgb) {
    double sum=0;int negative=0,upper=0,overY=0;std::vector<double> luminance;
    cv::Vec3d low(1e30,1e30,1e30),high(-1e30,-1e30,-1e30),channelUpper(0,0,0);
    for(int y=0;y<rgb.rows;++y)for(int x=0;x<rgb.cols;++x) {
        const auto v=rgb.at<cv::Vec3f>(y,x);
        for(int c=0;c<3;++c) {
            if(!std::isfinite(v[c]))throw std::runtime_error("Nonfinite stage");
            low[c]=std::min(low[c],double(v[c]));high[c]=std::max(high[c],double(v[c]));
            channelUpper[c]+=v[c]>=65534.f/65535;
        }
        negative+=std::min({v[0],v[1],v[2]})<0;
        upper+=std::max({v[0],v[1],v[2]})>=65534.f/65535;
        const double Y=.2126*v[0]+.7152*v[1]+.0722*v[2];
        sum+=Y;overY+=Y>=1;luminance.push_back(Y);
    }
    std::sort(luminance.begin(),luminance.end());const double n=double(luminance.size());
    QJsonArray quantiles,mins,maxs,uppers;
    for(double q:{.05,.5,.95,.99})quantiles.append(luminance[size_t(q*(n-1))]);
    for(int c=0;c<3;++c){mins.append(low[c]);maxs.append(high[c]);uppers.append(100*channelUpper[c]/n);}
    return {{"samples",n},{"mean_Y_unbounded",sum/n},{"Y_p05_p50_p95_p99",quantiles},
        {"negative_channel_percent",100*negative/n},{"upper_channel_percent",100*upper/n},
        {"Y_at_or_above_one_percent",100*overY/n},{"channel_min",mins},{"channel_max",maxs},
        {"channel_upper_percent",uppers}};
}

QString digest(const QString &path) {
    QFile f(path); if(!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot hash input");
    QCryptographicHash h(QCryptographicHash::Sha256);
    if(!h.addData(&f)) throw std::runtime_error("Cannot hash input");
    return QString::fromLatin1(h.result().toHex());
}
struct Curve {
    std::vector<double> x,y,m;
    explicit Curve(const QJsonArray &points) {
        for(int i=0;i<points.size();i+=2){x.push_back(points[i].toDouble());y.push_back(points[i+1].toDouble());}
        const int n=int(x.size()); if(n<2||x.front()!=0||x.back()!=1)throw std::runtime_error("Unsupported curve");
        m.resize(n,0);std::vector<double> u(n,0);
        for(int i=1;i<n;++i)if(x[i]<=x[i-1])throw std::runtime_error("Unsorted curve");
        // Natural cubic spline: zero second derivative at both endpoints.
        for(int i=1;i<n-1;++i) {
            double s=(x[i]-x[i-1])/(x[i+1]-x[i-1]),p=s*m[i-1]+2;
            m[i]=(s-1)/p;
            u[i]=(6*((y[i+1]-y[i])/(x[i+1]-x[i])-(y[i]-y[i-1])/(x[i]-x[i-1]))/(x[i+1]-x[i-1])-s*u[i-1])/p;
        }
        for(int i=n-2;i>=0;--i)m[i]=m[i]*m[i+1]+u[i];
    }
    double at(double v,bool cubic)const {
        v=std::clamp(v,0.,1.);
        int j=std::clamp(int(std::lower_bound(x.begin(),x.end(),v)-x.begin()),1,int(x.size())-1),i=j-1;
        double h=x[j]-x[i],a=(x[j]-v)/h,b=1-a;
        return a*y[i]+b*y[j]+(cubic?((a*a*a-a)*m[i]+(b*b*b-b)*m[j])*h*h/6:0);
    }
};
const cv::Matx33d proToSrgb=WorkingColor::toSrgb()*WorkingColor::fromXyzD50()*
    cv::Matx33d(.7976749,.1351917,.0313534,.2880402,.7118741,.0000857,0,0,.8252100);
cv::Mat renderCurve(const cv::Mat &before,const Curve &curve,bool cubic,bool hue) {
    cv::Mat output=before.clone();const auto inverse=proToSrgb.inv();
    for(int y=0;y<output.rows;++y)for(int x=0;x<output.cols;++x) {
        auto &p=output.at<cv::Vec3f>(y,x);cv::Vec3d v=inverse*cv::Vec3d(p);
        for(int c=0;c<3;++c)v[c]=std::clamp(v[c],0.,1.);
        double lo=std::min({v[0],v[1],v[2]}),hi=std::max({v[0],v[1],v[2]}),a=curve.at(lo,cubic),b=curve.at(hi,cubic);
        for(int c=0;c<3;++c)v[c]=hue&&b>=a?(hi>lo?a+(v[c]-lo)/(hi-lo)*(b-a):a):curve.at(v[c],cubic);
        p=cv::Vec3f(proToSrgb*v);
    }
    return output;
}
ColorPipeline::WorkingImage working(const cv::Mat &rgb) {
    ColorPipeline::WorkingImage w;
    std::vector<cv::Mat> c;cv::split(rgb,c);c.push_back(cv::Mat(rgb.rows,rgb.cols,CV_32F,cv::Scalar(1)));cv::merge(c,w.linearRgba);
    WorkingColor::transform(w.linearRgba, WorkingColor::fromSrgb());return w;
}
cv::Mat sample3(const cv::Mat &p) {
    cv::Mat q((p.rows+7)/8,(p.cols+7)/8,CV_32FC3);
    for(int y=0;y<q.rows;++y)for(int x=0;x<q.cols;++x) {
        const float *v=reinterpret_cast<const float*>(p.ptr(y*8))+x*8*p.channels();q.at<cv::Vec3f>(y,x)={v[0],v[1],v[2]};
    }return q;
}
double linear(double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);}
QJsonObject metrics(const cv::Mat &rgb) {
    auto image=ColorPipeline::toSrgb(working(rgb),ColorPipeline::OutputFormat::HighBitDepth);
    if(image.isNull())throw std::runtime_error("Output conversion failed");
    double sum=0,red=0,green=0;int clip=0,outside=0,dark=0,roi=0;std::vector<double> values;
    for(int y=0;y<rgb.rows;++y)for(int x=0;x<rgb.cols;++x) {
        const auto p=rgb.at<cv::Vec3f>(y,x);
        for(int c=0;c<3;++c)if(!std::isfinite(p[c]))throw std::runtime_error("Nonfinite output");
        outside+=std::min({p[0],p[1],p[2]})<0||std::max({p[0],p[1],p[2]})>1;
        const auto q=reinterpret_cast<const QRgba64*>(image.constScanLine(y))[x];
        double r=linear(q.red()/65535.),g=linear(q.green()/65535.),b=linear(q.blue()/65535.),Y=.2126*r+.7152*g+.0722*b;
        sum+=Y;values.push_back(Y);dark+=Y<=.001;clip+=q.red()>=65534||q.green()>=65534||q.blue()>=65534;
        if(x>=rgb.cols*.25&&x<rgb.cols*.75&&y>=rgb.rows*.48&&y<rgb.rows*.76){red+=r;green+=g;++roi;}
    }
    std::sort(values.begin(),values.end());double n=double(values.size());
    QJsonArray quantiles;for(double q:{.05,.5,.95})quantiles.append(values[size_t(q*(n-1))]);
    return {{"mean_Y",sum/n},{"Y_p05_p50_p95",quantiles},{"encoded_clipped_percent",100*clip/n},
            {"outside_srgb_percent",100*outside/n},{"black_percent",100*dark/n},
            {"central_ROI_G_over_R",red>0?QJsonValue(green/red):QJsonValue()},{"ROI_samples",roi}};
}
cv::Mat preserveLuminanceGamut(const cv::Mat &source) {
    cv::Mat result=source.clone();
    for(int y=0;y<result.rows;++y)for(int x=0;x<result.cols;++x) {
        auto &v=result.at<cv::Vec3f>(y,x);
        const float Y=.2126f*v[0]+.7152f*v[1]+.0722f*v[2];
        if(Y<=0){v={0,0,0};continue;}if(Y>=1){v={1,1,1};continue;}
        const cv::Vec3f neutral(Y,Y,Y),chroma=v-neutral;float scale=1;
        for(int c=0;c<3;++c) {
            if(chroma[c]>0)scale=std::min(scale,(1-Y)/chroma[c]);
            if(chroma[c]<0)scale=std::min(scale,-Y/chroma[c]);
        }
        v=neutral+chroma*std::clamp(scale,0.f,1.f);
    }return result;
}
void selfTest() {
    const QByteArray fixture=QByteArray::fromHex("49495243080000000100fcc60b00040000001a00000000000000");
    const auto hidden=hideDcpTags(fixture,{50940});
    if(hidden==fixture || hidden.left(10)!=fixture.left(10) || hidden.mid(12)!=fixture.mid(12) ||
       qFromLittleEndian<quint16>(hidden.constData()+10)!=65000)throw std::runtime_error("Unsafe DCP tag rewrite");
    if(hideDcpTags(fixture,{50938})!=fixture)throw std::runtime_error("Unexpected absent tag rewrite");
    const QByteArray big=QByteArray::fromHex("4d4d4352000000080001c6fc000b000000040000001a00000000");
    if(qFromBigEndian<quint16>(hideDcpTags(big,{50940}).constData()+10)!=65000)throw std::runtime_error("Big endian tag failed");
    bool refused=false;try{hideDcpTags(hidden,{50940});}catch(const std::runtime_error&){refused=true;}
    if(!refused)throw std::runtime_error("Diagnostic tag collision accepted");
    refused=false;try{hideDcpTags(fixture.left(15),{50940});}catch(const std::runtime_error&){refused=true;}
    if(!refused)throw std::runtime_error("Truncated IFD accepted");
    Curve identity(QJsonArray{0.,0.,.2,.2,.7,.7,1.,1.});
    for(int i=0;i<=1000;++i)if(std::abs(identity.at(i/1000.,true)-i/1000.)>1e-12)throw std::runtime_error("Identity spline failed");
    Curve curve(QJsonArray{0.,0.,.2,.35,.6,.8,1.,1.});
    for(size_t i=0;i<curve.x.size();++i)if(std::abs(curve.at(curve.x[i],true)-curve.y[i])>1e-12)throw std::runtime_error("Spline knot failed");
    // Nonzero curvature test, natural boundary conditions and RGB neutral invariant.
    if(std::abs(curve.at(.4,true)-curve.at(.4,false))<1e-5||curve.m.front()!=0||curve.m.back()!=0)throw std::runtime_error("Spline curvature failed");
    cv::Mat neutral(1,1,CV_32FC3,cv::Scalar(.2,.2,.2));
    if(cv::norm(renderCurve(neutral,identity,true,true),neutral,cv::NORM_INF)>1e-5)throw std::runtime_error("Neutral identity failed");
    cv::Mat extended(1,1,CV_32FC3,cv::Scalar(-.2,.5,1.2));
    const auto bounded=preserveLuminanceGamut(extended).at<cv::Vec3f>(0,0);
    const auto input=extended.at<cv::Vec3f>(0,0);const cv::Vec3f weights(.2126,.7152,.0722);
    if(std::abs(bounded.dot(weights)-input.dot(weights))>1e-6 ||
       std::min({bounded[0],bounded[1],bounded[2]})< -1e-6 ||
       std::max({bounded[0],bounded[1],bounded[2]})>1+1e-6)throw std::runtime_error("Luminance gamut invariant failed");
}
}

