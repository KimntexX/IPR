#pragma once
// Diagnostic candidates only. No alternate production path or saved process mode.
#include "Vibrance.h"
#include <opencv2/core.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <stdexcept>

namespace ColourStudy {
using RGB = cv::Vec3d;
inline constexpr double pi = 3.14159265358979323846;
inline double y(const RGB &p) { return p.dot(RGB(.2126,.7152,.0722)); }
inline double smooth(double a,double b,double x) {
    const double t=std::clamp((x-a)/(b-a),0.,1.); return t*t*(3-2*t);
}
inline double wrap(double h) { return h-360*std::floor((h+180)/360); }
inline RGB hsv(const RGB &p) {
    const double hi=std::max({p[0],p[1],p[2]}),lo=std::min({p[0],p[1],p[2]}),d=hi-lo;
    double h=0;
    if(d>1e-15) {
        h=hi==p[0] ? (p[1]-p[2])/d : hi==p[1] ? (p[2]-p[0])/d+2 : (p[0]-p[1])/d+4;
        h=std::fmod(h*60+360,360.);
    }
    return {h,hi>0?d/hi:0,hi};
}
inline RGB fromHsv(double h,double s,double v) {
    h-=360*std::floor(h/360); const double c=s*v,z=c*(1-std::abs(std::fmod(h/60,2.)-1));
    RGB p; const int k=int(h/60);
    switch(k) {case 0:p={c,z,0};break;case 1:p={z,c,0};break;case 2:p={0,c,z};break;
        case 3:p={0,z,c};break;case 4:p={z,0,c};break;default:p={c,0,z};}
    return p+RGB(v-c,v-c,v-c);
}
// Oklab matrices: Ottosson (2021), https://bottosson.github.io/posts/oklab/
inline RGB lab(const RGB &p) {
    RGB q(.4122214708*p[0]+.5363325363*p[1]+.0514459929*p[2],
          .2119034982*p[0]+.6806995451*p[1]+.1073969566*p[2],
          .0883024619*p[0]+.2817188376*p[1]+.6299787005*p[2]);
    for(int i=0;i<3;++i)q[i]=std::cbrt(q[i]);
    return {.2104542553*q[0]+.7936177850*q[1]-.0040720468*q[2],
            1.9779984951*q[0]-2.4285922050*q[1]+.4505937099*q[2],
            .0259040371*q[0]+.7827717662*q[1]-.8086757660*q[2]};
}
inline RGB fromLab(const RGB &p) {
    RGB q(p[0]+.3963377774*p[1]+.2158037573*p[2],
          p[0]-.1055613458*p[1]-.0638541728*p[2],
          p[0]-.0894841775*p[1]-1.2914855480*p[2]);
    for(int i=0;i<3;++i)q[i]=q[i]*q[i]*q[i];
    return {4.0767416621*q[0]-3.3077115913*q[1]+.2309699292*q[2],
           -1.2684380046*q[0]+2.6097574011*q[1]-.3413193965*q[2],
           -.0041960863*q[0]-.7034186147*q[1]+1.7076147010*q[2]};
}
// XYZ matrices / Bradford D65->D50: W3C CSS Color 4, sample conversions.
inline const cv::Matx33d srgbXyz(.4123907992659595,.357584339383878,.1804807884018343,
    .2126390058715104,.7151686787677559,.07219231536073371,
    .01933081871559185,.119194779794626,.9505321522496607);
inline const cv::Matx33d ppXyz(.7977666449006423,.13518129740053308,.0313477341283922,
    .2880748288194013,.711835234241873,.00008993693872564,0,0,.8251046025104602);
inline const cv::Matx33d adapt(1.0479297925449969,.022946870601609652,-.05019226628920524,
    .02962780877005599,.9904344267538799,-.017073799063418826,
    -.009243040646204504,.015055191490298152,.7518742814281371);
inline const cv::Matx33d toPP=ppXyz.inv()*adapt*srgbXyz;
inline const cv::Matx33d fromPP=toPP.inv();
inline double endpoint(double amount,bool negative) {
    const double t=std::abs(amount),b=smooth(.75,1,t);
    return amount*(1+(negative?-.035:.065)*b);
}
inline RGB rgbGain(const RGB &p,double v,double s,bool curve) {
    const cv::Vec3f input{float(p[0]),float(p[1]),float(p[2])};
    const float hi=std::max({input[0],input[1],input[2]}),lo=std::min({input[0],input[1],input[2]});
    const float c=hi>1e-6f?std::clamp((hi-lo)/hi,0.f,1.f):0;
    const float lum=std::max(0.f,.2126f*input[0]+.7152f*input[1]+.0722f*input[2]);
    const float a=float(curve?endpoint(v,v<0):v);
    // The candidate positive endpoint exceeds 1 intentionally, without modifying the UI range.
    // Experimental RGB comparator only, not a production fallback.
    const float gain=std::max(0.f,float(1+s)*(1+a*(a<0 ? 1-.1f*c : 1-c)));
    const cv::Vec3f neutral(lum,lum,lum),q=neutral+(input-neutral)*gain;
    return {q[0],q[1],q[2]};
}
inline RGB perceptual(const RGB &p,double v,double s) {
    if(p[0]==p[1] && p[1]==p[2])return p;
    RGB q=lab(p); const double c=std::hypot(q[1],q[2]);
    if(c<1e-12)return p;
    // Explicit experimental heuristic, not a measured skin detector / not Lightroom's law.
    const double relative=std::clamp(c/std::max(std::abs(q[0]),.05),0.,1.);
    const double h=std::atan2(q[2],q[1])*180/pi;
    const double skin=1-.4*std::exp(-std::pow(wrap(h-50)/25,2))
        *smooth(.10,.35,q[0])*(1-smooth(.85,1.10,q[0]))*smooth(.005,.025,c);
    const double a=endpoint(v,v<0),w=v>=0?std::pow(1-relative,2)*skin:1-.1*relative;
    const double gain=std::max(0.,(1+s)*(1+a*w));
    q[1]*=gain;q[2]*=gain;return fromLab(q);
}
// Equations/parameters from LightCraft PR #598 at b9e312a748fde0b0e5b0eeb070a69b7ecf2df59f.
// Re-expressed for this diagnostic in linear sRGB via Bradford/linear ProPhoto D50.
inline RGB pr598(const RGB &input,double amount,double saturation) {
    RGB p=toPP*input;
    if(amount!=0) {
        const RGB h=hsv(p);
        if(h[2]>0 && h[1]>0) {
            const double rest=std::max(1-h[1],0.),fade=std::min(h[1]/.3,1.);
            double exponent,dv;
            if(amount>0) {
                const double skin=1-.391*std::exp(-std::pow(wrap(h[0]-20)/35,2));
                exponent=amount*.751*std::pow(rest,.736)*skin;
                dv=amount*.15*skin*fade;
            } else {
                exponent=amount*1.288*std::pow(rest,.323);
                dv=amount*.608*std::pow(std::min(h[1],1.),.263)*fade;
            }
            p=fromHsv(h[0],std::min(h[1]*std::exp(exponent),std::max(h[1],1.)),h[2]*std::exp2(dv));
        }
    }
    if(saturation!=0) {
        const double lum=p.dot(RGB(ppXyz(1,0),ppXyz(1,1),ppXyz(1,2)));
        const double f=saturation>0 ? 1+saturation*.927*std::pow(1-std::clamp(hsv(p)[1],0.,1.),.076)
                                    : std::max(1+saturation,0.);
        p=RGB(lum,lum,lum)+(p-RGB(lum,lum,lum))*f;
    }
    return fromPP*p;
}
inline RGB apply(const RGB &p,const std::string &model,double v,double s) {
    if(v==0 && s==0)return p; // exact identity, no matrix round-trip
    if(model=="current")return RGB(Vibrance::apply(cv::Vec3f(p),v,s));
    if(model=="A1_endpoint_rgb")return rgbGain(p,v,s,true);
    if(model=="A2_adaptive_oklch")return perceptual(p,v,s);
    if(model=="B_pr598_prophoto")return pr598(p,v,s);
    throw std::invalid_argument("Unknown study model");
}
}
