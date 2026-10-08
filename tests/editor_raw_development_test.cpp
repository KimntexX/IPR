#include "imageeditor.h"
#include "imageprovider.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <atomic>
#include <iostream>

namespace {
bool settle(ImageEditor &editor)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll,&QTimer::timeout,&loop,[&] {
        if (editor.hasImage() && !editor.busy() && !editor.imageSource().isEmpty()) loop.quit();
    });
    poll.start(10);
    QTimer::singleShot(60000,&loop,&QEventLoop::quit);
    loop.exec();
    return editor.hasImage() && !editor.busy() && !editor.imageSource().isEmpty();
}
bool write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();
}
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    if (argc == 4) {
        // Private corpus validation uses a temporary RAW copy, so the editor
        // cannot create or overwrite the user's original sidecar.
        const QString copy=dir.filePath(QFileInfo(argv[1]).fileName());
        if (!QFile::copy(QString::fromLocal8Bit(argv[1]),copy)) return 2;
        ImageProvider provider;
        ImageEditor editor(&provider);
        editor.loadImage(QUrl::fromLocalFile(copy));
        if (!settle(editor)) return 3;
        QString neutral;
        for (const auto &value:editor.cameraProfiles()) {
            const auto item=value.toMap();
            if (item["name"].toString().endsWith(" Camera Neutral"))
                neutral=item["path"].toString();
        }
        if (neutral.isEmpty()) return 4;
        editor.setCameraProfile(neutral);
        if (!settle(editor)) return 5;
        const auto pixelsHash=[](const QImage &image) {
            return QString::fromLatin1(QCryptographicHash::hash(
                QByteArray(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),
                QCryptographicHash::Sha256).toHex());
        };
        const auto before=pixelsHash(provider.originalImage());
        editor.setRawNormalizationManifest(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[2])));
        if (!settle(editor)) return 6;
        editor.setRawNormalizationEnabled(true);
        if (!settle(editor) || !editor.rawDevelopmentStatus().contains("Normalisation appliquée")) return 7;
        const auto after=pixelsHash(provider.originalImage());
        const auto appliedStatus=editor.rawDevelopmentStatus();
        editor.setRawNormalizationEnabled(false);
        if (!settle(editor) || before!=pixelsHash(provider.originalImage()) || before==after) return 8;
        const QJsonObject proof{{"source",QString::fromLocal8Bit(argv[1])},
            {"profile",neutral},{"applied_status",appliedStatus},
            {"before_preview_sha256",before},{"normalized_preview_sha256",after},
            {"disabled_restores_original",true},{"original_sidecar_untouched",true}};
        QFile output(QString::fromLocal8Bit(argv[3]));
        if (!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return 9;
        const auto bytes=QJsonDocument(proof).toJson();
        if (output.write(bytes)!=bytes.size()) return 10;
        std::cout << bytes.constData();
        return 0;
    }
    const QString raw=dir.filePath("A.uimockraw"), profile=dir.filePath("Camera Neutral.dcp"),
                  manifest=dir.filePath("calibration.json");
    if (!write(raw,"mock") || !write(profile,"mock") || !write(manifest,"{}")) return 1;
    std::atomic<int> decodes{0};
    std::atomic<bool> environmentDisabled{false};
    RawDecoder::registerFormatHandler({"uimockraw"},
        [&](const QString &,const RawDecoder::Options &options,RawDecoder::SourceMetadata *meta,QString *) {
            ++decodes;
            environmentDisabled=!options.useNormalizationEnvironment;
            meta->cameraMake="Mock"; meta->cameraModel="Test camera";
            meta->availableDcpProfiles={profile};
            meta->dcpProfile=options.dcpProfileOverride.isEmpty()?"Adobe Standard":"Camera Neutral";
            meta->rawNormalizationApplied=!options.normalizationManifest.isEmpty() && options.dcpProfileOverride==profile;
            meta->normalizationWhiteLevel=13995;
            meta->normalizationBaselineExposureEv=.26;
            meta->normalizationProvenance="Synthetic wiring test, not a RAW calibration";
            meta->normalizationReason="No matching calibration";
            ColorPipeline::WorkingImage image;
            image.linearRgba=cv::Mat(8,8,CV_32FC4,cv::Scalar(.1f,.2f,.3f,1.f)).clone();
            image.colorSpace=QColorSpace(QColorSpace::SRgbLinear);
            return image;
        });
    ImageProvider provider;
    ImageEditor editor(&provider);
    const auto check=[](bool ok,const char *label) {
        if (!ok) std::cerr << "FAILED: " << label << '\n';
        return ok;
    };
    editor.loadImage(QUrl::fromLocalFile(raw));
    if (!check(settle(editor) && decodes==1 && editor.cameraProfiles().size()==2 && environmentDisabled,
               "initial decode discovers profiles, UI overrides environment")) return 1;
    editor.setCameraProfile(profile);
    if (!check(settle(editor) && decodes==2 && editor.selectedCameraProfile()==profile &&
               editor.rawDevelopmentStatus().contains("Camera Neutral"),
               "profile choice triggers fresh decode and actual status")) return 1;
    editor.setRawNormalizationManifest(QUrl::fromLocalFile(manifest));
    if (!check(settle(editor) && !editor.rawNormalizationEnabled(), "manifest choice does not enable implicitly")) return 1;
    editor.setRawNormalizationEnabled(true);
    if (!check(settle(editor) && decodes==3 && editor.rawDevelopmentStatus().contains("Normalisation appliquée"),
               "opt-in uses new cache key and applies through worker options")) return 1;
    editor.setRawNormalizationEnabled(false);
    if (!check(settle(editor) && decodes==3 && editor.rawDevelopmentStatus().contains("désactivée"),
               "disable restores original cached decode, not corrected pixels")) return 1;
    editor.setRawNormalizationEnabled(true);
    if (!check(settle(editor) && decodes==3, "same calibration safely reuses corrected cache")) return 1;
    if (!write(manifest,"{\"changed\":true}")) return 1;
    editor.setRawNormalizationManifest(QUrl::fromLocalFile(manifest));
    if (!check(settle(editor) && decodes==4, "manifest content changes invalidate decode")) return 1;
    editor.setCameraProfile("");
    if (!check(settle(editor) && decodes==5 && editor.rawDevelopmentStatus().contains("non appliquée"),
               "enabled is not equivalent to applied with another profile")) return 1;
    editor.setCameraProfile("unknown.dcp");
    if (!check(editor.selectedCameraProfile().isEmpty() && decodes==5, "unknown profile rejected")) return 1;
    std::cout << "RAW development session, reload, cache and status assertions passed\n";
    return 0;
}
