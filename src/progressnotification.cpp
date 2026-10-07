#include "progressnotification.h"

#include <QCoreApplication>
#include <QLocale>

#include <KLocalizedString>
#include <KUiServerV2JobTracker>

ProgressNotification::ProgressNotification(const QString &title, QObject *parent)
    : KJob(parent)
    , m_title(title)
{
    setCapabilities(KJob::NoCapabilities);

    static KUiServerV2JobTracker *tracker = new KUiServerV2JobTracker(QCoreApplication::instance());
    tracker->registerJob(this);
    Q_EMIT description(this, m_title);
}

void ProgressNotification::start()
{
}

void ProgressNotification::showStep(const QString &label)
{
    setProcessedAmount(KJob::Bytes, 0);
    setTotalAmount(KJob::Bytes, 0);
    setProcessedAmount(KJob::Files, 0);
    setTotalAmount(KJob::Files, 0);
    setPercent(0);
    m_speedTimer.invalidate();
    emitSpeed(0);
    Q_EMIT description(this, m_title, {i18n("Step"), label});
}

void ProgressNotification::showEstimate(qint64 bytes, qint64 files)
{
    m_totalBytes = bytes;
    m_totalFiles = files;
    Q_EMIT description(this,
                       m_title,
                       {i18n("Counting files"),
                        i18nc("@info:progress file count / total size",
                              "%1 files, %2",
                              QString::number(files),
                              QLocale().formattedDataSize(bytes))});
}

void ProgressNotification::showPercent(qint64 current, qint64 total)
{
    if (total > 0) {
        setPercent(static_cast<unsigned long>(qBound(qint64(1), current * 100 / total, qint64(100))));
    }
}

void ProgressNotification::showProgress(qint64 bytes, qint64 newBytes, qint64 files, const QString &path)
{
    if (m_totalBytes > 0) {
        setTotalAmount(KJob::Bytes, bytes < m_totalBytes ? m_totalBytes : bytes + bytes / 99);
        setTotalAmount(KJob::Files, qMax(m_totalFiles, files));
    }
    setProcessedAmount(KJob::Files, files);
    setProcessedAmount(KJob::Bytes, bytes);
    if (!m_speedTimer.isValid()) {
        m_speedTimer.start();
        m_speedBytes = bytes;
    } else if (m_speedTimer.elapsed() >= 1000) {
        emitSpeed(static_cast<unsigned long>(qMax(qint64(0), bytes - m_speedBytes) * 1000 / m_speedTimer.restart()));
        m_speedBytes = bytes;
    }
    Q_EMIT description(this,
                       m_title,
                       {i18n("File"), path},
                       {i18n("New"), QLocale().formattedDataSize(newBytes)});
}

void ProgressNotification::finish(bool ok, bool cancelled, const QString &errorText, const QString &archiveName)
{
    if (cancelled) {
        setError(KJob::KilledJobError);
    } else if (!ok) {
        setError(KJob::UserDefinedError);
        setErrorText(errorText);
    } else if (!archiveName.isEmpty()) {
        Q_EMIT description(this, m_title, {i18n("Archive"), archiveName});
    }
    emitResult();
}
