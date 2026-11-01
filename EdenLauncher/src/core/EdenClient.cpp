#include "EdenClient.h"

#include "AppConfig.h"
#include "ProcessUtil.h"
#include "WinUtil.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace {

// Eden 遇到未实现的 IPC 命令会 assert 并让 guest 触发 PANIC，
// 随后 Eden 打印 backtrace 时原生崩溃（退出代码 -1073741819 = 0xC0000005）。
// 打开 use_auto_stub 后，未实现命令直接返回 ResultSuccess，游戏可以继续运行。
bool ensureAutoStub(const QString &iniPath)
{
    if (iniPath.isEmpty() || !QFileInfo::exists(iniPath))
        return false;

    QFile file(iniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    QString patched = text;
    // Eden 的 ini 用 "use_auto_stub\default=true" 表示「用默认值」，
    // 它会直接忽略下面的数值行，所以两个键必须一起改成「非默认 + 打开」。
    patched.replace(QStringLiteral("use_auto_stub\\default=true"),
                    QStringLiteral("use_auto_stub\\default=false"));
    patched.replace(QStringLiteral("use_auto_stub=false"), QStringLiteral("use_auto_stub=true"));

    if (!patched.contains(QStringLiteral("use_auto_stub\\default="))) {
        static const QRegularExpression valueRe(QStringLiteral("(?m)^use_auto_stub="));
        const QRegularExpressionMatch match = valueRe.match(patched);
        if (match.hasMatch())
            patched.insert(match.capturedStart(), QStringLiteral("use_auto_stub\\default=false\n"));
    }

    if (patched == text)
        return false; // 已经是想要的状态，不动用户的配置

    QFile out(iniPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    out.write(patched.toUtf8());
    out.close();
    return true;
}

} // namespace

EdenClient::EdenClient(QObject *parent)
    : QObject(parent)
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray data = m_process->readAllStandardOutput();
        const QString text = QString::fromUtf8(data);
        for (const QString &line : text.split('\n', Qt::SkipEmptyParts))
            emit logLine(line.trimmed());
    });

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onProcessFinished(code); });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            m_lastError = tr("无法启动 eden-cli.exe，请检查运行时目录是否正确。");
            emit errorOccurred(m_lastError);
            m_pid = 0;
            m_hwnd = 0;
            m_fps = -1;
            emit stateChanged(false);
        }
    });

    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &EdenClient::poll);
}

EdenClient::~EdenClient()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1500);
    }
}

bool EdenClient::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void EdenClient::launch()
{
    if (isRunning())
        return;

    AppConfig &cfg = AppConfig::instance();
    const QString exe = cfg.edenCliExe();

    if (!QFileInfo::exists(exe)) {
        m_lastError = tr("找不到 %1").arg(exe);
        emit errorOccurred(m_lastError);
        return;
    }
    if (cfg.gameMain().isEmpty() || !QFileInfo::exists(cfg.gameMain())) {
        m_lastError = tr("找不到游戏主文件：%1").arg(cfg.gameMain());
        emit errorOccurred(m_lastError);
        return;
    }

    QStringList args;
    if (!cfg.gameConfigIni().isEmpty()) {
        if (ensureAutoStub(cfg.gameConfigIni()))
            emit logLine(tr("已为 %1 开启 auto-stub（避免未实现 IPC 导致崩溃）").arg(cfg.gameName()));
        args << QStringLiteral("-c") << cfg.gameConfigIni();
    }
    args << QStringLiteral("-g") << cfg.gameMain();
    args << cfg.extraArgs();

    m_process->setWorkingDirectory(cfg.runtimeDir());
    m_process->setProgram(exe);
    m_process->setArguments(args);
    m_process->start();

    if (!m_process->waitForStarted(8000)) {
        m_lastError = tr("启动超时：%1").arg(m_process->errorString());
        emit errorOccurred(m_lastError);
        return;
    }

    m_pid = static_cast<quint32>(m_process->processId());
    m_hwnd = 0;
    m_fps = -1;
    m_lastError.clear();

    emit logLine(tr("启动 %1 (pid %2)").arg(exe).arg(m_pid));
    emit logLine(tr("参数: %1").arg(args.join(' ')));
    emit stateChanged(true);

    m_timer->start();
}

void EdenClient::stop()
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;

    emit logLine(tr("正在关闭模拟器…"));
    m_process->terminate();
    if (!m_process->waitForFinished(4000)) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

void EdenClient::onProcessFinished(int exitCode)
{
    m_timer->stop();
    emit logLine(tr("模拟器已退出（代码 %1）").arg(exitCode));
    m_pid = 0;
    m_hwnd = 0;
    m_fps = -1;
    emit stateChanged(false);
}

void EdenClient::refresh()
{
    poll();
}

void EdenClient::resolveWindow()
{
    if (m_pid == 0)
        return;

    if (m_hwnd && WinUtil::isWindowVisible(m_hwnd) && !WinUtil::windowTitle(m_hwnd).isEmpty())
        return;

    m_hwnd = WinUtil::mainWindowOfProcess(m_pid);
    if (m_hwnd) {
        emit logLine(tr("已找到游戏窗口：%1").arg(WinUtil::windowTitle(m_hwnd)));
        emit windowFound(m_hwnd);
    }
}

void EdenClient::poll()
{
    if (!isRunning()) {
        if (m_pid != 0) {
            m_pid = 0;
            m_hwnd = 0;
            m_fps = -1;
            emit stateChanged(false);
        }
        return;
    }

    resolveWindow();

    if (m_hwnd) {
        const int fps = WinUtil::parseFpsFromTitle(WinUtil::windowTitle(m_hwnd));
        if (fps != m_fps) {
            m_fps = fps;
            emit fpsChanged(fps);
        }
        // 窗口句柄失效（切全屏等）时重新抓
        if (!WinUtil::windowTitle(m_hwnd).contains(QLatin1String("Eden"))
            && !WinUtil::windowTitle(m_hwnd).contains(QLatin1String("FPS"))) {
            m_hwnd = 0;
        }
    }
}
