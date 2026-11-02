#ifndef IPR_CORE_EXPOSURE_H
#define IPR_CORE_EXPOSURE_H

#include <opencv2/core.hpp>

// Exposure of a source without a camera signal (rendered rasters, RAW without
// DCP): one unit is one EV in linear light. With a DCP, exposure acts on the
// camera signal instead (CameraProfileEngine::renderFromCameraSignal).
namespace Exposure {
// Linear RGB (CV_32FC3), in place; localEv: per-pixel EV added to exposureEv
// (LocalValues), empty for none. A map that does not fit the image is ignored.
void apply(cv::Mat &linearRgb, float exposureEv, const cv::Mat &localEv = {});
}

#endif // IPR_CORE_EXPOSURE_H
