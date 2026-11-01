#include "AudioMixer.h"

#include "ProcessUtil.h"

#include <QHash>

#ifdef Q_OS_WIN
#include <windows.h>
// MinGW 不提供 CLSID_MMDeviceEnumerator / IID_IMMDeviceEnumerator 的定义，本地生成
#define INITGUID
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>
#endif

#ifdef Q_OS_WIN

namespace {

template <typename T>
void releasePtr(T *&p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

} // namespace

#endif

AudioMixer::AudioMixer(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_comInitialized = SUCCEEDED(hr);
#endif
}

AudioMixer::~AudioMixer()
{
#ifdef Q_OS_WIN
    if (m_comInitialized)
        CoUninitialize();
#endif
}

QList<AudioSessionInfo> AudioMixer::sessions()
{
    QList<AudioSessionInfo> result;
#ifdef Q_OS_WIN
    IMMDeviceEnumerator *enumerator = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                  IID_IMMDeviceEnumerator,
                                  reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        m_lastError = QStringLiteral("无法访问音频设备 (0x%1)").arg(hr, 0, 16);
        return result;
    }

    IMMDevice *device = nullptr;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    if (FAILED(hr) || !device) {
        releasePtr(enumerator);
        m_lastError = QStringLiteral("没有可用的输出设备");
        return result;
    }

    IAudioSessionManager2 *manager = nullptr;
    hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void **>(&manager));
    if (FAILED(hr) || !manager) {
        releasePtr(device);
        releasePtr(enumerator);
        m_lastError = QStringLiteral("无法创建音频会话管理器");
        return result;
    }

    IAudioSessionEnumerator *sessionEnum = nullptr;
    hr = manager->GetSessionEnumerator(&sessionEnum);
    if (SUCCEEDED(hr) && sessionEnum) {
        int count = 0;
        sessionEnum->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl *control = nullptr;
            if (FAILED(sessionEnum->GetSession(i, &control)) || !control)
                continue;

            IAudioSessionControl2 *control2 = nullptr;
            if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                               reinterpret_cast<void **>(&control2)))) {
                releasePtr(control);
                continue;
            }

            DWORD pid = 0;
            control2->GetProcessId(&pid);

            ISimpleAudioVolume *volume = nullptr;
            if (FAILED(control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                               reinterpret_cast<void **>(&volume)))) {
                releasePtr(control2);
                releasePtr(control);
                continue;
            }

            AudioSessionInfo info;
            info.pid = pid;
            info.isSystem = (pid == 0);
            info.processName = info.isSystem ? QStringLiteral("系统声音")
                                             : ProcessUtil::processName(pid);
            if (info.processName.isEmpty())
                info.processName = QStringLiteral("未知进程");

            float v = 1.0f;
            BOOL muted = FALSE;
            if (SUCCEEDED(volume->GetMasterVolume(&v)))
                info.volume = v;
            if (SUCCEEDED(volume->GetMute(&muted)))
                info.muted = muted != FALSE;

            result.append(info);

            releasePtr(volume);
            releasePtr(control2);
            releasePtr(control);
        }
    }

    releasePtr(sessionEnum);
    releasePtr(manager);
    releasePtr(device);
    releasePtr(enumerator);
#else
    Q_UNUSED(result);
#endif
    return result;
}

bool AudioMixer::setVolume(quint32 pid, float volume)
{
#ifdef Q_OS_WIN
    const float clamped = qBound(0.0f, volume, 1.0f);
    bool touched = false;

    IMMDeviceEnumerator *enumerator = nullptr;
    if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                IID_IMMDeviceEnumerator,
                                reinterpret_cast<void **>(&enumerator))))
        return false;

    IMMDevice *device = nullptr;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
        releasePtr(enumerator);
        return false;
    }

    IAudioSessionManager2 *manager = nullptr;
    if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void **>(&manager)))) {
        releasePtr(device);
        releasePtr(enumerator);
        return false;
    }

    IAudioSessionEnumerator *sessionEnum = nullptr;
    if (SUCCEEDED(manager->GetSessionEnumerator(&sessionEnum)) && sessionEnum) {
        int count = 0;
        sessionEnum->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl *control = nullptr;
            if (FAILED(sessionEnum->GetSession(i, &control)) || !control)
                continue;

            IAudioSessionControl2 *control2 = nullptr;
            if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                               reinterpret_cast<void **>(&control2)))) {
                releasePtr(control);
                continue;
            }

            DWORD sessionPid = 0;
            control2->GetProcessId(&sessionPid);
            if (sessionPid == pid) {
                ISimpleAudioVolume *volumePtr = nullptr;
                if (SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                      reinterpret_cast<void **>(&volumePtr)))) {
                    if (SUCCEEDED(volumePtr->SetMasterVolume(clamped, nullptr)))
                        touched = true;
                    releasePtr(volumePtr);
                }
            }
            releasePtr(control2);
            releasePtr(control);
        }
    }

    releasePtr(sessionEnum);
    releasePtr(manager);
    releasePtr(device);
    releasePtr(enumerator);
    return touched;
#else
    Q_UNUSED(pid);
    Q_UNUSED(volume);
    return false;
#endif
}

bool AudioMixer::setMuted(quint32 pid, bool muted)
{
#ifdef Q_OS_WIN
    bool touched = false;
    IMMDeviceEnumerator *enumerator = nullptr;
    if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                IID_IMMDeviceEnumerator,
                                reinterpret_cast<void **>(&enumerator))))
        return false;

    IMMDevice *device = nullptr;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
        releasePtr(enumerator);
        return false;
    }

    IAudioSessionManager2 *manager = nullptr;
    if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void **>(&manager)))) {
        releasePtr(device);
        releasePtr(enumerator);
        return false;
    }

    IAudioSessionEnumerator *sessionEnum = nullptr;
    if (SUCCEEDED(manager->GetSessionEnumerator(&sessionEnum)) && sessionEnum) {
        int count = 0;
        sessionEnum->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl *control = nullptr;
            if (FAILED(sessionEnum->GetSession(i, &control)) || !control)
                continue;

            IAudioSessionControl2 *control2 = nullptr;
            if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                               reinterpret_cast<void **>(&control2)))) {
                releasePtr(control);
                continue;
            }

            DWORD sessionPid = 0;
            control2->GetProcessId(&sessionPid);
            if (sessionPid == pid) {
                ISimpleAudioVolume *volumePtr = nullptr;
                if (SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                      reinterpret_cast<void **>(&volumePtr)))) {
                    if (SUCCEEDED(volumePtr->SetMute(muted ? TRUE : FALSE, nullptr)))
                        touched = true;
                    releasePtr(volumePtr);
                }
            }
            releasePtr(control2);
            releasePtr(control);
        }
    }

    releasePtr(sessionEnum);
    releasePtr(manager);
    releasePtr(device);
    releasePtr(enumerator);
    return touched;
#else
    Q_UNUSED(pid);
    Q_UNUSED(muted);
    return false;
#endif
}

int AudioMixer::isolate(quint32 keepPid)
{
    const QList<AudioSessionInfo> list = sessions();
    m_snapshot.clear();
    int affected = 0;
    for (const AudioSessionInfo &s : list) {
        if (s.pid == keepPid)
            continue;
        SavedState saved;
        saved.volume = s.volume;
        saved.muted = s.muted;
        saved.existed = true;
        m_snapshot.insert(s.pid, saved);
        if (setMuted(s.pid, true))
            ++affected;
    }
    emit changed();
    return affected;
}

bool AudioMixer::restoreAll()
{
    bool ok = true;
    for (auto it = m_snapshot.cbegin(); it != m_snapshot.cend(); ++it) {
        ok = setMuted(it.key(), it.value().muted) && ok;
        ok = setVolume(it.key(), it.value().volume) && ok;
    }
    m_snapshot.clear();
    emit changed();
    return ok;
}
