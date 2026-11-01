#pragma once

#include <QList>
#include <QString>

namespace ProcessUtil {

// 按可执行文件名（不含 .exe，大小写不敏感）查找进程 PID
QList<quint32> pidsByName(const QString &baseName);

bool isRunning(const QString &baseName);

// 结束所有同名进程，返回是否全部结束
bool killByName(const QString &baseName);

// 进程启动时间（毫秒时间戳），失败返回 -1
qint64 processStartTime(quint32 pid);

// 进程可执行文件完整路径
QString processImagePath(quint32 pid);

// 进程名（不含扩展名）
QString processName(quint32 pid);

} // namespace ProcessUtil
