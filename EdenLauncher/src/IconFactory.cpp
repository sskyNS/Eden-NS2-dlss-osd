#include "IconFactory.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace {

// Feather 风格线性图标，24x24 视图盒
const QHash<QString, QString> &pathTable()
{
    static const QHash<QString, QString> table = {
        {"dashboard", "M3 13h8V3H3v10zm0 8h8v-6H3v6zm10 0h8V11h-8v10zm0-18v6h8V3h-8z"},
        {"tools", "M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z"},
        {"update", "M21 12a9 9 0 1 1-2.64-6.36M21 3v6h-6"},
        {"settings", "M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z|M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"},
        {"play", "M5 3l14 9-14 9V3z"},
        {"stop", "M6 6h12v12H6z"},
        {"refresh", "M23 4v6h-6M1 20v-6h6|M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"},
        {"folder", "M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"},
        {"volume", "M11 5L6 9H2v6h4l5 4V5z|M19.07 4.93a10 10 0 0 1 0 14.14M15.54 8.46a5 5 0 0 1 0 7.07"},
        {"save", "M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z|M17 21v-8H7v8M7 3v5h8"},
        {"window", "M4 4h16v16H4z|M4 9h16M8 4v5"},
        {"chip", "M9 3h6v2H9zM9 19h6v2H9zM3 9h2v6H3zM19 9h2v6h-2z|M6 6h12v12H6z|M10 10h4v4h-4z"},
        {"terminal", "M4 17l6-6-6-6|M12 19h8"},
        {"download", "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4|M7 10l5 5 5-5|M12 15V3"},
        {"check", "M20 6L9 17l-5-5"},
        {"close", "M18 6L6 18M6 6l12 12"},
        {"git", "M12 2a10 10 0 0 0-3.16 19.49c.5.09.68-.22.68-.48v-1.7c-2.78.6-3.37-1.34-3.37-1.34-.45-1.16-1.11-1.47-1.11-1.47-.91-.62.07-.6.07-.6 1 .07 1.53 1.03 1.53 1.03.89 1.53 2.34 1.09 2.91.83.09-.65.35-1.09.63-1.34-2.22-.25-4.56-1.11-4.56-4.95 0-1.09.39-1.98 1.03-2.68-.1-.25-.45-1.27.1-2.65 0 0 .84-.27 2.75 1.02a9.5 9.5 0 0 1 5 0c1.91-1.29 2.75-1.02 2.75-1.02.55 1.38.2 2.4.1 2.65.64.7 1.03 1.59 1.03 2.68 0 3.85-2.34 4.7-4.57 4.95.36.31.68.92.68 1.85v2.74c0 .27.18.58.69.48A10 10 0 0 0 12 2z"},
        {"info", "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20z|M12 16v-4M12 8h.01"},
        {"bolt", "M13 2L3 14h9l-1 8 10-12h-9l1-8z"},
        {"monitor", "M2 3h20v14H2z|M8 21h8M12 17v4"},
        {"mute", "M11 5L6 9H2v6h4l5 4V5z|M23 9l-6 6M17 9l6 6"},
        {"music", "M9 18V5l12-2v13|M9 18a3 3 0 1 1-6 0 3 3 0 0 1 6 0zM21 16a3 3 0 1 1-6 0 3 3 0 0 1 6 0z"},
        {"package", "M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z|M3.27 6.96L12 12.01l8.73-5.05M12 22.08V12"},
    };
    return table;
}

QString svgFor(const QString &name, const QColor &color)
{
    const auto &table = pathTable();
    const QString raw = table.value(name, table.value(QStringLiteral("info")));
    QString body;
    for (const QString &part : raw.split('|'))
        body += QStringLiteral("<path d=\"%1\"/>").arg(part);

    // 实心图标（播放/停止）需要 fill，其余用描边
    const bool filled = (name == QLatin1String("play") || name == QLatin1String("stop"));
    const QString fill = filled ? color.name(QColor::HexRgb)
                                : QStringLiteral("none");
    const QString stroke = filled ? QStringLiteral("none")
                                  : color.name(QColor::HexRgb);

    return QStringLiteral(
               "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" "
               "fill=\"%1\" stroke=\"%2\" stroke-width=\"2\" stroke-linecap=\"round\" "
               "stroke-linejoin=\"round\">%3</svg>")
        .arg(fill, stroke, body);
}

} // namespace

namespace IconFactory {

QPixmap pixmap(const QString &name, const QColor &color, int size)
{
    const qreal ratio = 2.0;
    QPixmap pm(QSize(size, size) * ratio);
    pm.setDevicePixelRatio(ratio);
    pm.fill(Qt::transparent);

    QSvgRenderer renderer(svgFor(name, color).toUtf8());
    if (!renderer.isValid())
        return pm;

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0, 0, size, size));
    return pm;
}

QIcon icon(const QString &name, const QColor &color, int size)
{
    QIcon icon;
    icon.addPixmap(pixmap(name, color, size), QIcon::Normal);
    icon.addPixmap(pixmap(name, color.lighter(130), size), QIcon::Active);
    icon.addPixmap(pixmap(name, QColor(0x4C, 0x54, 0x68), size), QIcon::Disabled);
    return icon;
}

} // namespace IconFactory
