#include "SystemMemory.h"
#include "RawDecoder.h"
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QDir>
#include <algorithm>
#include <limits>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace SystemMemory {
Snapshot read()
{
    Snapshot result;
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        result = {status.ullTotalPhys, status.ullAvailPhys, status.ullAvailPageFile, true, true};
    }
#elif defined(Q_OS_LINUX)
    QFile info("/proc/meminfo");
    if (info.open(QIODevice::ReadOnly)) {
        bool hasAvailable = false;
        const auto lines = info.readAll().split('\n');
        for (const auto &line : lines) {
            const auto fields = line.simplified().split(' ');
            if (fields.size() < 2) continue;
            if (fields[0] == "MemTotal:") result.totalBytes = fields[1].toULongLong() * 1024;
            if (fields[0] == "MemAvailable:") { result.availableBytes = fields[1].toULongLong() * 1024; hasAvailable = true; }
        }
        result.valid = hasAvailable && result.totalBytes && result.availableBytes <= result.totalBytes;
        // Respect cgroup-v2 limits, including tighter ancestors in containers.
        QFile membership("/proc/self/cgroup");
        if (membership.open(QIODevice::ReadOnly)) {
            for (const auto &line : membership.readAll().split('\n')) {
                if (!line.startsWith("0::")) continue;
                QString path = QDir::cleanPath("/sys/fs/cgroup/" + QString::fromUtf8(line.mid(3)));
                while (path == "/sys/fs/cgroup" || path.startsWith("/sys/fs/cgroup/")) {
                    QFile limitFile(path + "/memory.max"), currentFile(path + "/memory.current");
                    if (limitFile.open(QIODevice::ReadOnly) && currentFile.open(QIODevice::ReadOnly)) {
                        bool ok = false;
                        const auto limit = limitFile.readAll().trimmed().toULongLong(&ok);
                        const auto current = currentFile.readAll().trimmed().toULongLong();
                        if (ok && limit) {
                            result.totalBytes = std::min(result.totalBytes, limit);
                            result.availableBytes = std::min(result.availableBytes, limit > current ? limit - current : 0);
                        }
                    }
                    if (path == "/sys/fs/cgroup") break;
                    path = QFileInfo(path).absolutePath();
                }
            }
        }
    }
#elif defined(Q_OS_MACOS)
    uint64_t total = 0;
    size_t length = sizeof(total);
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_size_t page = 0;
    const mach_port_t host = mach_host_self();
    if (!sysctlbyname("hw.memsize", &total, &length, nullptr, 0) &&
        host_page_size(host, &page) == KERN_SUCCESS &&
        host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) == KERN_SUCCESS) {
        result = {total, (quint64(vm.free_count) + vm.inactive_count) * page, 0, true, false};
    }
    mach_port_deallocate(mach_task_self(), host);
#endif
    result.availableBytes = std::min(result.availableBytes, result.totalBytes);
    return result;
}

quint64 estimateProcessingPeak(quint64 pixels)
{
    // Full float master, immutable camera RGB, decoder/demosaic intermediates,
    // render copies and proxy headroom. 32.3 MP CR3 peaks measured at 2.19-2.21
    // GiB of process memory; 80 B/px keeps ~15% above them. Not an RSS bound.
    constexpr quint64 margin = 128ULL * 1024 * 1024;
    constexpr quint64 bytesPerPixel = 80;
    return pixels > (std::numeric_limits<quint64>::max() - margin) / bytesPerPixel
        ? std::numeric_limits<quint64>::max() : pixels * bytesPerPixel + margin;
}

quint64 estimateDecodePeak(const QString &fileName)
{
    QSize size = RawDecoder::sourceDimensions(fileName);
    if (size.isEmpty()) size = QImageReader(fileName).size();
    if (!size.isEmpty()) return estimateProcessingPeak(quint64(size.width()) * size.height());
    // Future format handlers may not expose dimensions yet. Admit cautiously.
    const quint64 bytes = quint64(std::max<qint64>(0, QFileInfo(fileName).size()));
    const quint64 scaled = bytes > std::numeric_limits<quint64>::max() / 96
        ? std::numeric_limits<quint64>::max() : bytes * 96;
    return std::max(3ULL * 1024 * 1024 * 1024, scaled);
}
}
