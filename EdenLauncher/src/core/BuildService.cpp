#include "BuildService.h"

#include "AppConfig.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>

namespace {

QString toNative(const QString &p) { return QDir::toNativeSeparators(p); }

} // namespace

BuildService::BuildService(QObject *parent)
    : QObject(parent)
{
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &BuildService::onProcessOutput);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
                onProcessFinished(code, status == QProcess::CrashExit);
            });
}

BuildService::~BuildService()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
}

bool BuildService::isBusy() const
{
    return m_stage != Stage::Idle && m_stage != Stage::Done && m_stage != Stage::Failed;
}

void BuildService::start()
{
    if (isBusy())
        return;

    AppConfig &cfg = AppConfig::instance();
    m_sourceDir = cfg.buildSourceDir();
    m_buildDir = m_sourceDir + "/build-launcher";
    m_step = 0;
    m_cancelled = false;
    m_percent = 0;
    setStage(Stage::Preparing, tr("准备构建环境"));
    setProgress(2);
    advance();
}

void BuildService::cancel()
{
    if (!isBusy())
        return;
    m_cancelled = true;
    if (m_proc->state() != QProcess::NotRunning) {
        emit log(tr("正在取消构建…"));
        m_proc->kill();
    }
}

void BuildService::setStage(Stage stage, const QString &text)
{
    m_stage = stage;
    emit stageChanged(stage, text);
}

void BuildService::setProgress(int percent)
{
    if (percent == m_percent)
        return;
    m_percent = qBound(0, percent, 100);
    emit progressChanged(m_percent);
}

void BuildService::fail(const QString &reason)
{
    emit log(QStringLiteral("✗ ") + reason);
    setStage(Stage::Failed, tr("构建失败"));
    emit finished(false, reason);
}

void BuildService::run(const QString &program, const QStringList &args, const QString &workDir,
                       const QString &label, bool critical)
{
    m_criticalStep = critical;
    emit log(QStringLiteral("» %1").arg(label));
    emit log(QStringLiteral("  %1 %2").arg(program, args.join(' ')));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString mingwRoot = AppConfig::instance().mingwRoot();
    const QString mingwBin = mingwRoot + "/bin";
    env.insert(QStringLiteral("PATH"), mingwBin + ";" + env.value(QStringLiteral("PATH")));
    env.insert(QStringLiteral("MSYSTEM"), QStringLiteral("MINGW64"));
    env.insert(QStringLiteral("CHERE_INVOKING"), QStringLiteral("1"));
    // 让 Ninja / CMake 的 curl 找到 CA 证书，否则 CPM 下载依赖会因证书校验失败
    const QString caBundle = mingwRoot + "/etc/ssl/certs/ca-bundle.crt";
    if (QFileInfo::exists(caBundle)) {
        env.insert(QStringLiteral("CURL_CA_BUNDLE"), toNative(caBundle));
        env.insert(QStringLiteral("SSL_CERT_FILE"), toNative(caBundle));
    }

    // git / curl(CMake FetchContent) 都不会自动读取 Windows 的系统代理设置，
    // 这里主动把系统代理注入子进程，否则 libgav1 等依赖根本拉不下来
    const QSettings inet(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"),
        QSettings::NativeFormat);
    if (inet.value(QStringLiteral("ProxyEnable")).toInt() == 1) {
        QString server = inet.value(QStringLiteral("ProxyServer")).toString().trimmed();
        if (!server.isEmpty()) {
            if (!server.contains(QLatin1String("://")))
                server.prepend(QLatin1String("http://"));
            for (const char *key : {"HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY",
                                    "http_proxy", "https_proxy", "all_proxy"})
                env.insert(QString::fromLatin1(key), server);
            env.insert(QStringLiteral("NO_PROXY"), QStringLiteral("localhost,127.0.0.1,::1"));
            env.insert(QStringLiteral("no_proxy"), QStringLiteral("localhost,127.0.0.1,::1"));
            emit log(tr("使用系统代理：%1").arg(server));
        }
    }

    m_proc->setWorkingDirectory(workDir);
    m_proc->setProcessEnvironment(env);
    m_proc->setProgram(program);
    m_proc->setArguments(args);
    m_proc->start();

    if (!m_proc->waitForStarted(15000)) {
        if (critical) {
            fail(tr("%1 启动失败：%2").arg(label, m_proc->errorString()));
        } else {
            emit log(tr("%1 启动失败，已跳过").arg(label));
            advance();
        }
    }
}

void BuildService::onProcessOutput()
{
    const QByteArray data = m_proc->readAllStandardOutput();
    const QString text = QString::fromUtf8(data);
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        emit log(trimmed);

        if (m_stage == Stage::Building && trimmed.startsWith(QLatin1String("["))) {
            // ninja 形如 [123/456] Building ...
            const int slash = trimmed.indexOf('/');
            const int close = trimmed.indexOf(']');
            if (slash > 1 && close > slash) {
                bool ok = false;
                const int cur = trimmed.mid(1, slash - 1).toInt(&ok);
                const int total = trimmed.mid(slash + 1, close - slash - 1).toInt(&ok);
                if (ok && total > 0)
                    setProgress(60 + int(38.0 * cur / total));
            }
        }
    }
}

void BuildService::onProcessFinished(int exitCode, bool crashed)
{
    if (m_cancelled) {
        setStage(Stage::Idle, tr("已取消"));
        emit finished(false, tr("构建已取消"));
        return;
    }

    if (crashed || exitCode != 0) {
        if (m_criticalStep) {
            fail(tr("步骤失败（退出码 %1），详见日志").arg(exitCode));
            return;
        }
        emit log(tr("该步骤返回 %1，已忽略并继续").arg(exitCode));
    }

    advance();
}

void BuildService::advance()
{
    AppConfig &cfg = AppConfig::instance();
    const QString git = cfg.gitExe();
    const QString mingw = cfg.mingwRoot();
    const QString repo = cfg.updateRepo();

    switch (m_step++) {
    case 0: {
        // 创建工作区
        QDir().mkpath(m_sourceDir);
        if (!QFileInfo::exists(m_sourceDir + "/.git")) {
            if (QFileInfo::exists(m_sourceDir + "/CMakeLists.txt")) {
                // 已有非 git 源码，直接使用
                emit log(tr("检测到已有源码目录（非 git），跳过克隆"));
                m_step = 3;
                advance();
                return;
            }
            setStage(Stage::Fetching, tr("克隆上游仓库"));
            setProgress(5);
            QDir().rmdir(m_sourceDir);
            run(git,
                {QStringLiteral("clone"), QStringLiteral("--depth"),
                 QStringLiteral("1"), QStringLiteral("--single-branch"),
                 QStringLiteral("https://github.com/%1.git").arg(repo), m_sourceDir},
                QFileInfo(m_sourceDir).absolutePath(),
                tr("git clone %1").arg(repo), true);
            return;
        }
        setStage(Stage::Fetching, tr("拉取上游最新提交"));
        setProgress(5);
        run(git, {QStringLiteral("fetch"), QStringLiteral("--depth"), QStringLiteral("1"),
                  QStringLiteral("origin")},
            m_sourceDir, tr("git fetch origin"), false);
        return;
    }
    case 1: {
        if (!m_sourceDir.isEmpty() && QFileInfo::exists(m_sourceDir + "/.git")) {
            run(git, {QStringLiteral("reset"), QStringLiteral("--hard"),
                      QStringLiteral("FETCH_HEAD")},
                m_sourceDir, tr("git reset --hard FETCH_HEAD"), false);
            return;
        }
        advance();
        return;
    }
    case 2: {
        if (!m_sourceDir.isEmpty() && QFileInfo::exists(m_sourceDir + "/.git")) {
            // 保留构建目录与 CPM 依赖缓存，否则每次都要重新下载几百 MB 依赖
            run(git, {QStringLiteral("clean"), QStringLiteral("-fdx"),
                      QStringLiteral("-e"), QStringLiteral("build-launcher"),
                      QStringLiteral("-e"), QStringLiteral(".cache")},
                m_sourceDir, tr("git clean"), false);
            return;
        }
        advance();
        return;
    }
    case 3: {
        // 应用 NS2 补丁
        const QString patch = cfg.patchFile();
        if (patch.isEmpty() || !QFileInfo::exists(patch)) {
            emit log(tr("未找到 NS2 补丁，将构建标准版 Eden"));
            advance();
            return;
        }
        setStage(Stage::Patching, tr("应用 NS2 补丁"));
        setProgress(12);
        run(git, {QStringLiteral("apply"), QStringLiteral("--check"), patch},
            m_sourceDir, tr("检查 NS2 补丁可应用性"), false);
        return;
    }
    case 4: {
        const QString patch = cfg.patchFile();
        m_criticalStep = false;
        emit log(tr("» 应用 NS2 补丁"));
        run(git, {QStringLiteral("apply"), patch}, m_sourceDir, tr("git apply NS2.patch"), false);
        return;
    }
    case 5: {
        setStage(Stage::Configuring, tr("配置 CMake"));
        setProgress(16);
        const QString cmake = mingw + "/bin/cmake.exe";
        // 编译器等路径必须以正斜杠传给 CMake：反斜杠会被原样写进
        // CMakeRCCompiler.cmake，触发 “Invalid character escape '\d'”
        const QString unixRoot = QDir::fromNativeSeparators(mingw);
        run(cmake,
            {QStringLiteral("-S"), m_sourceDir,
             QStringLiteral("-B"), m_buildDir,
             QStringLiteral("-G"), QStringLiteral("Ninja"),
             QStringLiteral("-DCMAKE_BUILD_TYPE=Release"),
             QStringLiteral("-DCMAKE_C_COMPILER=%1").arg(unixRoot + "/bin/gcc.exe"),
             QStringLiteral("-DCMAKE_CXX_COMPILER=%1").arg(unixRoot + "/bin/g++.exe"),
             QStringLiteral("-DCMAKE_MAKE_PROGRAM=%1").arg(unixRoot + "/bin/ninja.exe"),
             QStringLiteral("-DCMAKE_RC_COMPILER=%1").arg(unixRoot + "/bin/windres.exe"),
             QStringLiteral("-DCMAKE_PREFIX_PATH=%1").arg(unixRoot),
             // CPM 通过 curl 拉取依赖，MinGW 环境常缺 CA 配置，关掉 TLS 校验才能下得动
             QStringLiteral("-DCMAKE_TLS_VERIFY=OFF"),
             QStringLiteral("-DENABLE_QT=ON"),
             QStringLiteral("-DCMAKE_CXX_STANDARD=20"),
             QStringLiteral("-DYUZU_CMD=ON"),
             QStringLiteral("-DENABLE_UPDATE_CHECKER=OFF"),
             QStringLiteral("-DENABLE_QT_TRANSLATION=OFF"),
             QStringLiteral("-DENABLE_LIBUSB=OFF"),
             QStringLiteral("-DENABLE_WEB_SERVICE=OFF"),
             // 上游默认 -Werror，MinGW 的警告更多，必须关掉否则编译直接失败
             QStringLiteral("-DENABLE_WERROR=OFF"),
             // 上游 src/tests 与核心代码不同步，直接编译不过；且启动器不需要测试
             QStringLiteral("-DBUILD_TESTING=OFF"),
             QStringLiteral("-DYUZU_TESTS=OFF")},
            m_sourceDir, tr("cmake 配置"), true);
        return;
    }
    case 6: {
        setStage(Stage::Building, tr("编译中（可能需要较长时间）"));
        setProgress(60);
        const QString cmake = mingw + "/bin/cmake.exe";
        // 单个编译单元（stb / 大型头文件）在 -O3 下可吃掉 1~2 GB 内存，
        // 并行度按内存保守限制，避免 cc1plus 被系统杀掉后报莫名字节码错误
        const int jobs = qBound(2, QThread::idealThreadCount() / 2, 8);
        run(cmake, {QStringLiteral("--build"), m_buildDir,
                    QStringLiteral("--parallel"), QString::number(jobs)},
            m_sourceDir, tr("cmake --build"), true);
        return;
    }
    case 7: {
        setStage(Stage::Deploying, tr("部署新版本"));
        setProgress(98);

        QString binDir;
        for (const QString &candidate : {m_buildDir + "/bin", m_buildDir + "/Release"}) {
            if (QFileInfo::exists(candidate + "/eden.exe")) {
                binDir = candidate;
                break;
            }
        }
        if (binDir.isEmpty()) {
            fail(tr("编译完成但未找到 eden.exe"));
            return;
        }
        m_outputDir = binDir;

        const QString runtime = cfg.runtimeDir();
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
        int copied = 0;
        for (const QString &name : {QStringLiteral("eden.exe"), QStringLiteral("eden-cli.exe")}) {
            const QString src = binDir + "/" + name;
            const QString dst = runtime + "/" + name;
            if (!QFileInfo::exists(src))
                continue;
            if (QFileInfo::exists(dst)) {
                const QString backup = dst + "." + stamp + ".bak";
                QFile::remove(backup);
                QFile::rename(dst, backup);
                emit log(tr("已备份旧版本 → %1").arg(backup));
            }
            if (QFile::copy(src, dst)) {
                ++copied;
                emit log(tr("已部署 %1").arg(dst));
            } else {
                emit log(tr("复制失败：%1").arg(dst));
            }
        }

        if (copied == 0) {
            fail(tr("没有成功部署任何二进制"));
            return;
        }

        setProgress(100);
        setStage(Stage::Done, tr("构建完成"));
        emit finished(true, tr("新版本已部署到运行时目录"));
        return;
    }
    default:
        return;
    }
}
