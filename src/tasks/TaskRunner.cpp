#include "TaskRunner.h"

#include <QDebug>
#include <QMutexLocker>

namespace Tasks {

TaskRunner::TaskRunner(int maxThreads, int shutdownMs, QObject *parent)
    : QObject(parent), m_pool(std::make_unique<QThreadPool>()), m_shutdownMs(shutdownMs)
{
    m_pool->setMaxThreadCount(std::max(1, maxThreads));
}

TaskRunner::~TaskRunner()
{
    cancelAll();
    if (!m_pool->waitForDone(m_shutdownMs)) {
        // A task ignored its cancellation: leaving its pool behind keeps the
        // close bounded; the task holds no reference to this runner.
        qCritical() << "TaskRunner: a task did not stop within" << m_shutdownMs << "ms; left running at close";
        (void)m_pool.release();
    }
}

quint64 TaskRunner::open(const QString &channel, const std::shared_ptr<std::atomic_bool> &stop)
{
    QMutexLocker lock(&m_mutex);
    Slot &slot = m_slots[channel];
    if (slot.stop)
        slot.stop->store(true);
    slot.future.cancel();
    slot.revision = ++m_nextRevision;
    slot.stop = stop;
    slot.future = {};
    return slot.revision;
}

void TaskRunner::attach(const QString &channel, quint64 revision, const QFuture<void> &future)
{
    QMutexLocker lock(&m_mutex);
    auto slot = m_slots.find(channel);
    // Superseded between open() and attach(): cancel the future it no longer owns.
    if (slot == m_slots.end() || slot->revision != revision) {
        QFuture<void>(future).cancel();
        return;
    }
    slot->future = future;
}

void TaskRunner::cancel(const QString &channel)
{
    QMutexLocker lock(&m_mutex);
    auto slot = m_slots.find(channel);
    if (slot == m_slots.end())
        return;
    if (slot->stop)
        slot->stop->store(true);
    slot->future.cancel();
}

void TaskRunner::cancelAll()
{
    QMutexLocker lock(&m_mutex);
    for (Slot &slot : m_slots) {
        if (slot.stop)
            slot.stop->store(true);
        slot.future.cancel();
    }
}

quint64 TaskRunner::currentRevision(const QString &channel) const
{
    QMutexLocker lock(&m_mutex);
    const auto slot = m_slots.constFind(channel);
    return slot == m_slots.cend() ? 0 : slot->revision;
}

} // namespace Tasks
