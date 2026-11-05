#ifndef IPR_CORE_LOCALVALUES_H
#define IPR_CORE_LOCALVALUES_H

#include "OpenCvBridge.h"

#include <QtGlobal>

#include <opencv2/core.hpp>

#include <array>
#include <unordered_map>
#include <vector>

// Local values of the in-chain local adjustments (ADR-0021): for a setting, one
// float per pixel of the rendered image (CV_32FC1), in the units of its global
// slider: the masks' values weighted by their coverage, summed. A module applies
// at each pixel its global value plus the local one. An empty map changes
// nothing: the module keeps its global path, identical to the bit.
namespace LocalValues {
enum Parameter {
    Exposure, Contrast, Temperature, Tint, Saturation, Vibrance,
    Shadows, Blacks, Highlights, Whites,
    Texture, Clarity, Dehaze, DenoiseLuminance, DenoiseChrominance, Sharpening,
    ParameterCount
};

struct Maps
{
    std::array<cv::Mat, ParameterCount> values;
    cv::Mat &operator[](Parameter parameter) { return values[size_t(parameter)]; }
    const cv::Mat &operator[](Parameter parameter) const { return values[size_t(parameter)]; }
};

// A map a module can read for an image of this size: empty, or one float per pixel.
inline bool fits(const cv::Mat &map, const ipr::Size &size)
{
    return map.empty() || (map.type() == CV_32FC1 && ipr::fromCv(map.size()) == size);
}

// One setting of a module that prepares a curve or a matrix per value, too costly
// per pixel. The local value, multiplied by scale, is rounded to a multiple of
// step; the value used is global plus that multiple, within [low, high].
struct Axis
{
    float global;
    const cv::Mat *local;   // absent, empty or misfit: the global value everywhere
    float scale;
    float low;
    float high;
    float step;
};

// The value combinations actually present in an image, so that a module prepares
// each once and reads the right one per pixel. A local value of zero, or a
// non-finite one, gives exactly the global value. With more than maxCount
// combinations, the step of every axis doubles until they fit.
class Combinations
{
public:
    static constexpr int MaxAxes = 4;
    using Values = std::array<float, MaxAxes>;

    Combinations(std::vector<Axis> axes, const ipr::Size &size, int maxCount);

    // False when no axis has a usable map: the module keeps its global path.
    bool active() const { return m_active; }
    int count() const { return int(m_keys.size()); }
    // The values of a combination, in the order of the axes.
    Values values(int index) const;

    // Reads the combination index of the pixels of one row, on any thread.
    class Row
    {
    public:
        int at(int x);

    private:
        friend class Combinations;
        Row(const Combinations &owner, int y);
        const Combinations &m_owner;
        std::array<const float *, MaxAxes> m_rows{};
        quint64 m_lastKey = 0;
        int m_lastIndex = -1;
    };
    Row row(int y) const;

private:
    quint64 keyAt(const std::array<const float *, MaxAxes> &rows, int x) const;
    std::array<const float *, MaxAxes> rowsAt(int y) const;
    void scan(const ipr::Size &size);

    std::vector<Axis> m_axes;
    std::vector<float> m_steps;
    std::vector<quint64> m_keys;
    std::unordered_map<quint64, int> m_indices;
    bool m_active = false;
};

// The few values, sorted and distinct, at which a neighbourhood module computes
// its image-wide part before mixing per pixel: those a map really needs, rounded
// to step and widened to at most maxCount. Without a usable map: the global value.
std::vector<float> latticeOf(float global, const cv::Mat &local, float low, float high, float step, int maxCount);

// Linear position of a value between the lattice's values, clamped to its ends;
// exactly on a value when it holds it (t = 0).
struct Bracket
{
    size_t low = 0;
    size_t high = 0;
    float t = 0.0f;
};
Bracket bracketOf(const std::vector<float> &lattice, float value);
}

#endif // IPR_CORE_LOCALVALUES_H
