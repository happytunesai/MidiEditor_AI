/*
 * test_mcp_session_table
 *
 * SP-06 (external system/performance review, 2026-09-06): McpServer closed a
 * session's SSE socket while holding the session mutex; QTcpSocket::close()
 * emits disconnected() synchronously and the server's disconnected handler
 * locks the same non-recursive mutex - a self-deadlock on the GUI thread on
 * quit, when the server was switched off, and when a session was removed or
 * expired with a live stream. Seen live as an editor process that never
 * finished exiting.
 *
 * McpSessionTable now owns the bookkeeping and never touches a socket: every
 * operation hands the sockets back with the lock released. This test drives
 * the table with REAL Qt TCP sockets and the server's handler shape - the
 * disconnected handler calls detachSocket(), which takes the table's lock,
 * while close() runs synchronously inside the shutdown sequence. A watchdog
 * thread turns a deadlock into a failed test instead of a hung one.
 */

#include <QtTest/QtTest>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "../src/ai/McpSessionTable.h"

namespace {

/** Aborts the process when a case does not finish in time: a deadlocked
 *  GUI thread cannot run a QTimer, so this has to be a real thread. */
class Watchdog {
public:
    explicit Watchdog(int seconds)
        : _thread([this, seconds]() {
              for (int tick = 0; tick < seconds * 10 && !_done.load(); ++tick) {
                  std::this_thread::sleep_for(std::chrono::milliseconds(100));
              }
              if (!_done.load()) {
                  std::fputs("WATCHDOG: a session-table operation did not return "
                             "- the shutdown deadlock is back\n", stderr);
                  std::fflush(stderr);
                  std::_Exit(3);
              }
          }) {}
    ~Watchdog()
    {
        _done = true;
        _thread.join();
    }

private:
    std::atomic<bool> _done{false};
    std::thread _thread;
};

struct Pair {
    QTcpSocket *server = nullptr; // the accepted (server-side) socket
    QTcpSocket *client = nullptr;
};

McpSessionTable::Session sessionWithId(const QString &id)
{
    McpSessionTable::Session s;
    s.id = id;
    s.created = QDateTime::currentDateTime();
    s.lastActivity = s.created;
    s.rateLimitWindow = s.created;
    return s;
}

} // namespace

class TestMcpSessionTable : public QObject {
    Q_OBJECT

private:
    QTcpServer *_listener = nullptr;
    QList<QTcpSocket *> _clients;

    Pair connectPair()
    {
        Pair p;
        p.client = new QTcpSocket(this);
        _clients.append(p.client);
        p.client->connectToHost(QHostAddress::LocalHost, _listener->serverPort());
        if (!p.client->waitForConnected(3000) || !_listener->waitForNewConnection(3000)) {
            return Pair();
        }
        p.server = _listener->nextPendingConnection(); // child of the listener, like McpServer's
        return p;
    }

private slots:
    void init()
    {
        _listener = new QTcpServer(this);
        QVERIFY(_listener->listen(QHostAddress::LocalHost, 0));
    }

    void cleanup()
    {
        for (QTcpSocket *c : _clients) {
            c->abort();
            delete c;
        }
        _clients.clear();
        delete _listener;
        _listener = nullptr;
    }

    // The shutdown sequence McpServer::stop() runs: clear the table, close the
    // streams it handed back. The handler locks the table from inside close().
    void clearThenCloseWithHandlerThatLocksTheTable_returns()
    {
        Watchdog dog(8);
        McpSessionTable table;
        table.insert(sessionWithId("a"));
        const Pair p = connectPair();
        QVERIFY(p.server);
        QCOMPARE(table.attachSse("a", p.server), nullptr);

        bool handlerRan = false;
        QString detachedId;
        connect(p.server, &QTcpSocket::disconnected, this, [&]() {
            handlerRan = true;
            detachedId = table.detachSocket(p.server); // takes the table's lock
        });

        const QList<QTcpSocket *> streams = table.clear();
        QCOMPARE(streams.size(), 1);
        QCOMPARE(streams.first(), p.server);
        QCOMPARE(table.count(), 0);
        for (QTcpSocket *s : streams) {
            if (s->isOpen()) s->close(); // outside the lock - must return
        }
        QVERIFY2(handlerRan, "disconnected() was not delivered synchronously - the "
                             "scenario no longer exercises the re-entrant path");
        QVERIFY(detachedId.isEmpty()); // the session was already gone
        QCOMPARE(p.server->state(), QAbstractSocket::UnconnectedState);
    }

    void removeHandsBackTheStream_closingItAfterwardsReturns()
    {
        Watchdog dog(8);
        McpSessionTable table;
        table.insert(sessionWithId("a"));
        const Pair p = connectPair();
        QVERIFY(p.server);
        table.attachSse("a", p.server);
        connect(p.server, &QTcpSocket::disconnected, this, [&]() {
            table.detachSocket(p.server);
        });

        McpSessionTable::Removed removed;
        QVERIFY(table.remove("a", removed));
        QCOMPARE(removed.id, QString("a"));
        QCOMPARE(removed.sseSocket, p.server);
        QCOMPARE(table.count(), 0);
        removed.sseSocket->close();
        QCOMPARE(p.server->state(), QAbstractSocket::UnconnectedState);

        McpSessionTable::Removed again;
        QVERIFY(!table.remove("a", again));
    }

    void expireReturnsIdleSessionsWithTheirStreams()
    {
        Watchdog dog(8);
        McpSessionTable table;
        McpSessionTable::Session idle = sessionWithId("idle");
        idle.lastActivity = QDateTime::currentDateTime().addSecs(-7200);
        table.insert(idle);
        table.insert(sessionWithId("fresh"));
        const Pair p = connectPair();
        QVERIFY(p.server);
        table.attachSse("idle", p.server);
        connect(p.server, &QTcpSocket::disconnected, this, [&]() {
            table.detachSocket(p.server);
        });

        const QList<McpSessionTable::Removed> expired =
            table.expire(QDateTime::currentDateTime(), 3600);
        QCOMPARE(expired.size(), 1);
        QCOMPARE(expired.first().id, QString("idle"));
        QCOMPARE(expired.first().sseSocket, p.server);
        QCOMPARE(table.count(), 1);
        QVERIFY(table.find("fresh"));
        QVERIFY(!table.find("idle"));
        expired.first().sseSocket->close();
        QCOMPARE(p.server->state(), QAbstractSocket::UnconnectedState);
    }

    void attachSseReturnsThePreviousStream_closingItDoesNotDetachTheNewOne()
    {
        Watchdog dog(8);
        McpSessionTable table;
        table.insert(sessionWithId("a"));
        const Pair first = connectPair();
        const Pair second = connectPair();
        QVERIFY(first.server && second.server);
        QCOMPARE(table.attachSse("a", first.server), nullptr);
        QCOMPARE(table.attachSse("a", second.server), first.server);
        QCOMPARE(table.attachSse("a", second.server), nullptr); // same stream again

        QString detachedId = QStringLiteral("unset");
        connect(first.server, &QTcpSocket::disconnected, this, [&]() {
            detachedId = table.detachSocket(first.server);
        });
        first.server->close(); // McpServer's MCP-004 path, after the switch
        QVERIFY(detachedId.isEmpty()); // the old stream was no session's stream any more
        QCOMPARE(table.find("a")->sseSocket, second.server);
        QCOMPARE(table.sseSockets(), QList<QTcpSocket *>{second.server});
    }

    void detachSocketForgetsOnlyTheMatchingSession()
    {
        McpSessionTable table;
        table.insert(sessionWithId("a"));
        table.insert(sessionWithId("b"));
        const Pair pa = connectPair();
        const Pair pb = connectPair();
        QVERIFY(pa.server && pb.server);
        table.attachSse("a", pa.server);
        table.attachSse("b", pb.server);

        QCOMPARE(table.detachSocket(pa.server), QString("a"));
        QCOMPARE(table.find("a")->sseSocket, nullptr);
        QCOMPARE(table.find("b")->sseSocket, pb.server);
        QVERIFY(table.detachSocket(pa.server).isEmpty()); // twice: nothing left to forget
        QVERIFY(table.detachSocket(nullptr).isEmpty());
        QCOMPARE(table.attachSse("unknown", pa.server), nullptr);
    }

    void forgetFileMarksTheBoundSession()
    {
        McpSessionTable table;
        MidiFile *const doc = reinterpret_cast<MidiFile *>(0x10); // identity only
        McpSessionTable::Session s = sessionWithId("a");
        s.boundFile = doc;
        table.insert(s);
        table.insert(sessionWithId("b"));

        table.forgetFile(doc);
        QCOMPARE(table.find("a")->boundFile, nullptr);
        QVERIFY(table.find("a")->boundFileClosed);
        QVERIFY(!table.find("b")->boundFileClosed);
        table.forgetFile(nullptr); // no-op
        QCOMPARE(table.ids(), (QStringList{"a", "b"}));
    }
};

QTEST_GUILESS_MAIN(TestMcpSessionTable)
#include "test_mcp_session_table.moc"
