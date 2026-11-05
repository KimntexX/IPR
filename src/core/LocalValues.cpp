#include "LocalValues.h"
#include "Parallel.h"
#include "OpenCvBridge.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_set>

namespace LocalValues {
namespace {
constexpr int KeyBits = 16;
constexpr int KeyOffset = 1 << (KeyBits - 1);
constexpr int MaxMultiple = KeyOffset - 1;
constexpr int MaxWidenings = 16;
}

Combinations::Combinations(std::vector<Axis> axes, const ipr::Size &size, int maxCount)
    : m_axes(std::move(axes))
{
    CV_Assert(int(m_axes.size()) <= MaxAxes && maxCount > 0);
    for (Axis &axis : m_axes) {
        if (axis.local && (axis.local->empty() || !fits(*axis.local, size) || axis.scale == 0.0f))
            axis.local = nullptr;
        m_active = m_active || axis.local;
        m_steps.push_back(axis.step);
    }
    if (!m_active) {
        // Still one combination, the global values, for a caller that reads it.
        m_keys.push_back(keyAt({}, 0));
        m_indices.emplace(m_keys.front(), 0);
        return;
    }
    scan(size);
    for (int widening = 0; int(m_keys.size()) > maxCount && widening < MaxWidenings; ++widening) {
        for (float &step : m_steps) step *= 2.0f;
        scan(size);
    }
}

void Combinations::scan(const ipr::Size &size)
{
    std::unordered_set<quint64> found;
    std::mutex mutex;
    ipr::parallelRows(size.height, [&](int from, int to) {
        std::unordered_set<quint64> local;
        for (int y = from; y < to; ++y) {
            const auto rows = rowsAt(y);
            quint64 last = 0;
            bool hasLast = false;
            for (int x = 0; x < size.width; ++x) {
                const quint64 key = keyAt(rows, x);
                if (hasLast && key == last) continue;
                local.insert(key);
                last = key;
                hasLast = true;
            }
        }
        const std::lock_guard<std::mutex> lock(mutex);
        found.insert(local.cbegin(), local.cend());
    });
    m_keys.assign(found.cbegin(), found.cend());
    std::sort(m_keys.begin(), m_keys.end());
    m_indices.clear();
    for (size_t index = 0; index < m_keys.size(); ++index)
        m_indices.emplace(m_keys[index], int(index));
}

std::array<const float *, Combinations::MaxAxes> Combinations::rowsAt(int y) const
{
    std::array<const float *, MaxAxes> rows{};
    for (size_t axis = 0; axis < m_axes.size(); ++axis)
        rows[axis] = m_axes[axis].local ? m_axes[axis].local->ptr<float>(y) : nullptr;
    return rows;
}

quint64 Combinations::keyAt(const std::array<const float *, MaxAxes> &rows, int x) const
{
    quint64 key = 0;
    for (size_t axis = 0; axis < m_axes.size(); ++axis) {
        int multiple = 0;
        if (rows[axis]) {
            const float local = rows[axis][x] * m_axes[axis].scale;
            if (std::isfinite(local))
                multiple = int(std::clamp(std::round(double(local) / double(m_steps[axis])),
                                          -double(MaxMultiple), double(MaxMultiple)));
        }
        key |= quint64(quint32(multiple + KeyOffset)) << (KeyBits * int(axis));
    }
    return key;
}

Combinations::Values Combinations::values(int index) const
{
    Values values{};
    const quint64 key = m_keys[size_t(index)];
    for (size_t axis = 0; axis < m_axes.size(); ++axis) {
        const int multiple = int((key >> (KeyBits * int(axis))) & ((1u << KeyBits) - 1)) - KeyOffset;
        const Axis &settings = m_axes[axis];
        values[axis] = multiple == 0 ? settings.global
            : std::clamp(settings.global + float(multiple) * m_steps[axis], settings.low, settings.high);
    }
    return values;
}

Combinations::Row::Row(const Combinations &owner, int y) : m_owner(owner), m_rows(owner.rowsAt(y)) {}

int Combinations::Row::at(int x)
{
    const quint64 key = m_owner.keyAt(m_rows, x);
    if (m_lastIndex < 0 || key != m_lastKey) {
        m_lastKey = key;
        m_lastIndex = m_owner.m_indices.at(key);
    }
    return m_lastIndex;
}

Combinations::Row Combinations::row(int y) const
{
    return Row(*this, y);
}

std::vector<float> latticeOf(float global, const cv::Mat &local, float low, float high, float step, int maxCount)
{
    const Combinations combinations({{global, &local, 1.0f, low, high, step}}, ipr::fromCv(local.size()), maxCount);
    std::vector<float> lattice;
    for (int index = 0; index < combinations.count(); ++index)
        lattice.push_back(combinations.values(index)[0]);
    std::sort(lattice.begin(), lattice.end());
    lattice.erase(std::unique(lattice.begin(), lattice.end()), lattice.end());
    return lattice;
}

Bracket bracketOf(const std::vector<float> &lattice, float value)
{
    const auto above = std::upper_bound(lattice.cbegin(), lattice.cend(), value);
    if (above == lattice.cbegin()) return {0, 0, 0.0f};
    if (above == lattice.cend()) return {lattice.size() - 1, lattice.size() - 1, 0.0f};
    const size_t high = size_t(above - lattice.cbegin()), low = high - 1;
    return {low, high, (value - lattice[low]) / (lattice[high] - lattice[low])};
}
}
