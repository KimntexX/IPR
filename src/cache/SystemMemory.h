#ifndef IPR_CACHE_SYSTEMMEMORY_H
#define IPR_CACHE_SYSTEMMEMORY_H

#include <QString>
#include <QtGlobal>

namespace SystemMemory {
struct Snapshot {
    quint64 totalBytes = 0;
    quint64 availableBytes = 0;
    quint64 availableCommitBytes = 0;
    bool valid = false;
    bool commitKnown = false;
};
Snapshot read();
// Admission estimate, not a hard upper bound on third-party decoder allocations.
quint64 estimateDecodePeak(const QString &fileName);
quint64 estimateProcessingPeak(quint64 pixels);
}

#endif // IPR_CACHE_SYSTEMMEMORY_H
