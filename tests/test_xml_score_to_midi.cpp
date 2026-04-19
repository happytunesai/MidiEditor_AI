// Unit tests for XmlScoreToMidi — the shared SMF writer used by the
// MusicXmlImporter and MsczImporter.
//
// Run from a build configured with -DBUILD_TESTING=ON:
//     ctest --test-dir build -V
// or directly:
//     build/tests/test_xml_score_to_midi.exe

#include "MusicXmlModels.h"
#include "XmlScoreToMidi.h"

#include <QtTest/QtTest>

class TestXmlScoreToMidi : public QObject {
    Q_OBJECT

private slots:
    void emptyScore_returnsEmpty();
    void singlePart_producesValidSmfHeader();
    void singlePart_hasExpectedTrackCount();
    void multiplePartsProduceMultipleTracks();
    void noteOff_orderedBeforeNoteOnAtSameTick();
    void defaultTempoAndTimeSig_areInsertedWhenMissing();

private:
    static XmlScore makeMinimalScore();
    static int readU16BE(const QByteArray& b, int off);
    static int readU32BE(const QByteArray& b, int off);
    static QList<QPair<int, int>> mtrkOffsets(const QByteArray& smf);
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

XmlScore TestXmlScoreToMidi::makeMinimalScore() {
    XmlScore s;
    s.title = QStringLiteral("Test");
    s.ticksPerQuarter = 960;

    XmlPart p;
    p.id = QStringLiteral("P1");
    p.name = QStringLiteral("Piano");
    p.channel = 0;
    p.program = 0;

    // Two quarter-note middle Cs back-to-back.
    XmlNote n1; n1.startTick = 0;   n1.duration = 960; n1.pitch = 60; n1.velocity = 90;
    XmlNote n2; n2.startTick = 960; n2.duration = 960; n2.pitch = 60; n2.velocity = 90;
    p.notes = { n1, n2 };
    s.parts = { p };
    return s;
}

int TestXmlScoreToMidi::readU16BE(const QByteArray& b, int off) {
    return ((quint8)b[off] << 8) | (quint8)b[off + 1];
}

int TestXmlScoreToMidi::readU32BE(const QByteArray& b, int off) {
    return ((quint8)b[off] << 24) | ((quint8)b[off + 1] << 16) |
           ((quint8)b[off + 2] << 8)  |  (quint8)b[off + 3];
}

QList<QPair<int, int>> TestXmlScoreToMidi::mtrkOffsets(const QByteArray& smf) {
    QList<QPair<int, int>> out;
    int i = 14; // after MThd chunk (4 magic + 4 len + 6 body)
    while (i + 8 <= smf.size()) {
        if (smf.mid(i, 4) != QByteArrayLiteral("MTrk")) break;
        int len = readU32BE(smf, i + 4);
        out.append({i + 8, len});
        i += 8 + len;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void TestXmlScoreToMidi::emptyScore_returnsEmpty() {
    XmlScore empty;
    QVERIFY(XmlScoreToMidi::encode(empty).isEmpty());
}

void TestXmlScoreToMidi::singlePart_producesValidSmfHeader() {
    QByteArray smf = XmlScoreToMidi::encode(makeMinimalScore());
    QVERIFY(smf.size() > 14);
    QCOMPARE(smf.mid(0, 4), QByteArrayLiteral("MThd"));
    QCOMPARE(readU32BE(smf, 4), 6);                  // header length
    QCOMPARE(readU16BE(smf, 8), 1);                  // SMF format 1
    QCOMPARE(readU16BE(smf, 12), 960);               // PPQ
}

void TestXmlScoreToMidi::singlePart_hasExpectedTrackCount() {
    QByteArray smf = XmlScoreToMidi::encode(makeMinimalScore());
    QCOMPARE(readU16BE(smf, 10), 2);                 // 1 meta + 1 part
    QCOMPARE(mtrkOffsets(smf).size(), 2);
}

void TestXmlScoreToMidi::multiplePartsProduceMultipleTracks() {
    XmlScore s = makeMinimalScore();
    XmlPart p2 = s.parts.first();
    p2.id = QStringLiteral("P2");
    p2.name = QStringLiteral("Bass");
    p2.channel = 1;
    p2.program = 32;
    s.parts.append(p2);

    QByteArray smf = XmlScoreToMidi::encode(s);
    QCOMPARE(readU16BE(smf, 10), 3);                 // 1 meta + 2 parts
    QCOMPARE(mtrkOffsets(smf).size(), 3);
}

void TestXmlScoreToMidi::noteOff_orderedBeforeNoteOnAtSameTick() {
    // Two notes where the second starts exactly when the first ends.
    // The writer must emit the note-off before the next note-on, otherwise
    // the second note immediately turns off.
    XmlScore s = makeMinimalScore();

    QByteArray smf = XmlScoreToMidi::encode(s);
    auto chunks = mtrkOffsets(smf);
    QVERIFY(chunks.size() == 2);

    // Track 1 = the part track. Walk it and locate the events at tick 960.
    const int trackStart = chunks[1].first;
    const int trackLen   = chunks[1].second;
    const QByteArray trk = smf.mid(trackStart, trackLen);

    int pos = 0;
    int absoluteTick = 0;
    int noteOffSeenAt960 = -1;  // event index when off arrived
    int noteOnSeenAt960  = -1;
    int eventIdx = 0;

    auto readVarLen = [&](const QByteArray& d, int& p) {
        int v = 0;
        for (int i = 0; i < 4; ++i) {
            quint8 b = (quint8)d[p++];
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        return v;
    };

    while (pos < trk.size()) {
        int delta = readVarLen(trk, pos);
        absoluteTick += delta;
        quint8 status = (quint8)trk[pos++];
        if (status == 0xFF) {
            // meta: type + len + body
            ++pos; // type
            int len = readVarLen(trk, pos);
            if (status == 0xFF && (quint8)trk[pos - 1 - len - 1] == 0x2F) break;
            pos += len;
            continue;
        }
        const quint8 type = status & 0xF0;
        if (type == 0xC0) {           // program change: 1 data byte
            ++pos;
        } else if (type == 0x80 || type == 0x90) {
            ++pos; ++pos;             // note byte + velocity
            if (absoluteTick == 960) {
                if (type == 0x80 || (type == 0x90 && (quint8)trk[pos - 1] == 0)) {
                    if (noteOffSeenAt960 < 0) noteOffSeenAt960 = eventIdx;
                } else {
                    if (noteOnSeenAt960 < 0)  noteOnSeenAt960  = eventIdx;
                }
            }
        } else {
            ++pos; ++pos;             // assume 2 data bytes for unknowns
        }
        ++eventIdx;
    }

    QVERIFY2(noteOffSeenAt960 >= 0, "Expected a note-off at tick 960");
    QVERIFY2(noteOnSeenAt960  >= 0, "Expected a note-on  at tick 960");
    QVERIFY2(noteOffSeenAt960 < noteOnSeenAt960,
             "Note-off must precede note-on at the same tick");
}

void TestXmlScoreToMidi::defaultTempoAndTimeSig_areInsertedWhenMissing() {
    XmlScore s = makeMinimalScore();
    QVERIFY(s.tempos.isEmpty());
    QVERIFY(s.timeSigs.isEmpty());

    QByteArray smf = XmlScoreToMidi::encode(s);
    auto chunks = mtrkOffsets(smf);
    QVERIFY(chunks.size() == 2);

    // Track 0 should contain at least one 0xFF 0x51 (tempo) and 0xFF 0x58 (time sig).
    QByteArray meta = smf.mid(chunks[0].first, chunks[0].second);
    QVERIFY(meta.contains(QByteArrayLiteral("\xFF\x51\x03")));
    QVERIFY(meta.contains(QByteArrayLiteral("\xFF\x58\x04")));
}

QTEST_APPLESS_MAIN(TestXmlScoreToMidi)
#include "test_xml_score_to_midi.moc"
