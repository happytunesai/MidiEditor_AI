/*
 * test_ai_file_naming
 *
 * Phase 51 (v2.5.0) - the "AI gate" for files MidiPilot and MCP clients save
 * (src/ai/AiFileNaming): every AI-written file is a MARKED copy
 * (<name>.midipilot.mid / <name>.mcp.mid), marks never stack, the extension is
 * always .mid, and an existing file is never written over - except the
 * document's own file when it already carries the caller's mark, and the
 * document's own MIDI file when the user chose "overwrite" (offersOverwrite).
 *
 * Pure Qt Core. planSave() is fed a real temporary folder, so "taken" is the
 * file system plus an explicit set standing in for other open documents.
 */

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>

#include "../src/ai/AiFileNaming.h"

using namespace AiFileNaming;

class TestAiFileNaming : public QObject {
    Q_OBJECT

private:
    QTemporaryDir _dir;

    QString path(const QString &name) const {
        return QDir::cleanPath(QDir(_dir.path()).absoluteFilePath(name));
    }

    void touch(const QString &name) {
        QFile f(path(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("MThd");
    }

    // Taken = on disk, or the path of another open document.
    static std::function<bool(const QString &)> takenBy(const QSet<QString> &openPaths) {
        return [openPaths](const QString &p) {
            return QFile::exists(p) || openPaths.contains(QDir::cleanPath(p));
        };
    }

private slots:
    void initTestCase() {
        QVERIFY(_dir.isValid());
    }

    void cleanup() {
        // Every test starts in an empty folder.
        QDir d(_dir.path());
        for (const QString &f : d.entryList(QDir::Files))
            d.remove(f);
    }

    // --- the pure helpers -------------------------------------------------

    void markFor_mcpSourcesOnly() {
        QCOMPARE(markFor(QString()), QStringLiteral("midipilot"));
        QCOMPARE(markFor(QStringLiteral("agent")), QStringLiteral("midipilot"));
        QCOMPARE(markFor(QStringLiteral("mcp")), QStringLiteral("mcp"));
        QCOMPARE(markFor(QStringLiteral("mcp:Codex 1.2")), QStringLiteral("mcp"));
    }

    void stripKnownExtension_onlyOpenableFormats() {
        QCOMPARE(stripKnownExtension(QStringLiteral("mozart.mid")), QStringLiteral("mozart"));
        QCOMPARE(stripKnownExtension(QStringLiteral("mozart.MIDI")), QStringLiteral("mozart"));
        QCOMPARE(stripKnownExtension(QStringLiteral("mozart.gp5")), QStringLiteral("mozart"));
        QCOMPARE(stripKnownExtension(QStringLiteral("mozart.musicxml")), QStringLiteral("mozart"));
        QCOMPARE(stripKnownExtension(QStringLiteral("mozart.sid")), QStringLiteral("mozart"));
        // A dot that belongs to the name stays.
        QCOMPARE(stripKnownExtension(QStringLiteral("Mozart K.525")),
                 QStringLiteral("Mozart K.525"));
        QCOMPARE(stripKnownExtension(QStringLiteral("notes.txt")), QStringLiteral("notes.txt"));
        QCOMPARE(stripKnownExtension(QStringLiteral(".mid")), QStringLiteral(".mid"));
    }

    void stripMarks_removesStackedMarksAndCounters() {
        QCOMPARE(stripMarks(QStringLiteral("mozart.midipilot")), QStringLiteral("mozart"));
        QCOMPARE(stripMarks(QStringLiteral("mozart.mcp.3")), QStringLiteral("mozart"));
        QCOMPARE(stripMarks(QStringLiteral("mozart.mcp.midipilot.2")), QStringLiteral("mozart"));
        QCOMPARE(stripMarks(QStringLiteral("mozart.MidiPilot")), QStringLiteral("mozart"));
        QCOMPARE(stripMarks(QStringLiteral("mozart")), QStringLiteral("mozart"));
        // Only trailing marks: a mark inside the name is part of the name.
        QCOMPARE(stripMarks(QStringLiteral("mcp.mozart")), QStringLiteral("mcp.mozart"));
    }

    void baseOf_normalisesEveryPassedForm() {
        // The acceptance case: x.mid, x.midi and x.midipilot.mid all end as "x".
        for (const QString &n : {QStringLiteral("x.mid"), QStringLiteral("x.midi"),
                                 QStringLiteral("x.midipilot.mid"), QStringLiteral("x"),
                                 QStringLiteral("x.midipilot.2.mid"), QStringLiteral("x.mcp.mid"),
                                 QStringLiteral("  x.mid  "), QStringLiteral("x.mid.midipilot.mid"),
                                 QStringLiteral("x.midipilot.gp5")}) {
            QCOMPARE(baseOf(n), QStringLiteral("x"));
        }
        QCOMPARE(baseOf(QStringLiteral("Mozart K.525.mid")), QStringLiteral("Mozart K.525"));
        // Windows drops trailing dots and spaces silently - so do we, visibly.
        QCOMPARE(baseOf(QStringLiteral("song. .")), QStringLiteral("song"));
    }

    void hasMark_requiresMarkDirectlyBeforeMidExtension() {
        QVERIFY(hasMark(QStringLiteral("C:/m/mozart.midipilot.mid"), QStringLiteral("midipilot")));
        QVERIFY(hasMark(QStringLiteral("C:/m/mozart.midipilot.2.mid"), QStringLiteral("midipilot")));
        QVERIFY(hasMark(QStringLiteral("C:/m/mozart.MCP.MID"), QStringLiteral("mcp")));
        QVERIFY(!hasMark(QStringLiteral("C:/m/mozart.mcp.mid"), QStringLiteral("midipilot")));
        QVERIFY(!hasMark(QStringLiteral("C:/m/mozart.mid"), QStringLiteral("midipilot")));
        QVERIFY(!hasMark(QStringLiteral("C:/m/midipilot.mid"), QStringLiteral("midipilot")));
        QVERIFY(!hasMark(QStringLiteral("C:/m/mozart.midipilot.gp5"), QStringLiteral("midipilot")));
    }

    void markedFileName_counterFromTwo() {
        QCOMPARE(markedFileName(QStringLiteral("x"), QStringLiteral("mcp")),
                 QStringLiteral("x.mcp.mid"));
        QCOMPARE(markedFileName(QStringLiteral("x"), QStringLiteral("mcp"), 1),
                 QStringLiteral("x.mcp.mid"));
        QCOMPARE(markedFileName(QStringLiteral("x"), QStringLiteral("midipilot"), 2),
                 QStringLiteral("x.midipilot.2.mid"));
    }

    // --- planSave: the gate itself ----------------------------------------

    // A user file: the marked copy next to it, the original is not a target.
    void planSave_userFile_writesMarkedCopyNextToIt() {
        touch(QStringLiteral("mozart.mid"));
        const SavePlan p = planSave(path(QStringLiteral("mozart.mid")), QString(),
                                    QStringLiteral("midipilot"), false, QString(), takenBy({}));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.targetPath, path(QStringLiteral("mozart.midipilot.mid")));
        QVERIFY(!p.inPlace);
        QVERIFY(!p.nameIgnored);
    }

    // The same over MCP carries the MCP mark.
    void planSave_mcpCaller_usesMcpMark() {
        touch(QStringLiteral("mozart.mid"));
        const SavePlan p = planSave(path(QStringLiteral("mozart.mid")), QString(),
                                    markFor(QStringLiteral("mcp:Codex")), false, QString(),
                                    takenBy({}));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.targetPath, path(QStringLiteral("mozart.mcp.mid")));
    }

    // The AI's own working copy (caller's mark) is saved in place, even
    // though the file exists - the ONE permitted overwrite.
    void planSave_ownMarkedCopy_savesInPlace() {
        touch(QStringLiteral("mozart.midipilot.mid"));
        const SavePlan p = planSave(path(QStringLiteral("mozart.midipilot.mid")),
                                    QStringLiteral("ignored-name"), QStringLiteral("midipilot"),
                                    false, QString(), takenBy({}));
        QVERIFY2(p.ok, qPrintable(p.error));
        QVERIFY(p.inPlace);
        QCOMPARE(p.targetPath, path(QStringLiteral("mozart.midipilot.mid")));
        QVERIFY(p.nameIgnored);
    }

    // Another AI's copy is NOT this caller's working copy: MCP saving a
    // MidiPilot copy writes its own marked file, without stacking marks.
    void planSave_otherAisCopy_isTreatedLikeAUserFile() {
        touch(QStringLiteral("mozart.midipilot.mid"));
        const SavePlan p = planSave(path(QStringLiteral("mozart.midipilot.mid")), QString(),
                                    QStringLiteral("mcp"), false, QString(), takenBy({}));
        QVERIFY2(p.ok, qPrintable(p.error));
        QVERIFY(!p.inPlace);
        QCOMPARE(p.targetPath, path(QStringLiteral("mozart.mcp.mid")));
    }

    // An existing marked file that is NOT the document -> counter, never an
    // overwrite.
    void planSave_takenName_getsCounter() {
        touch(QStringLiteral("mozart.mid"));
        touch(QStringLiteral("mozart.midipilot.mid"));
        touch(QStringLiteral("mozart.midipilot.2.mid"));
        const SavePlan p = planSave(path(QStringLiteral("mozart.mid")), QString(),
                                    QStringLiteral("midipilot"), false, QString(), takenBy({}));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.targetPath, path(QStringLiteral("mozart.midipilot.3.mid")));
    }

    // A path held by another open document counts as taken even when that
    // file is not on disk (yet / any more).
    void planSave_openDocumentPath_countsAsTaken() {
        touch(QStringLiteral("mozart.mid"));
        const SavePlan p = planSave(path(QStringLiteral("mozart.mid")), QString(),
                                    QStringLiteral("midipilot"), false, QString(),
                                    takenBy({path(QStringLiteral("mozart.midipilot.mid"))}));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.targetPath, path(QStringLiteral("mozart.midipilot.2.mid")));
    }

    // --- the user's "overwrite or copy?" decision --------------------------

    // Only a MIDI file the document was opened from raises the question - not
    // an untitled document, not an import that cannot be written back, not
    // the caller's own copy (another AI's copy is a file the user opened).
    void offersOverwrite_onlyForTheDocumentsOwnMidiFile() {
        QVERIFY(offersOverwrite(path(QStringLiteral("mozart.mid")), QStringLiteral("mcp")));
        QVERIFY(offersOverwrite(path(QStringLiteral("Mozart.MIDI")), QStringLiteral("mcp")));
        QVERIFY(offersOverwrite(path(QStringLiteral("mozart.midipilot.mid")), QStringLiteral("mcp")));
        QVERIFY(!offersOverwrite(QString(), QStringLiteral("mcp")));
        QVERIFY(!offersOverwrite(path(QStringLiteral("song.gp5")), QStringLiteral("mcp")));
        QVERIFY(!offersOverwrite(path(QStringLiteral("trio.musicxml")), QStringLiteral("mcp")));
        QVERIFY(!offersOverwrite(path(QStringLiteral("mozart.mcp.mid")), QStringLiteral("mcp")));
        QVERIFY(!offersOverwrite(path(QStringLiteral("mozart.mcp.2.mid")), QStringLiteral("mcp")));
    }

    // "overwrite": the document's own file, in place - a passed name is ignored.
    void planSave_overwriteChosen_writesTheDocumentsOwnFile() {
        touch(QStringLiteral("mozart.mid"));
        const SavePlan p = planSave(path(QStringLiteral("mozart.mid")), QStringLiteral("other"),
                                    QStringLiteral("mcp"), false, QString(), takenBy({}),
                                    /*overwriteDocumentFile=*/true);
        QVERIFY2(p.ok, qPrintable(p.error));
        QVERIFY(p.inPlace);
        QCOMPARE(p.targetPath, path(QStringLiteral("mozart.mid")));
        QVERIFY(p.nameIgnored);
    }

    // "overwrite" never reaches beyond that one file: save_document_as stays a
    // new file, and an import that cannot be written back gets its copy.
    void planSave_overwriteChosen_neverForSaveAsOrImports() {
        touch(QStringLiteral("mozart.mid"));
        touch(QStringLiteral("song.gp5"));
        const SavePlan asCopy = planSave(path(QStringLiteral("mozart.mid")), QStringLiteral("other"),
                                         QStringLiteral("mcp"), true, QString(), takenBy({}), true);
        QVERIFY2(asCopy.ok, qPrintable(asCopy.error));
        QVERIFY(!asCopy.inPlace);
        QCOMPARE(asCopy.targetPath, path(QStringLiteral("other.mcp.mid")));
        const SavePlan import = planSave(path(QStringLiteral("song.gp5")), QString(),
                                         QStringLiteral("mcp"), false, QString(), takenBy({}), true);
        QVERIFY2(import.ok, qPrintable(import.error));
        QVERIFY(!import.inPlace);
        QCOMPARE(import.targetPath, path(QStringLiteral("song.mcp.mid")));
    }

    // Imported, import-only source: marked .mid next to it.
    void planSave_importOnlySource_marksAndForcesMid() {
        touch(QStringLiteral("song.gp5"));
        const SavePlan p = planSave(path(QStringLiteral("song.gp5")), QString(),
                                    QStringLiteral("midipilot"), false, QString(), takenBy({}));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.targetPath, path(QStringLiteral("song.midipilot.mid")));
    }

    // save_document on a titled document ignores a passed name (and says so).
    void planSave_titledDocument_ignoresName() {
        touch(QStringLiteral("mozart.mid"));
        const SavePlan p = planSave(path(QStringLiteral("mozart.mid")), QStringLiteral("other"),
                                    QStringLiteral("midipilot"), false, QString(), takenBy({}));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.targetPath, path(QStringLiteral("mozart.midipilot.mid")));
        QVERIFY(p.nameIgnored);
    }

    // Untitled + bare name -> the fallback folder; any passed form of the
    // name ends marked and .mid.
    void planSave_untitled_bareNameGoesToFallbackFolder() {
        for (const QString &n : {QStringLiteral("octet"), QStringLiteral("octet.mid"),
                                 QStringLiteral("octet.midi"),
                                 QStringLiteral("octet.midipilot.mid")}) {
            const SavePlan p = planSave(QString(), n, QStringLiteral("midipilot"), false,
                                        _dir.path(), takenBy({}));
            QVERIFY2(p.ok, qPrintable(p.error));
            QCOMPARE(p.targetPath, path(QStringLiteral("octet.midipilot.mid")));
        }
    }

    // A relative name keeps only its file-name part: no escaping the folder.
    void planSave_relativeNameWithFolders_keepsOnlyFileName() {
        const SavePlan p = planSave(QString(), QStringLiteral("..\\..\\evil"),
                                    QStringLiteral("midipilot"), false, _dir.path(),
                                    takenBy({}));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.targetPath, path(QStringLiteral("evil.midipilot.mid")));
    }

    // A full path the user gave is used as given - mark and extension apply.
    void planSave_absolutePath_usedAsGiven() {
        const SavePlan p = planSave(QString(), path(QStringLiteral("given.mid")),
                                    QStringLiteral("mcp"), false, QStringLiteral("C:/elsewhere"),
                                    takenBy({}));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.targetPath, path(QStringLiteral("given.mcp.mid")));
    }

    // Untitled with nothing to go on -> refusal that tells the model to ask.
    void planSave_untitledWithoutName_isRefused() {
        const SavePlan p = planSave(QString(), QStringLiteral("   "), QStringLiteral("midipilot"),
                                    false, _dir.path(), takenBy({}));
        QVERIFY(!p.ok);
        QVERIFY2(p.error.contains(QStringLiteral("ask the user")), qPrintable(p.error));
    }

    void planSave_invalidOrEmptyNames_areRefused() {
        for (const QString &n : {QStringLiteral("a<b"), QStringLiteral("what?"),
                                 QStringLiteral(".midipilot.mid"), QStringLiteral("con"),
                                 QStringLiteral("LPT1.mid")}) {
            const SavePlan p = planSave(QString(), n, QStringLiteral("midipilot"), false,
                                        _dir.path(), takenBy({}));
            QVERIFY2(!p.ok, qPrintable(n));
            QVERIFY(!p.error.isEmpty());
        }
        // Too long.
        const SavePlan longName = planSave(QString(), QString(121, QLatin1Char('a')),
                                           QStringLiteral("midipilot"), false, _dir.path(),
                                           takenBy({}));
        QVERIFY(!longName.ok);
    }

    // Folders are never created.
    void planSave_missingFolder_isRefused() {
        const SavePlan p = planSave(QString(), path(QStringLiteral("nope/x.mid")),
                                    QStringLiteral("midipilot"), false, QString(), takenBy({}));
        QVERIFY(!p.ok);
        QVERIFY2(p.error.contains(QStringLiteral("does not exist")), qPrintable(p.error));
    }

    // save_document_as always writes a NEW file: from the own working copy it
    // takes the next counter instead of writing in place.
    void planSaveAs_fromOwnCopy_writesNewFile() {
        touch(QStringLiteral("mozart.midipilot.mid"));
        const SavePlan p = planSave(path(QStringLiteral("mozart.midipilot.mid")), QString(),
                                    QStringLiteral("midipilot"), true, QString(), takenBy({}));
        QVERIFY2(p.ok, qPrintable(p.error));
        QVERIFY(!p.inPlace);
        QCOMPARE(p.targetPath, path(QStringLiteral("mozart.midipilot.2.mid")));
    }

    // "Rename": save_document_as with a new bare name lands next to the
    // current file.
    void planSaveAs_newName_landsNextToCurrentFile() {
        touch(QStringLiteral("mozart.mid"));
        const SavePlan p = planSave(path(QStringLiteral("mozart.mid")),
                                    QStringLiteral("mozart-octet"), QStringLiteral("midipilot"),
                                    true, QStringLiteral("C:/elsewhere"), takenBy({}));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.targetPath, path(QStringLiteral("mozart-octet.midipilot.mid")));
        QVERIFY(!p.nameIgnored);
    }
};

QTEST_APPLESS_MAIN(TestAiFileNaming)
#include "test_ai_file_naming.moc"
