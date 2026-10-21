// Manual private-corpus diagnostic, excluded from CTest/default build.
#include "RawDecoder.h"
#include "ImageImporter.h"
#include "raw_opening_trial_helpers.h"
#include "dcp_headroom_experiment.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QElapsedTimer>
#include <QSysInfo>
#include <cstring>
#include <opencv2/imgproc.hpp>

int main(int argc, char **argv)
{
    try {
    QCoreApplication app(argc,argv);
    if (argc != 5 && argc != 6) {
        QTextStream(stderr) << "Usage: rawNormalizationCorpusProbe RAW DCP MANIFEST NEW_OUTPUT_DIRECTORY [LCP|--working-space-source|--working-space-area|--working-space-uncalibrated]\n";
        return 2;
    }
    const auto args = app.arguments();
    const bool areaStudy=argc==6 && args[5]=="--working-space-area";
    const bool uncalibratedStudy=argc==6 && args[5]=="--working-space-uncalibrated";
    const bool sourceOnly=areaStudy || uncalibratedStudy || (argc==6 && args[5]=="--working-space-source");
    QTemporaryDir profiles;
    QDir out(args[4]);
    if (!profiles.isValid() || out.exists() || !QDir().mkpath(out.absolutePath()) ||
        !QFile::copy(args[2], profiles.filePath(QFileInfo(args[2]).fileName()))) return 3;
    RawDecoder::Options options{false, {profiles.path()}, args[3]};
    if(uncalibratedStudy){options.normalizationManifest.clear();options.useNormalizationEnvironment=false;}
    if (argc==6 && !sourceOnly) options.lcpProfileOverride=args[5];
    if(sourceOnly){options.lensCorrectionEnabled=false;options.chromaticAberrationEnabled=false;options.automaticLcpSelection=false;}
    QElapsedTimer decodeTimer;decodeTimer.start();
    auto imported = ImageImport::load(args[1], options);
    auto image = std::move(imported.image);
    auto metadata = std::move(imported.metadata);
    QString error = imported.error;
    const auto decodeMs=decodeTimer.elapsed();
    if (image.isNull() || !imported.capabilities.cameraDevelopment ||
        (!metadata.rawNormalizationApplied && !uncalibratedStudy) ||
        (uncalibratedStudy && metadata.rawNormalizationApplied) ||
        ((argc==5||sourceOnly) && !metadata.lcpProfile.isEmpty())) {
        QTextStream(stderr) << "Decode/contract failed: " << error << '\n';
        return 4;
    }
    // The diagnostic profile copy is confined to a temporary folder; no LUT
    // payload is stored in the repository or in the resulting measurements.
    QJsonObject areaProof;
    if(areaStudy) {
        // Average only AFTER the complete production output transform. Point
        // samples alias high-ISO noise; averaging camera RGB before DCP would
        // instead change the nonlinear rendering being measured.
        const auto full=ColorPipeline::toSrgb(image,ColorPipeline::OutputFormat::HighBitDepth,&error)
                            .convertToFormat(QImage::Format_RGBA64);
        if(full.isNull())return 5;
        cv::Mat packed(full.height(),full.width(),CV_16UC4,
                       const_cast<uchar *>(full.constBits()),full.bytesPerLine());
        cv::Mat linear;
        packed.convertTo(linear,CV_32FC4,1.0/65535.0);
        cv::parallel_for_(cv::Range(0,linear.rows),[&](const cv::Range &range) {
            for(int y=range.start;y<range.end;++y) {
                auto *row=linear.ptr<cv::Vec4f>(y);
                for(int x=0;x<linear.cols;++x)for(int c=0;c<3;++c) {
                    const float v=row[x][c];
                    row[x][c]=v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);
                }
            }
        });
        cv::Mat area;
        cv::resize(linear,area,cv::Size((full.width()+3)/4,(full.height()+3)/4),0,0,cv::INTER_AREA);
        std::vector<cv::Mat> components;cv::split(area,components);components.resize(3);
        cv::Mat rgb;cv::merge(components,rgb);
        QFile data(out.filePath("output-area-linear-rgb.f32"));
        const qint64 bytes=qint64(rgb.total()*rgb.elemSize());
        if(!data.open(QIODevice::WriteOnly|QIODevice::NewOnly)||
           data.write(reinterpret_cast<const char *>(rgb.data),bytes)!=bytes)return 6;
        data.close();
        areaProof={{"rows",rgb.rows},{"cols",rgb.cols},{"source_width",full.width()},
                   {"source_height",full.height()},{"sha256",digest(data.fileName())},
                   {"domain","linear sRGB decoded from full production 16bit output, then INTER_AREA"}};
    }
    const int rows=(image.linearRgba.rows+7)/8, cols=(image.linearRgba.cols+7)/8;
    cv::Mat grid(rows,cols,CV_32FC4);
    for (int y=0;y<rows;++y)
        for (int x=0;x<cols;++x)
            grid.at<cv::Vec4f>(y,x)=image.linearRgba.at<cv::Vec4f>(8*y,8*x);
    image.linearRgba=grid;
    std::vector<cv::Mat> channels;cv::split(grid,channels);channels.resize(3);
    cv::Mat stageRgb;cv::merge(channels,stageRgb);
    WorkingColor::transform(stageRgb, WorkingColor::toSrgb()); // Explicit diagnostic domain.
    if(!image.dcpContext)return 8;
    if(sourceOnly) {
        auto replay=sample3(image.dcpContext->cameraRgb);
        if(!applyDcpForSrgbAnalysis(image.dcpContext->profilePath,replay,image.dcpContext->sourceKelvin,&error,image.dcpContext->options))return 8;
        const double replayError=cv::norm(replay,stageRgb,cv::NORM_INF);
        if(replayError>1e-6)return 9;
        QFile pixels(out.filePath("post-dcp-srgb-linear.f32"));const qint64 bytes=qint64(stageRgb.total()*stageRgb.elemSize());
        if(!pixels.open(QIODevice::WriteOnly|QIODevice::NewOnly)||pixels.write(reinterpret_cast<const char*>(stageRgb.data),bytes)!=bytes)return 6;
        pixels.close();
        const auto witness=ColorPipeline::toSrgb(image,ColorPipeline::OutputFormat::HighBitDepth,&error);
        if(witness.isNull()||!witness.save(out.filePath("native-current.png"),"PNG"))return 5;
        const auto cacheStats=CameraProfileEngine::preparedProfileCacheStats();
        const QJsonObject proof{{"raw",args[1]},{"raw_sha256",digest(args[1])},{"profile",args[2]},
            {"prepared_profile_cache",QJsonObject{{"budget_bytes",double(cacheStats.budgetBytes)},
                {"entry_limit",cacheStats.entryLimit},{"retained_bytes",double(cacheStats.retainedBytes)},
                {"entries",cacheStats.entries},{"hits",double(cacheStats.hits)},
                {"misses",double(cacheStats.misses)},{"preparations",double(cacheStats.preparations)},
                {"waits",double(cacheStats.waits)},{"evictions",double(cacheStats.evictions)}}},
            {"profile_sha256",digest(args[2])},{"rows",rows},{"cols",cols},{"sample_step",8},
            {"byte_order",QSysInfo::ByteOrder==QSysInfo::LittleEndian?"little":"big"},
            {"camera",metadata.cameraModel},{"decode_ms",double(decodeMs)},{"white_level",int(metadata.normalizationWhiteLevel)},
            {"decoder_id",metadata.decoderId},{"decoder_version",metadata.decoderVersion},
            {"baseline_exposure_ev",metadata.normalizationBaselineExposureEv},
            {"profile_exposure_compensation_ev",metadata.profileExposureCompensationEv},
            {"effective_camera_exposure_ev",metadata.normalizationBaselineExposureEv+metadata.profileExposureCompensationEv},
            {"profile_rendering_provenance",metadata.profileRenderingProvenance},
            {"source_kelvin",image.dcpContext->sourceKelvin},
            {"normalization_applied",metadata.rawNormalizationApplied},{"no_lcp",metadata.lcpProfile.isEmpty()},
            {"CA",metadata.chromaticAberrationApplied},{"lens_geometry",metadata.lensGeometryApplied},
            {"lens_vignetting",metadata.lensVignettingApplied},{"DCP_replay_max_abs_error",replayError},
            {"source_sha256",digest(pixels.fileName())},{"native_preview_sha256",digest(out.filePath("native-current.png"))},
            {"master_domain","Rec.2020 linear D65 float32"},
            {"backend_version",RawDecoder::backendVersion()},
            {"engine",ColorPipeline::engineIdentity()},
            {"domain","Unbounded post-DCP linear sRGB float32 analysis derivative; upstream tables and curve unchanged"},
            {"output_area",areaProof}};
        QFile manifest(out.filePath("manifest.json"));const auto json=QJsonDocument(proof).toJson();
        if(!manifest.open(QIODevice::WriteOnly|QIODevice::NewOnly)||manifest.write(json)!=json.size())return 7;
        QTextStream(stdout)<<json;return 0;
    }
    const auto cameraGrid=sample3(image.dcpContext->cameraRgb);
    QFile profileFile(args[2]);if(!profileFile.open(QIODevice::ReadOnly))return 8;
    const auto profileBytes=profileFile.readAll();
    QJsonObject dcpStages;
    const std::vector<std::pair<QString,std::vector<int>>> ablations{
        {"matrix_exposure",{50937,50938,50939,50981,50982,50940}},
        {"plus_hue_sat",{50981,50982,50940}},
        {"plus_look",{50940}}, {"full",{}}};
    double replayError=0;
    for(const auto &variant:ablations) {
        const auto path=profiles.filePath(variant.first+".dcp");QFile copy(path);
        const auto bytes=hideDcpTags(profileBytes,variant.second);
        if(!copy.open(QIODevice::WriteOnly|QIODevice::NewOnly)||copy.write(bytes)!=bytes.size())return 8;
        copy.close();auto rgb=cameraGrid.clone();
        if(!applyDcpForSrgbAnalysis(path,rgb,image.dcpContext->sourceKelvin,&error,image.dcpContext->options))return 8;
        auto stats=linearStageStats(rgb);
        // Reconstruct linear ProPhoto using the exact production matrices;
        // these are pre-curve coordinates only for the plus_look ablation.
        const cv::Matx33d proPhotoToXyz(.7976749,.1351917,.0313534,.2880402,.7118741,.0000857,0.,0.,.8252100);
        const auto srgbToProPhoto=(WorkingColor::toSrgb()*WorkingColor::fromXyzD50()*proPhotoToXyz).inv();
        cv::Mat proPhoto;cv::transform(rgb,proPhoto,srgbToProPhoto);
        auto bounds=linearStageStats(proPhoto);
        for(const auto &key:{"mean_Y_unbounded","Y_p05_p50_p95_p99","Y_at_or_above_one_percent"})bounds.remove(key);
        stats.insert("prophoto_bounds",bounds);
        stats.insert("profile_copy_sha256",digest(path));dcpStages.insert(variant.first,stats);
        if(variant.first=="full")replayError=cv::norm(rgb,stageRgb,cv::NORM_INF);
    }
    if(replayError>1e-6)return 9;
    const auto headroomStudy=runHeadroomExperiment(cameraGrid,*image.dcpContext,profileBytes,out,profiles);
    auto cameraStats=linearStageStats(cameraGrid);
    for(const auto &key:{"mean_Y_unbounded","Y_p05_p50_p95_p99","Y_at_or_above_one_percent"})cameraStats.remove(key);
    const auto candidate=preserveLuminanceGamut(stageRgb);
    double beforeY=0,currentY=0,candidateY=0;int negative=0,above=0,overY=0,cameraHigh=0;
    auto mapped=ColorPipeline::mapToSrgbGamut(image,&error);
    for(int y=0;y<rows;++y)for(int x=0;x<cols;++x) {
        const auto v=stageRgb.at<cv::Vec3f>(y,x);const auto m=mapped.linearRgba.at<cv::Vec4f>(y,x);const auto c=candidate.at<cv::Vec3f>(y,x);
        const double Y=.2126*v[0]+.7152*v[1]+.0722*v[2];beforeY+=Y;currentY+=.2126*m[0]+.7152*m[1]+.0722*m[2];candidateY+=.2126*c[0]+.7152*c[1]+.0722*c[2];
        negative+=std::min({v[0],v[1],v[2]})<0;above+=std::max({v[0],v[1],v[2]})>1;overY+=Y>=1;
        if(image.dcpContext) {
            const auto p=image.dcpContext->cameraRgb.at<cv::Vec3f>(y*8,x*8);
            cameraHigh+=std::max({p[0],p[1],p[2]})>=65534.f/65535;
        }
    }
    const double count=double(grid.total());
    const QJsonObject stageAudit{{"pre_gamut_mean_Y",beforeY/count},{"current_map_mean_Y",currentY/count},
        {"candidate_map_mean_Y",candidateY/count},{"negative_srgb_percent",100*negative/count},
        {"above_one_srgb_percent",100*above/count},{"luminance_above_one_percent",100*overY/count},
        {"camera_RGB_at_upper_bound_percent",100*cameraHigh/count},{"candidate",metrics(candidate)}};
    image=ColorPipeline::mapToSrgbGamut(image,&error);
    const auto encoded=ColorPipeline::toSrgb(image,ColorPipeline::OutputFormat::HighBitDepth,&error);
    if (encoded.isNull()) return 5;
    cv::Mat rgb(rows,cols,CV_32FC3);
    for(int y=0;y<rows;++y) {
        const auto *line=reinterpret_cast<const QRgba64 *>(encoded.constScanLine(y));
        for(int x=0;x<cols;++x)
            rgb.at<cv::Vec3f>(y,x)=cv::Vec3f(line[x].red(),line[x].green(),line[x].blue())/65535.f;
    }
    QFile pixels(out.filePath("encoded.f32"));
    const qint64 bytes=qint64(rgb.total()*rgb.elemSize());
    if (!pixels.open(QIODevice::WriteOnly|QIODevice::NewOnly) ||
        pixels.write(reinterpret_cast<const char *>(rgb.data),bytes)!=bytes) return 6;
    pixels.close();
    const QJsonObject proof{{"raw",args[1]}, {"profile",args[2]}, {"rows",rows},{"cols",cols},
        {"white_level",int(metadata.normalizationWhiteLevel)},
        {"baseline_exposure_ev",metadata.normalizationBaselineExposureEv},
        {"profile_exposure_compensation_ev",metadata.profileExposureCompensationEv},
        {"effective_camera_exposure_ev",metadata.normalizationBaselineExposureEv+metadata.profileExposureCompensationEv},
        {"normalization_applied",metadata.rawNormalizationApplied},{"highlight_stage_audit",stageAudit},
        {"dcp_stage_ablation",dcpStages},{"camera_buffer",cameraStats},{"full_replay_max_abs_error",replayError},
        {"headroom_experiment",headroomStudy},
        {"provenance",metadata.normalizationProvenance}, {"no_lcp",metadata.lcpProfile.isEmpty()},
        {"lcp_sha256",metadata.lcpSha256},{"lens_geometry",metadata.lensGeometryApplied},
        {"lens_vignetting",metadata.lensVignettingApplied},{"CA",metadata.chromaticAberrationApplied}};
    QFile meta(out.filePath("manifest.json"));
    const auto json=QJsonDocument(proof).toJson();
    if (!meta.open(QIODevice::WriteOnly|QIODevice::NewOnly) || meta.write(json)!=json.size()) return 7;
    QTextStream(stdout) << json;
    return 0;
    } catch(const std::exception &e) {
        QTextStream(stderr) << "Headroom diagnostic failed: " << e.what() << '\n';
        return 10;
    }
}
