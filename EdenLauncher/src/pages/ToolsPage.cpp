#include "ToolsPage.h"

#include "AppConfig.h"
#include "IconFactory.h"
#include "Theme.h"
#include "core/AudioMixer.h"
#include "core/EdenClient.h"
#include "core/SaveManager.h"
#include "core/WinUtil.h"
#include "widgets/Ui.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString humanBytes(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace

ToolsPage::ToolsPage(AudioMixer *audio, SaveManager *saves, EdenClient *eden, QWidget *parent)
    : QWidget(parent)
    , m_audio(audio)
    , m_saves(saves)
    , m_eden(eden)
{
    buildUi();

    m_audioTimer = new QTimer(this);
    m_audioTimer->setInterval(3000);
    connect(m_audioTimer, &QTimer::timeout, this, [this] { refreshAudio(); });
    m_audioTimer->start();

    connect(m_eden, &EdenClient::stateChanged, this, [this](bool) { refreshWindowInfo(); });

    refreshAudio(true);
    refreshSaves();
    refreshWindowInfo();
}

void ToolsPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 18, 26, 16);
    root->setSpacing(12);

    auto *header = new QHBoxLayout;
    auto *titleCol = new QVBoxLayout;
    titleCol->setSpacing(2);
    titleCol->addWidget(Ui::makeH1(tr("工具箱")));
    titleCol->addWidget(Ui::makeMuted(tr("音量、存档与窗口的即开即用控制")));
    header->addLayout(titleCol);
    header->addStretch(1);
    root->addLayout(header);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *canvas = new QWidget;
    auto *content = new QVBoxLayout(canvas);
    content->setContentsMargins(0, 0, 8, 0);
    content->setSpacing(12);

    content->addWidget(buildAudioCard());
    content->addWidget(buildSaveCard());
    content->addWidget(buildWindowCard());
    content->addStretch(1);

    scroll->setWidget(canvas);
    root->addWidget(scroll, 1);
}

// ── 音量与音频隔离 ────────────────────────────────────
QWidget *ToolsPage::buildAudioCard()
{
    auto *card = new Ui::Card(tr("音量与音频隔离"));
    card->setSubtitle(tr("按进程调节播放音量；隔离后只保留游戏声音，其它应用全部静音"));

    m_isolateBadge = Ui::makeBadge(tr("未隔离"));
    m_isolateBadge->hide();
    card->addHeaderWidget(m_isolateBadge);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(10);
    auto *refreshBtn = Ui::makeGhostButton(tr("刷新"), QStringLiteral("refresh"));
    auto *isolateBtn = Ui::makeButton(tr("只保留游戏声音"), QStringLiteral("volume"));
    auto *restoreBtn = Ui::makeButton(tr("恢复全部"), QStringLiteral("music"));
    toolbar->addWidget(refreshBtn);
    toolbar->addWidget(isolateBtn);
    toolbar->addWidget(restoreBtn);
    toolbar->addStretch(1);
    m_audioHint = Ui::makeMuted(QString());
    toolbar->addWidget(m_audioHint);
    card->body()->addLayout(toolbar);

    auto *sep = Ui::makeSeparator();
    card->body()->addWidget(sep);

    auto *holder = new QWidget;
    m_sessionLayout = new QVBoxLayout(holder);
    m_sessionLayout->setContentsMargins(0, 0, 0, 0);
    m_sessionLayout->setSpacing(6);
    card->body()->addWidget(holder);

    connect(refreshBtn, &QPushButton::clicked, this, [this] { refreshAudio(true); });
    connect(isolateBtn, &QPushButton::clicked, this, [this] {
        const quint32 pid = m_eden ? m_eden->pid() : 0;
        if (pid == 0) {
            m_audioHint->setText(tr("请先在控制台启动游戏"));
            return;
        }
        const int count = m_audio->isolate(pid);
        m_isolateBadge->setText(tr("已隔离 · %1 个会话静音").arg(count));
        Ui::setStyleProperty(m_isolateBadge, "badge", QStringLiteral("ok"));
        m_isolateBadge->show();
        refreshAudio(true);
    });
    connect(restoreBtn, &QPushButton::clicked, this, [this] {
        if (!m_audio->hasSnapshot()) {
            m_audioHint->setText(tr("当前没有需要恢复的隔离状态"));
            return;
        }
        m_audio->restoreAll();
        m_isolateBadge->setText(tr("未隔离"));
        Ui::setStyleProperty(m_isolateBadge, "badge", QStringLiteral("true"));
        m_isolateBadge->hide();
        refreshAudio(true);
    });

    updateAudioHint();
    return card;
}

void ToolsPage::clearAudioRows()
{
    for (const AudioRow &row : m_rows) {
        if (row.root) {
            m_sessionLayout->removeWidget(row.root);
            row.root->deleteLater();
        }
    }
    m_rows.clear();
}

void ToolsPage::updateAudioHint()
{
    if (!m_audioHint)
        return;
    m_audioHint->setText(tr("%1 个音频会话").arg(m_rows.size()));
}

void ToolsPage::refreshAudio(bool force)
{
    if (!m_audio)
        return;
    if (!force && !isVisible())
        return;
    for (const AudioRow &row : m_rows) {
        if (row.slider && row.slider->isSliderDown())
            return; // 用户正在拖动，别打断
    }

    const QList<AudioSessionInfo> list = m_audio->sessions();

    bool sameSet = list.size() == m_rows.size();
    if (sameSet) {
        for (int i = 0; i < list.size(); ++i) {
            if (m_rows[i].pid != list[i].pid) {
                sameSet = false;
                break;
            }
        }
    }

    if (!sameSet) {
        clearAudioRows();
        for (const AudioSessionInfo &info : list) {
            AudioRow row;
            row.pid = info.pid;

            row.root = new QWidget;
            auto *lay = new QHBoxLayout(row.root);
            lay->setContentsMargins(8, 4, 8, 4);
            lay->setSpacing(12);

            row.nameLabel = new QLabel;
            row.nameLabel->setMinimumWidth(170);
            row.nameLabel->setMaximumWidth(220);
            row.nameLabel->setToolTip(info.processName);
            row.root->setToolTip(QStringLiteral("PID %1").arg(info.pid));

            row.slider = new QSlider(Qt::Horizontal);
            row.slider->setRange(0, 100);
            row.slider->setMinimumWidth(120);

            row.pctLabel = new QLabel;
            row.pctLabel->setFixedWidth(46);
            row.pctLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

            row.muteBtn = new QPushButton;
            row.muteBtn->setCheckable(true);
            row.muteBtn->setCursor(Qt::PointingHandCursor);
            row.muteBtn->setFixedSize(28, 28);
            row.muteBtn->setIconSize(QSize(15, 15));
            row.muteBtn->setProperty("ghost", true);

            lay->addWidget(row.nameLabel);
            lay->addWidget(row.slider, 1);
            lay->addWidget(row.pctLabel);
            lay->addWidget(row.muteBtn);

            const quint32 pid = info.pid;
            connect(row.slider, &QSlider::valueChanged, this, [this, pid](int v) {
                if (m_updatingAudio)
                    return;
                m_audio->setVolume(pid, float(v) / 100.0f);
            });
            connect(row.muteBtn, &QPushButton::toggled, this, [this, pid](bool muted) {
                if (m_updatingAudio)
                    return;
                m_audio->setMuted(pid, muted);
                refreshAudio(true);
            });

            m_sessionLayout->addWidget(row.root);
            m_rows.append(row);
        }
        updateAudioHint();
    }

    m_updatingAudio = true;
    for (int i = 0; i < list.size() && i < m_rows.size(); ++i) {
        AudioRow &row = m_rows[i];
        const AudioSessionInfo &info = list[i];
        if (info.isSystem) {
            row.nameLabel->setText(tr("系统声音"));
        } else {
            row.nameLabel->setText(QFileInfo(info.processName).completeBaseName());
        }
        const int pct = qRound(info.volume * 100.0f);
        row.slider->setValue(pct);
        row.pctLabel->setText(QStringLiteral("%1%").arg(pct));
        row.pctLabel->setStyleSheet(info.muted ? QStringLiteral("color:%1;").arg(Theme::danger().name())
                                               : QStringLiteral("color:%1;").arg(Theme::textMuted().name()));
        row.muteBtn->setChecked(info.muted);
        row.muteBtn->setIcon(IconFactory::icon(info.muted ? QStringLiteral("mute")
                                                          : QStringLiteral("volume"),
                                               info.muted ? Theme::danger() : Theme::textMuted(), 15));
    }
    m_updatingAudio = false;
}

// ── 存档导出 ──────────────────────────────────────────
QWidget *ToolsPage::buildSaveCard()
{
    auto *card = new Ui::Card(tr("存档快速导出"));
    card->setSubtitle(tr("按标题 ID 打包成 zip，默认保存到桌面，方便备份与迁移"));

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(10);
    auto *refreshBtn = Ui::makeGhostButton(tr("刷新"), QStringLiteral("refresh"));
    auto *exportBtn = Ui::makeButton(tr("导出选中"), QStringLiteral("download"), true);
    auto *exportAllBtn = Ui::makeButton(tr("导出全部"), QStringLiteral("save"));
    auto *openDirBtn = Ui::makeGhostButton(tr("打开存档目录"), QStringLiteral("folder"));
    toolbar->addWidget(refreshBtn);
    toolbar->addWidget(exportBtn);
    toolbar->addWidget(exportAllBtn);
    toolbar->addWidget(openDirBtn);
    toolbar->addStretch(1);
    card->body()->addLayout(toolbar);

    m_saveTable = new QTableWidget(0, 4);
    m_saveTable->setHorizontalHeaderLabels(
        {tr("标题 ID"), tr("存档数"), tr("占用空间"), tr("最后修改")});
    m_saveTable->verticalHeader()->setVisible(false);
    m_saveTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_saveTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_saveTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_saveTable->setShowGrid(false);
    m_saveTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 4; ++i)
        m_saveTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    m_saveTable->setMinimumHeight(120);
    m_saveTable->setMaximumHeight(240);
    card->body()->addWidget(m_saveTable);

    m_saveHint = Ui::makeMuted(QString());
    m_saveHint->setWordWrap(true);
    card->body()->addWidget(m_saveHint);

    connect(refreshBtn, &QPushButton::clicked, this, &ToolsPage::refreshSaves);
    connect(exportBtn, &QPushButton::clicked, this, &ToolsPage::exportSelected);
    connect(exportAllBtn, &QPushButton::clicked, this, &ToolsPage::exportAll);
    connect(openDirBtn, &QPushButton::clicked, this, [this] {
        SaveManager::revealInExplorer(AppConfig::instance().saveDir());
    });

    return card;
}

void ToolsPage::refreshSaves()
{
    if (!m_saveTable)
        return;
    const QList<SaveTitle> list = m_saves->titles();
    m_saveTable->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i) {
        const SaveTitle &t = list.at(i);
        auto *idItem = new QTableWidgetItem(t.titleId);
        idItem->setData(Qt::UserRole, t.titleId);
        m_saveTable->setItem(i, 0, idItem);
        m_saveTable->setItem(i, 1, new QTableWidgetItem(QString::number(t.saveCount)));
        m_saveTable->setItem(i, 2, new QTableWidgetItem(humanBytes(t.totalBytes)));
        m_saveTable->setItem(i, 3,
                             new QTableWidgetItem(t.lastModified.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    }
    if (list.isEmpty()) {
        m_saveHint->setText(tr("没有找到存档：%1").arg(AppConfig::instance().saveDir()));
    } else {
        m_saveHint->setText(tr("共 %1 个游戏存档，路径：%2")
                                .arg(list.size())
                                .arg(AppConfig::instance().saveDir()));
    }

    // 表格高度跟随行数，避免空表占满整页
    const int headerH = m_saveTable->horizontalHeader()->sizeHint().height();
    const int wanted = headerH + 34 * qMax(1, int(list.size())) + 6;
    m_saveTable->setFixedHeight(qBound(110, wanted, 300));
}

void ToolsPage::exportSelected()
{
    const int row = m_saveTable->currentRow();
    if (row < 0) {
        m_saveHint->setText(tr("请先在上表中选择一个存档"));
        return;
    }
    const QString titleId = m_saveTable->item(row, 0)->data(Qt::UserRole).toString();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString suggested = QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
        + QStringLiteral("/Eden存档_%1_%2.zip").arg(titleId, stamp));

    const QString path = QFileDialog::getSaveFileName(this, tr("导出存档"), suggested,
                                                      tr("ZIP 压缩包 (*.zip)"));
    if (path.isEmpty())
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool ok = m_saves->exportTitle(titleId, path, &error);
    QApplication::restoreOverrideCursor();

    if (ok) {
        m_saveHint->setText(tr("已导出：%1").arg(QDir::toNativeSeparators(path)));
        SaveManager::revealInExplorer(QFileInfo(path).absolutePath());
    } else {
        m_saveHint->setText(tr("导出失败：%1").arg(error));
    }
}

void ToolsPage::exportAll()
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString suggested = QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
        + QStringLiteral("/Eden存档_全部_%1.zip").arg(stamp));

    const QString path = QFileDialog::getSaveFileName(this, tr("导出全部存档"), suggested,
                                                      tr("ZIP 压缩包 (*.zip)"));
    if (path.isEmpty())
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool ok = m_saves->exportAll(path, &error);
    QApplication::restoreOverrideCursor();

    if (ok) {
        m_saveHint->setText(tr("已导出全部存档：%1").arg(QDir::toNativeSeparators(path)));
        SaveManager::revealInExplorer(QFileInfo(path).absolutePath());
    } else {
        m_saveHint->setText(tr("导出失败：%1").arg(error));
    }
}

// ── 窗口调整 ──────────────────────────────────────────
QWidget *ToolsPage::buildWindowCard()
{
    auto *card = new Ui::Card(tr("游戏窗口调整"));
    card->setSubtitle(tr("一键把游戏窗口改成固定分辨率，或切换全屏 / 无边框 · 分辨率会按系统缩放换算为物理像素"));

    auto *backBtn = Ui::makeGhostButton(tr("打开控制台"), QStringLiteral("dashboard"));
    card->addHeaderWidget(backBtn);
    connect(backBtn, &QPushButton::clicked, this, &ToolsPage::requestDashboard);

    m_windowInfo = Ui::makeMuted(tr("等待游戏窗口…"));
    card->body()->addWidget(m_windowInfo);

    auto *sizes = new QGridLayout;
    sizes->setSpacing(10);
    struct Preset { const char *label; int w; int h; };
    const Preset presets[] = {
        {"1280 × 720", 1280, 720},
        {"1600 × 900", 1600, 900},
        {"1920 × 1080", 1920, 1080},
        {"2560 × 1440", 2560, 1440},
        {"2560 × 1600", 2560, 1600},
    };
    int index = 0;
    for (const Preset &p : presets) {
        auto *btn = Ui::makeButton(QString::fromUtf8(p.label), QStringLiteral("window"));
        btn->setMinimumHeight(36);
        const int w = p.w;
        const int h = p.h;
        connect(btn, &QPushButton::clicked, this, [this, w, h] { applySize(w, h); });
        sizes->addWidget(btn, index / 5, index % 5);
        ++index;
    }
    for (int i = 0; i < 5; ++i)
        sizes->setColumnStretch(i, 1);
    card->body()->addLayout(sizes);

    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    auto *centerBtn = Ui::makeButton(tr("居中"), QStringLiteral("window"));
    auto *maxBtn = Ui::makeButton(tr("最大化"), QStringLiteral("window"));
    auto *borderBtn = Ui::makeButton(tr("切换无边框"), QStringLiteral("window"));
    auto *refreshBtn = Ui::makeGhostButton(tr("刷新窗口"), QStringLiteral("refresh"));
    row->addWidget(centerBtn);
    row->addWidget(maxBtn);
    row->addWidget(borderBtn);
    row->addWidget(refreshBtn);
    row->addStretch(1);
    card->body()->addLayout(row);

    connect(centerBtn, &QPushButton::clicked, this, &ToolsPage::centerGameWindow);
    connect(maxBtn, &QPushButton::clicked, this, &ToolsPage::maximizeGameWindow);
    connect(borderBtn, &QPushButton::clicked, this, &ToolsPage::toggleBorderless);
    connect(refreshBtn, &QPushButton::clicked, this, &ToolsPage::refreshWindowInfo);

    return card;
}

quintptr ToolsPage::gameWindow() const
{
    if (!m_eden)
        return 0;
    if (m_eden->gameWindow())
        return m_eden->gameWindow();
    if (m_eden->pid())
        return WinUtil::mainWindowOfProcess(m_eden->pid());
    return 0;
}

void ToolsPage::refreshWindowInfo()
{
    if (!m_windowInfo)
        return;
    const quintptr hwnd = gameWindow();
    if (!hwnd) {
        m_windowInfo->setText(tr("未检测到游戏窗口，请先在控制台启动游戏"));
        return;
    }
    const QRect r = WinUtil::windowRect(hwnd);
    m_windowInfo->setText(tr("当前窗口：%1 × %2　位置 (%3, %4)")
                              .arg(r.width())
                              .arg(r.height())
                              .arg(r.x())
                              .arg(r.y()));
}

void ToolsPage::applySize(int width, int height)
{
    const quintptr hwnd = gameWindow();
    if (!hwnd) {
        m_windowInfo->setText(tr("未检测到游戏窗口，请先在控制台启动游戏"));
        return;
    }
    const qreal dpr = WinUtil::devicePixelRatioForWindow(hwnd);
    const QRect screen = WinUtil::virtualScreenRect();
    const int pw = int(width * dpr);
    const int ph = int(height * dpr);
    const int x = screen.x() + (screen.width() - pw) / 2;
    const int y = screen.y() + (screen.height() - ph) / 2;

    if (WinUtil::setWindowRect(hwnd, QRect(x, y, pw, ph))) {
        AppConfig::instance().setWindowPreset(QStringLiteral("%1x%2").arg(width).arg(height));
        m_windowInfo->setText(tr("已调整为 %1 × %2").arg(width).arg(height));
    } else {
        m_windowInfo->setText(tr("窗口调整失败，可能是游戏处于全屏独占模式"));
    }
    refreshWindowInfo();
}

void ToolsPage::centerGameWindow()
{
    const quintptr hwnd = gameWindow();
    if (!hwnd) {
        m_windowInfo->setText(tr("未检测到游戏窗口，请先在控制台启动游戏"));
        return;
    }
    WinUtil::centerWindow(hwnd);
    refreshWindowInfo();
}

void ToolsPage::maximizeGameWindow()
{
    const quintptr hwnd = gameWindow();
    if (!hwnd) {
        m_windowInfo->setText(tr("未检测到游戏窗口，请先在控制台启动游戏"));
        return;
    }
    WinUtil::maximizeWindow(hwnd);
    refreshWindowInfo();
}

void ToolsPage::toggleBorderless()
{
    const quintptr hwnd = gameWindow();
    if (!hwnd) {
        m_windowInfo->setText(tr("未检测到游戏窗口，请先在控制台启动游戏"));
        return;
    }
    WinUtil::toggleBorderless(hwnd);
    refreshWindowInfo();
}
