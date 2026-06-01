/*
 * MidiEditor AI - Unit test for the import-only format classifier
 * (src/gui/ImportOnlyFormats.h).
 *
 * These are the formats MidiEditor can only IMPORT - MidiFile::save() always
 * writes Standard MIDI bytes, so MainWindow::save() must redirect them to
 * "Save As" (.mid) instead of overwriting the original on disk. This test locks
 * that classification down: every importer dispatched in MainWindow::openFile()
 * (Guitar Pro, MML, 3MLE, MusicXML, MuseScore, SID) must be recognised, and
 * real MIDI containers must NOT be (so plain .mid editing still saves in place).
 *
 * Regression guard for the 1.8.0 data-loss fix: saving an imported .sid used to
 * overwrite the original .sid with MIDI bytes. `.sid` must stay in the set.
 *
 * Header-only module -> the executable links nothing but Qt::Core/Test.
 */

#include <QString>
#include <QtTest/QtTest>

#include "../src/gui/ImportOnlyFormats.h"

using namespace ImportFormats;

class ImportOnlyFormatsTest : public QObject {
    Q_OBJECT
private slots:
    // ---- every import-only suffix is recognised -------------------------

    void guitarPro_allVariants() {
        for (const QString &s : {QStringLiteral("gtp"), QStringLiteral("gp3"),
                                 QStringLiteral("gp4"), QStringLiteral("gp5"),
                                 QStringLiteral("gp6"), QStringLiteral("gp7"),
                                 QStringLiteral("gp8"), QStringLiteral("gpx"),
                                 QStringLiteral("gp")}) {
            QVERIFY2(isImportOnlySuffix(s), qPrintable(s));
        }
    }

    void mml_and_3mle() {
        QVERIFY(isImportOnlySuffix(QStringLiteral("mml")));
        QVERIFY(isImportOnlySuffix(QStringLiteral("3mle")));
    }

    void musicXml_allVariants() {
        QVERIFY(isImportOnlySuffix(QStringLiteral("musicxml")));
        QVERIFY(isImportOnlySuffix(QStringLiteral("xml")));
        QVERIFY(isImportOnlySuffix(QStringLiteral("mxl")));
    }

    void museScore_allVariants() {
        QVERIFY(isImportOnlySuffix(QStringLiteral("mscz")));
        QVERIFY(isImportOnlySuffix(QStringLiteral("mscx")));
    }

    // The 1.8.0 fix hinges on this one: a .sid must be import-only so saving
    // routes to .mid instead of clobbering the original .sid.
    void sid_isImportOnly() {
        QVERIFY(isImportOnlySuffix(QStringLiteral("sid")));
    }

    // ---- real MIDI / unrelated formats are NOT import-only --------------

    void midiContainers_areWritable() {
        QVERIFY(!isImportOnlySuffix(QStringLiteral("mid")));
        QVERIFY(!isImportOnlySuffix(QStringLiteral("midi")));
        QVERIFY(!isImportOnlySuffix(QStringLiteral("kar")));  // karaoke MIDI
    }

    void unrelatedFormats_areNotImportOnly() {
        QVERIFY(!isImportOnlySuffix(QStringLiteral("wav")));
        QVERIFY(!isImportOnlySuffix(QStringLiteral("txt")));
        QVERIFY(!isImportOnlySuffix(QString()));            // no extension
    }

    // ---- case-insensitivity ---------------------------------------------

    void caseInsensitive() {
        QVERIFY(isImportOnlySuffix(QStringLiteral("SID")));
        QVERIFY(isImportOnlySuffix(QStringLiteral("GP5")));
        QVERIFY(isImportOnlySuffix(QStringLiteral("MusicXML")));
        QVERIFY(!isImportOnlySuffix(QStringLiteral("MID")));
    }

    // ---- path-based overload extracts the suffix correctly --------------

    void pathOverload_importOnly() {
        QVERIFY(isImportOnly(QStringLiteral("C:/tunes/Commando.sid")));
        QVERIFY(isImportOnly(QStringLiteral("/home/u/Rondo Alla Turca.gp5")));
        QVERIFY(isImportOnly(QStringLiteral("score.musicxml")));
    }

    void pathOverload_writable() {
        QVERIFY(!isImportOnly(QStringLiteral("C:/tunes/song.mid")));
        QVERIFY(!isImportOnly(QStringLiteral("song.midi")));
        QVERIFY(!isImportOnly(QStringLiteral("/no/extension/here")));
    }

    // The classifier keys off the *extension*, not the stem: a MIDI file whose
    // name merely contains "sid" must still save in place.
    void pathOverload_stemNotConfusedForSuffix() {
        QVERIFY(!isImportOnly(QStringLiteral("acid jazz.mid")));
        QVERIFY(!isImportOnly(QStringLiteral("sidewinder.midi")));
        // Multiple dots: only the final segment is the suffix.
        QVERIFY(isImportOnly(QStringLiteral("my.cool.tune.gp5")));
    }
};

QTEST_MAIN(ImportOnlyFormatsTest)
#include "test_import_only_formats.moc"
