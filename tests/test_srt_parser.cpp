/*
 * test_srt_parser
 *
 * Unit tests for src/converter/SrtParser. SrtParser depends on MidiFile only
 * for tick<->ms conversion (two non-virtual methods). Rather than link the
 * whole MidiFile dep tree, this test provides an ODR shim: a minimal stub
 * class also named "MidiFile" that defines the same two member symbols. The
 * SrtParser.cpp translation unit compiles against the real header (it only
 * holds the pointer, it never constructs MidiFile), and the linker resolves
 * the qualified MidiFile::tick / MidiFile::msOfTick symbols against the
 * stub implementation below. The stub uses 1 tick == 1 ms so tick / ms
 * round-trips are trivially verifiable.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

class MidiEvent; // forward decl matches the real LyricBlock.h

// ---- Stub MidiFile (ODR shim) ----------------------------------------------
// Must NOT include the real MidiFile.h here. SrtParser.cpp pulls the real
// header in its own TU; the linker only cares about mangled symbol names.
// The methods MUST be defined out of line — inline in-class definitions do
// not emit external symbols, so SrtParser.cpp's external references would
// remain unresolved at link time.
class MidiFile {
public:
    int tick(int ms);
    int msOfTick(int t, QList<MidiEvent*>* events = nullptr, int msOfFirst = 0);
};

int MidiFile::tick(int ms) { return ms; }
int MidiFile::msOfTick(int t, QList<MidiEvent*>* /*events*/, int /*msOfFirst*/) { return t; }

#include "../src/midi/LyricBlock.h"
#include "../src/converter/SrtParser.h"

class TestSrtParser : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    // Helper: write `content` to a fresh file inside m_dir and return its
    // path. Uses plain QFile rather than QTemporaryFile to avoid Windows
    // exclusive-open issues and to keep error reporting visible (Q_ASSERT
    // is a no-op in Release builds).
    QString writeTempSrt(const QByteArray &content) {
        const QString path = m_dir.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".srt");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            qFatal("writeTempSrt: failed to open %s: %s",
                   qPrintable(path), qPrintable(f.errorString()));
        }
        const qint64 n = f.write(content);
        if (n != content.size()) {
            qFatal("writeTempSrt: short write to %s (%lld of %lld)",
                   qPrintable(path), n, qint64(content.size()));
        }
        f.close();
        return path;
    }

private slots:

    void initTestCase() {
        QVERIFY2(m_dir.isValid(), qPrintable(m_dir.errorString()));
    }

    void import_happyPath_twoEntries() {
        const QByteArray srt =
            "1\r\n"
            "00:00:05,000 --> 00:00:10,500\r\n"
            "First line\r\n"
            "\r\n"
            "2\r\n"
            "00:00:10,800 --> 00:00:15,200\r\n"
            "Second line\r\n"
            "\r\n";
        MidiFile file;
        const QList<LyricBlock> blocks = SrtParser::importSrt(writeTempSrt(srt), &file);
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].startTick, 5000);
        QCOMPARE(blocks[0].endTick,   10500);
        QCOMPARE(blocks[0].text, QString("First line"));
        QCOMPARE(blocks[1].startTick, 10800);
        QCOMPARE(blocks[1].endTick,   15200);
        QCOMPARE(blocks[1].text, QString("Second line"));
    }

    void import_dotSeparator_alsoAccepted() {
        // SRT spec uses comma, but the regex allows '.' for tolerance with
        // tools that emit periods.
        const QByteArray srt =
            "1\n"
            "00:01:02.250 --> 00:01:03.750\n"
            "Hello\n"
            "\n";
        MidiFile file;
        const QList<LyricBlock> blocks = SrtParser::importSrt(writeTempSrt(srt), &file);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].startTick, 62250);
        QCOMPARE(blocks[0].endTick,   63750);
    }

    void import_multiLineText_isJoinedWithSpace() {
        const QByteArray srt =
            "1\n"
            "00:00:01,000 --> 00:00:02,000\n"
            "Line one\n"
            "Line two\n"
            "\n";
        MidiFile file;
        const QList<LyricBlock> blocks = SrtParser::importSrt(writeTempSrt(srt), &file);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].text, QString("Line one Line two"));
    }

    void import_finalEntryWithoutTrailingBlank_isStillKept() {
        // No blank line after the last entry.
        const QByteArray srt =
            "1\n"
            "00:00:01,000 --> 00:00:02,000\n"
            "Only entry";
        MidiFile file;
        const QList<LyricBlock> blocks = SrtParser::importSrt(writeTempSrt(srt), &file);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].text, QString("Only entry"));
        QCOMPARE(blocks[0].startTick, 1000);
        QCOMPARE(blocks[0].endTick,   2000);
    }

    void import_malformedTimingLine_resetsAndContinues() {
        // First entry has a broken timing line (missing arrow). Parser
        // should drop it and recover for the next valid entry.
        const QByteArray srt =
            "1\n"
            "00:00:01,000 00:00:02,000\n"
            "broken\n"
            "\n"
            "2\n"
            "00:00:03,000 --> 00:00:04,000\n"
            "good\n"
            "\n";
        MidiFile file;
        const QList<LyricBlock> blocks = SrtParser::importSrt(writeTempSrt(srt), &file);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].text, QString("good"));
    }

    void import_bomPrefixedFile_isParsed() {
        // UTF-8 BOM (EF BB BF) at the start must be stripped before parsing
        // the sequence number.
        QByteArray srt;
        srt.append('\xEF'); srt.append('\xBB'); srt.append('\xBF');
        srt.append("1\n00:00:00,500 --> 00:00:01,500\nhi\n\n");
        MidiFile file;
        const QList<LyricBlock> blocks = SrtParser::importSrt(writeTempSrt(srt), &file);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].text, QString("hi"));
        QCOMPARE(blocks[0].startTick, 500);
    }

    void import_missingFile_returnsEmptyList() {
        MidiFile file;
        const QList<LyricBlock> blocks =
            SrtParser::importSrt(QStringLiteral("__definitely_not_a_real_file__.srt"), &file);
        QVERIFY(blocks.isEmpty());
    }

    void import_nullMidiFile_returnsEmptyList() {
        const QList<LyricBlock> blocks =
            SrtParser::importSrt(QStringLiteral("anything.srt"), nullptr);
        QVERIFY(blocks.isEmpty());
    }

    void exportImport_roundTrip_preservesTickAndText() {
        QList<LyricBlock> in;
        LyricBlock a; a.startTick = 5000;  a.endTick = 10500; a.text = "Verse one"; in.append(a);
        LyricBlock b; b.startTick = 10800; b.endTick = 15200; b.text = "Verse two"; in.append(b);

        const QString path = m_dir.filePath("roundtrip.srt");

        MidiFile file;
        QVERIFY(SrtParser::exportSrt(path, in, &file));

        const QList<LyricBlock> out = SrtParser::importSrt(path, &file);
        QCOMPARE(out.size(), in.size());
        for (int i = 0; i < in.size(); ++i) {
            QCOMPARE(out[i].startTick, in[i].startTick);
            QCOMPARE(out[i].endTick,   in[i].endTick);
            QCOMPARE(out[i].text,      in[i].text);
        }
    }

    void export_emptyBlocks_returnsFalse() {
        MidiFile file;
        QVERIFY(!SrtParser::exportSrt(QStringLiteral("dummy.srt"), {}, &file));
    }

    void export_nullMidiFile_returnsFalse() {
        QList<LyricBlock> blocks;
        LyricBlock b; b.startTick = 0; b.endTick = 1000; b.text = "x"; blocks.append(b);
        QVERIFY(!SrtParser::exportSrt(QStringLiteral("dummy.srt"), blocks, nullptr));
    }

    void export_emitsCanonicalCommaSeparatedTimecode() {
        QList<LyricBlock> blocks;
        LyricBlock b; b.startTick = 3661250; b.endTick = 3662000; b.text = "tc"; blocks.append(b);

        const QString path = m_dir.filePath("timecode.srt");

        MidiFile file;
        QVERIFY(SrtParser::exportSrt(path, blocks, &file));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(f.readAll());
        f.close();

        // 3,661,250 ms == 1h 01m 01s 250ms
        QVERIFY2(text.contains("01:01:01,250 --> 01:01:02,000"),
                 qPrintable(QStringLiteral("missing canonical timecode in:\n") + text));
    }
};

QTEST_APPLESS_MAIN(TestSrtParser)
#include "test_srt_parser.moc"
