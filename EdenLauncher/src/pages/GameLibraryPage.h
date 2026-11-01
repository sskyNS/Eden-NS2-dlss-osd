#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;
class QLineEdit;
class QScrollArea;

// 游戏库里的一个条目：目录型（exefs/main）或文件型（.nsp/.xci/...）
struct GameEntry
{
    QString name;
    QString dir;       // 游戏所在目录（文件型为文件所在目录）
    QString mainPath;  // exefs/main 或打包文件；为空表示缺少可执行入口
    qint64 bytes = 0;
    bool sizeKnown = false;
};

// 扫描 runtimeDir/game，用封面卡片展示游戏并可一键启动
class GameLibraryPage : public QWidget
{
    Q_OBJECT

public:
    explicit GameLibraryPage(QWidget *parent = nullptr);

    // 后台线程统计完体积后回到主线程调用
    void onSizeReady(const QString &dir, qint64 bytes);

signals:
    void requestLaunchGame();

public slots:
    void rescan();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void buildUi();
    void rebuildGrid();
    QWidget *buildCard(int index);
    void launchEntry(int index);
    QString configIniFor(const QString &name) const;
    void updateHint();

    QLineEdit *m_search = nullptr;
    QLabel *m_hint = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_canvas = nullptr;
    QGridLayout *m_grid = nullptr;
    QLabel *m_empty = nullptr;
    QVector<GameEntry> m_games;
    int m_columns = 3;
};
