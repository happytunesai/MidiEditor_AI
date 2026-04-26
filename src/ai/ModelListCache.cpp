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

void ModelListCache::writeFile(const QJsonObject &obj)
{
    QFile f(cacheFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    f.close();
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
