#pragma once

#include <QObject>
#include <QRect>
#include <QString>

class QTimer;

// Lossless Scaling（小黄鸭）控制：启动、抓取 API 校正、窗口探测、热键开关插帧
class LosslessScaling : public QObject
{
    Q_OBJECT

public:
    explicit LosslessScaling(QObject *parent = nullptr);

    bool isLsRunning() const;
    bool startLs(bool fixCaptureApiFirst = true);
    bool stopLs();

    // 把 Settings.xml 里所有配置的 CaptureApi 改成 WGC（DXGI 抓不到 Eden 的 SDL 窗口）
    bool fixCaptureApi();

    // 统计可见的、无标题的 LosslessScaling 输出窗口数量
    int scaleWindowCount() const;
    bool frameGenActive() const { return scaleWindowCount() > 0; }

    // 热键流程是否正在执行（避免重复触发）
    bool isBusy() const { return m_running; }

    // 异步：等待游戏出帧 → 拉起 LS → 抢前台 → 发热键 → 验证
    void requestEnableFrameGen(quintptr gameWindow, int waitFpsMs = 120000);
    void requestDisableFrameGen();
    void cancelSequence();

signals:
    void log(const QString &line);
    void frameGenChanged(bool enabled);
    void sequenceFinished(bool success);

private:
    void step();
    void finish(bool success);
    // 游戏窗口是否已经稳定出帧（Eden 不在标题里写 FPS，改用尺寸稳定性判定）
    bool gameWindowReady(qint64 now);

    QTimer *m_timer = nullptr;
    quintptr m_gameWindow = 0;
    qint64 m_deadline = 0;
    qint64 m_stableSince = 0;
    qint64 m_verifyDeadline = 0;
    QRect m_stableRect;
    int m_hotkeyAttempts = 0;
    int m_phase = 0;
    bool m_running = false;
    bool m_lastActive = false;
};
