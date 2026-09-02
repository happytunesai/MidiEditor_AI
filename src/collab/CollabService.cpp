/*
 * MidiEditor AI - CollabService implementation.
 */

#include "CollabService.h"

#include "../AppPaths.h"

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
    auto settingsPtr = AppPaths::settings();
    QSettings &settings = *settingsPtr;
    _enabled = settings.value("Collab/enabled", false).toBool();
}

bool CollabService::isEnabled() const {
    return _enabled;
}

void CollabService::setEnabled(bool enabled) {
    if (_enabled == enabled) return;
    _enabled = enabled;
    auto settingsPtr = AppPaths::settings();
    QSettings &settings = *settingsPtr;
    settings.setValue("Collab/enabled", enabled);
    emit enabledChanged(_enabled);
    // When the feature flips on, the open files might have sidecars we did
    // not pick up before. Re-evaluate every document so the menu state is
    // right whichever tab is active.
    if (_enabled) {
        for (auto it = _files.begin(); it != _files.end(); ++it) {
            if (it->path.isEmpty()) continue;
            it->initialized = it->history.load(it->path);
        }
        emit currentFileStateChanged();
    } else {
        for (auto it = _files.begin(); it != _files.end(); ++it) {
            it->lastSnapshot = QJsonArray();
        }
        emit currentFileStateChanged();
    }
}

CollabService::FileState *CollabService::stateFor(MidiFile *file) {
    if (!file) return nullptr;
    auto it = _files.find(file);
    return it == _files.end() ? nullptr : &it.value();
}

const CollabService::FileState *CollabService::stateFor(MidiFile *file) const {
    if (!file) return nullptr;
    auto it = _files.constFind(file);
    return it == _files.constEnd() ? nullptr : &it.value();
}

void CollabService::onFileLoaded(MidiFile *file, const QString &path) {
    _eventAuthor.clear();  // session-only highlight tags don't survive file changes
    _activeFile = file;

    if (file) {
        // WHY: with several open documents this fires on every tab switch.
        // Activation is a rebind, not a reload - a document we already know
        // keeps its history and its save baseline; only a file we have not
        // seen (or one whose path changed underneath us) gets a fresh record.
        FileState *known = stateFor(file);
        if (!known || known->path != path) {
            FileState fresh;
            fresh.path = path;
            if (_enabled && !path.isEmpty()) {
                fresh.initialized = fresh.history.load(path);
                if (fresh.initialized) {
                    // Capture the in-memory state as our baseline. Subsequent
                    // saves will diff against this until they replace it.
                    fresh.lastSnapshot = MidiSnapshot::ofFile(file);
                }
            }
            _files.insert(file, fresh);
        }
    }
    // Phase 9.5i: a different file is now active; the known-sidecars
    // cache covers this folder + the shared root, so re-scan to pick
    // up sidecars in the newly-active file's directory.
    if (_knownSidecarsLoaded) refreshKnownSidecars();
    emit activeFileChanged(file);
    emit currentFileStateChanged();
}

void CollabService::forgetFile(MidiFile *file) {
    if (!file) return;
    const bool known = _files.contains(file);
    // Listeners (the live session) still see a valid MidiFile here; the
    // record is dropped right after.
    if (known) emit fileClosing(file);
    _files.remove(file);
    if (_activeFile == file) {
        _activeFile = nullptr;
        emit currentFileStateChanged();
    }
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

    MidiFile *key = file ? file : _activeFile;
    if (!key) { clearPendingMerge(); return; }
    // A document saved before it was ever activated gets its record here.
    FileState &st = _files[key];

    // The path may have changed (Save As to a different file). Refresh
    // the document's state to follow the new path.
    if (path != st.path) {
        st.path = path;
        st.initialized = st.history.load(path);
        // Reset baseline; whatever was here referred to the old file.
        st.lastSnapshot = QJsonArray();
    }

    if (!st.initialized) { clearPendingMerge(); return; }  // file not opted in for collab

    QString hash = MidiHash::sha256OfFile(path);
    // BUG-COLLAB-013 (cont.): these two exits are ordinary "nothing to
    // commit" cases, but they must still consume the marker - a no-op
    // merge that hashes to the current head would otherwise leave it
    // armed and mis-attribute the next unrelated save to the PR author.
    if (hash.isEmpty()) { clearPendingMerge(); return; }
    if (hash == st.history.currentHead()) { clearPendingMerge(); return; }  // nothing changed

    // Compute hunks: diff(last known snapshot, current in-memory state).
    QJsonArray newSnapshot = MidiSnapshot::ofFile(file);
    QJsonArray hunks;
    if (file && !st.lastSnapshot.isEmpty()) {
        hunks = MidiDiff::compute(st.lastSnapshot, newSnapshot, file->ticksPerQuarter());
    }

    QString parentHash = st.history.currentHead();

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

    st.history.appendCommit(
        hash,
        parentHash,
        commitAuthor,
        CollabIdentity::machineId(),
        QDateTime::currentSecsSinceEpoch(),
        commitMessage,
        hunks);

    st.history.save(st.path);
    st.lastSnapshot = newSnapshot;
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
    return currentSidecarJson(_activeFile);
}

QJsonObject CollabService::currentSidecarJson(MidiFile *file) const {
    const FileState *st = stateFor(file);
    if (!st || !st->initialized) return QJsonObject();
    return st->history.toJson();
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
    //  1. The currently-active file's folder, then the folders of the
    //     other open documents (so a collab-init'd file opened from
    //     anywhere is immediately discoverable).
    //  2. Documents/MidiEditor_AI/shared/ — where LAN file-transfers land.
    QStringList scanDirs;
    auto addDirOf = [&scanDirs](const QString &midiPath) {
        if (midiPath.isEmpty()) return;
        QFileInfo fi(midiPath);
        QString d = fi.absolutePath();
        if (!d.isEmpty() && !scanDirs.contains(d)) scanDirs.append(d);
    };
    if (const FileState *active = stateFor(_activeFile)) addDirOf(active->path);
    for (auto it = _files.constBegin(); it != _files.constEnd(); ++it) {
        addDirOf(it->path);
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
    // The sidecar lands next to the file it was shipped for - the
    // session's document - not next to whichever tab is active.
    FileState *st = stateFor(file ? file : _activeFile);
    if (!st || st->path.isEmpty()) {
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

    st->history = incoming;
    if (!st->history.save(st->path)) {
        qWarning() << "CollabService::adoptRemoteSidecar — incoming sidecar "
                      "valid but save to" << st->path
                   << "failed (permissions / disk full?)";
        return false;
    }
    st->initialized = true;
    if (file) st->lastSnapshot = MidiSnapshot::ofFile(file);
    // Phase 9.5i: peer just adopted a host's sidecar; the file is now
    // discoverable by its (host-assigned) sessionId.
    if (_knownSidecarsLoaded) refreshKnownSidecars();
    emit currentFileStateChanged();
    qInfo() << "CollabService::adoptRemoteSidecar OK — adopted"
            << sidecarJson.value(QStringLiteral("history")).toArray().size()
            << "history entries into" << st->path;
    return true;
}

QString CollabService::liveCommitHash(const QJsonArray &snapshot,
                                      const QString &author,
                                      qint64 tsMs) {
    QByteArray seed = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
    seed.append(QByteArray::number(tsMs));
    seed.append(author.toUtf8());
    return QString::fromUtf8(
        QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex());
}

QString CollabService::recordRemoteLiveSync(MidiFile *file,
                                             const QString &author,
                                             const QString &machineId,
                                             const QString &message,
                                             const QJsonArray &hunks,
                                             const QString &commitHash) {
    if (!_enabled || !file) return QString();
    // Keyed by the file the hunks were applied to, so a live session on a
    // background tab never writes into the active tab's sidecar.
    FileState *st = stateFor(file);
    if (!st || !st->initialized) return QString();

    QJsonArray currentSnapshot = MidiSnapshot::ofFile(file);
    qint64 ts = QDateTime::currentMSecsSinceEpoch();
    // A host-assigned hash wins so every peer's chain matches the host's;
    // a locally synthesized one is only for commits nobody else records.
    QString hash = commitHash.isEmpty()
        ? liveCommitHash(currentSnapshot, author, ts)
        : commitHash;

    st->history.appendCommit(
        hash,
        st->history.currentHead(),
        author,
        machineId,
        ts / 1000,
        message,
        hunks);

    st->history.save(st->path);
    st->lastSnapshot = currentSnapshot;
    emit currentFileStateChanged();
    return hash;
}

int CollabService::compactHistory(int keepLastN) {
    FileState *st = stateFor(_activeFile);
    if (!st || !st->initialized || st->path.isEmpty()) return 0;
    int n = st->history.compactHistory(keepLastN);
    if (n > 0) {
        st->history.save(st->path);
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
    return isInitialized(_activeFile);
}

bool CollabService::isInitialized(MidiFile *file) const {
    const FileState *st = stateFor(file);
    return st && st->initialized;
}

bool CollabService::hasCurrentFile() const {
    const FileState *st = stateFor(_activeFile);
    return st && !st->path.isEmpty();
}

bool CollabService::initializeCurrentFile(MidiFile *file, const QString &commitMessage) {
    return initializeFile(file ? file : _activeFile, commitMessage);
}

bool CollabService::initializeFile(MidiFile *file, const QString &commitMessage) {
    if (!_enabled) return false;
    FileState *st = stateFor(file);
    if (!st || st->path.isEmpty()) return false;
    if (st->initialized) return false;
    if (CollabHistoryFile::exists(st->path)) {
        // Sidecar exists on disk but we did not load it (e.g. previously
        // failed parse). Try to load it now and treat as initialized.
        if (st->history.load(st->path)) {
            st->initialized = true;
            st->lastSnapshot = MidiSnapshot::ofFile(file);
            emit currentFileStateChanged();
        }
        return false;
    }

    QString hash = MidiHash::sha256OfFile(st->path);
    if (hash.isEmpty()) return false;

    st->history = CollabHistoryFile();
    st->history.setBranch(QStringLiteral("main"));
    st->history.ensureSessionId();  // assign a fresh UUID for this session
    // Initial commit: no parent, no hunks (nothing to diff against).
    st->history.appendCommit(
        hash,
        QString(),  // parentHash = empty for the initial commit
        CollabIdentity::displayName(),
        CollabIdentity::machineId(),
        QDateTime::currentSecsSinceEpoch(),
        commitMessage.isEmpty() ? QStringLiteral("Initialize for collaboration")
                                : commitMessage);

    if (!st->history.save(st->path)) return false;
    st->initialized = true;
    st->lastSnapshot = MidiSnapshot::ofFile(file);
    // Phase 9.5i: a freshly-init'd file should be discoverable by
    // sessionId immediately (e.g. for a peer joining shortly after).
    if (_knownSidecarsLoaded) refreshKnownSidecars();
    emit currentFileStateChanged();
    return true;
}

QString CollabService::currentHead() const {
    return currentHead(_activeFile);
}

QString CollabService::currentHead(MidiFile *file) const {
    const FileState *st = stateFor(file);
    return (st && st->initialized) ? st->history.currentHead() : QString();
}

QString CollabService::sessionId() const {
    return sessionId(_activeFile);
}

QString CollabService::sessionId(MidiFile *file) const {
    const FileState *st = stateFor(file);
    return (st && st->initialized) ? st->history.sessionId() : QString();
}

QJsonArray CollabService::history() const {
    const FileState *st = stateFor(_activeFile);
    return (st && st->initialized) ? st->history.history() : QJsonArray();
}

QString CollabService::lastSharedHead() const {
    const FileState *st = stateFor(_activeFile);
    return (st && st->initialized) ? st->history.lastSharedHead() : QString();
}

void CollabService::markCurrentAsShared() {
    FileState *st = stateFor(_activeFile);
    if (!st || !st->initialized) return;
    QString head = st->history.currentHead();
    if (head.isEmpty()) return;
    if (head == st->history.lastSharedHead()) return;  // no-op

    st->history.setLastSharedHead(head);
    st->history.save(st->path);
    emit currentFileStateChanged();
}
