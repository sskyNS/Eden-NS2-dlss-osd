#include "SettingsPage.h"

#include "AppConfig.h"
#include "Theme.h"
#include "widgets/Ui.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QWidget *makeEditRow(QWidget *owner, QLineEdit *edit, const QString &mode,
                     const QString &filter = QString())
{
    auto *holder = new QWidget;
    auto *lay = new QHBoxLayout(holder);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);
    lay->addWidget(edit, 1);

    if (!mode.isEmpty()) {
        auto *browse = Ui::makeGhostButton(QObject::tr("浏览…"));
        lay->addWidget(browse);
        QObject::connect(browse, &QPushButton::clicked, owner, [owner, edit, mode, filter] {
            QString start = edit->text();
            if (start.isEmpty() || !QFileInfo::exists(start))
                start = QDir::homePath();
            QString path;
            if (mode == QLatin1String("dir")) {
                path = QFileDialog::getExistingDirectory(owner, QObject::tr("选择目录"), start);
            } else {
                path = QFileDialog::getOpenFileName(owner, QObject::tr("选择文件"), start, filter);
            }
            if (!path.isEmpty())
                edit->setText(QDir::toNativeSeparators(path));
        });
    }
    return holder;
}

} // namespace

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    loadValues();
}

void SettingsPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 22, 26, 22);
    root->setSpacing(16);

    // ── 顶栏 ──
    auto *header = new QHBoxLayout;
    auto *titleCol = new QVBoxLayout;
    titleCol->setSpacing(2);
    titleCol->addWidget(Ui::makeH1(tr("设置")));
    titleCol->addWidget(Ui::makeMuted(tr("所有路径与启动行为都保存在本机，可随时调整")));
    header->addLayout(titleCol);
    header->addStretch(1);
    m_status = Ui::makeMuted(QString());
    header->addWidget(m_status);
    root->addLayout(header);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *canvas = new QWidget;
    auto *content = new QVBoxLayout(canvas);
    content->setContentsMargins(0, 0, 8, 0);
    content->setSpacing(16);

    auto *runtimeCard = new Ui::Card(tr("运行时"));
    runtimeCard->setSubtitle(tr("Eden NS2 运行时目录，决定 eden-cli.exe、游戏与存档的位置"));
    m_runtimeDir = new QLineEdit;
    m_runtimeDir->setPlaceholderText(tr("例如 E:/Eden_NS2_1.1.0"));
    runtimeCard->body()->addWidget(
        makeEditRow(this, m_runtimeDir, QStringLiteral("dir")));
    auto *detectBtn = Ui::makeGhostButton(tr("自动探测"), QStringLiteral("refresh"));
    runtimeCard->body()->addWidget(detectBtn);
    connect(detectBtn, &QPushButton::clicked, this, &SettingsPage::detectRuntime);
    content->addWidget(runtimeCard);

    // ── 游戏 ──
    auto *gameCard = new Ui::Card(tr("游戏启动"));
    gameCard->setSubtitle(tr("启动参数与配置文件路径"));
    auto *gameForm = new QFormLayout;
    gameForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    gameForm->setHorizontalSpacing(18);
    gameForm->setVerticalSpacing(10);

    m_gameName = new QLineEdit;
    m_gameMain = new QLineEdit;
    m_gameIni = new QLineEdit;
    m_extraArgs = new QLineEdit;
    m_extraArgs->setPlaceholderText(tr("多个参数用空格分隔（可选）"));

    gameForm->addRow(tr("游戏名称"), m_gameName);
    gameForm->addRow(tr("游戏 main 文件"), makeEditRow(this, m_gameMain, QStringLiteral("file")));
    gameForm->addRow(tr("配置文件 (.ini)"), makeEditRow(this, m_gameIni, QStringLiteral("file")));
    gameForm->addRow(tr("附加参数"), m_extraArgs);
    gameCard->body()->addLayout(gameForm);
    content->addWidget(gameCard);

    // ── 画面增强 ──
    auto *enhanceCard = new Ui::Card(tr("画面增强（小黄鸭 / OSD）"));
    enhanceCard->setSubtitle(tr("Lossless Scaling 插帧与 Eden OSD 性能面板的启动方式"));
    auto *enhanceForm = new QFormLayout;
    enhanceForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    enhanceForm->setHorizontalSpacing(18);
    enhanceForm->setVerticalSpacing(10);

    m_lsExe = new QLineEdit;
    m_lsHotkey = new QLineEdit;
    m_lsHotkey->setPlaceholderText(tr("例如 Ctrl+Alt+S"));
    enhanceForm->addRow(tr("小黄鸭程序"), makeEditRow(this, m_lsExe, QStringLiteral("file")));
    enhanceForm->addRow(tr("插帧热键"), m_lsHotkey);
    enhanceCard->body()->addLayout(enhanceForm);

    m_autoLs = new QCheckBox(tr("一键启动后自动开启插帧"));
    m_autoOsd = new QCheckBox(tr("一键启动时同时拉起 OSD"));
    m_forceWgc = new QCheckBox(tr("自动把小黄鸭抓取方式校正为 WGC（推荐）"));
    enhanceCard->body()->addWidget(m_autoLs);
    enhanceCard->body()->addWidget(m_autoOsd);
    enhanceCard->body()->addWidget(m_forceWgc);
    content->addWidget(enhanceCard);

    // ── 窗口 ──
    auto *windowCard = new Ui::Card(tr("窗口"));
    auto *windowForm = new QFormLayout;
    windowForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    windowForm->setHorizontalSpacing(18);
    windowForm->setVerticalSpacing(10);
    m_windowPreset = new QComboBox;
    m_windowPreset->addItem(tr("保持游戏默认"), QString());
    m_windowPreset->addItem(QStringLiteral("1280 × 720"), QStringLiteral("1280x720"));
    m_windowPreset->addItem(QStringLiteral("1600 × 900"), QStringLiteral("1600x900"));
    m_windowPreset->addItem(QStringLiteral("1920 × 1080"), QStringLiteral("1920x1080"));
    m_windowPreset->addItem(QStringLiteral("2560 × 1440"), QStringLiteral("2560x1440"));
    m_windowPreset->addItem(QStringLiteral("2560 × 1600"), QStringLiteral("2560x1600"));
    windowForm->addRow(tr("启动窗口尺寸"), m_windowPreset);
    windowCard->body()->addLayout(windowForm);
    m_minimize = new QCheckBox(tr("游戏启动后把启动器最小化"));
    windowCard->body()->addWidget(m_minimize);
    content->addWidget(windowCard);

    // ── 更新与构建 ──
    auto *buildCard = new Ui::Card(tr("更新与构建"));
    buildCard->setSubtitle(tr("上游仓库与本机编译工具链"));
    auto *buildForm = new QFormLayout;
    buildForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    buildForm->setHorizontalSpacing(18);
    buildForm->setVerticalSpacing(10);

    m_repo = new QLineEdit;
    m_repo->setPlaceholderText(QStringLiteral("owner/name"));
    m_sourceDir = new QLineEdit;
    m_patchFile = new QLineEdit;
    m_mingwRoot = new QLineEdit;
    m_gitExe = new QLineEdit;

    buildForm->addRow(tr("上游仓库"), m_repo);
    buildForm->addRow(tr("构建源码目录"), makeEditRow(this, m_sourceDir, QStringLiteral("dir")));
    buildForm->addRow(tr("NS2 补丁"), makeEditRow(this, m_patchFile, QStringLiteral("file")));
    buildForm->addRow(tr("MinGW 根目录"), makeEditRow(this, m_mingwRoot, QStringLiteral("dir")));
    buildForm->addRow(tr("Git 程序"), makeEditRow(this, m_gitExe, QStringLiteral("file")));
    buildCard->body()->addLayout(buildForm);
    content->addWidget(buildCard);

    content->addStretch(1);
    scroll->setWidget(canvas);
    root->addWidget(scroll, 1);

    // ── 底部操作栏 ──
    auto *footer = new QHBoxLayout;
    footer->setSpacing(10);
    auto *saveBtn = Ui::makeButton(tr("保存并应用"), QStringLiteral("check"), true);
    saveBtn->setMinimumHeight(38);
    saveBtn->setMinimumWidth(140);
    auto *reloadBtn = Ui::makeGhostButton(tr("放弃修改"), QStringLiteral("refresh"));
    footer->addWidget(saveBtn);
    footer->addWidget(reloadBtn);
    footer->addStretch(1);
    footer->addWidget(Ui::makeMuted(tr("Eden Launcher 2.0 · 全新重构版")));
    root->addLayout(footer);

    connect(saveBtn, &QPushButton::clicked, this, &SettingsPage::saveValues);
    connect(reloadBtn, &QPushButton::clicked, this, &SettingsPage::loadValues);
}

void SettingsPage::loadValues()
{
    const AppConfig &cfg = AppConfig::instance();
    m_runtimeDir->setText(QDir::toNativeSeparators(cfg.runtimeDir()));
    m_gameName->setText(cfg.gameName());
    m_gameMain->setText(QDir::toNativeSeparators(cfg.gameMain()));
    m_gameIni->setText(QDir::toNativeSeparators(cfg.gameConfigIni()));
    m_extraArgs->setText(cfg.extraArgs().join(QLatin1Char(' ')));

    m_lsExe->setText(QDir::toNativeSeparators(cfg.lsExe()));
    m_lsHotkey->setText(cfg.lsHotkey());
    m_autoLs->setChecked(cfg.autoStartLs());
    m_autoOsd->setChecked(cfg.autoStartOsd());
    m_forceWgc->setChecked(cfg.forceWgcCapture());

    const QString preset = cfg.windowPreset();
    int index = m_windowPreset->findData(preset);
    if (index < 0)
        index = 0;
    m_windowPreset->setCurrentIndex(index);
    m_minimize->setChecked(cfg.minimizeOnLaunch());

    m_repo->setText(cfg.updateRepo());
    m_sourceDir->setText(QDir::toNativeSeparators(cfg.buildSourceDir()));
    m_patchFile->setText(QDir::toNativeSeparators(cfg.patchFile()));
    m_mingwRoot->setText(QDir::toNativeSeparators(cfg.mingwRoot()));
    m_gitExe->setText(QDir::toNativeSeparators(cfg.gitExe()));

    m_status->clear();
}

void SettingsPage::saveValues()
{
    AppConfig &cfg = AppConfig::instance();
    cfg.setRuntimeDir(m_runtimeDir->text().trimmed());
    cfg.setGameName(m_gameName->text().trimmed());
    cfg.setGameMain(m_gameMain->text().trimmed());
    cfg.setGameConfigIni(m_gameIni->text().trimmed());
    cfg.setExtraArgs(m_extraArgs->text().split(QLatin1Char(' '), Qt::SkipEmptyParts));

    cfg.setLsExe(m_lsExe->text().trimmed());
    cfg.setLsHotkey(m_lsHotkey->text().trimmed().isEmpty() ? QStringLiteral("Ctrl+Alt+S")
                                                           : m_lsHotkey->text().trimmed());
    cfg.setAutoStartLs(m_autoLs->isChecked());
    cfg.setAutoStartOsd(m_autoOsd->isChecked());
    cfg.setForceWgcCapture(m_forceWgc->isChecked());
    cfg.setWindowPreset(m_windowPreset->currentData().toString());
    cfg.setMinimizeOnLaunch(m_minimize->isChecked());

    cfg.setUpdateRepo(m_repo->text().trimmed());
    cfg.setBuildSourceDir(m_sourceDir->text().trimmed());
    cfg.setPatchFile(m_patchFile->text().trimmed());
    cfg.setMingwRoot(m_mingwRoot->text().trimmed());
    cfg.setGitExe(m_gitExe->text().trimmed());
    cfg.save();

    loadValues();

    if (!QFileInfo::exists(cfg.runtimeDir() + "/eden-cli.exe")) {
        m_status->setText(tr("已保存，但运行时目录里没有 eden-cli.exe"));
        m_status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::warning().name()));
    } else {
        m_status->setText(tr("已保存"));
        m_status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::success().name()));
    }
    emit configSaved();
}

void SettingsPage::detectRuntime()
{
    const QString detected = AppConfig::detectRuntimeDir();
    m_runtimeDir->setText(QDir::toNativeSeparators(detected));
    m_status->setText(tr("已探测到：%1").arg(detected));
    m_status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::textMuted().name()));
}
