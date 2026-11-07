#ifndef IPR_IMAGE_IMAGE_H
#define IPR_IMAGE_IMAGE_H

#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ipr {

// S16 and S32: signed integers, for labels and indices.
enum class Depth { U8, U16, S16, S32, F32, F64 };

std::size_t depthBytes(Depth depth);

// A buffer of pixels whose sharing is written in the code: there is no copy by assignment, only
// clone (new pixels), share (the same pixels, read only) and mutableView (a writable part).
//
// A shared image never changes: share freezes the pixels for every holder, for good, and asking
// to write them afterwards throws instead of changing what another thread may be reading. To
// change them, clone. Standard C++ only. Reading from several threads is safe; writing the same
// pixels from several threads is not, and writing separate rows is.
class Image
{
public:
    Image() = default;
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
    Image(Image &&other) noexcept;
    Image &operator=(Image &&other) noexcept;
    ~Image() = default;

    // New pixels, not initialized, rows one after the other, aligned on 64 bytes. Throws
    // std::invalid_argument on a negative size or a channel count outside [1, 4], std::bad_alloc
    // when memory lacks or the size does not fit. An empty size gives an empty image.
    static Image create(Size size, Depth depth, int channels);
    // The same, every byte at zero.
    static Image zeros(Size size, Depth depth, int channels);
    // Pixels held elsewhere. owner keeps them alive until the last image on them goes; null
    // means the caller answers for it. stepBytes: from one row to the next, at least a row's
    // bytes. Throws std::invalid_argument on null pixels with a size, or an invalid layout.
    static Image wrap(void *pixels, Size size, Depth depth, int channels, std::ptrdiff_t stepBytes,
                      std::shared_ptr<void> owner = {});
    // The same, frozen from the start: these pixels are only ever read.
    static Image wrapReadOnly(const void *pixels, Size size, Depth depth, int channels, std::ptrdiff_t stepBytes,
                              std::shared_ptr<void> owner = {});

    Image clone() const;
    // The same pixels, read only, from now on for every holder. Empty stays empty.
    Image share() const;
    // A part of the same pixels, as read only as share. Empty when the rectangle leaves the image.
    Image view(const Recti &area) const;
    // A part of the same pixels to write, such as a tile. The image stays writable, and answers
    // for what this view writes. Throws std::logic_error on frozen pixels. Empty when the
    // rectangle leaves the image.
    Image mutableView(const Recti &area);
    // New pixels copied from a part: nothing shared, nothing frozen. Empty when the rectangle
    // leaves the image.
    Image copy(const Recti &area) const;

    bool empty() const { return m_size.empty(); }
    Size size() const { return m_size; }
    int width() const { return m_size.width; }
    int height() const { return m_size.height; }
    int channels() const { return m_channels; }
    Depth depth() const { return m_depth; }
    std::size_t pixelBytes() const { return depthBytes(m_depth) * std::size_t(m_channels); }
    std::ptrdiff_t stepBytes() const { return m_stepBytes; }
    bool continuous() const;
    bool frozen() const;
    bool sharesPixelsWith(const Image &other) const;
    // The block of memory the pixels lie in, the same for every image sharing it, and its size in
    // bytes: to count shared pixels once. Null and zero for an empty image.
    const void *block() const;
    std::size_t blockBytes() const;

    // Throws std::out_of_range outside the image: a row is the place to check, a pixel is not.
    template <typename T>
    const T *row(int y) const
    {
        return reinterpret_cast<const T *>(rowBytes(y));
    }
    // The same, and std::logic_error on frozen pixels.
    template <typename T>
    T *mutableRow(int y)
    {
        return reinterpret_cast<T *>(mutableRowBytes(y));
    }

private:
    struct Block;

    const std::uint8_t *rowBytes(int y) const;
    std::uint8_t *mutableRowBytes(int y);
    Image partOf(const Recti &area) const;

    std::shared_ptr<Block> m_block;
    std::uint8_t *m_pixels = nullptr;
    Size m_size;
    std::ptrdiff_t m_stepBytes = 0;
    Depth m_depth = Depth::U8;
    int m_channels = 0;
};

// Pixels by (y, x) without a check, for the stencils that read around each pixel: the image is
// checked once, here, and keeping (y, x) inside it is the caller's. T is the whole pixel, such as
// float or Vec3f. Valid while the image's pixels live; an empty image gives a reader of nothing.
template <typename T>
class PixelReader
{
public:
    explicit PixelReader(const Image &image)
        : m_first(image.empty() ? nullptr : image.row<std::uint8_t>(0)), m_step(image.stepBytes())
    {
    }
    const T &operator()(int y, int x) const
    {
        return reinterpret_cast<const T *>(m_first + m_step * std::ptrdiff_t(y))[x];
    }

private:
    const std::uint8_t *m_first;
    std::ptrdiff_t m_step;
};

// The same to write, refused on frozen pixels as mutableRow is.
template <typename T>
class PixelWriter
{
public:
    explicit PixelWriter(Image &image)
        : m_first(image.empty() ? nullptr : image.mutableRow<std::uint8_t>(0)), m_step(image.stepBytes())
    {
    }
    T &operator()(int y, int x) const
    {
        return reinterpret_cast<T *>(m_first + m_step * std::ptrdiff_t(y))[x];
    }

private:
    std::uint8_t *m_first;
    std::ptrdiff_t m_step;
};

} // namespace ipr

#endif // IPR_IMAGE_IMAGE_H
