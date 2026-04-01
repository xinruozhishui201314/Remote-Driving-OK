#include "memorymanager.h"
#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#ifdef Q_OS_LINUX
#include <sys/mman.h>
#include <unistd.h>
#endif

MemoryManager::MemoryManager(QObject* parent)
    : QObject(parent)
{}

bool MemoryManager::lockMemory()
{
#ifdef Q_OS_LINUX
    if (::mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
        m_locked = true;
        qInfo() << "[Client][MemoryManager] mlockall() succeeded - memory locked";
        return true;
    } else {
        qWarning() << "[Client][MemoryManager] mlockall() failed (errno=" << errno
                   << ") - need CAP_IPC_LOCK or ulimit -l unlimited";
        return false;
    }
#else
    qInfo() << "[Client][MemoryManager] memory locking only supported on Linux";
    return false;
#endif
}

void MemoryManager::prefaultStack(std::size_t bytes)
{
    // Touch each page to prefault
    std::vector<char> stack(bytes, 0);
    volatile char* ptr = stack.data();
    for (std::size_t i = 0; i < bytes; i += 4096) {
        ptr[i] = 0;
    }
    qInfo() << "[Client][MemoryManager] prefaulted" << bytes / 1024 << "KB of stack";
}

MemoryManager::MemoryStats MemoryManager::queryStats() const
{
    MemoryStats stats;
    stats.memLocked = m_locked;

#ifdef Q_OS_LINUX
    QFile f("/proc/self/status");
    if (f.open(QIODevice::ReadOnly)) {
        QTextStream stream(&f);
        while (!stream.atEnd()) {
            const QString line = stream.readLine();
            if (line.startsWith("VmSize:"))
                stats.virtualKb  = line.split(QRegularExpression("\\s+")).value(1).toULong();
            else if (line.startsWith("VmRSS:"))
                stats.residentKb = line.split(QRegularExpression("\\s+")).value(1).toULong();
            else if (line.startsWith("VmLck:") && line.split(QRegularExpression("\\s+")).value(1).toULong() > 0)
                stats.memLocked  = true;
        }
    }
#endif
    return stats;
}
