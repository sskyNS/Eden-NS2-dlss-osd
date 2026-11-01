#include "LosslessScaling.h"

#include "AppConfig.h"
#include "ProcessUtil.h"
#include "WinUtil.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace {
constexpr int kMinScaleWidth = 320;
constexpr int kMinScaleHeight = 180;
// 窗口尺寸连续稳定多久算「游戏已经在出帧」
constexpr qint64 kStableMs = 2000;
// 热键最多尝试次数（奇数，保证最后停在「开」而不是「关」）
constexpr int kMaxHotkeyAttempts = 3;
}

LosslessScaling::LosslessScaling(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &LosslessScaling::step);
}

bool LosslessScaling::isLsRunning() const
{
    return ProcessUtil::isRunning(QStringLiteral("LosslessScaling"));
}

bool LosslessScaling::fixCaptureApi()
{
    AppConfig &cfg = AppConfig::instance();
    const QString path = cfg.lsSettingsPath();
    if (!QFileInfo::exists(path)) {
        emit log(tr("未找到 LS 配置：%1").arg(path));
        return false;
    }
    if (isLsRunning()) {
        emit log(tr("Lossless Scaling 正在运行，跳过 CaptureApi 校正"));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit log(tr("读取 LS 配置失败：%1").arg(file.errorString()));
        return false;
    }
    QString xml = QString::fromUtf8(file.readAll());
    file.close();

    const QString original = xml;
    QRegularExpression captureRe(QStringLiteral("<CaptureApi>\\s*(DXGI|Auto|DwmSharedSurface|DisplayCapture)\\s*</CaptureApi>"),
                                 QRegularExpression::CaseInsensitiveOption);
    xml.replace(captureRe, QStringLiteral("<CaptureApi>WGC</CaptureApi>"));

    QRegularExpression adminRe(QStringLiteral("<StartAsAdmin>\\s*true\\s*</StartAsAdmin>"),
                               QRegularExpression::CaseInsensitiveOption);
    xml.replace(adminRe, QStringLiteral("<StartAsAdmin>false</StartAsAdmin>"));

    if (xml == original) {
        emit log(tr("LS 抓取方式已是 WGC"));
        return true;
    }

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        emit log(tr("写入 LS 配置失败：%1").arg(out.errorString()));
        return false;
    }
    out.write(xml.toUtf8());
    out.close();
    emit log(tr("已将 LS 抓取方式校正为 WGC"));
    return true;
}

bool LosslessScaling::startLs(bool fixFirst)
{
    AppConfig &cfg = AppConfig::instance();
    if (isLsRunning()) {
        emit log(tr("Lossless Scaling 已在运行"));
        return true;
    }

    const QString exe = cfg.lsExe();
    if (!QFileInfo::exists(exe)) {
        emit log(tr("找不到 LosslessScaling.exe：%1").arg(exe));
        return false;
    }

    if (fixFirst && cfg.forceWgcCapture())
        fixCaptureApi();

    const bool ok = QProcess::startDetached(exe, QStringList(), QFileInfo(exe).absolutePath());
    if (!ok) {
        emit log(tr("启动 Lossless Scaling 失败"));
        return false;
    }
    emit log(tr("已拉起 Lossless Scaling"));
    return true;
}

bool LosslessScaling::stopLs()
{
    if (!isLsRunning())
        return true;
    const bool ok = ProcessUtil::killByName(QStringLiteral("LosslessScaling"));
    emit log(ok ? tr("已关闭 Lossless Scaling") : tr("关闭 Lossless Scaling 失败"));
    return ok;
}

int LosslessScaling::scaleWindowCount() const
{
    int count = 0;
    for (const WinUtil::WindowInfo &w : WinUtil::topLevelWindows()) {
        if (!w.visible)
            continue;
        if (!w.className.startsWith(QLatin1String("LosslessScaling")))
            continue;
        if (!w.title.isEmpty())
            continue;
        if (w.rect.width() < kMinScaleWidth || w.rect.height() < kMinScaleHeight)
            continue;
        ++count;
    }
    return count;
}

void LosslessScaling::requestEnableFrameGen(quintptr gameWindow, int waitFpsMs)
{
    m_gameWindow = gameWindow;
    m_deadline = QDateTime::currentMSecsSinceEpoch() + waitFpsMs;
    m_stableSince = 0;
    m_stableRect = QRect();
    m_verifyDeadline = 0;
    m_hotkeyAttempts = 0;
    m_phase = 0;
    m_running = true;
    emit log(tr("开始插帧流程…"));
    m_timer->start(200);
}

void LosslessScaling::requestDisableFrameGen()
{
    m_running = false;
    m_timer->stop();
    if (!frameGenActive()) {
        emit log(tr("插帧当前未开启"));
        return;
    }
    WinUtil::bringToFront(m_gameWindow);
    QTimer::singleShot(300, this, [this] {
        WinUtil::sendHotkey(AppConfig::instance().lsHotkey());
        emit log(tr("已发送关闭插帧热键"));
    });
    QTimer::singleShot(1800, this, [this] {
        const bool active = frameGenActive();
        emit frameGenChanged(active);
        emit sequenceFinished(!active);
    });
}

void LosslessScaling::cancelSequence()
{
    m_running = false;
    m_timer->stop();
    emit log(tr("插帧流程已取消"));
}

void LosslessScaling::finish(bool success)
{
    m_running = false;
    m_timer->stop();
    emit frameGenChanged(frameGenActive());
    emit sequenceFinished(success);
}

void LosslessScaling::step()
{
    if (!m_running)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 阶段 0：确保 LS 已启动
    if (m_phase == 0) {
        if (!isLsRunning()) {
            if (!startLs())
                startLs(false); // 再试一次，不碰配置
            m_timer->start(1500);
            return;
        }
        m_phase = 1;
    }

    // 阶段 1：已经在插帧
    if (frameGenActive()) {
        emit log(tr("检测到插帧已开启（%1 个输出窗口）").arg(scaleWindowCount()));
        emit frameGenChanged(true);
        finish(true);
        return;
    }

    if (now > m_deadline) {
        emit log(tr("等待游戏出帧超时，插帧未开启"));
        finish(false);
        return;
    }

    // 阶段 2：等待游戏窗口稳定出帧
    if (!gameWindowReady(now)) {
        m_timer->start(600);
        return;
    }

    // 阶段 3：抢前台 + 发热键
    if (m_phase < 3) {
        WinUtil::bringToFront(m_gameWindow);
        m_phase = 3;
        m_timer->start(400);
        return;
    }

    if (m_phase == 3) {
        WinUtil::sendHotkey(AppConfig::instance().lsHotkey());
        ++m_hotkeyAttempts;
        emit log(tr("已发送插帧热键 %1（第 %2 次）")
                     .arg(AppConfig::instance().lsHotkey())
                     .arg(m_hotkeyAttempts));
        m_verifyDeadline = now + 15000;
        m_phase = 4;
        m_timer->start(700);
        return;
    }

    // 阶段 4：验证
    if (frameGenActive()) {
        emit log(tr("插帧已生效（%1 个输出窗口）").arg(scaleWindowCount()));
        finish(true);
        return;
    }
    if (now > m_verifyDeadline) {
        if (m_hotkeyAttempts < kMaxHotkeyAttempts) {
            emit log(tr("尚未检测到插帧输出，重新切回游戏后再试"));
            m_phase = 3;
            m_timer->start(600);
            return;
        }
        emit log(tr("热键已发送但未检测到 LS 输出窗口"));
        finish(false);
        return;
    }
    m_timer->start(500);
}

bool LosslessScaling::gameWindowReady(qint64 now)
{
    if (m_gameWindow == 0 || !WinUtil::isWindowVisible(m_gameWindow)) {
        m_stableRect = QRect();
        m_stableSince = 0;
        return false;
    }

    const QRect rect = WinUtil::windowRect(m_gameWindow);
    if (rect.width() < kMinScaleWidth || rect.height() < kMinScaleHeight) {
        m_stableRect = QRect();
        m_stableSince = 0;
        return false;
    }

    // 少数模拟器会把 FPS 写进标题，有就直接采信
    if (WinUtil::parseFpsFromTitle(WinUtil::windowTitle(m_gameWindow)) > 0)
        return true;

    // Eden 只在状态栏 QLabel 里显示 FPS，标题里没有：
    // 改用「游戏窗口尺寸连续稳定」判定画面已经起来，避免死等标题里的 FPS。
    if (rect != m_stableRect) {
        m_stableRect = rect;
        m_stableSince = now;
        return false;
    }
    if (m_stableSince == 0)
        m_stableSince = now;
    if (now - m_stableSince < kStableMs)
        return false;

    emit log(tr("游戏窗口已稳定（%1×%2），准备开启插帧")
                 .arg(rect.width())
                 .arg(rect.height()));
    return true;
}
