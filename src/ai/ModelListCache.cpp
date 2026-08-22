#include "ModelListCache.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

QString ModelListCache::cacheFilePath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return base + QStringLiteral("/midipilot_models.json");
}

QJsonObject ModelListCache::readFile()
{
    QFile f(cacheFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return QJsonObject();
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return QJsonObject();
    QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("version")).toInt() != CACHE_VERSION)
        return QJsonObject();
    return obj;
}

bool ModelListCache::writeFile(const QJsonObject &obj)
{
    QFile f(cacheFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    const bool ok = f.write(json) == json.size();
    f.close();
    return ok;
}

QJsonArray ModelListCache::models(const QString &provider)
{
    QJsonObject root = readFile();
    QJsonObject providers = root.value(QStringLiteral("providers")).toObject();
    QJsonObject entry = providers.value(provider).toObject();
    return entry.value(QStringLiteral("models")).toArray();
}

QDateTime ModelListCache::lastFetched(const QString &provider)
{
    QJsonObject root = readFile();
    QJsonObject providers = root.value(QStringLiteral("providers")).toObject();
    QJsonObject entry = providers.value(provider).toObject();
    QString iso = entry.value(QStringLiteral("fetched_at")).toString();
    return QDateTime::fromString(iso, Qt::ISODate);
}

bool ModelListCache::isStale(const QString &provider)
{
    QDateTime ts = lastFetched(provider);
    if (!ts.isValid())
        return true;
    return ts.daysTo(QDateTime::currentDateTimeUtc()) >= TTL_DAYS;
}

void ModelListCache::store(const QString &provider, const QJsonArray &models)
{
    QJsonObject root = readFile();
    if (root.isEmpty())
        root.insert(QStringLiteral("version"), CACHE_VERSION);

    QJsonObject providers = root.value(QStringLiteral("providers")).toObject();
    QJsonObject entry;
    entry.insert(QStringLiteral("fetched_at"),
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    entry.insert(QStringLiteral("models"), models);
    providers.insert(provider, entry);
    root.insert(QStringLiteral("providers"), providers);

    writeFile(root);
}

bool ModelListCache::forget(const QString &scope)
{
    if (scope.isEmpty())
        return false;  // addresses nothing - a caller bug, never a deletion

    QJsonObject root = readFile();
    if (root.isEmpty())
        return true;  // no cache (or a foreign version): nothing to forget

    QJsonObject providers = root.value(QStringLiteral("providers")).toObject();
    if (!providers.contains(scope))
        return true;  // leave the file untouched rather than rewrite it verbatim

    providers.remove(scope);
    root.insert(QStringLiteral("providers"), providers);
    // Honest reporting: when the rewrite fails the entry is still on disk, and
    // the caller (deleting a provider profile) must not claim it is gone.
    return writeFile(root);
}

int ModelListCache::contextWindowFor(const QString &modelId)
{
    if (modelId.isEmpty())
        return 0;
    QJsonObject root = readFile();
    QJsonObject providers = root.value(QStringLiteral("providers")).toObject();
    for (const QString &p : providers.keys()) {
        QJsonArray arr = providers.value(p).toObject()
                              .value(QStringLiteral("models")).toArray();
        for (const QJsonValue &v : arr) {
            QJsonObject m = v.toObject();
            if (m.value(QStringLiteral("id")).toString() == modelId) {
                int cw = m.value(QStringLiteral("contextWindow")).toInt();
                return cw;
            }
        }
    }
    return 0;
}
