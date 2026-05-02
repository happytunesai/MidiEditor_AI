/*
 * MidiEditor AI
 *
 * PasteSpecialDialog \u2014 Phase 34 implementation.
 */

#include "PasteSpecialDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QRadioButton>
#include <QStringList>
#include <QVBoxLayout>

namespace {

QString formatDuration(int ms) {
    if (ms <= 0) return QStringLiteral("0:00");
    const int totalSec = ms / 1000;
    const int min = totalSec / 60;
    const int sec = totalSec % 60;
    return QStringLiteral("%1:%2.%3")
        .arg(min)
        .arg(sec, 2, 10, QChar('0'))
        .arg((ms % 1000) / 100);
}

QString summaryText(const PasteClipboardSummary &s) {
    QStringList trackNames;
    trackNames.reserve(s.sourceTracks.size());
    for (const auto &p : s.sourceTracks) {
        trackNames << (p.second.isEmpty()
                           ? QObject::tr("Track %1").arg(p.first)
                           : p.second);
    }
    QStringList chanStrings;
    chanStrings.reserve(s.distinctChannels.size());
    for (int c : s.distinctChannels) chanStrings << QString::number(c);

    return QObject::tr(
               "Clipboard: %1 event(s), %2 source track(s) (%3), "
               "%4 channel(s) (%5), %6 of music.")
        .arg(s.totalEvents)
        .arg(s.sourceTrackCount)
        .arg(trackNames.join(QStringLiteral(", ")))
        .arg(s.distinctChannels.size())
        .arg(chanStrings.join(QStringLiteral(", ")))
        .arg(formatDuration(s.approxDurationMs));
}

} // namespace

PasteSpecialDialog::PasteSpecialDialog(const PasteClipboardSummary &summary,
                                       PasteAssignment defaultAssignment,
                                       QWidget *parent)
    : QDialog(parent),
      _radioNewTracks(nullptr),
      _radioPreserveMapping(nullptr),
      _radioCurrentTarget(nullptr),
      _dontAskAgain(nullptr),
      _makeDefault(nullptr),
      _summaryLabel(nullptr) {
    setWindowTitle(tr("Paste Special"));
    setModal(true);

    auto *root = new QVBoxLayout(this);

    _summaryLabel = new QLabel(summaryText(summary), this);
    _summaryLabel->setWordWrap(true);
    QFont f = _summaryLabel->font();
    f.setItalic(true);
    _summaryLabel->setFont(f);
    root->addWidget(_summaryLabel);

    auto *group = new QGroupBox(tr("Where should the pasted events go?"), this);
    auto *vbox = new QVBoxLayout(group);

    _radioNewTracks = new QRadioButton(
        tr("Create new tracks for pasted events  (recommended)"), group);
    _radioNewTracks->setToolTip(tr(
        "Adds one new track per source track. Channels are preserved. "
        "Best for cross-instance pastes \u2014 you can audition the pasted "
        "material in isolation before merging."));
    vbox->addWidget(_radioNewTracks);

    _radioPreserveMapping = new QRadioButton(
        tr("Preserve source track + channel mapping (1:1)"), group);
    _radioPreserveMapping->setToolTip(tr(
        "Reuses an existing target track when its name matches a source "
        "track. Otherwise creates a new track with the source's name. "
        "Channels are preserved."));
    vbox->addWidget(_radioPreserveMapping);

    _radioCurrentTarget = new QRadioButton(
        tr("Paste to current edit track + channel  (legacy)"), group);
    _radioCurrentTarget->setToolTip(tr(
        "Collapses every regular event onto the current edit target. "
        "Meta events keep their dedicated channels (16/17/18)."));
    vbox->addWidget(_radioCurrentTarget);

    auto *buttonGroup = new QButtonGroup(this);
    buttonGroup->addButton(_radioNewTracks, static_cast<int>(PasteAssignment::NewTracksPerSource));
    buttonGroup->addButton(_radioPreserveMapping, static_cast<int>(PasteAssignment::PreserveSourceMapping));
    buttonGroup->addButton(_radioCurrentTarget, static_cast<int>(PasteAssignment::CurrentEditTarget));

    switch (defaultAssignment) {
    case PasteAssignment::PreserveSourceMapping: _radioPreserveMapping->setChecked(true); break;
    case PasteAssignment::CurrentEditTarget:     _radioCurrentTarget->setChecked(true); break;
    case PasteAssignment::NewTracksPerSource:    // fall-through
    default:                                     _radioNewTracks->setChecked(true); break;
    }

    root->addWidget(group);

    _dontAskAgain = new QCheckBox(
        tr("Don't ask again \u2014 use this for the rest of the session"), this);
    root->addWidget(_dontAskAgain);

    _makeDefault = new QCheckBox(
        tr("Make this the new default (persists across launches)"), this);
    _makeDefault->setEnabled(false);
    root->addWidget(_makeDefault);

    connect(_dontAskAgain, &QCheckBox::toggled,
            this, &PasteSpecialDialog::updateMakeDefaultEnabled);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

PasteAssignment PasteSpecialDialog::chosenAssignment() const {
    if (_radioPreserveMapping && _radioPreserveMapping->isChecked()) {
        return PasteAssignment::PreserveSourceMapping;
    }
    if (_radioCurrentTarget && _radioCurrentTarget->isChecked()) {
        return PasteAssignment::CurrentEditTarget;
    }
    return PasteAssignment::NewTracksPerSource;
}

bool PasteSpecialDialog::dontAskAgainThisSession() const {
    return _dontAskAgain && _dontAskAgain->isChecked();
}

bool PasteSpecialDialog::makeThisTheNewDefault() const {
    return _makeDefault && _makeDefault->isChecked() && _makeDefault->isEnabled();
}

void PasteSpecialDialog::updateMakeDefaultEnabled() {
    if (!_makeDefault) return;
    const bool dontAsk = _dontAskAgain && _dontAskAgain->isChecked();
    _makeDefault->setEnabled(dontAsk);
    if (!dontAsk) _makeDefault->setChecked(false);
}
