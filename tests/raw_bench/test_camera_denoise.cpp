#include "CameraDenoise.h"
#include "Pixels.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
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
bool bits(const ipr::Image &a, const ipr::Image &b)
{
    if (a.size() != b.size() || a.depth() != b.depth() || a.channels() != b.channels()) return false;
    for (int y = 0; y < a.height(); ++y)
        if (std::memcmp(a.row<std::uint8_t>(y), b.row<std::uint8_t>(y), std::size_t(a.width()) * a.pixelBytes()) != 0) return false;
    return true;
}
// A pixel, to read or to write.
const ipr::Vec3f &at(const ipr::Image &image, int y, int x) { return image.row<ipr::Vec3f>(y)[x]; }
ipr::Vec3f &at(ipr::Image &image, int y, int x) { return image.mutableRow<ipr::Vec3f>(y)[x]; }
// A seeded normal law, as the fields draw their noise.
struct Gaussian
{
    explicit Gaussian(unsigned seed) : random(seed) {}
    float operator()(double sigma) { return float(std::normal_distribution<double>(0.0, sigma)(random)); }
    std::mt19937 random;
};
ipr::Image filled(int width, int height, const ipr::Vec3f &value)
{
    ipr::Image image = ipr::Image::create({width, height}, ipr::Depth::F32, 3);
    ipr::fill(image, {double(value[0]), double(value[1]), double(value[2])});
    return image;
}
ipr::Image field(int width = 128, int height = 128)
{
    ipr::Image result = ipr::Image::create({width, height}, ipr::Depth::F32, 3); Gaussian gaussian(0x6a91);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const float shared = gaussian(.007);
        const float r = gaussian(.008), g = gaussian(.008), b = gaussian(.008);
        at(result, y, x) = {-.01f + shared + r, .002f + shared + g, 1.2f + shared + b};
    }
    return result;
}
ipr::Vec3d variance(const ipr::Image &m)
{
    std::vector<double> mean, stddev; ipr::meanAndDeviationOf(m, mean, stddev);
    return {stddev[0] * stddev[0], stddev[1] * stddev[1], stddev[2] * stddev[2]};
}
ipr::Vec3f axes(const ipr::Vec3f &v)
{
    return {.25f*v[0]+.5f*v[1]+.25f*v[2], .5f*(v[0]-v[2]), v[1]-.5f*(v[0]+v[2])};
}
double intensityVariance(const ipr::Image &m)
{
    ipr::Image intensity = ipr::Image::create(m.size(), ipr::Depth::F32, 1);
    for (int y = 0; y < m.height(); ++y) for (int x = 0; x < m.width(); ++x)
        intensity.mutableRow<float>(y)[x] = axes(at(m, y, x))[0];
    std::vector<double> mean, stddev; ipr::meanAndDeviationOf(intensity, mean, stddev);
    return stddev[0] * stddev[0];
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
            auto image=field(width,height); const auto before=variance(image);
            const auto meanBefore=ipr::meanOf(image);
            std::cout<<"prepared "<<width<<'x'<<height<<'\n'<<std::flush;
            const auto start=std::chrono::steady_clock::now();
            const bool ok=apply(image,100,{});
            const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            const auto peak=peakBytes(); const auto after=variance(image); const auto meanAfter=ipr::meanOf(image);
            std::cout<<"benchmark_ms "<<elapsed<<" peak_bytes "<<peak<<" max_workers "<<workerThreads()<<" success "<<ok<<'\n';
            std::cout<<"variance_ratios "<<after[0]/before[0]<<' '<<after[1]/before[1]<<' '<<after[2]/before[2]<<'\n';
            std::cout<<"mean_delta "<<meanAfter[0]-meanBefore[0]<<' '<<meanAfter[1]-meanBefore[1]<<' '<<meanAfter[2]-meanBefore[2]<<'\n';
            return ok&&ipr::allFinite(image)?0:1;
        }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}
    }
    check(workerThreads() >= 1 && workerThreads() <= 8, "bounded worker pool");
    {
        auto image = field(); const auto original = image.clone(); const auto *pointer = image.row<std::uint8_t>(0);
        Context unused; unused.iso = std::numeric_limits<double>::quiet_NaN();
        check(apply(image, 0, unused, []() -> bool { throw std::runtime_error("zero must not call"); }), "zero forces bypass Context/callback");
        check(image.row<std::uint8_t>(0) == pointer && bits(image, original), "zero forces bitwise identity and same allocation");
        ipr::Image empty, wrong = ipr::Image::create({2, 2}, ipr::Depth::U16, 3);
        check(!apply(empty, 0, {}) && !apply(wrong, 0, {}), "invalid matrix rejected even at zero");
        check(!apply(image, 101, {}), "invalid setting not silently clamped");
    }
    {
        auto image = filled(19, 16, {-.02f, .4f, 1.3f}); const auto original = image.clone();
        const auto sourceAlias = image.share();
        check(apply(image, 100, {}), "constant extended field accepted");
        check(bits(image, original), "no measured noise leaves constants exact");
        check(image.row<std::uint8_t>(0) != sourceAlias.row<std::uint8_t>(0) && bits(sourceAlias, original), "nonzero abstention detaches immutable source for downstream optics");
        auto tiny = filled(1, 1, {-.1f, .2f, 2.f}); const auto before = tiny.clone();
        check(apply(tiny, 100, {}) && bits(tiny, before), "single pixel identity");
    }
    {
        const auto original = field(); auto image = original.clone(); const auto alias = image.share();
        check(apply(image, 100, {}), "signed correlated noisy field completes");
        check(ipr::allFinite(image) && image.depth() == ipr::Depth::F32 && image.channels() == 3, "finite float output");
        check(bits(alias, original), "aliased immutable source is never edited");
        check(image.row<std::uint8_t>(0) != alias.row<std::uint8_t>(0) && !bits(image, original), "nonzero measured noise actually processed");
        const double before = intensityVariance(original), after = intensityVariance(image);
        std::cout << "intensity variance ratio " << after/before << '\n';
        check(after < .9*before, "noise reduced on known constant truth");
        const auto a = ipr::meanOf(original), b = ipr::meanOf(image);
        check(std::abs(a[0]-b[0]) < 2e-7 && std::abs(a[1]-b[1]) < 2e-7 && std::abs(a[2]-b[2]) < 2e-7, "channel DC preserved");
        check(at(image, 0,0)[0] < 0 && at(image, 0,0)[2] > 1, "no lower/upper clamp");
        auto repeated = original.clone();
        check(apply(repeated, 100, {}) && bits(repeated, image), "deterministic replay, not cumulative");
        double last = before;
        for (int amount : {25, 50, 75, 100}) {
            auto current = original.clone();
            check(apply(current, amount, {}), "intermediate force completes");
            const double currentVariance = intensityVariance(current);
            check(currentVariance < last, "progressive force on fixed noisy field"); last = currentVariance;
        }
    }
    {
        const auto original = field();
        auto image = original.clone();
        check(apply(image, 100, {}), "intensity denoise completes");
        double error = 0;
        for (int y=0; y<image.height(); ++y) for (int x=0; x<image.width(); ++x) {
            const auto difference = axes(at(image, y,x))-axes(at(original, y,x));
            error = std::max(error, double(std::max(std::abs(difference[1]),std::abs(difference[2]))));
        }
        check(error < 5e-7, "opponent axes preserved");
    }
    {
        const auto original = field();
        for (int kind = 0; kind < 3; ++kind) {
            auto image = original.clone();
            at(image, 2, 3)[1] = kind==0 ? std::numeric_limits<float>::quiet_NaN() :
                (kind==1 ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
            const auto before = image.clone();
            check(!apply(image, 50, {}) && bits(image,before), "nonfinite rejected atomically");
        }
        for (int kind = 0; kind < 4; ++kind) {
            auto image = original.clone(); Context context;
            if (kind==0) context.pixelScale=0;
            if (kind==1) context.pixelScale=1.2;
            if (kind==2) context.iso=-1;
            if (kind==3) context.iso=std::numeric_limits<double>::quiet_NaN();
            check(!apply(image,50,context) && bits(image,original), "invalid Context rejected atomically");
        }
        auto image=original.clone();
        check(!apply(image,100,{},[]{return true;}) && bits(image,original), "early cancellation leaves input unchanged");
        check(!apply(image,100,{},[]()->bool{throw std::runtime_error("cancel exception");}) && bits(image,original), "callback exception leaves input unchanged");
    }
    {
        auto checker = ipr::Image::create({128, 128}, ipr::Depth::F32, 3);
        for(int y=0;y<checker.height();++y)for(int x=0;x<checker.width();++x) {
            const float v=(x+y)%2 ? .1f : -.1f;
            at(checker, y,x)={v,v,v};
        }
        const auto before=checker.clone();
        Context noWitness; int inventedFallback=0,rejectedLevels=0;
        noWitness.noiseModelObserver=[&](const NoiseModelSummary &m){
            if(m.candidates==0){++rejectedLevels;inventedFallback+=m.globalFallbackAvailable;}
        };
        check(apply(checker,100,noWitness) && ipr::largestDifference(checker,before)<1e-6, "clean coherent checkerboard not classified as noise");
        check(rejectedLevels>0&&inventedFallback==0,"no global fallback is invented on scales where every witness is rejected");
        auto edge = filled(128, 128, {.2f, .3f, .4f});
        for(int y=0;y<edge.height();++y)at(edge, y,64)={.5f,.6f,.7f};
        const auto clean=edge.clone();
        check(apply(edge,100,{}) && ipr::largestDifference(edge,clean)<1e-6, "isolated clean fine line preserved");
        const auto original=field(); auto shifted=original.clone(), regular=original.clone();
        const ipr::Vec3f offset(.2f,-.1f,.4f);
        for(int y=0;y<shifted.height();++y)for(int x=0;x<shifted.width();++x)at(shifted, y,x)+=offset;
        check(apply(shifted,70,{})&&apply(regular,70,{}), "shifted signed signal completes");
        double error=0;
        for(int y=0;y<shifted.height();++y)for(int x=0;x<shifted.width();++x)
            for(int c=0;c<3;++c)error=std::max(error,double(std::abs(at(shifted, y,x)[c]-at(regular, y,x)[c]-offset[c])));
        check(error<3e-6, "DC translation does not change the noise model appreciably");
    }
    {
        auto image = ipr::Image::create({576, 192}, ipr::Depth::F32, 3);Gaussian gaussian(0x914c);
        const std::array<float,3> levels{.04f,.25f,.7f},sigmas{.003f,.012f,.04f};
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            const int band=x/192;
            for(int c=0;c<3;++c)at(image, y,x)[c]=levels[size_t(band)]+gaussian(sigmas[size_t(band)]);
        }
        const auto before=image.clone();
        Context context; int bandModels=0; bool lowFloor=true;
        context.noiseModelObserver=[&](const NoiseModelSummary &m) {
            if(m.nodes.size()>1) {
                ++bandModels;
                for(int c=0;c<3;++c)lowFloor=lowFloor&&m.nodes.front().second[c]+1e-8f>=m.globalSigma[c];
            }
        };
        check(apply(image,100,context),"signal-dependent noisy bands complete");
        check(bandModels==3&&lowFloor,"darkest witnessed knot never falls below the pooled variance floor");
        for(int band=0;band<3;++band) {
            const ipr::Recti roi(band*192+64,32,64,128);
            const double a=intensityVariance(before.copy(roi)),b=intensityVariance(image.copy(roi));
            std::cout<<"heteroscedastic band "<<band<<" variance ratio "<<b/a<<'\n';
            check(b<.5*a,"known flat signal denoised in every brightness band");
        }
        auto clean = ipr::Image::create({576, 192}, ipr::Depth::F32, 3);
        for(int y=0;y<clean.height();++y)for(int x=0;x<clean.width();++x)at(clean, y,x)=ipr::Vec3f::all(levels[size_t(x/192)]);
        const auto truth=clean.clone();
        check(apply(clean,100,{})&&ipr::largestDifference(clean,truth)<1e-6,"clean brightness steps not learned as noise");
    }
    {
        auto original=field(128,128);
        const ipr::Vec3f gains(2,1,3);
        for(int y=0;y<original.height();++y)for(int x=0;x<original.width();++x)
            at(original, y,x)=at(original, y,x).mul(gains);
        auto image=original.clone();
        check(apply(image,100,{}),"already balanced unequal channels accepted");
        double error=0, movement=0;
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            const auto delta=at(image, y,x)-at(original, y,x);
            error=std::max(error,double(std::max(std::abs(delta[0]-delta[1]),std::abs(delta[2]-delta[1]))));
            movement=std::max(movement,double(std::sqrt(delta.dot(delta))));
        }
        check(movement>1e-5,"balanced axis test performs a real correction");
        check(error<5e-7,"luma delta neutral in already balanced RGB");
    }
    {
        const auto original=field(520,300); auto image=original.clone();
        const auto caller=std::this_thread::get_id(); int callbacks=0; bool wrongThread=false;
        check(!apply(image,100,{},[&]{wrongThread=wrongThread||std::this_thread::get_id()!=caller;return ++callbacks>1500;}) && bits(image,original), "late cancellation discards candidate and joins workers");
        check(!wrongThread, "cancellation only on caller thread");
        Context preview; preview.pixelScale=.25;
        image=original.clone(); check(apply(image,50,preview) && ipr::allFinite(image), "preview support accepted");
        const auto parent=field(150,140), before=parent.clone();
        auto roi=parent.view(ipr::Recti(3,5,128,120));
        check(apply(roi,50,{}) && bits(parent,before), "strided ROI source/parent untouched");
    }
    {
        const auto original=field(128,128); auto plain=original.clone(), observed=original.clone();
        Context context; int count=0; bool wrongThread=false, invalid=false;
        const auto caller=std::this_thread::get_id();
        context.noiseModelObserver=[&](const NoiseModelSummary &s) {
            ++count; wrongThread=wrongThread||std::this_thread::get_id()!=caller;
            invalid=invalid||s.flatCandidates>s.candidates||s.level<0||s.level>=3;
            if(s.supported)invalid=invalid||!(s.low<s.high)||s.nodes.empty();
        };
        check(apply(plain,100,{})&&apply(observed,100,context),"noise model observation completes");
        check(bits(plain,observed)&&count==3&&!wrongThread&&!invalid,"observer is caller-only and leaves render bit-identical");
        context.noiseModelObserver=[](const NoiseModelSummary &){throw std::runtime_error("observer failed");};
        auto image=original.clone();
        check(!apply(image,100,context)&&bits(image,original),"observer exception leaves input unchanged");
    }
    {
        auto image = ipr::Image::create({384, 384}, ipr::Depth::F32, 3);Gaussian gaussian(0x29a0);
        const auto ramp=[](int x,int y){return .05f+.0012f*x+.0013f*y;};
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            const float i=ramp(x,y)+gaussian(.004);
            const float co=gaussian(.004),cg=gaussian(.004);
            at(image, y,x)={i+co-.5f*cg,i+.5f*cg,i-co-.5f*cg};
        }
        const auto before=image.clone();Context context;int unsupported=0;
        context.noiseModelObserver=[&](const NoiseModelSummary &m){unsupported+=!m.supported&&m.candidates>0&&m.globalFallbackAvailable;};
        check(apply(image,100,context),"pooled fallback on a noisy smooth ramp completes");
        check(unsupported==3,"ramp has no flat conditional support on every scale");
        double input=0,output=0;
        for(int y=128;y<256;++y)for(int x=128;x<256;++x) {
            const double a=axes(at(before, y,x))[0]-ramp(x,y),b=axes(at(image, y,x))[0]-ramp(x,y);
            input+=a*a;output+=b*b;
        }
        std::cout<<"unsupported_ramp_intensity_mse_ratio "<<output/input<<'\n';
        check(output<.8*input,"missing conditional model uses the existing pooled witnesses");
    }
    {
        auto image = ipr::Image::create({512, 512}, ipr::Depth::F32, 3);Gaussian gaussian(0x29a1);
        const auto dark=[](int x,int y){return x>=182&&x<218&&y>=182&&y<218;};
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            // The dark region is between the 5x5 witness cores; the pooled
            // simulated read noise is identical in the dark and in the surround.
            const float i=(dark(x,y)?.01f:.2f)+gaussian(.004);
            const float co=gaussian(.004),cg=gaussian(.004);
            at(image, y,x)={i+co-.5f*cg,i+.5f*cg,i-co-.5f*cg};
        }
        const auto before=image.clone();Context context;int aboveDark=0;
        context.noiseModelObserver=[&](const NoiseModelSummary &m){aboveDark+=m.supported&&m.low>.1f&&m.globalFallbackAvailable;};
        check(apply(image,100,context),"pooled fallback outside witnessed signal range completes");
        check(aboveDark==3,"dark patch lies below every conditional support range");
        double input=0,output=0;
        for(int y=190;y<210;++y)for(int x=190;x<210;++x) {
            const double a=axes(at(before, y,x))[0]-.01,b=axes(at(image, y,x))[0]-.01;
            input+=a*a;output+=b*b;
        }
        std::cout<<"unwitnessed_dark_patch_intensity_mse_ratio "<<output/input<<'\n';
        check(output<.8*input,"out-of-domain dark patch does not silently receive zero denoise");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
