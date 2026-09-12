#include "borgrunner.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStandardPaths>

#include <KLocalizedString>

using namespace Qt::StringLiterals;

namespace
{
/// borg reports failures as a whole Python traceback. The exception line at
/// the end of it is the part worth putting in front of the user; the rest
/// stays in the log.
QString condenseError(const QString &message)
{
    static const QStringList trailerKeys = {
        u"Platform:"_s, u"Linux:"_s, u"Borg:"_s, u"PID:"_s, u"CWD:"_s,
        u"sys.argv:"_s, u"SSH_ORIGINAL_COMMAND:"_s, u"Traceback"_s,
    };

    const QStringList lines = message.split(u'\n', Qt::SkipEmptyParts);
    QString candidate;
    for (const QString &line : lines) {
        if (line.startsWith(u' ') || line.startsWith(u'\t') || line.startsWith(u"File \""_s)) {
            continue;
        }
        bool isTrailer = false;
        for (const QString &key : trailerKeys) {
            if (line.startsWith(key)) {
                isTrailer = true;
                break;
            }
        }
        if (!isTrailer) {
            candidate = line.trimmed();
        }
    }
    if (candidate.isEmpty()) {
        candidate = message.trimmed();
    }
    // Drop the module path in front of the exception name.
    const qsizetype colon = candidate.indexOf(u": "_s);
    if (colon > 0) {
        const QString type = candidate.left(colon);
        if (type.contains(u'.') && !type.contains(u' ')) {
            candidate = type.section(u'.', -1) + candidate.mid(colon);
        }
    }
    constexpr qsizetype maximumLength = 400;
    if (candidate.size() > maximumLength) {
        candidate = candidate.left(maximumLength) + u"…"_s;
    }
    return candidate;
}
}

BorgRunner::BorgRunner(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process, &QProcess::readyReadStandardError, this, &BorgRunner::readStandardError);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &BorgRunner::readStandardOutput);
    connect(&m_process, &QProcess::finished, this, &BorgRunner::stepFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_lastError = i18n("borg could not be started");
        }
    });
}

QString BorgRunner::borgExecutable()
{
    return QStandardPaths::findExecutable(u"borg"_s);
}

bool BorgRunner::running() const
{
    return m_running;
}

QString BorgRunner::stepLabel() const
{
    return m_stepLabel;
}

QString BorgRunner::progressText() const
{
    return m_progressText;
}

qreal BorgRunner::progress() const
{
    return m_progress;
}

void BorgRunner::setProgress(const QString &text, qreal value)
{
    if (m_progressText == text && qFuzzyCompare(m_progress + 2, value + 2)) {
        return;
    }
    m_progressText = text;
    m_progress = value;
    Q_EMIT progressChanged();
}

void BorgRunner::run(const QList<BorgStep> &steps, const QProcessEnvironment &environment)
{
    if (m_running) {
        return;
    }
    m_steps = steps;
    m_currentStep = -1;
    m_archiveName.clear();
    m_lastError.clear();
    m_sawWarning = false;
    m_cancelled = false;
    m_running = true;
    m_process.setProcessEnvironment(environment);
    Q_EMIT runningChanged();
    runNext();
}

void BorgRunner::runNext()
{
    ++m_currentStep;
    if (m_currentStep >= m_steps.size()) {
        finishRun(true, m_sawWarning ? i18n("Backup finished with warnings") : i18n("Backup finished"));
        return;
    }

    const BorgStep &step = m_steps.at(m_currentStep);
    m_stepLabel = step.label;
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    setProgress(step.label, -1);

    const QString borg = borgExecutable();
    Q_EMIT logLine(u"$ borg "_s + step.args.join(u' '));
    m_process.start(borg, step.args);
}

void BorgRunner::cancel()
{
    if (!m_running) {
        return;
    }
    m_cancelled = true;
    m_steps.clear();
    Q_EMIT logLine(i18n("Cancelling…"));
    m_process.terminate();
    if (!m_process.waitForFinished(5000)) {
        m_process.kill();
    }
}

void BorgRunner::finishRun(bool ok, const QString &message)
{
    m_running = false;
    m_stepLabel.clear();
    setProgress(QString(), -1);
    // finished() goes out before runningChanged() so that whoever records the
    // result has done so by the time anything reacts to the runner idling -
    // otherwise the backup still looks due and a second run starts right away.
    Q_EMIT finished(ok, message, m_archiveName);
    Q_EMIT runningChanged();
}

void BorgRunner::readStandardOutput()
{
    m_stdoutBuffer.append(m_process.readAllStandardOutput());
}

void BorgRunner::readStandardError()
{
    m_stderrBuffer.append(m_process.readAllStandardError());
    int newline = -1;
    while ((newline = m_stderrBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stderrBuffer.left(newline);
        m_stderrBuffer.remove(0, newline + 1);
        if (!line.trimmed().isEmpty()) {
            handleJsonLine(line);
        }
    }
}

void BorgRunner::handleJsonLine(const QByteArray &line)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        Q_EMIT logLine(QString::fromUtf8(line));
        return;
    }

    const QJsonObject object = document.object();
    const QString type = object.value(u"type"_s).toString();

    if (type == u"archive_progress"_s) {
        if (object.value(u"finished"_s).toBool()) {
            return;
        }
        const qint64 original = static_cast<qint64>(object.value(u"original_size"_s).toDouble());
        const qint64 deduplicated = static_cast<qint64>(object.value(u"deduplicated_size"_s).toDouble());
        const int files = object.value(u"nfiles"_s).toInt();
        const QString path = object.value(u"path"_s).toString();
        const QLocale locale;
        setProgress(i18nc("@info:progress read / stored / file count",
                          "%1 read, %2 new, %3 files — %4",
                          locale.formattedDataSize(original),
                          locale.formattedDataSize(deduplicated),
                          QString::number(files),
                          path),
                    -1);
        return;
    }

    if (type == u"progress_percent"_s) {
        const double current = object.value(u"current"_s).toDouble();
        const double total = object.value(u"total"_s).toDouble();
        const QString message = object.value(u"message"_s).toString();
        if (object.value(u"finished"_s).toBool()) {
            setProgress(m_stepLabel, -1);
            return;
        }
        setProgress(message.isEmpty() ? m_stepLabel : message,
                    total > 0 ? qBound(0.0, current / total, 1.0) : -1);
        return;
    }

    if (type == u"progress_message"_s) {
        const QString message = object.value(u"message"_s).toString();
        if (!message.isEmpty()) {
            setProgress(message, m_progress);
        }
        return;
    }

    if (type == u"log_message"_s) {
        const QString level = object.value(u"levelname"_s).toString();
        const QString message = object.value(u"message"_s).toString();
        if (level == u"WARNING"_s) {
            m_sawWarning = true;
        } else if (level == u"ERROR"_s || level == u"CRITICAL"_s) {
            m_lastError = condenseError(message);
        }
        Q_EMIT logLine(level + u": "_s + message);
        return;
    }

    Q_EMIT logLine(QString::fromUtf8(line));
}

void BorgRunner::stepFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_stdoutBuffer.trimmed().isEmpty()) {
        const QJsonDocument document = QJsonDocument::fromJson(m_stdoutBuffer);
        if (document.isObject()) {
            const QJsonObject archive = document.object().value(u"archive"_s).toObject();
            const QString name = archive.value(u"name"_s).toString();
            if (!name.isEmpty()) {
                m_archiveName = name;
            }
        } else {
            Q_EMIT logLine(QString::fromUtf8(m_stdoutBuffer.trimmed()));
        }
        m_stdoutBuffer.clear();
    }

    if (m_cancelled) {
        finishRun(false, i18n("Backup cancelled"));
        return;
    }

    if (status == QProcess::CrashExit) {
        finishRun(false, i18n("borg stopped unexpectedly"));
        return;
    }

    // borg uses exit code 1 for warnings that leave a usable archive behind.
    if (exitCode > 1) {
        finishRun(false, m_lastError.isEmpty() ? i18n("borg exited with code %1", exitCode) : m_lastError);
        return;
    }
    if (exitCode == 1) {
        m_sawWarning = true;
    }

    runNext();
}
