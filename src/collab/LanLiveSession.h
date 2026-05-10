/*
 * MidiEditor AI
 *
 * Singleton coordinator for LAN Live Mode (Plan §11).
 *
 * Role-aware (Idle / Hosting / Joined). Owns the discovery socket and
 * the TCP transport, runs a sync timer, and bridges to the existing
 * MidiSnapshot / MidiDiff / PrApply pipeline so live edits flow
 * through the same code paths as PRs.
 *
 * Wire format (JSON, framed by LanTransport's 4-byte length prefix):
 *
 *   { "type": "hello",     "sessionId": "...", "displayName": "...", "machineId": "..." }
 *   { "type": "snapshot",  "events": [...]  }     // initial state-sync on join
 *   { "type": "hunks",     "author": "...", "machineId": "...", "hunks": [...] }
 *   { "type": "heartbeat"  }
 *
 * Host-star topology (Plan §11.1): the user who clicked "Start LAN
 * Live Session" is Host (LanServer); everyone else is Client
 * (LanClient connecting to the host). Broadcast goes through the host;
 * a client never talks directly to another client.
 */

#ifndef LANLIVESESSION_H
#define LANLIVESESSION_H

#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "LanDiscovery.h"
#include "LanTransport.h"
#include "PrBundle.h"

class MidiFile;
class QTimer;
#ifdef MIDIEDITOR_WEBRTC_ENABLED
class RtcRendezvousClient;
#endif

class LanLiveSession : public QObject {
    Q_OBJECT

public:
    enum class Role {
        Idle,
        Hosting,
        Joined,
    };

    /** \brief Which transport carries the active session, if any. Used by
     *  the UI to label menu actions and the status-bar indicator
     *  ("Local" vs "Online") without leaking transport-layer concepts.
     *  None when role is Idle; otherwise mirrors which start/join entry
     *  point was called. See Plan §11.10k. */
    enum class Transport {
        None,
        Lan,
        Wan,
    };

    static LanLiveSession *instance();

    Role role() const { return _role; }
    Transport transport() const { return _transport; }

    // ---- Lifecycle ---------------------------------------------------

    /**
     * \brief Start hosting a LAN Live Session.
     *
     * Picks an OS-assigned TCP port, generates a 4-character pairing
     * code, starts multicast announcing. Returns the pairing code on
     * success, empty string on failure.
     */
    QString startHosting(MidiFile *file);

    /**
     * \brief Join a LAN Live Session by pairing code.
     *
     * Begins listening on the multicast group; when a host announcement
     * with the matching code arrives, opens a TCP connection. Async —
     * emits joined() / joinFailed() / pairingTimedOut().
     *
     * \a file may be null when the joining peer has nothing open: the
     * host will then transfer its own file via a `filetransfer` frame
     * and the local UI prompts the user to accept (Plan §11 file-on-join).
     */
    void joinSession(MidiFile *file, const QString &pairingCode);

#ifdef MIDIEDITOR_WEBRTC_ENABLED
    /**
     * \brief Start hosting a WAN session via the Cloudflare rendezvous
     *        (Plan §11.10, Phase 9.6). Async: the 4-character code is
     *        emitted via \ref wanCodeReady once the rendezvous accepts
     *        our offer; the data channel actually opens later when a
     *        peer joins. Returns true when the local setup succeeded
     *        and the rendezvous call is in flight; false on missing
     *        file or transport-create failure.
     */
    bool startHostingWan(MidiFile *file);

    /**
     * \brief Join a WAN session by 4-character rendezvous code. Async:
     *        emits \ref joined / \ref joinFailed once the WebRTC channel
     *        completes (or fails). \a file may be null — the host then
     *        offers its file via \c filetransfer like in the LAN flow.
     */
    void joinSessionWan(MidiFile *file, const QString &code);
#endif

    /**
     * \brief Bind an active MidiFile to the live session after-the-fact.
     *
     * Used by the file-on-join flow: when a client joined without a file
     * and the host shipped one, MainWindow opens the transferred .mid
     * and calls this so subsequent sync ticks have something to diff.
     * Resets the baseline snapshot to the new file's current state.
     */
    void setActiveFile(MidiFile *file);

    // ---- Review mode (Phase 9.5h, §11.10c) -------------------------

    /**
     * \brief Whether incoming live edits are paused for review instead
     *        of auto-applied. Persisted across sessions via QSettings
     *        ("Collab/lan/reviewMode"). Default false (current
     *        live-edit behavior).
     */
    bool isReviewModeEnabled() const { return _reviewModeEnabled; }

    /** \brief Toggle review mode. Persists to QSettings and emits
     *  \ref reviewModeChanged. When turned off while hunks are
     *  queued, the queued bundle is left intact — call
     *  \ref discardPendingReview to clear it. */
    void setReviewMode(bool enabled);

    /** \brief Number of hunks waiting in the pending-review queue. */
    int pendingReviewHunkCount() const { return _pendingReviewHunks.size(); }

    /** \brief Synthesized PrBundle aggregating all queued incoming
     *  hunks. Suitable for feeding to PrReviewDialog. Returns invalid
     *  bundle when nothing is queued. */
    PrBundle pendingReviewBundle() const;

    /**
     * \brief Called after PrReviewDialog applied the user's selection
     *        to our file. Clears the pending queue and re-baselines
     *        the sync snapshot so the next sync tick doesn't broadcast
     *        the just-applied hunks back to the peer (which would
     *        cause a redundant network round-trip).
     */
    void acknowledgeReviewApplied();

    /** \brief Drop everything currently queued without applying. */
    void discardPendingReview();

    /**
     * \brief Host-side response to \ref returningPeerArrived: accept
     *        the peer's selected hunks and broadcast the merge result.
     *
     * Caller (ReturningPeerDialog wrapper) provides the peer-token
     * received with the offer, the hunks the host selected, and the
     * list of hashes the host explicitly rejected (so the peer can
     * tell the user which of its commits weren't accepted).
     */
    void acceptReturningPeerMerge(const QString &peerToken,
                                  const QJsonArray &acceptedHunks,
                                  const QJsonArray &rejectedHunks,
                                  const QStringList &rejectedCommitHashes);

    /**
     * \brief Host-side response to \ref returningPeerArrived: drop the
     *        connection because the host doesn't want to merge.
     */
    void rejectReturningPeer(const QString &peerToken,
                             const QString &reason);

    /**
     * \brief Leave whatever session we're in (host or client) and
     *        return to Idle. No-op if already Idle.
     */
    void leaveSession();

    // ---- Status ------------------------------------------------------

    QString pairingCode() const { return _pairingCode; }
    int peerCount() const;
    QString hostDisplayName() const { return _hostDisplayName; }

    /** \brief Display names of currently connected peers (host-side only). */
    QStringList peerLabels() const;

signals:
    void roleChanged(Role newRole);
    void peerCountChanged(int newCount);
    /** \brief Fired when a peer's display name becomes known (hello frame
     *  arrived) without the count itself changing. Lets UIs refresh the
     *  visible peer list label. */
    void peerLabelsChanged();
    void joined(const QString &hostDisplayName);
    void joinFailed(const QString &reason);
    void statusMessage(const QString &message);

#ifdef MIDIEDITOR_WEBRTC_ENABLED
    /** \brief WAN host-side: the rendezvous accepted our offer and
     *  returned the 4-character code the user should share with the
     *  joining peer. Fires before any peer has connected; the dialog
     *  uses this to swap from "contacting rendezvous…" to "share
     *  this code". */
    void wanCodeReady(const QString &code);
#endif

    /**
     * \brief Host has offered to ship its MIDI file to us (we joined
     *        without one). The UI layer should prompt the user with a
     *        Yes/No and call \ref acceptFileTransfer or
     *        \ref rejectFileTransfer.
     *
     * Bytes are the raw .mid as the host had it on disk; filename is
     * the basename without the path so the UI can show it in the prompt
     * and pick a save location.
     */
    void fileTransferOffered(const QString &filename, const QByteArray &bytes);

    /**
     * \brief Phase 9.5i: a host announcement matched a local sidecar.
     *        Connected synchronously to MainWindow::openFile so the
     *        switch happens before our TCP connect. After the slot
     *        returns, this class proceeds with connectToHost using
     *        the now-active file's sessionId/head/tail in `hello`,
     *        which routes naturally into §11.10b reconciliation.
     */
    void switchToFileBeforeConnect(const QString &midiPath);

    /**
     * \brief Host-side: a returning peer has joined with commits the
     *        host doesn't have (LocalBehindRemote or Diverged per
     *        §11.10b). The UI should show a `ReturningPeerDialog` with
     *        Accept-all / Review / Reject options, then call
     *        \ref acceptReturningPeerMerge or \ref rejectReturningPeer.
     *
     * \a proposedBundle aggregates the peer's commits-since-fork for
     * direct feeding into \c PrReviewDialog. \a peerToken is an
     * opaque handle the UI passes back when answering — it identifies
     * which peer the response is for. \a peerName is the display
     * name for the prompt label.
     */
    void returningPeerArrived(const QString &peerName,
                              const QString &peerToken,
                              const PrBundle &proposedBundle);

    /**
     * \brief Peer-side: the merge has been resolved by the host; show
     *        the user a summary of what was applied vs rejected.
     */
    void welcomeBackOffered(const QString &hostName,
                            int acceptedHunkCount,
                            int rejectedCommitCount,
                            const QString &divergedFilePath);

    /** \brief Review-mode toggle changed (settings or programmatic). */
    void reviewModeChanged(bool enabled);

    /** \brief A peer's hunks were queued instead of applied (review
     *  mode is on). \a pendingCount is the new total queue size. */
    void pendingReviewChanged(int pendingCount);

private slots:
    void onSyncTick();
    /** \brief Phase 9.8 baseline: log traffic rate + totals every 30 s
     *  while a session is active. Lets us measure host bandwidth
     *  before flipping the multi-peer cap so we can size the queue /
     *  adaptive-rate logic against real numbers, not guesses. */
    void onTrafficLogTick();
    /** \brief BUG-COLLAB-030 follow-up: periodic heartbeat. Host sends
     *  a tiny `heartbeat` frame to every peer every 10 s and marks
     *  any peer silent for >30 s as gone (closeConnection); joiner
     *  side sends heartbeats to the host so the host's check works.
     *  Without this, an OS that doesn't report socket death (Wi-Fi
     *  toggled off, Ethernet unplugged) leaves the host's TCP socket
     *  hanging in "Connected" for hours, producing ghost peers when
     *  the same client reconnects. */
    void onHeartbeatTick();
    void onPeerFound(const QString &sessionId,
                     const QString &displayName,
                     const QHostAddress &hostAddress,
                     quint16 tcpPort);
    void onClientConnected();
    void onClientConnectFailed(const QString &reason);
    void onClientDisconnected();
    void onClientMessage(const QByteArray &payload);
    void onServerPeerConnected(IPeerLink *peer);
    void onServerPeerDisconnected(IPeerLink *peer);
    void onServerMessage(IPeerLink *peer, const QByteArray &payload);

private:
    explicit LanLiveSession(QObject *parent = nullptr);

    void setRole(Role r);
    void resetState();

    /** \brief Record an outgoing payload for traffic accounting.
     *  \a payloadBytes is a single send's payload size; for a broadcast
     *  the caller multiplies by peer count to reflect bytes-on-the-wire.
     *  No-op when the traffic timer isn't running (i.e. outside an
     *  active session). */
    void recordSent(int payloadBytes, int peerMultiplier = 1);
    /** \brief Record an incoming payload (single peer source). */
    void recordReceived(int payloadBytes);

    // Wire helpers
    QByteArray encodeHello() const;
    /** \brief Plan §11.10q: encode an ack for a previously-received
     *  hunks frame so the sender can show "✓ N events accepted" in
     *  its status bar. Decoupled from `encodeHunks` because acks are
     *  small and don't include diff content. */
    QByteArray encodeHunksAck(quint64 frameId, int applied, int skipped) const;
    QByteArray encodeSnapshot(const QJsonArray &events) const;
    QByteArray encodeHunks(const QJsonArray &hunks) const;
    QByteArray encodeHeartbeat() const;
    QByteArray encodeCollabSync(const QJsonObject &sidecar) const;
    QByteArray encodeFileTransfer(const QString &filename, const QByteArray &bytes) const;
    QByteArray encodeJoinRejected(const QString &reason) const;
    QByteArray encodeHistoryRequest(const QString &fromHash, const QString &toHash) const;
    QByteArray encodeHistoryBundle(const QJsonArray &commits, const QString &fromHash) const;
    QByteArray encodeMergeResult(const QString &baseHead,
                                 const QString &newHead,
                                 const QJsonArray &acceptedHunks,
                                 const QJsonArray &rejectedHunks,
                                 const QStringList &rejectedCommitHashes) const;

    // Frame handlers
    void handleIncomingHunks(const QString &author,
                             const QString &machineId,
                             const QJsonArray &hunks,
                             const QString &actionLabel = QString());
    /** \brief Plan §11.10q: receiver of an ack for one of OUR previous
     *  hunks broadcasts. Surfaces the result in the status bar so the
     *  user sees "✓ 17 events accepted" or "⚠ 3 of 17 dropped on PC2"
     *  instead of guessing whether a sync round-tripped. */
    void handleIncomingHunksAck(const QString &ackBy,
                                quint64 frameId,
                                int applied,
                                int skipped);
    void handleIncomingSnapshot(const QJsonArray &events);
    /** \brief Plan §11.10j: read the optional `fileMaxTick` field of a
     *  snapshot or hunks frame and grow our local file's end-tick if the
     *  remote's is larger. Never shrinks (would orphan local events).
     *  Silent no-op when the field is absent or we have no bound file. */
    void applyFileMaxTickFromFrame(const QJsonObject &frame);

    /** \brief Plan §11.10p: tear down + re-attach the
     *  `Protocol::actionFinished` listener whenever the bound file
     *  changes. Idempotent — calling with the same file is a no-op. */
    void rewireProtocolListener();
    void handleIncomingCollabSync(const QJsonObject &sidecar);
    void handleIncomingFileTransfer(const QString &filename, const QByteArray &bytes);
    void handleIncomingJoinRejected(const QString &reason);
    void handleIncomingHistoryRequest(IPeerLink *peer,
                                      const QString &fromHash,
                                      const QString &toHash);
    void handleIncomingHistoryBundle(const QJsonArray &commits,
                                     const QString &fromHash);
    void handleIncomingMergeResult(const QString &baseHead,
                                   const QString &newHead,
                                   const QJsonArray &acceptedHunks,
                                   const QJsonArray &rejectedHunks,
                                   const QStringList &rejectedCommitHashes);

    /** \brief Read the host file's on-disk bytes for shipping to a
     *         peer that has no file. Returns empty when the host has
     *         no path (unsaved file) or the file can't be read. */
    QByteArray readActiveFileBytes(QString *outFilename) const;

    Role _role = Role::Idle;
    Transport _transport = Transport::None;

    /** \brief Plan §11.10p: the most recent finished `Protocol` action
     *  description (e.g. "Chord Explode", "Track Split", "FFXIV Channel
     *  Fixer"). Captured on `Protocol::actionFinished`, included in the
     *  next outbound hunks frame as `actionLabel`, cleared after
     *  broadcast. Receivers use it to label commits in their
     *  Collaboration log so the history shows WHICH tool produced the
     *  changes, not just "Live: N changes from PC2". */
    QString _lastActionLabel;

    /** \brief Plan §11.10q (Phase 9.7c "safety mode"): monotonic
     *  counter for outbound hunks frames so the receiver can ack each
     *  frame independently. Wraps in a quint64 — effectively never
     *  collides during a single session. Mutable so `encodeHunks` can
     *  stay const while still bumping the counter on each call. */
    mutable quint64 _outboundFrameSeq = 0;
    /** \brief Apply-result captured by `handleIncomingHunks` for the
     *  dispatcher to forward as `hunksAck`. Reset before each call. */
    int _lastApplyApplied = 0;
    int _lastApplySkipped = 0;
    /** \brief Tracks the connect() to `_file->protocol()->actionFinished`
     *  so it can be torn down + re-attached when the bound file
     *  changes (file-transfer-on-join, leaveSession, etc.). */
    QMetaObject::Connection _protocolActionFinishedConn;

    // Plan §11.10h: WAN auto-reconnect state. _retryAttempt counts the
    // number of times we've already retried this session; reset to 0 on
    // a successful connect (first peerConnected on host side, first
    // ILiveClient::connected on join side) and after leaveSession().
    int _retryAttempt = 0;
    // BUG-COLLAB-024: Generation counter checked inside the reconnect
    // QTimer::singleShot lambda.  Each leaveSession()/resetState bumps
    // it; if the captured generation no longer matches the current
    // value when the timer fires, the lambda bails out — preventing
    // a "Leave clicked, but session restarts itself two seconds later"
    // situation when the user leaves between failure and the retry.
    int _retryGeneration = 0;

    /** \brief Active MidiFile, held via QPointer so it auto-nulls if
     *  the MidiFile is destroyed (e.g. user loads/closes a file mid-
     *  session). Without this guard, the next \ref onSyncTick would
     *  read from a freed object and crash with an access violation
     *  (observed 2026-05-06 when the host loaded a new file while
     *  hosting). The activeFileChanged hook below closes the
     *  session cleanly so we don't keep ticking on a stale ptr. */
    QPointer<MidiFile> _file;
    QString _pairingCode;
    QString _hostDisplayName;

    LanDiscovery *_discovery = nullptr;
    // Transport-agnostic — concrete type is LanServer/LanClient for
    // LAN multicast or WebRtcLiveServer/WebRtcLiveClient for WAN
    // (Plan §11.10). The wire-protocol handlers below don't care.
    ILiveServer *_server = nullptr;     // valid only when hosting
    ILiveClient *_client = nullptr;     // valid only when joined
    QTimer *_syncTimer = nullptr;
    QTimer *_heartbeatTimer = nullptr;

    // Phase 9.8 baseline: traffic counters logged every 30 s so we have a
    // characteristic curve (Kennlinie) of host bandwidth before we extend
    // to multi-peer WAN. recordSent multiplies by the active peer count
    // for broadcasts so the figure tracks "bytes pushed onto the wire",
    // not "bytes per send call". Reset in resetState.
    quint64 _bytesOutTotal = 0;
    quint64 _bytesInTotal = 0;
    quint64 _framesOutTotal = 0;
    quint64 _framesInTotal = 0;
    quint64 _bytesOutPeak1s = 0;          // peak per-second sample observed
    quint64 _bytesInPeak1s = 0;
    quint64 _bytesOutWindow = 0;          // accumulating between log ticks
    quint64 _bytesInWindow = 0;
    quint64 _framesOutWindow = 0;
    quint64 _framesInWindow = 0;
    qint64  _trafficWindowStartMs = 0;    // wall-clock start of current window
    QTimer *_trafficLogTimer = nullptr;
#ifdef MIDIEDITOR_WEBRTC_ENABLED
    /** \brief HTTP signaling client used by the WAN flow; null otherwise.
     *  Lives only between startHostingWan/joinSessionWan and the first
     *  peerConnected (rendezvous is bootstrap-only — see §11.10). */
    RtcRendezvousClient *_rdv = nullptr;
#endif

    QJsonArray _lastSyncedSnapshot;
    /** \brief Last `fileMaxTick` value we sent to the peer(s) — sync
     *  tick force-emits a frame when this differs from the file's
     *  current endTick, even if the event-diff is empty. Without this,
     *  insert / delete-measures into an event-free zone shifts no
     *  events, produces no hunks, and the file-length change never
     *  reaches the peer. -1 = nothing sent yet (initial state). */
    int _lastBroadcastEndTick = -1;

    /** \brief Sidecar JSON received from the host before the joining
     *  client had a file open. Replayed by setActiveFile() once the user
     *  accepts the file transfer. Empty in the normal both-files-open
     *  flow. */
    QJsonObject _pendingSidecar;

    /** \brief Last sidecar `currentHead` we broadcast to peers (host-
     *  side). Used to detect host-side sidecar changes (mid-session
     *  collab init, adoption from another peer, compaction) and re-
     *  ship the sidecar so peers stay in sync. Empty when no broadcast
     *  has happened yet in this session. */
    QString _lastBroadcastSidecarHead;

    /** \brief True between receiving a `filetransfer` frame and
     *  setActiveFile() being called by the UI layer. While set, any
     *  incoming `collabsync` is buffered into _pendingSidecar — never
     *  applied — even if _file is non-null. Without this guard the
     *  modal Yes/No prompt's nested event loop lets a sidecar arrive
     *  and clobber the user's pre-existing local sidecar. */
    bool _filetransferPending = false;

    // ---- Returning-peer reconciliation state (Phase 9.5g) -----------

    /** \brief Per-peer state held by the host between receiving hello
     *  with diverged history and receiving the historybundle response.
     *  Keyed by peer machineId (carried in hello). */
    struct PendingMerge {
        IPeerLink *peer = nullptr;
        QString peerName;
        QString peerHead;
        QString commonAncestor;
    };
    QHash<QString, PendingMerge> _pendingMerges;

    /** \brief Client-side: where to write the user's pre-merge state
     *  if the host's merge result rejects some of our commits. Set by
     *  setActiveFile and cleared after one merge cycle. */
    QString _activeFilePath;

    // ---- Review mode (Phase 9.5h) -------------------------------

    /** \brief When true, incoming hunks frames are queued into
     *  _pendingReviewHunks instead of auto-applied. Loaded from
     *  QSettings on construction; persisted via setReviewMode. */
    bool _reviewModeEnabled = false;

    /** \brief Hunks accumulated while review mode is on, waiting
     *  for the user's Accept/Cancel decision in PrReviewDialog. */
    QJsonArray _pendingReviewHunks;

    /** \brief Display name of the most recent peer whose hunks were
     *  queued — used as the synthesized bundle's `author`. */
    QString _pendingReviewAuthor;
    QString _pendingReviewMachineId;
};

#endif // LANLIVESESSION_H
