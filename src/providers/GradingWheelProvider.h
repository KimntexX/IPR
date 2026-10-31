#ifndef IPR_PROVIDERS_GRADINGWHEELPROVIDER_H
#define IPR_PROVIDERS_GRADINGWHEELPROVIDER_H

#include <QQuickImageProvider>

// Colour grading wheel of a zone for QML ("image://gradingwheel/<zone>/<diameter>"):
// every pixel shows the colour its hue and intensity give (ColorGrading::displayColour),
// full intensity at 90 % of the radius, transparent outside the wheel.
class GradingWheelProvider final : public QQuickImageProvider
{
public:
    GradingWheelProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

#endif // IPR_PROVIDERS_GRADINGWHEELPROVIDER_H
