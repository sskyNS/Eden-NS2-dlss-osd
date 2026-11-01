#pragma once

#include <QWidget>

class EdenClient;
class LosslessScaling;
class QLabel;
class QPushButton;
class QTimer;
class QProcess;

namespace Ui { class LogView; class ElidedLabel; }

// 主控台：一键启动 / 停止、实时 FPS、插帧与 OSD 开关
class DashboardPage : public QWidget
{
    Q_OBJECT

public:
    DashboardPage(EdenClient *eden, LosslessScaling *ls, QWidget *parent = nullptr);
    ~DashboardPage() override;

    void launchAll();
    void launchGameOnly();
    void stopAll();

    bool isOsdRunning() const;

signals:
    void logLine(const QString &line);

private:
    void buildUi();
    void wireSignals();
    void toggleOsd();
    void startOsd();
    void stopOsd();
    void updateRunState();
    void updateFps(int fps);
    void updateFrameGenUi();
    void tickUptime();
    void refreshLsStatus();
    void openEdenGui();

    EdenClient *m_eden = nullptr;
    LosslessScaling *m_ls = nullptr;

    QLabel *m_gameName = nullptr;
    Ui::ElidedLabel *m_gamePath = nullptr;
    QLabel *m_stateBadge = nullptr;
    QLabel *m_fpsTile = nullptr;
    QLabel *m_fpsCaption = nullptr;
    QLabel *m_fgTile = nullptr;
    QLabel *m_uptimeTile = nullptr;
    QLabel *m_pidTile = nullptr;

    QPushButton *m_launchBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_lsBtn = nullptr;
    QPushButton *m_osdBtn = nullptr;
    QPushButton *m_lsLaunchBtn = nullptr;
    QPushButton *m_edenGuiBtn = nullptr;

    Ui::LogView *m_log = nullptr;
    QTimer *m_uptimeTimer = nullptr;
    QTimer *m_lsWatch = nullptr;
    QProcess *m_osdProcess = nullptr;
    qint64 m_startedAt = 0;
    bool m_launchingAll = false;
};
