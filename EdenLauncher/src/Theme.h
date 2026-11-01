#pragma once

#include <QColor>
#include <QString>

class QApplication;
class QWidget;

namespace Theme {

// 应用统一的深色主题（调色板 + 样式表）
void apply(QApplication &app);

// 让 Windows 标题栏跟随深色模式
void applyDarkTitleBar(QWidget *widget);

// 主题色
QColor accent();        // #4F7BFF
QColor accentSoft();    // 半透明强调色
QColor success();       // #3DD68C
QColor warning();       // #F5A623
QColor danger();        // #FF6B81
QColor textPrimary();   // #E6E8EE
QColor textMuted();     // #6B7488
QColor surface();       // #14171F
QColor background();    // #0F1116
QColor border();        // #1E2330

QString styleSheet();

} // namespace Theme
