#ifndef MCPSESSIONTABLE_H
#define MCPSESSIONTABLE_H

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QString>
#include <QStringList>

class MidiFile;
class QTcpSocket;

/**
 * \class McpSessionTable
 *
 * \brief The MCP server's session bookkeeping, with one rule built into its
 *        interface: nothing in here ever calls into a socket.
 *
 * Every operation that ends in a socket being closed or written hands the
 * socket(s) back to the caller and returns with the lock already released,
 * so the caller acts on them OUTSIDE the lock.
 *
 * Why: QTcpSocket::close() emits disconnected() synchronously, and the
 * server's disconnected handler needs this table (to forget the SSE stream).
 * Closing an SSE socket while holding the session lock therefore re-entered
 * the same non-recursive mutex on the GUI thread and hung the editor - on
 * quit (McpServer::stop() from the destructor), when the MCP server was
 * switched off on the settings page, and when a session was removed or
 * expired while its SSE stream was open (external review 2026-09-06, SP-06;
 * seen live as an editor process that never finished exiting, its MCP port
 * still open). The lock exists for the cross-thread readers (count()); it is
 * held for map operations only.
 *
 * Pointers returned by find() stay valid until the session is removed
 * (QMap nodes do not move); the server uses them on its own thread only.
 */
class McpSessionTable {
public:
    struct Session {
        QString id;
        QString clientName;  // e.g. "VS Code Copilot 1.0"
        QTcpSocket *sseSocket = nullptr;  // SSE connection (GET /mcp)
        QDateTime created;
        QDateTime lastActivity;
        int toolCallCount = 0;
        QDateTime rateLimitWindow;
        // Phase 28 (editor groups): the document this session is working on. Like
        // MidiPilot's run origin, tool calls act on THIS document even if the user
        // switches tabs, so a read-then-write across a tab switch stays coherent.
        // get_editor_state resyncs it to the active document; forgetFile() clears
        // it when that document is closed. nullptr = bind to active on next use.
        MidiFile *boundFile = nullptr;
        // Set by forgetFile() when boundFile was the closed document, so the next
        // non-get_editor_state tool returns an error ("re-read state") instead of
        // silently rebinding to (and editing) whatever document is now active.
        bool boundFileClosed = false;
    };

    /** A removed or expired session and the SSE socket the caller must close. */
    struct Removed {
        QString id;
        QTcpSocket *sseSocket = nullptr;
    };

    /** Adds (or replaces) the session under session.id. */
    void insert(const Session &session);

    /** The session with this id, or nullptr. */
    Session *find(const QString &id);

    int count() const;
    QStringList ids() const;

    /** Registers \p socket as the session's SSE stream and returns the stream
     *  it replaces (or nullptr). Close that one AFTER this call returns. Returns
     *  nullptr as well when the session is unknown. */
    QTcpSocket *attachSse(const QString &id, QTcpSocket *socket);

    /** The socket went away: forget it where it was a session's SSE stream.
     *  \return that session's id, empty when the socket was no stream. */
    QString detachSocket(QTcpSocket *socket);

    /** Removes the session. \return false when unknown; otherwise \p removed
     *  carries the id and the SSE socket the caller closes afterwards. */
    bool remove(const QString &id, Removed &removed);

    /** Removes every session idle for longer than \p timeoutSecs at \p now and
     *  returns them (with their SSE sockets) for the caller. */
    QList<Removed> expire(const QDateTime &now, int timeoutSecs);

    /** Removes every session and returns all open SSE sockets for the caller. */
    QList<QTcpSocket *> clear();

    /** Snapshot of the current SSE sockets, for broadcasts written outside the lock. */
    QList<QTcpSocket *> sseSockets() const;

    /** A document was closed: drop it as any session's bound document and mark
     *  the session so its next tool call re-reads the state. */
    void forgetFile(MidiFile *file);

private:
    mutable QMutex _mutex;
    QMap<QString, Session> _sessions;
};

#endif // MCPSESSIONTABLE_H
