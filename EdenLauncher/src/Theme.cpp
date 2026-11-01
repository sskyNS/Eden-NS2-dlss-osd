#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

namespace Theme {

QColor accent()      { return QColor(0x4F, 0x7B, 0xFF); }
QColor accentSoft()  { return QColor(0x4F, 0x7B, 0xFF, 0x33); }
QColor success()     { return QColor(0x3D, 0xD6, 0x8C); }
QColor warning()     { return QColor(0xF5, 0xA6, 0x23); }
QColor danger()      { return QColor(0xFF, 0x6B, 0x81); }
QColor textPrimary() { return QColor(0xE6, 0xE8, 0xEE); }
QColor textMuted()   { return QColor(0x6B, 0x74, 0x88); }
QColor surface()     { return QColor(0x14, 0x17, 0x1F); }
QColor background()  { return QColor(0x0F, 0x11, 0x16); }
QColor border()      { return QColor(0x1E, 0x23, 0x30); }

void apply(QApplication &app)
{
    QPalette p;
    p.setColor(QPalette::Window, background());
    p.setColor(QPalette::WindowText, textPrimary());
    p.setColor(QPalette::Base, QColor(0x0C, 0x0E, 0x13));
    p.setColor(QPalette::AlternateBase, QColor(0x14, 0x17, 0x1F));
    p.setColor(QPalette::Text, textPrimary());
    p.setColor(QPalette::Button, QColor(0x1B, 0x20, 0x30));
    p.setColor(QPalette::ButtonText, textPrimary());
    p.setColor(QPalette::Highlight, accent());
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(0x55, 0x5D, 0x70));
    p.setColor(QPalette::ToolTipBase, QColor(0x1B, 0x20, 0x30));
    p.setColor(QPalette::ToolTipText, textPrimary());
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x4C, 0x54, 0x68));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x4C, 0x54, 0x68));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x4C, 0x54, 0x68));
    app.setPalette(p);

    QFont f(QStringLiteral("Microsoft YaHei UI"));
    f.setPointSizeF(9.5);
    f.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(f);

    app.setStyleSheet(styleSheet());
}

QString styleSheet()
{
    return QStringLiteral(R"QSS(
* {
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
    outline: none;
}
QMainWindow, QDialog, #contentRoot {
    background: #0F1116;
}
QToolTip {
    background: #1B2030;
    color: #E6E8EE;
    border: 1px solid #2B3247;
    border-radius: 6px;
    padding: 6px 8px;
}

/* ── 侧边栏 ───────────────────────────────────────── */
#sidebar {
    background: #0A0C11;
    border-right: 1px solid #191D27;
}
#logoMark {
    background: #4F7BFF;
    border-radius: 10px;
    color: #FFFFFF;
    font-size: 17px;
    font-weight: 700;
}
#logoText {
    font-size: 16px;
    font-weight: 700;
    letter-spacing: 0.5px;
    color: #FFFFFF;
}
#logoSub {
    font-size: 10px;
    color: #59627A;
    letter-spacing: 1.2px;
}
#navSection {
    font-size: 10px;
    color: #4B5468;
    letter-spacing: 1.4px;
    padding: 0 4px 4px 4px;
}
QPushButton#navItem {
    text-align: left;
    padding: 9px 12px;
    border: none;
    border-left: 3px solid transparent;
    border-radius: 8px;
    color: #98A2B8;
    background: transparent;
    font-size: 13.5px;
}
QPushButton#navItem:hover {
    background: #141824;
    color: #DCE2F0;
}
QPushButton#navItem:checked {
    background: #171C2B;
    border-left: 3px solid #4F7BFF;
    color: #FFFFFF;
    font-weight: 600;
}
#sidebarFooter {
    color: #454D60;
    font-size: 10.5px;
}
#statusDot {
    border-radius: 5px;
    background: #3DD68C;
}

/* ── 卡片 ─────────────────────────────────────────── */
QFrame[card="true"] {
    background: #14171F;
    border: 1px solid #1E2330;
    border-radius: 14px;
}
QFrame[card="true"][highlight="true"] {
    border: 1px solid #2C3A63;
    background: #151A28;
}
QFrame[stat="true"] {
    background: #171B25;
    border: 1px solid #202634;
    border-radius: 11px;
}
QFrame[sep="true"] {
    background: #1B1F2A;
    max-height: 1px;
    min-height: 1px;
    border: none;
}

/* ── 文本 ─────────────────────────────────────────── */
QLabel { background: transparent; color: #E6E8EE; }
QLabel[title="true"] {
    font-size: 15px;
    font-weight: 600;
    color: #FFFFFF;
}
QLabel[h1="true"] {
    font-size: 21px;
    font-weight: 700;
    color: #FFFFFF;
}
QLabel[muted="true"] {
    color: #6B7488;
    font-size: 12px;
}
QLabel[statValue="true"] {
    font-size: 20px;
    font-weight: 700;
    color: #FFFFFF;
}
QLabel[statLabel="true"] {
    font-size: 10.5px;
    color: #667089;
    letter-spacing: 0.6px;
}
QLabel[badge="true"] {
    background: #1B2438;
    border: 1px solid #2A3550;
    border-radius: 8px;
    padding: 2px 8px;
    color: #8FA6E0;
    font-size: 11px;
}
QLabel[badge="ok"] {
    background: #12291F;
    border: 1px solid #1E4633;
    color: #3DD68C;
}
QLabel[badge="warn"] {
    background: #2A2113;
    border: 1px solid #463716;
    color: #F5A623;
}

/* ── 按钮 ─────────────────────────────────────────── */
QPushButton {
    background: #1B2030;
    border: 1px solid #262D40;
    border-radius: 9px;
    padding: 8px 16px;
    color: #D8DDEA;
}
QPushButton:hover {
    background: #222941;
    border-color: #333B52;
}
QPushButton:pressed {
    background: #171C29;
}
QPushButton:disabled {
    color: #4C5468;
    background: #14171F;
    border-color: #1B1F2A;
}
QPushButton[accent="true"] {
    background: #4F7BFF;
    border-color: #4F7BFF;
    color: #FFFFFF;
    font-weight: 600;
}
QPushButton[accent="true"]:hover  { background: #5F88FF; border-color: #5F88FF; }
QPushButton[accent="true"]:pressed{ background: #426BE6; border-color: #426BE6; }
QPushButton[accent="true"]:disabled{ background: #23304F; border-color: #23304F; color: #7A849B; }
QPushButton[danger="true"] {
    background: #2E1A22;
    border-color: #4E2836;
    color: #FF8A9B;
}
QPushButton[danger="true"]:hover { background: #3A2029; border-color: #60303F; }
QPushButton[ghost="true"] {
    background: transparent;
    border: 1px solid #262D40;
    color: #9AA4BB;
}
QPushButton[ghost="true"]:hover { background: #171B25; color: #DCE2F0; }

/* ── 输入控件 ─────────────────────────────────────── */
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QComboBox {
    background: #0C0E13;
    border: 1px solid #242B3C;
    border-radius: 8px;
    padding: 7px 10px;
    color: #E6E8EE;
    selection-background-color: #4F7BFF;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QComboBox:focus {
    border-color: #4F7BFF;
}
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled {
    color: #4C5468;
    background: #101319;
}
QComboBox::drop-down {
    border: none;
    width: 22px;
}
QComboBox QAbstractItemView {
    background: #12151D;
    border: 1px solid #242B3C;
    border-radius: 8px;
    selection-background-color: #4F7BFF;
    padding: 4px;
}
QSpinBox::up-button, QSpinBox::down-button {
    width: 16px;
    border: none;
    background: #171C29;
}

/* ── 滑块 ─────────────────────────────────────────── */
QSlider::groove:horizontal {
    height: 6px;
    background: #1E2330;
    border-radius: 3px;
}
QSlider::sub-page:horizontal {
    background: #4F7BFF;
    border-radius: 3px;
}
QSlider::handle:horizontal {
    width: 15px;
    height: 15px;
    margin: -5px 0;
    border-radius: 7px;
    background: #FFFFFF;
}
QSlider::handle:horizontal:hover { background: #DCE6FF; }

/* ── 进度条 ───────────────────────────────────────── */
QProgressBar {
    background: #151922;
    border: 1px solid #222838;
    border-radius: 6px;
    height: 8px;
    text-align: center;
    color: transparent;
}
QProgressBar::chunk {
    background: #4F7BFF;
    border-radius: 5px;
}

/* ── 列表 / 表格 ──────────────────────────────────── */
QListWidget, QTreeWidget, QTableWidget {
    background: #0C0E13;
    border: 1px solid #1E2330;
    border-radius: 10px;
    color: #D8DDEA;
}
QListWidget::item, QTreeWidget::item {
    padding: 8px 10px;
    border-radius: 7px;
}
QListWidget::item:hover, QTreeWidget::item:hover { background: #151A24; }
QListWidget::item:selected, QTreeWidget::item:selected {
    background: #1B2438;
    color: #FFFFFF;
}
QTableWidget {
    gridline-color: #191D27;
    selection-background-color: #1B2438;
}
QHeaderView::section {
    background: #12151D;
    color: #7B859C;
    border: none;
    border-bottom: 1px solid #1E2330;
    padding: 7px 8px;
    font-size: 11px;
}
QTableWidget::item { padding: 6px 8px; }

/* ── 滚动条 ───────────────────────────────────────── */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: #262D40;
    border-radius: 5px;
    min-height: 30px;
}
QScrollBar::handle:vertical:hover { background: #333B52; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px;
}
QScrollBar::handle:horizontal {
    background: #262D40;
    border-radius: 5px;
    min-width: 30px;
}
QScrollBar::handle:horizontal:hover { background: #333B52; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* ── 选项卡 ───────────────────────────────────────── */
QTabWidget::pane {
    border: 1px solid #1E2330;
    border-radius: 12px;
    background: #14171F;
    top: -1px;
}
QTabBar::tab {
    background: transparent;
    color: #7B859C;
    padding: 8px 18px;
    border: none;
    border-bottom: 2px solid transparent;
    margin-right: 2px;
}
QTabBar::tab:hover { color: #C6CEE0; }
QTabBar::tab:selected {
    color: #FFFFFF;
    border-bottom: 2px solid #4F7BFF;
}

/* ── 复选框 ───────────────────────────────────────── */
QCheckBox, QRadioButton { color: #C6CEE0; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid #333B52;
    border-radius: 4px;
    background: #0C0E13;
}
QCheckBox::indicator:checked {
    background: #4F7BFF;
    border-color: #4F7BFF;
    image: none;
}
QCheckBox::indicator:hover { border-color: #4F7BFF; }

/* ── 分割线 ───────────────────────────────────────── */
QSplitter::handle { background: #1B1F2A; }
)QSS");
}

void applyDarkTitleBar(QWidget *widget)
{
#ifdef Q_OS_WIN
    if (!widget)
        return;
    BOOL dark = TRUE;
    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    // 老版本 Windows 10 使用属性 19
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    // 标题栏背景/文字颜色（Windows 11）
    const COLORREF caption = RGB(0x0F, 0x11, 0x16);
    const COLORREF text = RGB(0xE6, 0xE8, 0xEE);
    const COLORREF borderColor = RGB(0x19, 0x1D, 0x27);
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, 34, &borderColor, sizeof(borderColor));
#else
    Q_UNUSED(widget);
#endif
}

} // namespace Theme
