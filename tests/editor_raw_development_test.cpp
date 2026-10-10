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
#include <QDataStream>
#include <QElapsedTimer>
#include <atomic>
#include <iostream>

namespace {
bool settle(ImageEditor &editor)
{
    QEventLoop loop;
    QElapsedTimer idle;
    QTimer poll;
    QObject::connect(&poll,&QTimer::timeout,&loop,[&] {
        if (!editor.hasImage() || editor.busy() || editor.imageSource().isEmpty()) {
            idle.invalidate();
        } else if (!idle.isValid()) {
            idle.start();
        } else if (idle.elapsed() >= 150) {
            loop.quit();
        }
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

QByteArray matrixDcp(int numerator)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("II", 2);
    stream << quint16(0x4352) << quint32(8) << quint16(1);
    stream << quint16(50964) << quint16(10) << quint32(9) << quint32(26) << quint32(0);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            stream << qint32(row == col ? numerator : 0) << qint32(2);
    return bytes;
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
        QString alternate;
        for (const auto &value:editor.cameraProfiles()) {
            const auto item=value.toMap();
            if (item["name"].toString().endsWith(" Camera Neutral"))
                neutral=item["path"].toString();
            else if (!item["path"].toString().isEmpty())
                alternate=item["path"].toString();
        }
        if (neutral.isEmpty()) return 4;
        const bool fastAvailable = editor.rawProfileFastPathAvailable();
        qint64 profileChangeMs = -1;
        if (fastAvailable && !alternate.isEmpty()) {
            QElapsedTimer timer;
            timer.start();
            editor.setCameraProfile(alternate);
            if (!settle(editor) || !editor.rawProfileFastPathAvailable()) return 11;
            profileChangeMs = timer.elapsed();
        }
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
            {"dcp_fast_path_available",fastAvailable},{"alternate_profile_change_ms",profileChangeMs},
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
    // Full restart must restore RAW choices before the first decode, including
    // the automatic WB option (not just its checkbox after decoding).
    editor.setCameraProfile(profile);
    if (!settle(editor)) return 1;
    editor.setAutomaticWhiteBalance(true);
    if (!settle(editor)) return 1;
    editor.setColorMixer(6, 23, -17, 31);
    if (!settle(editor)) return 1;
    const QString other = dir.filePath("B.uimockraw");
    if (!write(other, "other")) return 1;
    editor.loadImage(QUrl::fromLocalFile(other));
    if (!check(settle(editor) && editor.selectedCameraProfile().isEmpty() && !editor.rawNormalizationEnabled() &&
               !editor.automaticWhiteBalance(), "new image must not inherit another photo's RAW choices")) return 1;
    {
        ImageProvider restoredProvider;
        ImageEditor restored(&restoredProvider);
        restored.loadImage(QUrl::fromLocalFile(raw));
        if (!check(settle(restored) && restored.selectedCameraProfile() == profile &&
                   restored.rawNormalizationEnabled() && restored.automaticWhiteBalance() &&
                   restored.rawNormalizationManifest() == manifest && restored.colorMixerValue(6,0) == 23,
                   "restart restores profile, manifest, activation, WB and mixer")) return 1;
        restored.resetAdjustments();
        if (!check(settle(restored) && restored.selectedCameraProfile() == profile &&
                   restored.rawNormalizationEnabled() && restored.automaticWhiteBalance() &&
                   restored.colorMixerValue(6,0) == 0,
                   "retouch reset preserves RAW development")) return 1;
    }
    if (!write(profile, "modified profile")) return 1;
    {
        ImageProvider changedProvider;
        ImageEditor changed(&changedProvider);
        changed.loadImage(QUrl::fromLocalFile(raw));
        if (!check(settle(changed) && changed.rawDevelopmentStatus().contains("absente ou modifiée") &&
                   changed.selectedCameraProfile() == profile,
                   "changed profile warns and preserves saved identity")) return 1;
    }

    // Missing resources must remain identifiable for later restoration.
    if (!QFile::rename(profile, profile + ".backup")) return 1;
    {
        ImageProvider missingProvider;
        ImageEditor missing(&missingProvider);
        missing.loadImage(QUrl::fromLocalFile(raw));
        if (!check(settle(missing) && missing.rawDevelopmentStatus().contains("absente ou modifiée") &&
                   missing.selectedCameraProfile() == profile,
                   "missing profile warns without forgetting the saved choice")) return 1;
    }

    // Exercise the real DCP-only fast path with small synthetic camera pixels.
    // No Adobe files or private corpus are needed by this regression test.
    const QString fastRaw = dir.filePath("fast.fastmockraw");
    const QString fastOther = dir.filePath("other.fastmockraw");
    const QString neutral = dir.filePath("Mock Test camera Camera Neutral.dcp");
    const QString alternate = dir.filePath("Mock Test camera Alternate.dcp");
    if (!write(fastRaw,"fast") || !write(fastOther,"other") ||
        !write(neutral,matrixDcp(2)) || !write(alternate,matrixDcp(1))) return 1;
    std::atomic<int> fastDecodes{0};
    RawDecoder::registerFormatHandler({"fastmockraw"},
        [&](const QString &,const RawDecoder::Options &options,RawDecoder::SourceMetadata *meta,QString *error) {
            ++fastDecodes;
            auto source = options.retainedDcpSource ? options.retainedDcpSource : std::make_shared<RawDecoder::DcpSource>();
            source->cameraRgb = cv::Mat(8,8,CV_32FC3,cv::Scalar(.15f,.2f,.1f));
            source->metadata.cameraMake = "Mock";
            source->metadata.cameraModel = "Test camera";
            source->metadata.availableDcpProfiles = {neutral,alternate};
            source->defaultProfile = neutral;
            source->automaticWhiteBalance = options.automaticWhiteBalance;
            source->whiteBalanceKelvin = 5200.;
            return RawDecoder::applyProfile(*source,options.dcpProfileOverride,meta,error);
        });
    QImage edited;
    {
        ImageProvider fastProvider;
        ImageEditor fast(&fastProvider);
        fast.loadImage(QUrl::fromLocalFile(fastRaw));
        if (!check(settle(fast) && fastDecodes == 1 && fast.selectedCameraProfile() == neutral,
                   "automatic Camera Neutral is frozen into recipe")) return 1;
        const QImage baseline = fastProvider.originalImage();
        fast.setBrightness(.75f);
        fast.setColorMixer(1, 12, 13, 14);
        if (!settle(fast)) return 1;
        bool lostImage = false;
        QObject::connect(&fast,&ImageEditor::hasImageChanged,&fast,[&] { lostImage |= !fast.hasImage(); });
        const QString displayed = fast.imageSource();
        fast.setCameraProfile(alternate);
        if (!check(fast.hasImage() && fast.imageSource() == displayed &&
                   !fastProvider.image().isNull(), "profile edit retains visible pixels during computation")) return 1;
        if (!check(settle(fast) && fastDecodes == 1 && !lostImage &&
                   fast.adjustmentValue("brightness") == .75 && fast.colorMixerValue(1,0) == 12 &&
                   fastProvider.originalImage() != baseline,
                   "profile edit uses camera buffer without RAW decode or adjustment reset")) return 1;
        edited = fastProvider.image();
        fast.resetRawDevelopment();
        if (!check(settle(fast) && fastDecodes == 1 && fastProvider.originalImage() == baseline &&
                   fast.adjustmentValue("brightness") == .75,
                   "RAW-only reset preserves edits and immutable camera buffer")) return 1;
        fast.setCameraProfile(alternate);
        if (!check(settle(fast) && fastProvider.image() == edited, "repeat profile selection is deterministic")) return 1;
        // Destructor flushes even before the sidecar debounce fires.
    }
    {
        ImageProvider fastProvider;
        ImageEditor fast(&fastProvider);
        fast.loadImage(QUrl::fromLocalFile(fastRaw));
        if (!check(settle(fast) && fast.selectedCameraProfile() == alternate && fastProvider.image() == edited,
                   "fresh editor reproduces edited pixels from saved RAW recipe")) return 1;
        if (!write(neutral,"broken DCP")) return 1;
        fast.setCameraProfile(neutral);
        if (!check(settle(fast) && fast.selectedCameraProfile() == alternate &&
                   fastProvider.image() == edited && fast.rawDevelopmentStatus().contains("refusée"),
                   "failed profile edit rolls back recipe and pixels")) return 1;
        fast.loadImage(QUrl::fromLocalFile(fastOther));
        fast.loadImage(QUrl::fromLocalFile(fastRaw));
        if (!check(settle(fast) && fast.selectedCameraProfile() == alternate && fastProvider.image() == edited,
                   "rapid navigation coalesces to latest image and restores its recipe")) return 1;
    }
    const QString opticRaw = dir.filePath("optical.opticmockraw");
    const QString lens = dir.filePath("test-lens.lcp");
    if (!write(opticRaw, "optical") || !write(lens, "mock LCP")) return 1;
    std::atomic<int> opticDecodes{0};
    RawDecoder::registerFormatHandler({"opticmockraw"},
        [&](const QString &, const RawDecoder::Options &options, RawDecoder::SourceMetadata *meta, QString *) {
            ++opticDecodes;
            const QString path = options.automaticLcpSelection ? lens : options.lcpProfileOverride;
            meta->lcpPath = path;
            meta->lcpProfile = QFileInfo(path).completeBaseName();
            QFile input(path);
            if (input.open(QIODevice::ReadOnly))
                meta->lcpSha256 = QString::fromLatin1(QCryptographicHash::hash(input.readAll(), QCryptographicHash::Sha256).toHex());
            meta->lensGeometryApplied = !path.isEmpty() && options.lensCorrectionEnabled;
            meta->lensVignettingApplied = meta->lensGeometryApplied;
            meta->chromaticAberrationApplied = !path.isEmpty() && options.chromaticAberrationEnabled;
            ColorPipeline::WorkingImage result;
            result.linearRgba = cv::Mat(8,8,CV_32FC4,cv::Scalar(
                meta->lensGeometryApplied ? .3f : .1f,
                meta->chromaticAberrationApplied ? .4f : .2f, .1f, 1.f)).clone();
            result.colorSpace = QColorSpace(QColorSpace::SRgbLinear);
            return result;
        });
    QImage opticalBaseline;
    {
        ImageProvider p;
        ImageEditor optics(&p);
        optics.loadImage(QUrl::fromLocalFile(opticRaw));
        if (!check(settle(optics) && optics.opticalDevelopmentStatus().contains("test-lens"),
                   "automatic LCP discovered and frozen on first open")) return 1;
        optics.setBrightness(.5f);
        if (!settle(optics)) return 1;
        const QImage beforeOptics = p.image();
        optics.setLensCorrectionEnabled(false);
        if (!check(settle(optics) && opticDecodes == 2 && !optics.lensCorrectionEnabled() &&
                   optics.chromaticAberrationEnabled() && p.image() != beforeOptics &&
                   optics.adjustmentValue("brightness") == .5,
                   "lens toggle invalidates decode, preserves edits, keeps CA")) return 1;
        optics.setChromaticAberrationEnabled(false);
        if (!check(settle(optics) && opticDecodes == 3 && !optics.chromaticAberrationEnabled(),
                   "CA toggle has independent cache identity")) return 1;
        opticalBaseline = p.image();
        optics.setChromaticAberrationEnabled(true);
        if (!settle(optics)) return 1;
        optics.setChromaticAberrationEnabled(false);
        if (!check(settle(optics) && opticDecodes == 3 && p.image() == opticalBaseline,
                   "cached optical states restore exactly without cumulative resampling")) return 1;
    }
    {
        ImageProvider p;
        ImageEditor optics(&p);
        optics.loadImage(QUrl::fromLocalFile(opticRaw));
        if (!check(settle(optics) && !optics.lensCorrectionEnabled() && !optics.chromaticAberrationEnabled() &&
                   p.image() == opticalBaseline && optics.opticalDevelopmentStatus().contains("test-lens"),
                   "v6 restart restores saved LCP, toggles and edited pixels")) return 1;
    }
    if (!write(lens, "changed LCP")) return 1;
    {
        ImageProvider p;
        ImageEditor optics(&p);
        optics.loadImage(QUrl::fromLocalFile(opticRaw));
        if (!check(settle(optics) && optics.rawDevelopmentStatus().contains("LCP enregistré est absent ou modifié") &&
                   optics.opticalDevelopmentStatus().contains("Aucun profil LCP"),
                   "modified LCP warns and never silently substitutes bytes")) return 1;
    }
    if (!QFile::rename(lens, lens + ".backup")) return 1;
    {
        ImageProvider p;
        ImageEditor optics(&p);
        optics.loadImage(QUrl::fromLocalFile(opticRaw));
        if (!check(settle(optics) && optics.rawDevelopmentStatus().contains("LCP enregistré est absent ou modifié"),
                   "missing LCP keeps saved fingerprint and warning")) return 1;
    }
    std::cout << "RAW recipe v6, optics, restart, fingerprints, profile-only processing and rollback passed\n";
    return 0;
}
