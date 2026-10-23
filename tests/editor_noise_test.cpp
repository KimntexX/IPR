#include "imageeditor.h"
#include "imageprovider.h"
#include "Sidecar.h"
#include "controlled_memory.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>

namespace {
bool settle(ImageEditor &editor)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll,&QTimer::timeout,&loop,[&] { if (!editor.busy()) loop.quit(); });
    poll.start(10);
    QTimer::singleShot(15000,&loop,&QEventLoop::quit);
    loop.exec();
    return !editor.busy();
}
bool check(bool condition,const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    const QString path=dir.filePath("noise.png"), other=dir.filePath("other.png");
    QImage fixture(256,256,QImage::Format_RGB32);
    cv::RNG rng(156);
    for (int y=0;y<256;++y) for (int x=0;x<256;++x) {
        const int v=std::clamp(100+static_cast<int>(rng.gaussian(8)),0,255);
        fixture.setPixel(x,y,qRgb(v,v,v));
    }
    if (!fixture.save(path) || !fixture.save(other)) return 1;
    ImageProvider provider;
    ImageEditor editor(&provider,nullptr,nullptr,controlledMemory);
    editor.estimateImageNoise();
    if (!check(!editor.noiseAnalysisRunning(),"no analysis without image")) return 1;
    editor.loadImage(QUrl::fromLocalFile(path));
    if (!check(settle(editor) && editor.hasImage(),"fixture loaded")) return 1;
    const QImage before=provider.image();
    editor.estimateImageNoise();
    if (!check(editor.busy() && settle(editor) && editor.noiseAnalysisReady(),"asynchronous analysis completes")) return 1;
    if (!check(provider.image()==before && editor.adjustmentValue("denoiseLuminance")==0,
               "analysis alone never changes pixels or amount")) return 1;
    editor.setDenoiseGlobal(75);
    if (!check(settle(editor) && editor.noiseAnalysisReady() &&
               editor.adjustmentValue("denoiseLuminance")==75 && editor.adjustmentValue("denoiseChrominance")==75,
               "global amount atomically coordinates both components without invalidation")) return 1;
    editor.saveCurrentAdjustments();
    EditRecipe::Settings saved; EditRecipe::UserImageMetadata metadata;
    if (!check(Sidecar::read(path,&saved,&metadata) && saved.denoiseLuminance==75 && saved.denoiseChrominance==75,
               "global intensity persists through existing recipe")) return 1;
    editor.setBrightness(1);
    if (!check(!editor.noiseAnalysisReady() && settle(editor),"upstream edits invalidate analysis state")) return 1;
    editor.estimateImageNoise();
    editor.loadImage(QUrl::fromLocalFile(other));
    if (!check(settle(editor) && !editor.noiseAnalysisReady() && editor.adjustmentValue("denoiseLuminance")==0,
               "navigation discards stale analysis and restores target settings")) return 1;
    editor.estimateImageNoise(); // Destruction must safely finish or cancel active work.
    return 0;
}
