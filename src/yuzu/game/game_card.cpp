// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QFileInfo>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include "game_card.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"

GameCard::GameCard(QObject* parent) : QStyledItemDelegate{parent} {
    setObjectName("GameCard");
}

void GameCard::paint(QPainter* painter, const QStyleOptionViewItem& option,
                     const QModelIndex& index) const {
    if (!index.isValid())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    constexpr int cardCornerRadius = 12;
    constexpr int iconCornerRadius = 8;
    constexpr int cardInset = 10;

    const QRect cardRect = getCardRect(option, index);

    const QPalette palette = option.palette;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    // Eden 焕新：卡片统一使用品牌蓝作为强调色，未选中时呈深色玻璃质感
    QColor accent = palette.highlight().color();
    if (!accent.isValid())
        accent = QColor(0x4F, 0x7B, 0xFF);

    QColor surfaceTop = palette.window().color().lighter(115);
    QColor surfaceBottom = palette.window().color().darker(105);
    QColor borderColor = palette.mid().color();
    QColor textColor = palette.text().color();

    if (selected) {
        surfaceTop = accent.darker(140);
        surfaceBottom = accent.darker(200);
        borderColor = accent;
        textColor = palette.highlightedText().color();
    } else if (hovered) {
        surfaceTop = palette.window().color().lighter(135);
        surfaceBottom = palette.window().color().lighter(108);
        borderColor = accent.lighter(115);
    }

    // 选中/悬停时的外发光，让当前卡片从网格里“浮”起来
    if (selected || hovered) {
        QColor glow = accent;
        glow.setAlpha(selected ? 90 : 45);
        painter->setPen(Qt::NoPen);
        painter->setBrush(glow);
        painter->drawRoundedRect(cardRect.adjusted(-2, -2, 2, 2), cardCornerRadius + 2,
                                 cardCornerRadius + 2);
    }

    QLinearGradient gradient(cardRect.topLeft(), cardRect.bottomLeft());
    gradient.setColorAt(0.0, surfaceTop);
    gradient.setColorAt(1.0, surfaceBottom);
    painter->setBrush(gradient);
    painter->setPen(QPen(borderColor, selected ? 2 : 1));
    painter->drawRoundedRect(cardRect.adjusted(1, 1, -1, -1), cardCornerRadius,
                             cardCornerRadius);

    const u32 icon_size = UISettings::values.game_icon_size.GetValue();
    QPixmap icon_pixmap = index.data(Qt::DecorationRole).value<QPixmap>();

    // 上游在拿不到 title 元数据时会塞一张全透明的占位图，这里按“无封面”处理
    if (index.data(GameListItemPath::TitleRole).toString().trimmed().isEmpty())
        icon_pixmap = QPixmap();

    QString title = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first().trimmed();
    if (title.isEmpty()) {
        // 解包 dump 读不到 title 时退回显示名字，避免卡片全空
        const QFileInfo info(index.data(GameListItemPath::FullPathRole).toString());
        if (info.fileName().compare(QLatin1String("exefs"), Qt::CaseInsensitive) == 0
            || info.fileName().compare(QLatin1String("romfs"), Qt::CaseInsensitive) == 0) {
            title = QFileInfo(info.absolutePath()).fileName();
        } else {
            title = info.fileName();
        }
    }

    QRect iconRect;
    if (!icon_pixmap.isNull()) {
        QSize scaled = icon_pixmap.size();
        scaled.scale(static_cast<int>(icon_size), static_cast<int>(icon_size),
                     Qt::KeepAspectRatio);

        iconRect = {cardRect.left() + (cardRect.width() - scaled.width()) / 2,
                    cardRect.top() + cardInset, scaled.width(), scaled.height()};

        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

        painter->save();
        QPainterPath clip_path;
        clip_path.addRoundedRect(iconRect, iconCornerRadius, iconCornerRadius);
        painter->setClipPath(clip_path);
        painter->drawPixmap(iconRect, icon_pixmap);
        painter->restore();
    } else {
        iconRect = {cardRect.left() + (cardRect.width() - static_cast<int>(icon_size)) / 2,
                    cardRect.top() + cardInset, static_cast<int>(icon_size),
                    static_cast<int>(icon_size)};

        // 无封面时画一个占位块，用首字母保持可辨识
        QColor placeholderFill = accent;
        placeholderFill.setAlpha(selected ? 70 : 34);
        painter->setPen(Qt::NoPen);
        painter->setBrush(placeholderFill);
        painter->drawRoundedRect(iconRect, iconCornerRadius, iconCornerRadius);

        if (!title.isEmpty()) {
            QFont placeholderFont = option.font;
            placeholderFont.setBold(true);
            placeholderFont.setPixelSize(static_cast<int>(icon_size) / 4);
            painter->setFont(placeholderFont);
            QColor placeholderColor = accent;
            placeholderColor.setAlpha(200);
            painter->setPen(placeholderColor);
            painter->drawText(iconRect, Qt::AlignCenter, title.left(1).toUpper());
        }
    }

    if (UISettings::values.show_game_name.GetValue()) {
        QRect textRect = cardRect;
        textRect.setTop(iconRect.bottom() + cardMargin);
        textRect.adjust(cardInset, 0, -cardInset, -cardInset);

        painter->setPen(textColor);
        QFont font = option.font;
        font.setBold(true);
        font.setPixelSize(std::max(11.0, std::sqrt(static_cast<double>(icon_size))));
        painter->setFont(font);

        const QFontMetrics metrics(font);
        const QString elided = metrics.elidedText(title, Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, elided);
    }

    painter->restore();
}

QRect GameCard::getCardRect(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int cell_width = option.rect.width();

    const int card_width = cell_width - m_padding;

    int card_left, card_top, card_height;

    if (m_columns >= 1) {
        // grid mode

        // center everything in-line, such that the leftmost and rightmost cards
        // have ~ equal padding to the edge of the viewport
        // spacing between each card is larger, but equal to each other
        const int column = index.row() % m_columns;

        const int row_width = m_columns * cell_width;
        const int total_gap = row_width - cardMargin * 2 - m_columns * card_width;
        const int gap = (m_columns > 1) ? (total_gap / (m_columns - 1)) : 0;
        card_left =
            option.rect.left() - column * cell_width + cardMargin + column * (card_width + gap) + 4;

        // fill cell vertically
        card_top = option.rect.top() + cardMargin;
        card_height = option.rect.height() - cardMargin - 1;
    } else {
        // carousel mode
        card_left = option.rect.left() + cardMargin + 4;

        // the delegate itself takes up the full height, but the card itself
        // gets centered
        const int content_height = m_contentSize.height() - cardMargin;
        const int cell_height = option.rect.height();

        card_height = std::min(content_height, cell_height - cardMargin * 2) - 1;
        card_top = option.rect.top() + (cell_height - card_height) / 2;
    }

    return QRect(card_left, card_top, card_width - cardMargin, card_height);
}

bool GameCard::hitTest(const QPoint& point, const QModelIndex& index, const QWidget* widget,
                       const QRect& cellRect) const {
    QStyleOptionViewItem option;
    option.initFrom(widget);
    option.rect = cellRect;
    return getCardRect(option, index).contains(point);
}

QSize GameCard::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return m_size;
}

void GameCard::setSize(const QSize& newSize, const QSize& contentSize, const int padding,
                       const int columns) {
    m_size = newSize;
    m_contentSize = contentSize;
    m_padding = padding;
    m_columns = columns;
}
