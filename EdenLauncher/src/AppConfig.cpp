#include "AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {

const char *kRuntimeCandidates[] = {
    // exe 同级（打包发布后的形态）
    "",
    // 用户机器的实际运行时（安装时保持不动）
    "E:/BaiduNetdiskDownload/Eden模拟器 NS2专版/Eden_NS2_1.1.0",
    "D:/eden-ns2/Eden-NS2-main",
};

QString defaultLsExe()
{
    const QStringList candidates = {
        QStringLiteral("C:/Program Files (x86)/Steam/steamapps/common/Lossless Scaling/LosslessScaling.exe"),
        QStringLiteral("D:/Steam/steamapps/common/Lossless Scaling/LosslessScaling.exe"),
        QStringLiteral("E:/Steam/steamapps/common/Lossless Scaling/LosslessScaling.exe"),
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }
    return candidates.first();
}

QString defaultMingwRoot()
{
    const QStringList candidates = {
        QStringLiteral("D:/devkitPro/msys2/mingw64"),
        QStringLiteral("C:/msys64/mingw64"),
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c + "/bin/g++.exe"))
            return c;
    }
    return candidates.first();
}

QString defaultGitExe()
{
    const QStringList candidates = {
        QStringLiteral("D:/Git/cmd/git.exe"),
        QStringLiteral("C:/Program Files/Git/cmd/git.exe"),
        QStringLiteral("C:/Program Files (x86)/Git/cmd/git.exe"),
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }
    return QStringLiteral("git");
}

} // namespace

AppConfig &AppConfig::instance()
{
    static AppConfig cfg;
    return cfg;
}

QString AppConfig::detectRuntimeDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const char *rel : kRuntimeCandidates) {
        const QString base = QString::fromUtf8(rel).isEmpty()
                                 ? appDir
                                 : QString::fromUtf8(rel);
        if (QFileInfo::exists(base + "/eden-cli.exe"))
            return QDir::toNativeSeparators(base);
    }
    return QDir::toNativeSeparators(appDir);
}

void AppConfig::load()
{
    QSettings s;
    m_runtimeDir    = s.value("paths/runtime").toString();
    m_gameName      = s.value("game/name", "Pokopia").toString();
    m_gameMain      = s.value("game/main").toString();
    m_gameConfigIni = s.value("game/config").toString();
    m_extraArgs     = s.value("game/extraArgs").toStringList();
    m_lsExe         = s.value("ls/exe").toString();
    m_lsHotkey      = s.value("ls/hotkey", "Ctrl+Alt+S").toString();
    m_autoStartLs   = s.value("ls/autoStart", true).toBool();
    m_autoStartOsd  = s.value("osd/autoStart", true).toBool();
    m_forceWgc      = s.value("ls/forceWgc", true).toBool();
    m_windowPreset  = s.value("window/preset").toString();
    m_minimizeOnLaunch = s.value("window/minimizeOnLaunch", false).toBool();
    m_updateRepo    = s.value("update/repo", "0x2593/Eden-NS2").toString();
    m_buildSourceDir= s.value("build/sourceDir").toString();
    m_patchFile     = s.value("build/patchFile").toString();
    m_mingwRoot     = s.value("build/mingwRoot").toString();
    m_gitExe        = s.value("build/gitExe").toString();
    m_lastSeenCommit= s.value("update/lastSeenCommit").toString();

    // 补默认值
    if (m_runtimeDir.isEmpty() || !QFileInfo::exists(m_runtimeDir + "/eden-cli.exe"))
        m_runtimeDir = detectRuntimeDir();
    if (m_lsExe.isEmpty() || !QFileInfo::exists(m_lsExe))
        m_lsExe = defaultLsExe();
    if (m_mingwRoot.isEmpty() || !QFileInfo::exists(m_mingwRoot + "/bin/g++.exe"))
        m_mingwRoot = defaultMingwRoot();
    if (m_gitExe.isEmpty())
        m_gitExe = defaultGitExe();

    if (m_gameMain.isEmpty() || !QFileInfo::exists(m_gameMain)) {
        // 新布局：所有游戏都放在 runtimeDir/game/<游戏名>/exefs/main
        const QDir gameRoot(gameDir());
        QStringList names = gameRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
            const bool pa = a.compare(QStringLiteral("pokopia"), Qt::CaseInsensitive) == 0;
            const bool pb = b.compare(QStringLiteral("pokopia"), Qt::CaseInsensitive) == 0;
            return pa != pb ? pa : a < b;
        });
        for (const QString &n : names) {
            const QString main = gameRoot.absoluteFilePath(n + "/exefs/main");
            if (QFileInfo::exists(main)) {
                m_gameMain = QDir::toNativeSeparators(main);
                break;
            }
        }
        // 兼容旧布局：游戏直接放在 runtimeDir/pokopia
        if (m_gameMain.isEmpty() || !QFileInfo::exists(m_gameMain)) {
            const QString legacy = m_runtimeDir + "/pokopia/exefs/main";
            if (QFileInfo::exists(legacy))
                m_gameMain = QDir::toNativeSeparators(legacy);
        }
    }
    if (m_gameConfigIni.isEmpty() || !QFileInfo::exists(m_gameConfigIni)) {
        const QString base = m_gameName.toLower().simplified().replace(' ', '-');
        QString guess = configDir() + "/" + base + ".ini";
        if (!base.isEmpty() && QFileInfo::exists(guess)) {
            m_gameConfigIni = QDir::toNativeSeparators(guess);
        } else {
            guess = m_runtimeDir + "/user/config/pokopia.ini";
            if (QFileInfo::exists(guess))
                m_gameConfigIni = QDir::toNativeSeparators(guess);
        }
    }
    if (m_buildSourceDir.isEmpty()) {
        // 默认使用启动器自己管理的干净克隆，绝不改动运行时里的源码目录
        m_buildSourceDir = QDir::toNativeSeparators(
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/src");
    }
    if (m_patchFile.isEmpty() || !QFileInfo::exists(m_patchFile)) {
        QString guess = m_runtimeDir + "/Eden-NS2-main/NS2.patch";
        if (QFileInfo::exists(guess))
            m_patchFile = QDir::toNativeSeparators(guess);
    }
}

void AppConfig::save()
{
    QSettings s;
    s.setValue("paths/runtime", m_runtimeDir);
    s.setValue("game/name", m_gameName);
    s.setValue("game/main", m_gameMain);
    s.setValue("game/config", m_gameConfigIni);
    s.setValue("game/extraArgs", m_extraArgs);
    s.setValue("ls/exe", m_lsExe);
    s.setValue("ls/hotkey", m_lsHotkey);
    s.setValue("ls/autoStart", m_autoStartLs);
    s.setValue("ls/forceWgc", m_forceWgc);
    s.setValue("osd/autoStart", m_autoStartOsd);
    s.setValue("window/preset", m_windowPreset);
    s.setValue("window/minimizeOnLaunch", m_minimizeOnLaunch);
    s.setValue("update/repo", m_updateRepo);
    s.setValue("update/lastSeenCommit", m_lastSeenCommit);
    s.setValue("build/sourceDir", m_buildSourceDir);
    s.setValue("build/patchFile", m_patchFile);
    s.setValue("build/mingwRoot", m_mingwRoot);
    s.setValue("build/gitExe", m_gitExe);
    s.sync();
}

QString AppConfig::runtimeDir() const { return m_runtimeDir; }
void AppConfig::setRuntimeDir(const QString &dir)
{
    const QString native = QDir::toNativeSeparators(dir);
    if (m_runtimeDir == native)
        return;
    m_runtimeDir = native;
    save();
    emit changed();
}

QString AppConfig::edenCliExe() const { return m_runtimeDir + "/eden-cli.exe"; }
QString AppConfig::edenGuiExe() const { return m_runtimeDir + "/eden.exe"; }
QString AppConfig::userDir() const    { return m_runtimeDir + "/user"; }
QString AppConfig::gameDir() const    { return m_runtimeDir + "/game"; }
QString AppConfig::saveDir() const    { return m_runtimeDir + "/user/nand/user/save"; }
QString AppConfig::configDir() const  { return m_runtimeDir + "/user/config"; }
QString AppConfig::osdDir() const     { return m_runtimeDir + "/eden-osd"; }
QString AppConfig::osdScript() const  { return m_runtimeDir + "/eden-osd/eden-osd.ps1"; }

QString AppConfig::lsSettingsPath() const
{
    const QString local = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    // GenericConfigLocation 在 Windows 上是 %LOCALAPPDATA%
    return local + "/Lossless Scaling/Settings.xml";
}

QString AppConfig::gameName() const { return m_gameName; }
void AppConfig::setGameName(const QString &name) { m_gameName = name; save(); emit changed(); }

QString AppConfig::gameMain() const { return m_gameMain; }
void AppConfig::setGameMain(const QString &path)
{
    m_gameMain = QDir::toNativeSeparators(path);
    save();
    emit changed();
}

QString AppConfig::gameConfigIni() const { return m_gameConfigIni; }
void AppConfig::setGameConfigIni(const QString &path)
{
    m_gameConfigIni = QDir::toNativeSeparators(path);
    save();
    emit changed();
}

QStringList AppConfig::extraArgs() const { return m_extraArgs; }
void AppConfig::setExtraArgs(const QStringList &args) { m_extraArgs = args; save(); }

QString AppConfig::lsExe() const { return m_lsExe; }
void AppConfig::setLsExe(const QString &path)
{
    m_lsExe = QDir::toNativeSeparators(path);
    save();
    emit changed();
}

QString AppConfig::lsHotkey() const { return m_lsHotkey; }
void AppConfig::setLsHotkey(const QString &hotkey) { m_lsHotkey = hotkey; save(); }

bool AppConfig::autoStartLs() const { return m_autoStartLs; }
void AppConfig::setAutoStartLs(bool on) { m_autoStartLs = on; save(); }

bool AppConfig::autoStartOsd() const { return m_autoStartOsd; }
void AppConfig::setAutoStartOsd(bool on) { m_autoStartOsd = on; save(); }

bool AppConfig::forceWgcCapture() const { return m_forceWgc; }
void AppConfig::setForceWgcCapture(bool on) { m_forceWgc = on; save(); }

QString AppConfig::windowPreset() const { return m_windowPreset; }
void AppConfig::setWindowPreset(const QString &preset) { m_windowPreset = preset; save(); }

bool AppConfig::minimizeOnLaunch() const { return m_minimizeOnLaunch; }
void AppConfig::setMinimizeOnLaunch(bool on) { m_minimizeOnLaunch = on; save(); }

QString AppConfig::updateRepo() const { return m_updateRepo; }
void AppConfig::setUpdateRepo(const QString &repo) { m_updateRepo = repo; save(); }

QString AppConfig::buildSourceDir() const { return m_buildSourceDir; }
void AppConfig::setBuildSourceDir(const QString &dir)
{
    m_buildSourceDir = QDir::toNativeSeparators(dir);
    save();
    emit changed();
}

QString AppConfig::patchFile() const { return m_patchFile; }
void AppConfig::setPatchFile(const QString &path)
{
    m_patchFile = QDir::toNativeSeparators(path);
    save();
}

QString AppConfig::mingwRoot() const { return m_mingwRoot; }
void AppConfig::setMingwRoot(const QString &dir)
{
    m_mingwRoot = QDir::toNativeSeparators(dir);
    save();
}

QString AppConfig::gitExe() const { return m_gitExe; }
void AppConfig::setGitExe(const QString &path) { m_gitExe = path; save(); }

QString AppConfig::lastSeenCommit() const { return m_lastSeenCommit; }
void AppConfig::setLastSeenCommit(const QString &sha) { m_lastSeenCommit = sha; save(); }
