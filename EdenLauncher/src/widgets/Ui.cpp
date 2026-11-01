#include "Ui.h"

#include "IconFactory.h"
#include "Theme.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOption>
#include <QVBoxLayout>

namespace Ui {

void setStyleProperty(QWidget *w, const char *name, const QVariant &value)
{
    if (!w)
        return;
    w->setProperty(name, value);
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

// ── Card ──────────────────────────────────────────────
Card::Card(const QString &title, QWidget *parent)
    : QFrame(parent)
{
    setProperty("card", true);
    setFrameShape(QFrame::NoFrame);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 16, 18, 16);
    outer->setSpacing(12);

    m_headerRow = new QHBoxLayout;
    m_headerRow->setSpacing(10);

    auto *textCol = new QVBoxLayout;
    textCol->setSpacing(2);
    m_titleLabel = new QLabel(title, this);
    m_titleLabel->setProperty("title", true);
    m_subtitleLabel = new QLabel(this);
    m_subtitleLabel->setProperty("muted", true);
    m_subtitleLabel->hide();

    textCol->addWidget(m_titleLabel);
    textCol->addWidget(m_subtitleLabel);
    m_headerRow->addLayout(textCol);
    m_headerRow->addStretch(1);
    outer->addLayout(m_headerRow);

    if (title.isEmpty())
        m_titleLabel->hide();

    m_body = new QVBoxLayout;
    m_body->setSpacing(10);
    outer->addLayout(m_body, 1);
}

void Card::setTitle(const QString &title)
{
    m_titleLabel->setText(title);
    m_titleLabel->setVisible(!title.isEmpty());
}

void Card::setSubtitle(const QString &subtitle)
{
    m_subtitleLabel->setText(subtitle);
    m_subtitleLabel->setVisible(!subtitle.isEmpty());
}

void Card::setHighlight(bool on)
{
    setStyleProperty(this, "highlight", on);
}

void Card::addHeaderWidget(QWidget *widget)
{
    m_headerRow->addWidget(widget);
}

// ── StatTile ──────────────────────────────────────────
StatTile::StatTile(const QString &label, const QString &value, QWidget *parent)
    : QFrame(parent)
{
    setProperty("stat", true);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 11, 14, 11);
    lay->setSpacing(3);

    m_value = new QLabel(value, this);
    m_value->setProperty("statValue", true);
    m_label = new QLabel(label, this);
    m_label->setProperty("statLabel", true);

    lay->addWidget(m_value);
    lay->addWidget(m_label);
}

void StatTile::setValue(const QString &value)
{
    m_value->setText(value);
}

void StatTile::setValueColor(const QColor &color)
{
    m_value->setStyleSheet(QStringLiteral("color:%1;").arg(color.name()));
}

void StatTile::setLabel(const QString &label)
{
    m_label->setText(label);
}

// ── LogView ───────────────────────────────────────────
LogView::LogView(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setMaximumBlockCount(20000);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont f(QStringLiteral("Consolas"));
    f.setPointSizeF(9.0);
    setFont(f);
    setStyleSheet(QStringLiteral(
        "QPlainTextEdit{background:#0A0C11;border:1px solid #1B1F2A;border-radius:10px;"
        "padding:8px;color:#AFB8CC;selection-background-color:#2A3550;}"));
}

void LogView::appendLine(const QString &line)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    appendPlainText(QStringLiteral("[%1] %2").arg(ts, line));
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void LogView::appendError(const QString &line)
{
    appendLine(QStringLiteral("✗ ") + line);
}

void LogView::appendOk(const QString &line)
{
    appendLine(QStringLiteral("✓ ") + line);
}

// ── ElidedLabel ───────────────────────────────────────
ElidedLabel::ElidedLabel(QWidget *parent)
    : QLabel(parent)
{
    setProperty("muted", true);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setTextInteractionFlags(Qt::TextSelectableByMouse);
}

void ElidedLabel::setLines(const QStringList &lines)
{
    m_lines = lines;
    setToolTip(lines.join('\n'));
    updateGeometry();
    update();
}

QSize ElidedLabel::sizeHint() const
{
    const QFontMetrics fm = fontMetrics();
    return QSize(80, fm.lineSpacing() * qMax(1, m_lines.size()));
}

QSize ElidedLabel::minimumSizeHint() const
{
    return QSize(40, sizeHint().height());
}

void ElidedLabel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QStyleOption opt;
    opt.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    p.setFont(font());
    p.setPen(palette().color(QPalette::WindowText));
    const QFontMetrics fm = fontMetrics();
    int y = (height() - fm.lineSpacing() * qMax(1, m_lines.size())) / 2;
    for (const QString &line : m_lines) {
        const QString elided = fm.elidedText(line, Qt::ElideMiddle, qMax(0, width()));
        p.drawText(QRect(0, y, width(), fm.lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter, elided);
        y += fm.lineSpacing();
    }
}

// ── 工厂函数 ──────────────────────────────────────────
QPushButton *makeButton(const QString &text, const QString &iconName, bool accent, bool danger)
{
    auto *b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    if (accent)
        b->setProperty("accent", true);
    if (danger)
        b->setProperty("danger", true);
    if (!iconName.isEmpty())
        b->setIcon(IconFactory::icon(iconName, accent ? QColor(Qt::white) : Theme::textPrimary(), 16));
    b->setIconSize(QSize(16, 16));
    return b;
}

QPushButton *makeGhostButton(const QString &text, const QString &iconName)
{
    auto *b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setProperty("ghost", true);
    if (!iconName.isEmpty())
        b->setIcon(IconFactory::icon(iconName, Theme::textMuted(), 16));
    b->setIconSize(QSize(16, 16));
    return b;
}

QLabel *makeTitle(const QString &text)
{
    auto *l = new QLabel(text);
    l->setProperty("title", true);
    return l;
}

QLabel *makeH1(const QString &text)
{
    auto *l = new QLabel(text);
    l->setProperty("h1", true);
    return l;
}

QLabel *makeMuted(const QString &text)
{
    auto *l = new QLabel(text);
    l->setProperty("muted", true);
    return l;
}

QLabel *makeBadge(const QString &text, const QString &state)
{
    auto *l = new QLabel(text);
    l->setProperty("badge", true);
    if (!state.isEmpty())
        l->setProperty("badge", state);
    l->setAlignment(Qt::AlignCenter);
    return l;
}

QFrame *makeSeparator()
{
    auto *f = new QFrame;
    f->setProperty("sep", true);
    f->setFrameShape(QFrame::HLine);
    f->setFixedHeight(1);
    return f;
}

} // namespace Ui
