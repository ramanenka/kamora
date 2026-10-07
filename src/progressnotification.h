#pragma once

#include <QElapsedTimer>

#include <KJob>

class ProgressNotification : public KJob
{
    Q_OBJECT

public:
    explicit ProgressNotification(const QString &title, QObject *parent = nullptr);

    void start() override;

    void showStep(const QString &label);
    void showEstimate(qint64 bytes, qint64 files);
    void showPercent(qint64 current, qint64 total);
    void showProgress(qint64 bytes, qint64 newBytes, qint64 files, const QString &path);
    void finish(bool ok, bool cancelled, const QString &errorText, const QString &archiveName);

private:
    QString m_title;
    qint64 m_totalBytes = 0;
    qint64 m_totalFiles = 0;
    QElapsedTimer m_speedTimer;
    qint64 m_speedBytes = 0;
};
