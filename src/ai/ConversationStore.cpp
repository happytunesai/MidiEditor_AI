#include "ConversationStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>

QString ConversationStore::storageDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString dir = base + QStringLiteral("/MidiPilotHistory");
    QDir().mkpath(dir);
    return dir;
}

QList<ConversationStore::ConversationMeta> ConversationStore::listConversations()
{
    QList<ConversationMeta> result;
    QDir dir(storageDir());
    QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"),
                                       QDir::Files, QDir::Time); // newest first

    for (const QString &fileName : files) {
        QFile f(dir.filePath(fileName));
        if (!f.open(QIODevice::ReadOnly))
            continue;

        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!doc.isObject())
            continue;

        QJsonObject obj = doc.object();
        ConversationMeta meta;
        meta.id = obj[QStringLiteral("id")].toString();
        meta.title = obj[QStringLiteral("title")].toString();
        meta.midiFile = obj[QStringLiteral("midiFile")].toString();
        meta.model = obj[QStringLiteral("model")].toString();
        meta.provider = obj[QStringLiteral("provider")].toString();
        meta.created = QDateTime::fromString(obj[QStringLiteral("created")].toString(), Qt::ISODate);
        meta.updated = QDateTime::fromString(obj[QStringLiteral("updated")].toString(), Qt::ISODate);

        QJsonArray msgs = obj[QStringLiteral("messages")].toArray();
        meta.messageCount = msgs.size();

        QJsonObject usage = obj[QStringLiteral("tokenUsage")].toObject();
        meta.promptTokens = usage[QStringLiteral("prompt")].toInt();
        meta.completionTokens = usage[QStringLiteral("completion")].toInt();

        if (!meta.id.isEmpty())
            result.append(meta);
    }

    return result;
}

QList<ConversationStore::ConversationMeta> ConversationStore::findByMidiFile(const QString &midiFilePath)
{
    QList<ConversationMeta> all = listConversations();
    QList<ConversationMeta> result;
    QString normalized = QDir::toNativeSeparators(midiFilePath).toLower();

    for (const ConversationMeta &m : all) {
        if (QDir::toNativeSeparators(m.midiFile).toLower() == normalized)
            result.append(m);
    }
    return result;
}

QJsonObject ConversationStore::loadConversation(const QString &id)
{
    QDir dir(storageDir());
    QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);

    for (const QString &fileName : files) {
        QFile f(dir.filePath(fileName));
        if (!f.open(QIODevice::ReadOnly))
            continue;

        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!doc.isObject())
            continue;

        QJsonObject obj = doc.object();
        if (obj[QStringLiteral("id")].toString() == id)
            return obj;
    }
    return QJsonObject();
}

void ConversationStore::saveConversation(const QJsonObject &data)
{
    QString id = data[QStringLiteral("id")].toString();
    if (id.isEmpty())
        return;

    // Use a stable filename derived from the conversation id
    QString fileName = id + QStringLiteral(".json");
    QString filePath = storageDir() + QStringLiteral("/") + fileName;

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    f.write(QJsonDocument(data).toJson(QJsonDocument::Indented));
    f.close();
}

void ConversationStore::deleteConversation(const QString &id)
{
    QString filePath = storageDir() + QStringLiteral("/") + id + QStringLiteral(".json");
    QFile::remove(filePath);
}

void ConversationStore::deleteAll()
{
    QDir dir(storageDir());
    QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (const QString &fileName : files) {
        dir.remove(fileName);
    }
}

QString ConversationStore::generateId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
}

QString ConversationStore::titleFromMessage(const QString &firstMessage)
{
    QString text = firstMessage.trimmed();
    // The user message may be a JSON object with an "instruction" field — extract that
    QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (doc.isObject()) {
        QString instr = doc.object()[QStringLiteral("instruction")].toString().trimmed();
        if (!instr.isEmpty())
            text = instr;
    }
    // Remove newlines, take first line
    int nl = text.indexOf('\n');
    if (nl > 0) text = text.left(nl);
    // Truncate to 60 chars
    if (text.length() > 60)
        text = text.left(57) + QStringLiteral("...");
    return text;
}
