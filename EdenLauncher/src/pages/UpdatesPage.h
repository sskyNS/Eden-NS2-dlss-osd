#pragma once

#include <QWidget>

#include "core/UpdateChecker.h"

class BuildService;
class QCheckBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QShowEvent;
class QTextBrowser;

namespace Ui { class LogView; }

// 更新与构建：检查上游新提交、查看提交详情与 diff、在线编译并部署
class UpdatesPage : public QWidget
{
    Q_OBJECT

public:
    explicit UpdatesPage(QWidget *parent = nullptr);
    ~UpdatesPage() override;

signals:
    void requestLaunchGame();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void checkUpdates();
    void onRepoInfo(const QString &defaultBranch, const QString &pushedAt,
                    const QString &description);
    void onCommits(const QList<CommitInfo> &commits);
    void onCommitSelected(QListWidgetItem *current);
    void openInBrowser();

    void startBuild();
    void cancelBuild();
    void onBuildFinished(bool ok, const QString &message);
    void setBuildRunning(bool running);

    UpdateChecker *m_checker = nullptr;
    BuildService *m_build = nullptr;

    QLabel *m_statusBadge = nullptr;
    QLabel *m_repoLabel = nullptr;
    QLabel *m_repoInfo = nullptr;
    QListWidget *m_commitList = nullptr;

    QLabel *m_commitTitle = nullptr;
    QLabel *m_commitMeta = nullptr;
    QTextBrowser *m_commitBody = nullptr;
    Ui::LogView *m_diffView = nullptr;
    QPushButton *m_openBtn = nullptr;

    QProgressBar *m_progress = nullptr;
    QLabel *m_stageLabel = nullptr;
    QLabel *m_buildInfo = nullptr;
    Ui::LogView *m_buildLog = nullptr;
    QPushButton *m_buildBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QCheckBox *m_autoLaunch = nullptr;
    QPushButton *m_checkBtn = nullptr;

    QString m_selectedSha;
    bool m_startAfterBuild = false;
    bool m_newCommits = false;
    bool m_autoChecked = false;
};
