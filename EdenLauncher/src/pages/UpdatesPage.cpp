#include "UpdatesPage.h"

#include "AppConfig.h"
#include "Theme.h"
#include "core/BuildService.h"
#include "widgets/Ui.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSplitter>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString prettyTime(const QString &iso)
{
    const QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid())
        return iso;
    return dt.toLocalTime().toString(QStringLiteral("MM-dd HH:mm"));
}

} // namespace

UpdatesPage::UpdatesPage(QWidget *parent)
    : QWidget(parent)
{
    m_checker = new UpdateChecker(this);
    m_build = new BuildService(this);

    buildUi();

    connect(m_checker, &UpdateChecker::repoInfoReady, this, &UpdatesPage::onRepoInfo);
    connect(m_checker, &UpdateChecker::commitsReady, this, &UpdatesPage::onCommits);
    connect(m_checker, &UpdateChecker::commitDetailReady, this, [this](const CommitInfo &info) {
        if (info.sha != m_selectedSha)
            return;
        QString text;
        text += info.title;
        if (!info.body.trimmed().isEmpty())
            text += QStringLiteral("\n\n") + info.body.trimmed();
        text += QStringLiteral("\n\n") + tr("变更统计：新增 %1 行，删除 %2 行，共 %3 个文件")
                                          .arg(info.additions)
                                          .arg(info.deletions)
                                          .arg(info.changedFiles);
        if (!info.files.isEmpty()) {
            text += QStringLiteral("\n\n") + tr("涉及文件：");
            const int limit = qMin(info.files.size(), 40);
            for (int i = 0; i < limit; ++i)
                text += QStringLiteral("\n  · ") + info.files.at(i);
            if (info.files.size() > limit)
                text += QStringLiteral("\n  … ") + tr("另有 %1 个文件").arg(info.files.size() - limit);
        }
        m_commitBody->setPlainText(text);
    });
    connect(m_checker, &UpdateChecker::diffReady, this, [this](const QString &sha, const QString &diff) {
        if (sha != m_selectedSha)
            return;
        if (diff.trimmed().isEmpty()) {
            m_diffView->setPlainText(tr("（该提交没有文本差异）"));
            return;
        }
        const int limit = 400000;
        m_diffView->setPlainText(diff.size() > limit
                                     ? diff.left(limit) + QStringLiteral("\n\n… 差异过大，已截断")
                                     : diff);
    });
    connect(m_checker, &UpdateChecker::failed, this, [this](const QString &message) {
        m_statusBadge->setText(tr("检查失败"));
        Ui::setStyleProperty(m_statusBadge, "badge", QStringLiteral("true"));
        m_checkBtn->setEnabled(true);
        m_diffView->appendError(message);
    });

    connect(m_build, &BuildService::log, this, [this](const QString &line) {
        m_buildLog->appendLine(line);
    });
    connect(m_build, &BuildService::stageChanged, this,
            [this](BuildService::Stage stage, const QString &text) {
                m_stageLabel->setText(text);
                if (stage == BuildService::Stage::Failed)
                    Ui::setStyleProperty(m_statusBadge, "badge", QStringLiteral("warn"));
            });
    connect(m_build, &BuildService::progressChanged, this, [this](int percent) {
        m_progress->setValue(percent);
    });
    connect(m_build, &BuildService::finished, this, &UpdatesPage::onBuildFinished);

    m_repoLabel->setText(tr("GitHub · %1").arg(AppConfig::instance().updateRepo()));
}

UpdatesPage::~UpdatesPage() = default;

void UpdatesPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_autoChecked) {
        m_autoChecked = true;
        checkUpdates();
    }
}

void UpdatesPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 22, 26, 22);
    root->setSpacing(16);

    // ── 顶栏 ──
    auto *header = new QHBoxLayout;
    header->setSpacing(12);
    auto *titleCol = new QVBoxLayout;
    titleCol->setSpacing(2);
    titleCol->addWidget(Ui::makeH1(tr("更新与构建")));
    titleCol->addWidget(Ui::makeMuted(tr("检查上游新提交，查看更新日志，在线编译最新版本")));
    header->addLayout(titleCol);
    header->addStretch(1);
    m_statusBadge = Ui::makeBadge(tr("尚未检查"));
    header->addWidget(m_statusBadge);
    m_checkBtn = Ui::makeButton(tr("检查更新"), QStringLiteral("update"), true);
    header->addWidget(m_checkBtn);
    root->addLayout(header);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *canvas = new QWidget;
    auto *content = new QVBoxLayout(canvas);
    content->setContentsMargins(0, 0, 8, 0);
    content->setSpacing(16);

    // ── 提交列表 + 详情 ──
    auto *commitsCard = new Ui::Card(tr("上游提交"));
    commitsCard->setSubtitle(tr("点击左侧提交查看完整提交说明与代码差异"));
    m_repoLabel = Ui::makeBadge(tr("GitHub"));
    commitsCard->addHeaderWidget(m_repoLabel);

    m_repoInfo = Ui::makeMuted(tr("尚未连接"));
    commitsCard->body()->addWidget(m_repoInfo);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);

    m_commitList = new QListWidget;
    m_commitList->setMinimumWidth(280);
    m_commitList->setMaximumWidth(400);
    m_commitList->setWordWrap(true);
    m_commitList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_commitList->setUniformItemSizes(false);
    splitter->addWidget(m_commitList);

    auto *detailPanel = new QWidget;
    auto *detailLay = new QVBoxLayout(detailPanel);
    detailLay->setContentsMargins(12, 0, 0, 0);
    detailLay->setSpacing(8);

    auto *detailHead = new QHBoxLayout;
    auto *detailTitleCol = new QVBoxLayout;
    detailTitleCol->setSpacing(2);
    m_commitTitle = Ui::makeTitle(tr("选择一条提交"));
    m_commitTitle->setWordWrap(true);
    m_commitMeta = Ui::makeMuted(QString());
    m_commitMeta->setWordWrap(true);
    detailTitleCol->addWidget(m_commitTitle);
    detailTitleCol->addWidget(m_commitMeta);
    detailHead->addLayout(detailTitleCol, 1);
    m_openBtn = Ui::makeGhostButton(tr("浏览器打开"), QStringLiteral("folder"));
    m_openBtn->setEnabled(false);
    detailHead->addWidget(m_openBtn);
    detailLay->addLayout(detailHead);

    m_commitBody = new QTextBrowser;
    m_commitBody->setMinimumHeight(70);
    m_commitBody->setMaximumHeight(170);
    detailLay->addWidget(m_commitBody);

    m_diffView = new Ui::LogView;
    m_diffView->setMinimumHeight(110);
    m_diffView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_diffView->setPlaceholderText(tr("代码差异会显示在这里"));
    detailLay->addWidget(m_diffView, 1);

    splitter->addWidget(detailPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({340, 700});
    commitsCard->body()->addWidget(splitter, 1);
    content->addWidget(commitsCard, 1);

    // ── 构建 ──
    auto *buildCard = new Ui::Card(tr("在线构建"));
    buildCard->setSubtitle(tr("拉取最新源码 → 应用 NS2 补丁 → CMake/Ninja 编译 → 自动部署二进制"));
    m_stageLabel = Ui::makeMuted(tr("空闲"));
    buildCard->addHeaderWidget(m_stageLabel);

    auto *buildRow = new QHBoxLayout;
    buildRow->setSpacing(10);
    m_autoLaunch = new QCheckBox(tr("完成后自动启动游戏"));
    m_autoLaunch->setChecked(true);
    buildRow->addWidget(m_autoLaunch);
    buildRow->addStretch(1);
    m_cancelBtn = Ui::makeButton(tr("取消"), QString(), false, true);
    m_cancelBtn->setEnabled(false);
    buildRow->addWidget(m_cancelBtn);
    m_buildBtn = Ui::makeButton(tr("构建并部署"), QStringLiteral("package"), true);
    buildRow->addWidget(m_buildBtn);
    buildCard->body()->addLayout(buildRow);

    m_buildInfo = Ui::makeMuted(QString());
    buildCard->body()->addWidget(m_buildInfo);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(8);
    buildCard->body()->addWidget(m_progress);

    m_buildLog = new Ui::LogView;
    m_buildLog->setMinimumHeight(90);
    m_buildLog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_buildLog->setPlaceholderText(tr("编译日志会实时显示在这里"));
    buildCard->body()->addWidget(m_buildLog, 1);
    content->addWidget(buildCard, 1);

    scroll->setWidget(canvas);
    root->addWidget(scroll, 1);

    connect(m_checkBtn, &QPushButton::clicked, this, &UpdatesPage::checkUpdates);
    connect(m_commitList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current, QListWidgetItem *) { onCommitSelected(current); });
    connect(m_openBtn, &QPushButton::clicked, this, &UpdatesPage::openInBrowser);
    connect(m_buildBtn, &QPushButton::clicked, this, &UpdatesPage::startBuild);
    connect(m_cancelBtn, &QPushButton::clicked, this, &UpdatesPage::cancelBuild);

    const AppConfig &cfg = AppConfig::instance();
    m_buildInfo->setText(tr("源码目录：%1\n构建输出：%2").arg(cfg.buildSourceDir(), cfg.mingwRoot()));
}

void UpdatesPage::checkUpdates()
{
    AppConfig &cfg = AppConfig::instance();
    m_checker->setRepo(cfg.updateRepo());
    m_repoLabel->setText(tr("GitHub · %1").arg(m_checker->repo()));
    m_statusBadge->setText(tr("检查中…"));
    Ui::setStyleProperty(m_statusBadge, "badge", QStringLiteral("true"));
    m_checkBtn->setEnabled(false);
    m_newCommits = false;
    m_checker->fetchRepoInfo();
    m_checker->fetchCommits(30);
}

void UpdatesPage::onRepoInfo(const QString &defaultBranch, const QString &pushedAt,
                             const QString &description)
{
    QString text = tr("默认分支 %1 · 最近推送 %2").arg(defaultBranch, prettyTime(pushedAt));
    if (!description.trimmed().isEmpty())
        text += QStringLiteral(" · ") + description.trimmed();
    m_repoInfo->setText(text);
}

void UpdatesPage::onCommits(const QList<CommitInfo> &commits)
{
    m_checkBtn->setEnabled(true);
    m_commitList->clear();

    AppConfig &cfg = AppConfig::instance();
    const QString lastSeen = cfg.lastSeenCommit();
    const bool hasSeen = !lastSeen.isEmpty();

    int newCount = 0;
    bool reachedSeen = false;
    for (int i = 0; i < commits.size(); ++i) {
        const CommitInfo &c = commits.at(i);
        bool isNew = hasSeen ? (!reachedSeen && c.sha != lastSeen) : (i == 0);
        if (c.sha == lastSeen)
            isNew = false;
        if (isNew)
            ++newCount;
        if (c.sha == lastSeen)
            reachedSeen = true;

        QString label = QStringLiteral("%1  %2\n%3 · %4")
                            .arg(c.shortSha.isEmpty() ? c.sha.left(7) : c.shortSha, c.title,
                                 c.author, prettyTime(c.date));
        if (isNew)
            label = tr("【新】") + label;

        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, c.sha);
        item->setData(Qt::UserRole + 1, c.title);
        item->setData(Qt::UserRole + 2, c.author);
        item->setData(Qt::UserRole + 3, prettyTime(c.date));
        if (isNew) {
            item->setForeground(Theme::warning());
        }
        m_commitList->addItem(item);
    }

    if (commits.isEmpty()) {
        m_statusBadge->setText(tr("没有拿到提交"));
        return;
    }

    m_newCommits = newCount > 0;
    if (m_newCommits) {
        m_statusBadge->setText(tr("发现 %1 个新提交").arg(newCount));
        Ui::setStyleProperty(m_statusBadge, "badge", QStringLiteral("warn"));
    } else {
        m_statusBadge->setText(tr("已是最新"));
        Ui::setStyleProperty(m_statusBadge, "badge", QStringLiteral("ok"));
    }

    cfg.setLastSeenCommit(commits.first().sha);
    m_commitList->setCurrentRow(0);
}

void UpdatesPage::onCommitSelected(QListWidgetItem *current)
{
    if (!current)
        return;
    m_selectedSha = current->data(Qt::UserRole).toString();
    m_commitTitle->setText(current->data(Qt::UserRole + 1).toString());
    m_commitMeta->setText(tr("%1 · %2 · %3")
                              .arg(m_selectedSha.left(7),
                                   current->data(Qt::UserRole + 2).toString(),
                                   current->data(Qt::UserRole + 3).toString()));
    m_commitBody->setPlainText(tr("正在获取提交详情…"));
    m_diffView->setPlainText(tr("正在获取代码差异…"));
    m_openBtn->setEnabled(true);

    m_checker->fetchCommitDetail(m_selectedSha);
    m_checker->fetchCommitDiff(m_selectedSha);
}

void UpdatesPage::openInBrowser()
{
    if (m_selectedSha.isEmpty())
        return;
    const QString url = QStringLiteral("https://github.com/%1/commit/%2")
                            .arg(m_checker->repo(), m_selectedSha);
    QDesktopServices::openUrl(QUrl(url));
}

void UpdatesPage::startBuild()
{
    if (m_build->isBusy())
        return;

    m_buildLog->clear();
    m_progress->setValue(0);
    m_startAfterBuild = m_autoLaunch->isChecked();
    setBuildRunning(true);

    const AppConfig &cfg = AppConfig::instance();
    m_buildInfo->setText(tr("源码目录：%1\n构建输出：%2").arg(cfg.buildSourceDir(), cfg.mingwRoot()));
    m_buildLog->appendLine(tr("开始构建上游最新版本…"));
    m_build->start();
}

void UpdatesPage::cancelBuild()
{
    m_startAfterBuild = false;
    m_build->cancel();
}

void UpdatesPage::setBuildRunning(bool running)
{
    m_buildBtn->setEnabled(!running);
    m_cancelBtn->setEnabled(running);
    m_checkBtn->setEnabled(!running);
}

void UpdatesPage::onBuildFinished(bool ok, const QString &message)
{
    setBuildRunning(false);
    if (ok) {
        m_progress->setValue(100);
        m_stageLabel->setText(tr("构建完成"));
        m_buildLog->appendOk(message);
        if (m_startAfterBuild) {
            m_startAfterBuild = false;
            emit requestLaunchGame();
        }
    } else {
        m_stageLabel->setText(tr("构建失败"));
        m_buildLog->appendError(message);
    }
}
