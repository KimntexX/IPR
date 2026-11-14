#include "RenderStageCache.h"

namespace {

// The first pixel and the size: the same source, without comparing its pixels.
bool sameSource(const ipr::Image &a, const ipr::Image &b)
{
    if (a.empty() || b.empty())
        return a.empty() && b.empty();
    return a.row<unsigned char>(0) == b.row<unsigned char>(0) && a.size() == b.size();
}

const void *firstPixel(const ipr::Image &image)
{
    return image.empty() ? nullptr : image.row<unsigned char>(0);
}

} // namespace

RenderStageCache::Start RenderStageCache::begin(const ipr::Image &source, double pixelScale, const Keys &keys)
{
    std::lock_guard lock(m_mutex);
    if (!sameSource(source, m_source) || pixelScale != m_pixelScale) {
        m_entries = {};
        m_camera.reset();
        m_previous.reset();
        m_source = source.share();
        m_pixelScale = pixelScale;
        ++m_generation;
    }
    Start start;
    start.keys = keys;
    start.generation = m_generation;
    for (int stage = 0; stage < StageCount; ++stage) {
        auto &entry = m_entries[size_t(stage)];
        if (entry && entry->key != keys[size_t(stage)]) entry.reset();
        if (entry) start.stage = stage;
    }
    if (start.stage >= 0) start.rgb = m_entries[size_t(start.stage)]->rgb.clone();

    // The stage being edited is the first whose key changed since the previous
    // render; the next renders of the same edit resume just before it.
    int edited = 0;
    if (m_previous)
        while (edited < StageCount && (*m_previous)[size_t(edited)] == keys[size_t(edited)]) ++edited;
    else
        edited = 1;   // first render of this source: keep the tones' output
    m_previous = keys;
    for (int stage = 0; stage < StageCount; ++stage) {
        const bool wanted = stage < edited && (stage == 0 || stage == edited - 1);
        if (!wanted) m_entries[size_t(stage)].reset();
        else if (!m_entries[size_t(stage)]) start.keep[size_t(stage)] = true;
    }
    return start;
}

void RenderStageCache::store(const Start &start, int stage, const ipr::Image &rgb)
{
    if (stage < 0 || stage >= StageCount || !start.keep[size_t(stage)] || rgb.empty()) return;
    ipr::Image copy = rgb.clone();   // outside the lock: about 5 ms on the proxy
    std::lock_guard lock(m_mutex);
    if (start.generation != m_generation) return;
    m_entries[size_t(stage)] = Entry{start.keys[size_t(stage)], std::move(copy)};
}

ipr::Image RenderStageCache::peek(const ipr::Image &source, double pixelScale, int stage, const QByteArray &key) const
{
    if (stage < 0 || stage >= StageCount) return {};
    std::lock_guard lock(m_mutex);
    const auto &entry = m_entries[size_t(stage)];
    if (!sameSource(source, m_source) || pixelScale != m_pixelScale || !entry || entry->key != key)
        return {};
    return entry->rgb.clone();
}

bool RenderStageCache::maskMaps(const ipr::Image &source, double pixelScale, const QByteArray &key, LocalValues::Maps &maps) const
{
    std::lock_guard lock(m_mutex);
    if (m_maskKey.isEmpty() || m_maskKey != key || m_maskSource != firstPixel(source) || m_maskScale != pixelScale)
        return false;
    maps = LocalValues::sharedCopy(m_maskMaps);
    return true;
}

void RenderStageCache::storeMaskMaps(const ipr::Image &source, double pixelScale, const QByteArray &key, const LocalValues::Maps &maps)
{
    std::lock_guard lock(m_mutex);
    m_maskKey = key;
    m_maskSource = firstPixel(source);
    m_maskScale = pixelScale;
    m_maskMaps = LocalValues::sharedCopy(maps);
}

ipr::Image RenderStageCache::cameraOutput(const ipr::Image &source, double pixelScale, const QByteArray &key) const
{
    std::lock_guard lock(m_mutex);
    if (!sameSource(source, m_source) || pixelScale != m_pixelScale || !m_camera || m_camera->key != key)
        return {};
    return m_camera->rgb.clone();
}

void RenderStageCache::storeCameraOutput(const ipr::Image &source, double pixelScale,
                                        const QByteArray &key, const ipr::Image &rgb)
{
    if (rgb.empty()) return;
    auto copy = rgb.clone();
    std::lock_guard lock(m_mutex);
    if (!sameSource(source, m_source) || pixelScale != m_pixelScale) return;
    m_camera = Entry{key, std::move(copy)};
}

void RenderStageCache::clear()
{
    {
        std::lock_guard lock(m_mutex);
        m_maskKey.clear();
        m_maskSource = nullptr;
        m_maskMaps = {};
    }
    std::lock_guard lock(m_mutex);
    m_entries = {};
    m_camera.reset();
    m_previous.reset();
    m_source = {};
    m_pixelScale = 0.0;
    ++m_generation;
}

int RenderStageCache::count() const
{
    std::lock_guard lock(m_mutex);
    int n = 0;
    for (const auto &entry : m_entries) n += entry.has_value();
    return n;
}
