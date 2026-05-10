/*
 * MidiEditor AI
 *
 * Collaboration service singleton.
 *
 * Owns the runtime opt-in toggle for collaboration features. When disabled
 * (the default), no menu items appear, no signals are connected, no sidecar
 * files are written, and no QSettings keys outside Collab/* are touched.
 *
 * The compile-time guard MIDIEDITOR_COLLAB_ENABLED already controls whether
 * any of this code is built at all; this runtime layer adds the second
 * opt-in stage. A user who builds the feature in but leaves the toggle off
 * pays only the cost of one bool check at boot and a single unused
 * QSettings key.
 *
 * The service also tracks the currently-active MIDI file's collaboration
 * state (Phase 9.1b): when a file is loaded, we look for a sidecar history
 * next to it; when a file is saved, we append a new commit to that history
 * if it exists. Files without a sidecar are untouched — the user must
 * explicitly opt them in via initializeCurrentFile().
 */

#ifndef COLLABSERVICE_H
#define COLLABSERVICE_H

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QString>

#include "CollabHistoryFile.h"

class MidiEvent;
class MidiFile;

class CollabService : public QObject {
    Q_OBJECT

public:
    /**
     * \brief Process-wide singleton accessor.
     *
     * Lazily instantiated on first call. Idempotent.
     */
    static CollabService *instance();

    /**
     * \brief Whether collaboration features are runtime-enabled.
     *
     * Reads Collab/enabled from QSettings. Default false.
     * Cheap to call from hot paths; the value is cached.
     */
    bool isEnabled() const;

    /**
     * \brief Toggle collaboration features at runtime.
     *
     * Persists to QSettings and emits enabledChanged() so that any UI that
     * needs to show or hide collab-specific elements can react.
     */
    void setEnabled(bool enabled);

    // ---- File lifecycle hooks (called from MainWindow) ------------------

    /**
     * \brief Notify the service that a new file is now active.
     *
     * Loads the sidecar history if one exists. No-op when collab is
     * disabled at runtime. Pass a null \a file or empty \a path to clear
     * the active state (e.g. on application shutdown).
     */
    void onFileLoaded(MidiFile *file, const QString &path);

    /**
     * \brief Notify the service that the active file has been saved.
     *
     * If the file has a sidecar history and \a file is non-null, computes
     * the SHA-256 of the .mid on disk and appends a new commit referencing
     * it. The new commit also carries `parentHash` (= previous head) and
     * `hunks` (= MidiDiff between the last-known snapshot and the current
     * in-memory state). Updates the last-known snapshot to the current
     * one in preparation for the next save. No-op when collab is disabled
     * or when the file is not collab-initialized.
     */
    void onFileSaved(MidiFile *file, const QString &path);

    /**
     * \brief Whether the active file has a sidecar history.
     *
     * Used by the File menu to enable/disable the "Initialize" action.
     */
    bool isCurrentFileInitialized() const;

    /**
     * \brief Whether a file is currently active (path is set).
     */
    bool hasCurrentFile() const;

    /**
     * \brief Initialize the active file for collaboration.
     *
     * Creates the sidecar history file next to the .mid and records an
     * initial commit referencing the current bytes on disk. The initial
     * commit has empty hunks (nothing to diff against). The MIDI file
     * itself is not modified. Returns false when:
     *   - collab is disabled
     *   - no file is active
     *   - the file has not been saved yet (no path on disk)
     *   - a sidecar already exists (call site should disable the action)
     *   - I/O error writing the sidecar
     */
    bool initializeCurrentFile(MidiFile *file, const QString &commitMessage);

    /**
     * \brief Mark that the next save will be the persistence step of a
     *        PR-merge (per Plan §10 / 9.1e). The author + message of
     *        the originating PR are remembered; the next \c onFileSaved
     *        consumes the marker and writes the resulting history entry
     *        with that author/message instead of the default
     *        "Save" / local user.
     *
     * Marker is single-use: any later save without an explicit re-mark
     * falls back to the normal local "Save" behavior.
     */
    void markPendingMerge(const QString &author, const QString &message);

    // ---- Per-event author tracking (session-only, for visual hints) -----

    /**
     * \brief Tag a live MidiEvent as having originated from a PR merge.
     *
     * Called by PrApply for every event it creates. The tag drives the
     * MatrixWidget highlight overlay and the hover tooltip. Tags are
     * session-only — they're cleared on file load/unload (see Plan §9.5
     * "Not a snapshot store"). The map keys are raw MidiEvent pointers;
     * the values never participate in pointer dereference, so a stale
     * pointer (event later deleted) is harmless — it just won't be
     * found by future lookups.
     */
    void registerEventAuthor(MidiEvent *event, const QString &author);

    /**
     * \brief Return the author tagged for \a event, or empty string.
     *
     * Cheap (single QHash lookup). Safe to call from a paint loop.
     */
    QString eventAuthor(MidiEvent *event) const;

    /**
     * \brief True if any event has a tag — quick test for paint loops
     *        to skip the per-event lookup when the map is empty.
     */
    bool hasAnyEventAuthors() const;

    /** \brief The current head hash for the active file (empty if none). */
    QString currentHead() const;

    /**
     * \brief The sessionId of the active sidecar (empty if not initialized).
     */
    QString sessionId() const;

    /**
     * \brief The full history of the active file, oldest first.
     *
     * Returns an empty array when the file is not collab-initialized or
     * collaboration is disabled. The structure of each entry matches the
     * sidecar schema (see CollabHistoryFile).
     */
    QJsonArray history() const;

    /**
     * \brief The hash of the last commit explicitly shared as a PR.
     *
     * Empty if nothing has been shared yet. PrCreateDialog uses this as
     * the lower bound for hunk aggregation: a PR contains all hunks
     * from commits between this hash and the current head, so a user
     * who saved 5 times sends one consolidated PR rather than five.
     */
    QString lastSharedHead() const;

    /**
     * \brief Mark the current head as shared (Copy token / Save bundle /
     *        Post webhook). Persists the new value to the sidecar.
     *
     * Subsequent Create-PR invocations aggregate only commits made
     * after this point.
     */
    void markCurrentAsShared();

    /**
     * \brief Strip hunk data from history entries older than the most
     *        recent \a keepLastN commits. Preserves hash/author/message
     *        chain; just removes the bulky diff payloads. Persists to
     *        the sidecar and emits currentFileStateChanged so the UI
     *        refreshes.
     *
     * Returns the number of entries from which hunks were stripped (0
     * when everything is already compact or fits within the keep
     * threshold).
     */
    int compactHistory(int keepLastN);

    /**
     * \brief Append a non-save commit entry attributed to a remote peer.
     *
     * Used by LAN Live Mode (Plan §11.8): incoming hunks are applied to
     * the in-memory file with `silent=true` (no auto-save), so the regular
     * onFileSaved() commit path doesn't fire. Without this hook the
     * Collaboration log would never show remote edits. Synthesizes a
     * commit hash from the current in-memory snapshot (so two distinct
     * batches don't collapse to the same id), persists the sidecar, and
     * emits currentFileStateChanged.
     *
     * No-op if collab is disabled or the file isn't initialized.
     */
    void recordRemoteLiveSync(MidiFile *file,
                              const QString &author,
                              const QString &machineId,
                              const QString &message,
                              const QJsonArray &hunks);

    /** \brief Snapshot of the active sidecar's JSON representation.
     *  Empty object when no file is initialized for collaboration. */
    QJsonObject currentSidecarJson() const;

    /**
     * \brief Adopt a sidecar shipped from a peer (LAN Live host) and
     *        persist it next to the active file.
     *
     * Called on the client after receiving a `collabsync` frame. Replaces
     * the in-memory state, writes the sidecar JSON to disk, marks the
     * file initialized, and emits currentFileStateChanged so the
     * Collaboration log refreshes. No-op when collab is disabled, the
     * incoming object is empty, or there's no current file path.
     *
     * Returns true when the sidecar was adopted and persisted.
     */
    bool adoptRemoteSidecar(MidiFile *file, const QJsonObject &sidecarJson);

    /**
     * \brief Path to the .mid whose local sidecar carries \a sessionId,
     *        or empty string if we don't know about it.
     *
     * Used by the transport-agnostic returning-peer flow (Plan §11.10d,
     * Phase 9.5i): LAN multicast, WebRTC signaling, and Ctrl+V token
     * paste all call this with the inbound sessionId, then route to
     * \c MainWindow::openFile when the lookup hits — so the existing
     * §11.10b reconciliation always sees a peer with the right
     * sessionId/head/tail in its hello.
     *
     * Cache lives in this object; first call scans
     * \c Documents/MidiEditor_AI/shared/, subsequent calls are O(1) until
     * \ref refreshKnownSidecars() invalidates it.
     */
    QString findFileBySessionId(const QString &sessionId);

    /**
     * \brief Re-scan the known sidecar locations and rebuild the
     *        sessionId → path cache. Called automatically on file-load
     *        and after a \c filetransfer is accepted (so a freshly-
     *        received shared file is immediately discoverable). UI
     *        layers don't normally call this directly.
     */
    void refreshKnownSidecars();

signals:
    /**
     * \brief Emitted when the runtime toggle changes.
     */
    void enabledChanged(bool enabled);

    /**
     * \brief Emitted whenever the active file's collab state changes
     *        (loaded a new file, initialized current file, recorded a new
     *        commit). Listeners (e.g. menu enable/disable) can refresh.
     */
    void currentFileStateChanged();

    /**
     * \brief Emitted when the active MidiFile pointer changes (file load
     *        or unload). Lets widgets that need direct access to the
     *        live MidiFile (e.g. the history panel for hunk-click event
     *        lookup) track the current file without coupling to
     *        MainWindow internals. \a file may be null on unload.
     */
    void activeFileChanged(MidiFile *file);

private:
    explicit CollabService(QObject *parent = nullptr);

    bool _enabled;

    // Active-file state. Empty when no file is loaded or collab is off.
    QString _currentPath;
    CollabHistoryFile _currentHistory;
    bool _currentInitialized = false;

    // Last-known snapshot of the in-memory MidiFile. Updated on file load,
    // initialize, and save. Used as the parent state when computing hunks
    // for the next save's history entry.
    QJsonArray _lastSnapshot;

    // Pending-merge marker: set by markPendingMerge() right before the
    // file is saved as part of a PR merge. Consumed (cleared) by the next
    // onFileSaved() call so subsequent regular saves go back to the
    // normal local "Save" message.
    bool _pendingMergeValid = false;
    QString _pendingMergeAuthor;
    QString _pendingMergeMessage;

    // Session-only event-author tags for the highlight overlay. Cleared
    // on file load/unload. Pointers are never dereferenced; a stale
    // pointer (deleted event) just won't be found by future lookups.
    QHash<MidiEvent *, QString> _eventAuthor;

    // Phase 9.5i transport-agnostic sessionId → midi-path cache. Built
    // lazily on the first findFileBySessionId() call by scanning
    // Documents/MidiEditor_AI/shared/, refreshed by refreshKnownSidecars().
    QHash<QString, QString> _knownSidecarsBySessionId;
    bool _knownSidecarsLoaded = false;
};

#endif // COLLABSERVICE_H
