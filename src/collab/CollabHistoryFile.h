/*
 * MidiEditor AI
 *
 * Sidecar JSON file that tracks the collaboration history of one MIDI
 * file. Lives at "<song>.midiedit-collab.json" next to the .mid.
 *
 * Schema (v1, Phase 9.1b):
 * {
 *   "schemaVersion": 1,
 *   "currentHead":   "<sha256-hex>",   // hash of last saved state
 *   "branch":        "main",
 *   "history": [
 *     { "hash": "<hex>", "author": "<name>", "machineId": "<uuid>",
 *       "ts": <epoch-seconds>, "message": "<commit-msg>" },
 *     ...
 *   ]
 * }
 *
 * Future phases will add: parentHead (for branch-detection on PR import),
 * knownPeers (trusted display-name + machineId pairs), and structural
 * fields needed by the diff/PR layer.
 */

#ifndef COLLABHISTORYFILE_H
#define COLLABHISTORYFILE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class CollabHistoryFile {
public:
    static constexpr int kSchemaVersion = 1;

    /**
     * \brief Translate a MIDI file path to its sidecar history path.
     *
     * "C:/songs/foo.mid" -> "C:/songs/foo.midiedit-collab.json".
     * If \a midiPath has no extension or has a multi-dot name, the suffix
     * is appended to the basename without trying to be clever about it.
     */
    static QString sidecarPathFor(const QString &midiPath);

    /** \brief True if a sidecar file exists at the expected location. */
    static bool exists(const QString &midiPath);

    CollabHistoryFile();

    /**
     * \brief Load the sidecar from disk. Returns false on missing file or
     * malformed JSON; in either case the in-memory state is left empty.
     */
    bool load(const QString &midiPath);

    /**
     * \brief Adopt a sidecar JSON object directly (no disk read).
     *
     * Used by LAN Live Mode (Plan §11): when a client joins a host that
     * already has collaboration initialized, the host serializes its
     * sidecar via toJson() and ships it through the TCP transport; the
     * client adopts it via this method. Same lenient field handling as
     * load(): missing fields fall back to defaults; malformed entries
     * leave the state empty and return false.
     */
    bool loadFromJson(const QJsonObject &obj);

    /** \brief Serialize the current state to a JSON object (no disk I/O).
     *  Mirrors save()'s schema; useful for shipping to peers. */
    QJsonObject toJson() const;

    /**
     * \brief Write the current in-memory state to the sidecar JSON file
     * next to \a midiPath. Uses QSaveFile for an atomic replace.
     */
    bool save(const QString &midiPath) const;

    /** \brief The sha256-hex of the current head (empty before any commit). */
    QString currentHead() const { return _currentHead; }
    void setCurrentHead(const QString &hash) { _currentHead = hash; }

    /** \brief The branch name (defaults to "main"). */
    QString branch() const { return _branch; }
    void setBranch(const QString &name) { _branch = name; }

    /**
     * \brief Stable per-file UUID used to discriminate PRs across sessions.
     *
     * Generated lazily on first need (initialize or load of a sidecar
     * that lacks the field). Persisted on save. See Plan §6.2 +
     * §10.5 cross-session paste.
     */
    QString sessionId() const { return _sessionId; }
    void setSessionId(const QString &id) { _sessionId = id; }
    /** \brief Generate a fresh sessionId if and only if the current one is empty. */
    void ensureSessionId();

    /**
     * \brief Hash of the last commit that the user explicitly shared as
     *        a PR (Copy token / Save bundle / Post webhook).
     *
     * Empty if nothing has been shared yet. Used by PrCreateDialog to
     * aggregate hunks across all saves since the last share, so users
     * who save many small times still send one consolidated PR rather
     * than spamming peers with one PR per save.
     *
     * Persisted as a top-level field in the sidecar JSON.
     */
    QString lastSharedHead() const { return _lastSharedHead; }
    void setLastSharedHead(const QString &hash) { _lastSharedHead = hash; }

    /** \brief All commits, oldest first. */
    QJsonArray history() const { return _history; }

    /**
     * \brief Append one commit to the end of the history.
     *
     * Convenience helper that builds the JSON object inline and updates
     * \a currentHead in one step.
     *
     * \param parentHash Hash of the previous commit ("" for the initial
     *        commit). Persisted into the entry as `parentHash` per §6.2.
     * \param hunks Diff hunks describing what changed since the parent.
     *        May be empty (initial commit, or save with no event-level
     *        change).
     */
    void appendCommit(const QString &hash,
                      const QString &parentHash,
                      const QString &author,
                      const QString &machineId,
                      qint64 epochSeconds,
                      const QString &message,
                      const QJsonArray &hunks = QJsonArray());

    /** \brief True after a successful load() or appendCommit(). */
    bool isLoaded() const { return _loaded; }

    /**
     * \brief Strip the `hunks` field from older history entries to keep
     *        the sidecar from growing without bound.
     *
     * The most recent \a keepLastN entries retain their full hunks. Older
     * entries keep all metadata (hash, parentHash, author, machineId, ts,
     * message) but get an empty `hunks` array. This preserves the audit
     * chain while shrinking the sidecar — typically by 90+%.
     *
     * Returns the number of entries from which hunks were stripped.
     */
    int compactHistory(int keepLastN);

private:
    bool _loaded = false;
    QString _sessionId;
    QString _currentHead;
    QString _lastSharedHead;
    QString _branch = QStringLiteral("main");
    QJsonArray _history;
};

#endif // COLLABHISTORYFILE_H
