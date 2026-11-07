#ifndef IPR_TESTS_STACK_FIXTURE_H
#define IPR_TESTS_STACK_FIXTURE_H

#include "CameraProfileEngine.h"
#include "StackSession.h"
#include "Filters.h"
#include "image_testing.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QThread>
#include <QUrl>

#include <atomic>
#include <functional>
#include <memory>

namespace StackFixture {

inline ipr::Image scene(const ipr::Size &size)
{
    ipr::Image image = ipr::Image::zeros(size, ipr::Depth::F32, 3);
    unsigned seed = 7;
    for (const double sigma : {1.5, 5.0, 18.0}) {
        const ipr::Image layer = ipr::gaussianBlurred(imagetest::normalNoise(size, 3, 0.0, 1.0, seed++), sigma);
        std::vector<double> mean, deviation;
        ipr::meanAndDeviationOf(layer, mean, deviation);
        const float weight = float(0.05 / std::max(1e-6, deviation[0]));
        image = ipr::mapped(image, layer, [weight](float a, float b) { return a + b * weight; });
    }
    return ipr::mapped(image, [](float v) { return v + 0.3f; });
}

// Frames named frame0, frame1... in place of a RAW decoder: small views of one scene.
struct Source
{
    // Shared, read only: the source is copied into the session's decoder.
    std::shared_ptr<const ipr::Image> base = std::make_shared<const ipr::Image>(scene(ipr::Size(500, 340)));
    std::shared_ptr<CameraProfileEngine::DcpWorkingContext> context;
    std::shared_ptr<std::atomic_int> calls = std::make_shared<std::atomic_int>(0);
    int slowMs = 0;

    StackSession::Decoded operator()(const QString &file, const std::function<bool()> &cancelled) const
    {
        ++*calls;
        for (int waited = 0; waited < slowMs && !cancelled(); waited += 5)
            QThread::msleep(5);
        const int index = QFileInfo(file).baseName().remove(QStringLiteral("frame")).toInt();
        const ipr::Matrix33d shift(1, 0, (index - 3) * 2.3, 0, 1, (index - 3) * -1.7, 0, 0, 1);
        const ipr::Image moved = imagetest::moved(*base, shift, base->size());
        const ipr::Image noise = imagetest::normalNoise(base->size(), 3, 0.0, 0.01, unsigned(100 + index));
        StackSession::Decoded decoded;
        decoded.cameraRgb = ipr::mapped(moved, noise, [](float a, float b) { return a + b; });
        decoded.context = context;
        decoded.metadata.cameraMake = QStringLiteral("Canon");
        decoded.metadata.cameraModel = QStringLiteral("EOS R6 Mark III");
        return decoded;
    }
};

inline QStringList frames(const QDir &folder, int count)
{
    QStringList urls;
    for (int i = 0; i < count; ++i)
        urls.append(QUrl::fromLocalFile(folder.filePath(QStringLiteral("frame%1.CR3").arg(i))).toString());
    return urls;
}

} // namespace StackFixture

#endif // IPR_TESTS_STACK_FIXTURE_H
