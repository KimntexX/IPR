#ifndef IPR_TASKS_TASKRUNNER_H
#define IPR_TASKS_TASKRUNNER_H

#include <QFuture>
#include <QHash>
#include <QMutex>
#include <QPointer>
#include <QPromise>
#include <QString>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentTask>

#include <atomic>
#include <exception>
#include <functional>
#include <memory>

// Common runner of the editor's long tasks (ADR-0021): priorities, progress,
// cooperative cancellation and a revision per channel, so that a result made
// obsolete by a newer request is never published. A channel ("render",
// "load", "export"...) holds one current task: submitting another supersedes
// it. Memory is reserved by the caller through ImageCacheController.
namespace Tasks {

// Higher runs first among queued tasks.
enum class Priority { Background = 0, Foreground = 1, Interactive = 2 };

// What a task sees of its run.
class Context
{
public:
    Context(std::function<bool()> cancelled, std::function<void(int, int)> progress)
        : m_cancelled(std::move(cancelled)), m_progress(std::move(progress)) {}
    // True once the task was cancelled or superseded: stop and return.
    bool cancelled() const { return m_cancelled(); }
    void progress(int done, int total) { m_progress(done, total); }

private:
    std::function<bool()> m_cancelled;
    std::function<void(int, int)> m_progress;
};

class TaskRunner;

template <typename T>
struct Ticket
{
    QFuture<T> future;
    quint64 revision = 0;
    QString channel;
    QPointer<TaskRunner> runner;
    // Whether this is still the newest task of its channel: publish only then.
    bool isCurrent() const;
};

class TaskRunner final : public QObject
{
public:
    // shutdownMs bounds the wait at destruction: a task that ignores
    // cancellation is left running rather than blocking the editor's close.
    explicit TaskRunner(int maxThreads, int shutdownMs = 10000, QObject *parent = nullptr);
    ~TaskRunner() override;

    template <typename T>
    Ticket<T> submit(const QString &channel, Priority priority, std::function<T(Context &)> work);

    void cancel(const QString &channel);
    void cancelAll();
    // Cancels every task and waits for them, at most shutdownMs in all: false when
    // one is still running. Done once, at destruction at the latest; what a task
    // left running uses must outlive this runner.
    bool shutdown();
    quint64 currentRevision(const QString &channel) const;

private:
    struct Slot
    {
        quint64 revision = 0;
        std::shared_ptr<std::atomic_bool> stop;
        QFuture<void> future;
    };
    // Supersedes the channel's current task and returns the new one's revision.
    quint64 open(const QString &channel, const std::shared_ptr<std::atomic_bool> &stop);
    void attach(const QString &channel, quint64 revision, const QFuture<void> &future);

    std::unique_ptr<QThreadPool> m_pool;
    int m_shutdownMs;
    bool m_shutDown = false;
    bool m_finished = true;
    mutable QMutex m_mutex;
    QHash<QString, Slot> m_slots;
    quint64 m_nextRevision = 0;
};

// The result of a finished task, moved out of its future; a default value when it
// published none (cancelled or superseded). Rethrows the task's exception.
template <typename T>
T takeResult(QFuture<T> future)
{
    future.waitForFinished();
    return future.resultCount() > 0 ? future.takeResult() : T{};
}

template <typename T>
bool Ticket<T>::isCurrent() const
{
    return runner && runner->currentRevision(channel) == revision;
}

template <typename T>
Ticket<T> TaskRunner::submit(const QString &channel, Priority priority, std::function<T(Context &)> work)
{
    auto stop = std::make_shared<std::atomic_bool>(false);
    const quint64 revision = open(channel, stop);
    QFuture<T> future = QtConcurrent::task([work = std::move(work), stop](QPromise<T> &promise) {
            Context context([&promise, stop] { return promise.isCanceled() || stop->load(); },
                            [&promise](int done, int total) {
                                promise.setProgressRange(0, total);
                                promise.setProgressValue(done);
                            });
            try {
                if (context.cancelled())
                    return;
                T result = work(context);
                // A cancelled or superseded run publishes nothing, even if it finished.
                if (!context.cancelled())
                    promise.addResult(std::move(result));
            } catch (...) {
                promise.setException(std::current_exception());
            }
        })
        .onThreadPool(*m_pool)
        .withPriority(int(priority))
        .spawn();
    attach(channel, revision, QFuture<void>(future));
    return {future, revision, channel, this};
}

} // namespace Tasks

#endif // IPR_TASKS_TASKRUNNER_H
