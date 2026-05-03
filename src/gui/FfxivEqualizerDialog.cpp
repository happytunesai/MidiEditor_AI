/*
 * MidiEditor AI — FFXIV SoundFont Equalizer dialog (Phase 39)
 * See FfxivEqualizerDialog.h for design notes.
 */

#include "FfxivEqualizerDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>
#include <QtMath>

#ifdef FLUIDSYNTH_SUPPORT
#  include "../midi/FluidSynthEngine.h"
#endif

// ---------------------------------------------------------------------------
// Slider <-> percent helpers (slider is integer 0..200, value is 0.00..2.00)
// ---------------------------------------------------------------------------
static int gainToSlider(float g)   { return qBound(0, qRound(g * 100.0f), 200); }
static float sliderToGain(int v)   { return qBound(0.0f, v / 100.0f, 2.0f); }

// ---------------------------------------------------------------------------
FfxivEqualizerDialog::FfxivEqualizerDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("FFXIV SoundFont Equalizer"));
    setModal(true);
    resize(720, 640);

    auto *svc = FfxivEqualizerService::instance();
    _initialSlots  = svc->currentSlotsSnapshot();
    _initialMaster = svc->masterGain();
    _initialPreset = svc->activePresetName();

    buildUi();
    seedRowsFromService();
    refreshPresetCombo();
}

// ---------------------------------------------------------------------------
void FfxivEqualizerDialog::buildUi() {
    auto *root = new QVBoxLayout(this);

    // Header strip ----------------------------------------------------------
    auto *headerBox = new QGroupBox(tr("Preset"), this);
    auto *headerLay = new QVBoxLayout(headerBox);

    auto *presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel(tr("Active:"), this));
    _presetCombo = new QComboBox(this);
    presetRow->addWidget(_presetCombo, 1);
    _saveAsBtn = new QPushButton(tr("Save As..."), this);
    _deleteBtn = new QPushButton(tr("Delete"), this);
    _resetDefaultsBtn = new QPushButton(tr("Reset to Built-in"), this);
    presetRow->addWidget(_saveAsBtn);
    presetRow->addWidget(_deleteBtn);
    presetRow->addWidget(_resetDefaultsBtn);
    headerLay->addLayout(presetRow);

    auto *masterRow = new QHBoxLayout();
    masterRow->addWidget(new QLabel(tr("Master:"), this));
    _masterSlider = new QSlider(Qt::Horizontal, this);
    _masterSlider->setRange(0, 200);
    _masterSlider->setValue(100);
    _masterSlider->setTickPosition(QSlider::TicksBelow);
    _masterSlider->setTickInterval(50);
    masterRow->addWidget(_masterSlider, 1);
    _masterSpin = new QDoubleSpinBox(this);
    _masterSpin->setRange(0.0, 2.0);
    _masterSpin->setDecimals(2);
    _masterSpin->setSingleStep(0.05);
    _masterSpin->setValue(1.0);
    masterRow->addWidget(_masterSpin);
    headerLay->addLayout(masterRow);

    auto *searchRow = new QHBoxLayout();
    searchRow->addWidget(new QLabel(tr("Filter:"), this));
    _searchEdit = new QLineEdit(this);
    _searchEdit->setPlaceholderText(tr("Type to filter instruments..."));
    searchRow->addWidget(_searchEdit, 1);
    headerLay->addLayout(searchRow);

    root->addWidget(headerBox);

    // Scrolling list of instrument rows ------------------------------------
    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    auto *listHost = new QWidget(_scrollArea);
    auto *listLay  = new QVBoxLayout(listHost);
    listLay->setContentsMargins(4, 4, 4, 4);
    listLay->setSpacing(2);

    const auto &table = FfxivEqualizerService::knownInstruments();
    _rows.reserve(table.size());
    for (const auto &pair : table) {
        RowControls rc;
        rc.name    = pair.first;
        rc.program = pair.second;
        rc.isDrum  = (rc.program == FfxivEqualizerService::kDrumKitProgram);

        rc.row = new QWidget(listHost);
        auto *rl = new QHBoxLayout(rc.row);
        rl->setContentsMargins(4, 2, 4, 2);

        auto *nameLbl = new QLabel(rc.name, rc.row);
        nameLbl->setMinimumWidth(180);
        rl->addWidget(nameLbl);

        rc.slider = new QSlider(Qt::Horizontal, rc.row);
        rc.slider->setRange(0, 200);
        rc.slider->setTickPosition(QSlider::TicksBelow);
        rc.slider->setTickInterval(50);
        rc.slider->setMinimumWidth(180);
        rl->addWidget(rc.slider, 1);

        rc.spin = new QDoubleSpinBox(rc.row);
        rc.spin->setRange(0.0, 2.0);
        rc.spin->setDecimals(2);
        rc.spin->setSingleStep(0.05);
        rl->addWidget(rc.spin);

        rc.mute = new QCheckBox(tr("Mute"), rc.row);
        rl->addWidget(rc.mute);

        rc.reset = new QPushButton(tr("Reset"), rc.row);
        rl->addWidget(rc.reset);

        rc.preview = new QPushButton(tr("▶ Preview"), rc.row);
        rc.preview->setToolTip(tr("Plays C-D-E-G with this instrument so you "
                                   "can audition the gain change live."));
        rl->addWidget(rc.preview);

        listLay->addWidget(rc.row);
        _rows.append(rc);
        wireRow(_rows.last());
    }

    listLay->addStretch(1);
    _scrollArea->setWidget(listHost);
    root->addWidget(_scrollArea, 1);

    // Footer ---------------------------------------------------------------
    auto *footer = new QDialogButtonBox(this);
    auto *okBtn     = footer->addButton(QDialogButtonBox::Ok);
    auto *cancelBtn = footer->addButton(QDialogButtonBox::Cancel);
    okBtn->setText(tr("Apply"));
    root->addWidget(footer);

    // Wire master controls -------------------------------------------------
    connect(_masterSlider, &QSlider::valueChanged, this,
            &FfxivEqualizerDialog::onMasterChanged);
    connect(_masterSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
        if (_suspendRowSignals) return;
        _masterSlider->setValue(qRound(v * 100.0));
    });

    connect(_searchEdit, &QLineEdit::textChanged, this,
            &FfxivEqualizerDialog::onSearchChanged);
    connect(_presetCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &FfxivEqualizerDialog::onPresetChanged);
    connect(_saveAsBtn, &QPushButton::clicked, this,
            &FfxivEqualizerDialog::onSavePresetAs);
    connect(_deleteBtn, &QPushButton::clicked, this,
            &FfxivEqualizerDialog::onDeletePreset);
    connect(_resetDefaultsBtn, &QPushButton::clicked, this,
            &FfxivEqualizerDialog::onResetToBuiltin);

    // Defensive: refresh the preset combo whenever the service mutates its
    // preset list (save, delete, external load), so the dropdown never lags
    // behind on-disk state.
    connect(FfxivEqualizerService::instance(),
            &FfxivEqualizerService::presetsChanged,
            this, &FfxivEqualizerDialog::refreshPresetCombo);

    connect(okBtn, &QPushButton::clicked, this, [this]() {
        // Apply = persist current state to active preset (in-place save).
        // Built-in preset can't be overwritten; the user has to "Save As"
        // first or the change stays only in the live mixer until next load.
        auto *svc = FfxivEqualizerService::instance();
        if (svc->activePresetName() != FfxivEqualizerService::builtinPresetName()) {
            svc->savePresetAs(svc->activePresetName());
        }
        accept();
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

// ---------------------------------------------------------------------------
void FfxivEqualizerDialog::wireRow(RowControls &rc) {
    QSlider *slider = rc.slider;
    QDoubleSpinBox *spin = rc.spin;
    QCheckBox *mute = rc.mute;
    int prog = rc.program;

    // Slider → service & spinbox
    connect(slider, &QSlider::valueChanged, this, [this, prog, spin](int v) {
        if (_suspendRowSignals) return;
        float g = sliderToGain(v);
        _suspendRowSignals = true;
        spin->setValue(g);
        _suspendRowSignals = false;
        FfxivEqualizerService::instance()->setProgramGain(prog, g);
    });
    // Spinbox → slider (slider already pushes to service)
    connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this, slider](double v) {
        if (_suspendRowSignals) return;
        slider->setValue(gainToSlider(static_cast<float>(v)));
    });
    // Mute toggle → service
    connect(mute, &QCheckBox::toggled, this, [this, prog](bool on) {
        if (_suspendRowSignals) return;
        FfxivEqualizerService::instance()->setProgramMuted(prog, on);
    });
    // Reset row → unity (1.0, unmuted). The user has Reset to Built-in
    // for getting curated FFXIV defaults across all rows.
    connect(rc.reset, &QPushButton::clicked, this, [this, prog]() {
        FfxivEqualizerService::instance()->setProgramGain(prog, 1.0f);
        FfxivEqualizerService::instance()->setProgramMuted(prog, false);
        seedRowsFromService();
    });
    // Live test-tone preview — capture values by copy, not a raw list pointer.
    bool isDrumRow = rc.isDrum;
    connect(rc.preview, &QPushButton::clicked, this, [this, prog, isDrumRow]() {
#ifdef FLUIDSYNTH_SUPPORT
        auto *engine = FluidSynthEngine::instance();
        if (!engine) return;
        engine->playPreviewArpeggio(isDrumRow ? 0 : prog, isDrumRow);
#endif
    });
}

// ---------------------------------------------------------------------------
void FfxivEqualizerDialog::previewRow(const RowControls &rc) {
#ifdef FLUIDSYNTH_SUPPORT
    auto *engine = FluidSynthEngine::instance();
    if (!engine) return;
    // For the synthetic Drum Kit slot we don't have a real GM PC \u2014
    // playPreviewArpeggio() routes to CH9 with isDrum=true and uses
    // GM kick/snare/hat/crash regardless of the program number we pass.
    int prog = rc.isDrum ? 0 : rc.program;
    engine->playPreviewArpeggio(prog, rc.isDrum);
#else
    Q_UNUSED(rc);
#endif
}

// ---------------------------------------------------------------------------
void FfxivEqualizerDialog::seedRowsFromService() {
    auto *svc = FfxivEqualizerService::instance();
    auto snap = svc->currentSlotsSnapshot();

    _suspendRowSignals = true;
    _masterSlider->setValue(qRound(svc->masterGain() * 100.0f));
    _masterSpin->setValue(svc->masterGain());

    for (auto &rc : _rows) {
        FfxivEqualizerService::Slot slot;
        auto it = snap.constFind(rc.program);
        if (it != snap.constEnd()) slot = it.value();
        rc.slider->setValue(gainToSlider(slot.gain));
        rc.spin->setValue(slot.gain);
        rc.mute->setChecked(slot.muted);
    }
    _suspendRowSignals = false;
}

// ---------------------------------------------------------------------------
void FfxivEqualizerDialog::refreshPresetCombo() {
    _suspendRowSignals = true;
    _presetCombo->clear();
    auto *svc = FfxivEqualizerService::instance();
    QStringList names = svc->allPresetNames();
    _presetCombo->addItems(names);
    int idx = names.indexOf(svc->activePresetName());
    if (idx >= 0) _presetCombo->setCurrentIndex(idx);
    _deleteBtn->setEnabled(svc->activePresetName()
                           != FfxivEqualizerService::builtinPresetName());
    _suspendRowSignals = false;
}

// ---------------------------------------------------------------------------
void FfxivEqualizerDialog::onPresetChanged(int idx) {
    if (_suspendRowSignals) return;
    if (idx < 0) return;
    QString name = _presetCombo->itemText(idx);
    FfxivEqualizerService::instance()->setActivePreset(name);
    seedRowsFromService();
    _deleteBtn->setEnabled(name != FfxivEqualizerService::builtinPresetName());
}

// ---------------------------------------------------------------------------
void FfxivEqualizerDialog::onSavePresetAs() {
    bool ok = false;
    QString name = QInputDialog::getText(
        this, tr("Save Equalizer Preset"),
        tr("Preset name:"), QLineEdit::Normal,
        FfxivEqualizerService::instance()->activePresetName(), &ok);
    if (!ok) return;
    name = name.trimmed();
    if (name.isEmpty()) return;
    if (name == FfxivEqualizerService::builtinPresetName()) {
        QMessageBox::warning(this, tr("Reserved Name"),
            tr("'%1' is reserved for the built-in preset.").arg(name));
        return;
    }
    const bool saved = FfxivEqualizerService::instance()->savePresetAs(name);
    refreshPresetCombo();
    if (saved) {
        QMessageBox::information(this, tr("Preset Saved"),
            tr("Preset '%1' has been saved.").arg(name));
    } else {
        QMessageBox::warning(this, tr("Save Failed"),
            tr("Could not save preset '%1'. Please try a different name.")
                .arg(name));
    }
}

// ---------------------------------------------------------------------------
void FfxivEqualizerDialog::onDeletePreset() {
    auto *svc = FfxivEqualizerService::instance();
    QString name = svc->activePresetName();
    if (name == FfxivEqualizerService::builtinPresetName()) return;
    int btn = QMessageBox::question(this, tr("Delete Preset"),
        tr("Delete preset '%1'?").arg(name));
    if (btn != QMessageBox::Yes) return;
    svc->deletePreset(name);
    refreshPresetCombo();
    seedRowsFromService();
}

// ---------------------------------------------------------------------------
void FfxivEqualizerDialog::onMasterChanged(int sliderValue) {
    if (_suspendRowSignals) return;
    float g = sliderToGain(sliderValue);
    _suspendRowSignals = true;
    _masterSpin->setValue(g);
    _suspendRowSignals = false;
    FfxivEqualizerService::instance()->setMasterGain(g);
}

// ---------------------------------------------------------------------------
void FfxivEqualizerDialog::onSearchChanged(const QString &text) {
    QString needle = text.trimmed().toLower();
    for (auto &rc : _rows) {
        bool match = needle.isEmpty()
                     || rc.name.toLower().contains(needle);
        rc.row->setVisible(match);
    }
}

// ---------------------------------------------------------------------------
void FfxivEqualizerDialog::onResetToBuiltin() {
    int btn = QMessageBox::question(this, tr("Reset to Built-in"),
        tr("Reset all instruments to the built-in FFXIV Default values?"));
    if (btn != QMessageBox::Yes) return;
    FfxivEqualizerService::instance()->resetToBuiltinDefault();
    seedRowsFromService();
}

// ---------------------------------------------------------------------------
void FfxivEqualizerDialog::reject() {
    // Roll the live mixer back to the snapshot taken on construction so
    // Cancel really does cancel \u2014 even though sliders are live.
    auto *svc = FfxivEqualizerService::instance();
    if (svc->activePresetName() != _initialPreset) {
        svc->setActivePreset(_initialPreset);
    } else {
        // Same preset, reapply our snapshot directly.
        for (auto &rc : _rows) {
            auto it = _initialSlots.constFind(rc.program);
            if (it != _initialSlots.constEnd()) {
                svc->setProgramGain(rc.program, it->gain);
                svc->setProgramMuted(rc.program, it->muted);
            }
        }
        svc->setMasterGain(_initialMaster);
    }
    QDialog::reject();
}
