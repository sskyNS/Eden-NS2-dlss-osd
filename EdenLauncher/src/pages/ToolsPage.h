#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class AudioMixer;
class EdenClient;
class QLabel;
class QPushButton;
class QSlider;
class QTableWidget;
class QTimer;
class QVBoxLayout;
class SaveManager;

// 工具箱：按进程音量 / 音频隔离、存档快速导出、游戏窗口尺寸调整
class ToolsPage : public QWidget
{
    Q_OBJECT

public:
    ToolsPage(AudioMixer *audio, SaveManager *saves, EdenClient *eden, QWidget *parent = nullptr);

signals:
    void requestDashboard();

private:
    struct AudioRow
    {
        quint32 pid = 0;
        QLabel *nameLabel = nullptr;
        QLabel *pctLabel = nullptr;
        QSlider *slider = nullptr;
        QPushButton *muteBtn = nullptr;
        QWidget *root = nullptr;
    };

    void buildUi();
    QWidget *buildAudioCard();
    QWidget *buildSaveCard();
    QWidget *buildWindowCard();

    void refreshAudio(bool force = false);
    void clearAudioRows();
    void updateAudioHint();

    void refreshSaves();
    void exportSelected();
    void exportAll();

    void applySize(int width, int height);
    void centerGameWindow();
    void maximizeGameWindow();
    void toggleBorderless();
    void refreshWindowInfo();
    quintptr gameWindow() const;

    AudioMixer *m_audio = nullptr;
    SaveManager *m_saves = nullptr;
    EdenClient *m_eden = nullptr;

    QVBoxLayout *m_sessionLayout = nullptr;
    QLabel *m_audioHint = nullptr;
    QLabel *m_isolateBadge = nullptr;
    QList<AudioRow> m_rows;
    QTimer *m_audioTimer = nullptr;
    bool m_updatingAudio = false;

    QTableWidget *m_saveTable = nullptr;
    QLabel *m_saveHint = nullptr;

    QLabel *m_windowInfo = nullptr;
};
