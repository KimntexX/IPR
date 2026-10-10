#include "ToneRecovery.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        check(ToneRecovery::positiveShadowDelta(0,0,1)==0, "Black must remain black");
        for (float ratio : {1.0f,2.0f,10.0f})
            for (float amount : {0.0f,.25f,.5f,.75f,1.0f}) {
                float previousShadow=0,previousHighlight=0;
                for (int i=1;i<=100000;++i) {
                    const float y=float(i)/100000,maximum=y*ratio;
                    const float ds=ToneRecovery::positiveShadowDelta(y,maximum,amount);
                    const float dh=ToneRecovery::positiveHighlightDelta(y,y,maximum,amount,
                        ToneRecovery::smoothStep(.02f,.50f,y));
                    check(ds>=0 && ds<=3*y+1e-7f,"Shadow gain exceeds +2 EV bound");
                    check(y+ds+1e-7f>=previousShadow,"Shadow curve reverses tones");
                    check(y+dh+1e-7f>=previousHighlight,"Highlight curve reverses tones");
                    if (maximum<1) {
                        check((y+ds)/y*maximum<=1+1e-6f,"Shadows introduce RGB overflow");
                        check((y+dh)/y*maximum<=1+1e-6f,"Highlights introduce RGB overflow");
                    } else check(ds==0 && dh==0,"Existing HDR must not be clipped or amplified");
                    if (amount==0) check(ds==0 && dh==0,"Zero must be identity");
                    previousShadow=y+ds; previousHighlight=y+dh;
                }
            }
        for (float y : {1e-12f,1e-9f,1e-6f,.001f,.01f,.025f,.1f,.3f,.56f}) {
            float previous=0;
            for (int value=0;value<=100;++value) {
                const float delta=ToneRecovery::positiveShadowDelta(y,y,float(value)/100);
                check(delta+1e-12f>=previous,"Slider progression must be monotone");
                previous=delta;
            }
        }
        check(ToneRecovery::positiveShadowDelta(.56f,.56f,1)==0,"Highlights must be protected");
        check(ToneRecovery::positiveShadowDelta(1e-12f,1e-12f,1)<=3e-12f,"No pedestal at black");
        check(ToneRecovery::positiveShadowDelta(.025f,.025f,1)<.019f,"Lift amplitude too high");
        if (argc==2) {
            QJsonArray rows;
            for (float amount : {.25f,.5f,.75f,1.0f})
                for (float ratio : {1.0f,2.0f,10.0f})
                    for (float y : {0.0f,1e-6f,1e-5f,.0001f,.001f,.005f,.01f,.025f,.05f,.1f,.18f,.3f,.45f,.56f,.8f,1.0f}) {
                        const float sm=1-ToneRecovery::smoothStep(.1f,.56f,y);
                        const float hm=ToneRecovery::smoothStep(.02f,.5f,y);
                        const float oldShadow=.065f*amount*sm*(1-std::sqrt(std::clamp(y/.45f,0.0f,1.0f)));
                        rows.append(QJsonObject{{"Y",double(y)},{"maximumChannel",double(y*ratio)},
                            {"amount",double(amount)}, {"oldShadowFormulaDeltaY",double(oldShadow)},
                            {"newShadowDeltaY",double(ToneRecovery::positiveShadowDelta(y,y*ratio,amount))},
                            {"oldHighlightFormulaDeltaY",double((1-y)*.7f*amount*hm)},
                            {"newHighlightDeltaY",double(ToneRecovery::positiveHighlightDelta(y,y,y*ratio,amount,hm))}});
                    }
            const auto digest=[](const QString &path) {
                QFile file(path); check(file.open(QIODevice::ReadOnly),"Cannot hash probe source");
                return QString::fromLatin1(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex());
            };
            const QString now=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            const QString header=QFileInfo(QString::fromLocal8Bit(__FILE__)).dir().filePath("../src/core/ToneRecovery.h");
            QJsonObject proof{{"campaign_id",QFileInfo(QString::fromLocal8Bit(argv[1])).completeBaseName()},
                {"schema_version",1},{"data_version",1},{"measured_at_utc",now},
                {"protocol_version","tone-recovery-synthetic-v1"},
                {"probe_executable_sha256",digest(QCoreApplication::applicationFilePath())},
                {"production_header_sha256",digest(header)},
                {"units","Y linear sRGB normalized; amount=slider/100"},
                {"old_formula_limit","Old application skipped RGB scaling at Y<=1e-6; rows describe formula only"},
                {"verification","1500000 ramp states: gain bounds, ordering, no new RGB overflow; no real-image parity claim"},
                {"records",rows}};
            QFile target(QString::fromLocal8Bit(argv[1]));
            check(target.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Refuse to overwrite probe JSON");
            const auto bytes=QJsonDocument(proof).toJson(QJsonDocument::Indented);
            check(target.write(bytes)==bytes.size(),"Incomplete probe JSON");
        }
        std::cout << "Tone recovery: bounded gain, tone ordering, slider progression, RGB headroom passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
