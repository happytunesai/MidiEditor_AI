/*
 * test_lrc_exporter
 *
 * Unit tests for src/converter/LrcExporter. LrcExporter (like SrtParser)
 * only depends on MidiFile via two non-virtual member functions:
 *     int  MidiFile::tick(int ms)
 *     int  MidiFile::msOfTick(int t, QList<MidiEvent*>*, int)
 * Rather than link the entire MidiFile dep tree, this test provides an
 * ODR shim: a minimal stub class also named "MidiFile" defining the same
 * two member symbols. The shim uses tick == ms so round-trip math is
 * trivially verifiable (1 tick = 1 ms).
 *
 * LrcExporter.h transitively pulls in LyricManager.h which holds Q_OBJECT.
 * AUTOMOC will generate moc_LyricManager.cpp; that TU only needs QtCore
 * symbols (QObject metadata) and never instantiates LyricManager, so the
 * test target links cleanly without LyricManager.cpp.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QList>
#include <QString>
#include <QUuid>

class MidiEvent; // matches the real header forward decls

// ---- Stub MidiFile (ODR shim) ----------------------------------------------
class MidiFile {
public:
    int tick(int ms);
    int msOfTick(int t, QList<MidiEvent*>* events = nullptr, int msOfFirst = 0);
};

int MidiFile::tick(int ms) { return ms; }
int MidiFile::msOfTick(int t, QList<MidiEvent*>* /*events*/, int /*msOfFirst*/) { return t; }

#include "../src/midi/LyricBlock.h"
#include "../src/midi/LyricManager.h"   // for LyricMetadata struct
#include "../src/converter/LrcExporter.h"

class TestLrcExporter : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString tempPath(const QString &suffix = QStringLiteral(".lrc")) {
        return m_dir.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + suffix);
    }

    QString writeTemp(const QByteArray &content) {
        const QString path = tempPath();
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            qFatal("writeTemp: failed to open %s: %s",
                   qPrintable(path), qPrintable(f.errorString()));
        }
        f.write(content);
        f.close();
        return path;
    }

    QByteArray readAll(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            qFatal("readAll: failed to open %s", qPrintable(path));
        }
        QByteArray data = f.readAll();
        f.close();
        return data;
    }

    static LyricBlock makeBlock(int startTick, int endTick, const QString &text) {
        LyricBlock b;
        b.startTick = startTick;
        b.endTick = endTick;
        b.text = text;
        return b;
    }

private slots:

    void initTestCase() {
        QVERIFY2(m_dir.isValid(), qPrintable(m_dir.errorString()));
    }

    // --- Export ---------------------------------------------------------------

    void export_happyPath_writesTimestampedLines() {
        QList<LyricBlock> blocks;
        blocks << makeBlock(    0,  1500, QStringLiteral("First"))
               << makeBlock( 5230,  6500, QStringLiteral("Second"))
               << makeBlock(62450, 63000, QStringLiteral("Third"));
        MidiFile file;
        const QString path = tempPath();
        QVERIFY(LrcExporter::exportLrc(path, blocks, &file));

        const QByteArray data = readAll(path);
        // tick == ms, so 0ms -> 00:00.00 ; 5230ms -> 00:05.23 ; 62450ms -> 01:02.45
        QVERIFY(data.contains("[00:00.00]First"));
        QVERIFY(data.contains("[00:05.23]Second"));
        QVERIFY(data.contains("[01:02.45]Third"));
    }

    void export_metadata_writesHeaderTags() {
        QList<LyricBlock> blocks;
        blocks << makeBlock(0, 1000, QStringLiteral("Hello"));
        LyricMetadata meta;
        meta.artist  = QStringLiteral("Some Artist");
        meta.title   = QStringLiteral("Some Title");
        meta.album   = QStringLiteral("Some Album");
        meta.lyricsBy = QStringLiteral("Translator");
        meta.offsetMs = -250;
        MidiFile file;
        const QString path = tempPath();
        QVERIFY(LrcExporter::exportLrc(path, blocks, &file, meta));

        const QByteArray data = readAll(path);
        QVERIFY(data.contains("[ar:Some Artist]"));
        QVERIFY(data.contains("[ti:Some Title]"));
        QVERIFY(data.contains("[al:Some Album]"));
        QVERIFY(data.contains("[by:Translator]"));
        QVERIFY(data.contains("[offset:-250]"));
        // Lyric line still present
        QVERIFY(data.contains("[00:00.00]Hello"));
    }

    void export_emptyBlocks_returnsFalse() {
        MidiFile file;
        const QString path = tempPath();
        QVERIFY(!LrcExporter::exportLrc(path, {}, &file));
        // No file should have been created
        QVERIFY(!QFile::exists(path));
    }

    void export_nullFile_returnsFalse() {
        QList<LyricBlock> blocks;
        blocks << makeBlock(0, 1000, QStringLiteral("Hi"));
        QVERIFY(!LrcExporter::exportLrc(tempPath(), blocks, nullptr));
    }

    // --- Import ---------------------------------------------------------------

    void import_happyPath_parsesTimestampsAndText() {
        const QByteArray lrc =
            "[00:00.00]First line\n"
            "[00:05.23]Second line\n"
            "[01:02.45]Third line\n";
        MidiFile file;
        const QList<LyricBlock> blocks = LrcExporter::importLrc(writeTemp(lrc), &file);
        QCOMPARE(blocks.size(), 3);
        QCOMPARE(blocks[0].startTick, 0);
        QCOMPARE(blocks[0].text, QString("First line"));
        QCOMPARE(blocks[1].startTick, 5230);
        QCOMPARE(blocks[1].text, QString("Second line"));
        QCOMPARE(blocks[2].startTick, 62450);
        QCOMPARE(blocks[2].text, QString("Third line"));
    }

    void import_endTicks_adjustedToNextBlockAndDefaultLast() {
        const QByteArray lrc =
            "[00:00.00]A\n"
            "[00:01.00]B\n"
            "[00:02.50]C\n";
        MidiFile file;
        const QList<LyricBlock> blocks = LrcExporter::importLrc(writeTemp(lrc), &file);
        QCOMPARE(blocks.size(), 3);
        QCOMPARE(blocks[0].endTick, 1000); // next block's start
        QCOMPARE(blocks[1].endTick, 2500); // next block's start
        // Last block: default 960 ticks
        QCOMPARE(blocks[2].endTick, blocks[2].startTick + 960);
    }

    void import_millisecondPrecision_parsesThreeDigitFraction() {
        const QByteArray lrc = "[00:01.234]Precise\n";
        MidiFile file;
        const QList<LyricBlock> blocks = LrcExporter::importLrc(writeTemp(lrc), &file);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].startTick, 1234);
    }

    void import_multiTimestampLine_producesOneBlockPerStamp() {
        // MidiBard2 / common LRC extension: same lyric replayed at multiple times.
        const QByteArray lrc = "[00:00.00][00:02.50][00:05.00]Repeat me\n";
        MidiFile file;
        const QList<LyricBlock> blocks = LrcExporter::importLrc(writeTemp(lrc), &file);
        QCOMPARE(blocks.size(), 3);
        // Sorted by startTick
        QCOMPARE(blocks[0].startTick, 0);
        QCOMPARE(blocks[1].startTick, 2500);
        QCOMPARE(blocks[2].startTick, 5000);
        for (const LyricBlock &b : blocks)
            QCOMPARE(b.text, QString("Repeat me"));
    }

    void import_metadata_parsedIntoOutParameter() {
        const QByteArray lrc =
            "[ar:Artist]\n"
            "[ti:Title]\n"
            "[al:Album]\n"
            "[by:Translator]\n"
            "[offset:120]\n"
            "[00:00.00]Lyric\n";
        MidiFile file;
        LyricMetadata meta;
        const QList<LyricBlock> blocks = LrcExporter::importLrc(writeTemp(lrc), &file, &meta);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(meta.artist,   QString("Artist"));
        QCOMPARE(meta.title,    QString("Title"));
        QCOMPARE(meta.album,    QString("Album"));
        QCOMPARE(meta.lyricsBy, QString("Translator"));
        QCOMPARE(meta.offsetMs, 120);
    }

    void import_missingFile_returnsEmpty() {
        MidiFile file;
        const QList<LyricBlock> blocks =
            LrcExporter::importLrc(m_dir.filePath(QStringLiteral("does_not_exist.lrc")), &file);
        QVERIFY(blocks.isEmpty());
    }

    void import_nullFile_returnsEmpty() {
        const QByteArray lrc = "[00:00.00]Hi\n";
        const QList<LyricBlock> blocks = LrcExporter::importLrc(writeTemp(lrc), nullptr);
        QVERIFY(blocks.isEmpty());
    }

    void import_emptyTextAfterStamp_isSkipped() {
        const QByteArray lrc =
            "[00:00.00]\n"           // no text -> skipped
            "[00:01.00]Real\n";
        MidiFile file;
        const QList<LyricBlock> blocks = LrcExporter::importLrc(writeTemp(lrc), &file);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].startTick, 1000);
        QCOMPARE(blocks[0].text, QString("Real"));
    }

    // --- Round-trip -----------------------------------------------------------

    void roundTrip_centisecondAlignedTicks_arePreserved() {
        // tick == ms in our shim, and LRC has centisecond resolution, so
        // every input tick MUST be a multiple of 10 to survive the round trip.
        QList<LyricBlock> in;
        in << makeBlock(    0, 0, QStringLiteral("alpha"))
           << makeBlock(  120, 0, QStringLiteral("beta"))
           << makeBlock( 5230, 0, QStringLiteral("gamma"))
           << makeBlock(62450, 0, QStringLiteral("delta"));
        MidiFile file;
        const QString path = tempPath();
        QVERIFY(LrcExporter::exportLrc(path, in, &file));

        const QList<LyricBlock> out = LrcExporter::importLrc(path, &file);
        QCOMPARE(out.size(), in.size());
        for (int i = 0; i < in.size(); ++i) {
            QCOMPARE(out[i].startTick, in[i].startTick);
            QCOMPARE(out[i].text,      in[i].text);
        }
    }

    void roundTrip_metadata_isPreserved() {
        QList<LyricBlock> in;
        in << makeBlock(0, 1000, QStringLiteral("only"));
        LyricMetadata meta;
        meta.artist   = QStringLiteral("A");
        meta.title    = QStringLiteral("T");
        meta.album    = QStringLiteral("Al");
        meta.lyricsBy = QStringLiteral("B");
        meta.offsetMs = 42;
        MidiFile file;
        const QString path = tempPath();
        QVERIFY(LrcExporter::exportLrc(path, in, &file, meta));

        LyricMetadata out;
        const QList<LyricBlock> blocks = LrcExporter::importLrc(path, &file, &out);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(out.artist,   meta.artist);
        QCOMPARE(out.title,    meta.title);
        QCOMPARE(out.album,    meta.album);
        QCOMPARE(out.lyricsBy, meta.lyricsBy);
        QCOMPARE(out.offsetMs, meta.offsetMs);
    }
};

QTEST_APPLESS_MAIN(TestLrcExporter)
#include "test_lrc_exporter.moc"
