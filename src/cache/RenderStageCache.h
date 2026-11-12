#ifndef IPR_CACHE_RENDERSTAGECACHE_H
#define IPR_CACHE_RENDERSTAGECACHE_H

#include "LocalValues.h"

#include <QByteArray>

#include "Image.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

// Outputs of the preview pipeline's costly stages (tones, perceptual colour,
// denoise, local contrast, dehaze), so a render resumes after the last stage
// whose settings did not change. Each stage's key covers the settings of that
// stage and of every stage before it; the entries belong to one source and one
// pixel scale. At most two outputs are kept (about 34 MB each on the 2048-pixel
// proxy): the one just before the stage being edited and the tones' one.
class RenderStageCache final
{
public:
    static constexpr int StageCount = 5;
    using Keys = std::array<QByteArray, StageCount>;

    struct Start
    {
        int stage = -1;   // deepest stage resumed from, -1 to render from the source
        ipr::Image rgb;      // private copy of that stage's output
        std::array<bool, StageCount> keep{};   // outputs to hand to store()
        Keys keys;
        std::uint64_t generation = 0;
    };

    Start begin(const ipr::Image &source, double pixelScale, const Keys &keys);
    // Ignored when the cache was cleared or rebound since begin().
    void store(const Start &start, int stage, const ipr::Image &rgb);
    // Copy of a kept stage output for this source, scale and key; empty otherwise.
    // Reads only: the keep policy of the renders is left as it is.
    ipr::Image peek(const ipr::Image &source, double pixelScale, int stage, const QByteArray &key) const;
    // What the masks add, for this source and scale, read-only for those who get them: a render
    // that moves another slider does not draw the masks again. One entry, under its own key.
    bool maskMaps(const ipr::Image &source, double pixelScale, const QByteArray &key, LocalValues::Maps &maps) const;
    void storeMaskMaps(const ipr::Image &source, double pixelScale, const QByteArray &key, const LocalValues::Maps &maps);
    void clear();
    int count() const;

private:
    struct Entry { QByteArray key; ipr::Image rgb; };

    mutable std::mutex m_mutex;
    ipr::Image m_source;   // shared: its pixels stay alive, so their address identifies them
    double m_pixelScale = 0.0;
    std::optional<Keys> m_previous;
    std::array<std::optional<Entry>, StageCount> m_entries;
    QByteArray m_maskKey;
    const void *m_maskSource = nullptr;
    double m_maskScale = 0.0;
    LocalValues::Maps m_maskMaps;
    std::uint64_t m_generation = 0;
};

#endif // IPR_CACHE_RENDERSTAGECACHE_H
