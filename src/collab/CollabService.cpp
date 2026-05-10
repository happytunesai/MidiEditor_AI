/*
 * MidiEditor AI - CollabService implementation.
 */

#include "CollabService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

#include "../midi/MidiFile.h"
#include "CollabIdentity.h"
#include "MidiDiff.h"
#include "MidiHash.h"
#include "MidiSnapshot.h"

namespace {
CollabService *s_instance = nullptr;
}

CollabService *CollabService::instance() {
    if (!s_instance) {
        s_instance = new CollabService(QCoreApplication::instance());
    }
    return s_instance;
}

CollabService::CollabService(QObject *parent)
    : QObject(parent), _enabled(false) {
    QSettings settings("MidiEditor", "NONE");
    _enabled = settings.value("Collab/enabled", false).toBool();
}

bool CollabService::isEnabled() const {
    return _enabled;
}

void CollabService::setEnabled(bool enabled) {
    if (_enabled == enabled) return;
    _enabled = enabled;
    QSettings settings("MidiEditor", "NONE");
    settings.setValue("Collab/enabled", enabled);
    emit enabledChanged(_enabled);
    // When the feature flips on, the current file might have a sidecar
    // we did not pick up before. Re-evaluate so the menu state is right.
    if (_enabled && !_currentPath.isEmpty()) {
        _currentInitialized = _currentHistory.load(_currentPath);
        emit currentFileStateChanged();
    } else if (!_enabled) {
        _lastSnapshot = QJsonArray();
        emit currentFileStateChanged();
    }
}

void CollabService::onFileLoaded(MidiFile *file, const QString &path) {
    _currentPath = path;
    _currentHistory = CollabHistoryFile();
    _currentInitialized = false;
    _lastSnapshot = QJsonArray();
    _eventAuthor.clear();  // session-only highlight tags don't survive file changes

    if (_enabled && !path.isEmpty()) {
        _currentInitialized = _currentHistory.load(path);
        if (_currentInitialized && file) {
            // Capture the in-memory state as our baseline. Subsequent
            // saves will diff against this until they replace it.
            _lastSnapshot = MidiSnapshot::ofFile(file);
        }
    }
    // Phase 9.5i: a different file is now active; the known-sidecars
    // cache covers this folder + the shared root, so re-scan to pick
    // up sidecars in the newly-active file's directory.
    if (_knownSidecarsLoaded) refreshKnownSidecars();
    emit activeFileChanged(file);
    emit currentFileStateChanged();
}

void CollabService::onFileSaved(MidiFile *file, const QString &path) {
    // BUG-COLLAB-013: ensure _pendingMerge is cleared on EVERY exit
    // path, not just the success path. Otherwise a Save-As to a
    // non-collab file leaves the marker active and the next unrelated
    // commit (possibly days later, in a different file) gets falsely
    // attributed to the original PR's author.
    auto clearPendingMerge = [this]() {
        _pendingMergeValid = false;
        _pendingMergeAuthor.clear();
        _pendingMergeMessage.clear();
    };

    if (!_enabled) { clearPendingMerge(); return; }
    if (path.isEmpty()) { clearPendingMerge(); return; }

    // The path may have changed (Save As to a different file). Refresh
    // active state to follow the new path.
    if (path != _currentPath) {
        _currentPath = path;
        _currentInitialized = _currentHistory.load(path);
        // Reset baseline; whatever was here referred to the old file.
        _lastSnapshot = QJsonArray();
    }

    if (!_currentInitialized) { clearPendingMerge(); return; }  // file not opted in for collab

    QString hash = MidiHash::sha256OfFile(path);
    if (hash.isEmpty()) return;
    if (hash == _currentHistory.currentHead()) return;  // nothing changed

    // Compute hunks: diff(last known snapshot, current in-memory state).
    QJsonArray newSnapshot = MidiSnapshot::ofFile(file);
    QJsonArray hunks;
    if (file && !_lastSnapshot.isEmpty()) {
        hunks = MidiDiff::compute(_lastSnapshot, newSnapshot, file->ticksPerQuarter());
    }

    QString parentHash = _currentHistory.currentHead();

    // Pending-merge marker (set by PrApply just before triggering this save):
    // attribute the resulting commit to the PR author with a "Merged from X:
    // <message>" label instead of the default local "Save" / local user.
    QString commitAuthor;
    QString commitMessage;
    if (_pendingMergeValid) {
        commitAuthor = _pendingMergeAuthor;
        commitMessage = QStringLiteral("Merged from %1: %2")
                            .arg(_pendingMergeAuthor, _pendingMergeMessage);
        _pendingMergeValid = false;
        _pendingMergeAuthor.clear();
        _pendingMergeMessage.clear();
    } else {
        commitAuthor = CollabIdentity::displayName();
        commitMessage = QStringLiteral("Save");
    }

    _currentHistory.appendCommit(
        hash,
        parentHash,
        commitAuthor,
        CollabIdentity::machineId(),
        QDateTime::currentSecsSinceEpoch(),
        commitMessage,
        hunks);

    _currentHistory.save(_currentPath);
    _lastSnapshot = newSnapshot;
    emit currentFileStateChanged();

    // NOTE: webhook posting is intentionally NOT triggered on every save —
    // that flooded peers with one Discord message per save. Posting is now
    // an explicit action via PrCreateDialog (Plan §10.3 + §10.4 revisited):
    // the user clicks Create PR and chooses how to share the aggregated
    // changes since their last share.
}

void CollabService::markPendingMerge(const QString &author, const QString &message) {
    _pendingMergeValid = true;
    _pendingMergeAuthor = author;
    _pendingMergeMessage = message;
}

QJsonObject CollabService::currentSidecarJson() const {
    if (!_currentInitialized) return QJsonObject();
    return _currentHistory.toJson();
}

QString CollabService::findFileBySessionId(const QString &sessionId) {
    if (sessionId.isEmpty()) return QString();
    if (!_knownSidecarsLoaded) refreshKnownSidecars();
    return _knownSidecarsBySessionId.value(sessionId);
}

void CollabService::refreshKnownSidecars() {
    _knownSidecarsBySessionId.clear();
    _knownSidecarsLoaded = true;

    // Locations we scan, in priority order:
    //  1. The currently-active file's folder (so a collab-init'd file
    //     opened from anywhere is immediately discoverable).
    //  2. Documents/MidiEditor_AI/shared/ — where LAN file-transfers land.
    QStringList scanDirs;
    if (!_currentPath.isEmpty()) {
        QFileInfo fi(_currentPath);
        QString d = fi.absolutePath();
        if (!d.isEmpty()) scanDirs.append(d);
    }
    QString sharedRoot = QDir(QStandardPaths::writableLocation(
                                  QStandardPaths::DocumentsLocation))
                             .filePath(QStringLiteral("MidiEditor_AI/shared"));
    if (!scanDirs.contains(sharedRoot)) scanDirs.append(sharedRoot);

    for (const QString &dirPath : scanDirs) {
        QDir d(dirPath);
        if (!d.exists()) continue;
        const QStringList sidecars = d.entryList(
            { QStringLiteral("*.midiedit-collab.json") }, QDir::Files);
        for (const QString &sc : sidecars) {
            QString sidecarPath = d.absoluteFilePath(sc);
            QFile f(sidecarPath);
            if (!f.open(QIODevice::ReadOnly)) continue;
            QByteArray bytes = f.readAll();
            f.close();
            QJsonParseError err{};
            QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;
            QString sessionId = doc.object()
                                    .value(QStringLiteral("sessionId")).toString();
            if (sessionId.isEmpty()) continue;
            // Reverse the sidecar-path → midi-path mapping. The
            // sidecar lives at "<stem>.midiedit-collab.json"; we want
            // <stem>.mid (or .midi/.kar). Probe each candidate so we
            // don't add a sessionId whose .mid is missing.
            QString stem = sc;
            stem.chop(QString(".midiedit-collab.json").size());
            for (const QString &ext : { QStringLiteral(".mid"),
                                         QStringLiteral(".midi"),
                                         QStringLiteral(".kar") }) {
                QString candidate = d.absoluteFilePath(stem + ext);
                if (QFileInfo::exists(candidate)) {
                    // First match wins; later identical sessionIds
                    // (shouldn't normally happen) are ignored.
                    if (!_knownSidecarsBySessionId.contains(sessionId)) {
                        _knownSidecarsBySessionId.insert(sessionId, candidate);
                    }
                    break;
                }
            }
        }
    }
}

bool CollabService::adoptRemoteSidecar(MidiFile *file, const QJsonObject &sidecarJson) {
    if (!_enabled) {
        qWarning() << "CollabService::adoptRemoteSidecar refused — collab is "
                      "disabled in this user's Settings (master toggle off)";
        return false;
    }
    if (sidecarJson.isEmpty()) {
        qWarning() << "CollabService::adoptRemoteSidecar refused — incoming "
                      "sidecar JSON is empty (host sent nothing useful)";
        return false;
    }
    if (_currentPath.isEmpty()) {
        qWarning() << "CollabService::adoptRemoteSidecar refused — no current "
                      "file path is bound (file isn't open in CollabService yet); "
                      "incoming entries="
                   << sidecarJson.value(QStringLiteral("history")).toArray().size();
        return false;
    }

    CollabHistoryFile incoming;
    if (!incoming.loadFromJson(sidecarJson)) {
        qWarning() << "CollabService::adoptRemoteSidecar refused — sidecar JSON "
                      "is malformed (loadFromJson failed)";
        return false;
    }

    _currentHistory = incoming;
    if (!_currentHistory.save(_currentPath)) {
        qWarning() << "CollabService::adoptRemoteSidecar — incoming sidecar "
                      "valid but save to" << _currentPath
                   << "failed (permissions / disk full?)";
        return false;
    }
    _currentInitialized = true;
    if (file) _lastSnapshot = MidiSnapshot::ofFile(file);
    // Phase 9.5i: peer just adopted a host's sidecar; the file is now
    // discoverable by its (host-assigned) sessionId.
    if (_knownSidecarsLoaded) refreshKnownSidecars();
    emit currentFileStateChanged();
    qInfo() << "CollabService::adoptRemoteSidecar OK — adopted"
            << sidecarJson.value(QStringLiteral("history")).toArray().size()
            << "history entries into" << _currentPath;
    return true;
}

void CollabService::recordRemoteLiveSync(MidiFile *file,
                                          const QString &author,
                                          const QString &machineId,
                                          const QString &message,
                                          const QJsonArray &hunks) {
    if (!_enabled || !_currentInitialized || !file) return;

    QJsonArray currentSnapshot = MidiSnapshot::ofFile(file);
    QByteArray snapshotBytes = QJsonDocument(currentSnapshot).toJson(QJsonDocument::Compact);
    qint64 ts = QDateTime::currentMSecsSinceEpoch();
    QByteArray seed = snapshotBytes;
    seed.append(QByteArray::number(ts));
    seed.append(author.toUtf8());
    QString hash = QString::fromUtf8(
        QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex());

    _currentHistory.appendCommit(
        hash,
        _currentHistory.currentHead(),
        author,
        machineId,
        ts / 1000,
        message,
        hunks);

    _currentHistory.save(_currentPath);
    _lastSnapshot = currentSnapshot;
    emit currentFileStateChanged();
}

int CollabService::compactHistory(int keepLastN) {
    if (!_currentInitialized || _currentPath.isEmpty()) return 0;
    int n = _currentHistory.compactHistory(keepLastN);
    if (n > 0) {
        _currentHistory.save(_currentPath);
        emit currentFileStateChanged();
    }
    return n;
}

void CollabService::registerEventAuthor(MidiEvent *event, const QString &author) {
    if (!event || author.isEmpty()) return;
    _eventAuthor.insert(event, author);
}

QString CollabService::eventAuthor(MidiEvent *event) const {
    if (!event) return QString();
    return _eventAuthor.value(event);
}

bool CollabService::hasAnyEventAuthors() const {
    return !_eventAuthor.isEmpty();
}

bool CollabService::isCurrentFileInitialized() const {
    return _currentInitialized;
}

bool CollabService::hasCurrentFile() const {
    return !_currentPath.isEmpty();
}

bool CollabService::initializeCurrentFile(MidiFile *file, const QString &commitMessage) {
    if (!_enabled) return false;
    if (_currentPath.isEmpty()) return false;
    if (_currentInitialized) return false;
    if (CollabHistoryFile::exists(_currentPath)) {
        // Sidecar exists on disk but we did not load it (e.g. previously
        // failed parse). Try to load it now and treat as initialized.
        if (_currentHistory.load(_currentPath)) {
            _currentInitialized = true;
            if (file) _lastSnapshot = MidiSnapshot::ofFile(file);
            emit currentFileStateChanged();
        }
        return false;
    }

    QString hash = MidiHash::sha256OfFile(_currentPath);
    if (hash.isEmpty()) return false;

    _currentHistory = CollabHistoryFile();
    _currentHistory.setBranch(QStringLiteral("main"));
    _currentHistory.ensureSessionId();  // assign a fresh UUID for this session
    // Initial commit: no parent, no hunks (nothing to diff against).
    _currentHistory.appendCommit(
        hash,
        QString(),  // parentHash = empty for the initial commit
        CollabIdentity::displayName(),
        CollabIdentity::machineId(),
        QDateTime::currentSecsSinceEpoch(),
        commitMessage.isEmpty() ? QStringLiteral("Initialize for collaboration")
                                : commitMessage);

    if (!_currentHistory.save(_currentPath)) return false;
    _currentInitialized = true;
    if (file) _lastSnapshot = MidiSnapshot::ofFile(file);
    // Phase 9.5i: a freshly-init'd file should be discoverable by
    // sessionId immediately (e.g. for a peer joining shortly after).
    if (_knownSidecarsLoaded) refreshKnownSidecars();
    emit currentFileStateChanged();
    return true;
}

QString CollabService::currentHead() const {
    return _currentInitialized ? _currentHistory.currentHead() : QString();
}

QString CollabService::sessionId() const {
    return _currentInitialized ? _currentHistory.sessionId() : QString();
}

QJsonArray CollabService::history() const {
    return _currentInitialized ? _currentHistory.history() : QJsonArray();
}

QString CollabService::lastSharedHead() const {
    return _currentInitialized ? _currentHistory.lastSharedHead() : QString();
}

void CollabService::markCurrentAsShared() {
    if (!_currentInitialized) return;
    QString head = _currentHistory.currentHead();
    if (head.isEmpty()) return;
    if (head == _currentHistory.lastSharedHead()) return;  // no-op

    _currentHistory.setLastSharedHead(head);
    _currentHistory.save(_currentPath);
    emit currentFileStateChanged();
}
