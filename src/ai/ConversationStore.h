#ifndef CONVERSATIONSTORE_H
#define CONVERSATIONSTORE_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

class ConversationStore {
public:
    struct ConversationMeta {
        QString id;
        QString title;
        QString midiFile;
        QString model;
        QString provider;
        QDateTime created;
        QDateTime updated;
        int messageCount;
        int promptTokens;
        int completionTokens;
    };

    static QString storageDir();
    static QList<ConversationMeta> listConversations();
    static QList<ConversationMeta> findByMidiFile(const QString &midiFilePath);
    static QJsonObject loadConversation(const QString &id);
    static void saveConversation(const QJsonObject &data);
    static void deleteConversation(const QString &id);
    static void deleteAll();

    static QString generateId();
    static QString titleFromMessage(const QString &firstMessage);
};

#endif // CONVERSATIONSTORE_H
