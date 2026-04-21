/*
 * test_text_event
 *
 * Unit tests for TextEvent's SMF byte encoding and accessors.
 *
 * What the production code does
 * -----------------------------
 *   QByteArray TextEvent::save() emits the canonical SMF text-meta layout:
 *      FF <type> <varlen> <utf8 bytes>
 *   where:
 *     - 0xFF is the meta-event prefix
 *     - <type> is one of TEXT / COPYRIGHT / TRACKNAME / INSTRUMENT_NAME /
 *       LYRIK / MARKER / COMMENT (0x01..0x07)
 *     - <varlen> is the SMF variable-length-quantity encoding of the
 *       UTF-8 byte count (NOT the QString character count)
 *     - <utf8 bytes> is the UTF-8 encoding of the QString payload
 *
 * Strategy
 * --------
 * Compiles only TextEvent.cpp directly. The MidiEvent base + ProtocolEntry
 * + GraphicObject + EventWidget + MidiTrack are ODR-shimmed exactly like
 * test_meta_events / test_note_pairing.
 *
 * One real symbol must be provided: MidiFile::writeVariableLengthValue
 * (called from TextEvent::save). We re-implement it byte-for-byte from
 * the production source so the encoded prefix is identical to what a
 * real MidiFile would emit.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QByteArray>
#include <QString>
#include <QPainter>

#include "../src/MidiEvent/TextEvent.h"

// ---- ODR shims: ProtocolEntry -------------------------------------------
ProtocolEntry::~ProtocolEntry() {}
ProtocolEntry *ProtocolEntry::copy() { return nullptr; }
void ProtocolEntry::reloadState(ProtocolEntry *) {}
MidiFile *ProtocolEntry::file() { return nullptr; }
void ProtocolEntry::protocol(ProtocolEntry *oldObj, ProtocolEntry *) {
    delete oldObj;
}

// ---- ODR shims: GraphicObject -------------------------------------------
#include "../src/gui/GraphicObject.h"
GraphicObject::GraphicObject() {}
void GraphicObject::draw(QPainter *, QColor) {}
bool GraphicObject::shown() { return false; }

// ---- ODR shims: EventWidget ---------------------------------------------
#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}

// ---- ODR shims: MidiEvent base ------------------------------------------
quint8 MidiEvent::_startByte = 0;
EventWidget *MidiEvent::_eventWidget = nullptr;

MidiEvent::MidiEvent(int channel, MidiTrack *track) {
    _track = track;
    numChannel = channel;
    timePos = 0;
    midiFile = nullptr;
    _tempID = -1;
}

MidiEvent::MidiEvent(MidiEvent &other)
    : ProtocolEntry(other), GraphicObject() {
    _track = other._track;
    numChannel = other.numChannel;
    timePos = other.timePos;
    midiFile = other.midiFile;
    _tempID = other._tempID;
}

MidiFile *MidiEvent::file() { return midiFile; }
int MidiEvent::line() { return UNKNOWN_LINE; }
QString MidiEvent::toMessage() { return QString(); }
QByteArray MidiEvent::save() { return QByteArray(); }
void MidiEvent::draw(QPainter *, QColor) {}
ProtocolEntry *MidiEvent::copy() { return nullptr; }
void MidiEvent::reloadState(ProtocolEntry *) {}
QString MidiEvent::typeString() { return QString(); }
bool MidiEvent::isOnEvent() { return false; }
void MidiEvent::setMidiTime(int t, bool) { timePos = t; }
void MidiEvent::moveToChannel(int channel, bool) { numChannel = channel; }
int MidiEvent::channel() {
    if (numChannel < 0 || numChannel > 18) return 0;
    return numChannel;
}
MidiTrack *MidiEvent::track() { return _track; }

// ---- ODR shims: MidiFile (only the helper we need) ----------------------
// IMPORTANT: do NOT define MidiFile::~MidiFile() (would anchor the vtable
// in this TU and pull in every other MidiFile virtual symbol). We never
// construct a MidiFile here.
class MidiFile;
#include "../src/midi/MidiFile.h"

QByteArray MidiFile::writeVariableLengthValue(int value) {
    // Verbatim copy of the production algorithm so the encoded prefix
    // matches what a real MidiFile would emit. Assumes value >= 0.
    QByteArray array;
    bool isFirst = true;
    for (int i = 3; i >= 0; i--) {
        int b = value >> (7 * i);
        qint8 byte = (qint8) b & 127;
        if (!isFirst || byte > 0 || i == 0) {
            isFirst = false;
            if (i > 0) {
                byte |= 128;
            }
            array.append(byte);
        }
    }
    return array;
}

// =========================================================================

namespace {

QByteArray buildExpected(int textType, const QByteArray &utf8) {
    QByteArray out;
    out.append(char(0xFF));
    out.append(char(textType));
    out.append(MidiFile::writeVariableLengthValue(utf8.size()));
    out.append(utf8);
    return out;
}

} // namespace

class TestTextEvent : public QObject {
    Q_OBJECT

private slots:

    void ctor_defaults_thenTypeIsTextAndPayloadEmpty() {
        TextEvent ev(16, nullptr);
        QCOMPARE(ev.type(), int(TextEvent::TEXT));
        QCOMPARE(ev.text(), QString());
        QCOMPARE(ev.line(), int(MidiEvent::TEXT_EVENT_LINE));
    }

    void save_emptyDefaultText_thenFiveBytePrologueWithZeroLength() {
        // FF 01 00  (no payload)
        TextEvent ev(16, nullptr);
        QByteArray expected;
        expected.append(char(0xFF));
        expected.append(char(0x01)); // TEXT
        expected.append(char(0x00)); // varlen 0
        QCOMPARE(ev.save(), expected);
    }

    void save_asciiText_thenTypeByteAndShortVarlenAndAsciiPayload() {
        TextEvent ev(16, nullptr);
        ev.setText(QStringLiteral("Hello"));
        const QByteArray expected = buildExpected(TextEvent::TEXT,
                                                  QByteArray("Hello"));
        QCOMPARE(ev.save(), expected);
        // "Hello" is 5 bytes -> varlen single byte 0x05 -> total = 3 + 5
        QCOMPARE(ev.save().size(), 8);
    }

    void save_lyricType_thenTypeByteIs0x05() {
        TextEvent ev(16, nullptr);
        ev.setType(TextEvent::LYRIK);
        ev.setText(QStringLiteral("la"));
        const QByteArray bytes = ev.save();
        QCOMPARE(bytes.at(0), char(0xFF));
        QCOMPARE(bytes.at(1), char(0x05)); // LYRIK
        QCOMPARE(bytes.at(2), char(0x02)); // varlen length 2
        QCOMPARE(bytes.mid(3), QByteArray("la"));
    }

    void save_markerType_thenTypeByteIs0x06() {
        TextEvent ev(16, nullptr);
        ev.setType(TextEvent::MARKER);
        ev.setText(QStringLiteral("Verse 1"));
        const QByteArray bytes = ev.save();
        QCOMPARE(bytes.at(1), char(0x06)); // MARKER
        QCOMPARE(bytes.at(2), char(0x07)); // 7 ASCII bytes
        QCOMPARE(bytes.mid(3), QByteArray("Verse 1"));
    }

    void save_trackNameAndInstrumentAndCopyright_thenTypeBytesMatch() {
        struct Case { int type; char expected; const char *payload; };
        const Case cases[] = {
            { TextEvent::COPYRIGHT,       char(0x02), "(c) 2026" },
            { TextEvent::TRACKNAME,       char(0x03), "Lead"     },
            { TextEvent::INSTRUMENT_NAME, char(0x04), "Piano"    },
            { TextEvent::COMMENT,         char(0x07), "TODO"     },
        };
        for (const Case &c : cases) {
            TextEvent ev(16, nullptr);
            ev.setType(c.type);
            ev.setText(QString::fromUtf8(c.payload));
            const QByteArray bytes = ev.save();
            QVERIFY2(bytes.size() >= 3, "prologue truncated");
            QCOMPARE(bytes.at(0), char(0xFF));
            QCOMPARE(bytes.at(1), c.expected);
            // varlen for ASCII payload <128 bytes is a single byte
            QCOMPARE(bytes.at(2), char(qstrlen(c.payload)));
            QCOMPARE(bytes.mid(3), QByteArray(c.payload));
        }
    }

    void save_utf8MultiByteCharacters_thenLengthCountsBytesNotCodepoints() {
        // U+00E9 'é' encodes to 2 UTF-8 bytes (C3 A9). The SMF length
        // prefix MUST count UTF-8 bytes, not QChar code units, otherwise
        // accented lyrics will desync the parser.
        TextEvent ev(16, nullptr);
        ev.setType(TextEvent::LYRIK);
        ev.setText(QString::fromUtf8("café"));   // 4 chars, 5 UTF-8 bytes
        const QByteArray utf8 = QByteArray::fromHex("636166c3a9"); // c a f é
        const QByteArray expected = buildExpected(TextEvent::LYRIK, utf8);
        QCOMPARE(ev.save(), expected);
        QCOMPARE(ev.save().at(2), char(0x05)); // 5 UTF-8 bytes
    }

    void save_payloadOver127Bytes_thenVarlenIsTwoBytes() {
        // The 0x80 byte boundary is the var-int rollover: 200 bytes of
        // payload must encode as 0x81 0x48 (two-byte varlen), not 0xC8.
        TextEvent ev(16, nullptr);
        const QByteArray payload(200, 'x');
        ev.setText(QString::fromUtf8(payload));
        const QByteArray bytes = ev.save();
        QCOMPARE(bytes.at(0), char(0xFF));
        QCOMPARE(bytes.at(1), char(0x01));
        QCOMPARE(bytes.at(2), char(0x81)); // continuation bit + high 7 bits
        QCOMPARE(bytes.at(3), char(0x48)); // low 7 bits  (0x81<<7 | 0x48 == 200)
        QCOMPARE(bytes.mid(4), payload);
        QCOMPARE(bytes.size(), 4 + 200);
    }

    void setText_roundTripsExactlyEvenWithEmbeddedNewlines() {
        TextEvent ev(16, nullptr);
        const QString payload = QStringLiteral("line1\nline2\n");
        ev.setText(payload);
        QCOMPARE(ev.text(), payload);
        const QByteArray bytes = ev.save();
        // The newline 0x0A is preserved verbatim — TextEvent does not
        // attempt to escape control characters.
        QCOMPARE(bytes.mid(3), payload.toUtf8());
    }

    void setType_persistsAndIsReturnedByType() {
        TextEvent ev(16, nullptr);
        QCOMPARE(ev.type(), int(TextEvent::TEXT));
        ev.setType(TextEvent::MARKER);
        QCOMPARE(ev.type(), int(TextEvent::MARKER));
        ev.setType(TextEvent::LYRIK);
        QCOMPARE(ev.type(), int(TextEvent::LYRIK));
    }

    void typeForNewEvents_isProcessWideStaticAndRoundTrips() {
        const int original = TextEvent::getTypeForNewEvents();
        TextEvent::setTypeForNewEvents(TextEvent::MARKER);
        QCOMPARE(TextEvent::getTypeForNewEvents(), int(TextEvent::MARKER));
        TextEvent::setTypeForNewEvents(TextEvent::LYRIK);
        QCOMPARE(TextEvent::getTypeForNewEvents(), int(TextEvent::LYRIK));
        TextEvent::setTypeForNewEvents(original); // restore for sibling tests
    }
};

QTEST_APPLESS_MAIN(TestTextEvent)
#include "test_text_event.moc"
