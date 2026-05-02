/*
 * MidiEditor AI - Phase 34 Paste Special dialog tests.
 *
 * Covers the GUI-only surface of `PasteSpecialDialog`:
 *   - default radio button matches the constructor's defaultAssignment
 *   - chosenAssignment() reflects the user's selection
 *   - "Make this the new default" stays disabled until "Don't ask again"
 *     is checked, and auto-clears when the user unchecks "Don't ask again"
 *   - summary text reaches the screen (smoke-checks formatting helper)
 *
 * The deeper SharedClipboard wire-format round-trip (v1 reject, v2
 * track-name-table prelude, PasteSourceInfo population) is intentionally
 * NOT covered here because exercising deserializeEvents() in isolation
 * would require the same heavy MidiFile / MidiTrack / MidiEvent ODR
 * shims that `test_tempo_conversion_service.cpp` carries. If that
 * coverage becomes important we can lift those shims into a shared
 * header and depend on them from a second TU.
 */

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QLabel>
#include <QList>
#include <QPair>
#include <QRadioButton>
#include <QString>
#include <QtTest/QtTest>

#include "../src/gui/PasteSpecialDialog.h"

namespace {

PasteClipboardSummary makeSummary() {
    PasteClipboardSummary s;
    s.totalEvents = 247;
    s.sourceTrackCount = 3;
    s.sourceTracks = {
        qMakePair(1, QStringLiteral("Lead")),
        qMakePair(2, QStringLiteral("Bass")),
        qMakePair(3, QStringLiteral("Drums")),
    };
    s.distinctChannels = {0, 1, 9};
    s.approxDurationMs = 4200; // 4.2 s
    return s;
}

QRadioButton *findRadioByText(PasteSpecialDialog *dlg, const QString &needle) {
    const auto radios = dlg->findChildren<QRadioButton *>();
    for (QRadioButton *r : radios) {
        if (r->text().contains(needle, Qt::CaseInsensitive)) return r;
    }
    return nullptr;
}

QCheckBox *findCheckByText(PasteSpecialDialog *dlg, const QString &needle) {
    const auto boxes = dlg->findChildren<QCheckBox *>();
    for (QCheckBox *c : boxes) {
        if (c->text().contains(needle, Qt::CaseInsensitive)) return c;
    }
    return nullptr;
}

} // namespace

class TestPasteSpecialDialog : public QObject {
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------
    void defaultAssignment_NewTracks_setsFirstRadio() {
        PasteSpecialDialog dlg(makeSummary(),
                               PasteAssignment::NewTracksPerSource);
        QCOMPARE(dlg.chosenAssignment(), PasteAssignment::NewTracksPerSource);

        QRadioButton *r = findRadioByText(&dlg, QStringLiteral("new tracks"));
        QVERIFY(r);
        QVERIFY(r->isChecked());
    }

    // -----------------------------------------------------------------
    void defaultAssignment_PreserveMapping_setsSecondRadio() {
        PasteSpecialDialog dlg(makeSummary(),
                               PasteAssignment::PreserveSourceMapping);
        QCOMPARE(dlg.chosenAssignment(), PasteAssignment::PreserveSourceMapping);

        QRadioButton *r = findRadioByText(&dlg, QStringLiteral("preserve"));
        QVERIFY(r);
        QVERIFY(r->isChecked());
    }

    // -----------------------------------------------------------------
    void defaultAssignment_CurrentTarget_setsLegacyRadio() {
        PasteSpecialDialog dlg(makeSummary(),
                               PasteAssignment::CurrentEditTarget);
        QCOMPARE(dlg.chosenAssignment(), PasteAssignment::CurrentEditTarget);

        QRadioButton *r = findRadioByText(&dlg, QStringLiteral("legacy"));
        QVERIFY(r);
        QVERIFY(r->isChecked());
    }

    // -----------------------------------------------------------------
    void chosenAssignment_followsRadioToggle() {
        PasteSpecialDialog dlg(makeSummary(),
                               PasteAssignment::NewTracksPerSource);

        QRadioButton *preserve = findRadioByText(&dlg, QStringLiteral("preserve"));
        QVERIFY(preserve);
        preserve->setChecked(true);
        QCOMPARE(dlg.chosenAssignment(), PasteAssignment::PreserveSourceMapping);

        QRadioButton *legacy = findRadioByText(&dlg, QStringLiteral("legacy"));
        QVERIFY(legacy);
        legacy->setChecked(true);
        QCOMPARE(dlg.chosenAssignment(), PasteAssignment::CurrentEditTarget);

        QRadioButton *neu = findRadioByText(&dlg, QStringLiteral("new tracks"));
        QVERIFY(neu);
        neu->setChecked(true);
        QCOMPARE(dlg.chosenAssignment(), PasteAssignment::NewTracksPerSource);
    }

    // -----------------------------------------------------------------
    void makeDefault_disabledUntilDontAskChecked() {
        PasteSpecialDialog dlg(makeSummary(),
                               PasteAssignment::NewTracksPerSource);

        QCheckBox *dontAsk = findCheckByText(&dlg, QStringLiteral("don't ask"));
        QCheckBox *makeDef = findCheckByText(&dlg, QStringLiteral("default"));
        QVERIFY(dontAsk);
        QVERIFY(makeDef);

        // Pristine: makeDefault disabled, both report false.
        QVERIFY(!makeDef->isEnabled());
        QVERIFY(!dlg.dontAskAgainThisSession());
        QVERIFY(!dlg.makeThisTheNewDefault());

        // Check "Don't ask again" -> makeDefault becomes interactive.
        dontAsk->setChecked(true);
        QVERIFY(makeDef->isEnabled());
        QVERIFY(dlg.dontAskAgainThisSession());
        // Still unchecked until the user opts in.
        QVERIFY(!dlg.makeThisTheNewDefault());

        makeDef->setChecked(true);
        QVERIFY(dlg.makeThisTheNewDefault());
    }

    // -----------------------------------------------------------------
    void makeDefault_clearsWhenDontAskUnchecked() {
        PasteSpecialDialog dlg(makeSummary(),
                               PasteAssignment::NewTracksPerSource);

        QCheckBox *dontAsk = findCheckByText(&dlg, QStringLiteral("don't ask"));
        QCheckBox *makeDef = findCheckByText(&dlg, QStringLiteral("default"));
        QVERIFY(dontAsk && makeDef);

        dontAsk->setChecked(true);
        makeDef->setChecked(true);
        QVERIFY(dlg.makeThisTheNewDefault());

        // Unchecking "Don't ask again" must auto-clear "Make default" so
        // we never persist a value the user didn't intend to lock in.
        dontAsk->setChecked(false);
        QVERIFY(!makeDef->isEnabled());
        QVERIFY(!makeDef->isChecked());
        QVERIFY(!dlg.makeThisTheNewDefault());
        QVERIFY(!dlg.dontAskAgainThisSession());
    }

    // -----------------------------------------------------------------
    void summaryLabel_containsCounts() {
        PasteSpecialDialog dlg(makeSummary(),
                               PasteAssignment::NewTracksPerSource);

        // The italic summary label is the only QLabel placed at the top
        // of the layout. Find it and confirm the formatter pulled the
        // numbers + names through.
        const auto labels = dlg.findChildren<QLabel *>();
        bool foundSummary = false;
        for (QLabel *l : labels) {
            const QString t = l->text();
            if (t.contains(QStringLiteral("247")) &&
                t.contains(QStringLiteral("Lead")) &&
                t.contains(QStringLiteral("Bass")) &&
                t.contains(QStringLiteral("Drums"))) {
                foundSummary = true;
                break;
            }
        }
        QVERIFY2(foundSummary,
                 "PasteSpecialDialog summary label did not surface event "
                 "count + source track names");
    }

    // -----------------------------------------------------------------
    void emptySummary_doesNotCrash() {
        PasteClipboardSummary empty;
        PasteSpecialDialog dlg(empty, PasteAssignment::NewTracksPerSource);
        QCOMPARE(dlg.chosenAssignment(), PasteAssignment::NewTracksPerSource);
        QVERIFY(!dlg.dontAskAgainThisSession());
        QVERIFY(!dlg.makeThisTheNewDefault());
    }
};

QTEST_MAIN(TestPasteSpecialDialog)
#include "test_paste_special_dialog.moc"
