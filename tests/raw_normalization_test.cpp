#include "RawNormalization.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <future>
#include <tuple>
#include <vector>

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QFile bundled(":/config/raw-normalization.json");
    if(!bundled.open(QIODevice::ReadOnly))return 1;
    const auto defaults=QJsonDocument::fromJson(bundled.readAll()).object();
    if(defaults["schema"].toInt()!=4 || defaults["entries"].toArray().size()!=7)return 1;
    QTemporaryDir dir;
    if(!dir.isValid())return 1;
    const auto write=[](const QString &p,const QByteArray &b){QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size();};
    const auto hash=[](const QByteArray &b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());};
    RawNormalization::Context c;
    c.fileName=dir.filePath("unknown-photo-not-in-corpus.CR3"); // Deliberately nonexistent: no RAW read/hash.
    c.profilePath=dir.filePath("neutral.dcp");
    if(!write(c.profilePath,"dcp"))return 1;
    c.cameraMake="Canon";c.cameraModel="EOS R6 Mark III";
    c.decoderId="libraw";c.decoderVersion="0.22.2-Release";
    c.iso=100;c.black.fill(512);c.nativeWhiteLevel=15871;
    c.cameraWhiteBalance=c.nativeBaselineUnknown=true;
    const auto rule=[&](QString model,int low,int high,int white,double ev){return QJsonObject{
        {"camera_make","Canon"},{"camera_model",model},{"profile_sha256",hash("dcp")},
        {"format_extension","cr3"},
        {"decoder_id","libraw"},{"decoder_version","0.22.2-Release"},
        {"native_white_min",13000},{"native_white_max",17000},{"black_min",low},{"black_max",high},
        {"white_level",white},{"baseline_exposure_ev",ev},{"provenance","camera-mode rendering test"}};};
    const QJsonObject low=rule(c.cameraModel,400,640,13995,.26),
        high=rule(c.cameraModel,1600,2400,14351,.28), r7=rule("EOS R7",0,4096,16384,.35);
    const QJsonObject rendering{{"camera_make","Canon"},{"camera_model","EOS R7"},
        {"profile_sha256",hash("dcp")},{"baseline_exposure_ev",.35},
        {"exposure_compensation_ev",-.02},{"provenance","user accepted R7 rendering"}};
    const auto path=dir.filePath("normalization.json");
    const auto save=[&](QJsonArray entries,QJsonArray render=QJsonArray{},QString version="0.22.2-Release"){
        if(version!="0.22.2-Release") {
            for(qsizetype i=0;i<entries.size();++i) {
                auto entry=entries[i].toObject();entry["decoder_version"]=version;entries[i]=entry;
            }
        }
        return write(path,QJsonDocument(QJsonObject{{"schema",4},
            {"entries",entries},{"profile_rendering",render}}).toJson());};
    int assertions=0;
    const auto check=[&](bool ok,const char *why){++assertions;if(!ok)std::cerr<<"FAILED: "<<why<<'\n';return ok;};
    if(!save({low,high,r7},{rendering}))return 1;
    const auto initialToken=RawNormalization::cacheToken(path);
    auto result=RawNormalization::resolve(path,c);
    if(!check(result&&result->whiteLevel==13995&&result->baselineExposureEv==.26&&
              result->profileExposureCompensationEv==0,"new R6 photo, no RAW whitelist and no R7 gain"))return 1;
    for(int iso:{50,125,800,6400,51200}) {
        auto changed=c;changed.iso=iso;
        if(!check(RawNormalization::resolve(path,changed).has_value(),"unseen ISO allowed, not claimed validated"))return 1;
    }
    auto changed=c;changed.black.fill(2047);changed.iso=6400;
    result=RawNormalization::resolve(path,changed);
    if(!check(result&&result->whiteLevel==14351&&result->baselineExposureEv==.28,"R6 high-black mode not low-black constants"))return 1;
    changed=c;changed.cameraModel="EOS R7";changed.iso=400;changed.globalBlack=2044;changed.black={0,1,1,2};
    result=RawNormalization::resolve(path,changed);
    if(!check(result&&result->whiteLevel==16384&&result->baselineExposureEv==.35&&
        result->profileExposureCompensationEv==-.02,"R7 unseen photo/ISO uses own accepted profile compensation"))return 1;
    result=RawNormalization::resolve(path,c);
    if(!check(result&&result->profileExposureCompensationEv==0,"R7 then R6 isolation"))return 1;
    changed=c;changed.nativeWhiteLevel=4095;
    if(!check(!RawNormalization::resolve(path,changed),"unsupported 12bit scale not overwritten by 14bit rule"))return 1;
    changed=c;changed.black.fill(1024);
    if(!check(!RawNormalization::resolve(path,changed),"unknown sensor mode not guessed"))return 1;
    changed=c;changed.cameraModel="Unknown";
    if(!check(!RawNormalization::resolve(path,changed),"unknown camera not assigned R6/R7 calibration"))return 1;
    changed=c;changed.cameraWhiteBalance=false;
    if(!check(!RawNormalization::resolve(path,changed),"auto-WB remains outside policy"))return 1;
    changed=c;changed.nativeBaselineUnknown=false;
    if(!check(!RawNormalization::resolve(path,changed),"no duplicate native exposure"))return 1;
    changed=c;changed.fileName="image.CR2";
    if(!check(!RawNormalization::resolve(path,changed),"CR2 not assigned CR3 policy"))return 1;
    if(!check(!RawNormalization::resolve({},c),"explicit disable has no config I/O"))return 1;
    if(!write(c.profilePath,"different")||!check(!RawNormalization::resolve(path,c),"modified DCP not assigned another profile's correction"))return 1;
    if(!write(c.profilePath,"dcp"))return 1;
    if(!save({low,low})||!check(!RawNormalization::resolve(path,c),"overlapping rules rejected"))return 1;
    if(!save({low},{},"different")||!check(!RawNormalization::resolve(path,c),"backend mismatch"))return 1;
    for(const auto &key:{QString("format_extension"),QString("decoder_id"),QString("decoder_version"),
                         QString("profile_sha256"),QString("white_level"),QString("baseline_exposure_ev"),
                         QString("provenance"),QString("black_min"),QString("native_white_max")}) {
        auto invalid=low;invalid.remove(key);
        if(!save({invalid})||!check(!RawNormalization::resolve(path,c),"missing rule field rejected"))return 1;
    }
    for(const auto &key:{QString("white_level"),QString("baseline_exposure_ev"),QString("black_min")}) {
        auto invalid=low;invalid[key]=key=="white_level"?13995.5:key=="baseline_exposure_ev"?3.0:-1.0;
        if(!save({invalid})||!check(!RawNormalization::resolve(path,c),"invalid numeric rule rejected"))return 1;
    }
    changed=c;changed.cameraModel="EOS R7";
    if(!save({r7},{rendering,rendering})||!check(!RawNormalization::resolve(path,changed),"duplicate render parameters rejected"))return 1;
    auto badRendering=rendering;badRendering["baseline_exposure_ev"]=.26;
    if(!save({r7},{badRendering})||!check(!RawNormalization::resolve(path,changed),"source baseline and render compensation distinct"))return 1;
    if(!check(initialToken!=RawNormalization::cacheToken(path),"config changes invalidate cache"))return 1;
    // Synthetic extensibility check, not a characterization claim about a real Sony camera.
    auto future=low;future["camera_make"]="Future vendor";future["camera_model"]="Future camera";
    future["format_extension"]="future2027";
    changed=c;changed.fileName="unseen.future2027";changed.cameraMake="Future vendor";
    changed.cameraModel="Future camera";
    if(!save({future})||!check(RawNormalization::resolve(path,changed).has_value(),
        "format and brand come from data, no hardcoded Canon/CR3 gate"))return 1;
    changed.decoderId="other-decoder";
    if(!check(!RawNormalization::resolve(path,changed),"another decoder cannot reuse characterization"))return 1;
    if(!save({low})||!check(RawNormalization::resolve(path,c).has_value(),
        "same path reread observes new catalog bytes"))return 1;
    auto modified=low;modified["white_level"]=14000;
    if(!save({modified}))return 1;
    result=RawNormalization::resolve(path,c);
    if(!check(result&&result->whiteLevel==14000,
        "parsed cache is content keyed, not stale path keyed"))return 1;
    auto otherBackend=low;otherBackend["decoder_id"]="future-backend";
    otherBackend["decoder_version"]="1";otherBackend["white_level"]=14100;
    if(!save({low,otherBackend}))return 1;
    changed=c;changed.decoderId="future-backend";changed.decoderVersion="1";
    result=RawNormalization::resolve(path,changed);
    if(!check(result&&result->whiteLevel==14100,"one catalog can isolate different backends"))return 1;
    std::vector<std::future<bool>> readers;
    for(int i=0;i<8;++i) readers.push_back(std::async(std::launch::async,[&,i] {
        const auto context=i%2?c:changed;
        for(int repeat=0;repeat<20;++repeat) {
            const auto resolved=RawNormalization::resolve(path,context);
            if(!resolved||resolved->whiteLevel!=(i%2?13995u:14100u))return false;
        }
        return true;
    }));
    for(auto &reader:readers)if(!check(reader.get(),"concurrent contexts never share mutable selections"))return 1;
    for(int i=0;i<10;++i) {
        modified["white_level"]=14000+i;
        if(!save({modified}))return 1;
        result=RawNormalization::resolve(path,c);
        if(!check(result&&result->whiteLevel==unsigned(14000+i),"catalog revisions survive bounded-cache eviction"))return 1;
    }
    // EOS 60D: equal black and native white in every ISO mode; the Canon
    // specular white of each file selects Adobe's white and baseline.
    {
        auto bundledEntries=defaults["entries"].toArray();
        for(qsizetype i=0;i<bundledEntries.size();++i){auto e=bundledEntries[i].toObject();e["profile_sha256"]=hash("dcp");bundledEntries[i]=e;}
        if(!save(bundledEntries))return 1;
        auto d60=c;d60.fileName="IMG_0001.CR2";d60.cameraModel="EOS 60D";d60.black={2049,2050,2048,2049};d60.nativeWhiteLevel=12279;
        const std::vector<std::tuple<unsigned,unsigned,double>> modes{{11222,11222,-.5},{12279,12279,-.34},{12749,12749,-.28},
                                                                       {15094,15000,0.0},{15677,15000,0.0}};
        for(const auto &[specular,white,ev]:modes){
            d60.specularWhiteLevel=specular;
            result=RawNormalization::resolve(path,d60);
            if(!check(result&&result->whiteLevel==white&&result->baselineExposureEv==ev,"60D mode from the specular white"))return 1;
        }
        d60.specularWhiteLevel=13500;
        if(!check(!RawNormalization::resolve(path,d60),"unseen 60D specular white not guessed"))return 1;
        d60.specularWhiteLevel=0;
        if(!check(!RawNormalization::resolve(path,d60),"unknown specular white matches no bounded rule"))return 1;
        changed=c;changed.specularWhiteLevel=12279;
        if(!check(RawNormalization::resolve(path,changed).has_value(),"rules without specular bounds ignore it"))return 1;
        auto bounded=low;bounded["specular_white_min"]=14000;
        if(!save({bounded})||!check(!RawNormalization::resolve(path,c),"half specular bounds rejected"))return 1;
        bounded["specular_white_max"]=13000;
        if(!save({bounded})||!check(!RawNormalization::resolve(path,c),"inverted specular bounds rejected"))return 1;
    }
    if(!write(path,"{broken")||!check(!RawNormalization::resolve(path,c),"invalid JSON rejected"))return 1;
    if(!write(path,QByteArray(1024*1024+1,' '))||!check(!RawNormalization::resolve(path,c),"size limit"))return 1;
    std::cout<<assertions<<" model-policy assertions passed\n";
    return 0;
}
