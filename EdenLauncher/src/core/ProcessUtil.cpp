#include "ProcessUtil.h"

#include <QDateTime>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

namespace ProcessUtil {

#ifdef Q_OS_WIN

QList<quint32> pidsByName(const QString &baseName)
{
    QList<quint32> result;
    const QString wanted = baseName.toLower();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            QString name = QString::fromWCharArray(pe.szExeFile);
            if (name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
                name.chop(4);
            if (name.toLower() == wanted)
                result.append(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return result;
}

bool isRunning(const QString &baseName)
{
    return !pidsByName(baseName).isEmpty();
}

bool killByName(const QString &baseName)
{
    const QList<quint32> pids = pidsByName(baseName);
    for (quint32 pid : pids) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!h)
            continue;
        TerminateProcess(h, 0);
        WaitForSingleObject(h, 3000);
        CloseHandle(h);
    }
    return !isRunning(baseName);
}

qint64 processStartTime(quint32 pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return -1;
    FILETIME creation{}, exitT{}, kernel{}, user{};
    qint64 result = -1;
    if (GetProcessTimes(h, &creation, &exitT, &kernel, &user)) {
        ULARGE_INTEGER ul;
        ul.LowPart = creation.dwLowDateTime;
        ul.HighPart = creation.dwHighDateTime;
        // FILETIME 100ns 起点为 1601-01-01
        const qint64 ms = static_cast<qint64>(ul.QuadPart / 10000LL);
        const qint64 epochDiff = 11644473600000LL;
        result = ms - epochDiff;
    }
    CloseHandle(h);
    return result;
}

QString processImagePath(quint32 pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return {};
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = MAX_PATH * 2;
    QString path;
    if (QueryFullProcessImageNameW(h, 0, buf, &size))
        path = QString::fromWCharArray(buf, static_cast<int>(size));
    CloseHandle(h);
    return path;
}

QString processName(quint32 pid)
{
    const QString path = processImagePath(pid);
    if (path.isEmpty())
        return {};
    QString base = QFileInfo(path).completeBaseName();
    return base;
}

#else

QList<quint32> pidsByName(const QString &) { return {}; }
bool isRunning(const QString &) { return false; }
bool killByName(const QString &) { return false; }
qint64 processStartTime(quint32) { return -1; }
QString processImagePath(quint32) { return {}; }
QString processName(quint32) { return {}; }

#endif

} // namespace ProcessUtil
