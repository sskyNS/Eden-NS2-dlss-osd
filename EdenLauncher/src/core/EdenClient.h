#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

// 管理 eden-cli.exe 的启动、停止与状态轮询（FPS / 游戏窗口句柄）
class EdenClient : public QObject
{
    Q_OBJECT

public:
    explicit EdenClient(QObject *parent = nullptr);
    ~EdenClient() override;

    bool isRunning() const;
    quint32 pid() const { return m_pid; }
    quintptr gameWindow() const { return m_hwnd; }
    int currentFps() const { return m_fps; }

    void launch();
    void stop();
    void refresh();

    QString lastError() const { return m_lastError; }

signals:
    void stateChanged(bool running);
    void fpsChanged(int fps);
    void windowFound(quintptr hwnd);
    void logLine(const QString &line);
    void errorOccurred(const QString &message);

private:
    void poll();
    void onProcessFinished(int exitCode);
    void resolveWindow();

    QProcess *m_process = nullptr;
    QTimer *m_timer = nullptr;
    quint32 m_pid = 0;
    quintptr m_hwnd = 0;
    int m_fps = -1;
    QString m_lastError;
};
