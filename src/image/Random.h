#ifndef IPR_IMAGE_RANDOM_H
#define IPR_IMAGE_RANDOM_H

#include <cstdint>

namespace ipr {

// OpenCV's multiply-with-carry generator, so that a draw seeded as before gives the same values:
// a robust fit then keeps the same samples. Not for anything that must be unpredictable.
class Random
{
public:
    explicit Random(std::uint64_t seed) : m_state(seed ? seed : 0xffffffffu) {}

    std::uint32_t next()
    {
        m_state = std::uint64_t(std::uint32_t(m_state)) * 4164903690u + (m_state >> 32);
        return std::uint32_t(m_state);
    }

    // An integer in [low, high), or low when they are equal.
    int uniform(int low, int high)
    {
        return low == high ? low : int(next() % std::uint32_t(high - low) + std::uint32_t(low));
    }

private:
    std::uint64_t m_state;
};

} // namespace ipr

#endif // IPR_IMAGE_RANDOM_H
