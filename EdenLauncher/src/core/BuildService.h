#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

// 在线构建：从上游拉取最新源码 → 应用 NS2 补丁 → cmake/ninja 编译 → 部署 exe
class BuildService : public QObject
{
    Q_OBJECT

public:
    enum class Stage {
        Idle,
        Preparing,
        Fetching,
        Patching,
        Configuring,
        Building,
        Deploying,
        Done,
        Failed
    };
    Q_ENUM(Stage)

    explicit BuildService(QObject *parent = nullptr);
    ~BuildService() override;

    void start();
    void cancel();

    bool isBusy() const;
    Stage stage() const { return m_stage; }
    const QString &builtOutputDir() const { return m_outputDir; }
    int progressPercent() const { return m_percent; }

signals:
    void log(const QString &line);
    void stageChanged(BuildService::Stage stage, const QString &text);
    void progressChanged(int percent);
    void finished(bool ok, const QString &message);

private:
    void advance();
    void run(const QString &program, const QStringList &args, const QString &workDir,
             const QString &label, bool critical);
    void onProcessOutput();
    void onProcessFinished(int exitCode, bool crashed);
    void fail(const QString &reason);
    void setStage(Stage stage, const QString &text);
    void setProgress(int percent);

    QProcess *m_proc = nullptr;
    Stage m_stage = Stage::Idle;
    int m_step = 0;
    int m_percent = 0;
    bool m_cancelled = false;
    bool m_criticalStep = true;
    QString m_outputDir;
    QString m_sourceDir;
    QString m_buildDir;
};
