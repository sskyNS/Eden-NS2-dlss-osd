#include "SaveManager.h"

#include "AppConfig.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace {

qint64 dirSize(const QString &path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QDateTime dirLastModified(const QString &path)
{
    QDateTime latest;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QDateTime t = it.fileInfo().lastModified();
        if (!latest.isValid() || t > latest)
            latest = t;
    }
    return latest;
}

QString defaultZipPath(const QString &suffix)
{
    const QString desktop =
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return QStringLiteral("%1/Eden存档_%2_%3.zip").arg(desktop, suffix, stamp);
}

bool runCompressArchive(const QString &sourcePattern, const QString &zipPath, QString *errorOut)
{
    const QString script =
        QStringLiteral("Compress-Archive -Path '%1' -DestinationPath '%2' -Force")
            .arg(sourcePattern, zipPath);

    QProcess ps;
    ps.setProgram(QStringLiteral("powershell.exe"));
    ps.setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                     QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                     QStringLiteral("-Command"), script});
    ps.setProcessChannelMode(QProcess::MergedChannels);
    ps.start();
    if (!ps.waitForStarted(8000)) {
        if (errorOut)
            *errorOut = QStringLiteral("无法启动 PowerShell");
        return false;
    }
    ps.waitForFinished(120000);
    const QString out = QString::fromLocal8Bit(ps.readAll());
    if (ps.exitCode() != 0 || !QFileInfo::exists(zipPath)) {
        if (errorOut)
            *errorOut = out.trimmed().isEmpty() ? QStringLiteral("打包失败") : out.trimmed();
        return false;
    }
    return true;
}

} // namespace

SaveManager::SaveManager(QObject *parent)
    : QObject(parent)
{
}

QList<SaveTitle> SaveManager::titles() const
{
    QList<SaveTitle> result;
    const QString root = AppConfig::instance().saveDir();
    QDir dir(root);
    if (!dir.exists())
        return result;

    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                    QDir::Name);
    for (const QFileInfo &entry : entries) {
        SaveTitle title;
        title.titleId = entry.fileName();
        const QFileInfoList saves = QDir(entry.absoluteFilePath())
                                        .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        title.saveCount = saves.size();
        title.totalBytes = dirSize(entry.absoluteFilePath());
        title.lastModified = dirLastModified(entry.absoluteFilePath());
        result.append(title);
    }
    return result;
}

bool SaveManager::exportTitle(const QString &titleId, const QString &zipPath, QString *errorOut)
{
    const QString source = AppConfig::instance().saveDir() + "/" + titleId;
    if (!QDir(source).exists()) {
        if (errorOut)
            *errorOut = QStringLiteral("存档目录不存在：%1").arg(source);
        return false;
    }
    const QString target = zipPath.isEmpty() ? defaultZipPath(titleId) : zipPath;
    return runCompressArchive(source + "/*", target, errorOut);
}

bool SaveManager::exportAll(const QString &zipPath, QString *errorOut)
{
    const QString source = AppConfig::instance().saveDir();
    if (!QDir(source).exists()) {
        if (errorOut)
            *errorOut = QStringLiteral("存档根目录不存在：%1").arg(source);
        return false;
    }
    const QString target = zipPath.isEmpty() ? defaultZipPath(QStringLiteral("全部"))
                                             : zipPath;
    return runCompressArchive(source + "/*", target, errorOut);
}

void SaveManager::revealInExplorer(const QString &path)
{
    if (path.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
