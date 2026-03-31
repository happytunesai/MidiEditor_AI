#include "UpdateChecker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QVersionNumber>
#include <QUrl>
#include <QNetworkRequest>


UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent)
{
    manager = new QNetworkAccessManager(this);
    connect(manager, &QNetworkAccessManager::finished, this, &UpdateChecker::onResult);
}

void UpdateChecker::checkForUpdates()
{
    QNetworkRequest request(QUrl("https://api.github.com/repos/happytunesai/MidiEditor_AI/releases/latest"));
    // GitHub API requires a User-Agent
    request.setHeader(QNetworkRequest::UserAgentHeader, "MidiEditor AI");
    manager->get(request);
}

void UpdateChecker::onResult(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();

    QString tagName = obj["tag_name"].toString();
    // Remove 'v' prefix if present
    if (tagName.startsWith("v")) {
        tagName = tagName.mid(1);
    }

    QString currentVersionStr = QCoreApplication::applicationVersion();
    // Remove 'v' prefix from current version if present (just in case)
    if (currentVersionStr.startsWith("v")) {
        currentVersionStr = currentVersionStr.mid(1);
    }

    QVersionNumber currentVersion = QVersionNumber::fromString(currentVersionStr);
    QVersionNumber latestVersion = QVersionNumber::fromString(tagName);

    if (latestVersion > currentVersion) {
        // Parse assets array to find the ZIP download URL
        QString zipDownloadUrl;
        qint64 zipSize = 0;
        QJsonArray assets = obj["assets"].toArray();
        for (const QJsonValue &assetVal : assets) {
            QJsonObject asset = assetVal.toObject();
            QString name = asset["name"].toString();
            if (name.contains("win64") && name.endsWith(".zip")) {
                zipDownloadUrl = asset["browser_download_url"].toString();
                zipSize = asset["size"].toVariant().toLongLong();
                break;
            }
        }
        emit updateAvailable(tagName, obj["html_url"].toString(), zipDownloadUrl, zipSize);
    } else {
        emit noUpdateAvailable();
    }

    reply->deleteLater();
}
