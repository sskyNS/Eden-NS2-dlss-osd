#pragma once

#include <QRect>
#include <QString>
#include <QVector>

class QWidget;

namespace WinUtil {

struct WindowInfo
{
    quintptr hwnd = 0;
    QString title;
    QString className;
    QRect rect;
    bool visible = false;
};

QVector<WindowInfo> topLevelWindows();
QVector<WindowInfo> windowsOfProcess(quint32 pid);

// 选取该进程最可能的「游戏主窗口」：可见、有标题、面积最大
quintptr mainWindowOfProcess(quint32 pid);

QString windowTitle(quintptr hwnd);
QString windowClassName(quintptr hwnd);
QRect windowRect(quintptr hwnd);
bool isWindowVisible(quintptr hwnd);

bool setWindowRect(quintptr hwnd, const QRect &rect, bool activate = true);
bool centerWindow(quintptr hwnd);
bool maximizeWindow(quintptr hwnd);
bool toggleBorderless(quintptr hwnd);
// 从窗口标题中提取 "FPS: 60" 里的数字，失败返回 -1
int parseFpsFromTitle(const QString &title);

void bringToFront(quintptr hwnd);

// 全局发送热键，如 "Ctrl+Alt+S"
bool sendHotkey(const QString &combination);

QRect virtualScreenRect();

// 高 DPI 下坐标换算用
qreal devicePixelRatioForWindow(quintptr hwnd);

} // namespace WinUtil
