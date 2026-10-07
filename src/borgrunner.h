#pragma once

#include <QJsonObject>
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
    bool estimatesSize = false;
};

/**
 * Runs a queue of borg commands and turns `--log-json` output into progress.
 */
class BorgRunner : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString stepLabel READ stepLabel NOTIFY stepLabelChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

public:
    explicit BorgRunner(QObject *parent = nullptr);

    bool running() const;
    bool cancelled() const;
    QString stepLabel() const;
    QString progressText() const;
    /// Fraction between 0 and 1, or -1 while the total is unknown.
    qreal progress() const;

    void run(const QList<BorgStep> &steps, const QProcessEnvironment &environment);
    Q_INVOKABLE void cancel();

    /// Absolute path of the borg executable, empty when it is not installed.
    static QString borgExecutable();

    /**
     * Boils one of borg's failures down to the line worth showing.
     *
     * borg reports a failure it did not recognise as a whole Python traceback;
     * the exception at the end of it is the part that means something to the
     * user, and the rest belongs in the log.
     */
    static QString condenseMessage(const QString &message);

Q_SIGNALS:
    void runningChanged();
    void progressChanged();
    void logLine(const QString &line);
    void finished(bool ok, const QString &message, const QString &archiveName);
    void estimateProgress(qint64 bytes, qint64 files);
    void archiveProgress(qint64 bytes, qint64 newBytes, qint64 files, const QString &path);
    void stepLabelChanged(const QString &label);
    void percentProgress(qint64 current, qint64 total);

private:
    void runNext();
    void readStandardError();
    void readStandardOutput();
    void handleJsonLine(const QByteArray &line);
    void addToEstimate(const QJsonObject &status);
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
    qint64 m_expectedBytes = 0;
    qint64 m_estimateBytes = 0;
    qint64 m_estimateFiles = 0;
    QString m_lastError;
    bool m_sawWarning = false;
    bool m_cancelled = false;
    bool m_running = false;
};
