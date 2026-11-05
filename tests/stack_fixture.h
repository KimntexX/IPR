#ifndef IPR_TESTS_STACK_FIXTURE_H
#define IPR_TESTS_STACK_FIXTURE_H

#include "CameraProfileEngine.h"
#include "StackSession.h"
#include "OpenCvBridge.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <opencv2/imgproc.hpp>

#include <atomic>
#include <functional>
#include <memory>

namespace StackFixture {

inline cv::Mat scene(const ipr::Size &size)
{
    cv::RNG random(7);
    cv::Mat image = cv::Mat::zeros(ipr::toCv(size), CV_32FC3);
    for (const double sigma : {1.5, 5.0, 18.0}) {
        cv::Mat layer(ipr::toCv(size), CV_32FC3);
        random.fill(layer, cv::RNG::NORMAL, cv::Scalar::all(0), cv::Scalar::all(1));
        cv::GaussianBlur(layer, layer, cv::Size(), sigma);
        cv::Scalar mean, deviation;
        cv::meanStdDev(layer, mean, deviation);
        image += layer * (0.05 / std::max(1e-6, deviation[0]));
    }
    return image + cv::Scalar::all(0.3);
}

// Frames named frame0, frame1... in place of a RAW decoder: small views of one scene.
struct Source
{
    cv::Mat base = scene(ipr::Size(500, 340));
    std::shared_ptr<CameraProfileEngine::DcpWorkingContext> context;
    std::shared_ptr<std::atomic_int> calls = std::make_shared<std::atomic_int>(0);
    int slowMs = 0;

    StackSession::Decoded operator()(const QString &file, const std::function<bool()> &cancelled) const
    {
        ++*calls;
        for (int waited = 0; waited < slowMs && !cancelled(); waited += 5)
            QThread::msleep(5);
        const int index = QFileInfo(file).baseName().remove(QStringLiteral("frame")).toInt();
        cv::Mat moved;
        const cv::Matx23d shift(1, 0, (index - 3) * 2.3, 0, 1, (index - 3) * -1.7);
        cv::warpAffine(base, moved, shift, base.size(), cv::INTER_LANCZOS4, cv::BORDER_REPLICATE);
        cv::RNG random(uint64_t(100 + index));
        cv::Mat noise(base.size(), CV_32FC3);
        random.fill(noise, cv::RNG::NORMAL, cv::Scalar::all(0), cv::Scalar::all(0.01));
        StackSession::Decoded decoded;
        decoded.cameraRgb = moved + noise;
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
