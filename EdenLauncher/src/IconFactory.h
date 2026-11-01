#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

// 用内嵌 SVG 生成矢量图标，无需外部资源文件
namespace IconFactory {

QIcon icon(const QString &name, const QColor &color, int size = 20);
QPixmap pixmap(const QString &name, const QColor &color, int size = 20);

} // namespace IconFactory
