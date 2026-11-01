#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

struct SaveTitle
{
    QString titleId;
    int saveCount = 0;
    qint64 totalBytes = 0;
    QDateTime lastModified;
};

// 存档管理：按 titleid 汇总、打包导出
class SaveManager : public QObject
{
    Q_OBJECT

public:
    explicit SaveManager(QObject *parent = nullptr);

    QList<SaveTitle> titles() const;

    // 导出到 zip（调用系统 Compress-Archive），zipPath 为空时自动生成到桌面
    bool exportTitle(const QString &titleId, const QString &zipPath, QString *errorOut = nullptr);
    bool exportAll(const QString &zipPath, QString *errorOut = nullptr);

    static void revealInExplorer(const QString &path);

private:
    QString m_saveRoot;
};
