#include "TaskRunner.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QMutex>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool waitFor(const std::function<bool()> &done, int milliseconds = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return done();
}

// Busy work that stops as soon as it is cancelled.
int countUntilCancelled(Tasks::Context &context, int steps)
{
    for (int step = 0; step < steps; ++step) {
        if (context.cancelled())
            return -1;
        context.progress(step + 1, steps);
        QThread::msleep(2);
    }
    return steps;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool passed = true;

    {
        // A result, with its progress reported through the future.
        Tasks::TaskRunner runner(2);
        std::atomic_int lastProgress{0};
        auto ticket = runner.submit<int>(QStringLiteral("render"), Tasks::Priority::Interactive,
                                         [](Tasks::Context &context) { return countUntilCancelled(context, 20); });
        QFutureWatcher<int> watcher;
        QObject::connect(&watcher, &QFutureWatcher<int>::progressValueChanged,
                         [&](int value) { lastProgress = value; });
        watcher.setFuture(ticket.future);
        passed &= check(waitFor([&] { return ticket.future.isFinished(); }), "a task finishes");
        QCoreApplication::processEvents();
        passed &= check(ticket.future.resultCount() == 1 && ticket.future.result() == 20, "its result is delivered");
        passed &= check(lastProgress == 20, "its progress reaches the end");
        passed &= check(ticket.isCurrent(), "the latest task of a channel is current");
    }

    {
        // Cancelling a channel stops its task cooperatively; nothing is published.
        Tasks::TaskRunner runner(2);
        auto ticket = runner.submit<int>(QStringLiteral("export"), Tasks::Priority::Foreground,
                                         [](Tasks::Context &context) { return countUntilCancelled(context, 5000); });
        QThread::msleep(20);
        runner.cancel(QStringLiteral("export"));
        passed &= check(waitFor([&] { return ticket.future.isFinished(); }, 2000), "a cancelled task ends quickly");
        passed &= check(ticket.future.isCanceled() && ticket.future.resultCount() == 0,
                        "a cancelled task publishes no result");
    }

    {
        // A newer task on the same channel supersedes the older one; other channels are untouched.
        Tasks::TaskRunner runner(3);
        auto older = runner.submit<int>(QStringLiteral("render"), Tasks::Priority::Interactive,
                                        [](Tasks::Context &context) { return countUntilCancelled(context, 5000); });
        auto other = runner.submit<int>(QStringLiteral("load"), Tasks::Priority::Foreground,
                                        [](Tasks::Context &context) { return countUntilCancelled(context, 30); });
        QThread::msleep(10);
        auto newer = runner.submit<int>(QStringLiteral("render"), Tasks::Priority::Interactive,
                                        [](Tasks::Context &) { return 7; });
        passed &= check(!older.isCurrent() && newer.isCurrent(), "only the newest task of a channel is current");
        passed &= check(waitFor([&] { return older.future.isFinished() && newer.future.isFinished()
                                             && other.future.isFinished(); }), "all tasks end");
        passed &= check(older.future.resultCount() == 0, "a superseded task publishes no result");
        passed &= check(newer.future.result() == 7, "the newest task publishes its result");
        passed &= check(other.isCurrent() && other.future.result() == 30, "another channel is not affected");
    }

    {
        // An exception becomes the future's exception, not a crash of the worker.
        Tasks::TaskRunner runner(1);
        auto ticket = runner.submit<int>(QStringLiteral("decode"), Tasks::Priority::Foreground,
                                         [](Tasks::Context &) -> int { throw std::runtime_error("decoder failed"); });
        passed &= check(waitFor([&] { return ticket.future.isFinished(); }), "a failing task ends");
        bool thrown = false;
        try {
            ticket.future.waitForFinished();
            (void)ticket.future.result();
        } catch (const std::runtime_error &) {
            thrown = true;
        }
        passed &= check(thrown, "the task's exception reaches the caller");
    }

    {
        // With one worker busy, a queued interactive task runs before queued background ones.
        Tasks::TaskRunner runner(1);
        QMutex mutex;
        std::vector<QString> order;
        std::atomic_bool release{false};
        auto blocker = runner.submit<int>(QStringLiteral("blocker"), Tasks::Priority::Foreground,
                                          [&](Tasks::Context &) { while (!release) QThread::msleep(1); return 0; });
        QThread::msleep(20);
        const auto record = [&](const QString &name) {
            return [&, name](Tasks::Context &) { QMutexLocker lock(&mutex); order.push_back(name); return 0; };
        };
        auto background = runner.submit<int>(QStringLiteral("prefetch"), Tasks::Priority::Background,
                                             record(QStringLiteral("background")));
        auto interactive = runner.submit<int>(QStringLiteral("render"), Tasks::Priority::Interactive,
                                              record(QStringLiteral("interactive")));
        release = true;
        passed &= check(waitFor([&] { return background.future.isFinished() && interactive.future.isFinished(); }),
                        "queued tasks run");
        passed &= check(order.size() == 2 && order.front() == QStringLiteral("interactive"),
                        "an interactive task overtakes queued background work");
        (void)blocker;
    }

    {
        // Reading a result: moved out, a default value when none was published, an exception rethrown.
        Tasks::TaskRunner runner(2);
        auto done = runner.submit<int>(QStringLiteral("done"), Tasks::Priority::Foreground,
                                       [](Tasks::Context &) { return 11; });
        passed &= check(Tasks::takeResult(done.future) == 11, "takeResult gives the result");
        auto cancelled = runner.submit<int>(QStringLiteral("cancelled"), Tasks::Priority::Foreground,
                                            [](Tasks::Context &context) { return countUntilCancelled(context, 5000); });
        runner.cancel(QStringLiteral("cancelled"));
        passed &= check(Tasks::takeResult(cancelled.future) == 0, "a cancelled task gives the default value");
        auto failed = runner.submit<int>(QStringLiteral("failed"), Tasks::Priority::Foreground,
                                         [](Tasks::Context &) -> int { throw std::runtime_error("failed"); });
        bool rethrown = false;
        try {
            Tasks::takeResult(failed.future);
        } catch (const std::runtime_error &) {
            rethrown = true;
        }
        passed &= check(rethrown, "takeResult rethrows the task's exception");
    }

    {
        // Shutting down waits for what ignores cancellation and finishes in time, once.
        Tasks::TaskRunner runner(1, 5000);
        std::atomic_bool ended{false};
        runner.submit<int>(QStringLiteral("export"), Tasks::Priority::Foreground, [&](Tasks::Context &) {
            QThread::msleep(100);
            ended = true;
            return 1;
        });
        QThread::msleep(20);
        passed &= check(runner.shutdown() && ended, "shutdown waits for a task that finishes in time");
        QElapsedTimer again;
        again.start();
        passed &= check(runner.shutdown() && again.elapsed() < 100, "a second shutdown returns at once");
    }

    {
        // A stuck task makes shutdown report it, within the bound.
        auto stuck = std::make_shared<std::atomic_bool>(true);
        {
            Tasks::TaskRunner runner(1, 100);
            runner.submit<int>(QStringLiteral("stuck"), Tasks::Priority::Foreground,
                               [stuck](Tasks::Context &) { while (*stuck) QThread::msleep(5); return 0; });
            QThread::msleep(20);
            QElapsedTimer timer;
            timer.start();
            passed &= check(!runner.shutdown() && timer.elapsed() < 2000, "shutdown reports a stuck task");
        }
        *stuck = false;
        QThread::msleep(50);
    }

    {
        // Closing never waits forever: tasks are cancelled, then the wait is bounded.
        QElapsedTimer timer;
        timer.start();
        auto stubborn = std::make_shared<std::atomic_bool>(true);
        QFuture<int> cooperative;
        {
            Tasks::TaskRunner runner(2, 200);
            cooperative = runner.submit<int>(QStringLiteral("render"), Tasks::Priority::Interactive,
                                             [](Tasks::Context &context) { return countUntilCancelled(context, 50000); })
                              .future;
            // A task that ignores cancellation must not hold the editor hostage at close.
            runner.submit<int>(QStringLiteral("stuck"), Tasks::Priority::Background,
                               [stubborn](Tasks::Context &) { while (*stubborn) QThread::msleep(5); return 0; });
            QThread::msleep(20);
        }
        passed &= check(timer.elapsed() < 2000, "closing the runner is bounded even with a stuck task");
        passed &= check(cooperative.isFinished() && cooperative.resultCount() == 0,
                        "a cooperative task is cancelled at close");
        *stubborn = false;   // let the stuck worker end before the process exits
        QThread::msleep(50);
    }

    std::cout << (passed ? "task runner: ok\n" : "task runner: FAILED\n");
    return passed ? 0 : 1;
}
