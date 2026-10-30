#include "CameraDenoise.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
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
    check(identity({}) && !identity({1,0}) && !identity({0,1}), "identity helper matches both controls");
    {
        auto image = field(); const auto original = image.clone(); auto *pointer = image.data;
        Context unused; unused.iso = std::numeric_limits<double>::quiet_NaN();
        check(apply(image, {}, unused, []() -> bool { throw std::runtime_error("zero must not call"); }), "zero forces bypass Context/callback");
        check(image.data == pointer && bits(image, original), "zero forces bitwise identity and same allocation");
        cv::Mat empty, wrong(2, 2, CV_16UC3);
        check(!apply(empty, {}, {}) && !apply(wrong, {}, {}), "invalid matrix rejected even at zero");
        check(!apply(image, {101, 0}, {}), "invalid setting not silently clamped");
    }
    {
        cv::Mat image(16, 19, CV_32FC3, cv::Scalar(-.02f, .4f, 1.3f)); const auto original = image.clone();
        const auto sourceAlias = image;
        check(apply(image, {100, 100}, {}), "constant extended field accepted");
        check(bits(image, original), "no measured noise leaves constants exact");
        check(image.data != sourceAlias.data && bits(sourceAlias, original), "nonzero abstention detaches immutable source for downstream optics");
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
        for (int kind = 0; kind < 4; ++kind) {
            auto image = original.clone(); Context context;
            if (kind==0) context.pixelScale=0;
            if (kind==1) context.pixelScale=1.2;
            if (kind==2) context.iso=-1;
            if (kind==3) context.iso=std::numeric_limits<double>::quiet_NaN();
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
        Context noWitness; int inventedFallback=0,rejectedLevels=0;
        noWitness.noiseModelObserver=[&](const NoiseModelSummary &m){
            if(m.candidates==0){++rejectedLevels;inventedFallback+=m.globalFallbackAvailable;}
        };
        check(apply(checker,{100,100},noWitness) && cv::norm(checker,before,cv::NORM_INF)<1e-6, "clean coherent checkerboard not classified as noise");
        check(rejectedLevels>0&&inventedFallback==0,"no global fallback is invented on scales where every witness is rejected");
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
        cv::Mat chroma(256,256,CV_32F);cv::RNG random(0x145a);
        random.fill(chroma,cv::RNG::NORMAL,0.,.08);
        cv::GaussianBlur(chroma,chroma,{},6);
        cv::Mat image(chroma.size(),CV_32FC3);
        for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x) {
            const float co=chroma.at<float>(y,x);
            image.at<cv::Vec3f>(y,x)={.25f+co,.25f,.25f-co};
        }
        const auto before=variance(image);
        check(apply(image,{0,100},{}),"broad synthetic chroma field completes");
        const auto after=variance(image);
        check(after[0]<before[0]&&after[2]<before[2],"broad chroma reduced on known flat truth");
        double error=0;
        for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x)
            error=std::max(error,double(std::abs(axes(image.at<cv::Vec3f>(y,x))[0]-.25f)));
        check(error<5e-7,"coarse chroma scales do not smooth camera intensity");
    }
    {
        cv::Mat image(192,576,CV_32FC3);cv::RNG random(0x914c);
        const std::array<float,3> levels{.04f,.25f,.7f},sigmas{.003f,.012f,.04f};
        for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x) {
            const int band=x/192;
            for(int c=0;c<3;++c)image.at<cv::Vec3f>(y,x)[c]=levels[size_t(band)]+float(random.gaussian(sigmas[size_t(band)]));
        }
        const auto before=image.clone();
        Context context; int bandModels=0; bool lowFloor=true;
        context.noiseModelObserver=[&](const NoiseModelSummary &m) {
            if(m.nodes.size()>1) {
                ++bandModels;
                for(int c=0;c<3;++c)lowFloor=lowFloor&&m.nodes.front().second[c]+1e-8f>=m.globalSigma[c];
            }
        };
        check(apply(image,{100,100},context),"signal-dependent noisy bands complete");
        check(bandModels==5&&lowFloor,"darkest witnessed knot never falls below the pooled variance floor");
        for(int band=0;band<3;++band) {
            const cv::Rect roi(band*192+64,32,64,128);
            const auto a=variance(before(roi)),b=variance(image(roi));
            std::cout<<"heteroscedastic band "<<band<<" variance ratio "<<b[0]/a[0]<<'\n';
            check(b[0]<.5*a[0]&&b[1]<.5*a[1]&&b[2]<.5*a[2],"known flat signal denoised in every brightness band");
        }
        cv::Mat clean(192,576,CV_32FC3);
        for(int y=0;y<clean.rows;++y)for(int x=0;x<clean.cols;++x)clean.at<cv::Vec3f>(y,x)=cv::Vec3f::all(levels[size_t(x/192)]);
        const auto truth=clean.clone();
        check(apply(clean,{100,100},{})&&cv::norm(clean,truth,cv::NORM_INF)<1e-6,"clean brightness steps not learned as noise");
    }
    {
        auto original=field(128,128);
        const cv::Vec3f gains(2,1,3);
        for(int y=0;y<original.rows;++y)for(int x=0;x<original.cols;++x)
            original.at<cv::Vec3f>(y,x)=original.at<cv::Vec3f>(y,x).mul(gains);
        for (bool chromaOnly : {false, true}) {
            auto image=original.clone();
            check(apply(image,chromaOnly?Settings{0,100}:Settings{100,0},{}),"already balanced unequal channels accepted");
            double error=0, movement=0;
            for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x) {
                const auto delta=image.at<cv::Vec3f>(y,x)-original.at<cv::Vec3f>(y,x);
                const float invariant=chromaOnly ? .25f*delta[0]+.5f*delta[1]+.25f*delta[2] :
                    std::max(std::abs(delta[0]-delta[1]),std::abs(delta[2]-delta[1]));
                error=std::max(error,double(std::abs(invariant)));
                movement=std::max(movement,double(cv::norm(delta)));
            }
            check(movement>1e-5,"balanced axis test performs a real correction");
            check(error<5e-7,chromaOnly?"chroma preserves balanced camera intensity":"luma delta neutral in already balanced RGB");
        }
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
    {
        const auto original=field(128,128); auto plain=original.clone(), observed=original.clone();
        Context context; int count=0; bool wrongThread=false, invalid=false;
        const auto caller=std::this_thread::get_id();
        context.noiseModelObserver=[&](const NoiseModelSummary &s) {
            ++count; wrongThread=wrongThread||std::this_thread::get_id()!=caller;
            invalid=invalid||s.flatCandidates>s.candidates||s.level<0||s.level>=5;
            if(s.supported)invalid=invalid||!(s.low<s.high)||s.nodes.empty();
        };
        check(apply(plain,{100,100},{})&&apply(observed,{100,100},context),"noise model observation completes");
        check(bits(plain,observed)&&count==5&&!wrongThread&&!invalid,"observer is caller-only and leaves render bit-identical");
        context.noiseModelObserver=[](const NoiseModelSummary &){throw std::runtime_error("observer failed");};
        auto image=original.clone();
        check(!apply(image,{100,100},context)&&bits(image,original),"observer exception leaves input unchanged");
    }
    {
        // Known achromatic texture: chroma contains ONLY additive simulated
        // fine noise and 3/6-pixel-correlated blotches, not a colour detail.
        constexpr int size=512;
        cv::Mat co(size,size,CV_32F),cg(size,size,CV_32F);cv::RNG random(0x91f6);
        random.fill(co,cv::RNG::NORMAL,0.,.04);random.fill(cg,cv::RNG::NORMAL,0.,.04);
        cv::GaussianBlur(co,co,{},3);cv::GaussianBlur(cg,cg,{},6);
        cv::Mat image(size,size,CV_32FC3);
        for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
            float intensity=x<171?.06f:(x<341?.15f:.4f);
            intensity+=float(random.gaussian(.0005));
            if(x>=176&&x<336&&y>=128&&y<384)intensity+=float(random.gaussian(.04));
            const float a=co.at<float>(y,x)+float(random.gaussian(.001));
            const float b=cg.at<float>(y,x)+float(random.gaussian(.001));
            image.at<cv::Vec3f>(y,x)={intensity+a-.5f*b,intensity+.5f*b,intensity-a-.5f*b};
        }
        const auto before=image.clone();
        check(apply(image,{0,100},{}),"chroma blotches on known achromatic texture complete");
        double input=0,output=0,intensityError=0;
        for(int y=208;y<304;++y)for(int x=224;x<288;++x) {
            const auto a=axes(before.at<cv::Vec3f>(y,x)),b=axes(image.at<cv::Vec3f>(y,x));
            input+=double(a[1])*a[1]+double(a[2])*a[2];
            output+=double(b[1])*b[1]+double(b[2])*b[2];
            intensityError=std::max(intensityError,double(std::abs(a[0]-b[0])));
        }
        std::cout<<"achromatic_texture_chroma_mse_ratio "<<output/input<<'\n';
        cv::Mat inputChroma(size,size,CV_32FC2),outputChroma(size,size,CV_32FC2);
        for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
            const auto a=axes(before.at<cv::Vec3f>(y,x)),b=axes(image.at<cv::Vec3f>(y,x));
            inputChroma.at<cv::Vec2f>(y,x)={a[1],a[2]};
            outputChroma.at<cv::Vec2f>(y,x)={b[1],b[2]};
        }
        cv::GaussianBlur(inputChroma,inputChroma,{},4);
        cv::GaussianBlur(outputChroma,outputChroma,{},4);
        double lowBefore=0,lowAfter=0;
        for(int y=208;y<304;++y)for(int x=224;x<288;++x) {
            const auto a=inputChroma.at<cv::Vec2f>(y,x),b=outputChroma.at<cv::Vec2f>(y,x);
            lowBefore+=double(a[0])*a[0]+double(a[1])*a[1];
            lowAfter+=double(b[0])*b[0]+double(b[1])*b[1];
        }
        std::cout<<"achromatic_texture_chroma_lowpass_sigma4_mse_ratio "<<lowAfter/lowBefore<<'\n';
        check(output<.5*input,"achromatic luminance texture must not shield simulated chroma noise");
        check(intensityError<5e-7,"chroma blotch reduction preserves actual camera intensity texture");
    }
    {
        // Genuine isoluminant colour features, surrounded by stationary noise
        // witnesses. Protect hue edges and a one-pixel colour line.
        cv::Mat image(256,512,CV_32FC3);cv::RNG random(0x741a);
        for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x) {
            const float intensity=x<171?.06f:(x<341?.15f:.4f);
            const float co=(x<256?.04f:-.04f)+float(random.gaussian(.001));
            const float cg=(x==400?.05f:0.f)+float(random.gaussian(.001));
            image.at<cv::Vec3f>(y,x)={intensity+co-.5f*cg,intensity+.5f*cg,intensity-co-.5f*cg};
        }
        check(apply(image,{0,100},{}),"genuine colour edge and fine line complete");
        double contrast=0,line=0;
        for(int y=64;y<192;++y) {
            contrast+=axes(image.at<cv::Vec3f>(y,255))[1]-axes(image.at<cv::Vec3f>(y,256))[1];
            line+=axes(image.at<cv::Vec3f>(y,400))[2]-.5*(axes(image.at<cv::Vec3f>(y,399))[2]+axes(image.at<cv::Vec3f>(y,401))[2]);
        }
        contrast/=128;line/=128;
        std::cout<<"genuine_colour_edge_contrast "<<contrast<<" fine_line "<<line<<'\n';
        check(contrast>.072&&line>.045,"retain at least 90 percent of true chromatic edge and fine-line contrast");
    }
    {
        cv::Mat image(384,384,CV_32FC3);cv::RNG random(0x29a0);
        for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x) {
            const float i=.05f+.0012f*x+.0013f*y+float(random.gaussian(.001));
            const float co=float(random.gaussian(.004)),cg=float(random.gaussian(.004));
            image.at<cv::Vec3f>(y,x)={i+co-.5f*cg,i+.5f*cg,i-co-.5f*cg};
        }
        const auto before=image.clone();Context context;int unsupported=0;
        context.noiseModelObserver=[&](const NoiseModelSummary &m){unsupported+=!m.supported&&m.candidates>0&&m.globalFallbackAvailable;};
        check(apply(image,{0,100},context),"pooled fallback on a noisy smooth ramp completes");
        check(unsupported==5,"ramp has no flat conditional support on every scale");
        double input=0,output=0;
        for(int y=128;y<256;++y)for(int x=128;x<256;++x) {
            const auto a=axes(before.at<cv::Vec3f>(y,x)),b=axes(image.at<cv::Vec3f>(y,x));
            input+=double(a[1])*a[1]+double(a[2])*a[2];
            output+=double(b[1])*b[1]+double(b[2])*b[2];
        }
        std::cout<<"unsupported_ramp_chroma_mse_ratio "<<output/input<<'\n';
        check(output<.8*input,"missing conditional model uses the existing pooled witnesses");
    }
    {
        cv::Mat image(512,512,CV_32FC3);cv::RNG random(0x29a1);
        for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x) {
            // The dark region is between the 5x5 witness cores; the pooled
            // simulated read noise is identical in the dark and in the surround.
            const bool dark=x>=182&&x<218&&y>=182&&y<218;
            const float i=(dark?.01f:.2f)+float(random.gaussian(.0008));
            const float co=float(random.gaussian(.004)),cg=float(random.gaussian(.004));
            image.at<cv::Vec3f>(y,x)={i+co-.5f*cg,i+.5f*cg,i-co-.5f*cg};
        }
        const auto before=image.clone();Context context;int aboveDark=0;
        context.noiseModelObserver=[&](const NoiseModelSummary &m){aboveDark+=m.supported&&m.low>.1f&&m.globalFallbackAvailable;};
        check(apply(image,{0,100},context),"pooled fallback outside witnessed signal range completes");
        check(aboveDark==5,"dark patch lies below every conditional support range");
        double input=0,output=0;
        for(int y=190;y<210;++y)for(int x=190;x<210;++x) {
            const auto a=axes(before.at<cv::Vec3f>(y,x)),b=axes(image.at<cv::Vec3f>(y,x));
            input+=double(a[1])*a[1]+double(a[2])*a[2];
            output+=double(b[1])*b[1]+double(b[2])*b[2];
        }
        std::cout<<"unwitnessed_dark_patch_chroma_mse_ratio "<<output/input<<'\n';
        check(output<.8*input,"out-of-domain dark patch does not silently receive zero denoise");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
