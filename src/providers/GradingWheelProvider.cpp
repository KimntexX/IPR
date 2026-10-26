#include "GradingWheelProvider.h"

#include "ColorGrading.h"

#include <QImage>
#include <QStringList>

#include <algorithm>
#include <cmath>

GradingWheelProvider::GradingWheelProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage GradingWheelProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    bool zoneOk = false, diameterOk = false;
    const int zone = parts.value(0).toInt(&zoneOk);
    int diameter = parts.value(1).toInt(&diameterOk);
    if (requestedSize.isValid()) diameter = std::min(requestedSize.width(), requestedSize.height());
    QImage wheel(std::clamp(diameter, 1, 1024), std::clamp(diameter, 1, 1024), QImage::Format_ARGB32);
    wheel.fill(Qt::transparent);
    if (zoneOk && diameterOk && zone >= 0 && zone < 3) {
        const double centre = wheel.width() / 2.0;
        const double radius = centre - 0.5;
        constexpr double pi = 3.14159265358979323846;
        for (int y = 0; y < wheel.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(wheel.scanLine(y));
            for (int x = 0; x < wheel.width(); ++x) {
                const double dx = x + 0.5 - centre, dy = y + 0.5 - centre;
                const double distance = std::hypot(dx, dy);
                const double coverage = std::clamp(radius + 0.5 - distance, 0.0, 1.0);
                if (coverage <= 0.0) continue;
                // Hue 0 at the top, clockwise, as the wheel's pointer handling.
                const int hue = int(std::lround(std::fmod(std::atan2(dx, -dy) * 180.0 / pi + 360.0, 360.0))) % 360;
                const int intensity = int(std::lround(std::min(100.0, distance / (0.9 * radius) * 100.0)));
                const cv::Vec3f colour = ColorGrading::displayColour(zone, hue, intensity);
                const auto channel = [](float v) { return int(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
                row[x] = qRgba(channel(colour[0]), channel(colour[1]), channel(colour[2]), int(std::lround(coverage * 255.0)));
            }
        }
    }
    if (size) *size = wheel.size();
    return wheel;
}
