/*
 * test_conversation_store
 *
 * Unit tests for src/ai/ConversationStore. The store is a pure Qt JSON
 * persister — no AiClient / EditorContext / MidiFile dependencies — so
 * compiling ConversationStore.cpp into the test executable is clean.
 *
 * Storage location is QStandardPaths::AppDataLocation + "/MidiPilotHistory".
 * QStandardPaths::setTestModeEnabled(true) is enabled in initTestCase() so
 * the tests write into an isolated XDG path instead of the user's real
 * AppData. cleanupTestCase() / init() use deleteAll() to keep each test
 * starting from a known empty state.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>

#include "../src/ai/ConversationStore.h"

namespace {

QJsonObject makeConversation(const QString &id,
                             const QString &title = QStringLiteral("Untitled"),
                             const QString &midi  = QString(),
                             int messages = 0)
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("title")] = title;
    obj[QStringLiteral("midiFile")] = midi;
    obj[QStringLiteral("model")] = QStringLiteral("gpt-test");
    obj[QStringLiteral("provider")] = QStringLiteral("openai");
    obj[QStringLiteral("created")] = QDateTime(QDate(2026, 4, 1), QTime(10, 0, 0), Qt::UTC).toString(Qt::ISODate);
    obj[QStringLiteral("updated")] = QDateTime(QDate(2026, 4, 1), QTime(11, 30, 0), Qt::UTC).toString(Qt::ISODate);

    QJsonArray msgs;
    for (int i = 0; i < messages; ++i) {
        QJsonObject m;
        m[QStringLiteral("role")] = (i % 2 == 0) ? QStringLiteral("user") : QStringLiteral("assistant");
        m[QStringLiteral("content")] = QStringLiteral("msg %1").arg(i);
        msgs.append(m);
    }
    obj[QStringLiteral("messages")] = msgs;

    QJsonObject usage;
    usage[QStringLiteral("prompt")] = 100;
    usage[QStringLiteral("completion")] = 250;
    obj[QStringLiteral("tokenUsage")] = usage;
    return obj;
}

} // namespace

class TestConversationStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Redirect AppDataLocation to a private XDG path so we never touch
        // the developer's real MidiPilotHistory folder.
        QStandardPaths::setTestModeEnabled(true);
        ConversationStore::deleteAll();
    }

    void cleanupTestCase()
    {
        ConversationStore::deleteAll();
    }

    void init()
    {
        // Each test starts from an empty store.
        ConversationStore::deleteAll();
    }

    // ---- generateId -------------------------------------------------------

    void generateId_returnsTwelveCharUniqueString()
    {
        const QString a = ConversationStore::generateId();
        const QString b = ConversationStore::generateId();
        QCOMPARE(a.length(), 12);
        QCOMPARE(b.length(), 12);
        QVERIFY(a != b);
    }

    // ---- titleFromMessage -------------------------------------------------

    void titleFromMessage_plainShortText_returnedAsIs()
    {
        QCOMPARE(ConversationStore::titleFromMessage(QStringLiteral("Add a drum loop")),
                 QStringLiteral("Add a drum loop"));
    }

    void titleFromMessage_multiLine_keepsOnlyFirstLine()
    {
        const QString in = QStringLiteral("First line\nSecond line\nThird");
        QCOMPARE(ConversationStore::titleFromMessage(in), QStringLiteral("First line"));
    }

    void titleFromMessage_longText_truncatedWithEllipsis()
    {
        const QString in = QString(80, QChar('a'));
        const QString out = ConversationStore::titleFromMessage(in);
        QCOMPARE(out.length(), 60);
        QVERIFY(out.endsWith(QStringLiteral("...")));
        QCOMPARE(out.left(57), QString(57, QChar('a')));
    }

    void titleFromMessage_jsonWithInstruction_extractsInstructionField()
    {
        const QString in = QStringLiteral("{\"instruction\":\"Compose a melody\",\"context\":\"ignored\"}");
        QCOMPARE(ConversationStore::titleFromMessage(in), QStringLiteral("Compose a melody"));
    }

    // ---- save / load round-trip -------------------------------------------

    void saveThenLoad_withValidObject_returnsEqualObject()
    {
        const QString id = ConversationStore::generateId();
        const QJsonObject src = makeConversation(id, QStringLiteral("Round trip"),
                                                 QStringLiteral("C:/songs/test.mid"), 4);

        ConversationStore::saveConversation(src);
        const QJsonObject loaded = ConversationStore::loadConversation(id);

        QCOMPARE(loaded, src);
    }

    void save_withEmptyId_isNoOp()
    {
        QJsonObject obj = makeConversation(QString());
        ConversationStore::saveConversation(obj);

        // Nothing should have landed on disk.
        QCOMPARE(ConversationStore::listConversations().size(), 0);
    }

    // ---- listConversations ------------------------------------------------

    void listConversations_returnsMetadataForAllSavedFiles()
    {
        ConversationStore::saveConversation(makeConversation(QStringLiteral("aaaaaaaaaaaa"),
                                                             QStringLiteral("First"),
                                                             QStringLiteral("a.mid"), 2));
        ConversationStore::saveConversation(makeConversation(QStringLiteral("bbbbbbbbbbbb"),
                                                             QStringLiteral("Second"),
                                                             QStringLiteral("b.mid"), 5));

        const auto list = ConversationStore::listConversations();
        QCOMPARE(list.size(), 2);

        QStringList ids;
        for (const auto &m : list) {
            ids << m.id;
            if (m.id == QStringLiteral("aaaaaaaaaaaa")) {
                QCOMPARE(m.title, QStringLiteral("First"));
                QCOMPARE(m.messageCount, 2);
                QCOMPARE(m.promptTokens, 100);
                QCOMPARE(m.completionTokens, 250);
                QCOMPARE(m.provider, QStringLiteral("openai"));
            }
        }
        QVERIFY(ids.contains(QStringLiteral("aaaaaaaaaaaa")));
        QVERIFY(ids.contains(QStringLiteral("bbbbbbbbbbbb")));
    }

    void listConversations_skipsCorruptedAndEmptyIdFiles()
    {
        // One valid entry.
        ConversationStore::saveConversation(makeConversation(QStringLiteral("validfileid1")));

        const QString dir = ConversationStore::storageDir();

        // Truncated JSON.
        QFile bad(dir + QStringLiteral("/broken.json"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ \"id\": \"x\", \"title\":");
        bad.close();

        // Valid JSON but empty id — should be filtered out.
        QFile noid(dir + QStringLiteral("/noid.json"));
        QVERIFY(noid.open(QIODevice::WriteOnly));
        noid.write("{ \"id\": \"\", \"title\": \"ghost\" }");
        noid.close();

        // Valid JSON but a top-level array (not an object).
        QFile arr(dir + QStringLiteral("/array.json"));
        QVERIFY(arr.open(QIODevice::WriteOnly));
        arr.write("[1, 2, 3]");
        arr.close();

        const auto list = ConversationStore::listConversations();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().id, QStringLiteral("validfileid1"));
    }

    void listConversations_missingFieldsDefaultGracefully()
    {
        // Minimal valid object — only id is set. All other accessors should
        // fall back to defaults without throwing.
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("minimalentry");
        ConversationStore::saveConversation(obj);

        const auto list = ConversationStore::listConversations();
        QCOMPARE(list.size(), 1);
        const auto &m = list.first();
        QCOMPARE(m.id, QStringLiteral("minimalentry"));
        QCOMPARE(m.title, QString());
        QCOMPARE(m.messageCount, 0);
        QCOMPARE(m.promptTokens, 0);
        QCOMPARE(m.completionTokens, 0);
    }

    // ---- findByMidiFile ---------------------------------------------------

    void findByMidiFile_matchesCaseInsensitivelyAndAcrossSeparators()
    {
        ConversationStore::saveConversation(makeConversation(QStringLiteral("matchitem001"),
                                                             QStringLiteral("A"),
                                                             QStringLiteral("C:/Songs/Hit.mid")));
        ConversationStore::saveConversation(makeConversation(QStringLiteral("matchitem002"),
                                                             QStringLiteral("B"),
                                                             QStringLiteral("C:/Songs/Other.mid")));

        const auto hits = ConversationStore::findByMidiFile(QStringLiteral("c:\\songs\\hit.mid"));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().id, QStringLiteral("matchitem001"));
    }

    void findByMidiFile_withNoMatch_returnsEmpty()
    {
        ConversationStore::saveConversation(makeConversation(QStringLiteral("xxxxxxxxxxxx"),
                                                             QStringLiteral("A"),
                                                             QStringLiteral("C:/Songs/Hit.mid")));
        QVERIFY(ConversationStore::findByMidiFile(QStringLiteral("C:/Songs/Missing.mid")).isEmpty());
    }

    // ---- loadConversation corruption tolerance ----------------------------

    void loadConversation_unknownId_returnsEmptyObject()
    {
        QVERIFY(ConversationStore::loadConversation(QStringLiteral("does-not-exist")).isEmpty());
    }

    void loadConversation_truncatedJson_returnsEmptyObject()
    {
        const QString id = QStringLiteral("brokenfile01");
        QFile f(ConversationStore::storageDir() + QStringLiteral("/") + id + QStringLiteral(".json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ \"id\": \"brokenfile01\", ");
        f.close();

        QVERIFY(ConversationStore::loadConversation(id).isEmpty());
    }

    void loadConversation_jsonArrayInsteadOfObject_returnsEmptyObject()
    {
        const QString id = QStringLiteral("arrayfile001");
        QFile f(ConversationStore::storageDir() + QStringLiteral("/") + id + QStringLiteral(".json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[1, 2, 3]");
        f.close();

        QVERIFY(ConversationStore::loadConversation(id).isEmpty());
    }

    // ---- delete -----------------------------------------------------------

    void deleteConversation_removesOnlyMatchingFile()
    {
        ConversationStore::saveConversation(makeConversation(QStringLiteral("keepkeepkeep")));
        ConversationStore::saveConversation(makeConversation(QStringLiteral("dropdropdrop")));

        ConversationStore::deleteConversation(QStringLiteral("dropdropdrop"));

        QVERIFY(ConversationStore::loadConversation(QStringLiteral("dropdropdrop")).isEmpty());
        QVERIFY(!ConversationStore::loadConversation(QStringLiteral("keepkeepkeep")).isEmpty());
        QCOMPARE(ConversationStore::listConversations().size(), 1);
    }

    void deleteAll_clearsStorage()
    {
        ConversationStore::saveConversation(makeConversation(QStringLiteral("zzzzzzzzzzzz")));
        QCOMPARE(ConversationStore::listConversations().size(), 1);

        ConversationStore::deleteAll();
        QCOMPARE(ConversationStore::listConversations().size(), 0);
    }
};

QTEST_APPLESS_MAIN(TestConversationStore)
#include "test_conversation_store.moc"
