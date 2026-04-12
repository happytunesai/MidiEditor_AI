#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

/**
 * Parsed changelog summary for a single version.
 */
struct ChangelogSummary {
    QString version;
    QString title;
    QStringList bullets;
};

class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);
    void checkForUpdates();

    /**
     * Fetch changelog.html from the website and extract the summary bullets
     * for the given version. Emits changelogReady() when done.
     */
    void fetchChangelog(const QString &version);

    /**
     * Parse changelog HTML content and extract summary for a specific version.
     * Static so it can be used without a network fetch (e.g. from cached data).
     */
    static ChangelogSummary parseChangelogForVersion(const QString &html, const QString &version);

signals:
    void updateAvailable(QString version, QString releaseUrl, QString zipDownloadUrl, qint64 zipSize);
    void noUpdateAvailable();
    void errorOccurred(QString error);
    void changelogReady(ChangelogSummary summary);
    void changelogFetchFailed();

private slots:
    void onResult(QNetworkReply *reply);

private:
    QNetworkAccessManager *manager;
    QString _pendingChangelogVersion;
};

#endif // UPDATECHECKER_H
