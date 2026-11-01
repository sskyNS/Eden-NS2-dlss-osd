#include "DashboardPage.h"

#include "AppConfig.h"
#include "IconFactory.h"
#include "Theme.h"
#include "core/EdenClient.h"
#include "core/LosslessScaling.h"
#include "core/ProcessUtil.h"
#include "widgets/Ui.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

DashboardPage::DashboardPage(EdenClient *eden, LosslessScaling *ls, QWidget *parent)
    : QWidget(parent)
    , m_eden(eden)
    , m_ls(ls)
{
    buildUi();
    wireSignals();
    updateRunState();
    updateFrameGenUi();
}

DashboardPage::~DashboardPage()
{
    stopOsd();
}

void DashboardPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 22, 26, 22);
    root->setSpacing(16);

    // ── 顶栏 ──
    auto *header = new QHBoxLayout;
    auto *titleCol = new QVBoxLayout;
    titleCol->setSpacing(2);
    titleCol->addWidget(Ui::makeH1(tr("游戏控制台")));
    titleCol->addWidget(Ui::makeMuted(tr("一键启动模拟器、画面插帧与性能面板")));
    header->addLayout(titleCol);
    header->addStretch(1);
    m_stateBadge = Ui::makeBadge(tr("未运行"));
    header->addWidget(m_stateBadge);
    root->addLayout(header);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *canvas = new QWidget;
    auto *content = new QVBoxLayout(canvas);
    content->setContentsMargins(0, 0, 8, 0);
    content->setSpacing(16);

    // ── 主卡片 ──
    auto *hero = new Ui::Card(tr("当前游戏"));
    hero->setHighlight(true);

    auto *heroTop = new QHBoxLayout;
    heroTop->setSpacing(16);
    auto *nameCol = new QVBoxLayout;
    nameCol->setSpacing(4);
    m_gameName = new QLabel(AppConfig::instance().gameName());
    m_gameName->setProperty("h1", true);
    m_gamePath = new Ui::ElidedLabel;
    nameCol->addWidget(m_gameName);
    nameCol->addWidget(m_gamePath);
    heroTop->addLayout(nameCol, 1);
    hero->body()->addLayout(heroTop);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(10);
    m_launchBtn = Ui::makeButton(tr("一键启动"), QStringLiteral("play"), true);
    m_launchBtn->setMinimumHeight(40);
    m_launchBtn->setMinimumWidth(150);
    m_stopBtn = Ui::makeButton(tr("全部停止"), QStringLiteral("stop"), false, true);
    m_stopBtn->setMinimumHeight(40);
    actions->addWidget(m_launchBtn);
    actions->addWidget(m_stopBtn);
    actions->addStretch(1);
    hero->body()->addLayout(actions);

    // ── 状态块 ──
    auto *stats = new QGridLayout;
    stats->setSpacing(12);
    auto makeTile = [](const QString &label, QLabel **valueOut) {
        auto *tile = new Ui::StatTile(label);
        for (auto *child : tile->findChildren<QLabel *>()) {
            if (child->property("statValue").toBool())
                *valueOut = child;
        }
        return tile;
    };
    stats->addWidget(makeTile(tr("游戏帧率"), &m_fpsTile), 0, 0);
    stats->addWidget(makeTile(tr("插帧状态"), &m_fgTile), 0, 1);
    stats->addWidget(makeTile(tr("运行时长"), &m_uptimeTile), 0, 2);
    stats->addWidget(makeTile(tr("进程 PID"), &m_pidTile), 0, 3);
    for (int i = 0; i < 4; ++i)
        stats->setColumnStretch(i, 1);
    hero->body()->addLayout(stats);
    content->addWidget(hero);

    // ── 增强功能卡片 ──
    auto *enhance = new Ui::Card(tr("画面增强"));
    enhance->setSubtitle(tr("Lossless Scaling 插帧与 Eden OSD 性能面板"));

    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    m_lsBtn = Ui::makeButton(tr("开启插帧"), QStringLiteral("bolt"));
    m_osdBtn = Ui::makeButton(tr("启动 OSD"), QStringLiteral("monitor"));
    m_lsLaunchBtn = Ui::makeButton(tr("启动小黄鸭"), QStringLiteral("bolt"));
    m_edenGuiBtn = Ui::makeButton(tr("Eden 主界面"), QStringLiteral("window"));
    row->addWidget(m_lsBtn);
    row->addWidget(m_osdBtn);
    row->addWidget(m_lsLaunchBtn);
    row->addWidget(m_edenGuiBtn);
    row->addStretch(1);
    enhance->body()->addLayout(row);
    enhance->body()->addWidget(Ui::makeMuted(
        tr("插帧前会自动把 Lossless Scaling 的抓取方式校正为 WGC，否则 Eden 的画面会全黑。")));
    content->addWidget(enhance);

    // ── 日志 ──
    auto *logCard = new Ui::Card(tr("运行日志"));
    m_log = new Ui::LogView;
    m_log->setMinimumHeight(110);
    m_log->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    logCard->body()->addWidget(m_log);
    content->addWidget(logCard, 1);

    scroll->setWidget(canvas);
    root->addWidget(scroll, 1);

    m_uptimeTimer = new QTimer(this);
    m_uptimeTimer->setInterval(1000);
    connect(m_uptimeTimer, &QTimer::timeout, this, &DashboardPage::tickUptime);

    m_lsWatch = new QTimer(this);
    m_lsWatch->setInterval(1500);
    connect(m_lsWatch, &QTimer::timeout, this, &DashboardPage::refreshLsStatus);
}

void DashboardPage::wireSignals()
{
    connect(m_launchBtn, &QPushButton::clicked, this, &DashboardPage::launchAll);
    connect(m_stopBtn, &QPushButton::clicked, this, &DashboardPage::stopAll);
    connect(m_edenGuiBtn, &QPushButton::clicked, this, &DashboardPage::openEdenGui);
    connect(m_osdBtn, &QPushButton::clicked, this, &DashboardPage::toggleOsd);
    connect(m_lsLaunchBtn, &QPushButton::clicked, this, [this] {
        m_ls->startLs();
    });
    connect(m_lsBtn, &QPushButton::clicked, this, [this] {
        if (m_ls->frameGenActive()) {
            m_ls->requestDisableFrameGen();
        } else if (m_eden->gameWindow()) {
            m_ls->requestEnableFrameGen(m_eden->gameWindow());
        } else {
            m_log->appendError(tr("还没有检测到游戏窗口，无法开启插帧"));
        }
    });

    connect(m_eden, &EdenClient::stateChanged, this, [this](bool) { updateRunState(); });
    connect(m_eden, &EdenClient::fpsChanged, this, &DashboardPage::updateFps);
    connect(m_eden, &EdenClient::logLine, this, [this](const QString &line) {
        m_log->appendLine(line);
        emit logLine(line);
    });
    connect(m_eden, &EdenClient::errorOccurred, this, [this](const QString &msg) {
        m_log->appendError(msg);
    });
    connect(m_eden, &EdenClient::windowFound, this, [this](quintptr) { updateRunState(); });

    connect(m_ls, &LosslessScaling::log, this, [this](const QString &line) {
        m_log->appendLine(tr("[LS] ") + line);
        emit logLine(line);
    });
    connect(m_ls, &LosslessScaling::frameGenChanged, this, [this](bool) { updateFrameGenUi(); });
    connect(m_ls, &LosslessScaling::sequenceFinished, this, [this](bool ok) {
        updateFrameGenUi();
        if (ok)
            m_log->appendOk(tr("插帧流程完成"));
        refreshLsStatus();
    });
}

void DashboardPage::launchAll()
{
    AppConfig &cfg = AppConfig::instance();
    m_gameName->setText(cfg.gameName());
    m_gamePath->setLines({cfg.gameMain(), tr("配置：%1").arg(cfg.gameConfigIni())});

    if (cfg.autoStartOsd() && !isOsdRunning())
        startOsd();

    m_launchingAll = true;
    launchGameOnly();
}

void DashboardPage::launchGameOnly()
{
    if (m_eden->isRunning()) {
        m_log->appendLine(tr("模拟器已在运行"));
        return;
    }
    m_eden->launch();
}

void DashboardPage::stopAll()
{
    m_launchingAll = false;
    if (m_eden->isRunning())
        m_eden->stop();
    updateRunState();
}

void DashboardPage::openEdenGui()
{
    AppConfig &cfg = AppConfig::instance();
    const QString exe = cfg.edenGuiExe();
    if (!QFileInfo::exists(exe)) {
        m_log->appendError(tr("找不到 Eden 主界面：%1").arg(exe));
        return;
    }
    // eden.exe 在没有标准输出/错误句柄时会自行退出，这里把输出重定向到日志文件
    const QString logFile = cfg.userDir() + "/log/eden-gui.log";
    QDir().mkpath(QFileInfo(logFile).absolutePath());

    QProcess proc;
    proc.setProgram(exe);
    proc.setWorkingDirectory(cfg.runtimeDir());
    proc.setStandardOutputFile(logFile, QIODevice::WriteOnly);
    proc.setStandardErrorFile(logFile, QIODevice::WriteOnly);

    if (!proc.startDetached()) {
        m_log->appendError(tr("Eden 主界面启动失败"));
        return;
    }
    m_log->appendOk(tr("已打开 Eden 主界面"));
}

void DashboardPage::updateRunState()
{
    const bool running = m_eden->isRunning();
    m_stateBadge->setText(running ? tr("运行中") : tr("未运行"));
    m_stateBadge->setProperty("badge", running ? QStringLiteral("ok") : QString());
    m_stateBadge->style()->unpolish(m_stateBadge);
    m_stateBadge->style()->polish(m_stateBadge);

    m_launchBtn->setEnabled(!running);
    m_stopBtn->setEnabled(running);
    m_pidTile->setText(running ? QString::number(m_eden->pid()) : QStringLiteral("--"));

    AppConfig &cfg = AppConfig::instance();
    m_gameName->setText(cfg.gameName());
    m_gamePath->setLines({cfg.gameMain(), tr("配置：%1").arg(cfg.gameConfigIni())});

    if (running) {
        if (m_startedAt == 0)
            m_startedAt = QDateTime::currentMSecsSinceEpoch();
        m_uptimeTimer->start();
        m_lsWatch->start();
        tickUptime();
    } else {
        m_startedAt = 0;
        m_uptimeTimer->stop();
        m_lsWatch->stop();
        m_uptimeTile->setText(QStringLiteral("--"));
        m_fpsTile->setText(QStringLiteral("--"));
    }
}

void DashboardPage::updateFps(int fps)
{
    m_fpsTile->setText(fps > 0 ? QStringLiteral("%1").arg(fps) : QStringLiteral("--"));
}

void DashboardPage::tickUptime()
{
    if (m_startedAt == 0) {
        m_uptimeTile->setText(QStringLiteral("--"));
        return;
    }
    const qint64 secs = (QDateTime::currentMSecsSinceEpoch() - m_startedAt) / 1000;
    const qint64 h = secs / 3600;
    const qint64 m = (secs % 3600) / 60;
    const qint64 s = secs % 60;
    m_uptimeTile->setText(h > 0
                              ? QStringLiteral("%1:%2:%3")
                                    .arg(h)
                                    .arg(m, 2, 10, QLatin1Char('0'))
                                    .arg(s, 2, 10, QLatin1Char('0'))
                              : QStringLiteral("%1:%2")
                                    .arg(m, 2, 10, QLatin1Char('0'))
                                    .arg(s, 2, 10, QLatin1Char('0')));
}

void DashboardPage::updateFrameGenUi()
{
    const bool active = m_ls->frameGenActive();
    m_lsBtn->setText(active ? tr("关闭插帧") : tr("开启插帧"));
    m_fgTile->setText(active ? tr("已开启") : tr("关闭"));
    m_fgTile->setStyleSheet(QStringLiteral("color:%1;")
                                .arg(active ? Theme::success().name() : Theme::textMuted().name()));
}

void DashboardPage::refreshLsStatus()
{
    updateFrameGenUi();
    // 游戏窗口已就绪但还没开插帧 → 自动走一次。
    // 注意：Eden 的 FPS 只写在状态栏控件里，窗口标题没有 FPS，
    // 因此这里不能再以 currentFps() > 0 作为条件，交给 LosslessScaling 内部按窗口稳定度判定。
    if (m_launchingAll && AppConfig::instance().autoStartLs() && m_eden->isRunning()
        && !m_ls->frameGenActive() && m_eden->gameWindow() != 0 && !m_ls->isBusy()) {
        m_launchingAll = false;
        m_ls->requestEnableFrameGen(m_eden->gameWindow());
    }
}

bool DashboardPage::isOsdRunning() const
{
    return m_osdProcess && m_osdProcess->state() != QProcess::NotRunning;
}

void DashboardPage::toggleOsd()
{
    if (isOsdRunning())
        stopOsd();
    else
        startOsd();
}

void DashboardPage::startOsd()
{
    AppConfig &cfg = AppConfig::instance();
    const QString script = cfg.osdScript();
    if (!QFileInfo::exists(script)) {
        m_log->appendError(tr("找不到 OSD 脚本：%1").arg(script));
        return;
    }

    if (!m_osdProcess) {
        m_osdProcess = new QProcess(this);
        m_osdProcess->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_osdProcess, &QProcess::readyReadStandardOutput, this, [this] {
            const QString text = QString::fromLocal8Bit(m_osdProcess->readAllStandardOutput());
            for (const QString &line : text.split('\n', Qt::SkipEmptyParts))
                m_log->appendLine(tr("[OSD] ") + line.trimmed());
        });
        connect(m_osdProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus) {
                    m_log->appendLine(tr("[OSD] 已退出（代码 %1）").arg(code));
                    m_osdBtn->setText(tr("启动 OSD"));
                });
    }

    m_osdProcess->setWorkingDirectory(cfg.osdDir());
    m_osdProcess->setProgram(QStringLiteral("powershell.exe"));
    m_osdProcess->setArguments({QStringLiteral("-NoProfile"),
                                QStringLiteral("-Sta"),
                                QStringLiteral("-ExecutionPolicy"),
                                QStringLiteral("Bypass"),
                                QStringLiteral("-WindowStyle"),
                                QStringLiteral("Hidden"),
                                QStringLiteral("-File"),
                                script});
    m_osdProcess->start();
    if (!m_osdProcess->waitForStarted(5000)) {
        m_log->appendError(tr("OSD 启动失败：%1").arg(m_osdProcess->errorString()));
        return;
    }
    m_osdBtn->setText(tr("停止 OSD"));
    m_log->appendOk(tr("OSD 已启动"));
}

void DashboardPage::stopOsd()
{
    if (!m_osdProcess || m_osdProcess->state() == QProcess::NotRunning) {
        m_osdBtn->setText(tr("启动 OSD"));
        return;
    }
    m_osdProcess->kill();
    m_osdProcess->waitForFinished(3000);
    m_osdBtn->setText(tr("启动 OSD"));
    if (m_log)
        m_log->appendLine(tr("OSD 已停止"));
}
