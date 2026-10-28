#include "RawInput.h"

#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

// An RGGB mosaic with LibRaw's labelling of the second green.
RawInput::Input sample()
{
    RawInput::Input input;
    input.mosaic = cv::Mat(4, 6, CV_16UC1, cv::Scalar(2048));
    input.pattern = {{{RawInput::Red, RawInput::Green}, {RawInput::Green2, RawInput::Blue}}};
    input.black = {512, 512, 512, 512};
    input.white = 16383;
    input.asShotMultipliers = {2.0, 1.0, 1.5, 1.0};
    return input;
}
} // namespace

int main()
{
    bool ok = check(RawInput::valid(sample()), "a Bayer mosaic with its levels is valid");
    RawInput::Input input = sample();
    input.pattern[1][0] = RawInput::Green;
    ok &= check(RawInput::valid(input), "both greens may share the label");
    input = sample();
    input.pattern[0][0] = RawInput::Blue;
    ok &= check(!RawInput::valid(input), "two blues and no red is not Bayer");
    input = sample();
    input.white = 400;
    ok &= check(!RawInput::valid(input), "the white must be above every black");
    input = sample();
    input.spatialBlack = true;
    ok &= check(!RawInput::valid(input), "a spatial black stays on the decoder's development");
    input = sample();
    input.asShotMultipliers[2] = 0.0;
    ok &= check(!RawInput::valid(input), "a missing multiplier is refused");
    input = sample();
    input.mosaic = cv::Mat(4, 6, CV_32FC1);
    ok &= check(!RawInput::valid(input) && !RawInput::valid(RawInput::Input{}), "the mosaic is 16-bit and not empty");
    if (ok) std::cout << "rawInputTests passed\n";
    return ok ? 0 : 1;
}
