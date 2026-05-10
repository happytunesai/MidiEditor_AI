/*
 * MidiEditor AI - HistoryReconciliation implementation.
 */

#include "HistoryReconciliation.h"

#include <QJsonObject>
#include <QSet>

QString HistoryReconciliation::findMergeBase(const QStringList &localTail,
                                              const QStringList &remoteTail) {
    if (localTail.isEmpty() || remoteTail.isEmpty()) return QString();

    // Hash the remote side once for O(1) membership queries.
    QSet<QString> remote(remoteTail.begin(), remoteTail.end());
    for (const QString &h : localTail) {
        if (h.isEmpty()) continue;
        if (remote.contains(h)) return h;
    }
    return QString();
}

QStringList HistoryReconciliation::tailHashes(const QJsonArray &history,
                                              int maxEntries) {
    QStringList out;
    if (history.isEmpty() || maxEntries <= 0) return out;
    int total = history.size();
    int firstIndex = qMax(0, total - maxEntries);
    out.reserve(total - firstIndex);
    // Walk backwards from the head so the result is newest-first.
    for (int i = total - 1; i >= firstIndex; --i) {
        QString h = history.at(i).toObject().value(QStringLiteral("hash")).toString();
        if (!h.isEmpty()) out.append(h);
    }
    return out;
}

QJsonArray HistoryReconciliation::commitsSinceFork(const QJsonArray &history,
                                                    const QString &ancestorHash) {
    if (history.isEmpty()) return QJsonArray();
    if (ancestorHash.isEmpty()) return history;

    // Find the index of ancestorHash; everything strictly after it is
    // the divergent slice. If not found, return the whole history
    // (caller will treat as unrelated).
    int forkIndex = -1;
    for (int i = 0; i < history.size(); ++i) {
        if (history.at(i).toObject().value(QStringLiteral("hash")).toString()
            == ancestorHash) {
            forkIndex = i;
            break;
        }
    }
    if (forkIndex < 0) return history;

    QJsonArray slice;
    for (int i = forkIndex + 1; i < history.size(); ++i) {
        slice.append(history.at(i));
    }
    return slice;
}

PrBundle HistoryReconciliation::synthesizeBundle(const QJsonArray &slice,
                                                  const QString &sessionId,
                                                  const QString &ancestorHash) {
    PrBundle b;
    if (slice.isEmpty()) return b;  // invalid — caller should handle

    // Aggregate fields from the slice. Author + machineId come from the
    // most recent commit — that's whose work the host is reviewing.
    QJsonObject newest = slice.last().toObject();
    b.sessionId  = sessionId;
    b.author     = newest.value(QStringLiteral("author")).toString();
    b.machineId  = newest.value(QStringLiteral("machineId")).toString();
    b.parentHash = ancestorHash;
    b.timestamp  = static_cast<qint64>(newest.value(QStringLiteral("ts")).toDouble());

    int commitCount = slice.size();
    QString tipMsg = newest.value(QStringLiteral("message")).toString();
    if (commitCount == 1) {
        b.message = tipMsg.isEmpty() ? QStringLiteral("(no message)") : tipMsg;
    } else {
        b.message = QStringLiteral("%1 commits since %2: %3")
                         .arg(commitCount)
                         .arg(ancestorHash.left(8),
                              tipMsg.isEmpty() ? QStringLiteral("(latest unnamed)")
                                               : tipMsg);
    }

    // Concatenate hunks chronologically (oldest first). Multiple commits
    // touching the same scope produce multiple entries; PrReviewDialog
    // shows each as its own checkbox row, which is the right behavior —
    // the user sees each edit as a separate decision.
    for (const QJsonValue &entryVal : slice) {
        QJsonArray entryHunks = entryVal.toObject()
                                    .value(QStringLiteral("hunks")).toArray();
        for (const QJsonValue &h : entryHunks) {
            b.hunks.append(h);
        }
    }
    return b;
}

HistoryReconciliation::Relation HistoryReconciliation::classify(
        const QString &localHead, const QString &remoteHead,
        const QStringList &localTail, const QStringList &remoteTail) {
    if (!localHead.isEmpty() && localHead == remoteHead) return Relation::SameHead;

    QString base = findMergeBase(localTail, remoteTail);
    if (base.isEmpty()) return Relation::Unrelated;

    // If the merge base equals one side's head, the other is strictly
    // ahead → fast-forward case.
    if (base == localHead && base != remoteHead) return Relation::LocalBehindRemote;
    if (base == remoteHead && base != localHead) return Relation::RemoteBehindLocal;
    return Relation::Diverged;
}
