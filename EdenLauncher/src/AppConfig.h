#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// 全局配置：核心是「运行时目录」，其余路径都由它推导，用户可在设置页覆盖
class AppConfig : public QObject
{
    Q_OBJECT

public:
    static AppConfig &instance();

    void load();
    void save();

    // ── 运行时目录 ────────────────────────────────────
    QString runtimeDir() const;
    void setRuntimeDir(const QString &dir);

    // 推导路径
    QString edenCliExe() const;      // runtimeDir/eden-cli.exe
    QString edenGuiExe() const;      // runtimeDir/eden.exe
    QString userDir() const;         // runtimeDir/user
    QString gameDir() const;         // runtimeDir/game（游戏库根目录）
    QString saveDir() const;         // runtimeDir/user/nand/user/save
    QString configDir() const;       // runtimeDir/user/config
    QString osdDir() const;          // runtimeDir/eden-osd
    QString osdScript() const;       // runtimeDir/eden-osd/eden-osd.ps1
    QString lsSettingsPath() const;  // %LOCALAPPDATA%/Lossless Scaling/Settings.xml

    // ── 游戏启动配置 ──────────────────────────────────
    QString gameName() const;
    void setGameName(const QString &name);

    QString gameMain() const;        // 例：runtimeDir/pokopia/exefs/main
    void setGameMain(const QString &path);

    QString gameConfigIni() const;   // 例：runtimeDir/user/config/pokopia.ini
    void setGameConfigIni(const QString &path);

    // 启动时的额外附加参数
    QStringList extraArgs() const;
    void setExtraArgs(const QStringList &args);

    // ── 小黄鸭 / OSD ──────────────────────────────────
    QString lsExe() const;
    void setLsExe(const QString &path);

    QString lsHotkey() const;        // "Ctrl+Alt+S"
    void setLsHotkey(const QString &hotkey);

    bool autoStartLs() const;
    void setAutoStartLs(bool on);

    bool autoStartOsd() const;
    void setAutoStartOsd(bool on);

    bool forceWgcCapture() const;
    void setForceWgcCapture(bool on);

    // ── 窗口 / 显示 ───────────────────────────────────
    QString windowPreset() const;    // 例如 "1920x1080"
    void setWindowPreset(const QString &preset);

    bool minimizeOnLaunch() const;
    void setMinimizeOnLaunch(bool on);

    // ── 更新与构建 ────────────────────────────────────
    QString updateRepo() const;      // "0x2593/Eden-NS2"
    void setUpdateRepo(const QString &repo);

    QString buildSourceDir() const;  // 本地源码目录（用于对比/构建）
    void setBuildSourceDir(const QString &dir);

    QString patchFile() const;       // NS2.patch 路径
    void setPatchFile(const QString &path);

    QString mingwRoot() const;       // MSYS2 mingw64 根目录
    void setMingwRoot(const QString &dir);

    QString gitExe() const;
    void setGitExe(const QString &path);

    // ── 上次检查到的上游提交 ──────────────────────────
    QString lastSeenCommit() const;
    void setLastSeenCommit(const QString &sha);

    // 自动探测运行时目录
    static QString detectRuntimeDir();

signals:
    void changed();

private:
    AppConfig() = default;

    QString m_runtimeDir;
    QString m_gameName;
    QString m_gameMain;
    QString m_gameConfigIni;
    QStringList m_extraArgs;
    QString m_lsExe;
    QString m_lsHotkey;
    bool m_autoStartLs = true;
    bool m_autoStartOsd = true;
    bool m_forceWgc = true;
    QString m_windowPreset;
    bool m_minimizeOnLaunch = false;
    QString m_updateRepo;
    QString m_buildSourceDir;
    QString m_patchFile;
    QString m_mingwRoot;
    QString m_gitExe;
    QString m_lastSeenCommit;
};
