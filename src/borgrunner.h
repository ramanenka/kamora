#pragma once

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>

/// One borg invocation inside a backup run.
struct BorgStep {
    QString label;
    QStringList args;
    /// borg prints a JSON summary on stdout for this step (borg create --json).
    bool jsonSummary = false;
};

/**
 * Runs a queue of borg commands and turns `--log-json` output into progress.
 */
class BorgRunner : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString stepLabel READ stepLabel NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

public:
    explicit BorgRunner(QObject *parent = nullptr);

    bool running() const;
    QString stepLabel() const;
    QString progressText() const;
    /// Fraction between 0 and 1, or -1 while the total is unknown.
    qreal progress() const;

    void run(const QList<BorgStep> &steps, const QProcessEnvironment &environment);
    Q_INVOKABLE void cancel();

    /// Absolute path of the borg executable, empty when it is not installed.
    static QString borgExecutable();

Q_SIGNALS:
    void runningChanged();
    void progressChanged();
    void logLine(const QString &line);
    void finished(bool ok, const QString &message, const QString &archiveName);

private:
    void runNext();
    void readStandardError();
    void readStandardOutput();
    void handleJsonLine(const QByteArray &line);
    void stepFinished(int exitCode, QProcess::ExitStatus status);
    void finishRun(bool ok, const QString &message);
    void setProgress(const QString &text, qreal value);

    QProcess m_process;
    QList<BorgStep> m_steps;
    int m_currentStep = -1;
    QByteArray m_stderrBuffer;
    QByteArray m_stdoutBuffer;
    QString m_stepLabel;
    QString m_progressText;
    qreal m_progress = -1;
    QString m_archiveName;
    QString m_lastError;
    bool m_sawWarning = false;
    bool m_cancelled = false;
    bool m_running = false;
};
