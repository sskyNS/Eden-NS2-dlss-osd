#include "MainWindow.h"

#include "IconFactory.h"
#include "Theme.h"
#include "core/AudioMixer.h"
#include "core/EdenClient.h"
#include "core/LosslessScaling.h"
#include "core/SaveManager.h"
#include "pages/DashboardPage.h"
#include "pages/GameLibraryPage.h"
#include "pages/SettingsPage.h"
#include "pages/ToolsPage.h"
#include "pages/UpdatesPage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Eden Launcher"));
    setMinimumSize(1000, 680);
    resize(1240, 820);

    m_eden = new EdenClient(this);
    m_ls = new LosslessScaling(this);
    m_audio = new AudioMixer(this);
    m_saves = new SaveManager(this);

    auto *central = new QWidget;
    central->setObjectName(QStringLiteral("contentRoot"));
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildSidebar());

    auto *rightCol = new QVBoxLayout;
    rightCol->setContentsMargins(0, 0, 0, 0);
    rightCol->setSpacing(0);

    m_stack = new QStackedWidget;
    m_dashboard = new DashboardPage(m_eden, m_ls);
    m_library = new GameLibraryPage;
    m_tools = new ToolsPage(m_audio, m_saves, m_eden);
    m_updates = new UpdatesPage;
    m_settings = new SettingsPage;
    m_stack->addWidget(m_dashboard);   // 0 控制台
    m_stack->addWidget(m_library);     // 1 游戏库
    m_stack->addWidget(m_tools);       // 2 工具箱
    m_stack->addWidget(m_updates);     // 3 更新与构建
    m_stack->addWidget(m_settings);    // 4 设置

    rightCol->addWidget(m_stack, 1);
    rightCol->addWidget(buildFooter());
    root->addLayout(rightCol, 1);
    setCentralWidget(central);

    // 控制台日志回流到底部状态条
    connect(m_dashboard, &DashboardPage::logLine, this, [this](const QString &line) {
        if (m_statusLabel)
            m_statusLabel->setText(line);
    });

    // 工具箱里需要回到控制台继续操作
    connect(m_tools, &ToolsPage::requestDashboard, this, [this] { switchPage(0); });

    // 游戏库里选中游戏后直接回控制台启动
    connect(m_library, &GameLibraryPage::requestLaunchGame, this, [this] {
        switchPage(0);
        m_dashboard->launchAll();
    });

    // 构建完成后自动切回控制台并启动游戏
    connect(m_updates, &UpdatesPage::requestLaunchGame, this, [this] {
        switchPage(0);
        m_dashboard->launchAll();
    });

    connect(m_settings, &SettingsPage::configSaved, this, [this] {
        if (m_statusLabel)
            m_statusLabel->setText(tr("配置已保存并生效"));
    });

    switchPage(0);
}

QWidget *MainWindow::buildSidebar()
{
    auto *bar = new QFrame;
    bar->setObjectName(QStringLiteral("sidebar"));
    bar->setFixedWidth(228);

    auto *lay = new QVBoxLayout(bar);
    lay->setContentsMargins(16, 20, 16, 16);
    lay->setSpacing(4);

    // ── 品牌区 ──
    auto *logoRow = new QHBoxLayout;
    logoRow->setSpacing(10);

    auto *mark = new QLabel(QStringLiteral("E"));
    mark->setObjectName(QStringLiteral("logoMark"));
    mark->setAlignment(Qt::AlignCenter);
    mark->setFixedSize(34, 34);

    auto *textCol = new QVBoxLayout;
    textCol->setSpacing(1);
    auto *appName = new QLabel(QStringLiteral("EDEN LAUNCHER"));
    appName->setObjectName(QStringLiteral("logoText"));
    auto *appSub = new QLabel(QStringLiteral("REIMAGINED 2.0"));
    appSub->setObjectName(QStringLiteral("logoSub"));
    textCol->addWidget(appName);
    textCol->addWidget(appSub);

    logoRow->addWidget(mark);
    logoRow->addLayout(textCol);
    logoRow->addStretch(1);
    lay->addLayout(logoRow);
    lay->addSpacing(22);

    // ── 导航 ──
    auto *section = new QLabel(tr("主 导 航"));
    section->setObjectName(QStringLiteral("navSection"));
    lay->addWidget(section);
    lay->addWidget(addNav(tr("游戏控制台"), QStringLiteral("dashboard"), 0));
    lay->addWidget(addNav(tr("游戏库"), QStringLiteral("package"), 1));
    lay->addWidget(addNav(tr("智能工具箱"), QStringLiteral("tools"), 2));
    lay->addWidget(addNav(tr("更新与构建"), QStringLiteral("update"), 3));

    lay->addSpacing(16);
    auto *section2 = new QLabel(tr("偏 好 设 置"));
    section2->setObjectName(QStringLiteral("navSection"));
    lay->addWidget(section2);
    lay->addWidget(addNav(tr("设置"), QStringLiteral("settings"), 4));

    lay->addStretch(1);

    auto *foot = new QLabel(tr("Eden Launcher 2.0.0\nQt %1 · Windows x64")
                                .arg(QString::fromLatin1(qVersion())));
    foot->setObjectName(QStringLiteral("sidebarFooter"));
    foot->setWordWrap(true);
    lay->addWidget(foot);

    return bar;
}

QPushButton *MainWindow::addNav(const QString &text, const QString &iconName, int pageIndex)
{
    auto *btn = new QPushButton(IconFactory::icon(iconName, Theme::textMuted()), text);
    btn->setObjectName(QStringLiteral("navItem"));
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setIconSize(QSize(18, 18));
    btn->setMinimumHeight(38);
    connect(btn, &QPushButton::clicked, this, [this, pageIndex] { switchPage(pageIndex); });
    m_navButtons.append(btn);
    return btn;
}

QWidget *MainWindow::buildFooter()
{
    auto *foot = new QFrame;
    foot->setObjectName(QStringLiteral("footerBar"));
    foot->setStyleSheet(QStringLiteral(
        "QFrame#footerBar { background:#0C0E14; border-top:1px solid #191D27; }"));
    foot->setFixedHeight(30);

    auto *lay = new QHBoxLayout(foot);
    lay->setContentsMargins(18, 0, 18, 0);
    lay->setSpacing(12);

    m_statusLabel = new QLabel(tr("就绪"));
    m_statusLabel->setStyleSheet(QStringLiteral("color:#59627A; font-size:11px; background:transparent;"));
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_statusLabel->setMinimumWidth(0);
    lay->addWidget(m_statusLabel, 1);

    auto *hint = new QLabel(tr("WGC 采集 · 插帧就绪"));
    hint->setStyleSheet(QStringLiteral("color:#3E4658; font-size:11px; background:transparent;"));
    lay->addWidget(hint);

    return foot;
}

void MainWindow::switchPage(int index)
{
    if (!m_stack || index < 0 || index >= m_stack->count())
        return;
    m_stack->setCurrentIndex(index);
    for (int i = 0; i < m_navButtons.size(); ++i)
        m_navButtons.at(i)->setChecked(i == index);
}
