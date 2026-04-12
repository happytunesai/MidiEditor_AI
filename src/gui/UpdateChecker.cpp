#include "UpdateChecker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QVersionNumber>
#include <QUrl>
#include <QNetworkRequest>
#include <QRegularExpression>


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

void UpdateChecker::fetchChangelog(const QString &version)
{
    _pendingChangelogVersion = version;

    // Use a separate manager so the changelog response doesn't go through onResult()
    auto *clManager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl("https://midieditor-ai.de/changelog.html"));
    request.setHeader(QNetworkRequest::UserAgentHeader, "MidiEditor AI");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = clManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, clManager]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Changelog fetch failed:" << reply->errorString();
            emit changelogFetchFailed();
            reply->deleteLater();
            clManager->deleteLater();
            return;
        }

        QString html = QString::fromUtf8(reply->readAll());
        ChangelogSummary summary = parseChangelogForVersion(html, _pendingChangelogVersion);
        emit changelogReady(summary);
        reply->deleteLater();
        clManager->deleteLater();
    });
}

ChangelogSummary UpdateChecker::parseChangelogForVersion(const QString &html, const QString &version)
{
    ChangelogSummary result;
    result.version = version;

    // Find the <article> block for this version using data-version attribute
    // Pattern: <article class="cl-version..." data-version="X.Y.Z">
    QString versionEscaped = QRegularExpression::escape(version);
    QRegularExpression articleRe(
        QString("<article[^>]*data-version=\"%1\"[^>]*>(.*?)</article>").arg(versionEscaped),
        QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatch articleMatch = articleRe.match(html);
    if (!articleMatch.hasMatch()) {
        return result;
    }
    QString articleHtml = articleMatch.captured(1);

    // Extract the title from <div class="cl-title">...</div>
    QRegularExpression titleRe("<div class=\"cl-title\">(.*?)</div>");
    QRegularExpressionMatch titleMatch = titleRe.match(articleHtml);
    if (titleMatch.hasMatch()) {
        result.title = titleMatch.captured(1);
        // Decode HTML entities
        result.title.replace("&amp;", "&");
        result.title.replace("&lt;", "<");
        result.title.replace("&gt;", ">");
        result.title.replace("&mdash;", "-");
        result.title.replace("&ndash;", "-");
    }

    // Extract summary bullets from <ul class="cl-summary"><li>...</li>...</ul>
    QRegularExpression summaryRe(
        "<ul class=\"cl-summary\">(.*?)</ul>",
        QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch summaryMatch = summaryRe.match(articleHtml);
    if (summaryMatch.hasMatch()) {
        QString summaryHtml = summaryMatch.captured(1);

        // Extract each <li>...</li>
        QRegularExpression liRe("<li>(.*?)</li>", QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatchIterator liIt = liRe.globalMatch(summaryHtml);
        while (liIt.hasNext()) {
            QRegularExpressionMatch liMatch = liIt.next();
            QString bullet = liMatch.captured(1);
            // Strip HTML tags for plain text display
            bullet.replace(QRegularExpression("<[^>]*>"), "");
            // Decode HTML entities
            bullet.replace("&amp;", "&");
            bullet.replace("&lt;", "<");
            bullet.replace("&gt;", ">");
            bullet.replace("&mdash;", "-");
            bullet.replace("&ndash;", "-");
            bullet.replace("&hellip;", "...");
            bullet.replace("&quot;", "\"");
            bullet = bullet.trimmed();
            if (!bullet.isEmpty()) {
                result.bullets.append(bullet);
            }
        }
    }

    return result;
}
