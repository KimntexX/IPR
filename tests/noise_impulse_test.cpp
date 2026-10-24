#include "noise_impulse_filter.h"
#include <iostream>
#include <stdexcept>

int main()
{
    bool ok=true;
    const auto check=[&](bool value,const char *message) { if(!value) {std::cerr<<message<<'\n';ok=false;} };
    const cv::Mat flat(16,16,CV_32F,cv::Scalar(.25)), sigma(flat.size(),CV_32F,cv::Scalar(.01));
    check(cv::norm(NoiseImpulseStudy::correct(flat,sigma,6).plane,flat,cv::NORM_INF)==0,"flat identity");
    cv::Mat impulse=flat.clone(); impulse.at<float>(8,8)=.8f;
    const auto result=NoiseImpulseStudy::correct(impulse,sigma,6);
    check(result.changed.at<uchar>(8,8)==255 && result.plane.at<float>(8,8)==.25f,"isolated impulse removed");
    check(impulse.at<float>(8,8)==.8f,"input untouched");
    cv::Mat line=flat.clone(); line.row(8).setTo(.8);
    check(cv::norm(NoiseImpulseStudy::correct(line,sigma,6).plane,line,cv::NORM_INF)==0,"thin line directional support");
    bool rejected=false;
    try { NoiseImpulseStudy::correct({},sigma,6); } catch(const std::invalid_argument &) {rejected=true;}
    check(rejected,"invalid inputs rejected");
    // A real one-pixel highlight and an impulse have the same observation: record
    // this limitation rather than claiming that the filter knows their origin.
    const auto controls=NoiseImpulseStudy::syntheticControls();
    check(controls.size()==126,"complete fixed-seed synthetic controls");
    return ok?0:1;
}
