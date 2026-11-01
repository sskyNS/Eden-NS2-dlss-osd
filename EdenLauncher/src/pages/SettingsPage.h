#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

// 设置：运行时路径、游戏启动参数、小黄鸭 / OSD、更新与构建工具链
class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

signals:
    void configSaved();

private:
    void buildUi();
    void loadValues();
    void saveValues();
    void detectRuntime();

    QLineEdit *m_runtimeDir = nullptr;
    QLineEdit *m_gameName = nullptr;
    QLineEdit *m_gameMain = nullptr;
    QLineEdit *m_gameIni = nullptr;
    QLineEdit *m_extraArgs = nullptr;

    QLineEdit *m_lsExe = nullptr;
    QLineEdit *m_lsHotkey = nullptr;
    QCheckBox *m_autoLs = nullptr;
    QCheckBox *m_autoOsd = nullptr;
    QCheckBox *m_forceWgc = nullptr;

    QComboBox *m_windowPreset = nullptr;
    QCheckBox *m_minimize = nullptr;

    QLineEdit *m_repo = nullptr;
    QLineEdit *m_sourceDir = nullptr;
    QLineEdit *m_patchFile = nullptr;
    QLineEdit *m_mingwRoot = nullptr;
    QLineEdit *m_gitExe = nullptr;

    QLabel *m_status = nullptr;
};
