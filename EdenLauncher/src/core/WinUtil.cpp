#include "WinUtil.h"

#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace WinUtil {

#ifdef Q_OS_WIN

namespace {

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lParam)
{
    auto *list = reinterpret_cast<QVector<WindowInfo> *>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    WindowInfo info;
    info.hwnd = reinterpret_cast<quintptr>(hwnd);
    info.visible = IsWindowVisible(hwnd) != FALSE;

    wchar_t titleBuf[512] = {};
    GetWindowTextW(hwnd, titleBuf, 512);
    info.title = QString::fromWCharArray(titleBuf);

    wchar_t classBuf[256] = {};
    GetClassNameW(hwnd, classBuf, 256);
    info.className = QString::fromWCharArray(classBuf);

    RECT r{};
    if (GetWindowRect(hwnd, &r))
        info.rect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);

    list->append(info);
    return TRUE;
}

struct PidFilter
{
    DWORD pid;
    QVector<WindowInfo> *out;
};

BOOL CALLBACK enumProcForPid(HWND hwnd, LPARAM lParam)
{
    auto *f = reinterpret_cast<PidFilter *>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != f->pid)
        return TRUE;

    WindowInfo info;
    info.hwnd = reinterpret_cast<quintptr>(hwnd);
    info.visible = IsWindowVisible(hwnd) != FALSE;

    wchar_t titleBuf[512] = {};
    GetWindowTextW(hwnd, titleBuf, 512);
    info.title = QString::fromWCharArray(titleBuf);

    wchar_t classBuf[256] = {};
    GetClassNameW(hwnd, classBuf, 256);
    info.className = QString::fromWCharArray(classBuf);

    RECT r{};
    if (GetWindowRect(hwnd, &r))
        info.rect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);

    f->out->append(info);
    return TRUE;
}

WORD vkForToken(const QString &token, bool &ok)
{
    ok = true;
    const QString t = token.trimmed().toUpper();
    if (t == "CTRL" || t == "CONTROL") return VK_CONTROL;
    if (t == "ALT")                   return VK_MENU;
    if (t == "SHIFT")                 return VK_SHIFT;
    if (t == "WIN" || t == "META")    return VK_LWIN;
    if (t == "SPACE")                 return VK_SPACE;
    if (t == "TAB")                   return VK_TAB;
    if (t == "ENTER" || t == "RETURN")return VK_RETURN;
    if (t == "ESC" || t == "ESCAPE")  return VK_ESCAPE;
    if (t == "PLUS" || t == "+")      return VK_OEM_PLUS;
    if (t == "MINUS" || t == "-")     return VK_OEM_MINUS;

    if (t.startsWith('F') && t.size() >= 2) {
        bool numOk = false;
        const int n = t.mid(1).toInt(&numOk);
        if (numOk && n >= 1 && n <= 24)
            return static_cast<WORD>(VK_F1 + n - 1);
    }
    if (t.size() == 1) {
        const QChar c = t.at(0);
        if (c.isLetterOrNumber())
            return static_cast<WORD>(c.unicode());
    }
    ok = false;
    return 0;
}

} // namespace

QVector<WindowInfo> topLevelWindows()
{
    QVector<WindowInfo> list;
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(&list));
    return list;
}

QVector<WindowInfo> windowsOfProcess(quint32 pid)
{
    QVector<WindowInfo> list;
    PidFilter filter{static_cast<DWORD>(pid), &list};
    EnumWindows(enumProcForPid, reinterpret_cast<LPARAM>(&filter));
    return list;
}

quintptr mainWindowOfProcess(quint32 pid)
{
    quintptr best = 0;
    qint64 bestArea = 0;
    for (const WindowInfo &w : windowsOfProcess(pid)) {
        if (!w.visible || w.title.isEmpty())
            continue;
        const qint64 area = qint64(w.rect.width()) * w.rect.height();
        if (area > bestArea) {
            bestArea = area;
            best = w.hwnd;
        }
    }
    return best;
}

QString windowTitle(quintptr hwnd)
{
    wchar_t buf[1024] = {};
    GetWindowTextW(reinterpret_cast<HWND>(hwnd), buf, 1024);
    return QString::fromWCharArray(buf);
}

QString windowClassName(quintptr hwnd)
{
    wchar_t buf[256] = {};
    GetClassNameW(reinterpret_cast<HWND>(hwnd), buf, 256);
    return QString::fromWCharArray(buf);
}

QRect windowRect(quintptr hwnd)
{
    RECT r{};
    if (!GetWindowRect(reinterpret_cast<HWND>(hwnd), &r))
        return {};
    return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

bool isWindowVisible(quintptr hwnd)
{
    return IsWindowVisible(reinterpret_cast<HWND>(hwnd)) != FALSE;
}

bool setWindowRect(quintptr hwnd, const QRect &rect, bool activate)
{
    HWND h = reinterpret_cast<HWND>(hwnd);
    if (!IsWindow(h))
        return false;

    // 去掉最大化状态，否则 SetWindowPos 不生效
    if (IsZoomed(h))
        ShowWindow(h, SW_RESTORE);

    const BOOL ok = SetWindowPos(h, nullptr, rect.x(), rect.y(), rect.width(), rect.height(),
                                 SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    if (ok && activate)
        SetForegroundWindow(h);
    return ok != FALSE;
}

bool centerWindow(quintptr hwnd)
{
    const QRect cur = windowRect(hwnd);
    const QRect screen = virtualScreenRect();
    const QRect target(screen.center().x() - cur.width() / 2,
                       screen.center().y() - cur.height() / 2,
                       cur.width(), cur.height());
    return setWindowRect(hwnd, target);
}

bool maximizeWindow(quintptr hwnd)
{
    HWND h = reinterpret_cast<HWND>(hwnd);
    if (!IsWindow(h))
        return false;
    ShowWindow(h, SW_MAXIMIZE);
    return true;
}

bool toggleBorderless(quintptr hwnd)
{
    HWND h = reinterpret_cast<HWND>(hwnd);
    if (!IsWindow(h))
        return false;

    LONG style = GetWindowLongW(h, GWL_STYLE);
    const bool borderless = (style & WS_CAPTION) == 0;
    if (borderless) {
        style |= WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        SetWindowLongW(h, GWL_STYLE, style);
        SetWindowPos(h, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    } else {
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        SetWindowLongW(h, GWL_STYLE, style);
        SetWindowPos(h, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    return true;
}

int parseFpsFromTitle(const QString &title)
{
    static const QRegularExpression re(QStringLiteral("FPS[:\\s]+(\\d+)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(title);
    if (m.hasMatch())
        return m.captured(1).toInt();
    return -1;
}

void bringToFront(quintptr hwnd)
{
    HWND h = reinterpret_cast<HWND>(hwnd);
    if (!IsWindow(h))
        return;

    // 空敲一下 Alt，解除前台锁定
    INPUT alt{};
    alt.type = INPUT_KEYBOARD;
    alt.ki.wVk = VK_MENU;
    alt.ki.dwFlags = 0;
    SendInput(1, &alt, sizeof(INPUT));
    alt.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &alt, sizeof(INPUT));

    const DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD myThread = GetCurrentThreadId();
    const bool attached = fgThread && fgThread != myThread
                          && AttachThreadInput(myThread, fgThread, TRUE);

    if (IsIconic(h))
        ShowWindow(h, SW_RESTORE);
    ShowWindow(h, SW_SHOW);

    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    BringWindowToTop(h);
    SetForegroundWindow(h);
    SetFocus(h);
    SwitchToThisWindow(h, TRUE);

    if (attached)
        AttachThreadInput(myThread, fgThread, FALSE);
}

bool sendHotkey(const QString &combination)
{
    const QStringList tokens = combination.split('+', Qt::SkipEmptyParts);
    if (tokens.isEmpty())
        return false;

    QVector<WORD> keys;
    for (const QString &token : tokens) {
        bool ok = false;
        const WORD vk = vkForToken(token, ok);
        if (!ok)
            return false;
        keys.append(vk);
    }

    QVector<INPUT> inputs;
    for (WORD vk : keys) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.dwFlags = 0;
        inputs.append(in);
    }
    for (int i = keys.size() - 1; i >= 0; --i) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = keys.at(i);
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.append(in);
    }

    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    return sent == static_cast<UINT>(inputs.size());
}

QRect virtualScreenRect()
{
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return QRect(vx, vy, vw, vh);
}

qreal devicePixelRatioForWindow(quintptr hwnd)
{
    HWND h = reinterpret_cast<HWND>(hwnd);
    if (!h)
        return 1.0;
    const UINT dpi = GetDpiForWindow(h);
    if (dpi == 0)
        return 1.0;
    return dpi / 96.0;
}

#else

QVector<WindowInfo> topLevelWindows() { return {}; }
QVector<WindowInfo> windowsOfProcess(quint32) { return {}; }
quintptr mainWindowOfProcess(quint32) { return 0; }
QString windowTitle(quintptr) { return {}; }
QString windowClassName(quintptr) { return {}; }
QRect windowRect(quintptr) { return {}; }
bool isWindowVisible(quintptr) { return false; }
bool setWindowRect(quintptr, const QRect &, bool) { return false; }
bool centerWindow(quintptr) { return false; }
bool maximizeWindow(quintptr) { return false; }
bool toggleBorderless(quintptr) { return false; }
int parseFpsFromTitle(const QString &) { return -1; }
void bringToFront(quintptr) {}
bool sendHotkey(const QString &) { return false; }
QRect virtualScreenRect() { return {}; }
qreal devicePixelRatioForWindow(quintptr) { return 1.0; }

#endif

} // namespace WinUtil
