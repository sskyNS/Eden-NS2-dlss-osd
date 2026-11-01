#include "GameLibraryPage.h"

#include "AppConfig.h"
#include "IconFactory.h"
#include "Theme.h"
#include "widgets/Ui.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QLocale>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QRunnable>
#include <QScrollArea>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int kCardWidth = 232;
constexpr int kCardPadding = 12;
constexpr int kCoverHeight = 130;
constexpr int kGap = 18;
constexpr int kRadius = 10;

QString humanBytes(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

// 缩放裁剪 + 圆角，返回 2x 图以适配高 DPI
QPixmap roundCrop(const QPixmap &src, const QSize &size, int radius)
{
    if (src.isNull())
        return src;

    constexpr int dpr = 2;
    QPixmap out(size * dpr);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPainterPath path;
    path.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), radius, radius);
    p.setClipPath(path);

    const QPixmap scaled = src.scaled(size * dpr, Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation);
    const QRectF source((scaled.width() - size.width() * dpr) / 2.0,
                        (scaled.height() - size.height() * dpr) / 2.0,
                        size.width() * dpr, size.height() * dpr);
    p.drawPixmap(QRectF(QPointF(0, 0), QSizeF(size)), scaled, source);
    return out;
}

// 没有封面时用首字母 + 渐变占位（色相按名字散开，避免所有卡片一个颜色）
QPixmap placeholderCover(const QString &name, const QSize &size, int radius)
{
    constexpr int dpr = 2;
    QPixmap out(size * dpr);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath path;
    path.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), radius, radius);

    const int hue = 200 + int(qHash(name) % 80); // 200~279，贴近主题蓝紫区间
    QLinearGradient grad(QPointF(0, 0), QPointF(size.width(), size.height()));
    grad.setColorAt(0.0, QColor::fromHsv(hue % 360, 150, 150));
    grad.setColorAt(1.0, QColor::fromHsv((hue + 26) % 360, 140, 74));
    p.fillPath(path, grad);

    QFont f(QStringLiteral("Microsoft YaHei UI"));
    f.setPixelSize(46);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 210));
    p.drawText(QRectF(QPointF(0, 0), QSizeF(size)), Qt::AlignCenter,
               name.left(1).toUpper());
    return out;
}

QPixmap coverPixmap(const QString &dir, const QString &name, const QSize &size)
{
    static const char *kCandidates[] = {
        "cover.png", "cover.jpg", "cover.jpeg", "cover.webp",
        "icon.png", "icon.jpg", "boxart.png", "artwork.png",
    };
    for (const char *file : kCandidates) {
        const QString path = dir + QLatin1Char('/') + QLatin1String(file);
        if (!QFileInfo::exists(path))
            continue;
        const QPixmap pm(path);
        if (!pm.isNull())
            return roundCrop(pm, size, kRadius);
    }
    return placeholderCover(name, size, kRadius);
}

// 后台线程递归统计体积（跳过压缩包，避免把 6GB 的 .7z 重复算进去）
class GameSizeTask : public QRunnable
{
public:
    GameSizeTask(QPointer<GameLibraryPage> page, QString dir)
        : m_page(std::move(page))
        , m_dir(std::move(dir))
    {
    }

    void run() override
    {
        qint64 total = 0;
        QDirIterator it(m_dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString suffix = it.fileInfo().suffix().toLower();
            if (suffix == QLatin1String("7z") || suffix == QLatin1String("zip")
                || suffix == QLatin1String("rar"))
                continue;
            total += it.fileInfo().size();
        }

        GameLibraryPage *page = m_page.data();
        if (!page)
            return;
        const QString dir = m_dir;
        QMetaObject::invokeMethod(
            page, [page, dir, total] { page->onSizeReady(dir, total); }, Qt::QueuedConnection);
    }

private:
    QPointer<GameLibraryPage> m_page;
    QString m_dir;
};

// 双击卡片直接启动
class GameCard : public QFrame
{
public:
    std::function<void()> onActivate;

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onActivate) {
            onActivate();
            return;
        }
        QFrame::mouseDoubleClickEvent(event);
    }
};

} // namespace

GameLibraryPage::GameLibraryPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void GameLibraryPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 18, 26, 16);
    root->setSpacing(12);

    auto *header = new QHBoxLayout;
    header->setSpacing(10);

    auto *titleCol = new QVBoxLayout;
    titleCol->setSpacing(2);
    titleCol->addWidget(Ui::makeH1(tr("游戏库")));
    m_hint = Ui::makeMuted(QString());
    titleCol->addWidget(m_hint);
    header->addLayout(titleCol);
    header->addStretch(1);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("搜索游戏名称…"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(220);
    header->addWidget(m_search);

    auto *openBtn = Ui::makeGhostButton(tr("打开目录"), QStringLiteral("folder"));
    auto *refreshBtn = Ui::makeGhostButton(tr("重新扫描"), QStringLiteral("refresh"));
    header->addWidget(openBtn);
    header->addWidget(refreshBtn);
    root->addLayout(header);

    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->viewport()->installEventFilter(this);

    m_canvas = new QWidget;
    m_grid = new QGridLayout(m_canvas);
    m_grid->setContentsMargins(0, 0, 8, 0);
    m_grid->setHorizontalSpacing(kGap);
    m_grid->setVerticalSpacing(kGap);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_scroll->setWidget(m_canvas);
    root->addWidget(m_scroll, 1);

    m_empty = Ui::makeMuted(QString());
    m_empty->setParent(m_canvas);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setMinimumHeight(180);
    m_empty->hide();

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) { rebuildGrid(); });
    connect(refreshBtn, &QPushButton::clicked, this, &GameLibraryPage::rescan);
    connect(openBtn, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppConfig::instance().gameDir()));
    });

    rescan();
}

void GameLibraryPage::rescan()
{
    m_games.clear();

    const QDir root(AppConfig::instance().gameDir());
    if (root.exists()) {
        // 解包目录型：exefs/main 是可执行入口
        const QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &name : dirs) {
            const QString dirPath = root.absoluteFilePath(name);
            GameEntry entry;
            entry.name = name;
            entry.dir = QDir::toNativeSeparators(dirPath);

            const QString main = dirPath + QStringLiteral("/exefs/main");
            if (QFileInfo::exists(main)) {
                entry.mainPath = QDir::toNativeSeparators(main);
            } else if (!QFileInfo::exists(dirPath + QStringLiteral("/romfs"))
                       && !QFileInfo::exists(dirPath + QStringLiteral("/romfs.bin"))) {
                continue; // 既没有 exefs 也没有 romfs，不是游戏目录
            }
            m_games.append(entry);
        }

        // 打包文件型
        static const QStringList kPackExt = {QStringLiteral("nsp"), QStringLiteral("xci"),
                                             QStringLiteral("xcz"), QStringLiteral("nro")};
        const QStringList files = root.entryList(QDir::Files, QDir::Name);
        for (const QString &file : files) {
            if (!kPackExt.contains(QFileInfo(file).suffix().toLower()))
                continue;
            const QFileInfo info(root.absoluteFilePath(file));
            GameEntry entry;
            entry.name = info.completeBaseName();
            entry.dir = QDir::toNativeSeparators(info.absolutePath());
            entry.mainPath = QDir::toNativeSeparators(info.absoluteFilePath());
            entry.bytes = info.size();
            entry.sizeKnown = true;
            m_games.append(entry);
        }
    }

    for (const GameEntry &entry : m_games) {
        if (entry.sizeKnown)
            continue;
        QThreadPool::globalInstance()->start(
            new GameSizeTask(QPointer<GameLibraryPage>(this), entry.dir));
    }

    updateHint();
    rebuildGrid();
}

void GameLibraryPage::onSizeReady(const QString &dir, qint64 bytes)
{
    bool changed = false;
    for (GameEntry &entry : m_games) {
        if (!entry.sizeKnown && entry.dir == dir) {
            entry.bytes = bytes;
            entry.sizeKnown = true;
            changed = true;
        }
    }
    if (changed)
        rebuildGrid();
}

void GameLibraryPage::updateHint()
{
    m_hint->setText(tr("共 %1 个游戏 · %2")
                        .arg(m_games.size())
                        .arg(QDir::toNativeSeparators(AppConfig::instance().gameDir())));
}

void GameLibraryPage::rebuildGrid()
{
    for (int i = 0; i < 16; ++i) {
        m_grid->setColumnStretch(i, 0);
        m_grid->setRowStretch(i, 0);
    }

    while (QLayoutItem *item = m_grid->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            if (w == m_empty)
                w->hide();
            else
                w->deleteLater();
        }
        delete item;
    }

    const QString query = m_search ? m_search->text().trimmed() : QString();
    QVector<int> shown;
    shown.reserve(m_games.size());
    for (int i = 0; i < m_games.size(); ++i) {
        if (query.isEmpty() || m_games.at(i).name.contains(query, Qt::CaseInsensitive))
            shown.append(i);
    }

    if (shown.isEmpty()) {
        m_empty->setText(m_games.isEmpty()
                             ? tr("在 %1 下没有找到游戏").arg(QDir::toNativeSeparators(
                                   AppConfig::instance().gameDir()))
                             : tr("没有匹配「%1」的游戏").arg(query));
        m_grid->addWidget(m_empty, 0, 0, 1, qMax(1, m_columns));
        m_empty->show();
        updateHint();
        return;
    }

    const int cols = qMax(1, m_columns);
    for (int i = 0; i < shown.size(); ++i)
        m_grid->addWidget(buildCard(shown.at(i)), i / cols, i % cols, Qt::AlignTop);

    const int rows = (shown.size() + cols - 1) / cols;
    m_grid->setColumnStretch(qMin(cols, 15), 1);
    m_grid->setRowStretch(qMin(rows, 15), 1);

    updateHint();
}

QWidget *GameLibraryPage::buildCard(int index)
{
    const GameEntry &entry = m_games.at(index);

    auto *card = new GameCard;
    card->setProperty("card", true);
    card->setFixedWidth(kCardWidth);

    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(kCardPadding, kCardPadding, kCardPadding, kCardPadding);
    lay->setSpacing(8);

    auto *cover = new QLabel;
    cover->setFixedSize(kCardWidth - kCardPadding * 2, kCoverHeight);
    cover->setPixmap(coverPixmap(entry.dir, entry.name, cover->size()));
    cover->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    lay->addWidget(cover);

    auto *name = new QLabel(entry.name);
    name->setProperty("title", true);
    name->setWordWrap(true);
    name->setFixedHeight(38);
    name->setToolTip(entry.name);
    lay->addWidget(name);

    auto *path = new Ui::ElidedLabel;
    path->setLines({entry.dir});
    lay->addWidget(path);

    auto *foot = new QHBoxLayout;
    foot->setSpacing(6);

    const bool isActive = !entry.mainPath.isEmpty()
                          && entry.mainPath == AppConfig::instance().gameMain();
    if (isActive) {
        foot->addWidget(Ui::makeBadge(tr("当前"), QStringLiteral("ok")));
    } else if (entry.mainPath.isEmpty()) {
        foot->addWidget(Ui::makeBadge(tr("缺少入口"), QStringLiteral("warn")));
    } else {
        foot->addWidget(Ui::makeBadge(entry.sizeKnown ? humanBytes(entry.bytes)
                                                      : tr("统计中…")));
    }
    if (isActive && entry.sizeKnown)
        foot->addWidget(Ui::makeBadge(humanBytes(entry.bytes)));
    foot->addStretch(1);

    auto *launchBtn = Ui::makeButton(tr("启动"), QStringLiteral("play"), true);
    launchBtn->setEnabled(!entry.mainPath.isEmpty());
    foot->addWidget(launchBtn);
    lay->addLayout(foot);

    if (isActive)
        card->setProperty("highlight", true);

    connect(launchBtn, &QPushButton::clicked, this, [this, index] { launchEntry(index); });
    card->onActivate = [this, index] { launchEntry(index); };

    return card;
}

QString GameLibraryPage::configIniFor(const QString &name) const
{
    AppConfig &cfg = AppConfig::instance();
    const QString base = name.toLower().simplified().replace(' ', '-');
    const QString guess = cfg.configDir() + QLatin1Char('/') + base + QStringLiteral(".ini");
    if (!base.isEmpty() && QFileInfo::exists(guess))
        return guess;
    return cfg.gameConfigIni(); // 没有专属配置就沿用当前可用的
}

void GameLibraryPage::launchEntry(int index)
{
    if (index < 0 || index >= m_games.size())
        return;

    const GameEntry &entry = m_games.at(index);
    if (entry.mainPath.isEmpty())
        return;

    AppConfig &cfg = AppConfig::instance();
    cfg.setGameName(entry.name);
    cfg.setGameMain(entry.mainPath);
    const QString ini = configIniFor(entry.name);
    if (!ini.isEmpty())
        cfg.setGameConfigIni(ini);

    rebuildGrid(); // 刷新「当前」标记
    emit requestLaunchGame();
}

bool GameLibraryPage::eventFilter(QObject *obj, QEvent *event)
{
    if (m_scroll && obj == m_scroll->viewport() && event->type() == QEvent::Resize) {
        const int available = m_scroll->viewport()->width() - 8;
        const int cols = qBound(1, available / (kCardWidth + kGap), 6);
        if (cols != m_columns) {
            m_columns = cols;
            rebuildGrid();
        }
    }
    return QWidget::eventFilter(obj, event);
}
