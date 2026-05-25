/*
 * MidiEditor AI
 *
 * Session-wide editing-rights model for Live Sessions (Phase 9.9 §15.2).
 *
 * Header-only so the helpers are unit-testable without dragging in the
 * full LanLiveSession dependency tree (LanDiscovery, LanTransport,
 * MidiFile, etc.). The functions are pure JSON-in / JSON-out — no Qt
 * meta-object machinery, no member state, no I/O — so they belong in
 * their own tiny module rather than inside the 1500-line LanLiveSession.
 *
 * Wire-string encoding is lowercase + stable across releases. Unknown
 * values fall back to Edit, which keeps newer hosts forward-compatible
 * with older joiners that don't recognise a future mode value (e.g.
 * "review", "lockstep", …).
 */

#ifndef SESSIONMODE_H
#define SESSIONMODE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1String>
#include <QString>
#include <QStringLiteral>
#include <QVector>

namespace LiveSession {

enum class Mode {
    /// Free-for-all: every peer can edit and broadcast hunks
    /// (the original Phase 9.5/9.6/9.8 behaviour).
    Edit,
    /// Presentation: only the current presenter can edit; everyone
    /// else watches. Hat can be passed via requestHat / hatTransferred.
    Show,
};

inline const char *modeToWire(Mode m) {
    switch (m) {
    case Mode::Show: return "show";
    case Mode::Edit:
    default:         return "edit";
    }
}

inline Mode modeFromWire(const QString &s) {
    if (s.compare(QLatin1String("show"), Qt::CaseInsensitive) == 0)
        return Mode::Show;
    return Mode::Edit;
}

/// Host → joiner welcome payload (the host's first frame in response
/// to the joiner's hello). Carries the session-wide mode and the
/// current presenter pointer so the joiner adopts both before any
/// snapshot / sidecar arrives.
///
/// \a appVersion is the host's MidiEditor AI build version (e.g.
/// "1.7.2"). Added in v1.7.2 alongside the symmetric field in
/// `hello`: a joiner that sees an empty / missing appVersion knows
/// the host is on a pre-1.7.2 build that doesn't ship version info.
inline QJsonObject encodeWelcomeJson(Mode mode,
                                     const QString &presenterMachineId,
                                     const QString &appVersion = QString()) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("sessionWelcome"));
    o.insert(QStringLiteral("mode"), QString::fromLatin1(modeToWire(mode)));
    o.insert(QStringLiteral("presenterMachineId"), presenterMachineId);
    if (!appVersion.isEmpty())
        o.insert(QStringLiteral("appVersion"), appVersion);
    return o;
}

/// Inverse of encodeWelcomeJson. Missing or unknown fields are treated
/// as a legacy host that didn't ship the frame: outMode → Edit,
/// outPresenterMachineId → empty, outAppVersion → empty (which the
/// caller interprets as "pre-1.7.2"). Caller falls back gracefully —
/// the pre-9.9 behaviour is exactly Edit mode with no presenter pointer.
inline void decodeWelcomeJson(const QJsonObject &obj,
                              Mode *outMode,
                              QString *outPresenterMachineId,
                              QString *outAppVersion = nullptr) {
    if (outMode) {
        *outMode = modeFromWire(obj.value(QStringLiteral("mode")).toString());
    }
    if (outPresenterMachineId) {
        *outPresenterMachineId =
            obj.value(QStringLiteral("presenterMachineId")).toString();
    }
    if (outAppVersion) {
        *outAppVersion = obj.value(QStringLiteral("appVersion")).toString();
    }
}

// -----------------------------------------------------------------------
// Hat-passing wire frames (Phase 9.9b §15.2)
// -----------------------------------------------------------------------
//
// Three frame types implement the strict request-and-approve handshake:
//
//   requestHat       — viewer → host → routed to current presenter.
//                      The viewer asks for editing rights. The presenter
//                      sees a notification and either calls
//                      transferHatTo(...) or ignores.
//
//   hatTransferred   — presenter → host → broadcast to every peer.
//                      Announces the new presenter; every peer updates
//                      its local presenterMachineId on receipt.
//                      Carries `reason` so the host's special-privilege
//                      take-over (post-30s silence) is distinguishable
//                      from a normal user-driven transfer in the log.
//
//   hatRejected      — host → presenter (the requester of a transfer),
//                      sent when the target peer of an outgoing transfer
//                      is no longer connected (race against disconnect).
//                      Presenter UI surfaces a "Bob disconnected" toast
//                      and keeps the hat.
//
// All three pieces stay pure JSON helpers so SessionMode.h remains the
// single header-only home for Show-mode wire shapes, and the unit test
// validates the entire vocabulary without dragging in LanLiveSession.

inline QJsonObject encodeRequestHatJson(const QString &machineId,
                                        const QString &displayName) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("requestHat"));
    o.insert(QStringLiteral("machineId"), machineId);
    o.insert(QStringLiteral("displayName"), displayName);
    return o;
}

inline void decodeRequestHatJson(const QJsonObject &obj,
                                 QString *outMachineId,
                                 QString *outDisplayName) {
    if (outMachineId)
        *outMachineId = obj.value(QStringLiteral("machineId")).toString();
    if (outDisplayName)
        *outDisplayName = obj.value(QStringLiteral("displayName")).toString();
}

inline QJsonObject encodeHatTransferredJson(const QString &newPresenterMachineId,
                                            const QString &newPresenterDisplayName,
                                            const QString &reason) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("hatTransferred"));
    o.insert(QStringLiteral("newPresenterMachineId"), newPresenterMachineId);
    o.insert(QStringLiteral("newPresenterDisplayName"), newPresenterDisplayName);
    // Defaults to "transfer" (normal user-driven) when caller passes
    // empty so legacy frames stay forward-compatible.
    o.insert(QStringLiteral("reason"),
             reason.isEmpty() ? QStringLiteral("transfer") : reason);
    return o;
}

inline void decodeHatTransferredJson(const QJsonObject &obj,
                                     QString *outNewPresenterMachineId,
                                     QString *outNewPresenterDisplayName,
                                     QString *outReason) {
    if (outNewPresenterMachineId)
        *outNewPresenterMachineId =
            obj.value(QStringLiteral("newPresenterMachineId")).toString();
    if (outNewPresenterDisplayName)
        *outNewPresenterDisplayName =
            obj.value(QStringLiteral("newPresenterDisplayName")).toString();
    if (outReason) {
        const QString r = obj.value(QStringLiteral("reason")).toString();
        *outReason = r.isEmpty() ? QStringLiteral("transfer") : r;
    }
}

inline QJsonObject encodeHatRejectedJson(const QString &reason) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("hatRejected"));
    o.insert(QStringLiteral("reason"), reason);
    return o;
}

/// Presenter-side yield: "I'm done, hat goes back to the host."
/// Empty payload — the server (host) infers the sender from the
/// peer-link's machineId, validates it matches the current presenter,
/// then broadcasts a regular `hatTransferred(host, reason="yield")`.
inline QJsonObject encodeYieldHatJson() {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("yieldHat"));
    return o;
}

inline void decodeHatRejectedJson(const QJsonObject &obj,
                                  QString *outReason) {
    if (outReason)
        *outReason = obj.value(QStringLiteral("reason")).toString();
}

// -----------------------------------------------------------------------
// Follow-the-host view-state frame (Phase 9.9f §15.2 — added 2026-05-21)
// -----------------------------------------------------------------------
//
// Presenter → all-viewers broadcast carrying transient UI state:
// viewport (scroll/zoom), per-track visibility, per-channel visibility.
// Sent only by the current presenter, in Show mode, throttled to one
// frame per kViewStateThrottleMs at the LanLiveSession layer.
//
// Not part of the MIDI file — visibility flags and viewport are
// editor-side state, never written to .mid. Viewers apply the frame
// SILENTLY (no Protocol step), so a hat-pass leaves the new presenter's
// own undo history untouched by the previous presenter's view choices.

constexpr int kViewStateThrottleMs = 250;

/// Viewport rectangle on the wire. Coords match MatrixWidget's
/// scrollChanged signal payload (ms for horizontal, line number for
/// vertical). The viewer reverses the conversion via scrollXChanged /
/// scrollYChanged on receipt.
///
/// \a scaleX / \a scaleY are the presenter's zoom factors. Without
/// them, a viewer with a different visible-line count would have its
/// scrollYChanged() clamp the requested startLine to 0 ("snap to
/// top") because endLineY would exceed NUM_LINES. Sending the zoom
/// lets the viewer match the presenter's effective viewport first.
/// Defaults of 1.0 represent the editor's reset zoom level — same
/// value used by zoomStd() / resetView().
struct ViewportState {
    int    startMs    = 0;
    int    maxMs      = 0;
    int    startLine  = 0;
    int    maxLine    = 0;
    double scaleX     = 1.0;
    double scaleY     = 1.0;
    /// Display name of the presenter's active editing tool — e.g.
    /// "Select Events (Box)", "Place note (Pencil)", etc. Picked from
    /// the tool's toolTip text so the wire never carries a brittle
    /// class-name or numeric ID. Empty when no tool is selected (rare;
    /// usually a Standard tool is bound at all times).
    QString activeToolName;
    /// Presenter's edit-cursor tick. -1 means "no cursor" (file
    /// hasn't loaded yet, or the cursor was never moved on the host).
    int    cursorTick  = -1;
    /// Fit-to-focus extents (2026-05-21 — Sven's report). When both
    /// of these are non-negative, the viewer ignores startMs/startLine
    /// as scroll positions and instead fits its OWN viewport so that
    /// the rectangle [startMs..focusEndMs × startLine..focusEndLine]
    /// is fully visible (with a small padding). This sidesteps the
    /// clamp-to-zero problem that 1:1 scroll mirroring hit whenever
    /// the viewer's window/zoom differed from the presenter's.
    /// Defaults of -1 keep the wire backward-compatible with the
    /// initial v1.7.2 builds — those frames still decode cleanly and
    /// the viewer falls back to old position-based behaviour.
    int    focusEndMs   = -1;
    int    focusEndLine = -1;

    /// Presenter's selected-event identity tuples (2026-05-21 — Sven's
    /// follow-up request). Each entry encodes (tick, channel, line,
    /// type) — enough to disambiguate within a single channel-tick.
    /// Empty when nothing is selected on the presenter side; viewer
    /// then clears its own selection mirror. Receiver matches tuples
    /// against the local file's events and applies the result via
    /// Selection::setSelectionSilent (no undo pollution).
    struct SelectedEventId {
        int     tick    = 0;
        int     channel = 0;
        int     line    = 0;
        QString type;
    };
    QVector<SelectedEventId> selectedEvents;
};

/// Build the viewState frame. \a trackVisible[i] = true iff track i is
/// visible; same for channelVisible. The arrays are dense — receiver
/// iterates with index = position. Out-of-range entries are ignored
/// silently (legacy track/channel layouts).
inline QJsonObject encodeViewStateJson(const QString &senderMachineId,
                                       const ViewportState &vp,
                                       const QVector<bool> &trackVisible,
                                       const QVector<bool> &channelVisible) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("viewState"));
    o.insert(QStringLiteral("sender"), senderMachineId);

    QJsonObject viewport;
    viewport.insert(QStringLiteral("startMs"),    vp.startMs);
    viewport.insert(QStringLiteral("maxMs"),      vp.maxMs);
    viewport.insert(QStringLiteral("startLine"),  vp.startLine);
    viewport.insert(QStringLiteral("maxLine"),    vp.maxLine);
    viewport.insert(QStringLiteral("scaleX"),     vp.scaleX);
    viewport.insert(QStringLiteral("scaleY"),     vp.scaleY);
    viewport.insert(QStringLiteral("cursorTick"), vp.cursorTick);
    if (vp.focusEndMs   >= 0) viewport.insert(QStringLiteral("focusEndMs"),   vp.focusEndMs);
    if (vp.focusEndLine >= 0) viewport.insert(QStringLiteral("focusEndLine"), vp.focusEndLine);
    if (!vp.activeToolName.isEmpty())
        viewport.insert(QStringLiteral("activeTool"), vp.activeToolName);
    // selectedEvents: omitted entirely when empty (saves wire bytes
    // on every frame in the common case where nothing is selected).
    if (!vp.selectedEvents.isEmpty()) {
        QJsonArray sel;
        for (const auto &id : vp.selectedEvents) {
            QJsonObject entry;
            entry.insert(QStringLiteral("tick"),    id.tick);
            entry.insert(QStringLiteral("channel"), id.channel);
            entry.insert(QStringLiteral("line"),    id.line);
            entry.insert(QStringLiteral("type"),    id.type);
            sel.append(entry);
        }
        viewport.insert(QStringLiteral("selectedEvents"), sel);
    }
    o.insert(QStringLiteral("viewport"), viewport);

    QJsonArray tracks;
    for (bool v : trackVisible) tracks.append(v);
    o.insert(QStringLiteral("trackVisibility"), tracks);

    QJsonArray channels;
    for (bool v : channelVisible) channels.append(v);
    o.insert(QStringLiteral("channelVisibility"), channels);

    return o;
}

inline void decodeViewStateJson(const QJsonObject &obj,
                                QString *outSenderMachineId,
                                ViewportState *outViewport,
                                QVector<bool> *outTrackVisible,
                                QVector<bool> *outChannelVisible) {
    if (outSenderMachineId)
        *outSenderMachineId = obj.value(QStringLiteral("sender")).toString();
    if (outViewport) {
        QJsonObject vp = obj.value(QStringLiteral("viewport")).toObject();
        outViewport->startMs   = vp.value(QStringLiteral("startMs")).toInt();
        outViewport->maxMs     = vp.value(QStringLiteral("maxMs")).toInt();
        outViewport->startLine = vp.value(QStringLiteral("startLine")).toInt();
        outViewport->maxLine   = vp.value(QStringLiteral("maxLine")).toInt();
        // scaleX/scaleY: default to 1.0 when the field is absent, which
        // happens for a legacy host (or our own v1.7.2 builds shipped
        // BEFORE this fix landed). 1.0 = no zoom, same as zoomStd().
        QJsonValue sx = vp.value(QStringLiteral("scaleX"));
        QJsonValue sy = vp.value(QStringLiteral("scaleY"));
        outViewport->scaleX = sx.isDouble() ? sx.toDouble() : 1.0;
        outViewport->scaleY = sy.isDouble() ? sy.toDouble() : 1.0;
        // cursorTick: -1 sentinel when absent (legacy frame from a
        // build that pre-dates 2026-05-21).
        QJsonValue ct = vp.value(QStringLiteral("cursorTick"));
        outViewport->cursorTick = ct.isUndefined() ? -1 : ct.toInt(-1);
        outViewport->activeToolName =
            vp.value(QStringLiteral("activeTool")).toString();
        // Fit-to-focus extents — -1 means "absent" (legacy initial
        // v1.7.2 frames). When set, viewer uses fit-to-focus instead
        // of 1:1 scroll mirroring.
        QJsonValue fEnd  = vp.value(QStringLiteral("focusEndMs"));
        QJsonValue fLine = vp.value(QStringLiteral("focusEndLine"));
        outViewport->focusEndMs   = fEnd.isUndefined()  ? -1 : fEnd.toInt(-1);
        outViewport->focusEndLine = fLine.isUndefined() ? -1 : fLine.toInt(-1);
        // selectedEvents: array absent or empty → outViewport->selectedEvents
        // stays empty (viewer interprets as "clear selection").
        outViewport->selectedEvents.clear();
        QJsonArray sel = vp.value(QStringLiteral("selectedEvents")).toArray();
        outViewport->selectedEvents.reserve(sel.size());
        for (const QJsonValue &v : sel) {
            QJsonObject e = v.toObject();
            ViewportState::SelectedEventId id;
            id.tick    = e.value(QStringLiteral("tick")).toInt();
            id.channel = e.value(QStringLiteral("channel")).toInt();
            id.line    = e.value(QStringLiteral("line")).toInt();
            id.type    = e.value(QStringLiteral("type")).toString();
            outViewport->selectedEvents.append(id);
        }
    }
    if (outTrackVisible) {
        outTrackVisible->clear();
        for (const QJsonValue &v :
             obj.value(QStringLiteral("trackVisibility")).toArray()) {
            outTrackVisible->append(v.toBool());
        }
    }
    if (outChannelVisible) {
        outChannelVisible->clear();
        for (const QJsonValue &v :
             obj.value(QStringLiteral("channelVisibility")).toArray()) {
            outChannelVisible->append(v.toBool());
        }
    }
}

// -----------------------------------------------------------------------
// Chat side-channel wire frame (Phase 9.11 §15.3)
// -----------------------------------------------------------------------
//
// Lightweight in-session chat. Travels over the same WebRTC data channel
// (WAN) or LAN TCP socket as MIDI hunks and heartbeats. Host re-broadcasts
// to all other peers; the sender already appended optimistically locally
// so they don't echo their own message back from the network.
//
// Soft caps enforced at the host:
//   - 4 KB per text payload
//   - 1 message per 200 ms per sender machineId

constexpr int kChatTextMaxBytes      = 4 * 1024;
constexpr int kChatRateLimitMsPerSender = 200;

inline QJsonObject encodeChatJson(const QString &senderMachineId,
                                  const QString &displayName,
                                  const QString &text,
                                  qint64 timestampMs) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("chat"));
    o.insert(QStringLiteral("sender"), senderMachineId);
    o.insert(QStringLiteral("displayName"), displayName);
    o.insert(QStringLiteral("text"), text);
    // QJsonValue stores integers as double internally and loses precision
    // around 2^53; current Unix-ms is ~1.7e12 which is well within range,
    // but we use toVariant().toLongLong on the decode side regardless to
    // future-proof.
    o.insert(QStringLiteral("timestamp"), static_cast<double>(timestampMs));
    return o;
}

inline void decodeChatJson(const QJsonObject &obj,
                           QString *outSenderMachineId,
                           QString *outDisplayName,
                           QString *outText,
                           qint64 *outTimestampMs) {
    if (outSenderMachineId)
        *outSenderMachineId = obj.value(QStringLiteral("sender")).toString();
    if (outDisplayName)
        *outDisplayName = obj.value(QStringLiteral("displayName")).toString();
    if (outText)
        *outText = obj.value(QStringLiteral("text")).toString();
    if (outTimestampMs) {
        *outTimestampMs =
            obj.value(QStringLiteral("timestamp")).toVariant().toLongLong();
    }
}

// -----------------------------------------------------------------------
// Playback trigger frame (Show Mode follow-the-host extension)
// -----------------------------------------------------------------------
//
// Presenter pressed Play or Stop → broadcast to every viewer so they
// hear the playback in their own editor. Carries the cursor tick at
// the moment of the trigger so the viewer can `setCursorTick(tick)`
// before calling MidiPlayer::play(). No clock-sync attempt — first-
// frame latency (50-200 ms typical) and audio-clock drift over long
// playbacks are accepted as "good enough for tutorials and demos";
// continuous re-sync is deliberately out of scope.

// -----------------------------------------------------------------------
// Mid-session mode switch (Edit ↔ Show)
// -----------------------------------------------------------------------
//
// Host-only broadcast that flips the session mode without leaving and
// re-starting. Wire shape mirrors sessionWelcome on purpose — same
// `{mode, presenterMachineId}` payload — but the type tag is distinct
// so the joiner-side handler can apply it without resetting the
// already-established session state.
//
// Authorization: only the host's local code path should send this
// (LanLiveSession::switchSessionMode does the host-role check). No
// server-side accept handler — peers can't initiate.

inline QJsonObject encodeSessionModeSwitchJson(Mode newMode,
                                               const QString &presenterMachineId) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("sessionModeSwitch"));
    o.insert(QStringLiteral("mode"), QString::fromLatin1(modeToWire(newMode)));
    o.insert(QStringLiteral("presenterMachineId"), presenterMachineId);
    return o;
}

inline void decodeSessionModeSwitchJson(const QJsonObject &obj,
                                        Mode *outMode,
                                        QString *outPresenterMachineId) {
    if (outMode)
        *outMode = modeFromWire(obj.value(QStringLiteral("mode")).toString());
    if (outPresenterMachineId)
        *outPresenterMachineId =
            obj.value(QStringLiteral("presenterMachineId")).toString();
}

inline QJsonObject encodePlaybackJson(const QString &action,
                                      int tickPosition) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("playback"));
    o.insert(QStringLiteral("action"), action);
    o.insert(QStringLiteral("tickPosition"), tickPosition);
    return o;
}

inline void decodePlaybackJson(const QJsonObject &obj,
                               QString *outAction,
                               int *outTickPosition) {
    if (outAction)
        *outAction = obj.value(QStringLiteral("action")).toString();
    if (outTickPosition)
        *outTickPosition = obj.value(QStringLiteral("tickPosition")).toInt(-1);
}

}  // namespace LiveSession

#endif  // SESSIONMODE_H
