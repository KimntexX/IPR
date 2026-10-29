#include "CameraDenoise.h"
#include <opencv2/core.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {
int failures = 0, checks = 0;
void check(bool condition, const char *message)
{
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
bool bits(const cv::Mat &a, const cv::Mat &b)
{
    if (a.size() != b.size() || a.type() != b.type()) return false;
    for (int y = 0; y < a.rows; ++y)
        if (std::memcmp(a.ptr(y), b.ptr(y), std::size_t(a.cols) * a.elemSize()) != 0) return false;
    return true;
}
cv::Mat field(int width = 128, int height = 128)
{
    cv::Mat result(height, width, CV_32FC3); cv::RNG random(0x6a91);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const float shared = float(random.gaussian(.007));
        result.at<cv::Vec3f>(y, x) = {
            -.01f + shared + float(random.gaussian(.008)),
            .002f + shared + float(random.gaussian(.008)),
            1.2f + shared + float(random.gaussian(.008))};
    }
    return result;
}
cv::Vec3d variance(const cv::Mat &m)
{
    cv::Scalar mean, stddev; cv::meanStdDev(m, mean, stddev);
    return {stddev[0] * stddev[0], stddev[1] * stddev[1], stddev[2] * stddev[2]};
}
cv::Vec3f axes(const cv::Vec3f &v)
{
    return {.25f*v[0]+.5f*v[1]+.25f*v[2], .5f*(v[0]-v[2]), v[1]-.5f*(v[0]+v[2])};
}
std::size_t peakBytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info{}; info.cb=sizeof(info);
    return GetProcessMemoryInfo(GetCurrentProcess(),&info,sizeof(info)) ? std::size_t(info.PeakWorkingSetSize) : 0;
#else
    struct rusage info{};
    if(getrusage(RUSAGE_SELF,&info)!=0) return 0;
#ifdef __APPLE__
    return std::size_t(info.ru_maxrss);
#else
    return std::size_t(info.ru_maxrss)*1024;
#endif
#endif
}
}

int main(int argc,char **argv)
{
    using namespace CameraDenoise;
    if(argc>1) {
        try {
            if(argc!=4||std::string(argv[1])!="--benchmark")return 2;
            const int width=std::stoi(argv[2]),height=std::stoi(argv[3]);
            if(width<8||height<8||width>12000||height>12000||double(width)*height>60000000)return 2;
            cv::setNumThreads(1);
            auto image=field(width,height); const auto before=variance(image);
            const auto meanBefore=cv::mean(image);
            std::cout<<"prepared "<<width<<'x'<<height<<'\n'<<std::flush;
            const auto start=std::chrono::steady_clock::now();
            const bool ok=apply(image,{100,100},{});
            const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            const auto peak=peakBytes(); const auto after=variance(image); const auto meanAfter=cv::mean(image);
            std::cout<<"benchmark_ms "<<elapsed<<" peak_bytes "<<peak<<" max_workers "<<workerThreads()<<" success "<<ok<<'\n';
            std::cout<<"variance_ratios "<<after[0]/before[0]<<' '<<after[1]/before[1]<<' '<<after[2]/before[2]<<'\n';
            std::cout<<"mean_delta "<<meanAfter[0]-meanBefore[0]<<' '<<meanAfter[1]-meanBefore[1]<<' '<<meanAfter[2]-meanBefore[2]<<'\n';
            return ok&&cv::checkRange(image)?0:1;
        }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}
    }
    check(workerThreads() >= 1 && workerThreads() <= 8, "bounded worker pool");
    {
        auto image = field(); const auto original = image.clone(); auto *pointer = image.data;
        Context unused; unused.divisor = 0; unused.gains[0] = std::numeric_limits<double>::quiet_NaN();
        check(apply(image, {}, unused, []() -> bool { throw std::runtime_error("zero must not call"); }), "zero forces bypass Context/callback");
        check(image.data == pointer && bits(image, original), "zero forces bitwise identity and same allocation");
        cv::Mat empty, wrong(2, 2, CV_16UC3);
        check(!apply(empty, {}, {}) && !apply(wrong, {}, {}), "invalid matrix rejected even at zero");
        check(!apply(image, {101, 0}, {}), "invalid setting not silently clamped");
    }
    {
        cv::Mat image(16, 19, CV_32FC3, cv::Scalar(-.02f, .4f, 1.3f)); const auto original = image.clone();
        check(apply(image, {100, 100}, {}), "constant extended field accepted");
        check(bits(image, original), "no measured noise leaves constants exact");
        cv::Mat tiny(1, 1, CV_32FC3, cv::Scalar(-.1f, .2f, 2.f)); const auto before = tiny.clone();
        check(apply(tiny, {100, 100}, {}) && bits(tiny, before), "single pixel identity");
    }
    {
        const auto original = field(); auto image = original.clone(); const auto alias = image;
        check(apply(image, {100, 100}, {}), "signed correlated noisy field completes");
        check(cv::checkRange(image) && image.type() == CV_32FC3, "finite float output");
        check(bits(alias, original), "aliased immutable source is never edited");
        check(image.data != alias.data && !bits(image, original), "nonzero measured noise actually processed");
        const auto before = variance(original), after = variance(image);
        std::cout << "variance ratios " << after[0]/before[0] << ' ' << after[1]/before[1] << ' ' << after[2]/before[2] << '\n';
        check(after[0] < .9*before[0] && after[1] < .9*before[1] && after[2] < .9*before[2], "noise reduced on known constant truth");
        const auto a = cv::mean(original), b = cv::mean(image);
        check(std::abs(a[0]-b[0]) < 2e-7 && std::abs(a[1]-b[1]) < 2e-7 && std::abs(a[2]-b[2]) < 2e-7, "channel DC preserved");
        check(image.at<cv::Vec3f>(0,0)[0] < 0 && image.at<cv::Vec3f>(0,0)[2] > 1, "no lower/upper clamp");
        auto repeated = original.clone();
        check(apply(repeated, {100, 100}, {}) && bits(repeated, image), "deterministic replay, not cumulative");
        double last = before[0];
        for (int amount : {25, 50, 75, 100}) {
            auto current = original.clone();
            check(apply(current, {amount, amount}, {}), "intermediate force completes");
            const double currentVariance = variance(current)[0];
            check(currentVariance < last, "progressive force on fixed noisy field"); last = currentVariance;
        }
    }
    {
        const auto original = field();
        for (bool chromaOnly : {false, true}) {
            auto image = original.clone();
            check(apply(image, chromaOnly ? Settings{0, 100} : Settings{100, 0}, {}), "independent axes complete");
            double error = 0;
            for (int y=0; y<image.rows; ++y) for (int x=0; x<image.cols; ++x) {
                const auto difference = axes(image.at<cv::Vec3f>(y,x))-axes(original.at<cv::Vec3f>(y,x));
                error = std::max(error, chromaOnly ? double(std::abs(difference[0])) : double(std::max(std::abs(difference[1]),std::abs(difference[2]))));
            }
            check(error < 5e-7, "inactive intensity/opponent axes preserved");
        }
    }
    {
        const auto original = field();
        for (int kind = 0; kind < 3; ++kind) {
            auto image = original.clone();
            image.at<cv::Vec3f>(2, 3)[1] = kind==0 ? std::numeric_limits<float>::quiet_NaN() :
                (kind==1 ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
            const auto before = image.clone();
            check(!apply(image, {50,50}, {}) && bits(image,before), "nonfinite rejected atomically");
        }
        for (int kind = 0; kind < 6; ++kind) {
            auto image = original.clone(); Context context;
            if (kind==0) context.gains[0]=0;
            if (kind==1) context.divisor=0;
            if (kind==2) context.pixelScale=0;
            if (kind==3) context.pixelScale=1.2;
            if (kind==4) context.iso=-1;
            if (kind==5) context.pattern={{{1,1},{1,1}}};
            check(!apply(image,{50,50},context) && bits(image,original), "invalid Context rejected atomically");
        }
        auto image=original.clone();
        check(!apply(image,{100,100},{},[]{return true;}) && bits(image,original), "early cancellation leaves input unchanged");
        check(!apply(image,{100,100},{},[]()->bool{throw std::runtime_error("cancel exception");}) && bits(image,original), "callback exception leaves input unchanged");
    }
    {
        cv::Mat checker(128,128,CV_32FC3);
        for(int y=0;y<checker.rows;++y)for(int x=0;x<checker.cols;++x) {
            const float v=(x+y)%2 ? .1f : -.1f;
            checker.at<cv::Vec3f>(y,x)={v,v,v};
        }
        const auto before=checker.clone();
        check(apply(checker,{100,100},{}) && cv::norm(checker,before,cv::NORM_INF)<1e-6, "clean coherent checkerboard not classified as noise");
        cv::Mat edge(128,128,CV_32FC3,cv::Scalar(.2f,.3f,.4f));
        for(int y=0;y<edge.rows;++y)edge.at<cv::Vec3f>(y,64)={.5f,.6f,.7f};
        const auto clean=edge.clone();
        check(apply(edge,{100,100},{}) && cv::norm(edge,clean,cv::NORM_INF)<1e-6, "isolated clean fine line preserved");
        const auto original=field(); auto shifted=original.clone(), regular=original.clone();
        const cv::Vec3f offset(.2f,-.1f,.4f);
        for(int y=0;y<shifted.rows;++y)for(int x=0;x<shifted.cols;++x)shifted.at<cv::Vec3f>(y,x)+=offset;
        check(apply(shifted,{70,70},{})&&apply(regular,{70,70},{}), "shifted signed signal completes");
        double error=0;
        for(int y=0;y<shifted.rows;++y)for(int x=0;x<shifted.cols;++x)
            for(int c=0;c<3;++c)error=std::max(error,double(std::abs(shifted.at<cv::Vec3f>(y,x)[c]-regular.at<cv::Vec3f>(y,x)[c]-offset[c])));
        check(error<3e-6, "DC translation does not change the noise model appreciably");
    }
    {
        const auto original=field(520,300); auto image=original.clone();
        const auto caller=std::this_thread::get_id(); int callbacks=0; bool wrongThread=false;
        check(!apply(image,{100,100},{},[&]{wrongThread=wrongThread||std::this_thread::get_id()!=caller;return ++callbacks>1500;}) && bits(image,original), "late cancellation discards candidate and joins workers");
        check(!wrongThread, "cancellation only on caller thread");
        Context preview; preview.pixelScale=.25;
        image=original.clone(); check(apply(image,{50,50},preview) && cv::checkRange(image), "preview support accepted");
        cv::Mat parent=field(150,140), before=parent.clone(), roi=parent(cv::Rect(3,5,128,120));
        check(apply(roi,{50,50},{}) && bits(parent,before), "strided ROI source/parent untouched");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
