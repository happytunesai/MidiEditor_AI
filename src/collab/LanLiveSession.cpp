/*
 * MidiEditor AI - LanLiveSession implementation.
 */

#include "LanLiveSession.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMultiMap>
#include <QSettings>
#include <QTcpSocket>
#include <QTimer>

#include "../MidiEvent/MidiEvent.h"
#include "../midi/MidiChannel.h"
#include "../midi/MidiFile.h"
#include "../protocol/Protocol.h"
#include "../protocol/ProtocolStep.h"
#include "CollabIdentity.h"
#include "CollabService.h"
#include "HistoryReconciliation.h"
#include "MidiDiff.h"
#include "MidiSnapshot.h"
#include "PrApply.h"

#ifdef MIDIEDITOR_WEBRTC_ENABLED
#include "RtcRendezvousClient.h"
#include "WebRtcLiveClient.h"
#include "WebRtcLiveServer.h"
#endif

Q_DECLARE_LOGGING_CATEGORY(lanLog)

namespace {
LanLiveSession *s_instance = nullptr;
constexpr int kSyncIntervalMs = 1000;

// Plan §11.10h connection-quality knobs. All three live in the
// app-wide ("MidiEditor","NONE") settings store alongside identity /
// webhook / rendezvous-URL. Defaults match the values hard-coded into
// the previous build so existing setups behave identically.
int loadIceGatheringTimeoutMs() {
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    int v = s.value(QStringLiteral("Collab/wan/iceGatheringTimeoutMs"), 8000).toInt();
    return qBound(2000, v, 30000);
}
bool loadAutoReconnect() {
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    return s.value(QStringLiteral("Collab/wan/autoReconnect"), false).toBool();
}
int loadAutoReconnectMaxAttempts() {
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    return qBound(1, s.value(QStringLiteral("Collab/wan/autoReconnectMaxAttempts"), 2).toInt(), 5);
}
constexpr int kRetryBackoffMs = 2000;

// BUG-COLLAB-030: liveness check for connected peers. We send a tiny
// `heartbeat` frame to every peer at this cadence; if a peer hasn't
// been seen sending ANY frame (heartbeat or actual edit) within the
// silence deadline, we declare it gone and close the socket. This
// fires the disconnected() signal which prunes the ghost from the
// peer list. 10 s ping × 3 missed = 30 s deadline gives a comfortable
// window for short network glitches (Wi-Fi roams, brief ISP hiccups)
// without false-positive kicks, and the cadence is gentle on the
// rendezvous worker (5 peers × 1 hb / 10 s = 30 frames/min, trivial).
constexpr int kHeartbeatIntervalMs = 10000;
constexpr qint64 kPeerSilenceDeadlineMs = 30000;
}

LanLiveSession *LanLiveSession::instance() {
    if (!s_instance) {
        s_instance = new LanLiveSession(QCoreApplication::instance());
    }
    return s_instance;
}

LanLiveSession::LanLiveSession(QObject *parent) : QObject(parent) {
    // If the user loads / closes a MIDI file while a session is running,
    // the MainWindow deletes our previously-bound MidiFile. Catching
    // this signal lets us end the session cleanly rather than crash on
    // the next sync tick reading freed memory.
    connect(CollabService::instance(), &CollabService::activeFileChanged,
            this, [this](MidiFile *newFile) {
                if (_role == Role::Idle) return;
                if (newFile == _file.data()) return;  // our own setActiveFile
                if (_filetransferPending) return;      // file-on-join: we triggered this
                qCWarning(lanLog) << "session: active file changed externally —"
                                  << "ending LAN session to avoid stale pointer"
                                  << "(role=" << static_cast<int>(_role)
                                  << "newFile=" << newFile
                                  << "_file=" << _file.data()
                                  << "filetransferPending=" << _filetransferPending << ")";
                emit statusMessage(tr("LAN session ended — file was changed."));
                leaveSession();
            });

    // Review-mode preference is persisted across sessions (per §11.10c).
    QSettings s("MidiEditor", "NONE");
    _reviewModeEnabled = s.value("Collab/lan/reviewMode", false).toBool();

    // Re-broadcast the sidecar to all peers whenever the host's
    // collab state changes mid-session (e.g. user initializes collab
    // after the session was already running, or adopts a peer's
    // history). Without this, peers that joined while the host had
    // no sidecar are stuck at "not initialized" forever even though
    // hunks keep flowing — recordRemoteLiveSync silently drops every
    // hunk on an uninitialized file.
    connect(CollabService::instance(), &CollabService::currentFileStateChanged,
            this, [this]() {
                if (_role != Role::Hosting || !_server) return;
                QJsonObject sidecar = CollabService::instance()->currentSidecarJson();
                if (sidecar.isEmpty()) return;
                QString head = sidecar.value(QStringLiteral("currentHead")).toString();
                if (head == _lastBroadcastSidecarHead) return;
                _lastBroadcastSidecarHead = head;
                qCInfo(lanLog) << "session: host sidecar changed (head="
                               << head.left(8) << ") — broadcasting to"
                               << _server->peerCount() << "peer(s)";
                QByteArray syncFrame = encodeCollabSync(sidecar);
                if (_server->broadcast(syncFrame))
                    recordSent(syncFrame.size(), _server->peerCount());
            });
}

void LanLiveSession::setRole(Role r) {
    if (_role == r) return;
    _role = r;
    emit roleChanged(r);

    // Phase 9.8 baseline: spin the traffic log up on first transition
    // out of Idle, tear it down when we go back to Idle. Counters are
    // already zero (initialised in resetState); we only need to start
    // the periodic emit.
    if (r != Role::Idle) {
        if (!_trafficLogTimer) {
            _trafficWindowStartMs = QDateTime::currentMSecsSinceEpoch();
            _trafficLogTimer = new QTimer(this);
            _trafficLogTimer->setInterval(30000);  // every 30 s
            connect(_trafficLogTimer, &QTimer::timeout,
                    this, &LanLiveSession::onTrafficLogTick);
            _trafficLogTimer->start();
            qCInfo(lanLog) << "session: traffic logger armed (30 s interval)";
        }
        if (!_heartbeatTimer) {
            _heartbeatTimer = new QTimer(this);
            _heartbeatTimer->setInterval(kHeartbeatIntervalMs);
            connect(_heartbeatTimer, &QTimer::timeout,
                    this, &LanLiveSession::onHeartbeatTick);
            _heartbeatTimer->start();
            qCInfo(lanLog) << "session: heartbeat armed ("
                           << kHeartbeatIntervalMs << "ms interval, "
                           << kPeerSilenceDeadlineMs << "ms deadline)";
        }
    } else {
        if (_trafficLogTimer) {
            // Emit one last sample so the user sees a final summary.
            onTrafficLogTick();
            _trafficLogTimer->stop();
            _trafficLogTimer->deleteLater();
            _trafficLogTimer = nullptr;
        }
        if (_heartbeatTimer) {
            _heartbeatTimer->stop();
            _heartbeatTimer->deleteLater();
            _heartbeatTimer = nullptr;
        }
    }
}

void LanLiveSession::onHeartbeatTick() {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (_role == Role::Hosting && _server) {
        const QByteArray hb = encodeHeartbeat();
        // Snapshot the peer list — closeConnection() inside the loop
        // can mutate it via the disconnected() signal.
        QList<IPeerLink *> snapshot = _server->peers();
        int sentCount = 0;
        for (IPeerLink *peer : snapshot) {
            // 1. Send a heartbeat so the peer can update OUR
            //    lastSeenMs on their side.
            peer->sendMessage(hb);
            ++sentCount;
            // 2. Check if THIS peer has been silent too long. We give
            //    them a grace period equal to the deadline before any
            //    lastSeen is recorded — first heartbeat we receive
            //    establishes the timer, no need to kick yet.
            qint64 lastSeen = peer->lastSeenMs();
            if (lastSeen > 0 && (nowMs - lastSeen) > kPeerSilenceDeadlineMs) {
                qCWarning(lanLog) << "session: kicking silent peer"
                                  << peer->peerLabel()
                                  << "(silent for"
                                  << (nowMs - lastSeen) << "ms)";
                peer->closeConnection();
            }
        }
        if (sentCount > 0) recordSent(hb.size(), sentCount);
    } else if (_role == Role::Joined && _client && _client->isConnected()) {
        // Client side: send heartbeat so the host's silence-deadline
        // check sees fresh activity from us. We don't track the
        // host's silence on the client side because ILiveClient's
        // disconnected() fires when the local TCP/WebRTC layer
        // notices the socket dying — the client reacts there.
        const QByteArray hb = encodeHeartbeat();
        _client->sendMessage(hb);
        recordSent(hb.size(), 1);
    }
}

void LanLiveSession::recordSent(int payloadBytes, int peerMultiplier) {
    if (!_trafficLogTimer) return;
    if (payloadBytes <= 0 || peerMultiplier <= 0) return;
    const quint64 onWire =
        static_cast<quint64>(payloadBytes) * static_cast<quint64>(peerMultiplier);
    _bytesOutTotal   += onWire;
    _bytesOutWindow  += onWire;
    _framesOutTotal  += static_cast<quint64>(peerMultiplier);
    _framesOutWindow += static_cast<quint64>(peerMultiplier);
}

void LanLiveSession::recordReceived(int payloadBytes) {
    if (!_trafficLogTimer) return;
    if (payloadBytes <= 0) return;
    _bytesInTotal   += static_cast<quint64>(payloadBytes);
    _bytesInWindow  += static_cast<quint64>(payloadBytes);
    _framesInTotal  += 1;
    _framesInWindow += 1;
}

void LanLiveSession::onTrafficLogTick() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double elapsedSec = qMax<qint64>(now - _trafficWindowStartMs, 1) / 1000.0;
    const double bytesOutPerSec = _bytesOutWindow / elapsedSec;
    const double bytesInPerSec  = _bytesInWindow  / elapsedSec;
    const double framesOutPerSec = _framesOutWindow / elapsedSec;
    const double framesInPerSec  = _framesInWindow  / elapsedSec;

    if (bytesOutPerSec > _bytesOutPeak1s) _bytesOutPeak1s = quint64(bytesOutPerSec);
    if (bytesInPerSec  > _bytesInPeak1s)  _bytesInPeak1s  = quint64(bytesInPerSec);

    auto fmtBytes = [](double bps) -> QString {
        if (bps < 1024.0)             return QStringLiteral("%1 B/s").arg(bps, 0, 'f', 0);
        if (bps < 1024.0 * 1024.0)    return QStringLiteral("%1 KB/s").arg(bps / 1024.0, 0, 'f', 1);
        return QStringLiteral("%1 MB/s").arg(bps / (1024.0 * 1024.0), 0, 'f', 2);
    };
    auto fmtTotal = [](quint64 b) -> QString {
        if (b < 1024)              return QStringLiteral("%1 B").arg(b);
        if (b < 1024ULL * 1024)    return QStringLiteral("%1 KB").arg(b / 1024.0, 0, 'f', 1);
        if (b < 1024ULL * 1024 * 1024) return QStringLiteral("%1 MB").arg(b / (1024.0 * 1024.0), 0, 'f', 2);
        return QStringLiteral("%1 GB").arg(b / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    };

    qCInfo(lanLog).noquote() << QStringLiteral(
        "session: traffic — peers=%1 role=%2 | out %3 (%4 fr/s) "
        "in %5 (%6 fr/s) | session-total out=%7 in=%8 | peak out=%9 in=%10")
            .arg(peerCount())
            .arg(_role == Role::Hosting ? QStringLiteral("host")
                : _role == Role::Joined ? QStringLiteral("client") : QStringLiteral("idle"))
            .arg(fmtBytes(bytesOutPerSec))
            .arg(framesOutPerSec, 0, 'f', 1)
            .arg(fmtBytes(bytesInPerSec))
            .arg(framesInPerSec, 0, 'f', 1)
            .arg(fmtTotal(_bytesOutTotal))
            .arg(fmtTotal(_bytesInTotal))
            .arg(fmtBytes(_bytesOutPeak1s))
            .arg(fmtBytes(_bytesInPeak1s));

    // Reset rolling window.
    _bytesOutWindow  = 0;
    _bytesInWindow   = 0;
    _framesOutWindow = 0;
    _framesInWindow  = 0;
    _trafficWindowStartMs = now;
}

void LanLiveSession::resetState() {
    if (_syncTimer) {
        _syncTimer->stop();
        _syncTimer->deleteLater();
        _syncTimer = nullptr;
    }
    if (_discovery) {
        _discovery->deleteLater();
        _discovery = nullptr;
    }
    if (_server) {
        _server->stop();
        _server->deleteLater();
        _server = nullptr;
    }
    if (_client) {
        _client->disconnectFromHost();
        _client->deleteLater();
        _client = nullptr;
    }
#ifdef MIDIEDITOR_WEBRTC_ENABLED
    if (_rdv) {
        _rdv->cancelPolling();
        _rdv->deleteLater();
        _rdv = nullptr;
    }
#endif
    _file = nullptr;
    _lastActionLabel.clear();
    rewireProtocolListener();  // disconnects since _file == nullptr
    _pairingCode.clear();
    _hostDisplayName.clear();
    _lastSyncedSnapshot = QJsonArray();
    _pendingSidecar = QJsonObject();
    _filetransferPending = false;
    _lastBroadcastSidecarHead.clear();
    _lastBroadcastEndTick = -1;
    _transport = Transport::None;
    // Phase 9.9 §15.2: a fresh resetState reverts the editing-rights
    // model to Edit so the next session starts from a known-good
    // default regardless of what the previous one ended with.
    _mode = SessionMode::Edit;
    _presenterMachineId.clear();
    // Phase 9.11a §15.3: clear the per-sender chat rate-limit table —
    // a fresh session shouldn't inherit rate-limit timers from an
    // older one (would let the first message after rejoin sneak in
    // faster than allowed, or block a legitimate early message).
    _lastChatMsBySender.clear();
    // v1.7.2 §15.4: legacy-host flags reset per session so the
    // warning can fire again next time the user joins a different
    // (older) host.
    _gotSessionWelcome = false;
    _legacyHostWarningEmitted = false;
    // Phase 9.9f §15.2: stop any pending viewState broadcast — the
    // timer object itself is kept (cheap to reuse next session).
    if (_viewStateThrottle && _viewStateThrottle->isActive())
        _viewStateThrottle->stop();
    _viewStatePending = false;
    _pendingTrackVisibility.clear();
    _pendingChannelVisibility.clear();
    // BUG-COLLAB-024: invalidate any pending reconnect timer so it
    // can't restart the session the user just torn down.
    ++_retryGeneration;

    // Phase 9.8 baseline: reset traffic counters between sessions so
    // each session's log starts clean. The timer itself is owned by
    // setRole() — it shuts down when the role flips back to Idle.
    _bytesOutTotal   = 0;
    _bytesInTotal    = 0;
    _framesOutTotal  = 0;
    _framesInTotal   = 0;
    _bytesOutPeak1s  = 0;
    _bytesInPeak1s   = 0;
    _bytesOutWindow  = 0;
    _bytesInWindow   = 0;
    _framesOutWindow = 0;
    _framesInWindow  = 0;
    _trafficWindowStartMs = 0;
    if (!_pendingReviewHunks.isEmpty()) {
        _pendingReviewHunks = QJsonArray();
        _pendingReviewAuthor.clear();
        _pendingReviewMachineId.clear();
        emit pendingReviewChanged(0);
    }
}

QString LanLiveSession::startHosting(MidiFile *file, SessionMode mode) {
    if (!file) return QString();
    leaveSession();

    // Plan §11.10n auto-init: the user explicitly clicked "Start LAN
    // Live Session" — that's their consent to track + sync. If the
    // file isn't yet initialized for collab, do it transparently so
    // users don't hit the silent-no-sync footgun. Master toggle is
    // already enforced by the menu's `setVisible(enabled)` gate.
    if (!file->path().isEmpty()
        && !CollabService::instance()->isCurrentFileInitialized()) {
        CollabService::instance()->initializeCurrentFile(
            file, tr("Auto-init for live session"));
    }

    _file = file;
    rewireProtocolListener();
    _pairingCode = LanDiscovery::generatePairingCode();
    // Phase 9.9 §15.2: host = initial presenter. We set this BEFORE
    // any peer can connect so the very first `sessionWelcome` we ship
    // already carries the right pointer.
    _mode = mode;
    _presenterMachineId = CollabIdentity::machineId();
    qCInfo(lanLog) << "session: startHosting code=" << _pairingCode
                   << "tracks=" << file->numTracks()
                   << "ticksPerQuarter=" << file->ticksPerQuarter()
                   << "mode=" << LiveSession::modeToWire(_mode);

    // Spin up TCP server first so we know the port for the announcement.
    auto *lanSrv = new LanServer(this);
    quint16 port = lanSrv->startListening();
    if (port == 0) {
        lanSrv->deleteLater();
        emit statusMessage(tr("Could not open a TCP port for the LAN session."));
        resetState();
        return QString();
    }
    _server = lanSrv;
    connect(_server, &ILiveServer::peerConnected,
            this, &LanLiveSession::onServerPeerConnected);
    connect(_server, &ILiveServer::peerDisconnected,
            this, &LanLiveSession::onServerPeerDisconnected);
    connect(_server, &ILiveServer::messageReceived,
            this, &LanLiveSession::onServerMessage);

    // Multicast announce. Use the file's collab sessionId if available
    // (so receivers can detect cross-session pairings), else a fresh UUID
    // would also work — for now keep it empty when collab isn't init'd.
    QString sessionId = CollabService::instance()->sessionId();
    _discovery = new LanDiscovery(this);
    if (!_discovery->startAnnouncing(sessionId,
                                      CollabIdentity::displayName(),
                                      port,
                                      _pairingCode)) {
        emit statusMessage(tr("Could not bind multicast announce socket. "
                              "Other peers in your LAN won't auto-discover this session."));
        // Continue anyway — manual-IP fallback (Phase 9.5f) lets a peer
        // join even without multicast.
    }

    // Take baseline snapshot. Until somebody else edits, our diff
    // will see no changes.
    _lastSyncedSnapshot = MidiSnapshot::ofFile(_file);

    // Sync timer.
    _syncTimer = new QTimer(this);
    _syncTimer->setInterval(kSyncIntervalMs);
    connect(_syncTimer, &QTimer::timeout, this, &LanLiveSession::onSyncTick);
    _syncTimer->start();

    _transport = Transport::Lan;
    setRole(Role::Hosting);
    // bugfix 2026-05-21: surface the chosen mode to the GUI layer
    // explicitly. roleChanged alone isn't enough because applyShowModeLock
    // checks _mode, and on the host the mode was set above but the GUI
    // lambda doesn't know to re-check after roleChanged unless it's
    // told. sessionModeChanged is the unified entry point.
    emit sessionModeChanged();
    emit statusMessage(tr("Hosting LAN session — code %1, port %2.").arg(_pairingCode).arg(port));
    return _pairingCode;
}

void LanLiveSession::joinSession(MidiFile *file, const QString &pairingCode) {
    if (pairingCode.isEmpty()) {
        emit joinFailed(tr("A pairing code is required."));
        return;
    }
    leaveSession();
    _file = file;  // may be null — host will offer to ship its file
    rewireProtocolListener();
    _pairingCode = pairingCode.toUpper();
    // Plan §11.10n auto-init: same rationale as host path. Skip when
    // file is null (we'll receive the host's via filetransfer + adopt
    // their sidecar) or path is empty (untitled file — sidecar lives
    // on disk next to the .mid).
    if (file && !file->path().isEmpty()
        && !CollabService::instance()->isCurrentFileInitialized()) {
        CollabService::instance()->initializeCurrentFile(
            file, tr("Auto-init for live session"));
    }
    if (file) {
        qCInfo(lanLog) << "session: joinSession code=" << _pairingCode
                       << "localTracks=" << file->numTracks()
                       << "ticksPerQuarter=" << file->ticksPerQuarter();
    } else {
        qCInfo(lanLog) << "session: joinSession code=" << _pairingCode
                       << "(no local file — will request transfer from host)";
    }

    // Listen on multicast for the host's announcement.
    _discovery = new LanDiscovery(this);
    connect(_discovery, &LanDiscovery::peerFound,
            this, &LanLiveSession::onPeerFound);
    if (!_discovery->startListening(_pairingCode)) {
        emit joinFailed(tr("Could not bind to the multicast group. "
                           "Try the manual-IP fallback if your network blocks UDP multicast."));
        resetState();
        return;
    }
    _transport = Transport::Lan;
    emit statusMessage(tr("Searching the LAN for code %1…").arg(_pairingCode));
}

#ifdef MIDIEDITOR_WEBRTC_ENABLED

bool LanLiveSession::startHostingWan(MidiFile *file, SessionMode mode) {
    if (!file) return false;
    leaveSession();
    _retryAttempt = 0;  // fresh session — caller restores via retry timer if applicable

    // Plan §11.10n auto-init (mirror of LAN host path, see startHosting).
    if (!file->path().isEmpty()
        && !CollabService::instance()->isCurrentFileInitialized()) {
        CollabService::instance()->initializeCurrentFile(
            file, tr("Auto-init for live session"));
    }

    _file = file;
    rewireProtocolListener();
    // Phase 9.9 §15.2: host = initial presenter. Same rationale as LAN path.
    _mode = mode;
    _presenterMachineId = CollabIdentity::machineId();
    qCInfo(lanLog) << "session: startHostingWan tracks=" << file->numTracks()
                   << "ticksPerQuarter=" << file->ticksPerQuarter()
                   << "mode=" << LiveSession::modeToWire(_mode);

    // Create the WebRTC server first; it'll start ICE gathering and emit
    // offerReady once the SDP is finalized. Wire the same ILiveServer
    // signals as the LAN flow so onServerPeerConnected/Disconnected/
    // Message handle the peer transparently.
    auto *rtcSrv = new WebRtcLiveServer(this);
    _server = rtcSrv;
    connect(_server, &ILiveServer::peerConnected,
            this, &LanLiveSession::onServerPeerConnected);
    connect(_server, &ILiveServer::peerDisconnected,
            this, &LanLiveSession::onServerPeerDisconnected);
    connect(_server, &ILiveServer::messageReceived,
            this, &LanLiveSession::onServerMessage);

    _rdv = new RtcRendezvousClient(this);

    // Phase 9.8 multi-peer flow: post a session marker, then poll the
    // rendezvous for joiner offers. For each new joinerId observed we
    // ask the server to spin up a Responder transport, generate an
    // answer, and post that answer back to the rendezvous keyed by
    // the joinerId.
    QString sessionId = CollabService::instance()->sessionId();
    QString displayName = CollabIdentity::displayName();
    _rdv->postSession(sessionId, displayName);

    // Rendezvous accepted the session announcement → tell the UI and
    // start polling for joiner offers.
    connect(_rdv, &RtcRendezvousClient::sessionPosted,
            this, [this](const QString &code) {
                _pairingCode = code;
                emit wanCodeReady(code);
                emit statusMessage(tr("Sharing code %1 — waiting for peer(s)…").arg(code));
                if (_rdv) _rdv->pollJoinerOffers(code);
            });

    // A new joiner posted their offer → spin up a Responder transport
    // for that joinerId. The server emits answerReady when the answer
    // is finalised; we forward that to the rendezvous below.
    connect(_rdv, &RtcRendezvousClient::joinerOfferReceived,
            this, [this, rtcSrv](const QString &joinerId, const QString &offerSdp) {
                qCInfo(lanLog) << "session: joiner-offer for" << joinerId.left(8);
                emit statusMessage(tr("Peer joining — establishing connection…"));
                rtcSrv->acceptJoinerOffer(joinerId, offerSdp);
            });

    // Server's answer for a specific joiner is ready → ship it via
    // rendezvous so the joiner can complete the WebRTC handshake.
    connect(rtcSrv, &WebRtcLiveServer::answerReady,
            this, [this](const QString &joinerId, const QString &sdp) {
                if (_rdv && !_pairingCode.isEmpty()) {
                    _rdv->postHostAnswer(_pairingCode, joinerId, sdp);
                }
            });

    // Any rendezvous error before the channel is open is fatal for this
    // attempt. After the channel opens we never call the rendezvous
    // again, so late errors here can't fire.
    connect(_rdv, &RtcRendezvousClient::error,
            this, [this](const QString &stage, const QString &reason) {
                qCWarning(lanLog) << "session: WAN host rendezvous"
                                  << stage << "failed:" << reason;
                emit statusMessage(tr("WAN session failed (%1): %2").arg(stage, reason));
                leaveSession();
            });

    connect(rtcSrv, &WebRtcLiveServer::transportFailed,
            this, [this](const QString &joinerId, const QString &reason) {
                qCWarning(lanLog) << "session: WAN host transport failed for"
                                  << joinerId.left(8) << ":" << reason;
                // Phase 9.8: a single joiner's transport failure no
                // longer kills the session — other joiners stay
                // connected, the host keeps polling for new offers.
                // Auto-reconnect now applies only when ALL joiners
                // have gone (peerCount == 0) AND the user opted in.
                if (_server && _server->peerCount() > 0) return;
                // Plan §11.10h: optional auto-reconnect for WAN. Host
                // side gets a NEW rendezvous code on each retry — the
                // start dialog updates automatically because it's bound
                // to the wanCodeReady signal.
                MidiFile *retryFile = _file;
                if (loadAutoReconnect() && retryFile
                    && _retryAttempt < loadAutoReconnectMaxAttempts()) {
                    int next = ++_retryAttempt;
                    int max = loadAutoReconnectMaxAttempts();
                    emit statusMessage(tr("Reconnecting (attempt %1 of %2)…")
                                           .arg(next).arg(max));
                    // Phase 9.9 §15.2: leaveSession()/resetState() reverts
                    // _mode to Edit, so capture the current mode here
                    // before tearing down — otherwise a Show-mode session
                    // would silently downgrade to Edit on auto-reconnect.
                    SessionMode retryMode = _mode;
                    leaveSession();
                    // startHostingWan resets _retryAttempt = 0 (because it
                    // can't tell a user-fresh-start from a retry); we
                    // restore the counter immediately afterwards so the
                    // NEXT failure correctly counts against the budget.
                    // BUG-COLLAB-024: gen-check makes Leave-during-backoff
                    // cancel the pending retry. leaveSession() bumps
                    // _retryGeneration in resetState; if the user clicks
                    // Leave again, the captured `gen` no longer matches
                    // and the lambda exits before re-starting hosting.
                    int gen = _retryGeneration;
                    QTimer::singleShot(kRetryBackoffMs, this,
                        [this, retryFile, next, gen, retryMode]() {
                            if (gen != _retryGeneration) return;
                            startHostingWan(retryFile, retryMode);
                            _retryAttempt = next;
                        });
                    return;
                }
                emit statusMessage(tr("WAN connection failed: %1").arg(reason));
                leaveSession();
            });

    // Same baseline + sync-timer setup as LAN. Ticks broadcast to zero
    // peers until the channel opens, which is a no-op.
    _lastSyncedSnapshot = MidiSnapshot::ofFile(_file);
    _syncTimer = new QTimer(this);
    _syncTimer->setInterval(kSyncIntervalMs);
    connect(_syncTimer, &QTimer::timeout, this, &LanLiveSession::onSyncTick);
    _syncTimer->start();

    _transport = Transport::Wan;
    setRole(Role::Hosting);
    // bugfix 2026-05-21: surface the chosen mode — see startHosting.
    emit sessionModeChanged();
    emit statusMessage(tr("Starting WAN session — contacting rendezvous…"));
    rtcSrv->start(loadIceGatheringTimeoutMs());  // begin ICE gathering → eventual offerReady
    return true;
}

void LanLiveSession::joinSessionWan(MidiFile *file, const QString &code) {
    QString upper = code.trimmed().toUpper();
    if (upper.isEmpty()) {
        emit joinFailed(tr("A rendezvous code is required."));
        return;
    }
    leaveSession();
    _retryAttempt = 0;  // fresh session — caller restores via retry timer if applicable
    _file = file;  // may be null — host will offer to ship its file
    rewireProtocolListener();
    _pairingCode = upper;
    // Plan §11.10n auto-init: same rationale as the LAN paths.
    if (file && !file->path().isEmpty()
        && !CollabService::instance()->isCurrentFileInitialized()) {
        CollabService::instance()->initializeCurrentFile(
            file, tr("Auto-init for live session"));
    }
    qCInfo(lanLog) << "session: joinSessionWan code=" << upper
                   << "(localFile=" << (file != nullptr) << ")";

    auto *rtcCli = new WebRtcLiveClient(this);
    _client = rtcCli;
    connect(_client, &ILiveClient::connected,
            this, &LanLiveSession::onClientConnected);
    connect(_client, &ILiveClient::connectionFailed,
            this, &LanLiveSession::onClientConnectFailed);
    connect(_client, &ILiveClient::disconnected,
            this, &LanLiveSession::onClientDisconnected);
    connect(_client, &ILiveClient::messageReceived,
            this, &LanLiveSession::onClientMessage);

    _rdv = new RtcRendezvousClient(this);

    // Phase 9.8 multi-peer flow:
    //   1. Verify the code is valid → fast 404 if expired.
    //   2. Start the WebRTC client (Initiator) → emit offerReady once
    //      ICE gathering completes.
    //   3. POST our offer to the rendezvous with a fresh joinerId.
    //   4. Poll for the host's answer keyed by our joinerId.
    //   5. Feed the answer to the client's transport → DTLS completes.
    const QString joinerId = RtcRendezvousClient::newJoinerId();
    qCInfo(lanLog) << "session: joinSessionWan our joinerId=" << joinerId.left(8);

    connect(_rdv, &RtcRendezvousClient::codeVerified,
            this, [this, rtcCli](const QString &sid, const QString &dn) {
                Q_UNUSED(sid);
                if (!dn.isEmpty()) _hostDisplayName = dn;
                emit statusMessage(tr("Code verified — gathering candidates…"));
                rtcCli->start(loadIceGatheringTimeoutMs());
            });

    // Our offer is ready → ship to rendezvous with our joinerId, then
    // start polling for the host's answer slot.
    connect(rtcCli, &WebRtcLiveClient::offerReady,
            this, [this, upper, joinerId](const QString &sdp) {
                if (!_rdv) return;
                _rdv->postJoinerOffer(upper, joinerId, sdp);
                _rdv->pollHostAnswer(upper, joinerId);
            });

    // Host posted our answer → feed it to the transport.
    connect(_rdv, &RtcRendezvousClient::hostAnswerReceived,
            this, [this, rtcCli](const QString &sdp) {
                emit statusMessage(tr("Host answered — establishing connection…"));
                rtcCli->setRemoteAnswer(sdp);
            });

    connect(_rdv, &RtcRendezvousClient::error,
            this, [this](const QString &stage, const QString &reason) {
                qCWarning(lanLog) << "session: WAN join rendezvous"
                                  << stage << "failed:" << reason;
                emit joinFailed(tr("Rendezvous (%1): %2").arg(stage, reason));
                resetState();
                setRole(Role::Idle);
            });
    connect(rtcCli, &WebRtcLiveClient::transportFailed,
            this, [this](const QString &reason) {
                qCWarning(lanLog) << "session: WAN join transport failed:" << reason;
                // Plan §11.10h: peer-side retry. Reuses the same code
                // (we can't ask the user to retype it). If the host
                // also reconnected, its NEW code won't match — getOffer
                // returns 404 and the user is told to ask for a fresh
                // code. That asymmetry is documented in §11.10h.
                MidiFile *retryFile = _file;
                QString retryCode = _pairingCode;
                if (loadAutoReconnect() && !retryCode.isEmpty()
                    && _retryAttempt < loadAutoReconnectMaxAttempts()) {
                    int next = ++_retryAttempt;
                    int max = loadAutoReconnectMaxAttempts();
                    emit statusMessage(tr("Reconnecting (attempt %1 of %2)…")
                                           .arg(next).arg(max));
                    resetState();
                    setRole(Role::Idle);
                    // Same counter-restore trick as the host path —
                    // joinSessionWan zeroes _retryAttempt on entry,
                    // we put `next` back so the next failure counts.
                    // BUG-COLLAB-024: gen-check, see host path above.
                    int gen = _retryGeneration;
                    QTimer::singleShot(kRetryBackoffMs, this,
                        [this, retryFile, retryCode, next, gen]() {
                            if (gen != _retryGeneration) return;
                            joinSessionWan(retryFile, retryCode);
                            _retryAttempt = next;
                        });
                    return;
                }
                emit joinFailed(tr("Transport: %1").arg(reason));
                resetState();
                setRole(Role::Idle);
            });

    _transport = Transport::Wan;
    emit statusMessage(tr("Looking up code %1…").arg(upper));
    _rdv->verifyCode(upper);
}

#endif // MIDIEDITOR_WEBRTC_ENABLED

void LanLiveSession::leaveSession() {
    if (_role == Role::Idle && !_discovery && !_server && !_client) return;
    resetState();
    setRole(Role::Idle);
    emit peerCountChanged(0);
    emit statusMessage(tr("LAN session ended."));
}

int LanLiveSession::peerCount() const {
    if (_role == Role::Hosting && _server) return _server->peerCount();
    if (_role == Role::Joined && _client && _client->isConnected()) return 1;
    return 0;
}

QStringList LanLiveSession::peerLabels() const {
    QStringList names;
    if (_role == Role::Hosting && _server) {
        for (IPeerLink *p : _server->peers()) {
            QString label = p->peerLabel();
            if (label.isEmpty()) label = QStringLiteral("(anonymous)");
            names.append(label);
        }
    } else if (_role == Role::Joined && !_hostDisplayName.isEmpty()) {
        names.append(_hostDisplayName);
    }
    return names;
}

QList<QPair<QString, QString>> LanLiveSession::connectedPeerInfo() const {
    QList<QPair<QString, QString>> out;
    if (_role != Role::Hosting || !_server) return out;
    for (IPeerLink *p : _server->peers()) {
        QString mid = p->peerMachineId();
        if (mid.isEmpty()) continue;  // hello hasn't completed yet — skip
        QString label = p->peerLabel();
        if (label.isEmpty()) label = QStringLiteral("(anonymous)");
        out.append(qMakePair(mid, label));
    }
    return out;
}

// ---------------------------------------------------------------------
// Hat-passing public API (Phase 9.9b §15.2)
// ---------------------------------------------------------------------

void LanLiveSession::requestHat() {
    // Only meaningful in Show mode; Edit mode has no presenter pointer.
    if (_mode != SessionMode::Edit && _role != Role::Idle) {
        // Already hold the hat → no-op (caller's UI shouldn't expose
        // the request button in that case, but be defensive).
        if (isPresenter()) {
            qCDebug(lanLog) << "session: requestHat ignored — local peer is presenter";
            return;
        }
    } else {
        // Edit mode or no session → no-op.
        qCDebug(lanLog) << "session: requestHat ignored — not in Show mode";
        return;
    }

    QByteArray frame = encodeRequestHat();
    if (_role == Role::Hosting && _server) {
        // Host is requesting the hat from a remote presenter. Route
        // locally: the host's own onServerMessage path treats the
        // frame the same way as if it came from a peer.
        handleIncomingRequestHat(nullptr,
                                  CollabIdentity::machineId(),
                                  CollabIdentity::displayName());
    } else if (_role == Role::Joined && _client && _client->isConnected()) {
        _client->sendMessage(frame);
        recordSent(frame.size(), 1);
    }
}

void LanLiveSession::transferHatTo(const QString &machineId,
                                   const QString &targetDisplayName) {
    if (machineId.isEmpty()) return;
    if (_mode != SessionMode::Show) return;
    if (!isPresenter()) {
        // Defensive: only the current presenter can transfer. Viewers
        // who somehow trigger this (stale UI state) are silently dropped.
        qCWarning(lanLog) << "session: transferHatTo called while not presenter — ignored";
        return;
    }
    // No-op when transferring to ourselves.
    if (machineId == CollabIdentity::machineId()) {
        qCDebug(lanLog) << "session: transferHatTo self — ignored";
        return;
    }

    if (_role == Role::Hosting) {
        // Host is the presenter → run the authoritative path locally.
        handleIncomingHatTransferred(
            nullptr,
            CollabIdentity::machineId(),
            machineId,
            targetDisplayName,
            QStringLiteral("transfer"));
    } else if (_role == Role::Joined && _client && _client->isConnected()) {
        QByteArray frame = encodeHatTransferred(
            machineId, targetDisplayName, QStringLiteral("transfer"));
        _client->sendMessage(frame);
        recordSent(frame.size(), 1);
    }
}

bool LanLiveSession::hostCanTakeHat() const {
    if (_role != Role::Hosting) return false;
    if (_mode != SessionMode::Show) return false;
    // Host is already presenter → no take-over needed.
    if (isPresenter()) return false;
    // The current presenter must be absent from the connected peer
    // list. The heartbeat eviction logic already removes peers that
    // have been silent past the 30 s deadline (BUG-COLLAB-030), so
    // "not in peer list" is the right signal for "presenter is gone".
    if (!_server) return true;
    for (IPeerLink *p : _server->peers()) {
        if (p->peerMachineId() == _presenterMachineId) return false;
    }
    return true;
}

void LanLiveSession::hostTakeHat() {
    if (!hostCanTakeHat()) {
        qCDebug(lanLog) << "session: hostTakeHat — not permitted";
        return;
    }
    qCInfo(lanLog) << "session: host taking the hat (presenter"
                   << _presenterMachineId.left(8) << "is silent)";
    // Host bypasses the "sender must be current presenter" rule via
    // the host-takeover reason. handleIncomingHatTransferred is aware
    // of this and skips the equality check when reason matches.
    handleIncomingHatTransferred(
        nullptr,
        CollabIdentity::machineId(),
        CollabIdentity::machineId(),
        CollabIdentity::displayName(),
        QStringLiteral("host-takeover"));
}

void LanLiveSession::yieldHat() {
    if (_mode != SessionMode::Show) return;
    if (!isPresenter()) return;
    if (_role == Role::Hosting) {
        // Presenter is the host already — yield to self is a no-op.
        qCDebug(lanLog) << "session: yieldHat — host is presenter, no-op";
        return;
    }
    // Send the yield to the host; host's handleIncomingYieldHat
    // validates + does the actual hatTransferred broadcast.
    QByteArray frame = encodeYieldHat();
    if (_client && _client->isConnected()) {
        _client->sendMessage(frame);
        recordSent(frame.size(), 1);
    }
}

// ---------------------------------------------------------------------
// Hat-passing handlers (Phase 9.9b §15.2)
// ---------------------------------------------------------------------

void LanLiveSession::handleIncomingRequestHat(IPeerLink *fromPeer,
                                              const QString &requesterMachineId,
                                              const QString &requesterDisplayName) {
    // Race-resolution per §15.2: if the requester is already the
    // current presenter (because a `hatTransferred` we just shipped
    // landed between them sending and us receiving), drop silently.
    if (!_presenterMachineId.isEmpty()
        && requesterMachineId == _presenterMachineId) {
        qCInfo(lanLog) << "server: requestHat from"
                       << requesterDisplayName
                       << "dropped — already the presenter (race)";
        return;
    }
    // Host-routed forwarding: where is the presenter?
    if (_presenterMachineId == CollabIdentity::machineId()) {
        // Host IS the presenter → emit the local notification signal.
        emit hatRequested(requesterDisplayName, requesterMachineId);
        return;
    }
    // Presenter is a remote peer → forward the requestHat frame to it.
    if (_server) {
        for (IPeerLink *p : _server->peers()) {
            if (p->peerMachineId() == _presenterMachineId) {
                QJsonObject o = LiveSession::encodeRequestHatJson(
                    requesterMachineId, requesterDisplayName);
                QByteArray frame =
                    QJsonDocument(o).toJson(QJsonDocument::Compact);
                p->sendMessage(frame);
                recordSent(frame.size(), 1);
                return;
            }
        }
        // Presenter not found in peer list — silently drop. The host's
        // own host-takeover privilege (hostCanTakeHat / hostTakeHat)
        // is the recovery path; nothing to reject back to the requester.
        qCWarning(lanLog) << "server: requestHat from"
                          << requesterDisplayName
                          << "— presenter peer not found, dropping";
        Q_UNUSED(fromPeer);
    }
}

void LanLiveSession::handleIncomingHatTransferred(
        IPeerLink *fromPeer,
        const QString &senderMachineId,
        const QString &newPresenterMachineId,
        const QString &newPresenterDisplayName,
        const QString &reason) {

    if (_role != Role::Hosting) {
        // Defensive: only the host runs the authoritative path.
        // A non-host receiving this would be a protocol error.
        qCWarning(lanLog) << "session: handleIncomingHatTransferred called as non-host — bug";
        return;
    }

    const bool isHostTakeover = (reason == QLatin1String("host-takeover"));

    // Host-takeover is a HOST-ONLY privilege — fromPeer == nullptr is
    // the marker that the host's own code path is calling us (via
    // hostTakeHat -> handleIncomingHatTransferred(nullptr, ...)). A
    // remote peer that ships {reason:"host-takeover"} to escalate
    // privileges gets rejected with the same treatment as any other
    // unauthorised transfer attempt. Without this guard, the special-
    // case skip below would let any peer seize the hat by lying about
    // the reason field.
    if (isHostTakeover && fromPeer != nullptr) {
        qCWarning(lanLog) << "server: hatTransferred with host-takeover "
                             "reason from remote peer rejected "
                             "(privilege escalation attempt)";
        QByteArray frame = encodeHatRejected(
            tr("Host-takeover is a host-only privilege."));
        fromPeer->sendMessage(frame);
        recordSent(frame.size(), 1);
        return;
    }

    // Authorisation: either the sender IS the current presenter, or
    // this is the host's special-privilege host-takeover (which is
    // only allowed when the current presenter is silent past the
    // heartbeat deadline — gated by hostCanTakeHat at the caller).
    if (!isHostTakeover) {
        if (_presenterMachineId.isEmpty()
            || senderMachineId != _presenterMachineId) {
            qCWarning(lanLog) << "server: hatTransferred from"
                              << senderMachineId.left(8)
                              << "rejected — sender is not the current presenter";
            if (fromPeer) {
                QByteArray frame = encodeHatRejected(
                    tr("Only the current presenter can transfer the hat."));
                fromPeer->sendMessage(frame);
                recordSent(frame.size(), 1);
            }
            return;
        }
    }

    // Validate the target is still connected (or is the host itself).
    bool targetReachable = (newPresenterMachineId == CollabIdentity::machineId());
    if (!targetReachable && _server) {
        for (IPeerLink *p : _server->peers()) {
            if (p->peerMachineId() == newPresenterMachineId) {
                targetReachable = true;
                break;
            }
        }
    }
    if (!targetReachable) {
        QString reasonText = tr("%1 has disconnected — the hat stays with you.")
                                 .arg(newPresenterDisplayName.isEmpty()
                                      ? tr("the target peer")
                                      : newPresenterDisplayName);
        qCInfo(lanLog) << "server: hatTransferred target not connected ("
                       << newPresenterMachineId.left(8) << "), rejecting";
        if (fromPeer) {
            QByteArray frame = encodeHatRejected(reasonText);
            fromPeer->sendMessage(frame);
            recordSent(frame.size(), 1);
        } else {
            // Host's own transfer attempt failed — surface locally.
            emit hatTransferRejected(reasonText);
        }
        return;
    }

    // Authorised + target reachable → commit and broadcast.
    qCInfo(lanLog) << "server: hat transferred to"
                   << newPresenterMachineId.left(8)
                   << "(" << newPresenterDisplayName << "), reason=" << reason;
    _presenterMachineId = newPresenterMachineId;
    QByteArray frame = encodeHatTransferred(
        newPresenterMachineId, newPresenterDisplayName, reason);
    if (_server) {
        int peers = _server->peerCount();
        if (peers > 0 && _server->broadcast(frame))
            recordSent(frame.size(), peers);
    }
    // Emit the local signal too — the host is one of the peers whose
    // UI needs to refresh on a hat change. sessionModeChanged is the
    // canonical "re-evaluate the show-mode lock" trigger.
    emit hatTransferred(newPresenterMachineId, newPresenterDisplayName, reason);
    emit sessionModeChanged();
}

void LanLiveSession::handleClientHatTransferred(
        const QString &newPresenterMachineId,
        const QString &newPresenterDisplayName,
        const QString &reason) {
    if (_mode != SessionMode::Show) {
        qCWarning(lanLog) << "client: hatTransferred received in Edit mode — ignoring";
        return;
    }
    qCInfo(lanLog) << "client: hat transferred to"
                   << newPresenterMachineId.left(8)
                   << "(" << newPresenterDisplayName << "), reason=" << reason;
    _presenterMachineId = newPresenterMachineId;
    emit hatTransferred(newPresenterMachineId, newPresenterDisplayName, reason);
    emit sessionModeChanged();
}

void LanLiveSession::handleIncomingHatRejected(const QString &reason) {
    qCInfo(lanLog) << "session: hat transfer rejected:" << reason;
    emit hatTransferRejected(reason);
}

void LanLiveSession::handleIncomingYieldHat(IPeerLink *fromPeer) {
    if (_role != Role::Hosting) return;
    if (_mode != SessionMode::Show) return;
    QString senderMid = fromPeer ? fromPeer->peerMachineId() : QString();
    if (senderMid.isEmpty() || senderMid != _presenterMachineId) {
        qCWarning(lanLog) << "server: yieldHat from non-presenter"
                          << senderMid.left(8) << "— dropping";
        return;
    }
    qCInfo(lanLog) << "server: presenter" << senderMid.left(8)
                   << "yielded the hat; returning to host";
    _presenterMachineId = CollabIdentity::machineId();
    QByteArray frame = encodeHatTransferred(
        CollabIdentity::machineId(),
        CollabIdentity::displayName(),
        QStringLiteral("yield"));
    if (_server) {
        int peers = _server->peerCount();
        if (peers > 0 && _server->broadcast(frame))
            recordSent(frame.size(), peers);
    }
    // Also fire locally so the host's own UI refreshes (status bar,
    // PRESENTING indicator). sessionModeChanged drives the lock /
    // refreshLiveLabel pipeline.
    emit hatTransferred(CollabIdentity::machineId(),
                        CollabIdentity::displayName(),
                        QStringLiteral("yield"));
    emit sessionModeChanged();
}

bool LanLiveSession::isHunkSenderAuthorised(const QString &senderMachineId,
                                            const QString &senderDisplayName,
                                            int hunkCount) {
    if (_mode == SessionMode::Edit) return true;
    if (_role == Role::Idle) return true;
    if (senderMachineId == _presenterMachineId) return true;
    // Show mode + unauthorised sender → diagnostics signal + drop.
    qCWarning(lanLog) << "session: dropping" << hunkCount
                      << "Show-mode hunk(s) from unauthorised sender"
                      << senderMachineId.left(8)
                      << "(" << senderDisplayName << ")";
    emit unauthorisedHunkDropped(senderMachineId, senderDisplayName, hunkCount);
    return false;
}

// ---------------------------------------------------------------------
// Chat side-channel (Phase 9.11 §15.3)
// ---------------------------------------------------------------------

void LanLiveSession::sendChatMessage(const QString &text) {
    if (text.isEmpty()) return;
    if (_role == Role::Idle) return;
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QByteArray frame = encodeChat(text, nowMs);
    if (_role == Role::Hosting) {
        // Host runs the inbound path locally so its own messages are
        // subject to the same rate-limit + size cap as a peer's, and
        // get re-broadcast to all connected peers from a single place.
        QJsonDocument doc = QJsonDocument::fromJson(frame);
        if (doc.isObject()) handleIncomingChat(nullptr, doc.object(), frame);
    } else if (_role == Role::Joined && _client && _client->isConnected()) {
        _client->sendMessage(frame);
        recordSent(frame.size(), 1);
    }
}

void LanLiveSession::handleIncomingChat(IPeerLink *fromPeer,
                                        const QJsonObject &obj,
                                        const QByteArray &payload) {
    if (_role != Role::Hosting) {
        qCWarning(lanLog) << "session: handleIncomingChat called as non-host — bug";
        return;
    }
    QString senderMid, displayName, text;
    qint64 timestampMs = 0;
    LiveSession::decodeChatJson(obj, &senderMid, &displayName, &text, &timestampMs);

    // Size cap. UTF-8 byte count, not character count — a peer sending
    // 4 KiB of multi-byte characters still gets dropped.
    if (text.toUtf8().size() > LiveSession::kChatTextMaxBytes) {
        qCWarning(lanLog) << "chat: dropping oversize message from"
                          << senderMid.left(8)
                          << "(" << text.toUtf8().size() << "bytes)";
        emit chatMessageDropped(senderMid, tr("message too large"));
        if (fromPeer) {
            // No structured error frame for chat (§15.3 explicitly
            // out-of-scope); the sender's UI just won't see their
            // message appear on other peers.
        }
        return;
    }

    // Rate limit per sender.
    qint64 lastMs = _lastChatMsBySender.value(senderMid, 0);
    if (lastMs > 0
        && (timestampMs - lastMs) < LiveSession::kChatRateLimitMsPerSender) {
        qCDebug(lanLog) << "chat: rate-limited message from"
                        << senderMid.left(8);
        emit chatMessageDropped(senderMid, tr("rate limit"));
        return;
    }
    _lastChatMsBySender.insert(senderMid, timestampMs);

    // Surface to local UI. CAVEAT (bugfix 2026-05-21): the host's own
    // sendChatMessage path also routes through handleIncomingChat —
    // so the local CollabChatWidget already appended optimistically
    // before this point. Emitting chatMessageReceived for the host's
    // own machineId would double-append. Skip the signal in that case;
    // the broadcastExcept below still re-broadcasts to other peers.
    if (senderMid != CollabIdentity::machineId()) {
        emit chatMessageReceived(senderMid, displayName, text, timestampMs);
    }

    // Re-broadcast to every other peer. Use broadcastExcept so the
    // original sender doesn't see their own message echoed (their UI
    // appended it optimistically before the wire send).
    if (_server) {
        int others = _server->peerCount() - (fromPeer ? 1 : 0);
        if (others > 0) {
            bool ok = _server->broadcastExcept(payload, fromPeer);
            if (ok) recordSent(payload.size(), others);
        }
    }
}

void LanLiveSession::handleClientChat(const QJsonObject &obj) {
    QString senderMid, displayName, text;
    qint64 timestampMs = 0;
    LiveSession::decodeChatJson(obj, &senderMid, &displayName, &text, &timestampMs);
    emit chatMessageReceived(senderMid, displayName, text, timestampMs);
}

// ---------------------------------------------------------------------
// Follow-the-host viewState (Phase 9.9f §15.2)
// ---------------------------------------------------------------------

void LanLiveSession::broadcastViewState(
        const LiveSession::ViewportState &viewport,
        const QVector<bool> &trackVisibility,
        const QVector<bool> &channelVisibility) {
    // Guards: only the presenter in an active Show-mode session may
    // broadcast view state. Anything else is silently dropped.
    if (_role == Role::Idle) return;
    if (_mode != SessionMode::Show) return;
    if (!isPresenter()) return;

    // Stash the latest state; flushViewStateBroadcast will pick it up
    // when the throttle timer fires.
    _pendingViewport          = viewport;
    _pendingTrackVisibility   = trackVisibility;
    _pendingChannelVisibility = channelVisibility;

    if (_viewStatePending) {
        // Timer is already armed — just updated the queued state.
        return;
    }
    _viewStatePending = true;
    if (!_viewStateThrottle) {
        _viewStateThrottle = new QTimer(this);
        _viewStateThrottle->setSingleShot(true);
        connect(_viewStateThrottle, &QTimer::timeout,
                this, &LanLiveSession::flushViewStateBroadcast);
    }
    _viewStateThrottle->start(LiveSession::kViewStateThrottleMs);
}

void LanLiveSession::flushViewStateBroadcast() {
    _viewStatePending = false;
    if (_role != Role::Hosting) {
        // Presenter on a joiner: ship through _client. (Edge case for
        // when a non-host peer holds the hat in a multi-peer session;
        // the host then re-broadcasts via handleIncomingViewState.)
        if (_role == Role::Joined && _client && _client->isConnected()) {
            QByteArray frame = encodeViewState(
                _pendingViewport, _pendingTrackVisibility, _pendingChannelVisibility);
            _client->sendMessage(frame);
            recordSent(frame.size(), 1);
        }
        return;
    }
    // Host path: re-broadcast straight to every peer (no exclude —
    // every viewer needs the state). Host's own local view doesn't
    // need a self-loop; the presenter IS the source of truth.
    if (!_server) return;
    QByteArray frame = encodeViewState(
        _pendingViewport, _pendingTrackVisibility, _pendingChannelVisibility);
    int peers = _server->peerCount();
    if (peers > 0 && _server->broadcast(frame))
        recordSent(frame.size(), peers);
}

void LanLiveSession::handleIncomingViewState(IPeerLink *fromPeer,
                                              const QJsonObject &obj,
                                              const QByteArray &payload) {
    if (_role != Role::Hosting) return;
    // Same authorisation gate as hunks — only the presenter is allowed
    // to broadcast view state in Show mode. Edit mode shouldn't see
    // these frames at all; defensive drop.
    if (_mode != SessionMode::Show) {
        qCWarning(lanLog) << "server: viewState received in Edit mode — dropping";
        return;
    }
    QString senderMid = obj.value(QStringLiteral("sender")).toString();
    if (senderMid != _presenterMachineId) {
        qCWarning(lanLog) << "server: viewState from non-presenter"
                          << senderMid.left(8) << "— dropping";
        return;
    }

    // Decode + emit locally too — the host is also a "viewer" of the
    // presenter's view when the presenter is a remote peer.
    LiveSession::ViewportState vp;
    QVector<bool> tracks, channels;
    QString s;
    LiveSession::decodeViewStateJson(obj, &s, &vp, &tracks, &channels);
    if (senderMid != CollabIdentity::machineId()) {
        emit viewStateReceived(vp, tracks, channels);
    }

    // Re-broadcast to every other peer.
    if (_server) {
        int others = _server->peerCount() - (fromPeer ? 1 : 0);
        if (others > 0 && _server->broadcastExcept(payload, fromPeer))
            recordSent(payload.size(), others);
    }
}

void LanLiveSession::handleClientViewState(const QJsonObject &obj) {
    QString senderMid;
    LiveSession::ViewportState vp;
    QVector<bool> tracks, channels;
    LiveSession::decodeViewStateJson(obj, &senderMid, &vp, &tracks, &channels);
    // Don't apply our own view state echoed back (shouldn't happen on
    // joiner side since host uses broadcastExcept, but defensive).
    if (senderMid == CollabIdentity::machineId()) return;
    emit viewStateReceived(vp, tracks, channels);
}

// ---------------------------------------------------------------------
// Playback trigger (Show-mode follow-the-host)
// ---------------------------------------------------------------------

void LanLiveSession::broadcastPlayback(const QString &action, int tickPosition) {
    if (_role == Role::Idle) return;
    if (_mode != SessionMode::Show) return;
    if (!isPresenter()) return;

    QByteArray frame = encodePlayback(action, tickPosition);
    if (_role == Role::Hosting) {
        // Run through the inbound path locally so the same validator
        // gate applies + the re-broadcast happens from one place.
        QJsonDocument doc = QJsonDocument::fromJson(frame);
        if (doc.isObject()) handleIncomingPlayback(nullptr, doc.object(), frame);
    } else if (_role == Role::Joined && _client && _client->isConnected()) {
        _client->sendMessage(frame);
        recordSent(frame.size(), 1);
    }
}

void LanLiveSession::handleIncomingPlayback(IPeerLink *fromPeer,
                                            const QJsonObject &obj,
                                            const QByteArray &payload) {
    if (_role != Role::Hosting) return;
    if (_mode != SessionMode::Show) {
        qCWarning(lanLog) << "server: playback received in Edit mode — dropping";
        return;
    }
    QString senderMid = obj.value(QStringLiteral("sender")).toString();
    if (senderMid != _presenterMachineId) {
        qCWarning(lanLog) << "server: playback from non-presenter"
                          << senderMid.left(8) << "— dropping";
        return;
    }

    // Surface locally too — the host is also a viewer of whatever the
    // presenter (potentially a remote peer) just triggered. Skip when
    // the host IS the presenter — they triggered it via the local
    // play/stop click, no need to reflect.
    QString action;
    int tickPosition = -1;
    LiveSession::decodePlaybackJson(obj, &action, &tickPosition);
    if (senderMid != CollabIdentity::machineId()) {
        emit playbackTriggerReceived(action, tickPosition);
    }

    // Re-broadcast to other peers.
    if (_server) {
        int others = _server->peerCount() - (fromPeer ? 1 : 0);
        if (others > 0 && _server->broadcastExcept(payload, fromPeer))
            recordSent(payload.size(), others);
    }
}

void LanLiveSession::handleClientPlayback(const QJsonObject &obj) {
    QString senderMid = obj.value(QStringLiteral("sender")).toString();
    if (senderMid == CollabIdentity::machineId()) return;  // own echo
    QString action;
    int tickPosition = -1;
    LiveSession::decodePlaybackJson(obj, &action, &tickPosition);
    emit playbackTriggerReceived(action, tickPosition);
}

// ---------------------------------------------------------------------
// Mid-session mode switch (host-only)
// ---------------------------------------------------------------------

void LanLiveSession::switchSessionMode(SessionMode newMode) {
    if (_role != Role::Hosting) {
        qCWarning(lanLog) << "session: switchSessionMode called as non-host — ignored";
        return;
    }
    if (newMode == _mode) {
        qCDebug(lanLog) << "session: switchSessionMode no-op (already in mode)";
        return;
    }
    qCInfo(lanLog) << "session: switching mode"
                   << LiveSession::modeToWire(_mode) << "->"
                   << LiveSession::modeToWire(newMode);

    _mode = newMode;
    // On Edit->Show the host becomes the initial presenter (same as
    // fresh startHosting with mode=Show). On Show->Edit the presenter
    // pointer is irrelevant; clear it so isPresenter() stops
    // matching against stale state.
    if (newMode == SessionMode::Show) {
        _presenterMachineId = CollabIdentity::machineId();
    } else {
        _presenterMachineId.clear();
    }

    // Broadcast the switch to every peer.
    QByteArray frame = encodeSessionModeSwitch(newMode, _presenterMachineId);
    if (_server) {
        int peers = _server->peerCount();
        if (peers > 0 && _server->broadcast(frame))
            recordSent(frame.size(), peers);
    }

    // Surface locally — drives MainWindow's lock + status-bar refresh.
    emit sessionModeChanged();
    emit statusMessage(newMode == SessionMode::Show
        ? tr("Session is now in Show mode. You are the presenter.")
        : tr("Session is now in Edit mode. All peers can edit again."));
}

void LanLiveSession::handleClientSessionModeSwitch(const QJsonObject &obj) {
    SessionMode newMode = SessionMode::Edit;
    QString newPresenter;
    LiveSession::decodeSessionModeSwitchJson(obj, &newMode, &newPresenter);
    if (newMode == _mode && newPresenter == _presenterMachineId) return;
    qCInfo(lanLog) << "client: host switched session mode to"
                   << LiveSession::modeToWire(newMode)
                   << "presenter=" << newPresenter.left(8);
    _mode = newMode;
    _presenterMachineId = newPresenter;
    emit sessionModeChanged();
    emit statusMessage(newMode == SessionMode::Show
        ? tr("Host switched the session to Show mode.")
        : tr("Host switched the session back to Edit mode."));
}

// ---------------------------------------------------------------------
// Sync tick — same logic for host and client; differences are in WHERE
// the hunks are sent (broadcast vs to-host).
// ---------------------------------------------------------------------
void LanLiveSession::onSyncTick() {
    if (!_file) return;
    // Plan §11.10p: don't broadcast our local snapshot while a
    // filetransfer-on-join is sitting in the modal dialog. Otherwise
    // the joiner's PRE-transfer file (which the host doesn't care
    // about — it's about to be replaced) bleeds onto the wire and the
    // host briefly shrinks its endTick to the joiner's old value
    // before re-extending. Visible in logs as
    //   "mirroring remote end tick: 208350 → 7680" (bogus shrink)
    //   "mirroring remote end tick: 7680 → 208350" (extend back)
    if (_filetransferPending) return;
    QJsonArray now = MidiSnapshot::ofFile(_file);
    QJsonArray hunks = MidiDiff::compute(_lastSyncedSnapshot, now, _file->ticksPerQuarter());

    // Plan §11.10j: force a frame when end-tick changed even if the
    // event diff is empty — Insert measures into an empty zone shifts
    // no events, so without this guard the file-length change never
    // reaches peers.
    int currentEndTick = _file->endTick();
    bool endTickChanged = (_lastBroadcastEndTick != currentEndTick);
    if (hunks.isEmpty() && !endTickChanged) {
        // Nothing to send — but we still need to advance the snapshot
        // baseline so a future tick that does have changes diffs against
        // current state, not a stale baseline.
        _lastSyncedSnapshot = now;
        return;
    }

    // Phase 9.9c §15.2: Show-mode viewer guard. A viewer's local edits
    // (which shouldn't happen once the UI lock lands, but might during
    // the window between this commit and the lock commit, or via MCP /
    // MidiPilot bypasses if a peer's build is out of date) must NEVER
    // hit the wire. We still advance the baseline so a future
    // hat-take starts from the current local state — without this the
    // diff would include all the silently-dropped local edits in the
    // viewer's first authorised broadcast, dumping minutes of stale
    // changes onto the peers' files all at once.
    if (_mode == SessionMode::Show && !isPresenter()) {
        if (!hunks.isEmpty()) {
            qCDebug(lanLog) << "syncTick: viewer suppressing"
                            << hunks.size() << "local hunks (Show mode)";
        }
        _lastSyncedSnapshot = now;
        _lastBroadcastEndTick = currentEndTick;
        _lastActionLabel.clear();
        return;
    }

    qCInfo(lanLog) << "session: syncTick broadcast — hunks=" << hunks.size()
                   << "fileMaxTick=" << currentEndTick
                   << "role=" << (_role == Role::Hosting ? "host" : "client")
                   << "endTickChanged=" << endTickChanged;
    QByteArray payload = encodeHunks(hunks);  // includes fileMaxTick + actionLabel

    // BUG-COLLAB-021: don't advance baselines until the send actually
    // succeeded.  `sendMessage` returns false when the payload exceeds
    // libdatachannel's negotiated max-message-size (16 MiB after Plan
    // §11.10j), or when the underlying socket is mid-teardown.  If we
    // had advanced `_lastSyncedSnapshot` first, the next tick would
    // diff against post-edit state and the unsent hunks would vanish
    // silently — divergence with no warning.
    bool sendOk = false;
    if (_role == Role::Hosting && _server) {
        sendOk = _server->broadcast(payload);
        if (sendOk) recordSent(payload.size(), _server->peerCount());
    } else if (_role == Role::Joined && _client && _client->isConnected()) {
        sendOk = _client->sendMessage(payload);
        if (sendOk) recordSent(payload.size(), 1);
    } else {
        // Neither path applies (e.g. transient role mismatch or
        // disconnected client).  Treat as "nothing sent yet" so the
        // baseline doesn't move until we're actually wired up.
        sendOk = false;
    }

    if (sendOk) {
        _lastSyncedSnapshot = now;
        _lastBroadcastEndTick = currentEndTick;
        // Plan §11.10p: action label is consumed by the broadcast — clear
        // so a follow-up syncTick without new actions doesn't re-send the
        // stale tool name.
        _lastActionLabel.clear();
    } else {
        qCWarning(lanLog) << "session: syncTick send FAILED — payload"
                          << payload.size() << "bytes ("
                          << hunks.size() << "hunks). Baseline NOT advanced; "
                             "next tick will retry the same diff.";
        // One status message per failure.  Sustained failure (e.g. a
        // huge edit that exceeds the SCTP frame budget) will keep
        // emitting one per tick — that's intentional, the user needs
        // to know live sync isn't keeping up.
        emit statusMessage(tr("Live sync paused — last edit was too large "
                              "to send. Save and split the change, or "
                              "leave the session and rejoin."));
    }
}

// ---------------------------------------------------------------------
// Discovery → join
// ---------------------------------------------------------------------
void LanLiveSession::onPeerFound(const QString &sessionId,
                                  const QString &displayName,
                                  const QHostAddress &hostAddress,
                                  quint16 tcpPort) {
    if (_role != Role::Idle) {
        qCDebug(lanLog) << "session: ignoring peerFound — already in role" << static_cast<int>(_role);
        return;
    }
    qCInfo(lanLog) << "session: peer found" << displayName << "@"
                   << hostAddress.toString() << ":" << tcpPort
                   << "sessionId=" << sessionId.left(8);

    // Phase 9.5i transport-agnostic sessionId lookup. If we have this
    // file locally (from a prior session), switch to it BEFORE
    // connecting so our `hello` carries the right sessionId/head/tail
    // and §11.10b reconciliation routes correctly. The signal is
    // wired to MainWindow::openFile via Qt::AutoConnection on the
    // same thread, so it runs synchronously.
    QString currentActiveSession = CollabService::instance()->sessionId();
    bool alreadyOnRightFile = !sessionId.isEmpty()
                               && sessionId == currentActiveSession;
    if (!sessionId.isEmpty() && !alreadyOnRightFile) {
        QString matchPath = CollabService::instance()->findFileBySessionId(sessionId);
        if (!matchPath.isEmpty()) {
            qCInfo(lanLog) << "session: matched local file for sessionId"
                           << sessionId.left(8) << "—" << matchPath;
            emit switchToFileBeforeConnect(matchPath);
            // After the synchronous slot returns, CollabService has
            // loaded the file and emitted activeFileChanged; the
            // file-change handler in our ctor early-returns when
            // role is Idle. We still need to bind the new MidiFile
            // pointer to _file — MainWindow's slot calls
            // setActiveFile(file) for us, so by the time we get
            // here _file is already set.
        }
    }

    _hostDisplayName = displayName;
    _discovery->stopListening();  // we found our peer; stop scanning

    auto *lanCli = new LanClient(this);
    _client = lanCli;
    connect(_client, &ILiveClient::connected,
            this, &LanLiveSession::onClientConnected);
    connect(_client, &ILiveClient::connectionFailed,
            this, &LanLiveSession::onClientConnectFailed);
    connect(_client, &ILiveClient::disconnected,
            this, &LanLiveSession::onClientDisconnected);
    connect(_client, &ILiveClient::messageReceived,
            this, &LanLiveSession::onClientMessage);

    lanCli->connectToHost(hostAddress, tcpPort);
    emit statusMessage(tr("Connecting to %1 (%2:%3)…")
                            .arg(displayName, hostAddress.toString())
                            .arg(tcpPort));
}

void LanLiveSession::onClientConnected() {
    _retryAttempt = 0;  // Plan §11.10h: success restores the retry budget
    qCInfo(lanLog) << "session: client connected to host" << _hostDisplayName
                   << "— sending hello (localFile=" << (_file != nullptr) << ")";
    // Send hello so the host can show our name in its peer list, and
    // signal whether we already have a file open (host uses this to
    // decide whether to ship its .mid in a `filetransfer` frame).
    _client->sendMessage(encodeHello());
    setRole(Role::Joined);

    if (_file) {
        _lastSyncedSnapshot = MidiSnapshot::ofFile(_file);
        qCInfo(lanLog) << "session: baseline snapshot taken,"
                       << _lastSyncedSnapshot.size() << "events";
    } else {
        // Wait for the host's filetransfer + setActiveFile() before any
        // sync ticks have data to diff.
        _lastSyncedSnapshot = QJsonArray();
    }

    _syncTimer = new QTimer(this);
    _syncTimer->setInterval(kSyncIntervalMs);
    connect(_syncTimer, &QTimer::timeout, this, &LanLiveSession::onSyncTick);
    _syncTimer->start();

    emit joined(_hostDisplayName);
    emit peerCountChanged(1);
    emit statusMessage(tr("Joined LAN session hosted by %1.").arg(_hostDisplayName));
}

void LanLiveSession::onClientConnectFailed(const QString &reason) {
    emit joinFailed(reason);
    resetState();
    setRole(Role::Idle);
}

void LanLiveSession::onClientDisconnected() {
    emit statusMessage(tr("Disconnected from host. LAN session ended."));
    leaveSession();
}

void LanLiveSession::onClientMessage(const QByteArray &payload) {
    recordReceived(payload.size());
    qCDebug(lanLog) << "client: message received," << payload.size() << "bytes";
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        qCWarning(lanLog) << "client: payload is not a JSON object — discarding";
        return;
    }
    QJsonObject obj = doc.object();
    QString type = obj.value(QStringLiteral("type")).toString();
    qCInfo(lanLog) << "client: frame type=" << type;
    if (type == QLatin1String("sessionWelcome")) {
        // Phase 9.9 §15.2: host's welcome — adopt the session-wide mode
        // and the current presenter pointer before any snapshot / hunks
        // arrive, so isPresenter() returns the right answer from the
        // first sync tick onwards. A legacy host that doesn't ship this
        // frame leaves us with our defaults (Edit, empty presenter),
        // which matches the pre-9.9 behaviour.
        SessionMode newMode = SessionMode::Edit;
        QString newPresenter;
        QString hostVer;
        LiveSession::decodeWelcomeJson(obj, &newMode, &newPresenter, &hostVer);
        _mode = newMode;
        _presenterMachineId = newPresenter;
        _gotSessionWelcome = true;
        qCInfo(lanLog) << "client: sessionWelcome mode=" << LiveSession::modeToWire(_mode)
                       << "presenter=" << _presenterMachineId.left(8)
                       << "hostVersion=" << (hostVer.isEmpty()
                                              ? QStringLiteral("<unknown>")
                                              : hostVer);
        // bugfix 2026-05-21: drive the GUI lock NOW that we know the
        // real session mode. The `joined` signal fired earlier (when
        // we shipped our hello) when _mode was still the Edit default.
        emit sessionModeChanged();
        // v1.7.2 §15.4: warn local UI if the host's version differs
        // from ours. Skip when both empty (e.g. dev builds without the
        // compile def) — a no-version-vs-no-version match is not a
        // mismatch in any meaningful sense.
        QString ourVer;
#ifdef MIDIEDITOR_RELEASE_VERSION_STRING_DEF
        ourVer = QStringLiteral(MIDIEDITOR_RELEASE_VERSION_STRING_DEF);
#endif
        if (!hostVer.isEmpty() && hostVer != ourVer) {
            emit hostVersionMismatch(hostVer, ourVer);
        }
        return;
    }
    // v1.7.2 §15.4: legacy-host detection. If any frame OTHER than
    // sessionWelcome arrives first on a freshly-joined client, the
    // host is on a pre-1.7.2 build (newer hosts ship sessionWelcome
    // as their first response to our hello). Emit exactly once.
    if (_role == Role::Joined
        && !_gotSessionWelcome
        && !_legacyHostWarningEmitted
        && type != QLatin1String("sessionWelcome")) {
        _legacyHostWarningEmitted = true;
        qCInfo(lanLog) << "client: legacy host detected (no sessionWelcome — pre-1.7.2)";
        // Defer the emit so the MainWindow modal opens AFTER
        // onClientMessage returns — same reentrancy reasoning as the
        // peerVersionMismatch fix in onServerMessage. Otherwise the
        // modal's nested event loop could process a host disconnect
        // mid-dispatch and the rest of this function would operate
        // on torn-down state.
        QTimer::singleShot(0, this, [this]() { emit legacyHostDetected(); });
    }
    // ---- Hat-passing wire frames (Phase 9.9b §15.2) -----------------
    if (type == QLatin1String("requestHat")) {
        // Host forwarded a viewer's request to us because we are the
        // current presenter. Show the Accept / Decline prompt via the
        // signal.
        QString reqMid, reqName;
        LiveSession::decodeRequestHatJson(obj, &reqMid, &reqName);
        // Race-resolution echo: if the requester is now the presenter
        // (a hatTransferred crossed on the wire), drop silently.
        if (!_presenterMachineId.isEmpty()
            && reqMid == _presenterMachineId) {
            qCDebug(lanLog) << "client: requestHat dropped — already presenter";
            return;
        }
        emit hatRequested(reqName, reqMid);
        return;
    }
    if (type == QLatin1String("hatTransferred")) {
        QString newMid, newName, reason;
        LiveSession::decodeHatTransferredJson(obj, &newMid, &newName, &reason);
        handleClientHatTransferred(newMid, newName, reason);
        return;
    }
    if (type == QLatin1String("hatRejected")) {
        QString reason;
        LiveSession::decodeHatRejectedJson(obj, &reason);
        handleIncomingHatRejected(reason);
        return;
    }
    if (type == QLatin1String("chat")) {
        handleClientChat(obj);
        return;
    }
    if (type == QLatin1String("viewState")) {
        handleClientViewState(obj);
        return;
    }
    if (type == QLatin1String("playback")) {
        handleClientPlayback(obj);
        return;
    }
    if (type == QLatin1String("sessionModeSwitch")) {
        handleClientSessionModeSwitch(obj);
        return;
    }
    if (type == QLatin1String("hunks")) {
        handleIncomingHunks(obj.value(QStringLiteral("author")).toString(),
                            obj.value(QStringLiteral("machineId")).toString(),
                            obj.value(QStringLiteral("hunks")).toArray(),
                            obj.value(QStringLiteral("actionLabel")).toString());
        applyFileMaxTickFromFrame(obj);
        // Plan §11.10q: ack this frame back to the sender (host) so
        // they see the apply result in their status bar. frameId is
        // server-assigned monotonic; older builds without the field
        // get frameId=0 and the sender silently ignores the ack.
        if (_client && _client->isConnected()) {
            // BUG-COLLAB-020: clamp negatives to 0 — qint64→quint64 of
            // a negative value (e.g. from an older buggy build) becomes
            // 0xFFFF…F, which would surface in the host's status bar
            // as a nonsense ack ID.
            qint64 raw = obj.value(QStringLiteral("frameId")).toVariant().toLongLong();
            if (raw < 0) raw = 0;
            quint64 frameId = quint64(raw);
            _client->sendMessage(encodeHunksAck(frameId, _lastApplyApplied, _lastApplySkipped));
        }
    } else if (type == QLatin1String("hunksAck")) {
        qint64 raw = obj.value(QStringLiteral("frameId")).toVariant().toLongLong();
        if (raw < 0) raw = 0;  // BUG-COLLAB-020
        handleIncomingHunksAck(
            obj.value(QStringLiteral("ackBy")).toString(),
            quint64(raw),
            obj.value(QStringLiteral("applied")).toInt(),
            obj.value(QStringLiteral("skipped")).toInt());
    } else if (type == QLatin1String("snapshot")) {
        handleIncomingSnapshot(obj.value(QStringLiteral("events")).toArray());
        applyFileMaxTickFromFrame(obj);
    } else if (type == QLatin1String("collabsync")) {
        handleIncomingCollabSync(obj.value(QStringLiteral("sidecar")).toObject());
    } else if (type == QLatin1String("filetransfer")) {
        QString filename = obj.value(QStringLiteral("filename")).toString();
        QByteArray bytes = QByteArray::fromBase64(
            obj.value(QStringLiteral("bytesB64")).toString().toLatin1());
        handleIncomingFileTransfer(filename, bytes);
    } else if (type == QLatin1String("joinrejected")) {
        handleIncomingJoinRejected(obj.value(QStringLiteral("reason")).toString());
    } else if (type == QLatin1String("historybundle")) {
        // Host shipped us its commits-since-fork (we were behind) OR
        // host is responding to our historyrequest (very rare on
        // client side; only happens if a future iteration adds two-way
        // request flow). Either way, we treat it as a fast-forward.
        handleIncomingHistoryBundle(obj.value(QStringLiteral("commits")).toArray(),
                                     obj.value(QStringLiteral("fromHash")).toString());
    } else if (type == QLatin1String("mergeresult")) {
        QStringList rejectedCommits;
        for (const QJsonValue &v : obj.value(QStringLiteral("rejectedCommitHashes")).toArray()) {
            rejectedCommits.append(v.toString());
        }
        handleIncomingMergeResult(
            obj.value(QStringLiteral("baseHead")).toString(),
            obj.value(QStringLiteral("newHead")).toString(),
            obj.value(QStringLiteral("acceptedHunks")).toArray(),
            obj.value(QStringLiteral("rejectedHunks")).toArray(),
            rejectedCommits);
    }
    // hello / heartbeat ignored on the client side for now
}

// ---------------------------------------------------------------------
// Server-side: peer connect / disconnect / message routing
// ---------------------------------------------------------------------
void LanLiveSession::onServerPeerConnected(IPeerLink *peer) {
    // Don't ship snapshot/sidecar yet — the hello frame tells us whether
    // the peer already has the file. If not, we send a `filetransfer`
    // first so they can open the same .mid before any state-sync arrives.
    Q_UNUSED(peer);
    _retryAttempt = 0;  // Plan §11.10h: success restores the retry budget
    emit peerCountChanged(_server->peerCount());
    emit statusMessage(tr("Peer connected — %1 total.").arg(_server->peerCount()));
}

void LanLiveSession::onServerPeerDisconnected(IPeerLink *peer) {
    qCInfo(lanLog) << "session: peer disconnected, remaining="
                   << (_server ? _server->peerCount() : 0);
    // 2026-05-24: drop any pending-merge entries that referenced this
    // peer. Without this, a subsequent historybundle / mergeresult /
    // rejectReturningPeer with a matching peerMachineId could call
    // sendMessage on the now-dangling IPeerLink stored in
    // PendingMerge::peer. (bughunter MEDIUM finding)
    for (auto it = _pendingMerges.begin(); it != _pendingMerges.end(); ) {
        if (it.value().peer == peer) {
            it = _pendingMerges.erase(it);
        } else {
            ++it;
        }
    }
    if (!_server) return;
    emit peerCountChanged(_server->peerCount());
    emit statusMessage(tr("Peer disconnected — %1 remaining.").arg(_server->peerCount()));
}

void LanLiveSession::onServerMessage(IPeerLink *peer, const QByteArray &payload) {
    recordReceived(payload.size());
    // BUG-COLLAB-030: heartbeat liveness check. Any frame from a peer
    // counts as "still alive" — heartbeat, hunks, hello, anything. The
    // periodic onHeartbeatTick uses this timestamp to spot silent
    // peers and kick them.
    if (peer) peer->touchLastSeen(QDateTime::currentMSecsSinceEpoch());
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        qCWarning(lanLog) << "server: dropping non-JSON payload," << payload.size() << "bytes";
        return;
    }
    QJsonObject obj = doc.object();
    QString type = obj.value(QStringLiteral("type")).toString();
    qCDebug(lanLog) << "server: frame type=" << type << "from peer" << peer->peerLabel();

    if (type == QLatin1String("hello")) {
        QString name = obj.value(QStringLiteral("displayName")).toString();
        QString peerMachineId = obj.value(QStringLiteral("machineId")).toString();
        QString peerSessionId = obj.value(QStringLiteral("sessionId")).toString();
        QString peerHead = obj.value(QStringLiteral("currentHead")).toString();
        QStringList peerTail;
        for (const QJsonValue &v : obj.value(QStringLiteral("historyTailHashes")).toArray()) {
            QString h = v.toString();
            if (!h.isEmpty()) peerTail.append(h);
        }
        qCInfo(lanLog) << "server: hello from" << name
                       << "sessionId=" << peerSessionId.left(8)
                       << "head=" << peerHead.left(8)
                       << "tailDepth=" << peerTail.size();
        peer->setPeerLabel(name);

        // BUG-COLLAB-030: ghost-peer dedup. If the user-side network
        // dropped without a clean TCP FIN (Wi-Fi disable, sleep,
        // Ethernet unplug), the host's TCP socket can hang in a
        // "still-connected" state indefinitely (Windows TCP-keepalive
        // default is 2 hours). When the same client reconnects, the
        // host ends up with TWO peer entries for the same physical
        // machine — old one zombie-alive, new one actually working.
        //
        // Fix: when a `hello` arrives carrying a machineId we already
        // have a peer for, close the old peer's socket. Its
        // disconnected() signal will fire, onPeerDisconnected will
        // remove it from _peers, and the count returns to sane.
        // Match by machineId only — the same physical PC reconnecting
        // is the case we care about; same-displayName-different-PC is
        // fine (and rare in practice).
        if (_server && !peerMachineId.isEmpty()) {
            QList<IPeerLink *> stale;
            for (IPeerLink *other : _server->peers()) {
                if (other == peer) continue;
                if (other->peerMachineId() == peerMachineId) {
                    stale.append(other);
                }
            }
            for (IPeerLink *old : stale) {
                qCInfo(lanLog) << "server: kicking ghost peer for"
                               << peerMachineId.left(8)
                               << "(re-hello from same machine)";
                old->closeConnection();
            }
        }
        peer->setPeerMachineId(peerMachineId);
        emit peerLabelsChanged();

        // v1.7.2 §15.4: surface version mismatches to the host's own
        // UI. A missing field = pre-1.7.2 build (the "blind generation"
        // — they have no UI to surface a reverse warning, so the host
        // is the only one who can tell the user to update). A present-
        // but-different version is informational.
        {
            QString peerVer = obj.value(QStringLiteral("appVersion")).toString();
            QString ourVer;
#ifdef MIDIEDITOR_RELEASE_VERSION_STRING_DEF
            ourVer = QStringLiteral(MIDIEDITOR_RELEASE_VERSION_STRING_DEF);
#endif
            if (peerVer != ourVer) {
                qCInfo(lanLog) << "server: peer version mismatch — peer="
                               << (peerVer.isEmpty()
                                   ? QStringLiteral("<pre-1.7.2>")
                                   : peerVer)
                               << "ours=" << ourVer;
                // CRITICAL FIX 2026-05-24 (host-crash on legacy-client
                // connect): defer the signal emit. The MainWindow slot
                // opens a modal QMessageBox::information which spins a
                // nested event loop processing socket events +
                // DeferredDelete. If the legacy peer (or any other
                // peer) disconnects during the modal, IPeerLink *peer
                // becomes dangling. The remainder of this function
                // (sendMessage on `peer` at line 1752 + the
                // reconciliation branches below) would then UAF.
                // the user-reported crash. Deferring via QTimer makes
                // the dialog open AFTER onServerMessage returns and
                // `peer` is out of scope.
                QTimer::singleShot(0, this,
                    [this, name, peerMachineId, peerVer, ourVer]() {
                        emit peerVersionMismatch(name, peerMachineId, peerVer, ourVer);
                    });
            }
        }

        // Phase 9.9 §15.2: ship sessionWelcome as the FIRST host-to-joiner
        // frame so the joiner knows the mode + presenter pointer before
        // any snapshot / sidecar / filetransfer arrives. A legacy joiner
        // (pre-9.9 build) silently ignores the frame, which is fine — it
        // already behaves like Edit mode and host-side hunk validation
        // catches any unauthorised broadcasts.
        peer->sendMessage(encodeSessionWelcome());

        // -- Returning-peer reconciliation (Plan §11.10b, Phase 9.5g) --
        // If the joining peer's sessionId matches ours, we're seeing the
        // same logical file at potentially different commits. Decide
        // what to do based on the relation between our heads.
        QString hostSessionId = CollabService::instance()->sessionId();
        QString hostHead = CollabService::instance()->currentHead();
        QJsonObject hostSidecar = CollabService::instance()->currentSidecarJson();
        QJsonArray hostHistory = hostSidecar.value(QStringLiteral("history")).toArray();
        QStringList hostTail = HistoryReconciliation::tailHashes(hostHistory);

        bool sameSession = !peerSessionId.isEmpty()
                            && peerSessionId == hostSessionId;
        if (sameSession) {
            HistoryReconciliation::Relation rel = HistoryReconciliation::classify(
                hostHead, peerHead, hostTail, peerTail);
            QString mergeBase = HistoryReconciliation::findMergeBase(hostTail, peerTail);
            qCInfo(lanLog) << "session: reconcile relation=" << static_cast<int>(rel)
                           << "mergeBase=" << mergeBase.left(8);

            switch (rel) {
            case HistoryReconciliation::Relation::SameHead: {
                // Heads match — skip filetransfer, just ship sidecar in
                // case peer's is missing/older, and let sync take over.
                QJsonObject sidecar = CollabService::instance()->currentSidecarJson();
                if (!sidecar.isEmpty()) peer->sendMessage(encodeCollabSync(sidecar));
                emit statusMessage(tr("Peer %1 reconnected at the same commit — no merge needed.").arg(name));
                return;
            }
            case HistoryReconciliation::Relation::RemoteBehindLocal: {
                // Peer is behind us — ship our commits-since-fork so
                // they auto-fast-forward. No dialog on either side.
                QJsonArray hostSlice = HistoryReconciliation::commitsSinceFork(
                    hostHistory, mergeBase);
                qCInfo(lanLog) << "session: peer behind by" << hostSlice.size()
                               << "commits — sending fast-forward bundle";
                peer->sendMessage(encodeHistoryBundle(hostSlice, mergeBase));
                emit statusMessage(tr("Peer %1 reconnected, fast-forwarded by %2 commit(s).")
                                       .arg(name).arg(hostSlice.size()));
                return;
            }
            case HistoryReconciliation::Relation::LocalBehindRemote:
            case HistoryReconciliation::Relation::Diverged: {
                // Peer has commits we don't. Request their slice; we'll
                // emit returningPeerArrived once it arrives.
                if (rel == HistoryReconciliation::Relation::Diverged) {
                    // Send our commits to peer first so they fast-forward.
                    QJsonArray hostSlice = HistoryReconciliation::commitsSinceFork(
                        hostHistory, mergeBase);
                    peer->sendMessage(encodeHistoryBundle(hostSlice, mergeBase));
                }
                PendingMerge pm;
                pm.peer            = peer;
                pm.peerName        = name;
                pm.peerHead        = peerHead;
                pm.commonAncestor  = mergeBase;
                _pendingMerges.insert(peerMachineId, pm);
                peer->sendMessage(encodeHistoryRequest(mergeBase, peerHead));
                emit statusMessage(tr("Peer %1 has %2 unmerged commit(s) — awaiting their bundle…")
                                       .arg(name).arg("?"));
                return;
            }
            case HistoryReconciliation::Relation::Unrelated:
                // Same sessionId but no overlap (very long fork or compaction
                // truncated the tails). Fall through to wholesale file
                // transfer with a warning in the log.
                qCWarning(lanLog) << "session: matching sessionId but no merge "
                                  << "base in supplied tails; falling back to filetransfer";
                break;
            }
        }
        // Default path: ship file + sidecar (existing behavior).

        QString filename;
        QByteArray bytes = readActiveFileBytes(&filename);
        if (bytes.isEmpty()) {
            // Two reasons readActiveFileBytes returns empty: host has no
            // saved path (unsaved file), or the file exceeds the
            // ~24 MB transport budget. Tell the peer explicitly so
            // they don't sit waiting indefinitely, then drop the connection.
            QString reason = (_file && !_file->path().isEmpty())
                ? tr("Host file is too large for the LAN transport "
                     "(over 24 MB). Save a slimmer .mid before sharing.")
                : tr("Host hasn't saved this file yet. Tell the host "
                     "to save the .mid before joining.");
            qCWarning(lanLog) << "session: rejecting" << name << "—" << reason;
            peer->sendMessage(encodeJoinRejected(reason));
            // Close the link; the server's onPeerDisconnected will then
            // prune our peer list.
            peer->closeConnection();
            emit statusMessage(reason);
        } else {
            qCInfo(lanLog) << "session: shipping" << bytes.size()
                           << "bytes of" << filename << "to" << name;
            peer->sendMessage(encodeFileTransfer(filename, bytes));

            // Sidecar belongs to the file we just sent; ship it too so
            // the peer adopts the same history. Snapshot is unnecessary
            // — transferred bytes already match our on-disk state, and
            // the sync timer reconciles any in-memory edits.
            QJsonObject sidecar = CollabService::instance()->currentSidecarJson();
            if (!sidecar.isEmpty()) {
                peer->sendMessage(encodeCollabSync(sidecar));
            }
        }
        return;
    }
    if (type == QLatin1String("hunks")) {
        QString author = obj.value(QStringLiteral("author")).toString();
        QString machineId = obj.value(QStringLiteral("machineId")).toString();
        QJsonArray hunks = obj.value(QStringLiteral("hunks")).toArray();
        QString actionLabel = obj.value(QStringLiteral("actionLabel")).toString();
        // Phase 9.9b §15.2: Show-mode authorisation gate. Drops hunks
        // from any peer that isn't the current presenter — defensive
        // even when the UI lock is in place, per the design note
        // "never trust client-side UI lock alone".
        if (!isHunkSenderAuthorised(machineId, author, hunks.size())) {
            return;
        }
        handleIncomingHunks(author, machineId, hunks, actionLabel);
        applyFileMaxTickFromFrame(obj);
        // Plan §11.10q: ack this frame back to the originator so they
        // can show "✓ N events accepted by HOST" in their status bar.
        // Sent only to the source peer (1:1 case is degenerate but
        // mesh-safe).
        if (peer) {
            // BUG-COLLAB-020: see client-side handler.
            qint64 raw = obj.value(QStringLiteral("frameId")).toVariant().toLongLong();
            if (raw < 0) raw = 0;
            quint64 frameId = quint64(raw);
            peer->sendMessage(encodeHunksAck(frameId, _lastApplyApplied, _lastApplySkipped));
        }
        // Forward to all other peers (echo-loop prevention per Plan §11.2).
        if (_server) {
            int otherPeers = _server->peerCount() - 1;  // exclude originator
            if (otherPeers > 0 && _server->broadcastExcept(payload, peer))
                recordSent(payload.size(), otherPeers);
        }
        return;
    }
    // ---- Hat-passing wire frames (Phase 9.9b §15.2) -----------------
    if (type == QLatin1String("requestHat")) {
        QString reqMid, reqName;
        LiveSession::decodeRequestHatJson(obj, &reqMid, &reqName);
        handleIncomingRequestHat(peer, reqMid, reqName);
        return;
    }
    if (type == QLatin1String("hatTransferred")) {
        QString newMid, newName, reason;
        LiveSession::decodeHatTransferredJson(obj, &newMid, &newName, &reason);
        // The sender's machineId is the *peer* link's identity here —
        // not the JSON payload, because the payload only carries the
        // target. The presenter's frame is signed by the TCP/WebRTC
        // link it arrived on.
        QString senderMid = peer ? peer->peerMachineId() : QString();
        handleIncomingHatTransferred(peer, senderMid, newMid, newName, reason);
        return;
    }
    if (type == QLatin1String("yieldHat")) {
        handleIncomingYieldHat(peer);
        return;
    }
    if (type == QLatin1String("chat")) {
        handleIncomingChat(peer, obj, payload);
        return;
    }
    if (type == QLatin1String("viewState")) {
        handleIncomingViewState(peer, obj, payload);
        return;
    }
    if (type == QLatin1String("playback")) {
        handleIncomingPlayback(peer, obj, payload);
        return;
    }
    if (type == QLatin1String("hunksAck")) {
        qint64 raw = obj.value(QStringLiteral("frameId")).toVariant().toLongLong();
        if (raw < 0) raw = 0;  // BUG-COLLAB-020
        handleIncomingHunksAck(
            obj.value(QStringLiteral("ackBy")).toString(),
            quint64(raw),
            obj.value(QStringLiteral("applied")).toInt(),
            obj.value(QStringLiteral("skipped")).toInt());
        return;
    }
    if (type == QLatin1String("historyrequest")) {
        // Peer (the host's perspective is reversed: a CLIENT can also act
        // as the requester here when both sides need to exchange slices).
        // We respond with our slice from fromHash..toHash.
        handleIncomingHistoryRequest(peer,
            obj.value(QStringLiteral("fromHash")).toString(),
            obj.value(QStringLiteral("toHash")).toString());
        return;
    }
    if (type == QLatin1String("historybundle")) {
        // Host received the peer's commits-since-fork in response to
        // its historyrequest. Synthesize a PrBundle and emit the
        // returningPeerArrived signal so the host UI shows the merge
        // dialog.
        handleIncomingHistoryBundle(obj.value(QStringLiteral("commits")).toArray(),
                                     obj.value(QStringLiteral("fromHash")).toString());
        return;
    }
    // snapshot / heartbeat ignored host-side for now
}

// ---------------------------------------------------------------------
// Apply incoming changes locally
// ---------------------------------------------------------------------
void LanLiveSession::handleIncomingHunks(const QString &author,
                                          const QString &machineId,
                                          const QJsonArray &hunks,
                                          const QString &actionLabel) {
    // Plan §11.10q: reset apply-result members so the dispatcher can
    // build an accurate hunksAck from this call's outcome.
    _lastApplyApplied = 0;
    _lastApplySkipped = 0;
    if (!_file) {
        qCWarning(lanLog) << "session: incoming hunks dropped — no file";
        return;
    }
    if (hunks.isEmpty()) return;

    // Review mode (§11.10c): queue instead of apply. The user reviews
    // the accumulated bundle later via the Collab menu. This is purely
    // host-/peer-local — the sender doesn't know we paused, so the
    // wire protocol is unchanged.
    if (_reviewModeEnabled) {
        for (const QJsonValue &v : hunks) _pendingReviewHunks.append(v);
        _pendingReviewAuthor = author;
        _pendingReviewMachineId = machineId;
        qCInfo(lanLog) << "session: queued" << hunks.size()
                       << "hunks from" << author
                       << "for review (total pending="
                       << _pendingReviewHunks.size() << ")";
        emit pendingReviewChanged(_pendingReviewHunks.size());
        emit statusMessage(tr("Live edits paused for review: %1 hunk(s) waiting from %2.")
                                .arg(_pendingReviewHunks.size()).arg(author));
        return;
    }

    qCInfo(lanLog) << "session: applying" << hunks.size() << "hunks from" << author;

    QList<QJsonObject> hunkList;
    hunkList.reserve(hunks.size());
    for (const QJsonValue &v : hunks) hunkList.append(v.toObject());

    int applied = 0;
    // Plan §11.10p: prefer the peer's tool name in the apply label /
    // commit message so the Protocol view + Collaboration log show
    // "Chord Explode (PC2)" instead of the generic "Live sync from
    // PC2". Falls back to the generic phrase when the peer's build
    // didn't include `actionLabel` (older builds before §11.10p).
    QString applyLabel = actionLabel.isEmpty()
        ? QStringLiteral("Live sync from %1").arg(author)
        : QStringLiteral("%1 (from %2)").arg(actionLabel, author);
    PrApply::Result r = PrApply::apply(_file, hunkList, author, applyLabel, /*silent=*/true);
    qCInfo(lanLog) << "session: apply result added=" << r.addedCount
                   << "removed=" << r.removedCount
                   << "modified=" << r.modifiedCount
                   << "skipped=" << r.skippedCount;

    // Re-baseline so our next sync tick doesn't echo this back as a diff.
    _lastSyncedSnapshot = MidiSnapshot::ofFile(_file);

    applied = r.addedCount + r.removedCount + r.modifiedCount;
    _lastApplyApplied = applied;
    _lastApplySkipped = r.skippedCount;
    if (applied > 0) {
        // Live-sync applies are silent (no auto-save), so onFileSaved's
        // commit path doesn't fire. Record an explicit history entry so the
        // Collaboration log shows incoming peer activity.
        QString logMsg = actionLabel.isEmpty()
            ? QStringLiteral("Live: %1 change(s) from %2").arg(applied).arg(author)
            : QStringLiteral("%1 — %2 change(s) from %3")
                  .arg(actionLabel).arg(applied).arg(author);
        CollabService::instance()->recordRemoteLiveSync(
            _file, author, machineId, logMsg, hunks);
    }

    if (r.skippedCount > 0) {
        emit statusMessage(tr("Live sync from %1: %2 hunks applied, %3 skipped.")
                                .arg(author).arg(applied).arg(r.skippedCount));
    }
}

void LanLiveSession::handleIncomingSnapshot(const QJsonArray &events) {
    if (!_file) {
        qCWarning(lanLog) << "session: incoming snapshot dropped — no file";
        return;
    }
    qCInfo(lanLog) << "session: incoming snapshot —" << events.size()
                   << "host events, local has" << _file->numTracks() << "tracks";

    // Pre-flight track-range scan: if the host's snapshot references track
    // indices that don't exist locally, we can't apply those events. PrApply
    // will skip them with a warning, but we still want to surface this in
    // the status bar so the user knows the files weren't structurally
    // matched.
    int outOfRange = 0;
    int maxTrackSeen = -1;
    for (const QJsonValue &v : events) {
        if (!v.isObject()) continue;
        int t = v.toObject().value(QStringLiteral("track")).toInt(-1);
        if (t > maxTrackSeen) maxTrackSeen = t;
        if (t >= 0 && t >= _file->numTracks()) ++outOfRange;
    }
    if (outOfRange > 0) {
        qCWarning(lanLog) << "session: host snapshot references" << outOfRange
                          << "events on tracks beyond local file (max host track="
                          << maxTrackSeen << ", local numTracks=" << _file->numTracks()
                          << "). Open the same .mid on both PCs for a clean sync.";
        emit statusMessage(tr("Warning: host's file has more tracks (%1) than yours (%2). "
                              "%3 events will be skipped — open the same .mid on both PCs.")
                                .arg(maxTrackSeen + 1).arg(_file->numTracks()).arg(outOfRange));
    }

    // The host's full state arrived as a one-shot diff (current vs nothing).
    // Compute the diff from OUR baseline (= our current snapshot at join
    // time) to the host's snapshot, and apply it. If we and the host
    // already match (e.g. both opened the same .mid), this is a no-op.
    QJsonArray current = MidiSnapshot::ofFile(_file);
    qCInfo(lanLog) << "session: computing diff (local=" << current.size()
                   << "host=" << events.size() << ")";
    QJsonArray hunks = MidiDiff::compute(current, events, _file->ticksPerQuarter());
    qCInfo(lanLog) << "session: diff produced" << hunks.size() << "hunks";
    if (hunks.isEmpty()) {
        _lastSyncedSnapshot = events;
        return;
    }

    QList<QJsonObject> hunkList;
    hunkList.reserve(hunks.size());
    for (const QJsonValue &v : hunks) hunkList.append(v.toObject());
    QString hostName = _hostDisplayName.isEmpty() ? QStringLiteral("host") : _hostDisplayName;
    qCInfo(lanLog) << "session: applying initial-sync hunks from" << hostName;
    PrApply::Result r = PrApply::apply(_file, hunkList, hostName,
                   tr("Joined LAN session — synced from %1").arg(hostName),
                   /*silent=*/true);
    qCInfo(lanLog) << "session: initial-sync result added=" << r.addedCount
                   << "removed=" << r.removedCount
                   << "modified=" << r.modifiedCount
                   << "skipped=" << r.skippedCount;
    _lastSyncedSnapshot = MidiSnapshot::ofFile(_file);

    int applied = r.addedCount + r.removedCount + r.modifiedCount;
    if (applied > 0) {
        QString logMsg = QStringLiteral("Joined LAN — initial sync from %1 (%2 changes)")
                              .arg(hostName).arg(applied);
        CollabService::instance()->recordRemoteLiveSync(
            _file, hostName, /*machineId=*/QString(), logMsg, hunks);
    }
}

// ---------------------------------------------------------------------
// Show Mode (Phase 9.9 §15.2) — bridge to the pure helpers in SessionMode.h
// ---------------------------------------------------------------------

bool LanLiveSession::isPresenter() const {
    // Idle / Edit mode → always editable.
    if (_role == Role::Idle) return true;
    if (_mode == SessionMode::Edit) return true;
    // Show mode — only the peer whose machineId matches the current
    // presenter pointer is allowed to edit.
    return !_presenterMachineId.isEmpty()
        && _presenterMachineId == CollabIdentity::machineId();
}

QByteArray LanLiveSession::encodeSessionWelcome() const {
    // v1.7.2+: include host's build version so the joiner can warn its
    // own user about a legacy host. The symmetric joiner→host check
    // lives on the `hello` frame; together they close the loop so any
    // version mismatch is visible from BOTH sides starting with 1.7.2.
    QString ver;
#ifdef MIDIEDITOR_RELEASE_VERSION_STRING_DEF
    ver = QStringLiteral(MIDIEDITOR_RELEASE_VERSION_STRING_DEF);
#endif
    QJsonObject o = LiveSession::encodeWelcomeJson(
        _mode, _presenterMachineId, ver);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeRequestHat() const {
    QJsonObject o = LiveSession::encodeRequestHatJson(
        CollabIdentity::machineId(), CollabIdentity::displayName());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeHatTransferred(
        const QString &newPresenterMachineId,
        const QString &newPresenterDisplayName,
        const QString &reason) const {
    QJsonObject o = LiveSession::encodeHatTransferredJson(
        newPresenterMachineId, newPresenterDisplayName, reason);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeHatRejected(const QString &reason) const {
    QJsonObject o = LiveSession::encodeHatRejectedJson(reason);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeYieldHat() const {
    QJsonObject o = LiveSession::encodeYieldHatJson();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeChat(const QString &text, qint64 timestampMs) const {
    QJsonObject o = LiveSession::encodeChatJson(
        CollabIdentity::machineId(),
        CollabIdentity::displayName(),
        text,
        timestampMs);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeViewState(
        const LiveSession::ViewportState &viewport,
        const QVector<bool> &trackVisibility,
        const QVector<bool> &channelVisibility) const {
    QJsonObject o = LiveSession::encodeViewStateJson(
        CollabIdentity::machineId(),
        viewport, trackVisibility, channelVisibility);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodePlayback(const QString &action,
                                          int tickPosition) const {
    QJsonObject o = LiveSession::encodePlaybackJson(action, tickPosition);
    // Add sender so the host-side validator can check sender ==
    // presenterMachineId (same gate as the hunks validator).
    o.insert(QStringLiteral("sender"), CollabIdentity::machineId());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeSessionModeSwitch(
        SessionMode newMode, const QString &presenterMachineId) const {
    QJsonObject o = LiveSession::encodeSessionModeSwitchJson(
        newMode, presenterMachineId);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ---------------------------------------------------------------------
// Wire encoders
// ---------------------------------------------------------------------
QByteArray LanLiveSession::encodeHello() const {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("hello"));
    o.insert(QStringLiteral("sessionId"), CollabService::instance()->sessionId());
    o.insert(QStringLiteral("displayName"), CollabIdentity::displayName());
    o.insert(QStringLiteral("machineId"), CollabIdentity::machineId());
    // v1.7.2+: build version so the host can warn its own user about
    // older joiners. Missing field = pre-1.7.2 build (the "blind
    // generation" — they have no UI to surface a reverse warning).
#ifdef MIDIEDITOR_RELEASE_VERSION_STRING_DEF
    o.insert(QStringLiteral("appVersion"),
             QStringLiteral(MIDIEDITOR_RELEASE_VERSION_STRING_DEF));
#endif
    // Returning-peer reconciliation (Plan §11.10b, Phase 9.5g): the host
    // uses these to detect a fork against its own sidecar and decide
    // whether to ship the file wholesale, fast-forward us, or invoke a
    // cherry-pick merge dialog. Empty when we have no sidecar yet.
    o.insert(QStringLiteral("currentHead"),
             CollabService::instance()->currentHead());
    QJsonObject sidecar = CollabService::instance()->currentSidecarJson();
    QStringList tail = HistoryReconciliation::tailHashes(
        sidecar.value(QStringLiteral("history")).toArray());
    QJsonArray tailJson;
    for (const QString &h : tail) tailJson.append(h);
    o.insert(QStringLiteral("historyTailHashes"), tailJson);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeSnapshot(const QJsonArray &events) const {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("snapshot"));
    o.insert(QStringLiteral("events"), events);
    // Plan §11.10j: file-length sidecar so insert-measures into an empty
    // section (which shifts no events) still propagates. Receivers that
    // don't know this field ignore it.
    if (_file) o.insert(QStringLiteral("fileMaxTick"), _file->endTick());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeHunks(const QJsonArray &hunks) const {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("hunks"));
    o.insert(QStringLiteral("author"), CollabIdentity::displayName());
    o.insert(QStringLiteral("machineId"), CollabIdentity::machineId());
    o.insert(QStringLiteral("hunks"), hunks);
    if (_file) o.insert(QStringLiteral("fileMaxTick"), _file->endTick());
    // Plan §11.10p: include the most recent finished `Protocol` action
    // description so the receiver can label the resulting commit with
    // the actual tool that ran (e.g. "Chord Explode") instead of the
    // generic "Live: N changes from PC2".
    if (!_lastActionLabel.isEmpty()) {
        o.insert(QStringLiteral("actionLabel"), _lastActionLabel);
    }
    // Plan §11.10q: monotonic frameId so the receiver can ack this
    // specific broadcast and the sender can correlate the ack back to
    // the frame it sent (e.g. "PC2 confirmed your last sync of 17 events").
    o.insert(QStringLiteral("frameId"), qint64(_outboundFrameSeq++));
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeHunksAck(quint64 frameId, int applied, int skipped) const {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("hunksAck"));
    o.insert(QStringLiteral("frameId"), qint64(frameId));
    o.insert(QStringLiteral("applied"), applied);
    o.insert(QStringLiteral("skipped"), skipped);
    o.insert(QStringLiteral("ackBy"), CollabIdentity::displayName());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeHeartbeat() const {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("heartbeat"));
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeCollabSync(const QJsonObject &sidecar) const {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("collabsync"));
    o.insert(QStringLiteral("sidecar"), sidecar);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeFileTransfer(const QString &filename,
                                              const QByteArray &bytes) const {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("filetransfer"));
    o.insert(QStringLiteral("filename"), filename);
    // Base64 keeps the JSON wrapper UTF-8 safe; LanTransport's 32 MB
    // frame cap covers the encoded size for any realistic .mid.
    o.insert(QStringLiteral("bytesB64"), QString::fromLatin1(bytes.toBase64()));
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeJoinRejected(const QString &reason) const {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("joinrejected"));
    o.insert(QStringLiteral("reason"), reason);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeHistoryRequest(const QString &fromHash,
                                                const QString &toHash) const {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("historyrequest"));
    o.insert(QStringLiteral("fromHash"), fromHash);
    o.insert(QStringLiteral("toHash"), toHash);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeHistoryBundle(const QJsonArray &commits,
                                                const QString &fromHash) const {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("historybundle"));
    o.insert(QStringLiteral("fromHash"), fromHash);
    o.insert(QStringLiteral("commits"), commits);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::encodeMergeResult(const QString &baseHead,
                                              const QString &newHead,
                                              const QJsonArray &acceptedHunks,
                                              const QJsonArray &rejectedHunks,
                                              const QStringList &rejectedCommitHashes) const {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("mergeresult"));
    o.insert(QStringLiteral("baseHead"), baseHead);
    o.insert(QStringLiteral("newHead"), newHead);
    o.insert(QStringLiteral("acceptedHunks"), acceptedHunks);
    // rejectedHunks lets the peer un-apply the work the host turned
    // down, so both sides converge to the host's chosen state. Without
    // this the peer would still carry its rejected commits in its
    // live file and the next sync would cause a re-divergence.
    o.insert(QStringLiteral("rejectedHunks"), rejectedHunks);
    QJsonArray rj;
    for (const QString &h : rejectedCommitHashes) rj.append(h);
    o.insert(QStringLiteral("rejectedCommitHashes"), rj);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanLiveSession::readActiveFileBytes(QString *outFilename) const {
    if (!_file) return QByteArray();
    QString path = _file->path();
    if (path.isEmpty()) return QByteArray();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    QByteArray bytes = f.readAll();
    // LanTransport caps a single frame at 32 MB; base64 expands ~133%, so
    // raw .mid larger than ~24 MB cannot be shipped without the transport
    // killing the connection. Reject up-front with a clear log so the
    // host knows why a peer never finished joining.
    constexpr int kMaxRawBytes = 24 * 1024 * 1024;
    if (bytes.size() > kMaxRawBytes) {
        qCWarning(lanLog) << "session: file too large to ship —"
                          << bytes.size() << "bytes (limit"
                          << kMaxRawBytes << "). Save a smaller .mid.";
        return QByteArray();
    }
    if (outFilename) *outFilename = QFileInfo(path).fileName();
    return bytes;
}

void LanLiveSession::applyFileMaxTickFromFrame(const QJsonObject &frame) {
    if (!_file) {
        qCInfo(lanLog) << "session: applyFileMaxTickFromFrame skipped — no file bound";
        return;
    }
    QJsonValue raw = frame.value(QStringLiteral("fileMaxTick"));
    if (!raw.isDouble()) {
        // Older builds didn't include this field. Useful diagnostic
        // when only one peer in a session has the new code.
        qCInfo(lanLog) << "session: incoming frame has no fileMaxTick "
                          "(remote may be running an older build)";
        return;
    }
    int incoming = raw.toInt(-1);
    int current = _file->endTick();
    if (incoming < 0 || incoming == current) {
        qCDebug(lanLog) << "session: incoming fileMaxTick" << incoming
                        << "matches local — no-op (sync tracker)";
        // Even when the value matches our local state, sync our
        // last-broadcast tracking to the peer's so the next syncTick
        // doesn't echo the same value back.
        _lastBroadcastEndTick = incoming;
        return;
    }
    // Plan §11.10j: mirror the remote file-length in BOTH directions.
    // Earlier versions only allowed grow ("never shrink locally") to
    // avoid orphaning local events past the new end, but that broke
    // delete-measures sync — host trims tail measures, peer keeps the
    // old length and the change appears one-sided. Events past the
    // shrunk end are not removed by setEndTick (it only updates the
    // length metadata); they remain in the channel maps and would
    // surface in the next snapshot diff if they're real.
    qCInfo(lanLog) << "session: mirroring remote end tick:"
                   << current << "→" << incoming;
    QString actionLabel = (incoming > current)
        ? tr("Sync: extend file length")
        : tr("Sync: shrink file length");
    const bool shrinking = (incoming < current);
    if (_file->protocol()) {
        _file->protocol()->startNewAction(actionLabel);
        _file->setEndTick(incoming);
        // BUG-COLLAB-022: setEndTick only updates the length metadata;
        // events past the new end stay in the channel maps, get
        // re-snapshotted on the next sync tick, and either re-broadcast
        // back (oscillation loop) or just leak as ghost notes that play
        // beyond the visible end. When the host shrinks the file we
        // remove our orphaned events too, in the SAME Protocol action,
        // so undo restores both pieces atomically.
        if (shrinking) {
            int removed = 0;
            // Channels 0..18: 0–15 audio, 16 text/key-sig meta, 17 tempo,
            // 18 time-sig (Plan §11.10j). All carry events that shouldn't
            // exceed the file length.
            for (int ch = 0; ch < 19; ++ch) {
                MidiChannel *channel = _file->channel(ch);
                if (!channel) continue;
                QList<MidiEvent *> toRemove;
                QMultiMap<int, MidiEvent *> *map = channel->eventMap();
                for (auto it = map->begin(); it != map->end(); ++it) {
                    if (it.key() > incoming) toRemove.append(it.value());
                }
                for (MidiEvent *ev : toRemove) {
                    channel->removeEvent(ev);
                    ++removed;
                }
            }
            if (removed > 0) {
                qCInfo(lanLog) << "session: shrink removed" << removed
                               << "orphan event(s) past new end-tick"
                               << incoming;
            }
        }
        _file->protocol()->endAction();
    } else {
        _file->setEndTick(incoming);
    }
    // Track this so onSyncTick doesn't echo it back as a "changed".
    _lastBroadcastEndTick = incoming;
}

void LanLiveSession::handleIncomingHunksAck(const QString &ackBy,
                                              quint64 frameId,
                                              int applied,
                                              int skipped) {
    qCInfo(lanLog) << "session: hunksAck from" << ackBy
                   << "frame=" << frameId
                   << "applied=" << applied
                   << "skipped=" << skipped;
    QString peer = ackBy.isEmpty() ? tr("peer") : ackBy;
    if (skipped == 0 && applied == 0) {
        // Receiver got the frame but had nothing to apply (review-mode
        // queued it, or empty/end-tick-only frame). Don't surface as
        // either success or warning — just log.
        return;
    }
    QString text;
    if (skipped == 0) {
        text = tr("✓ %1 accepted %2 change%3.")
                   .arg(peer).arg(applied).arg(applied == 1 ? "" : "s");
    } else if (applied == 0) {
        text = tr("⚠ %1 rejected all %2 change%3 — see log.")
                   .arg(peer).arg(skipped).arg(skipped == 1 ? "" : "s");
    } else {
        text = tr("⚠ %1 accepted %2 of %3 change%4 — %5 skipped.")
                   .arg(peer).arg(applied).arg(applied + skipped)
                   .arg((applied + skipped) == 1 ? "" : "s").arg(skipped);
    }
    emit statusMessage(text);
}

void LanLiveSession::rewireProtocolListener() {
    // Plan §11.10p: capture every finished `Protocol` action's
    // description so the next outbound hunks frame can label its
    // commits with the actual tool that ran (Chord Explode, Track
    // Split, FFXIV Channel Fixer, …). The Protocol object is owned
    // by MidiFile, so the connection has to be torn down + re-made
    // whenever `_file` changes.
    if (_protocolActionFinishedConn) {
        QObject::disconnect(_protocolActionFinishedConn);
        _protocolActionFinishedConn = QMetaObject::Connection();
    }
    if (!_file || !_file->protocol()) return;
    Protocol *proto = _file->protocol();
    _protocolActionFinishedConn = connect(proto, &Protocol::actionFinished,
        this, [this, proto]() {
            // Defensive: ignore late-arriving signals after file swap.
            if (!_file || _file->protocol() != proto) return;
            int n = proto->stepsBack();
            if (n <= 0) return;
            ProtocolStep *step = proto->undoStep(n - 1);
            if (!step) return;
            QString label = step->description().trimmed();
            // Skip generic / framework labels that don't tell the user
            // which tool ran. The `Sync: …` labels are produced by our
            // own apply paths and would create a feedback loop; the
            // empty case happens when an action ends without a
            // description (rare).
            if (label.isEmpty()) return;
            if (label.startsWith(QStringLiteral("Sync:"))) return;
            if (label.startsWith(QStringLiteral("Merge PR"))) return;
            if (label.startsWith(QStringLiteral("Live"))) return;
            _lastActionLabel = label;
        });
}

void LanLiveSession::handleIncomingCollabSync(const QJsonObject &sidecar) {
    if (sidecar.isEmpty()) {
        qCInfo(lanLog) << "session: collabsync arrived but host has no sidecar";
        return;
    }
    // Buffer if a file transfer is pending acceptance OR if no file is
    // bound yet. Both cases mean the sidecar belongs to the file we're
    // about to swap to, NOT the user's currently loaded file. Adopting
    // it now would write the host's history into the user's local
    // sidecar — silent data loss.
    if (_filetransferPending || !_file) {
        qCInfo(lanLog) << "session: collabsync buffered (filetransferPending="
                       << _filetransferPending << ", _file=" << (_file != nullptr) << ")";
        _pendingSidecar = sidecar;
        return;
    }
    int incomingEntries = sidecar.value(QStringLiteral("history")).toArray().size();
    bool ok = CollabService::instance()->adoptRemoteSidecar(_file, sidecar);
    qCInfo(lanLog) << "session: adoptRemoteSidecar →"
                   << (ok ? "OK" : "FAILED")
                   << "(incoming entries=" << incomingEntries
                   << ", collab enabled=" << CollabService::instance()->isEnabled()
                   << ", file path=" << (_file ? _file->path() : QString("<null>"))
                   << ")";
    if (ok) {
        emit statusMessage(tr("Collaboration history synced from host (%1 entries).")
                                .arg(incomingEntries));
    } else if (!CollabService::instance()->isEnabled()) {
        emit statusMessage(tr("Host has %1 collaboration entries but Collaboration is "
                              "disabled in your Settings — entries not adopted.")
                                .arg(incomingEntries));
    } else {
        emit statusMessage(tr("Could not adopt host's %1 collaboration entries — "
                              "see midieditor_ai.log for details.")
                                .arg(incomingEntries));
    }
}

void LanLiveSession::handleIncomingFileTransfer(const QString &filename,
                                                 const QByteArray &bytes) {
    qCInfo(lanLog) << "session: file transfer offered —" << filename
                   << "(" << bytes.size() << "bytes)";
    if (filename.isEmpty() || bytes.isEmpty()) {
        qCWarning(lanLog) << "session: filetransfer payload incomplete; ignoring";
        return;
    }
    // Block sidecar adoption until the UI accepts the file transfer and
    // setActiveFile() runs. See _filetransferPending field comment.
    _filetransferPending = true;
    emit fileTransferOffered(filename, bytes);
}

void LanLiveSession::handleIncomingJoinRejected(const QString &reason) {
    qCWarning(lanLog) << "session: host rejected our join —" << reason;
    emit joinFailed(reason.isEmpty()
                    ? tr("Host rejected the connection.")
                    : reason);
    leaveSession();
}

void LanLiveSession::handleIncomingHistoryRequest(IPeerLink *peer,
                                                   const QString &fromHash,
                                                   const QString &toHash) {
    Q_UNUSED(toHash);  // we always go to head; toHash is informational
    qCInfo(lanLog) << "session: peer requested history slice since"
                   << fromHash.left(8);
    QJsonObject sidecar = CollabService::instance()->currentSidecarJson();
    QJsonArray history = sidecar.value(QStringLiteral("history")).toArray();
    QJsonArray slice = HistoryReconciliation::commitsSinceFork(history, fromHash);
    qCInfo(lanLog) << "session: shipping slice of" << slice.size()
                   << "commits to peer" << peer->peerLabel();
    peer->sendMessage(encodeHistoryBundle(slice, fromHash));
}

void LanLiveSession::handleIncomingHistoryBundle(const QJsonArray &commits,
                                                  const QString &fromHash) {
    qCInfo(lanLog) << "session: history bundle —" << commits.size()
                   << "commits since" << fromHash.left(8);
    if (commits.isEmpty()) return;

    if (_role == Role::Hosting) {
        // We requested this slice from a returning peer. Synthesize a
        // PrBundle and offer it to the host UI for cherry-pick review.
        // Find which pending merge this bundle answers — match by the
        // ancestor (fromHash) since each peer requested its own slice.
        QString peerMachineId;
        for (auto it = _pendingMerges.constBegin();
             it != _pendingMerges.constEnd(); ++it) {
            if (it.value().commonAncestor == fromHash) {
                peerMachineId = it.key();
                break;
            }
        }
        if (peerMachineId.isEmpty()) {
            qCWarning(lanLog) << "session: history bundle but no pending merge "
                              << "matches ancestor" << fromHash.left(8);
            return;
        }
        const PendingMerge pm = _pendingMerges.value(peerMachineId);
        PrBundle bundle = HistoryReconciliation::synthesizeBundle(
            commits, CollabService::instance()->sessionId(), fromHash);
        if (!bundle.isValid()) {
            qCWarning(lanLog) << "session: synthesized bundle is invalid; "
                              << "auto-rejecting peer" << pm.peerName;
            rejectReturningPeer(peerMachineId,
                                tr("Couldn't build a merge bundle from your history."));
            return;
        }
        emit returningPeerArrived(pm.peerName, peerMachineId, bundle);
        return;
    }

    // Client side: host is fast-forwarding us. Apply each commit's
    // hunks via PrApply, in order. Re-baseline so the next sync tick
    // sees a clean diff.
    if (!_file) {
        qCWarning(lanLog) << "session: history bundle dropped — no file";
        return;
    }
    int totalApplied = 0;
    for (const QJsonValue &v : commits) {
        QJsonObject c = v.toObject();
        QString author = c.value(QStringLiteral("author")).toString();
        QString message = c.value(QStringLiteral("message")).toString();
        QJsonArray hunks = c.value(QStringLiteral("hunks")).toArray();
        QList<QJsonObject> hunkList;
        hunkList.reserve(hunks.size());
        for (const QJsonValue &h : hunks) hunkList.append(h.toObject());
        PrApply::Result r = PrApply::apply(_file, hunkList, author, message,
                                           /*silent=*/true);
        // BUG-COLLAB-016: only record per-commit if THIS commit applied
        // something. The earlier guard tested the running total, so once
        // any commit produced a change, every subsequent (possibly empty)
        // commit also recorded a sidecar entry — duplicate / mis-attributed
        // history rows.
        int thisApplied = r.addedCount + r.removedCount + r.modifiedCount;
        totalApplied += thisApplied;

        // Mirror in our local sidecar so the Collab log stays in sync.
        if (thisApplied > 0) {
            CollabService::instance()->recordRemoteLiveSync(
                _file, author,
                c.value(QStringLiteral("machineId")).toString(),
                QStringLiteral("Fast-forward: %1").arg(message), hunks);
        }
    }
    _lastSyncedSnapshot = MidiSnapshot::ofFile(_file);
    QString hostName = _hostDisplayName.isEmpty()
        ? QStringLiteral("host") : _hostDisplayName;
    emit welcomeBackOffered(hostName, totalApplied, /*rejected=*/0,
                            /*divergedFilePath=*/QString());
    emit statusMessage(tr("Welcome back — fast-forwarded %1 change(s) from %2.")
                            .arg(totalApplied).arg(hostName));
}

void LanLiveSession::handleIncomingMergeResult(const QString &baseHead,
                                                const QString &newHead,
                                                const QJsonArray &acceptedHunks,
                                                const QJsonArray &rejectedHunks,
                                                const QStringList &rejectedCommitHashes) {
    Q_UNUSED(baseHead);
    Q_UNUSED(newHead);
    qCInfo(lanLog) << "session: merge result —" << acceptedHunks.size()
                   << "hunks accepted," << rejectedHunks.size()
                   << "rejected," << rejectedCommitHashes.size()
                   << "commit(s) rejected";
    if (!_file) {
        qCWarning(lanLog) << "session: merge result dropped — no file";
        return;
    }

    // If the host rejected anything, snapshot our pre-merge state to
    // <name>_diverged.mid in the same folder before reverting the
    // rejected work, so the user can recover it manually.
    QString divergedPath;
    if (!rejectedHunks.isEmpty() && !_activeFilePath.isEmpty()) {
        QFileInfo fi(_activeFilePath);
        QString stem = fi.completeBaseName();
        QString suffix = fi.suffix();
        QString divergedName = suffix.isEmpty()
            ? stem + QStringLiteral("_diverged")
            : stem + QStringLiteral("_diverged.") + suffix;
        divergedPath = fi.absolutePath() + QLatin1Char('/') + divergedName;
        bool ok = _file->save(divergedPath);
        qCInfo(lanLog) << "session: pre-merge state saved to"
                       << divergedPath << "ok=" << ok;
        if (!ok) divergedPath.clear();
    }

    QString hostName = _hostDisplayName.isEmpty()
        ? QStringLiteral("host") : _hostDisplayName;

    // Un-apply rejected hunks first so we converge to the host's
    // chosen state (the host applied only the accepted subset).
    int reverted = 0;
    if (!rejectedHunks.isEmpty()) {
        QList<QJsonObject> revertList;
        revertList.reserve(rejectedHunks.size());
        for (const QJsonValue &h : rejectedHunks) revertList.append(h.toObject());
        PrApply::Result rr = PrApply::applyInverted(
            _file, revertList, hostName,
            QStringLiteral("Reverted by merge: host rejected %1 hunk(s)")
                .arg(rejectedHunks.size()));
        reverted = rr.addedCount + rr.removedCount + rr.modifiedCount;
    }

    // Apply host's accepted hunks. Most are no-ops on the peer side
    // because the peer's file already has them (they came from the
    // peer's own commits); PrApply silently skips events that already
    // match. The rare case where it matters is when the host's
    // cherry-pick included hunks the peer hasn't seen yet (e.g. host
    // edits made between our hello and the merge dialog confirm).
    QList<QJsonObject> applyList;
    applyList.reserve(acceptedHunks.size());
    for (const QJsonValue &h : acceptedHunks) applyList.append(h.toObject());
    PrApply::Result r = PrApply::apply(
        _file, applyList, hostName,
        QStringLiteral("Merge result from %1").arg(hostName),
        /*silent=*/true);
    int applied = r.addedCount + r.removedCount + r.modifiedCount;
    _lastSyncedSnapshot = MidiSnapshot::ofFile(_file);

    if (applied + reverted > 0) {
        CollabService::instance()->recordRemoteLiveSync(
            _file, hostName, QString(),
            QStringLiteral("Merge from %1: %2 accepted, %3 reverted")
                .arg(hostName).arg(applied).arg(reverted),
            acceptedHunks);
    }

    emit welcomeBackOffered(hostName, applied,
                            rejectedCommitHashes.size(), divergedPath);
}

void LanLiveSession::acceptReturningPeerMerge(const QString &peerToken,
                                               const QJsonArray &acceptedHunks,
                                               const QJsonArray &rejectedHunks,
                                               const QStringList &rejectedCommitHashes) {
    auto it = _pendingMerges.find(peerToken);
    if (it == _pendingMerges.end()) {
        qCWarning(lanLog) << "session: acceptReturningPeerMerge — unknown peer token"
                          << peerToken;
        return;
    }
    PendingMerge pm = it.value();
    _pendingMerges.erase(it);
    if (!pm.peer) return;

    qCInfo(lanLog) << "session: host accepted" << acceptedHunks.size()
                   << "hunks from" << pm.peerName
                   << "(" << rejectedHunks.size() << "rejected hunks,"
                   << rejectedCommitHashes.size() << "rejected commits)";

    // Apply the host's selection to our own file so we converge.
    if (_file && !acceptedHunks.isEmpty()) {
        QList<QJsonObject> hunkList;
        hunkList.reserve(acceptedHunks.size());
        for (const QJsonValue &h : acceptedHunks) hunkList.append(h.toObject());
        PrApply::Result r = PrApply::apply(
            _file, hunkList, pm.peerName,
            QStringLiteral("Merge from %1 (returning peer)").arg(pm.peerName),
            /*silent=*/true);
        _lastSyncedSnapshot = MidiSnapshot::ofFile(_file);
        if (r.addedCount + r.removedCount + r.modifiedCount > 0) {
            CollabService::instance()->recordRemoteLiveSync(
                _file, pm.peerName, QString(),
                QStringLiteral("Merge from %1: %2 change(s) (host-cherry-picked)")
                    .arg(pm.peerName)
                    .arg(r.addedCount + r.removedCount + r.modifiedCount),
                acceptedHunks);
        }
    }

    QString newHead = CollabService::instance()->currentHead();
    QByteArray frame = encodeMergeResult(pm.commonAncestor, newHead,
                                         acceptedHunks, rejectedHunks,
                                         rejectedCommitHashes);
    pm.peer->sendMessage(frame);
    if (_server) _server->broadcastExcept(frame, pm.peer);
}

void LanLiveSession::setReviewMode(bool enabled) {
    if (_reviewModeEnabled == enabled) return;
    _reviewModeEnabled = enabled;
    QSettings s("MidiEditor", "NONE");
    s.setValue("Collab/lan/reviewMode", enabled);
    qCInfo(lanLog) << "session: review mode" << (enabled ? "ON" : "OFF");
    emit reviewModeChanged(enabled);
}

PrBundle LanLiveSession::pendingReviewBundle() const {
    PrBundle b;
    if (_pendingReviewHunks.isEmpty()) return b;
    b.sessionId  = CollabService::instance()->sessionId();
    b.author     = _pendingReviewAuthor.isEmpty()
                       ? QStringLiteral("(remote peer)")
                       : _pendingReviewAuthor;
    b.machineId  = _pendingReviewMachineId;
    b.parentHash = CollabService::instance()->currentHead();
    b.timestamp  = QDateTime::currentSecsSinceEpoch();
    b.message    = QStringLiteral("Live edits queued for review (%1 hunk(s))")
                        .arg(_pendingReviewHunks.size());
    b.hunks      = _pendingReviewHunks;
    return b;
}

void LanLiveSession::acknowledgeReviewApplied() {
    if (_file) {
        // Re-baseline so the next sync tick doesn't see the
        // just-applied hunks as a local delta to broadcast.
        _lastSyncedSnapshot = MidiSnapshot::ofFile(_file);
    }
    if (_pendingReviewHunks.isEmpty()) return;
    qCInfo(lanLog) << "session: review applied —"
                   << _pendingReviewHunks.size() << "hunks cleared from queue";
    _pendingReviewHunks = QJsonArray();
    _pendingReviewAuthor.clear();
    _pendingReviewMachineId.clear();
    emit pendingReviewChanged(0);
}

void LanLiveSession::discardPendingReview() {
    if (_pendingReviewHunks.isEmpty()) return;
    qCInfo(lanLog) << "session: discarding"
                   << _pendingReviewHunks.size() << "queued review hunks";
    _pendingReviewHunks = QJsonArray();
    _pendingReviewAuthor.clear();
    _pendingReviewMachineId.clear();
    emit pendingReviewChanged(0);
}

void LanLiveSession::rejectReturningPeer(const QString &peerToken,
                                          const QString &reason) {
    auto it = _pendingMerges.find(peerToken);
    if (it == _pendingMerges.end()) return;
    PendingMerge pm = it.value();
    _pendingMerges.erase(it);
    if (!pm.peer) return;
    qCInfo(lanLog) << "session: host rejected returning peer"
                   << pm.peerName << "—" << reason;
    pm.peer->sendMessage(encodeJoinRejected(
        reason.isEmpty()
            ? tr("Host doesn't want to merge your changes right now.")
            : reason));
    pm.peer->closeConnection();
}

void LanLiveSession::setActiveFile(MidiFile *file) {
    // Allowed in Idle (Phase 9.5i pre-connect file switch) and Joined
    // (file-transfer-on-join). Refuse Hosting — switching the host's
    // file mid-session is not a supported operation.
    if (_role == Role::Hosting) return;
    _file = file;
    rewireProtocolListener();
    if (!file) {
        _lastSyncedSnapshot = QJsonArray();
        _activeFilePath.clear();
        qCInfo(lanLog) << "session: setActiveFile(nullptr) — detached during swap";
        return;
    }
    _lastSyncedSnapshot = MidiSnapshot::ofFile(_file);
    _activeFilePath = _file->path();  // remembered for _diverged.mid placement
    qCInfo(lanLog) << "session: setActiveFile — baseline snapshot taken,"
                   << _lastSyncedSnapshot.size() << "events";
    // File swap complete; sidecar adoption is now safe to run on the
    // newly-open file (and its sidecar path).
    _filetransferPending = false;
    if (!_pendingSidecar.isEmpty()) {
        QJsonObject pending = _pendingSidecar;
        _pendingSidecar = QJsonObject();
        handleIncomingCollabSync(pending);
    }
}
