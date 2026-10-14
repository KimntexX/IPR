#include "colour_model_study.h"
#include <iostream>
#include <stdexcept>

void require(bool ok,const char *message) { if(!ok)throw std::runtime_error(message); }
int main() {
    using namespace ColourStudy;
    try {
        for(const RGB p:{RGB(.5,.2,.1),RGB(.1,.3,.7),RGB(0,0,0),RGB(.18,.18,.18),
                RGB(2,.4,.1),RGB(-.1,.2,.5)}) {
            require(cv::norm(fromPP*(toPP*p)-p)<1e-10,"ProPhoto round trip");
            require(cv::norm(fromLab(lab(p))-p)<1e-6,"Oklab round trip including signed/HDR");
            for(const std::string m:{"current","A1_endpoint_rgb","A2_adaptive_oklch","B_pr598_prophoto"}) {
                require(cv::norm(apply(p,m,0,0)-p)==0,"Exact neutral identity");
                for(int i=-100;i<=100;++i) {
                    const auto q=apply(p,m,i/100.,0);
                    for(int c=0;c<3;++c)require(std::isfinite(q[c]),"Finite vibrance on SDR/HDR/signed colors");
                    const auto r=apply(p,m,0,i/100.);
                    for(int c=0;c<3;++c)require(std::isfinite(r[c]),"Finite saturation on SDR/HDR/signed colors");
                }
            }
            for(double v:{-1.,-.5,-.25,.25,.5,1.})
                require(cv::norm(apply(p,"current",v,0)-apply(RGB(cv::Vec3f(p)),"A2_adaptive_oklch",v,0))<2e-6,
                        "Production must match the studied A1/A2 candidate");
            for(double v:{-.9,-.5,.5,.9}) {
                const auto q=apply(p,"A2_adaptive_oklch",v,0);
                require(std::abs(lab(q)[0]-lab(p)[0])<1e-6,"OKLCh candidate must preserve L before output");
            }
        }
        double previous=0;
        for(int i=0;i<=1000;++i) {
            const double effective=-endpoint(-i/1000.,true);
            require(effective>=previous && effective>=0 && effective<=1,"Negative endpoint mapping must be monotone and bounded");
            previous=effective;
        }
        require(std::abs(endpoint(-1,true)+.965)<1e-12,"Negative endpoint strength");
        require(std::abs(endpoint(1,false)-1.065)<1e-12,"Positive endpoint strength");
        for(double gray:{0.,.001,.18,.5,1.,4.})for(double v:{-1.,-.5,.5,1.})
            for(const std::string m:{"current","A1_endpoint_rgb","A2_adaptive_oklch","B_pr598_prophoto"})
                if(cv::norm(apply(RGB(gray,gray,gray),m,v,0)-RGB(gray,gray,gray))>=2e-6) {
                    std::cerr<<"Grey failed: "<<m<<" gray="<<gray<<" v="<<v<<" delta="
                             <<cv::norm(apply(RGB(gray,gray,gray),m,v,0)-RGB(gray,gray,gray))<<'\n';
                    throw std::runtime_error("Grey/black invariant within numerical precision");
                }
        const RGB p=fromPP*RGB(.5,.2,.1);
        for(double s:{-1.,-.5,.5,1.}) {
            const RGB q=toPP*apply(p,"B_pr598_prophoto",0,s);
            const RGB original=toPP*p,w(ppXyz(1,0),ppXyz(1,1),ppXyz(1,2));
            require(std::abs(q.dot(w)-original.dot(w))<1e-10,"PR saturation preserves D50 ProPhoto Y");
        }
        // Values independently evaluated from the PR equations, in its own ProPhoto coordinates.
        const RGB blue=fromPP*fromHsv(200,.5,.4);
        const RGB result=hsv(toPP*apply(blue,"B_pr598_prophoto",-1,0));
        require(std::abs(result[1]-.5*std::exp(-1.288*std::pow(.5,.323)))<1e-10,"PR negative saturation exponent");
        require(std::abs(result[2]-.4*std::exp2(-.608*std::pow(.5,.263)))<1e-10,"PR negative value coupling");
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
