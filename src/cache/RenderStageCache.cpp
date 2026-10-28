#include "RenderStageCache.h"

RenderStageCache::Start RenderStageCache::begin(const cv::Mat &source, double pixelScale, const Keys &keys)
{
    std::lock_guard lock(m_mutex);
    if (source.data != m_source.data || source.size != m_source.size || pixelScale != m_pixelScale) {
        m_entries = {};
        m_previous.reset();
        m_source = source;
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

void RenderStageCache::store(const Start &start, int stage, const cv::Mat &rgb)
{
    if (stage < 0 || stage >= StageCount || !start.keep[size_t(stage)] || rgb.empty()) return;
    cv::Mat copy = rgb.clone();   // outside the lock: about 5 ms on the proxy
    std::lock_guard lock(m_mutex);
    if (start.generation != m_generation) return;
    m_entries[size_t(stage)] = Entry{start.keys[size_t(stage)], std::move(copy)};
}

cv::Mat RenderStageCache::peek(const cv::Mat &source, double pixelScale, int stage, const QByteArray &key) const
{
    if (stage < 0 || stage >= StageCount) return {};
    std::lock_guard lock(m_mutex);
    const auto &entry = m_entries[size_t(stage)];
    if (source.data != m_source.data || source.size != m_source.size || pixelScale != m_pixelScale ||
        !entry || entry->key != key)
        return {};
    return entry->rgb.clone();
}

void RenderStageCache::clear()
{
    std::lock_guard lock(m_mutex);
    m_entries = {};
    m_previous.reset();
    m_source.release();
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
