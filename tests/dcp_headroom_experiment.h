#ifndef IPR_TESTS_DCP_HEADROOM_EXPERIMENT_H
#define IPR_TESTS_DCP_HEADROOM_EXPERIMENT_H
// Requires raw_opening_trial_helpers.h. Diagnostic only; no production API change.
#include "raw_highlight_headroom.h"
#include <QTemporaryDir>
#include <QFileInfo>

QJsonObject runHeadroomExperiment(const ipr::Image &camera,
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
    const auto apply=[&](ipr::Image rgb,const QString &path) {
        QString error;
        if(!applyDcpForSrgbAnalysis(path,rgb,context.sourceKelvin,&error,context.options))
            throw std::runtime_error(error.toStdString());
        return rgb;
    };
    // Recover the exact matrix/exposure operator from basis vectors, then query
    // isolated real DCP stages through its inverse. No proprietary LUT copy in JSON.
    ipr::Image basis=ipr::Image::zeros({3,1},ipr::Depth::F32,3);
    for(int c=0;c<3;++c)imagetest::at<ipr::Vec3f>(basis, 0,c)[c]=1;
    const auto mappedBasis=apply(std::move(basis),matrix);
    ipr::Matrix33d cameraToSrgb;
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)cameraToSrgb(r,c)=imagetest::at<ipr::Vec3f>(mappedBasis, 0,c)[r];
    if(std::abs(ipr::determinant(cameraToSrgb))<1e-12)throw std::runtime_error("Singular study matrix");
    const auto proToCamera=cameraToSrgb.inv()*proToSrgb;
    const auto srgbToPro=proToSrgb.inv();
    const auto transform=[](const ipr::Image &rgb,const ipr::Matrix33d &matrix) {
        ipr::Image result=rgb.clone();WorkingColor::transform(result,matrix);return result;
    };
    const auto input=transform(apply(camera.clone(),matrix),srgbToPro);
    const auto stage=[&](const ipr::Image &signal,const QString &path,bool extended) {
        auto query=signal.clone();
        if(extended)for(int y=0;y<query.height();++y)for(int x=0;x<query.width();++x) {
            auto &v=imagetest::at<ipr::Vec3f>(query, y,x);const float s=std::max(1.f,std::max({v[0],v[1],v[2]}));
            for(int c=0;c<3;++c)v[c]=std::clamp(v[c]/s,0.f,1.f);
        }
        auto result=transform(apply(transform(query,proToCamera),path),srgbToPro);
        if(extended)for(int y=0;y<result.height();++y)for(int x=0;x<result.width();++x) {
            const auto p=imagetest::at<ipr::Vec3f>(signal, y,x),q=imagetest::at<ipr::Vec3f>(std::as_const(query), y,x);
            const float s=std::max(1.f,std::max({p[0],p[1],p[2]}));
            auto &v=imagetest::at<ipr::Vec3f>(result, y,x);v=p+s*(v-q);
        }
        if(!imagetest::within(result,-1e10,1e10))throw std::runtime_error("Nonfinite study stage");
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
    const double error=ipr::largestDifference(witness,direct);
    if(error>2e-5)throw std::runtime_error("Isolated-table replay witness diverged: "+std::to_string(error));
    auto extended=tables(true);
    auto shoulder=extended.clone();
    for(int y=0;y<shoulder.height();++y)for(int x=0;x<shoulder.width();++x) {
        auto &p=imagetest::at<ipr::Vec3f>(shoulder, y,x);const double peak=std::max({p[0],p[1],p[2]});
        if(peak>.85)p*=float(RawHighlightStudy::softShoulder(peak,.85)/peak);
    }
    const auto boundedCurve=transform(stage(extended,curve,false),proToSrgb);
    const auto extendedCurve=transform(stage(extended,curve,true),proToSrgb);
    const auto shoulderCurve=transform(stage(shoulder,curve,false),proToSrgb);
    const auto softGamut=[](const ipr::Image &source) {
        auto result=source.clone();
        for(int y=0;y<result.height();++y)for(int x=0;x<result.width();++x) {
            auto &p=imagetest::at<ipr::Vec3f>(result, y,x);const double Y=.2126*p[0]+.7152*p[1]+.0722*p[2];
            if(Y<=0){p={0,0,0};continue;}if(Y>=1){p={1,1,1};continue;}
            double extent=0;for(int c=0;c<3;++c)extent=std::max(extent,p[c]>=Y?(p[c]-Y)/(1-Y):(Y-p[c])/Y);
            const double scale=extent>.85?RawHighlightStudy::softShoulder(extent,.85)/extent:1;
            for(int c=0;c<3;++c)p[c]=float(Y+scale*(p[c]-Y));
        }return result;
    };
    const auto currentGamut=[](const ipr::Image &source) {
        auto mapped=ColorPipeline::mapToSrgbGamut(working(source));
        if(mapped.isNull())throw std::runtime_error("Study gamut failed");
        return ipr::withoutAlpha(mapped.linearRgba);
    };
    const auto baseline=currentGamut(direct);
    const auto gated=[&](const ipr::Image &source) {
        const auto hard=currentGamut(source),soft=softGamut(source);auto result=hard.clone();
        for(int y=0;y<result.height();++y)for(int x=0;x<result.width();++x) {
            const auto b=imagetest::at<ipr::Vec3f>(hard, y,x),s=imagetest::at<ipr::Vec3f>(soft, y,x);
            const double Y=.2126*b[0]+.7152*b[1]+.0722*b[2];
            const float w=float(RawHighlightStudy::highlightWeight(Y,.25,.75));
            imagetest::at<ipr::Vec3f>(result, y,x)=w==0?b:b+w*(s-b);
        }return result;
    };
    QJsonObject models;
    std::vector<std::pair<QString,ipr::Image>> variants;
    variants.emplace_back("current",baseline.clone());
    variants.emplace_back("table_extended_curve_bounded",currentGamut(boundedCurve));
    variants.emplace_back("table_and_curve_extended",currentGamut(extendedCurve));
    variants.emplace_back("table_extended_precurve_shoulder",currentGamut(shoulderCurve));
    variants.emplace_back("soft_gamut_only",softGamut(direct));
    variants.emplace_back("table_shoulder_soft_gamut",softGamut(shoulderCurve));
    variants.emplace_back("highlight_gated_soft_gamut",gated(direct));
    variants.emplace_back("table_shoulder_highlight_gated_gamut",gated(shoulderCurve));
    for(const auto &variant:variants) {
        auto stats=metrics(variant.second);
        stats.insert("linear_mean_abs_change_to_current",imagetest::sumOfDifferences(variant.second,baseline)/(3.0*double(baseline.size().area())));
        stats.insert("linear_max_abs_change_to_current",ipr::largestDifference(variant.second,baseline));
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
