#pragma once

#include <QList>
#include <QMainWindow>

class QLabel;
class QPushButton;
class QStackedWidget;

class AudioMixer;
class DashboardPage;
class EdenClient;
class GameLibraryPage;
class LosslessScaling;
class SaveManager;
class SettingsPage;
class ToolsPage;
class UpdatesPage;

// 主窗口：侧边栏导航 + 五个功能页（控制台 / 游戏库 / 工具箱 / 更新构建 / 设置）
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    QWidget *buildSidebar();
    QWidget *buildFooter();
    QPushButton *addNav(const QString &text, const QString &iconName, int pageIndex);
    void switchPage(int index);

    EdenClient *m_eden = nullptr;
    LosslessScaling *m_ls = nullptr;
    AudioMixer *m_audio = nullptr;
    SaveManager *m_saves = nullptr;

    QStackedWidget *m_stack = nullptr;
    DashboardPage *m_dashboard = nullptr;
    GameLibraryPage *m_library = nullptr;
    ToolsPage *m_tools = nullptr;
    UpdatesPage *m_updates = nullptr;
    SettingsPage *m_settings = nullptr;

    QLabel *m_statusLabel = nullptr;
    QList<QPushButton *> m_navButtons;
};
