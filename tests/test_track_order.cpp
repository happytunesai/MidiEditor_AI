/*
 * test_track_order
 *
 * Track order and the song-wide ("conductor") events - tempo, time and key
 * signatures, markers, cue points, copyright - track-order review (2.5.0):
 *
 *  1. TrackDropTarget: a dragged track lands where the drop indicator is
 *     drawn (above / below the row, or at the end) - TR-02.
 *  2. Moving the first track down hands the song-wide events to the new
 *     first track; the track keeps its own notes, program change, name and
 *     lyrics; undo and redo restore both order and ownership - TR-09.
 *  3. Moving another track into slot 0 hands them to that track - TR-09.
 *  4. Removing the first track keeps the complete tempo map, meter, key and
 *     markers (they go to the new first track); undo restores - TR-03.
 *  5. Removing any track keeps a song-wide event it happened to own - TR-03.
 *  6. Saving writes the song-wide events into the first chunk whatever track
 *     owns them; the reloaded file has each of them exactly once - TR-09.
 *  7. An export that skips muted tracks keeps the tempo map of a muted first
 *     track - TR-07.
 *  8. Loading: a tempo map in a later chunk no longer gets a default 120 BPM
 *     and 4/4 next to it, a file without one gets the defaults exactly once,
 *     and song-wide events from any chunk end up in the first track - TR-04.
 *
 * Harness: the REAL MidiFile/MidiChannel/MidiTrack/Protocol/MidiEvent stack;
 * only the GUI periphery is ODR-shimmed (Appearance colors, EventWidget),
 * same approach as test_ffxiv_fixer_resync.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>

#include "../src/gui/TrackDropTarget.h"
#include "../src/midi/MidiFile.h"
#include "../src/midi/MidiChannel.h"
#include "../src/midi/MidiTrack.h"
#include "../src/protocol/Protocol.h"
#include "../src/MidiEvent/KeySignatureEvent.h"
#include "../src/MidiEvent/MidiEvent.h"
#include "../src/MidiEvent/NoteOnEvent.h"
#include "../src/MidiEvent/OffEvent.h"
#include "../src/MidiEvent/ProgChangeEvent.h"
#include "../src/MidiEvent/TempoChangeEvent.h"
#include "../src/MidiEvent/TextEvent.h"
#include "../src/MidiEvent/TimeSignatureEvent.h"

// ---- ODR shims: Appearance colors (statics used by midi core / events) ---
#include "../src/gui/Appearance.h"
QColor Appearance::borderColor() { return QColor(); }
QColor *Appearance::channelColor(int) {
    static QColor c(128, 128, 128);
    return &c;
}
QColor *Appearance::trackColor(int) {
    static QColor c(128, 128, 128);
    return &c;
}

// ---- ODR shims: EventWidget ----------------------------------------------
#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}
QList<MidiEvent *> EventWidget::events() { return {}; }

// ==========================================================================

class TestTrackOrder : public QObject {
    Q_OBJECT

private:
    // Tracks: 0 "Piano" (conductor data + its own note, program, lyric),
    // 1 "Flute", 2 "Oboe" (a note each).
    struct Song {
        MidiFile *f = nullptr;
        MidiTrack *piano = nullptr, *flute = nullptr, *oboe = nullptr;
        NoteOnEvent *pianoNote = nullptr;
        ProgChangeEvent *pianoProgram = nullptr;
        TextEvent *pianoLyric = nullptr;
        QList<MidiEvent *> songWide; // every song-wide event of the file
    };

    static void place(MidiFile *f, MidiEvent *ev, int ch, int tick) {
        ev->setFile(f);
        f->channel(ch)->insertEvent(ev, tick, false);
    }

    static Song makeSong() {
        Song s;
        s.f = new MidiFile();
        MidiFile *f = s.f;
        f->protocol()->startNewAction("setup");
        f->addTrack();
        f->protocol()->endAction();
        s.piano = f->track(0);
        s.flute = f->track(1);
        s.oboe = f->track(2);
        s.piano->setName("Piano");
        s.flute->setName("Flute");
        s.oboe->setName("Oboe");
        s.piano->assignChannel(0);
        s.flute->assignChannel(1);
        s.oboe->assignChannel(2);

        // the default tempo and meter of a new file already sit on track 0
        for (int ch = 16; ch < 19; ++ch) {
            for (MidiEvent *ev : f->channel(ch)->eventMap()->values()) {
                if (MidiFile::isSongWideEvent(ev)) s.songWide << ev;
            }
        }
        auto *tempo = new TempoChangeEvent(17, 666667, s.piano);   // 90 BPM at bar 2
        place(f, tempo, 17, 1920);
        auto *meter = new TimeSignatureEvent(18, 3, 2, 24, 8, s.piano); // 3/4 at bar 3
        place(f, meter, 18, 3840);
        auto *key = new KeySignatureEvent(16, 2, false, s.piano);  // D major
        place(f, key, 16, 0);
        auto *marker = new TextEvent(16, s.piano);
        marker->setType(TextEvent::MARKER);
        marker->setText("Chorus");
        place(f, marker, 16, 960);
        auto *cue = new TextEvent(16, s.piano);
        cue->setType(TextEvent::COMMENT);
        cue->setText("Cue");
        place(f, cue, 16, 480);
        auto *copyright = new TextEvent(16, s.piano);
        copyright->setType(TextEvent::COPYRIGHT);
        copyright->setText("(c) test");
        place(f, copyright, 16, 0);
        s.songWide << tempo << meter << key << marker << cue << copyright;

        f->protocol()->startNewAction("notes");
        s.pianoNote = f->channel(0)->insertNote(60, 0, 400, 100, s.piano);
        f->channel(1)->insertNote(72, 0, 400, 100, s.flute);
        f->channel(2)->insertNote(64, 0, 400, 100, s.oboe);
        f->protocol()->endAction();
        s.pianoProgram = new ProgChangeEvent(0, 0, s.piano);
        place(f, s.pianoProgram, 0, 0);
        s.pianoLyric = new TextEvent(16, s.piano);
        s.pianoLyric->setType(TextEvent::LYRIK);
        s.pianoLyric->setText("la");
        place(f, s.pianoLyric, 16, 0);
        return s;
    }

    static bool allOwnedBy(const QList<MidiEvent *> &events, MidiTrack *track) {
        for (MidiEvent *ev : events) {
            if (ev->track() != track) return false;
        }
        return true;
    }

    static QList<int> tempos(MidiFile *f) {
        QList<int> out;
        QMultiMap<int, MidiEvent *> *map = f->channel(17)->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (auto *t = dynamic_cast<TempoChangeEvent *>(it.value()))
                out << it.key() << t->microsPerQuarter();
        }
        return out;
    }

    static QList<int> meters(MidiFile *f) {
        QList<int> out;
        QMultiMap<int, MidiEvent *> *map = f->channel(18)->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (auto *t = dynamic_cast<TimeSignatureEvent *>(it.value()))
                out << it.key() << t->num();
        }
        return out;
    }

    static int eventsOf(MidiFile *f, int ch, MidiTrack *track) {
        int n = 0;
        QMultiMap<int, MidiEvent *> *map = f->channel(ch)->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (it.value()->track() == track) ++n;
        }
        return n;
    }

    static QString tempPath() {
        QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/trackorder_XXXXXX.mid"));
        tmp.setAutoRemove(false);
        tmp.open();
        const QString path = tmp.fileName();
        tmp.close();
        return path;
    }

    // Meta event types (0x51 tempo, 0x58 meter, 0x59 key, 0x06 marker, ...) and
    // channel status nibbles (0x90 note-on, 0xC0 program) per chunk.
    static QList<QList<int>> chunkContents(const QString &path) {
        QList<QList<int>> out;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return out;
        const QByteArray b = file.readAll();
        auto u8 = [&](int i) { return static_cast<int>(static_cast<unsigned char>(b.at(i))); };
        int p = 14;
        while (p + 8 <= b.size()) {
            const int len = (u8(p + 4) << 24) | (u8(p + 5) << 16) | (u8(p + 6) << 8) | u8(p + 7);
            int q = p + 8;
            const int end = q + len;
            QList<int> kinds;
            int running = 0;
            auto varlen = [&]() {
                int v = 0;
                while (true) {
                    const int c = u8(q++);
                    v = (v << 7) | (c & 0x7F);
                    if (!(c & 0x80)) break;
                }
                return v;
            };
            while (q < end) {
                varlen();
                int st = u8(q);
                if (st < 0x80) st = running; else ++q;
                if (st == 0xFF) {
                    const int type = u8(q++);
                    const int l = varlen();
                    if (type != 0x2F) kinds << type;
                    q += l;
                    continue;
                }
                if (st == 0xF0 || st == 0xF7) { q += varlen(); continue; }
                running = st;
                const int hi = st & 0xF0;
                ++q;
                if (hi != 0xC0 && hi != 0xD0) ++q;
                kinds << hi;
            }
            out << kinds;
            p = end;
        }
        return out;
    }

    // Standard MIDI file (format 1, 480 tpq) from hand-written track bodies.
    static QByteArray smf(const QList<QByteArray> &bodies) {
        QByteArray out("MThd");
        out.append(QByteArray::fromHex("00000006" "0001"));
        out.append(char(0)).append(char(bodies.size()));
        out.append(QByteArray::fromHex("01E0"));
        for (const QByteArray &body : bodies) {
            const QByteArray data = body + QByteArray::fromHex("00FF2F00");
            out.append("MTrk");
            const quint32 len = static_cast<quint32>(data.size());
            out.append(char((len >> 24) & 0xFF)).append(char((len >> 16) & 0xFF))
               .append(char((len >> 8) & 0xFF)).append(char(len & 0xFF));
            out.append(data);
        }
        return out;
    }

    static MidiFile *load(const QByteArray &bytes) {
        const QString path = tempPath();
        QFile file(path);
        file.open(QIODevice::WriteOnly);
        file.write(bytes);
        file.close();
        bool ok = false;
        MidiFile *f = new MidiFile(path, &ok);
        QFile::remove(path);
        if (!ok) {
            delete f;
            return nullptr;
        }
        return f;
    }

private slots:

    // ---- TR-02: the drop lands at the indicator -------------------------------

    void dropTarget_followsTheIndicator() {
        using namespace TrackDropTarget;
        // four tracks
        QCOMPARE(finalIndex(2, 0, AboveItem, 4), 0);
        QCOMPARE(finalIndex(2, 0, BelowItem, 4), 1);  // right below the first track
        QCOMPARE(finalIndex(3, 1, AboveItem, 4), 1);  // moving up, line above row 1
        QCOMPARE(finalIndex(3, 1, BelowItem, 4), 2);  // moving up, line below row 1
        QCOMPARE(finalIndex(0, 2, AboveItem, 4), 1);  // moving down, line above row 2
        QCOMPARE(finalIndex(0, 2, BelowItem, 4), 2);  // moving down, line below row 2
        QCOMPARE(finalIndex(0, 3, BelowItem, 4), 3);
        QCOMPARE(finalIndex(0, -1, OnViewport, 4), 3); // empty space below the list
        QCOMPARE(finalIndex(3, 1, OnItem, 4), 1);      // treated as "above"
        QCOMPARE(finalIndex(1, 2, OnItem, 4), -1);     // "above the next row" = where it is
        // drops that do not move anything
        QCOMPARE(finalIndex(2, 2, AboveItem, 4), -1);
        QCOMPARE(finalIndex(2, 2, BelowItem, 4), -1);
        QCOMPARE(finalIndex(2, 3, AboveItem, 4), -1);
        QCOMPARE(finalIndex(3, -1, OnViewport, 4), -1);
        QCOMPARE(finalIndex(0, 0, BelowItem, 4), -1);
        QCOMPARE(finalIndex(5, 0, AboveItem, 4), -1);  // invalid source
    }

    // ---- TR-09: moving the first track ---------------------------------------

    void moveFirstTrackDown_handsSongWideDataToTheNewFirstTrack() {
        Song s = makeSong();
        MidiFile *f = s.f;
        QVERIFY(allOwnedBy(s.songWide, s.piano));

        f->protocol()->startNewAction("move");
        QVERIFY(f->moveTrack(s.piano, +1));
        f->protocol()->endAction();

        QCOMPARE(f->track(0), s.flute);
        QCOMPARE(f->track(1), s.piano);
        QVERIFY(allOwnedBy(s.songWide, s.flute));
        // the Piano keeps what belongs to it
        QCOMPARE(s.pianoNote->track(), s.piano);
        QCOMPARE(s.pianoProgram->track(), s.piano);
        QCOMPARE(s.pianoLyric->track(), s.piano);
        QCOMPARE(s.piano->name(), QString("Piano"));
        QCOMPARE(s.flute->name(), QString("Flute"));

        f->protocol()->undo();
        QCOMPARE(f->track(0), s.piano);
        QVERIFY(allOwnedBy(s.songWide, s.piano));
        f->protocol()->redo();
        QCOMPARE(f->track(0), s.flute);
        QVERIFY(allOwnedBy(s.songWide, s.flute));
        delete f;
    }

    void moveTrackIntoFirstSlot_takesTheSongWideData() {
        Song s = makeSong();
        MidiFile *f = s.f;
        f->protocol()->startNewAction("move");
        QVERIFY(f->moveTrack(s.oboe, -1));
        QVERIFY(f->moveTrack(s.oboe, -1));
        f->protocol()->endAction();
        QCOMPARE(f->track(0), s.oboe);
        QVERIFY(allOwnedBy(s.songWide, s.oboe));
        QCOMPARE(s.pianoNote->track(), s.piano);
        // a move that leaves slot 0 alone changes no ownership
        f->protocol()->startNewAction("move 2");
        QVERIFY(f->moveTrack(s.piano, +1));
        f->protocol()->endAction();
        QVERIFY(allOwnedBy(s.songWide, s.oboe));
        delete f;
    }

    // ---- TR-03: removing tracks keeps the song-wide data ----------------------

    void removeFirstTrack_keepsTempoMeterKeyAndMarkers() {
        Song s = makeSong();
        MidiFile *f = s.f;
        const QList<int> tempoBefore = tempos(f);
        const QList<int> meterBefore = meters(f);
        QCOMPARE(tempoBefore.size(), 4);   // 120 BPM at 0 + 90 BPM at 1920

        f->protocol()->startNewAction("remove");
        QVERIFY(f->removeTrack(s.piano));
        f->protocol()->endAction();

        QCOMPARE(f->track(0), s.flute);
        QCOMPARE(tempos(f), tempoBefore);
        QCOMPARE(meters(f), meterBefore);
        for (MidiEvent *ev : s.songWide) {
            QVERIFY(f->channel(ev->channel())->eventMap()->values().contains(ev));
        }
        QVERIFY(allOwnedBy(s.songWide, s.flute));
        QCOMPARE(eventsOf(f, 0, s.piano), 0);  // the Piano's own events went with it
        QVERIFY(!f->channel(16)->eventMap()->values().contains(s.pianoLyric));

        f->protocol()->undo();
        QCOMPARE(f->track(0), s.piano);
        QVERIFY(allOwnedBy(s.songWide, s.piano));
        QVERIFY(eventsOf(f, 0, s.piano) > 0);
        delete f;
    }

    void removeAnyTrack_keepsASongWideEventItOwned() {
        Song s = makeSong();
        MidiFile *f = s.f;
        // e.g. drawn with the tempo editor while the Oboe was the edit track
        auto *stray = new TempoChangeEvent(17, 400000, s.oboe);
        place(f, stray, 17, 2880);
        f->protocol()->startNewAction("remove");
        QVERIFY(f->removeTrack(s.oboe));
        f->protocol()->endAction();
        QVERIFY(f->channel(17)->eventMap()->values(2880).contains(stray));
        QCOMPARE(stray->track(), s.piano);
        delete f;
    }

    // ---- TR-09 / TR-07: saving ------------------------------------------------

    void save_songWideEventsGoIntoTheFirstChunk() {
        Song s = makeSong();
        MidiFile *f = s.f;
        f->protocol()->startNewAction("move");
        f->moveTrack(s.piano, +1);
        f->moveTrack(s.piano, +1);   // Piano last: Flute, Oboe, Piano
        f->protocol()->endAction();
        // a song-wide event on a later track is still saved in the first chunk
        auto *stray = new TempoChangeEvent(17, 400000, s.oboe);
        place(f, stray, 17, 2880);

        const QString path = tempPath();
        QVERIFY(f->save(path));
        const QList<QList<int>> chunks = chunkContents(path);
        QCOMPARE(chunks.size(), 3);
        QCOMPARE(chunks[0].count(0x51), 3);            // 120, 90 and the stray 150 BPM
        QCOMPARE(chunks[0].count(0x58), 2);            // 4/4 and 3/4
        QCOMPARE(chunks[0].count(0x59), 1);            // key
        QCOMPARE(chunks[0].count(0x06), 1);            // marker
        QCOMPARE(chunks[0].count(0x07), 1);            // cue point
        QCOMPARE(chunks[0].count(0x02), 1);            // copyright
        for (int c = 1; c < 3; ++c) {
            QCOMPARE(chunks[c].count(0x51) + chunks[c].count(0x58) + chunks[c].count(0x59)
                     + chunks[c].count(0x06) + chunks[c].count(0x07) + chunks[c].count(0x02), 0);
        }
        QVERIFY(chunks[2].contains(0x90) && chunks[2].contains(0xC0)); // the Piano's own events
        QVERIFY(chunks[2].contains(0x05));                              // its lyric

        bool ok = false;
        MidiFile *g = new MidiFile(path, &ok);
        QVERIFY(ok);
        QCOMPARE(tempos(g), (QList<int>{0, 500000, 1920, 666667, 2880, 400000}));
        QCOMPARE(meters(g), (QList<int>{0, 4, 3840, 3}));
        QCOMPARE(g->track(0)->name(), QString("Flute"));
        QCOMPARE(g->track(2)->name(), QString("Piano"));
        delete g;
        QFile::remove(path);
        delete f;
    }

    void save_skippingMutedTracks_keepsTheTempoMapOfAMutedFirstTrack() {
        Song s = makeSong();
        MidiFile *f = s.f;
        f->protocol()->startNewAction("mute");
        s.piano->setMuted(true);
        f->protocol()->endAction();
        const QString path = tempPath();
        QVERIFY(f->save(path, /*skipMutedTrackEvents=*/true));
        const QList<QList<int>> chunks = chunkContents(path);
        QCOMPARE(chunks[0].count(0x51), 2);    // the tempo map survives the mute...
        QVERIFY(!chunks[0].contains(0x90));    // ...the Piano's notes do not
        QFile::remove(path);
        delete f;
    }

    // ---- TR-04: loading --------------------------------------------------------

    void load_tempoMapInALaterChunk_noDefaultNextToIt() {
        const QByteArray note = QByteArray::fromHex("00 903C64 8360 803C00"); // 480 ticks
        const QByteArray body0 = QByteArray::fromHex("00 FF0305 506961 6E6F") + note; // "Piano"
        const QByteArray body1 = QByteArray::fromHex("00 FF5103 0A2C2B 00 FF5804 03021808"
                                                     " 00 FF5902 0200 00 FF0602 4368"); // 90 BPM, 3/4, D, marker
        MidiFile *f = load(smf({body0, body1}));
        QVERIFY(f);
        QCOMPARE(tempos(f), (QList<int>{0, 666667}));
        QCOMPARE(meters(f), (QList<int>{0, 3}));
        QCOMPARE(f->msOfTick(1920), 2666);   // four beats at 90 BPM
        for (int ch = 16; ch < 19; ++ch) {
            for (MidiEvent *ev : f->channel(ch)->eventMap()->values()) {
                if (MidiFile::isSongWideEvent(ev)) QCOMPARE(ev->track(), f->track(0));
            }
        }
        delete f;
    }

    void load_noTempoMap_defaultsExactlyOnce() {
        const QByteArray note = QByteArray::fromHex("00 903C64 8360 803C00");
        MidiFile *f = load(smf({note, note, note}));
        QVERIFY(f);
        QCOMPARE(tempos(f), (QList<int>{0, 500000}));
        QCOMPARE(meters(f), (QList<int>{0, 4}));
        QCOMPARE(f->channel(17)->eventMap()->first()->track(), f->track(0));
        delete f;
    }
};

QTEST_GUILESS_MAIN(TestTrackOrder)
#include "test_track_order.moc"
