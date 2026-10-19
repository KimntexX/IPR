#ifndef IPR_TESTS_DCP_HEADROOM_EXPERIMENT_H
#define IPR_TESTS_DCP_HEADROOM_EXPERIMENT_H
// Requires raw_opening_trial_helpers.h. Diagnostic only; no production API change.
#include "raw_highlight_headroom.h"
#include <QTemporaryDir>
#include <QFileInfo>

QJsonObject runHeadroomExperiment(const cv::Mat &camera,
        const CameraProfileEngine::DcpWorkingContext &context,
        const QByteArray &profileBytes, const QDir &out, QTemporaryDir &temporary)
{
    const auto profile=[&](const QString &name,const std::vector<int> &hidden) {
        const QString path=temporary.filePath("study-"+name+".dcp");QFile file(path);
        const auto bytes=hideDcpTags(profileBytes,hidden);
        if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)||file.write(bytes)!=bytes.size())
            throw std::runtime_error("Cannot create diagnostic profile");
        file.close();return path;
    };
    const QString matrix=profile("matrix",{50937,50938,50939,50981,50982,50940});
    const QString hue=profile("hue",{50981,50982,50940});
    const QString look=profile("look",{50937,50938,50939,50940});
    const QString curve=profile("curve",{50937,50938,50939,50981,50982});
    const auto apply=[&](cv::Mat rgb,const QString &path) {
        QString error;
        if(!applyDcpForSrgbAnalysis(path,rgb,context.sourceKelvin,&error,context.options))
            throw std::runtime_error(error.toStdString());
        return rgb;
    };
    // Recover the exact matrix/exposure operator from basis vectors, then query
    // isolated real DCP stages through its inverse. No proprietary LUT copy in JSON.
    cv::Mat basis=cv::Mat::zeros(1,3,CV_32FC3);
    for(int c=0;c<3;++c)basis.at<cv::Vec3f>(0,c)[c]=1;
    const auto mappedBasis=apply(basis,matrix);
    cv::Matx33d cameraToSrgb;
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)cameraToSrgb(r,c)=mappedBasis.at<cv::Vec3f>(0,c)[r];
    if(std::abs(cv::determinant(cameraToSrgb))<1e-12)throw std::runtime_error("Singular study matrix");
    const auto proToCamera=cameraToSrgb.inv()*proToSrgb;
    const auto srgbToPro=proToSrgb.inv();
    const auto transform=[](const cv::Mat &rgb,const cv::Matx33d &matrix) {
        cv::Mat result;cv::transform(rgb,result,matrix);return result;
    };
    const auto input=transform(apply(camera.clone(),matrix),srgbToPro);
    const auto stage=[&](const cv::Mat &signal,const QString &path,bool extended) {
        auto query=signal.clone();
        if(extended)for(int y=0;y<query.rows;++y)for(int x=0;x<query.cols;++x) {
            auto &v=query.at<cv::Vec3f>(y,x);const float s=std::max(1.f,std::max({v[0],v[1],v[2]}));
            for(int c=0;c<3;++c)v[c]=std::clamp(v[c]/s,0.f,1.f);
        }
        auto result=transform(apply(transform(query,proToCamera),path),srgbToPro);
        if(extended)for(int y=0;y<result.rows;++y)for(int x=0;x<result.cols;++x) {
            const auto p=signal.at<cv::Vec3f>(y,x),q=query.at<cv::Vec3f>(y,x);
            const float s=std::max(1.f,std::max({p[0],p[1],p[2]}));
            auto &v=result.at<cv::Vec3f>(y,x);v=p+s*(v-q);
        }
        if(!cv::checkRange(result,true,nullptr,-1e10,1e10))throw std::runtime_error("Nonfinite study stage");
        return result;
    };
    // All current study profiles have no HueSat map. A matrix-only isolated
    // stage needs no round trip, which also avoids injecting numerical noise.
    const bool hasHue=hideDcpTags(profileBytes,{50937,50938,50939})!=profileBytes;
    const bool hasLook=hideDcpTags(profileBytes,{50981,50982})!=profileBytes;
    const auto tables=[&](bool extended) {
        auto rgb=input.clone();
        if(hasHue)rgb=stage(rgb,hue,extended);
        if(hasLook)rgb=stage(rgb,look,extended);
        return rgb;
    };
    const auto witnessPro=stage(tables(false),curve,false);
    const auto witness=transform(witnessPro,proToSrgb);
    const auto direct=apply(camera.clone(),context.profilePath);
    const double error=cv::norm(witness,direct,cv::NORM_INF);
    if(error>2e-5)throw std::runtime_error("Isolated-table replay witness diverged: "+std::to_string(error));
    auto extended=tables(true);
    auto shoulder=extended.clone();
    for(int y=0;y<shoulder.rows;++y)for(int x=0;x<shoulder.cols;++x) {
        auto &p=shoulder.at<cv::Vec3f>(y,x);const double peak=std::max({p[0],p[1],p[2]});
        if(peak>.85)p*=float(RawHighlightStudy::softShoulder(peak,.85)/peak);
    }
    const auto boundedCurve=transform(stage(extended,curve,false),proToSrgb);
    const auto extendedCurve=transform(stage(extended,curve,true),proToSrgb);
    const auto shoulderCurve=transform(stage(shoulder,curve,false),proToSrgb);
    const auto softGamut=[](const cv::Mat &source) {
        auto result=source.clone();
        for(int y=0;y<result.rows;++y)for(int x=0;x<result.cols;++x) {
            auto &p=result.at<cv::Vec3f>(y,x);const double Y=.2126*p[0]+.7152*p[1]+.0722*p[2];
            if(Y<=0){p={0,0,0};continue;}if(Y>=1){p={1,1,1};continue;}
            double extent=0;for(int c=0;c<3;++c)extent=std::max(extent,p[c]>=Y?(p[c]-Y)/(1-Y):(Y-p[c])/Y);
            const double scale=extent>.85?RawHighlightStudy::softShoulder(extent,.85)/extent:1;
            for(int c=0;c<3;++c)p[c]=float(Y+scale*(p[c]-Y));
        }return result;
    };
    const auto currentGamut=[](const cv::Mat &source) {
        auto mapped=ColorPipeline::mapToSrgbGamut(working(source));
        if(mapped.isNull())throw std::runtime_error("Study gamut failed");
        std::vector<cv::Mat> parts;cv::split(mapped.linearRgba,parts);parts.resize(3);
        cv::Mat rgb;cv::merge(parts,rgb);return rgb;
    };
    const auto baseline=currentGamut(direct);
    const auto gated=[&](const cv::Mat &source) {
        const auto hard=currentGamut(source),soft=softGamut(source);auto result=hard.clone();
        for(int y=0;y<result.rows;++y)for(int x=0;x<result.cols;++x) {
            const auto b=hard.at<cv::Vec3f>(y,x),s=soft.at<cv::Vec3f>(y,x);
            const double Y=.2126*b[0]+.7152*b[1]+.0722*b[2];
            const float w=float(RawHighlightStudy::highlightWeight(Y,.25,.75));
            result.at<cv::Vec3f>(y,x)=w==0?b:b+w*(s-b);
        }return result;
    };
    QJsonObject models;
    const std::vector<std::pair<QString,cv::Mat>> variants{
        {"current",baseline},{"table_extended_curve_bounded",currentGamut(boundedCurve)},
        {"table_and_curve_extended",currentGamut(extendedCurve)},
        {"table_extended_precurve_shoulder",currentGamut(shoulderCurve)},
        {"soft_gamut_only",softGamut(direct)},
        {"table_shoulder_soft_gamut",softGamut(shoulderCurve)},
        {"highlight_gated_soft_gamut",gated(direct)},
        {"table_shoulder_highlight_gated_gamut",gated(shoulderCurve)}};
    for(const auto &variant:variants) {
        auto stats=metrics(variant.second);
        stats.insert("linear_mean_abs_change_to_current",cv::norm(variant.second,baseline,cv::NORM_L1)/(3*baseline.total()));
        stats.insert("linear_max_abs_change_to_current",cv::norm(variant.second,baseline,cv::NORM_INF));
        const auto encoded=ColorPipeline::toSrgb(working(variant.second),ColorPipeline::OutputFormat::HighBitDepth);
        const QString path=out.filePath(variant.first+".png");
        if(QFileInfo::exists(path)||!encoded.save(path,"PNG"))throw std::runtime_error("Cannot save study preview");
        stats.insert("preview",path);stats.insert("preview_sha256",digest(path));models.insert(variant.first,stats);
    }
    return {{"models",models},{"isolated_stage_replay_max_abs_error",error},{"sample_step",8},
        {"has_hue_map",hasHue},{"has_look_table",hasLook},{"shoulder_knee",.85},
        {"gamut_chroma_knee",.85},{"highlight_gate_Y_begin",.25},{"highlight_gate_Y_end",.75},
        {"production_modified",false}};
}

#endif // IPR_TESTS_DCP_HEADROOM_EXPERIMENT_H
