#pragma once

#include <QColor>
#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QString>
#include <QStringList>

class QLabel;
class QLayout;
class QVBoxLayout;
class QHBoxLayout;
class QPushButton;

namespace Ui {

// 卡片容器：标题 + 副标题 + 内容区
class Card : public QFrame
{
    Q_OBJECT
public:
    explicit Card(const QString &title = QString(), QWidget *parent = nullptr);

    QVBoxLayout *body() const { return m_body; }
    void setTitle(const QString &title);
    void setSubtitle(const QString &subtitle);
    void setHighlight(bool on);
    // 右上角放按钮/控件
    void addHeaderWidget(QWidget *widget);

private:
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QVBoxLayout *m_body = nullptr;
    QHBoxLayout *m_headerRow = nullptr;
};

// 小指标块：上面大数字，下面小标题
class StatTile : public QFrame
{
    Q_OBJECT
public:
    explicit StatTile(const QString &label, const QString &value = QStringLiteral("--"),
                      QWidget *parent = nullptr);
    void setValue(const QString &value);
    void setValueColor(const QColor &color);
    void setLabel(const QString &label);

private:
    QLabel *m_value = nullptr;
    QLabel *m_label = nullptr;
};

// 可复制的日志输出
class LogView : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit LogView(QWidget *parent = nullptr);
    void appendLine(const QString &line);
    void appendError(const QString &line);
    void appendOk(const QString &line);
};

// 多行文本标签：宽度不够时按行中间省略，长路径不会把布局撑破
class ElidedLabel : public QLabel
{
    Q_OBJECT
public:
    explicit ElidedLabel(QWidget *parent = nullptr);

    void setLines(const QStringList &lines);
    const QStringList &lines() const { return m_lines; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QStringList m_lines;
};

QPushButton *makeButton(const QString &text, const QString &iconName = QString(),
                        bool accent = false, bool danger = false);
QPushButton *makeGhostButton(const QString &text, const QString &iconName = QString());

QLabel *makeTitle(const QString &text);
QLabel *makeH1(const QString &text);
QLabel *makeMuted(const QString &text);
QLabel *makeBadge(const QString &text, const QString &state = QString());

QFrame *makeSeparator();

// 给按钮打上 QSS 属性
void setStyleProperty(QWidget *w, const char *name, const QVariant &value);

} // namespace Ui
