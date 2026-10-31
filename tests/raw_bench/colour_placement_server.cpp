// Research-only placement of unchanged filters. No production source changes.
#include "TestRecipes.h"
#include "CameraDenoise.h"
#include "CameraProfileEngine.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
void require(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
}

QJsonObject survey(const cv::Mat &rgb)
{
    require(!rgb.empty() && rgb.type() == CV_32FC3, "Missing float camera/profile buffer");
    std::array<double, 3> sum{}, negative{}, zero{};
    double low = 0, high = 0;
    for (int y = 0; y < rgb.rows; ++y) {
        const auto *p = rgb.ptr<cv::Vec3f>(y);
        for (int x = 0; x < rgb.cols; ++x) {
            const float intensity = .25f*p[x][0]+.5f*p[x][1]+.25f*p[x][2];
            low += intensity < .002f;
            high += intensity >= .02f;
            for (int c = 0; c < 3; ++c) {
                const float v = p[x][c];
                require(std::isfinite(v), "Nonfinite intermediate");
                sum[c] += v; negative[c] += v < 0; zero[c] += v == 0;
            }
        }
    }
    QJsonArray means, negatives, zeros;
    const double n = double(rgb.total());
    for (int c = 0; c < 3; ++c) {
        means.append(sum[c]/n); negatives.append(negative[c]/n); zeros.append(zero[c]/n);
    }
    return {{"mean_rgb", means}, {"negative_fraction_rgb", negatives},
            {"zero_fraction_rgb", zeros}, {"I_below_002_fraction", low/n}, {"I_above_02_fraction", high/n}};
}

class Experiment {
public:
    void render(const QJsonObject &request)
    {
        QElapsedTimer timer; timer.start();
        const QString source = request.value("source").toString();
        const QString placement = request.value("placement").toString();
        require(request.value("signed").isBool(), "Explicit signed policy required");
        const bool signedMode = request.value("signed").toBool();
        const int strength = request.value("strength").toInt(-1);
        require(strength >= 0 && strength <= 100, "Strength outside 0..100");
        require(placement == "post" || placement == "pre" || placement == "hybrid", "Unknown placement");
        if (!master_ || source_ != source || signed_ != signedMode) {
            master_.reset(); filtered_.release(); filteredStrength_ = -1;
            source_ = source; signed_ = signedMode;
            TestRecipes::Decode decode; decode.signedCameraSignal = signedMode;
            master_ = TestRecipes::load(source, decode);
            require(bool(master_->image.dcpContext), "Actual DCP context required");
            require(!master_->metadata.lensGeometryApplied && !master_->metadata.lensVignettingApplied &&
                    !master_->metadata.chromaticAberrationApplied, "Optics must be disabled");
            cameraSurvey_ = survey(master_->image.dcpContext->cameraRgb);
            cv::Mat rgb; cv::cvtColor(master_->image.linearRgba, rgb, cv::COLOR_RGBA2RGB);
            profileSurvey_ = survey(rgb);
        }
        auto settings = TestRecipes::neutral(*master_);
        settings.denoiseLuminance = 0; settings.sharpeningAmount = 0;
        settings.denoiseBeforeProfile = signedMode;
        settings.denoiseChrominance = placement == "pre" ? 0 : strength;
        auto working = master_->image;
        QJsonObject preSurvey = cameraSurvey_, profileSurvey = profileSurvey_;
        if (placement != "post" && (strength > 0 || request.value("identityRerender").toBool())) {
            const auto &original = master_->image.dcpContext->cameraRgb;
            if (filteredStrength_ != strength) {
                filtered_ = original;
                require(CameraDenoise::apply(filtered_, {0, strength}, {}), "Camera filter failed");
                filteredStrength_ = strength;
            }
            auto context = std::make_shared<CameraProfileEngine::DcpWorkingContext>(*master_->image.dcpContext);
            context->cameraRgb = filtered_;
            if (placement == "hybrid") {
                cv::Mat guide(original.rows, original.cols, CV_32F);
                for (int y = 0; y < original.rows; ++y) {
                    const auto *p = original.ptr<cv::Vec3f>(y); auto *q = guide.ptr<float>(y);
                    for (int x = 0; x < original.cols; ++x) q[x]=.25f*p[x][0]+.5f*p[x][1]+.25f*p[x][2];
                }
                cv::GaussianBlur(guide, guide, {}, 4., 4., cv::BORDER_REFLECT_101);
                context->cameraRgb = original.clone();
                for (int y = 0; y < original.rows; ++y) {
                    const auto *p=original.ptr<cv::Vec3f>(y), *f=filtered_.ptr<cv::Vec3f>(y);
                    const auto *g=guide.ptr<float>(y); auto *q=context->cameraRgb.ptr<cv::Vec3f>(y);
                    for (int x=0; x<original.cols; ++x) {
                        const float t=std::clamp((g[x]-.002f)/.018f,0.f,1.f);
                        const float weight=1.f-t*t*(3.f-2.f*t);
                        q[x]=p[x]+weight*(f[x]-p[x]);
                    }
                }
            }
            preSurvey = survey(context->cameraRgb);
            cv::Mat rgb; QString error;
            require(CameraProfileEngine::renderFromCameraSignal(*context, settings.temperature, 0, 0, rgb, &error),
                    qPrintable(error));
            profileSurvey = survey(rgb);
            cv::Mat rgba; cv::cvtColor(rgb, rgba, cv::COLOR_RGB2RGBA);
            working.linearRgba = std::move(rgba);
            working.dcpContext = context;
            // Keep neutral tone statistics and WB reference unchanged. Denoise L=0,
            // so the editor does not apply CameraDenoise a second time.
        }
        QString error;
        const QImage image=ImageEditor::renderSrgb16(working, settings, &error);
        require(!image.isNull() && image.format()==QImage::Format_RGBA64, qPrintable(error));
        std::vector<quint16> pixels(size_t(image.width())*size_t(image.height())*3);
        for (int y=0;y<image.height();++y) {
            const auto *p=reinterpret_cast<const quint16 *>(image.constScanLine(y));
            auto *q=pixels.data()+size_t(y)*size_t(image.width())*3;
            for (int x=0;x<image.width();++x) for (int c=0;c<3;++c) q[size_t(x)*3+size_t(c)]=p[size_t(x)*4+size_t(c)];
        }
        static_assert(Q_BYTE_ORDER==Q_LITTLE_ENDIAN,"Little endian wire format required");
        const auto declaration=TestRecipes::declaration(*master_,settings,source,"bruit","neutral",0,"own neutral");
        const QString profilePath = master_->image.dcpContext->profilePath;
        const QString manifestPath = ImageEditor::defaultCalibrationManifest();
        QByteArray manifestBytes;
        if (manifestPath.startsWith(":/")) {
            QFile manifest(manifestPath);
            require(manifest.open(QIODevice::ReadOnly), "Cannot read embedded calibration manifest");
            manifestBytes = manifest.readAll();
            require(manifest.error() == QFileDevice::NoError, "Embedded manifest read failed");
        }
        send({{"ok",true},{"width",image.width()},{"height",image.height()},{"channels",3},
              {"dtype","<u2"},{"bytes",qint64(pixels.size()*2)},{"signed",signedMode},
              {"placement",placement},{"strength",strength},{"camera_source",cameraSurvey_},
              {"camera_filtered",preSurvey},{"profile_output",profileSurvey},
              {"profile_path",profilePath},{"profile_sha256",TestRecipes::sha256(profilePath)},
              {"manifest_path",manifestPath},{"manifest_sha256",TestRecipes::sha256(manifestPath)},
              {"manifest_kind",manifestPath.startsWith(":/") ? "qt_resource" : "file"},
              {"manifest_base64",QString::fromLatin1(manifestBytes.toBase64())},
              {"declaration",declaration},{"elapsed_ms",double(timer.elapsed())}}, pixels);
    }

    static void send(const QJsonObject &header, const std::vector<quint16> &pixels = {})
    {
        const auto line=QJsonDocument(header).toJson(QJsonDocument::Compact)+'\n';
        require(std::fwrite(line.constData(),1,size_t(line.size()),stdout)==size_t(line.size()),"Header write failed");
        require(pixels.empty() || std::fwrite(pixels.data(),sizeof(quint16),pixels.size(),stdout)==pixels.size(),"Pixel write failed");
        require(std::fflush(stdout)==0,"Flush failed");
    }
private:
    std::optional<TestRecipes::LoadResult> master_;
    QString source_;
    bool signed_=false;
    cv::Mat filtered_;
    int filteredStrength_=-1;
    QJsonObject cameraSurvey_, profileSurvey_;
};
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QCoreApplication::setOrganizationName("IPR"); QCoreApplication::setApplicationName("IPR");
    cv::setNumThreads(1);
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    Experiment experiment; std::string line;
    for(int c;(c=std::getchar())!=EOF;) {
        if(c!='\n') { line.push_back(char(c)); continue; }
        try {
            QJsonParseError error;
            const auto request=QJsonDocument::fromJson(QByteArray::fromStdString(line),&error).object(); line.clear();
            require(error.error==QJsonParseError::NoError,"Invalid JSON");
            if(request.value("op").toString()=="quit") return 0;
            require(request.value("op").toString()=="render","Unknown operation");
            experiment.render(request);
        } catch(const std::exception &e) {
            line.clear();
            try { Experiment::send({{"ok",false},{"error",QString::fromUtf8(e.what())}}); }
            catch(const std::exception &) { return 1; }
        }
    }
    return 0;
}
