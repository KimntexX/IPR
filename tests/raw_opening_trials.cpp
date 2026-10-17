#include "raw_opening_trial_helpers.h"
#include "dcp_headroom_experiment.h"
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTextStream out(stdout);
    try {
        selfTest();if(argc==2&&app.arguments()[1]=="--self-test"){out<<"Spline tests passed"<<Qt::endl;return 0;}
        if(argc!=3)throw std::runtime_error("Usage: rawOpeningTrials SPEC_JSON NEW_RESULT_JSON");
        QFile input(app.arguments()[1]);if(!input.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read specification");
        QJsonObject spec=QJsonDocument::fromJson(input.readAll()).object();QJsonArray records;
        for(const auto item:spec["scenes"].toArray()) {
            const auto scene=item.toObject();const QString path=scene["RAW"].toString(),sourceHash=digest(path);
            out<<"Trial "<<path<<Qt::endl;
            RawDecoder::Options options;options.automaticWhiteBalance=false;options.preferCameraNeutral=true;options.useNormalizationEnvironment=false;options.lensCorrectionEnabled=false;options.chromaticAberrationEnabled=false;
            ImageMetadata metadata;QString error;auto full=RawDecoder::decode(path,options,&metadata,&error);
            if(full.isNull()||!full.dcpContext)throw std::runtime_error(error.toStdString());
            auto current=sample3(full.linearRgba);WorkingColor::transform(current, WorkingColor::toSrgb());
            auto context=*full.dcpContext;context.cameraRgb=sample3(context.cameraRgb);full={};
            if(digest(context.profilePath)!=scene["DCP_sha256"].toString())throw std::runtime_error("Unexpected DCP");
            Curve curve(scene["curve"].toArray());QJsonObject models{{"current",metrics(current)}};double witness=0;
            const auto run=[&](double ev,const QString &prefix) {
                auto renderOptions=context.options;renderOptions.cameraBaselineExposureEv=ev;
                cv::Mat before=context.cameraRgb.clone();
                if(!applyDcpForSrgbAnalysis(scene["without_curve"].toString(),before,context.sourceKelvin,&error,renderOptions))throw std::runtime_error(error.toStdString());
                for(bool hue:{false,true})for(bool cubic:{false,true}) {
                    auto result=renderCurve(before,curve,cubic,hue);
                    QString name=prefix+(hue?"hue_":"channel_")+(cubic?"natural_spline":"linear");models[name]=metrics(result);
                    if(ev==0&&!hue&&!cubic)witness=cv::norm(result,current,cv::NORM_INF);
                }
            };
            run(0,"");
            if(scene.contains("camera_baseline_EV"))run(scene["camera_baseline_EV"].toDouble(),"camera_baseline_");
            if(witness>2e-5)throw std::runtime_error("Curve replay witness diverged");
            if(sourceHash!=digest(path))throw std::runtime_error("RAW changed");
            records.append(QJsonObject{{"scene",scene["scene"]},{"RAW_sha256",sourceHash},{"DCP_sha256",digest(context.profilePath)},
                {"camera",metadata.cameraModel},{"reference_kelvin",context.sourceKelvin},{"curve_replay_max_abs_error",witness},{"models",models}});
        }
        QJsonObject result{{"measured_at_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"sample_step",8},{"spec_sha256",digest(app.arguments()[1])},{"executable_sha256",digest(app.applicationFilePath())},{"records",records}};
        QFile output(app.arguments()[2]);if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))throw std::runtime_error("Refuse result overwrite");
        const QByteArray bytes=QJsonDocument(result).toJson();if(output.write(bytes)!=bytes.size()||!output.flush())throw std::runtime_error("Write failed");
        out<<"Complete "<<records.size()<<" scenes"<<Qt::endl;return 0;
    }catch(const std::exception &e){QTextStream(stderr)<<e.what()<<Qt::endl;return 1;}
}
