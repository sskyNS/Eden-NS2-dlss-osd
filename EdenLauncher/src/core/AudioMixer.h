#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

struct AudioSessionInfo
{
    QString processName;
    quint32 pid = 0;
    float volume = 1.0f;
    bool muted = false;
    bool isSystem = false;
};

// Windows Core Audio：按进程调节音量、静音、以及「只保留某个应用出声」
class AudioMixer : public QObject
{
    Q_OBJECT

public:
    explicit AudioMixer(QObject *parent = nullptr);
    ~AudioMixer() override;

    QList<AudioSessionInfo> sessions();
    bool setVolume(quint32 pid, float volume);
    bool setMuted(quint32 pid, bool muted);

    // 把除 keepPid 以外的所有会话静音；返回被静音的会话数
    int isolate(quint32 keepPid);
    bool restoreAll();
    bool hasSnapshot() const { return !m_snapshot.isEmpty(); }

    QString lastError() const { return m_lastError; }

signals:
    void changed();

private:
    struct SavedState
    {
        float volume = 1.0f;
        bool muted = false;
        bool existed = false;
    };

    QHash<quint32, SavedState> m_snapshot;
    QString m_lastError;
    bool m_comInitialized = false;
};
