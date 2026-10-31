#include "Dehaze.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <iostream>

namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

// A scene seen through haze on its upper half (I = J t + A (1 - t)), clear below,
// under a sky at the airlight (the top rows), as in a hazy landscape.
cv::Mat hazyScene()
{
    cv::Mat scene(512, 512, CV_32FC3);
    for (int y = 0; y < scene.rows; ++y)
        for (int x = 0; x < scene.cols; ++x) {
            const float detail = 0.08f + 0.06f * float(((x / 16) + (y / 16)) % 2);
            const float t = y < 32 ? 0.0f : y < 256 ? 0.35f : 1.0f;
            const float airlight = 0.8f;
            const cv::Vec3f j(detail * 1.2f, detail, detail * 0.8f);
            scene.at<cv::Vec3f>(y, x) = j * t + cv::Vec3f(airlight, airlight, airlight) * (1 - t);
        }
    return scene;
}

// Detail contrast: mean step of green between neighbouring squares of a row, free
// of the smooth haze gradient.
double contrast(const cv::Mat &rgb, int y0, int y1)
{
    double sum = 0;
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = 64; x < 448 - 16; ++x, ++n)
            sum += std::abs(rgb.at<cv::Vec3f>(y, x)[1] - rgb.at<cv::Vec3f>(y, x + 16)[1]);
    return sum / n;
}

double mean(const cv::Mat &rgb, int y0, int y1)
{
    return cv::mean(rgb(cv::Range(y0, y1), cv::Range(64, 448)))[1];
}

} // namespace

int main()
{
    const cv::Mat scene = hazyScene();

    cv::Mat same = scene.clone();
    Dehaze::apply(same, 0);
    if (!check(cv::norm(same, scene, cv::NORM_INF) == 0, "zero leaves the image unchanged")) return 1;

    cv::Mat clearer = scene.clone();
    Dehaze::apply(clearer, 100);
    if (!check(contrast(clearer, 64, 192) > 1.3 * contrast(scene, 64, 192) && mean(clearer, 64, 192) < mean(scene, 64, 192),
               "+100 removes haze: more contrast and a darker veil in the hazy half")) return 1;

    cv::Mat veiled = scene.clone();
    Dehaze::apply(veiled, -100);
    if (!check(contrast(veiled, 64, 192) < contrast(scene, 64, 192) && mean(veiled, 64, 192) > mean(scene, 64, 192),
               "-100 adds a veil: less contrast and brighter in the hazy half")) return 1;

    bool valid = true;
    for (const cv::Mat *m : {&clearer, &veiled})
        for (int y = 0; y < m->rows; ++y)
            for (int x = 0; x < m->cols; ++x)
                for (int c = 0; c < 3; ++c) {
                    const float v = m->at<cv::Vec3f>(y, x)[c];
                    valid = valid && std::isfinite(v) && v >= 0;
                }
    if (!check(valid, "output stays finite and nonnegative")) return 1;

    // On a 1/4 proxy, the haze is measured over the same full-image radii.
    {
        cv::Mat expected, reduced, proxy;
        cv::resize(clearer, expected, cv::Size(128, 128), 0, 0, cv::INTER_AREA);
        cv::resize(scene, reduced, cv::Size(128, 128), 0, 0, cv::INTER_AREA);
        proxy = reduced.clone();
        Dehaze::apply(proxy, 100, 0.25);
        const double change = cv::norm(expected - reduced), error = cv::norm(proxy - expected);
        if (!check(error < 0.25 * change, "on a 1/4 proxy the dehaze matches the reduced full-resolution one")) return 1;
    }

    cv::Mat empty;
    Dehaze::apply(empty, 50);
    if (!check(empty.empty(), "an empty image is left alone")) return 1;
    return 0;
}
