#include "McpSessionTable.h"

#include <QMutexLocker>

// Design rule, enforced by test_shutdown_guards: this file never calls into a
// QTcpSocket. Sockets are opaque pointers here; whoever gets one back closes
// or writes it after the call - outside the table's lock.

void McpSessionTable::insert(const Session &session) {
    QMutexLocker lock(&_mutex);
    _sessions[session.id] = session;
}

McpSessionTable::Session *McpSessionTable::find(const QString &id) {
    QMutexLocker lock(&_mutex);
    auto it = _sessions.find(id);
    if (it == _sessions.end()) {
        return nullptr;
    }
    return &it.value();
}

int McpSessionTable::count() const {
    QMutexLocker lock(&_mutex);
    return _sessions.size();
}

QStringList McpSessionTable::ids() const {
    QMutexLocker lock(&_mutex);
    return _sessions.keys();
}

QTcpSocket *McpSessionTable::attachSse(const QString &id, QTcpSocket *socket) {
    QMutexLocker lock(&_mutex);
    auto it = _sessions.find(id);
    if (it == _sessions.end()) {
        return nullptr;
    }
    QTcpSocket *previous = it->sseSocket;
    it->sseSocket = socket;
    return previous == socket ? nullptr : previous;
}

QString McpSessionTable::detachSocket(QTcpSocket *socket) {
    if (!socket) {
        return QString();
    }
    QMutexLocker lock(&_mutex);
    for (auto it = _sessions.begin(); it != _sessions.end(); ++it) {
        if (it->sseSocket == socket) {
            it->sseSocket = nullptr;
            return it.key();
        }
    }
    return QString();
}

bool McpSessionTable::remove(const QString &id, Removed &removed) {
    QMutexLocker lock(&_mutex);
    auto it = _sessions.find(id);
    if (it == _sessions.end()) {
        return false;
    }
    removed.id = it.key();
    removed.sseSocket = it->sseSocket;
    _sessions.erase(it);
    return true;
}

QList<McpSessionTable::Removed> McpSessionTable::expire(const QDateTime &now, int timeoutSecs) {
    QList<Removed> expired;
    QMutexLocker lock(&_mutex);
    for (auto it = _sessions.begin(); it != _sessions.end();) {
        if (it->lastActivity.secsTo(now) > timeoutSecs) {
            expired.append({it.key(), it->sseSocket});
            it = _sessions.erase(it);
        } else {
            ++it;
        }
    }
    return expired;
}

QList<QTcpSocket *> McpSessionTable::clear() {
    QList<QTcpSocket *> streams;
    QMutexLocker lock(&_mutex);
    for (auto it = _sessions.begin(); it != _sessions.end(); ++it) {
        if (it->sseSocket) {
            streams.append(it->sseSocket);
        }
    }
    _sessions.clear();
    return streams;
}

QList<QTcpSocket *> McpSessionTable::sseSockets() const {
    QList<QTcpSocket *> streams;
    QMutexLocker lock(&_mutex);
    for (auto it = _sessions.cbegin(); it != _sessions.cend(); ++it) {
        if (it->sseSocket) {
            streams.append(it->sseSocket);
        }
    }
    return streams;
}

void McpSessionTable::forgetFile(MidiFile *file) {
    if (!file) {
        return;
    }
    QMutexLocker lock(&_mutex);
    for (auto it = _sessions.begin(); it != _sessions.end(); ++it) {
        if (it->boundFile == file) {
            it->boundFile = nullptr;
            it->boundFileClosed = true; // next tool call must re-read, not silently retarget
        }
    }
}
