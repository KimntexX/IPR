#include "Image.h"

#include <atomic>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace ipr {

namespace {

constexpr std::align_val_t pixelAlignment{64};

void checkLayout(Size size, int channels)
{
    if (size.width < 0 || size.height < 0)
        throw std::invalid_argument("An image has no negative side");
    if (channels < 1 || channels > 4)
        throw std::invalid_argument("An image has one to four channels");
}

struct AlignedDelete
{
    void operator()(void *pixels) const { ::operator delete(pixels, pixelAlignment); }
};

} // namespace

// What every image on the same pixels holds together.
struct Image::Block
{
    std::shared_ptr<void> owner;
    std::uint8_t *first = nullptr;   // the extent of the pixels, for sharesPixelsWith
    std::uint8_t *last = nullptr;
    std::atomic<bool> frozen{false};
};

std::size_t depthBytes(Depth depth)
{
    switch (depth) {
    case Depth::U8: return 1;
    case Depth::U16: return 2;
    case Depth::S16: return 2;
    case Depth::S32: return 4;
    case Depth::F32: return 4;
    case Depth::F64: return 8;
    }
    throw std::invalid_argument("Unknown depth");
}

Image::Image(Image &&other) noexcept
    : m_block(std::move(other.m_block))
    , m_pixels(std::exchange(other.m_pixels, nullptr))
    , m_size(std::exchange(other.m_size, Size{}))
    , m_stepBytes(std::exchange(other.m_stepBytes, 0))
    , m_depth(other.m_depth)
    , m_channels(std::exchange(other.m_channels, 0))
{
}

Image &Image::operator=(Image &&other) noexcept
{
    if (this != &other) {
        m_block = std::move(other.m_block);
        m_pixels = std::exchange(other.m_pixels, nullptr);
        m_size = std::exchange(other.m_size, Size{});
        m_stepBytes = std::exchange(other.m_stepBytes, 0);
        m_depth = other.m_depth;
        m_channels = std::exchange(other.m_channels, 0);
    }
    return *this;
}

Image Image::create(Size size, Depth depth, int channels)
{
    checkLayout(size, channels);
    Image image;
    image.m_depth = depth;
    image.m_channels = channels;
    const std::size_t pixel = depthBytes(depth) * std::size_t(channels);
    if (size.empty())
        return image;
    const std::size_t limit = std::size_t(std::numeric_limits<std::ptrdiff_t>::max());
    if (std::size_t(size.width) > limit / pixel || std::size_t(size.width) * pixel > limit / std::size_t(size.height))
        throw std::bad_alloc();
    const std::size_t step = std::size_t(size.width) * pixel;
    const std::size_t total = step * std::size_t(size.height);
    std::shared_ptr<void> pixels(::operator new(total, pixelAlignment), AlignedDelete{});
    image.m_block = std::make_shared<Block>();
    image.m_pixels = static_cast<std::uint8_t *>(pixels.get());
    image.m_block->first = image.m_pixels;
    image.m_block->last = image.m_pixels + total;
    image.m_block->owner = std::move(pixels);
    image.m_size = size;
    image.m_stepBytes = std::ptrdiff_t(step);
    return image;
}

Image Image::zeros(Size size, Depth depth, int channels)
{
    Image image = create(size, depth, channels);
    if (!image.empty())
        std::memset(image.m_pixels, 0, std::size_t(image.m_stepBytes) * std::size_t(image.height()));
    return image;
}

Image Image::wrap(void *pixels, Size size, Depth depth, int channels, std::ptrdiff_t stepBytes, std::shared_ptr<void> owner)
{
    checkLayout(size, channels);
    Image image;
    image.m_depth = depth;
    image.m_channels = channels;
    if (size.empty())
        return image;
    const std::size_t pixel = depthBytes(depth) * std::size_t(channels);
    if (!pixels)
        throw std::invalid_argument("No pixels to wrap");
    if (stepBytes < 0 || std::size_t(stepBytes) / pixel < std::size_t(size.width))
        throw std::invalid_argument("A row does not fit in its step");
    image.m_block = std::make_shared<Block>();
    image.m_pixels = static_cast<std::uint8_t *>(pixels);
    image.m_block->first = image.m_pixels;
    image.m_block->last = image.m_pixels + stepBytes * std::ptrdiff_t(size.height - 1) +
                          std::ptrdiff_t(std::size_t(size.width) * pixel);
    image.m_block->owner = std::move(owner);
    image.m_size = size;
    image.m_stepBytes = stepBytes;
    return image;
}

Image Image::wrapReadOnly(const void *pixels, Size size, Depth depth, int channels, std::ptrdiff_t stepBytes,
                          std::shared_ptr<void> owner)
{
    // Never written through: a frozen image refuses every way of doing so.
    Image image = wrap(const_cast<void *>(pixels), size, depth, channels, stepBytes, std::move(owner));
    if (image.m_block)
        image.m_block->frozen.store(true, std::memory_order_relaxed);
    return image;
}

Image Image::clone() const
{
    Image copy = create(m_size, m_depth, std::max(m_channels, 1));
    copy.m_channels = m_channels;
    const std::size_t rowLength = std::size_t(m_size.width) * (m_channels > 0 ? pixelBytes() : 0);
    for (int y = 0; y < height(); ++y)
        std::memcpy(copy.m_pixels + copy.m_stepBytes * y, m_pixels + m_stepBytes * y, rowLength);
    return copy;
}

Image Image::share() const
{
    Image other;
    other.m_depth = m_depth;
    other.m_channels = m_channels;
    if (empty())
        return other;
    m_block->frozen.store(true, std::memory_order_release);
    other.m_block = m_block;
    other.m_pixels = m_pixels;
    other.m_size = m_size;
    other.m_stepBytes = m_stepBytes;
    return other;
}

Image Image::partOf(const Recti &area) const
{
    Image other;
    other.m_depth = m_depth;
    other.m_channels = m_channels;
    if (empty() || area.empty() || area.x < 0 || area.y < 0 || area.width > width() - area.x || area.height > height() - area.y)
        return other;
    other.m_block = m_block;
    other.m_pixels = m_pixels + m_stepBytes * std::ptrdiff_t(area.y) + std::ptrdiff_t(area.x) * std::ptrdiff_t(pixelBytes());
    other.m_size = {area.width, area.height};
    other.m_stepBytes = m_stepBytes;
    return other;
}

Image Image::view(const Recti &area) const
{
    Image other = partOf(area);
    if (!other.empty())
        other.m_block->frozen.store(true, std::memory_order_release);
    return other;
}

Image Image::mutableView(const Recti &area)
{
    if (frozen())
        throw std::logic_error("These pixels are shared: clone them to change them");
    return partOf(area);
}

bool Image::continuous() const
{
    return !empty() && (height() == 1 || std::size_t(m_stepBytes) == std::size_t(width()) * pixelBytes());
}

bool Image::frozen() const
{
    return m_block && m_block->frozen.load(std::memory_order_acquire);
}

bool Image::sharesPixelsWith(const Image &other) const
{
    if (empty() || other.empty())
        return false;
    return m_block == other.m_block ||
           (m_block->first < other.m_block->last && other.m_block->first < m_block->last);
}

const std::uint8_t *Image::rowBytes(int y) const
{
    if (y < 0 || y >= height())
        throw std::out_of_range("No such row");
    return m_pixels + m_stepBytes * std::ptrdiff_t(y);
}

std::uint8_t *Image::mutableRowBytes(int y)
{
    if (y < 0 || y >= height())
        throw std::out_of_range("No such row");
    if (frozen())
        throw std::logic_error("These pixels are shared: clone them to change them");
    return m_pixels + m_stepBytes * std::ptrdiff_t(y);
}

} // namespace ipr
