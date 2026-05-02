/*
 * MidiEditor AI
 *
 * TempoConversionDialog — Phase 33 implementation.
 */

#include "TempoConversionDialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QVBoxLayout>

#include "../MidiEvent/TempoChangeEvent.h"
#include "../midi/MidiFile.h"

namespace {

double detectSourceBpm(MidiFile *file) {
    if (!file) {
        return 120.0;
    }
    QMultiMap<int, MidiEvent *> *events = file->tempoEvents();
    if (!events || events->isEmpty()) {
        return 120.0;
    }
    int bestTick = -1;
    int bpm = 120;
    for (auto it = events->begin(); it != events->end(); ++it) {
        if (it.key() < 0) {
            continue;
        }
        if (bestTick < 0 || it.key() < bestTick) {
            if (auto *tc = dynamic_cast<TempoChangeEvent *>(it.value())) {
                bestTick = it.key();
                bpm = tc->beatsPerQuarter();
            }
        }
    }
    return static_cast<double>(bpm);
}

} // namespace

TempoConversionDialog::TempoConversionDialog(MidiFile *file,
                                             const TempoConversionScopeHint &hint,
                                             QWidget *parent)
    : QDialog(parent), _file(file), _hint(hint) {
    setWindowTitle(tr("Convert Tempo (Preserve Duration)"));
    setMinimumWidth(520);
    buildUi();

    const double detected = detectSourceBpm(file);
    _sourceBpm->setValue(detected);
    _targetBpm->setValue(detected);

    // Pre-set scope from hint.
    int idx = 0;
    switch (_hint.scope) {
    case TempoConversionScope::WholeProject:    idx = 0; break;
    case TempoConversionScope::SelectedTracks:  idx = 1; break;
    case TempoConversionScope::SelectedChannels: idx = 2; break;
    case TempoConversionScope::SelectedEvents:  idx = 3; break;
    }
    _scopeCombo->setCurrentIndex(idx);

    _previewTimer = new QTimer(this);
    _previewTimer->setSingleShot(true);
    _previewTimer->setInterval(100);
    connect(_previewTimer, &QTimer::timeout, this, &TempoConversionDialog::runPreviewNow);

    connect(_sourceBpm, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &TempoConversionDialog::schedulePreview);
    connect(_targetBpm, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &TempoConversionDialog::schedulePreview);
    connect(_scopeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TempoConversionDialog::schedulePreview);
    connect(_modeReplaceFixed, &QRadioButton::toggled,
            this, &TempoConversionDialog::schedulePreview);
    connect(_modeScaleMap, &QRadioButton::toggled,
            this, &TempoConversionDialog::schedulePreview);
    connect(_modeEventsOnly, &QRadioButton::toggled,
            this, &TempoConversionDialog::schedulePreview);

    runPreviewNow();
}

void TempoConversionDialog::buildUi() {
    auto *root = new QVBoxLayout(this);

    // BPM row
    auto *form = new QFormLayout();
    _sourceBpm = new QDoubleSpinBox(this);
    _sourceBpm->setRange(1.0, 999.0);
    _sourceBpm->setDecimals(2);
    _sourceBpm->setSingleStep(1.0);
    form->addRow(tr("Source BPM (detected):"), _sourceBpm);

    _targetBpm = new QDoubleSpinBox(this);
    _targetBpm->setRange(1.0, 999.0);
    _targetBpm->setDecimals(2);
    _targetBpm->setSingleStep(1.0);
    form->addRow(tr("Target BPM:"), _targetBpm);

    _scopeCombo = new QComboBox(this);
    _scopeCombo->addItem(tr("Whole project"));
    _scopeCombo->addItem(tr("Selected tracks"));
    _scopeCombo->addItem(tr("Selected channels"));
    _scopeCombo->addItem(tr("Selected events"));
    form->addRow(tr("Apply to:"), _scopeCombo);
    root->addLayout(form);

    // Hint summary (shows what was pre-filled from the launching context menu).
    QString hintText;
    if (!_hint.trackIds.isEmpty()) {
        QStringList ids;
        for (int t : _hint.trackIds) ids << QString::number(t);
        hintText = tr("Pre-filled tracks: %1").arg(ids.join(QStringLiteral(", ")));
    } else if (!_hint.channelIds.isEmpty()) {
        QStringList ids;
        for (int c : _hint.channelIds) ids << QString::number(c);
        hintText = tr("Pre-filled channels: %1").arg(ids.join(QStringLiteral(", ")));
    } else if (!_hint.selectedEventPtrs.isEmpty()) {
        hintText = tr("Pre-filled with %1 selected event(s).").arg(_hint.selectedEventPtrs.size());
    }
    if (!hintText.isEmpty()) {
        auto *hintLabel = new QLabel(hintText, this);
        hintLabel->setStyleSheet(QStringLiteral("QLabel { color: #4a90e2; font-style: italic; }"));
        root->addWidget(hintLabel);
    }

    // Tempo-map mode group
    auto *modeBox = new QGroupBox(tr("Tempo map handling"), this);
    auto *modeLayout = new QVBoxLayout(modeBox);
    _modeReplaceFixed = new QRadioButton(tr("Replace with single fixed tempo at tick 0"), modeBox);
    _modeReplaceFixed->setChecked(true);
    _modeScaleMap = new QRadioButton(tr("Scale existing tempo map (preserve curves)"), modeBox);
    _modeEventsOnly = new QRadioButton(tr("Scale events only (leave tempo map untouched)"), modeBox);
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->addButton(_modeReplaceFixed);
    modeGroup->addButton(_modeScaleMap);
    modeGroup->addButton(_modeEventsOnly);
    modeLayout->addWidget(_modeReplaceFixed);
    modeLayout->addWidget(_modeScaleMap);
    modeLayout->addWidget(_modeEventsOnly);
    root->addWidget(modeBox);

    // Preview
    _previewLabel = new QLabel(tr("(preview)"), this);
    _previewLabel->setWordWrap(true);
    _previewLabel->setStyleSheet(QStringLiteral(
        "QLabel { padding: 6px; background-color: rgba(0,0,0,40); border-radius: 4px; }"));
    root->addWidget(_previewLabel);

    _warningLabel = new QLabel(QString(), this);
    _warningLabel->setWordWrap(true);
    _warningLabel->setStyleSheet(QStringLiteral("QLabel { color: #c97a00; }"));
    _warningLabel->hide();
    root->addWidget(_warningLabel);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Convert"));
    connect(buttons, &QDialogButtonBox::accepted, this, &TempoConversionDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

TempoConversionOptions TempoConversionDialog::currentOptions() const {
    TempoConversionOptions opts;
    opts.sourceBpm = _sourceBpm->value();
    opts.targetBpm = _targetBpm->value();
    switch (_scopeCombo->currentIndex()) {
    case 0: opts.scope = TempoConversionScope::WholeProject; break;
    case 1: opts.scope = TempoConversionScope::SelectedTracks; break;
    case 2: opts.scope = TempoConversionScope::SelectedChannels; break;
    case 3: opts.scope = TempoConversionScope::SelectedEvents; break;
    default: opts.scope = TempoConversionScope::WholeProject; break;
    }
    if (_modeReplaceFixed->isChecked()) {
        opts.tempoMode = TempoConversionTempoMode::ReplaceFixed;
    } else if (_modeScaleMap->isChecked()) {
        opts.tempoMode = TempoConversionTempoMode::ScaleTempoMap;
    } else {
        opts.tempoMode = TempoConversionTempoMode::EventsOnly;
    }
    opts.trackIds = _hint.trackIds;
    opts.channelIds = _hint.channelIds;
    opts.selectedEventPtrs = _hint.selectedEventPtrs;
    opts.includeTempo = true;
    opts.includeTimeSig = true;
    opts.includeMeta = true;
    return opts;
}

QString TempoConversionDialog::formatDuration(qint64 ms) {
    if (ms < 0) ms = 0;
    const qint64 totalSec = ms / 1000;
    const qint64 minutes = totalSec / 60;
    const qint64 seconds = totalSec % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

void TempoConversionDialog::schedulePreview() {
    if (_previewTimer) {
        _previewTimer->start();
    }
}

void TempoConversionDialog::runPreviewNow() {
    const TempoConversionOptions opts = currentOptions();
    const TempoConversionResult r = TempoConversionService::preview(_file, opts);
    if (!r.ok) {
        _previewLabel->setText(tr("Error: %1").arg(r.error));
        _warningLabel->hide();
        return;
    }
    QString text = tr("Scale: %1\xC3\x97 \xE2\x80\x94 %2 events affected"
                      "\nDuration: %3 \xE2\x86\x92 %4")
                       .arg(r.scaleFactor, 0, 'f', 4)
                       .arg(r.affectedEvents)
                       .arg(formatDuration(r.oldDurationMs))
                       .arg(formatDuration(r.newDurationMs));
    if (r.tempoEventsRemoved > 0 || r.tempoEventsInserted > 0) {
        text += tr("\nTempo events: \xE2\x88\x92%1 / +%2")
                    .arg(r.tempoEventsRemoved)
                    .arg(r.tempoEventsInserted);
    }
    _previewLabel->setText(text);
    if (!r.warning.isEmpty()) {
        _warningLabel->setText(r.warning);
        _warningLabel->show();
    } else {
        _warningLabel->hide();
    }
}

void TempoConversionDialog::onAccept() {
    const TempoConversionOptions opts = currentOptions();
    const TempoConversionResult r = TempoConversionService::convert(_file, opts);
    if (!r.ok) {
        _previewLabel->setText(tr("Error: %1").arg(r.error));
        return;
    }
    accept();
}
