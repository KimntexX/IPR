#include "DetailProcessing.cpp" // Diagnostic TU: access the actual private helpers.
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

float median(std::vector<float> values) {
    auto middle = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}
cv::Mat plane(const cv::Mat &rgb) {
    cv::Mat out(rgb.rows, rgb.cols, CV_32F);
    for (int y=0; y<rgb.rows; ++y) for (int x=0; x<rgb.cols; ++x) {
        auto p=rgb.at<cv::Vec3f>(y,x);
        out.at<float>(y,x)=.25f*p[0]+.5f*p[1]+.25f*p[2];
    }
    return out;
}
float hhSigma(const cv::Mat &p) {
    std::vector<float> hh;
    for (int y=0; y<p.rows-1; y+=2) for (int x=0; x<p.cols-1; x+=2)
        hh.push_back((p.at<float>(y,x)-p.at<float>(y,x+1)-p.at<float>(y+1,x)+p.at<float>(y+1,x+1))*.5f);
    float center=median(hh);
    for (float &v:hh) v=std::abs(v-center);
    return median(hh)/.67448975f;
}
void run(const char *name, float signal, float sigma, bool colored) {
    cv::RNG rng(812);
    cv::Mat rgb(512,512,CV_32FC3);
    for(int y=0;y<rgb.rows;++y) for(int x=0;x<rgb.cols;++x) {
        float v=std::max(0.f,signal+static_cast<float>(rng.gaussian(sigma)));
        rgb.at<cv::Vec3f>(y,x)=colored ? cv::Vec3f(v,std::max(0.f,signal+static_cast<float>(rng.gaussian(sigma))),
                                                   std::max(0.f,signal+static_cast<float>(rng.gaussian(sigma)))) : cv::Vec3f(v,v,v);
    }
    cv::Mat source=plane(rgb), base,gx,gy,edge;
    cv::GaussianBlur(source,base,cv::Size(),.8,.8,cv::BORDER_REPLICATE);
    std::vector<float> residual;
    for(int y=0;y<source.rows;y+=4) for(int x=0;x<source.cols;x+=4)
        residual.push_back(std::abs(source.at<float>(y,x)-base.at<float>(y,x)));
    float estimate=std::max(.00025f,median(residual)/.67449f);
    cv::Sobel(source,gx,CV_32F,1,0,3);cv::Sobel(source,gy,CV_32F,0,1,3);cv::magnitude(gx,gy,edge);
    int protectedHalf=0,protectedFull=0;
    for(int y=0;y<edge.rows;++y) for(int x=0;x<edge.cols;++x) {
        float t=std::clamp((edge.at<float>(y,x)-1.5f*estimate)/(4.5f*estimate),0.f,1.f);
        float weight=t*t*(3-2*t);
        protectedHalf+=weight>=.5f; protectedFull+=weight>=.99f;
    }
    std::cout << name << " original_mean=" << cv::mean(source)[0] << " HH_sigma=" << hhSigma(source)
              << " estimated_sigma=" << estimate << " edge_protection_half_fraction=" << float(protectedHalf)/edge.total()
              << " edge_protection_full_fraction=" << float(protectedFull)/edge.total() << '\n';
    for(int amount:{1,5,10,25,50,75,100}) {
        auto out=rgb.clone();
        DetailProcessing::reduceNoise(out,{amount,0});
        auto p=plane(out);
        auto unclampedLuma=DetailProcessing::denoisePlane(source,edge,
                             DetailProcessing::estimateNoiseSigma(source),amount,false);
        std::cout << "  luminance=" << amount << " sigma_ratio=" << hhSigma(p)/hhSigma(source)
                  << " mean_ratio=" << cv::mean(p)[0]/cv::mean(source)[0]
                  << " pre_RGB_clamp_mean_ratio=" << cv::mean(unclampedLuma)[0]/cv::mean(source)[0] << '\n';
    }
}
int main() {
    run("gray_low_noise",.05f,.0001f,false);
    run("gray_noisy",.05f,.005f,false);
    run("dark_colored_noise",.005f,.006f,true);
}
