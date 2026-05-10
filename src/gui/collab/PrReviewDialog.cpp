/*
 * MidiEditor AI - PrReviewDialog implementation.
 */

#include "PrReviewDialog.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "../../MidiEvent/MidiEvent.h"
#include "../../collab/CollabService.h"
#include "../../collab/PrApply.h"
#include "../../midi/MidiFile.h"

PrReviewDialog::PrReviewDialog(const PrBundle &bundle, MidiFile *targetFile,
                                Mode mode, QWidget *parent)
    : QDialog(parent), _bundle(bundle), _file(targetFile), _mode(mode) {
    QString titlePrefix = _mode == Mode::ReviewApplied
        ? tr("Review AI changes — %1")
        : tr("Review PR — %1");
    setWindowTitle(titlePrefix.arg(bundle.message));
    setModal(true);
    resize(620, 480);

    QJsonArray hunkArr = _bundle.hunks;
    for (const QJsonValue &v : hunkArr) {
        _hunks.append(v.toObject());
    }

    buildLayout();
}

QString PrReviewDialog::formatHunkLabel(const QJsonObject &hunk) const {
    QJsonObject scope = hunk.value(QStringLiteral("scope")).toObject();
    int ch = scope.value(QStringLiteral("channel")).toInt();
    int trk = scope.value(QStringLiteral("track")).toInt();
    int mStart = scope.value(QStringLiteral("measureStart")).toInt();
    int mEnd = scope.value(QStringLiteral("measureEnd")).toInt();
    int addedN = hunk.value(QStringLiteral("added")).toArray().size();
    int removedN = hunk.value(QStringLiteral("removed")).toArray().size();
    int modifiedN = hunk.value(QStringLiteral("modified")).toArray().size();

    QString measureLabel = (mStart == mEnd)
        ? tr("m. %1").arg(mStart + 1)
        : tr("m. %1–%2").arg(mStart + 1).arg(mEnd + 1);

    QString base = QStringLiteral("ch %1  trk %2  %3   +%4  ⋅%5  ~%6")
        .arg(ch).arg(trk).arg(measureLabel)
        .arg(addedN).arg(removedN).arg(modifiedN);

    // Pre-flight applicability check: how many of the hunk's `removed` and
    // `modified.before` events still exist in the current file? This tells
    // the user whether the hunk will apply cleanly or whether some events
    // will be skipped because the file has diverged since the bundle was
    // created.
    int preexistingExpected = 0;
    int preexistingFound = 0;
    QJsonArray removed = hunk.value(QStringLiteral("removed")).toArray();
    for (const QJsonValue &v : removed) {
        preexistingExpected++;
        if (PrApply::findMatchingEvent(_file, v.toObject())) preexistingFound++;
    }
    QJsonArray modified = hunk.value(QStringLiteral("modified")).toArray();
    for (const QJsonValue &v : modified) {
        preexistingExpected++;
        if (PrApply::findMatchingEvent(_file, v.toObject().value(QStringLiteral("before")).toObject()))
            preexistingFound++;
    }

    if (preexistingExpected == 0) {
        // Pure-add hunk — always applies cleanly.
        return base;
    }
    if (preexistingFound == preexistingExpected) {
        return base + QStringLiteral("   ✓");
    }
    return base + QStringLiteral("   ⚠ %1/%2 found")
                      .arg(preexistingFound).arg(preexistingExpected);
}

bool PrReviewDialog::sessionMatchesTarget() const {
    QString targetSession = CollabService::instance()->sessionId();
    if (targetSession.isEmpty()) return true;  // target file not initialized — can't say
    return _bundle.sessionId == targetSession;
}

void PrReviewDialog::buildLayout() {
    QVBoxLayout *root = new QVBoxLayout(this);

    // ---- Header ---------------------------------------------------
    QString tsStr = _bundle.timestamp > 0
        ? QDateTime::fromSecsSinceEpoch(_bundle.timestamp).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
        : tr("(unknown)");
    QString sessionPrefix = _bundle.sessionId.left(8);
    QString sessionStatus;
    if (sessionMatchesTarget()) {
        sessionStatus = tr("<span style='color: #66cc66;'>matches your file ✓</span>");
    } else {
        sessionStatus = tr("<span style='color: #e0a020;'>different session ⚠</span>");
    }

    // Parent-hash status: does the bundle's parent commit match our
    // current head? If yes, the hunks were computed against the same
    // state we have — they should apply cleanly. If no, we've moved
    // forward (or diverged) since the bundle was created and some
    // hunks may not apply.
    QString parentPrefix = _bundle.parentHash.left(8);
    QString currentHead = CollabService::instance()->currentHead();
    QString parentStatus;
    if (_bundle.parentHash.isEmpty()) {
        parentStatus = tr("<span style='color: gray;'>(initial commit)</span>");
    } else if (_bundle.parentHash == currentHead) {
        parentStatus = tr("<span style='color: #66cc66;'>matches your current head ✓</span>");
    } else {
        parentStatus = tr("<span style='color: #e0a020;'>different from your current head ⚠ "
                          "— bundle is from an older or diverged version</span>");
    }

    QString headerHtml = tr(
        "<b>From:</b> %1 &nbsp;&nbsp; <b>Created:</b> %2<br>"
        "<b>Session:</b> <code>%3…</code> &nbsp; %4<br>"
        "<b>Parent:</b> <code>%5%6</code> &nbsp; %7<br>"
        "<b>Message:</b> %8")
        .arg(_bundle.author.toHtmlEscaped(),
             tsStr,
             sessionPrefix.toHtmlEscaped(),
             sessionStatus,
             parentPrefix.toHtmlEscaped(),
             _bundle.parentHash.isEmpty() ? QString() : QStringLiteral("…"),
             parentStatus,
             _bundle.message.toHtmlEscaped());
    QLabel *header = new QLabel(headerHtml, this);
    header->setWordWrap(true);
    header->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(header);

    if (!_bundle.parentHash.isEmpty() && _bundle.parentHash != currentHead) {
        QLabel *hashWarn = new QLabel(tr(
            "<i>Some hunks may not apply cleanly because the file has changed since "
            "this PR was created. Hunks marked <b>⚠ N/M found</b> reference events "
            "that no longer exist; the rest will still apply.</i>"), this);
        hashWarn->setWordWrap(true);
        hashWarn->setStyleSheet("background: rgba(224,160,32,0.10); color: #e0a020; padding: 6px;");
        root->addWidget(hashWarn);
    }

    if (!sessionMatchesTarget()) {
        QLabel *warn = new QLabel(tr(
            "<i>This PR was created in a different collaboration session than your "
            "current file. Applying it will mix events from another session into "
            "this file. Proceed only if you know what you're doing.</i>"), this);
        warn->setWordWrap(true);
        warn->setStyleSheet("background: rgba(224,160,32,0.15); color: #e0a020; padding: 6px;");
        root->addWidget(warn);
    }

    QFrame *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep);

    // ---- Hunk list (scrollable) -----------------------------------
    QWidget *hunkContainer = new QWidget(this);
    QVBoxLayout *hunkLayout = new QVBoxLayout(hunkContainer);
    hunkLayout->setContentsMargins(0, 0, 0, 0);
    hunkLayout->setSpacing(2);

    if (_hunks.isEmpty()) {
        QLabel *empty = new QLabel(tr("<i>This PR contains no hunks (initial commit or no event-level changes).</i>"), hunkContainer);
        empty->setStyleSheet("color: gray; padding: 8px;");
        hunkLayout->addWidget(empty);
    } else {
        for (int i = 0; i < _hunks.size(); ++i) {
            QCheckBox *cb = new QCheckBox(formatHunkLabel(_hunks[i]), hunkContainer);
            cb->setChecked(true);
            _hunkChecks.append(cb);
            hunkLayout->addWidget(cb);
        }
    }
    hunkLayout->addStretch(1);

    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidget(hunkContainer);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll, 1);

    // ---- Bulk-select buttons -------------------------------------
    QHBoxLayout *bulkRow = new QHBoxLayout();
    QPushButton *all = new QPushButton(tr("Select all"), this);
    QPushButton *none = new QPushButton(tr("Select none"), this);
    QPushButton *inv = new QPushButton(tr("Invert"), this);
    bulkRow->addWidget(all);
    bulkRow->addWidget(none);
    bulkRow->addWidget(inv);
    bulkRow->addStretch(1);
    connect(all, &QPushButton::clicked, this, &PrReviewDialog::onSelectAll);
    connect(none, &QPushButton::clicked, this, &PrReviewDialog::onSelectNone);
    connect(inv, &QPushButton::clicked, this, &PrReviewDialog::onInvert);
    root->addLayout(bulkRow);

    // ---- Apply / Cancel ------------------------------------------
    QDialogButtonBox *box = new QDialogButtonBox(this);
    QString applyLabel = _mode == Mode::ReviewApplied
        ? tr("Keep selected (revert rest)")
        : tr("Apply selected hunks");
    QString cancelLabel = _mode == Mode::ReviewApplied
        ? tr("Revert all")
        : tr("Cancel");
    QPushButton *apply = box->addButton(applyLabel, QDialogButtonBox::AcceptRole);
    QPushButton *cancel = box->addButton(cancelLabel, QDialogButtonBox::RejectRole);
    connect(apply, &QPushButton::clicked, this, &PrReviewDialog::onApply);
    connect(cancel, &QPushButton::clicked, this, &PrReviewDialog::onCancel);
    apply->setEnabled(!_hunks.isEmpty());
    root->addWidget(box);
}

void PrReviewDialog::onSelectAll() {
    for (QCheckBox *cb : _hunkChecks) cb->setChecked(true);
}

void PrReviewDialog::onSelectNone() {
    for (QCheckBox *cb : _hunkChecks) cb->setChecked(false);
}

void PrReviewDialog::onInvert() {
    for (QCheckBox *cb : _hunkChecks) cb->setChecked(!cb->isChecked());
}

void PrReviewDialog::onApply() {
    QList<QJsonObject> selected;
    QList<QJsonObject> rejected;
    for (int i = 0; i < _hunkChecks.size(); ++i) {
        if (_hunkChecks[i]->isChecked()) selected.append(_hunks[i]);
        else rejected.append(_hunks[i]);
    }
    if (_mode == Mode::PreApply && selected.isEmpty()) {
        QMessageBox::information(this, tr("Nothing selected"),
            tr("Select at least one hunk to apply, or click Cancel."));
        return;
    }

    // Snapshot the user's decision so callers (e.g. the LAN returning-
    // peer flow) can forward it after exec() returns.
    _selectedHunks = selected;
    _rejectedHunks = rejected;

    if (_mode == Mode::PreApply) {
        // Standard incoming-PR path: apply the selected hunks.
        PrApply::Result r = PrApply::apply(_file, selected, _bundle.author, _bundle.message);
        if (!r.success) {
            QMessageBox::warning(this, tr("Apply failed"),
                tr("The PR could not be applied. %1").arg(r.warnings.value(0)));
            return;
        }
        QString summary = tr("Merged: +%1 added, ⋅%2 removed, ~%3 modified")
            .arg(r.addedCount).arg(r.removedCount).arg(r.modifiedCount);
        if (r.skippedCount > 0) {
            summary += tr(" (%1 skipped — see details below)").arg(r.skippedCount);
        }
        if (!r.warnings.isEmpty() && r.skippedCount > 0) {
            QMessageBox b(QMessageBox::Information, tr("PR merged"), summary, QMessageBox::Ok, this);
            b.setDetailedText(r.warnings.join(QStringLiteral("\n")));
            b.exec();
        } else {
            QMessageBox::information(this, tr("PR merged"), summary);
        }
        accept();
        return;
    }

    // ReviewApplied path: hunks are already in the file. We only need to
    // revert the hunks the user did NOT keep. If everything is selected,
    // there's nothing to do — close as accept.
    if (rejected.isEmpty()) {
        QMessageBox::information(this, tr("All changes kept"),
            tr("Kept all %1 hunks. The AI's changes remain in the file.").arg(selected.size()));
        accept();
        return;
    }

    PrApply::Result r = PrApply::applyInverted(_file, rejected, _bundle.author,
        tr("rejected %1 of %2 hunks").arg(rejected.size()).arg(_hunks.size()));
    if (!r.success) {
        QMessageBox::warning(this, tr("Revert failed"),
            tr("Some hunks could not be reverted. %1").arg(r.warnings.value(0)));
        return;
    }
    QString summary = tr("Kept %1 of %2 hunks. Reverted: -%3 added, +%4 removed, ~%5 modified")
        .arg(selected.size()).arg(_hunks.size())
        .arg(r.addedCount).arg(r.removedCount).arg(r.modifiedCount);
    if (r.skippedCount > 0) {
        summary += tr(" (%1 skipped)").arg(r.skippedCount);
    }
    QMessageBox::information(this, tr("AI changes reviewed"), summary);
    accept();
}

void PrReviewDialog::onCancel() {
    if (_mode == Mode::PreApply) {
        reject();
        return;
    }
    // ReviewApplied: revert ALL hunks — agent's run is undone in full.
    PrApply::Result r = PrApply::applyInverted(_file, _hunks, _bundle.author,
        tr("revert all AI changes"));
    if (!r.success) {
        QMessageBox::warning(this, tr("Revert failed"),
            tr("Some hunks could not be reverted. %1").arg(r.warnings.value(0)));
    }
    reject();
}
