#include "UpdateChecker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <functional>

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
    m_net = new QNetworkAccessManager(this);
}

void UpdateChecker::setRepo(const QString &repo)
{
    m_repo = repo.trimmed();
    if (m_repo.endsWith(QLatin1String(".git")))
        m_repo.chop(4);
    if (m_repo.startsWith(QLatin1String("https://github.com/")))
        m_repo = m_repo.mid(int(qstrlen("https://github.com/")));
    while (m_repo.endsWith('/'))
        m_repo.chop(1);
}

void UpdateChecker::get(const QUrl &url,
                        std::function<void(const QByteArray &, const QString &)> handler)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("EdenLauncher/2.0 (+https://github.com/)"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    ++m_pending;
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, handler] {
        reply->deleteLater();
        --m_pending;
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit failed(status == 0
                            ? QStringLiteral("网络请求失败：%1").arg(reply->errorString())
                            : QStringLiteral("请求失败（HTTP %1）：%2")
                                  .arg(status)
                                  .arg(reply->errorString()));
            return;
        }
        handler(reply->readAll(), reply->url().toString());
    });
}

void UpdateChecker::fetchRepoInfo()
{
    if (m_repo.isEmpty())
        return;
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1").arg(m_repo));
    get(url, [this](const QByteArray &data, const QString &) {
        const QJsonObject obj = QJsonDocument::fromJson(data).object();
        emit repoInfoReady(obj.value(QStringLiteral("default_branch")).toString(),
                           obj.value(QStringLiteral("pushed_at")).toString(),
                           obj.value(QStringLiteral("description")).toString());
    });
}

void UpdateChecker::fetchCommits(int count)
{
    if (m_repo.isEmpty())
        return;
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/commits?per_page=%2")
                       .arg(m_repo)
                       .arg(qBound(1, count, 100)));
    get(url, [this](const QByteArray &data, const QString &) {
        const QJsonArray arr = QJsonDocument::fromJson(data).array();
        if (arr.isEmpty()) {
            emit failed(QStringLiteral("上游没有返回任何提交"));
            return;
        }
        QList<CommitInfo> commits;
        commits.reserve(arr.size());
        for (const QJsonValue &value : arr) {
            const QJsonObject o = value.toObject();
            const QJsonObject commit = o.value(QStringLiteral("commit")).toObject();
            const QJsonObject author = commit.value(QStringLiteral("author")).toObject();
            const QString message = commit.value(QStringLiteral("message")).toString();

            CommitInfo info;
            info.sha = o.value(QStringLiteral("sha")).toString();
            info.shortSha = info.sha.left(8);
            info.htmlUrl = o.value(QStringLiteral("html_url")).toString();

            const int newline = message.indexOf('\n');
            if (newline < 0) {
                info.title = message.trimmed();
            } else {
                info.title = message.left(newline).trimmed();
                info.body = message.mid(newline + 1).trimmed();
            }
            info.author = author.value(QStringLiteral("name")).toString();
            info.authorEmail = author.value(QStringLiteral("email")).toString();
            info.date = author.value(QStringLiteral("date")).toString();
            commits.append(info);
        }
        emit commitsReady(commits);
    });
}

void UpdateChecker::fetchCommitDetail(const QString &sha)
{
    if (m_repo.isEmpty() || sha.isEmpty())
        return;
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/commits/%2").arg(m_repo, sha));
    get(url, [this](const QByteArray &data, const QString &) {
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        const QJsonObject commit = o.value(QStringLiteral("commit")).toObject();
        const QJsonObject author = commit.value(QStringLiteral("author")).toObject();

        CommitInfo info;
        info.sha = o.value(QStringLiteral("sha")).toString();
        info.shortSha = info.sha.left(8);
        info.htmlUrl = o.value(QStringLiteral("html_url")).toString();
        info.author = author.value(QStringLiteral("name")).toString();
        info.date = author.value(QStringLiteral("date")).toString();

        const QString message = commit.value(QStringLiteral("message")).toString();
        const int newline = message.indexOf('\n');
        info.title = newline < 0 ? message.trimmed() : message.left(newline).trimmed();
        info.body = newline < 0 ? QString() : message.mid(newline + 1).trimmed();

        const QJsonObject stats = o.value(QStringLiteral("stats")).toObject();
        info.additions = stats.value(QStringLiteral("additions")).toInt();
        info.deletions = stats.value(QStringLiteral("deletions")).toInt();
        info.changedFiles = stats.value(QStringLiteral("total")).toInt();

        const QJsonArray files = o.value(QStringLiteral("files")).toArray();
        for (const QJsonValue &value : files) {
            const QJsonObject f = value.toObject();
            info.files.append(QStringLiteral("%1  (+%2 / -%3)")
                                  .arg(f.value(QStringLiteral("filename")).toString())
                                  .arg(f.value(QStringLiteral("additions")).toInt())
                                  .arg(f.value(QStringLiteral("deletions")).toInt()));
        }
        info.detailLoaded = true;
        emit commitDetailReady(info);
    });
}

void UpdateChecker::fetchCommitDiff(const QString &sha)
{
    if (m_repo.isEmpty() || sha.isEmpty())
        return;
    const QUrl url(QStringLiteral("https://github.com/%1/commit/%2.diff").arg(m_repo, sha));
    get(url, [this, sha](const QByteArray &data, const QString &) {
        emit diffReady(sha, QString::fromUtf8(data));
    });
}
