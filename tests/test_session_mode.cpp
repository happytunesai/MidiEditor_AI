/*
 * MidiEditor AI — Unit test for LiveSession::Mode helpers (Phase 9.9 §15.2)
 *
 * Tests the pure wire-format helpers in src/collab/SessionMode.h:
 *   - modeToWire / modeFromWire (round-trip + case-insensitive +
 *     unknown-value fallback)
 *   - encodeWelcomeJson / decodeWelcomeJson (full JSON round-trip,
 *     legacy-host fallback, type field presence)
 *
 * Header-only module → test executable links nothing but Qt::Core/Test.
 * No QSettings, no MidiFile, no network mocks.
 */

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtTest/QtTest>

#include "../src/collab/SessionMode.h"

class SessionModeTest : public QObject {
    Q_OBJECT
private slots:
    // ---- enum <-> wire string ---------------------------------------

    void wireString_edit() {
        QCOMPARE(QString::fromLatin1(LiveSession::modeToWire(LiveSession::Mode::Edit)),
                 QStringLiteral("edit"));
    }

    void wireString_show() {
        QCOMPARE(QString::fromLatin1(LiveSession::modeToWire(LiveSession::Mode::Show)),
                 QStringLiteral("show"));
    }

    void wireString_roundTrip() {
        for (auto m : {LiveSession::Mode::Edit, LiveSession::Mode::Show}) {
            QString wire = QString::fromLatin1(LiveSession::modeToWire(m));
            QCOMPARE(LiveSession::modeFromWire(wire), m);
        }
    }

    void wireString_caseInsensitive() {
        // The wire emits lowercase, but a tolerant parser handles any
        // casing so we don't bite on a future host that ships "Show".
        QCOMPARE(LiveSession::modeFromWire(QStringLiteral("SHOW")),
                 LiveSession::Mode::Show);
        QCOMPARE(LiveSession::modeFromWire(QStringLiteral("Show")),
                 LiveSession::Mode::Show);
        QCOMPARE(LiveSession::modeFromWire(QStringLiteral("show")),
                 LiveSession::Mode::Show);
    }

    void wireString_unknownFallsBackToEdit() {
        // Forward-compatibility: an unknown future value (e.g. "review",
        // "lockstep") must read as Edit so the joiner stays in a known-
        // good state instead of crashing or undefined-behaving.
        QCOMPARE(LiveSession::modeFromWire(QStringLiteral("review")),
                 LiveSession::Mode::Edit);
        QCOMPARE(LiveSession::modeFromWire(QStringLiteral("")),
                 LiveSession::Mode::Edit);
        QCOMPARE(LiveSession::modeFromWire(QStringLiteral("EDIT")),
                 LiveSession::Mode::Edit);
    }

    // ---- welcome JSON ------------------------------------------------

    void welcomeJson_typeFieldPresent() {
        QJsonObject o = LiveSession::encodeWelcomeJson(
            LiveSession::Mode::Show,
            QStringLiteral("abc-123-def"));
        QCOMPARE(o.value(QStringLiteral("type")).toString(),
                 QStringLiteral("sessionWelcome"));
    }

    void welcomeJson_carriesMode() {
        QJsonObject editObj = LiveSession::encodeWelcomeJson(
            LiveSession::Mode::Edit, QStringLiteral("any"));
        QCOMPARE(editObj.value(QStringLiteral("mode")).toString(),
                 QStringLiteral("edit"));

        QJsonObject showObj = LiveSession::encodeWelcomeJson(
            LiveSession::Mode::Show, QStringLiteral("any"));
        QCOMPARE(showObj.value(QStringLiteral("mode")).toString(),
                 QStringLiteral("show"));
    }

    void welcomeJson_carriesPresenter() {
        const QString presenter = QStringLiteral("aa11bb22-cc33-dd44");
        QJsonObject o = LiveSession::encodeWelcomeJson(
            LiveSession::Mode::Show, presenter);
        QCOMPARE(o.value(QStringLiteral("presenterMachineId")).toString(), presenter);
    }

    void welcomeJson_emptyPresenterIsOk() {
        // Initial Edit-mode host that doesn't track a presenter (yet)
        // should be allowed to ship an empty presenter field without
        // confusing the joiner-side parser.
        QJsonObject o = LiveSession::encodeWelcomeJson(
            LiveSession::Mode::Edit, QString());
        LiveSession::Mode m = LiveSession::Mode::Show;
        QString p = QStringLiteral("should-be-cleared");
        LiveSession::decodeWelcomeJson(o, &m, &p);
        QCOMPARE(m, LiveSession::Mode::Edit);
        QCOMPARE(p, QString());
    }

    void welcomeJson_decodeRoundTrip() {
        // Build → serialise to bytes → parse back → decode. This is
        // the path the actual wire takes, so we want a single end-to-
        // end test that catches any silent type coercion in QJson.
        const QString presenter = QStringLiteral("11112222-3333-4444-aaaa-bbbbccccdddd");
        QJsonObject built = LiveSession::encodeWelcomeJson(
            LiveSession::Mode::Show, presenter);
        QByteArray bytes = QJsonDocument(built).toJson(QJsonDocument::Compact);

        QJsonDocument parsed = QJsonDocument::fromJson(bytes);
        QVERIFY(parsed.isObject());
        QJsonObject obj = parsed.object();

        LiveSession::Mode outMode = LiveSession::Mode::Edit;
        QString outPresenter;
        LiveSession::decodeWelcomeJson(obj, &outMode, &outPresenter);
        QCOMPARE(outMode, LiveSession::Mode::Show);
        QCOMPARE(outPresenter, presenter);
    }

    void welcomeJson_legacyHostMissingFields() {
        // A pre-9.9 host wouldn't ship the sessionWelcome at all. But
        // for defensive testing: if the welcome arrives without one of
        // its fields (e.g. an intermediate-build proxy stripped them),
        // the joiner must still land in a valid state. Edit + empty
        // presenter = the safest equivalent of "no Show mode active".
        QJsonObject empty;
        empty.insert(QStringLiteral("type"), QStringLiteral("sessionWelcome"));
        LiveSession::Mode outMode = LiveSession::Mode::Show;
        QString outPresenter = QStringLiteral("garbage-from-previous-state");
        LiveSession::decodeWelcomeJson(empty, &outMode, &outPresenter);
        QCOMPARE(outMode, LiveSession::Mode::Edit);
        QCOMPARE(outPresenter, QString());
    }

    void welcomeJson_nullOutPointersAreSafe() {
        // Defensive: the API spec says callers can pass null for either
        // out-pointer; verify the helper doesn't crash.
        QJsonObject o = LiveSession::encodeWelcomeJson(
            LiveSession::Mode::Show, QStringLiteral("xyz"));
        LiveSession::decodeWelcomeJson(o, nullptr, nullptr, nullptr);
        // No crash = pass.
        QVERIFY(true);
    }

    // ---- v1.7.2 §15.4: appVersion in sessionWelcome -----------------

    void welcomeJson_appVersionPresentWhenSupplied() {
        QJsonObject o = LiveSession::encodeWelcomeJson(
            LiveSession::Mode::Edit,
            QStringLiteral("host-mid"),
            QStringLiteral("1.7.2"));
        QCOMPARE(o.value(QStringLiteral("appVersion")).toString(),
                 QStringLiteral("1.7.2"));
    }

    void welcomeJson_appVersionOmittedWhenEmpty() {
        // Caller passing an empty version (e.g. dev build without the
        // compile def) should produce a frame without the field, NOT a
        // field with an empty string. Receivers treat absence as
        // "pre-1.7.2"; that's the right legacy signal for unbuilt or
        // misconfigured peers too.
        QJsonObject o = LiveSession::encodeWelcomeJson(
            LiveSession::Mode::Edit,
            QStringLiteral("host-mid"),
            QString());
        QVERIFY(!o.contains(QStringLiteral("appVersion")));
    }

    void welcomeJson_appVersionRoundTrip() {
        QJsonObject o = LiveSession::encodeWelcomeJson(
            LiveSession::Mode::Show,
            QStringLiteral("host-mid"),
            QStringLiteral("1.8.0-beta"));
        QString ver;
        LiveSession::decodeWelcomeJson(o, nullptr, nullptr, &ver);
        QCOMPARE(ver, QStringLiteral("1.8.0-beta"));
    }

    void welcomeJson_legacyWelcomeDecodesAsEmptyVersion() {
        // A pre-1.7.2 host's hypothetical sessionWelcome (it doesn't
        // actually ship the frame at all in that era, but we want the
        // decoder to also handle "1.7.2 wire format with no appVersion
        // field" cleanly — important if a v1.7.x.y intermediate build
        // ever lands).
        QJsonObject obj;
        obj.insert(QStringLiteral("type"), QStringLiteral("sessionWelcome"));
        obj.insert(QStringLiteral("mode"), QStringLiteral("edit"));
        obj.insert(QStringLiteral("presenterMachineId"), QStringLiteral("x"));
        // No appVersion field.
        QString ver = QStringLiteral("stale-from-previous-state");
        LiveSession::decodeWelcomeJson(obj, nullptr, nullptr, &ver);
        QCOMPARE(ver, QString());
    }

    // ---- requestHat (Phase 9.9b) ------------------------------------

    void requestHatJson_typeAndFields() {
        QJsonObject o = LiveSession::encodeRequestHatJson(
            QStringLiteral("abc-mid"), QStringLiteral("Alice"));
        QCOMPARE(o.value(QStringLiteral("type")).toString(),
                 QStringLiteral("requestHat"));
        QCOMPARE(o.value(QStringLiteral("machineId")).toString(),
                 QStringLiteral("abc-mid"));
        QCOMPARE(o.value(QStringLiteral("displayName")).toString(),
                 QStringLiteral("Alice"));
    }

    void requestHatJson_decodeRoundTrip() {
        QJsonObject o = LiveSession::encodeRequestHatJson(
            QStringLiteral("xyz-mid"), QStringLiteral("Bob"));
        QString mid, name;
        LiveSession::decodeRequestHatJson(o, &mid, &name);
        QCOMPARE(mid, QStringLiteral("xyz-mid"));
        QCOMPARE(name, QStringLiteral("Bob"));
    }

    // ---- hatTransferred (Phase 9.9b) --------------------------------

    void hatTransferredJson_typeAndFields() {
        QJsonObject o = LiveSession::encodeHatTransferredJson(
            QStringLiteral("new-mid"),
            QStringLiteral("Carol"),
            QStringLiteral("transfer"));
        QCOMPARE(o.value(QStringLiteral("type")).toString(),
                 QStringLiteral("hatTransferred"));
        QCOMPARE(o.value(QStringLiteral("newPresenterMachineId")).toString(),
                 QStringLiteral("new-mid"));
        QCOMPARE(o.value(QStringLiteral("newPresenterDisplayName")).toString(),
                 QStringLiteral("Carol"));
        QCOMPARE(o.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("transfer"));
    }

    void hatTransferredJson_emptyReasonDefaultsToTransfer() {
        // Caller convenience: passing empty reason should produce the
        // canonical "transfer" wire value so legacy receivers don't
        // see an empty string.
        QJsonObject o = LiveSession::encodeHatTransferredJson(
            QStringLiteral("m"), QStringLiteral("n"), QString());
        QCOMPARE(o.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("transfer"));
    }

    void hatTransferredJson_hostTakeoverReason() {
        QJsonObject o = LiveSession::encodeHatTransferredJson(
            QStringLiteral("host-mid"),
            QStringLiteral("Host"),
            QStringLiteral("host-takeover"));
        QCOMPARE(o.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("host-takeover"));
    }

    void hatTransferredJson_decodeRoundTrip() {
        QJsonObject o = LiveSession::encodeHatTransferredJson(
            QStringLiteral("nm"),
            QStringLiteral("Dora"),
            QStringLiteral("host-takeover"));
        QString mid, name, reason;
        LiveSession::decodeHatTransferredJson(o, &mid, &name, &reason);
        QCOMPARE(mid, QStringLiteral("nm"));
        QCOMPARE(name, QStringLiteral("Dora"));
        QCOMPARE(reason, QStringLiteral("host-takeover"));
    }

    void hatTransferredJson_decodeMissingReason() {
        // A legacy host that doesn't carry the reason field should be
        // decoded as a normal "transfer", not as empty.
        QJsonObject obj;
        obj.insert(QStringLiteral("type"), QStringLiteral("hatTransferred"));
        obj.insert(QStringLiteral("newPresenterMachineId"), QStringLiteral("a"));
        obj.insert(QStringLiteral("newPresenterDisplayName"), QStringLiteral("b"));
        // No "reason" field.
        QString reason = QStringLiteral("dirty");
        LiveSession::decodeHatTransferredJson(obj, nullptr, nullptr, &reason);
        QCOMPARE(reason, QStringLiteral("transfer"));
    }

    // ---- hatRejected (Phase 9.9b) -----------------------------------

    void hatRejectedJson_typeAndReason() {
        QJsonObject o = LiveSession::encodeHatRejectedJson(
            QStringLiteral("Bob disconnected"));
        QCOMPARE(o.value(QStringLiteral("type")).toString(),
                 QStringLiteral("hatRejected"));
        QCOMPARE(o.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("Bob disconnected"));
    }

    void hatRejectedJson_decodeRoundTrip() {
        QJsonObject o = LiveSession::encodeHatRejectedJson(
            QStringLiteral("Some reason"));
        QString reason;
        LiveSession::decodeHatRejectedJson(o, &reason);
        QCOMPARE(reason, QStringLiteral("Some reason"));
    }

    void yieldHatJson_typeOnlyPayload() {
        // yieldHat is empty-payload by design — the server (host)
        // infers the yielder from the peer-link machineId. Just verify
        // the type marker is there.
        QJsonObject o = LiveSession::encodeYieldHatJson();
        QCOMPARE(o.value(QStringLiteral("type")).toString(),
                 QStringLiteral("yieldHat"));
        QCOMPARE(o.size(), 1);  // only the type field
    }

    // ---- Chat frame (Phase 9.11a §15.3) -----------------------------

    void chatJson_typeAndFields() {
        QJsonObject o = LiveSession::encodeChatJson(
            QStringLiteral("alice-mid"),
            QStringLiteral("Alice"),
            QStringLiteral("Hi everyone"),
            1715693472000LL);
        QCOMPARE(o.value(QStringLiteral("type")).toString(),
                 QStringLiteral("chat"));
        QCOMPARE(o.value(QStringLiteral("sender")).toString(),
                 QStringLiteral("alice-mid"));
        QCOMPARE(o.value(QStringLiteral("displayName")).toString(),
                 QStringLiteral("Alice"));
        QCOMPARE(o.value(QStringLiteral("text")).toString(),
                 QStringLiteral("Hi everyone"));
        QCOMPARE(o.value(QStringLiteral("timestamp")).toVariant().toLongLong(),
                 1715693472000LL);
    }

    void chatJson_decodeRoundTrip() {
        QJsonObject o = LiveSession::encodeChatJson(
            QStringLiteral("bob-mid"),
            QStringLiteral("Bob"),
            QStringLiteral("Track 3 könnte 5 dB leiser"),
            1715693999LL);
        QString sender, name, text;
        qint64 ts = 0;
        LiveSession::decodeChatJson(o, &sender, &name, &text, &ts);
        QCOMPARE(sender, QStringLiteral("bob-mid"));
        QCOMPARE(name, QStringLiteral("Bob"));
        QCOMPARE(text, QStringLiteral("Track 3 könnte 5 dB leiser"));
        QCOMPARE(ts, 1715693999LL);
    }

    void chatJson_preservesUtf8MultibyteText() {
        // Plain UTF-8 means German Umlaute, CJK, emoji, etc. should all
        // round-trip verbatim. QJsonValue stores QString internally so
        // there's no encoding step here; this test is a regression
        // belt-and-braces in case the helpers are ever ported away
        // from QJson.
        const QString utf8 = QStringLiteral("こんにちは 🎩 ñ ä");
        QJsonObject o = LiveSession::encodeChatJson(
            QStringLiteral("m"), QStringLiteral("n"), utf8, 0);
        QString text;
        LiveSession::decodeChatJson(o, nullptr, nullptr, &text, nullptr);
        QCOMPARE(text, utf8);
    }

    void chatJson_wireSerializationRoundTrip() {
        // Build → toJson(Compact) → fromJson → decode. Same end-to-end
        // pattern as welcomeJson_decodeRoundTrip.
        QJsonObject built = LiveSession::encodeChatJson(
            QStringLiteral("xyz-mid"),
            QStringLiteral("Carol"),
            QStringLiteral("multi\nline\ntext"),
            1715694000123LL);
        QByteArray bytes = QJsonDocument(built).toJson(QJsonDocument::Compact);
        QJsonDocument parsed = QJsonDocument::fromJson(bytes);
        QVERIFY(parsed.isObject());
        QString sender, name, text;
        qint64 ts = 0;
        LiveSession::decodeChatJson(parsed.object(), &sender, &name, &text, &ts);
        QCOMPARE(sender, QStringLiteral("xyz-mid"));
        QCOMPARE(name, QStringLiteral("Carol"));
        QCOMPARE(text, QStringLiteral("multi\nline\ntext"));
        QCOMPARE(ts, 1715694000123LL);
    }

    void chatJson_constants() {
        // Make sure the spec'd caps are visible at compile time so a
        // future maintainer can grep for the constants and find a
        // single source of truth.
        QCOMPARE(LiveSession::kChatTextMaxBytes, 4 * 1024);
        QCOMPARE(LiveSession::kChatRateLimitMsPerSender, 200);
    }

    // ---- viewState frame (follow-the-host) --------------------------

    void viewStateJson_typeAndSender() {
        LiveSession::ViewportState vp;
        vp.startMs = 100; vp.maxMs = 60000;
        vp.startLine = 40; vp.maxLine = 88;
        vp.scaleX = 1.5; vp.scaleY = 0.8;
        vp.focusEndMs = 5000; vp.focusEndLine = 70;
        vp.cursorTick = 1920;
        vp.activeToolName = QStringLiteral("Place note (Pencil)");
        QJsonObject o = LiveSession::encodeViewStateJson(
            QStringLiteral("presenter-mid"), vp, {true, false, true}, {true, true, false});
        QCOMPARE(o.value(QStringLiteral("type")).toString(),
                 QStringLiteral("viewState"));
        QCOMPARE(o.value(QStringLiteral("sender")).toString(),
                 QStringLiteral("presenter-mid"));
    }

    void viewStateJson_focusExtentsRoundTrip() {
        LiveSession::ViewportState vp;
        vp.startMs = 1000; vp.startLine = 30;
        vp.focusEndMs = 4000; vp.focusEndLine = 80;
        vp.scaleX = 2.0; vp.scaleY = 1.5;
        QJsonObject o = LiveSession::encodeViewStateJson(
            QStringLiteral("m"), vp, {}, {});
        LiveSession::ViewportState decoded;
        QString sender;
        QVector<bool> tracks, channels;
        LiveSession::decodeViewStateJson(o, &sender, &decoded, &tracks, &channels);
        QCOMPARE(decoded.startMs,    1000);
        QCOMPARE(decoded.startLine,  30);
        QCOMPARE(decoded.focusEndMs,   4000);
        QCOMPARE(decoded.focusEndLine, 80);
        QCOMPARE(decoded.scaleX,     2.0);
        QCOMPARE(decoded.scaleY,     1.5);
    }

    void viewStateJson_legacyAbsentFieldsDefault() {
        // Legacy v1.7.2-dev frame without focusEndMs / focusEndLine /
        // cursorTick / scaleX / scaleY — decoder must produce safe
        // defaults so the viewer falls back to old 1:1 mirror behaviour.
        QJsonObject obj;
        obj.insert(QStringLiteral("type"), QStringLiteral("viewState"));
        obj.insert(QStringLiteral("sender"), QStringLiteral("legacy-host"));
        QJsonObject vp;
        vp.insert(QStringLiteral("startMs"), 0);
        vp.insert(QStringLiteral("startLine"), 50);
        obj.insert(QStringLiteral("viewport"), vp);
        // No tracks/channels arrays, no focus, no cursor, no scale.
        LiveSession::ViewportState decoded;
        QString sender;
        QVector<bool> tracks, channels;
        LiveSession::decodeViewStateJson(obj, &sender, &decoded, &tracks, &channels);
        QCOMPARE(decoded.focusEndMs,   -1);  // absent → -1
        QCOMPARE(decoded.focusEndLine, -1);
        QCOMPARE(decoded.cursorTick,   -1);
        QCOMPARE(decoded.scaleX,       1.0);
        QCOMPARE(decoded.scaleY,       1.0);
    }

    void viewStateJson_selectionTupleRoundTrip() {
        LiveSession::ViewportState vp;
        LiveSession::ViewportState::SelectedEventId id1;
        id1.tick = 480; id1.channel = 0; id1.line = 60;
        id1.type = QStringLiteral("NoteOn");
        LiveSession::ViewportState::SelectedEventId id2;
        id2.tick = 960; id2.channel = 1; id2.line = 64;
        id2.type = QStringLiteral("NoteOn");
        vp.selectedEvents = {id1, id2};
        QJsonObject o = LiveSession::encodeViewStateJson(
            QStringLiteral("m"), vp, {}, {});
        LiveSession::ViewportState decoded;
        LiveSession::decodeViewStateJson(o, nullptr, &decoded, nullptr, nullptr);
        QCOMPARE(decoded.selectedEvents.size(), 2);
        QCOMPARE(decoded.selectedEvents[0].tick, 480);
        QCOMPARE(decoded.selectedEvents[1].type, QStringLiteral("NoteOn"));
    }

    void viewStateJson_emptySelectionOmitted() {
        // selectedEvents array is dropped from the JSON entirely when
        // empty — saves wire bytes on every frame in the common case.
        LiveSession::ViewportState vp;
        QJsonObject o = LiveSession::encodeViewStateJson(
            QStringLiteral("m"), vp, {}, {});
        QJsonObject vpObj = o.value(QStringLiteral("viewport")).toObject();
        QVERIFY(!vpObj.contains(QStringLiteral("selectedEvents")));
    }

    // ---- playback trigger frame (Show-mode follow-the-host) ---------

    void playbackJson_startShape() {
        QJsonObject o = LiveSession::encodePlaybackJson(
            QStringLiteral("start"), 1920);
        QCOMPARE(o.value(QStringLiteral("type")).toString(),
                 QStringLiteral("playback"));
        QCOMPARE(o.value(QStringLiteral("action")).toString(),
                 QStringLiteral("start"));
        QCOMPARE(o.value(QStringLiteral("tickPosition")).toInt(), 1920);
    }

    void playbackJson_stopShape() {
        QJsonObject o = LiveSession::encodePlaybackJson(
            QStringLiteral("stop"), 480);
        QCOMPARE(o.value(QStringLiteral("action")).toString(),
                 QStringLiteral("stop"));
        QCOMPARE(o.value(QStringLiteral("tickPosition")).toInt(), 480);
    }

    void playbackJson_decodeRoundTrip() {
        QJsonObject o = LiveSession::encodePlaybackJson(
            QStringLiteral("start"), 7680);
        QString action;
        int tick = 0;
        LiveSession::decodePlaybackJson(o, &action, &tick);
        QCOMPARE(action, QStringLiteral("start"));
        QCOMPARE(tick,    7680);
    }

    void playbackJson_decodeAbsentTickIsMinusOne() {
        // A malformed / legacy frame without tickPosition should not
        // crash — decode produces -1 sentinel so the viewer can skip
        // the setCursorTick step and just trigger play/stop.
        QJsonObject obj;
        obj.insert(QStringLiteral("type"), QStringLiteral("playback"));
        obj.insert(QStringLiteral("action"), QStringLiteral("stop"));
        QString action;
        int tick = 999;
        LiveSession::decodePlaybackJson(obj, &action, &tick);
        QCOMPARE(action, QStringLiteral("stop"));
        QCOMPARE(tick,   -1);
    }

    void playbackJson_nullOutPointersAreSafe() {
        QJsonObject o = LiveSession::encodePlaybackJson(
            QStringLiteral("start"), 100);
        LiveSession::decodePlaybackJson(o, nullptr, nullptr);
        QVERIFY(true);  // no crash
    }

    // ---- sessionModeSwitch (host-only mid-session toggle) -----------

    void sessionModeSwitchJson_typeAndFields() {
        QJsonObject o = LiveSession::encodeSessionModeSwitchJson(
            LiveSession::Mode::Show, QStringLiteral("host-mid"));
        QCOMPARE(o.value(QStringLiteral("type")).toString(),
                 QStringLiteral("sessionModeSwitch"));
        QCOMPARE(o.value(QStringLiteral("mode")).toString(),
                 QStringLiteral("show"));
        QCOMPARE(o.value(QStringLiteral("presenterMachineId")).toString(),
                 QStringLiteral("host-mid"));
    }

    void sessionModeSwitchJson_editClearsPresenter() {
        QJsonObject o = LiveSession::encodeSessionModeSwitchJson(
            LiveSession::Mode::Edit, QString());
        QCOMPARE(o.value(QStringLiteral("mode")).toString(),
                 QStringLiteral("edit"));
        QCOMPARE(o.value(QStringLiteral("presenterMachineId")).toString(),
                 QString());
    }

    void sessionModeSwitchJson_decodeRoundTrip() {
        QJsonObject o = LiveSession::encodeSessionModeSwitchJson(
            LiveSession::Mode::Show, QStringLiteral("xyz"));
        LiveSession::Mode m = LiveSession::Mode::Edit;
        QString presenter;
        LiveSession::decodeSessionModeSwitchJson(o, &m, &presenter);
        QCOMPARE(m, LiveSession::Mode::Show);
        QCOMPARE(presenter, QStringLiteral("xyz"));
    }
};

QTEST_MAIN(SessionModeTest)
#include "test_session_mode.moc"
