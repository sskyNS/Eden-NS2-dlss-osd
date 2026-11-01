#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

struct CommitInfo
{
    QString sha;
    QString shortSha;
    QString title;      // message 第一行
    QString body;       // message 余下部分
    QString author;
    QString authorEmail;
    QString date;       // ISO8601
    QString htmlUrl;
    int additions = 0;
    int deletions = 0;
    int changedFiles = 0;
    QStringList files;
    bool detailLoaded = false;

    bool isValid() const { return !sha.isEmpty(); }
};

// 上游更新检查：GitHub REST API
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    void setRepo(const QString &repo);   // "owner/name"
    QString repo() const { return m_repo; }

    void fetchRepoInfo();
    void fetchCommits(int count = 20);
    void fetchCommitDetail(const QString &sha);
    void fetchCommitDiff(const QString &sha);

    bool isBusy() const { return m_pending > 0; }

signals:
    void repoInfoReady(const QString &defaultBranch, const QString &pushedAt, const QString &description);
    void commitsReady(const QList<CommitInfo> &commits);
    void commitDetailReady(const CommitInfo &info);
    void diffReady(const QString &sha, const QString &diff);
    void failed(const QString &message);

private:
    void get(const QUrl &url, std::function<void(const QByteArray &, const QString &)> handler);

    QNetworkAccessManager *m_net = nullptr;
    QString m_repo;
    int m_pending = 0;
};
