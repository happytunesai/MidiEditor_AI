#include "FFXIVFixerDialog.h"

#include <QButtonGroup>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

FFXIVFixerDialog::FFXIVFixerDialog(const QJsonObject &analysis, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("FFXIV Channel Fixer"));
    setModal(true);
    setupUI(analysis);
    setMinimumWidth(520);
}

void FFXIVFixerDialog::setupUI(const QJsonObject &analysis) {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // --- File Analysis Summary ---
    QGroupBox *infoGroup = new QGroupBox(tr("File Analysis"), this);
    QVBoxLayout *infoLayout = new QVBoxLayout(infoGroup);

    int trackCount      = analysis["trackCount"].toInt();
    int ffxivTrackCount  = analysis["ffxivTrackCount"].toInt();
    bool hasGuitar       = analysis["hasGuitar"].toBool();
    int totalPCs         = analysis["totalProgramChanges"].toInt();
    int autoTier         = analysis["autoDetectedTier"].toInt(2);
    QJsonArray guitarArr = analysis["guitarVariants"].toArray();
    QJsonArray percArr   = analysis["percussionTracks"].toArray();

    QString infoText = QString(
        "<b>%1</b> tracks detected, <b>%2</b> FFXIV instruments recognized<br>"
        "Existing program changes: <b>%3</b>")
        .arg(trackCount).arg(ffxivTrackCount).arg(totalPCs);

    if (hasGuitar) {
        QStringList gv;
        for (const auto &v : guitarArr) gv << v.toString();
        infoText += QString("<br>Guitar variants: <b>%1</b>").arg(gv.join(", "));
    }
    if (!percArr.isEmpty()) {
        QStringList pv;
        for (const auto &v : percArr) pv << v.toString();
        infoText += QString("<br>Percussion: <b>%1</b>").arg(pv.join(", "));
    }

    QString tierLabel;
    switch (autoTier) {
        case 3:  tierLabel = tr("Preserve (Minimal Changes)");  break;
        default: tierLabel = tr("Rebuild (Full Reassignment)");  break;
    }
    infoText += QString("<br><br>Auto-detected: <b>%1</b>").arg(tierLabel);

    QLabel *infoLabel = new QLabel(infoText, infoGroup);
    infoLabel->setWordWrap(true);
    infoLayout->addWidget(infoLabel);
    mainLayout->addWidget(infoGroup);

    // --- Tier Selection ---
    QGroupBox *tierGroup = new QGroupBox(tr("Select Action"), this);
    QVBoxLayout *tierLayout = new QVBoxLayout(tierGroup);

    _tierGroup = new QButtonGroup(this);

    // Rebuild (Full Reassignment)
    _tier2Radio = new QRadioButton(tr("Rebuild (Full Reassignment)"), tierGroup);
    tierLayout->addWidget(_tier2Radio);
    QString tier2Text = QString(
        tr("Full channel reassignment:\n"
           "• Maps each track to its matching channel (T0→CH0, T1→CH1, …)\n"
           "• Percussion tracks → CH9\n"
           "• Removes all %1 existing program changes\n"
           "• Moves all events to correct channels\n"
           "• Inserts new program changes at tick 0\n"
           "Best for: New or unconfigured MIDI files."))
        .arg(totalPCs);
    QLabel *tier2Desc = new QLabel(tier2Text, tierGroup);
    tier2Desc->setWordWrap(true);
    tier2Desc->setStyleSheet("QLabel { color: gray; font-size: 10px; margin-left: 20px; margin-bottom: 8px; }");
    tierLayout->addWidget(tier2Desc);

    // Preserve (Minimal Changes)
    _tier3Radio = new QRadioButton(tr("Preserve (Minimal Changes)"), tierGroup);
    tierLayout->addWidget(_tier3Radio);
    QString tier3Text = QString(
        tr("Minimal-invasive preservation:\n"
           "\u2022 Keeps all existing channel assignments\n"
           "\u2022 Non-guitar channels completely untouched\n"
           "\u2022 Only refreshes guitar program changes (tick 0 + switch points)\n"
           "\u2022 Auto-renames guitar tracks if first note differs from track name\n"
           "\u2022 Normalizes all note velocities to 127 (max)\n"
           "Best for: Already configured files needing a guitar touch-up."));
    QLabel *tier3Desc = new QLabel(tier3Text, tierGroup);
    tier3Desc->setWordWrap(true);
    tier3Desc->setStyleSheet("QLabel { color: gray; font-size: 10px; margin-left: 20px; margin-bottom: 8px; }");
    tierLayout->addWidget(tier3Desc);

    _tierGroup->addButton(_tier2Radio, 2);
    _tierGroup->addButton(_tier3Radio, 3);

    mainLayout->addWidget(tierGroup);

    // --- Buttons ---
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    _abortButton = new QPushButton(tr("Abort"), this);
    _continueButton = new QPushButton(tr("Continue"), this);
    _continueButton->setEnabled(false); // disabled until selection
    _continueButton->setDefault(true);

    buttonLayout->addWidget(_continueButton);
    buttonLayout->addWidget(_abortButton);
    mainLayout->addLayout(buttonLayout);

    // --- Connections ---
    connect(_tierGroup, &QButtonGroup::idToggled, this, &FFXIVFixerDialog::onSelectionChanged);
    connect(_continueButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(_abortButton,    &QPushButton::clicked, this, &QDialog::reject);

    // Pre-select the auto-detected tier
    if (autoTier == 3)
        _tier3Radio->setChecked(true);
    else
        _tier2Radio->setChecked(true);
}

int FFXIVFixerDialog::selectedTier() const {
    int id = _tierGroup->checkedId();
    return (id == 2 || id == 3) ? id : 0;
}

void FFXIVFixerDialog::onSelectionChanged() {
    _continueButton->setEnabled(_tierGroup->checkedId() >= 1);
}
