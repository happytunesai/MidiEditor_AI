/*
 * MidiEditor AI - LoggingSettingsWidget implementation (v1.7.1 redesign).
 */

#include "LoggingSettingsWidget.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Severity bar colours, matching the column-headers used in the
// preview pane. Red for critical, orange for warnings, blue for
// info, gray for debug. The bar's filled cells map 1:1 to the
// severities a level lets through.
const char *kColorErrors   = "#f85149";  // red
const char *kColorWarnings = "#f0883e";  // orange
const char *kColorInfo     = "#58a6ff";  // blue
const char *kColorDebug    = "#9aa0a6";  // gray
const char *kColorOff      = "transparent";

// Build the inline HTML for a 4-cell colour bar showing which
// severities a given level enables. Each cell is a fixed-width
// monospace block coloured if the level includes it.
QString severityBarHtml(LoggingConfig::Level lvl) {
    auto cell = [](bool on, const char *colour) {
        QString style = on
            ? QStringLiteral("background:%1; border:1px solid %1;").arg(colour)
            : QStringLiteral("background:transparent; border:1px solid #444;");
        return QStringLiteral(
            "<span style='display:inline-block; width:14px; height:14px; "
            "vertical-align:middle; %1 margin-right:2px;'></span>")
            .arg(style);
    };
    bool enErrors   = (lvl >= LoggingConfig::Level::Errors);
    bool enWarnings = (lvl >= LoggingConfig::Level::Warnings);
    bool enInfo     = (lvl >= LoggingConfig::Level::Info);
    bool enDebug    = (lvl >= LoggingConfig::Level::Debug);
    return cell(enErrors, kColorErrors)
         + cell(enWarnings, kColorWarnings)
         + cell(enInfo, kColorInfo)
         + cell(enDebug, kColorDebug);
}

} // namespace

LoggingSettingsWidget::LoggingSettingsWidget(QWidget *parent)
    : SettingsWidget(tr("Logging"), parent) {

    QVBoxLayout *outer = new QVBoxLayout(this);
    setLayout(outer);
    setMinimumSize(440, 460);

    outer->addWidget(createInfoBox(
        tr("Controls how much detail the app writes to "
           "<i>midieditor_ai.log</i> next to the executable. "
           "Pick the lowest level that still surfaces what "
           "you're investigating &mdash; <i>Debug</i> can produce hundreds "
           "of lines per second on a busy session.<br><br>"
           "These rules apply to the whole app (Qt internals, MIDI engine, "
           "AI runner, collaboration, &hellip;). For just-collab verbose "
           "logging use <i>Settings &rarr; Collaboration &rarr; Enable "
           "verbose collaboration logging</i> instead.")));

    outer->addWidget(separator());

    // ---- Level stack: 5 radios with colored bars + descriptions ----
    QLabel *stackHeader = new QLabel(tr("<b>What gets logged</b>"), this);
    outer->addWidget(stackHeader);

    QFrame *stack = new QFrame(this);
    stack->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout *stackLayout = new QVBoxLayout(stack);
    stackLayout->setContentsMargins(8, 4, 8, 4);
    stackLayout->setSpacing(2);

    _levelGroup = new QButtonGroup(this);

    stackLayout->addWidget(makeLevelRow(LoggingConfig::Level::Off,
        tr("Off"),
        tr("No logging. The file stops growing entirely.")));
    stackLayout->addWidget(makeLevelRow(LoggingConfig::Level::Errors,
        tr("Errors only"),
        tr("Critical / fatal entries. Catches crashes; misses everything else.")));
    stackLayout->addWidget(makeLevelRow(LoggingConfig::Level::Warnings,
        tr("Warnings (default)"),
        tr("+ recoverable issues, suspect state, network warnings.")));
    stackLayout->addWidget(makeLevelRow(LoggingConfig::Level::Info,
        tr("Info (verbose)"),
        tr("+ lifecycle events: session start/stop, file load, sync ticks.")));
    stackLayout->addWidget(makeLevelRow(LoggingConfig::Level::Debug,
        tr("Debug (very verbose)"),
        tr("+ per-frame trace, every tool call, ICE candidates, raw frames.")));

    outer->addWidget(stack);

    setCurrentLevel(LoggingConfig::loadLevel());
    connect(_levelGroup, &QButtonGroup::idClicked,
            this, &LoggingSettingsWidget::onLevelChanged);

    // ---- Live preview pane -----------------------------------------
    QLabel *previewHeader = new QLabel(
        tr("<b>Preview</b> &mdash; sample lines you'd see at the selected level"),
        this);
    outer->addWidget(previewHeader);
    _previewEdit = new QPlainTextEdit(this);
    _previewEdit->setReadOnly(true);
    QFont monoFont = _previewEdit->font();
    monoFont.setFamily(QStringLiteral("Consolas"));
    monoFont.setPointSize(9);
    _previewEdit->setFont(monoFont);
    _previewEdit->setMaximumHeight(110);
    _previewEdit->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #0d1117; color: #c9d1d9; "
        "border: 1px solid #30363d; border-radius: 4px; padding: 4px; }"));
    outer->addWidget(_previewEdit);

    // ---- Size warning callout --------------------------------------
    _sizeWarning = new QLabel(this);
    _sizeWarning->setWordWrap(true);
    _sizeWarning->setTextFormat(Qt::RichText);
    _sizeWarning->setStyleSheet(QStringLiteral(
        "QLabel { background: rgba(240,136,62,0.08); "
        "border-left: 3px solid #f0883e; "
        "padding: 8px 12px; border-radius: 3px; }"));
    outer->addWidget(_sizeWarning);

    // Initial preview / warning render for the loaded level.
    onLevelChanged();

    outer->addWidget(separator());

    // ---- Open log file ---------------------------------------------
    QHBoxLayout *fileRow = new QHBoxLayout();
    fileRow->setContentsMargins(0, 0, 0, 0);
    _openFileButton = new QPushButton(tr("Open log file"), this);
    _openFileButton->setToolTip(
        tr("Open midieditor_ai.log in your default text editor / file viewer."));
    connect(_openFileButton, &QPushButton::clicked, this, []() {
        QString primary = QCoreApplication::applicationDirPath()
                          + QStringLiteral("/midieditor_ai.log");
        QString fallback;
        QString dataDir = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        if (!dataDir.isEmpty()) {
            fallback = dataDir + QStringLiteral("/midieditor_ai.log");
        }
        QString chosen = QFile::exists(primary) ? primary
                       : (QFile::exists(fallback) ? fallback : primary);
        QDesktopServices::openUrl(QUrl::fromLocalFile(chosen));
    });
    fileRow->addWidget(_openFileButton);
    fileRow->addStretch(1);
    QLabel *rotationHint = new QLabel(
        tr("<i>Rotates at 10 MB &middot; .log.1 / .2 / .3 backups</i>"),
        this);
    rotationHint->setStyleSheet("color: gray;");
    fileRow->addWidget(rotationHint);
    outer->addLayout(fileRow);

    // ---- Advanced rules --------------------------------------------
    QLabel *advHeader = new QLabel(
        tr("<i>Advanced &mdash; per-category overrides "
           "(one Qt filter rule per line)</i>"),
        this);
    advHeader->setStyleSheet("color: gray; font-size: 11px;");
    outer->addWidget(advHeader);

    _perCategoryEdit = new QPlainTextEdit(this);
    _perCategoryEdit->setPlaceholderText(QStringLiteral(
        "midieditor.collab.diff=true\n"
        "midieditor.collab.rtc.*.debug=false\n"
        "qt.network.ssl.*=false"));
    _perCategoryEdit->setPlainText(LoggingConfig::loadPerCategory());
    _perCategoryEdit->setFont(monoFont);
    _perCategoryEdit->setMaximumHeight(110);
    _perCategoryEdit->setToolTip(
        tr("Standard Qt logging filter rules. Per-category overrides take "
           "precedence over the level above. One rule per line. Empty = "
           "no overrides.\n\n"
           "Examples:\n"
           "  midieditor.collab.diff=true        — enable a single category\n"
           "  midieditor.collab.rtc.*.debug=false — silence rtc-only debug\n"
           "  qt.*=false                          — silence all of Qt"));
    outer->addWidget(_perCategoryEdit);

    outer->addStretch(1);
}

QWidget *LoggingSettingsWidget::makeLevelRow(LoggingConfig::Level lvl,
                                              const QString &name,
                                              const QString &description) {
    QWidget *row = new QWidget(this);
    QHBoxLayout *h = new QHBoxLayout(row);
    h->setContentsMargins(4, 4, 4, 4);
    h->setSpacing(8);

    QRadioButton *radio = new QRadioButton(row);
    radio->setMinimumWidth(20);
    h->addWidget(radio);

    // Severity bar
    QLabel *bar = new QLabel(severityBarHtml(lvl), row);
    bar->setTextFormat(Qt::RichText);
    bar->setMinimumWidth(80);
    bar->setMaximumWidth(80);
    h->addWidget(bar);

    // Name + description
    QWidget *labels = new QWidget(row);
    QVBoxLayout *lv = new QVBoxLayout(labels);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(0);
    QLabel *nameLabel = new QLabel(QStringLiteral("<b>%1</b>").arg(name), labels);
    QLabel *descLabel = new QLabel(description, labels);
    descLabel->setStyleSheet("color: gray; font-size: 11px;");
    descLabel->setWordWrap(true);
    lv->addWidget(nameLabel);
    lv->addWidget(descLabel);
    h->addWidget(labels, 1);

    // Wire to button group
    int id = static_cast<int>(lvl);
    _levelGroup->addButton(radio, id);
    switch (lvl) {
        case LoggingConfig::Level::Off:      _radioOff = radio; break;
        case LoggingConfig::Level::Errors:   _radioErrors = radio; break;
        case LoggingConfig::Level::Warnings: _radioWarnings = radio; break;
        case LoggingConfig::Level::Info:     _radioInfo = radio; break;
        case LoggingConfig::Level::Debug:    _radioDebug = radio; break;
    }
    return row;
}

LoggingConfig::Level LoggingSettingsWidget::currentLevel() const {
    if (!_levelGroup) return LoggingConfig::Level::Warnings;
    int id = _levelGroup->checkedId();
    if (id < 0 || id > 4) return LoggingConfig::Level::Warnings;
    return static_cast<LoggingConfig::Level>(id);
}

void LoggingSettingsWidget::setCurrentLevel(LoggingConfig::Level lvl) {
    QRadioButton *target = nullptr;
    switch (lvl) {
        case LoggingConfig::Level::Off:      target = _radioOff; break;
        case LoggingConfig::Level::Errors:   target = _radioErrors; break;
        case LoggingConfig::Level::Warnings: target = _radioWarnings; break;
        case LoggingConfig::Level::Info:     target = _radioInfo; break;
        case LoggingConfig::Level::Debug:    target = _radioDebug; break;
    }
    if (target) target->setChecked(true);
}

void LoggingSettingsWidget::onLevelChanged() {
    LoggingConfig::Level lvl = currentLevel();
    if (_previewEdit) _previewEdit->setPlainText(sampleLinesFor(lvl));
    if (_sizeWarning) _sizeWarning->setText(sizeEstimateFor(lvl));
}

QString LoggingSettingsWidget::sampleLinesFor(LoggingConfig::Level lvl) const {
    QStringList lines;
    if (lvl == LoggingConfig::Level::Off) {
        lines << tr("(no log lines emitted at any severity)");
        return lines.join(QLatin1Char('\n'));
    }
    if (lvl >= LoggingConfig::Level::Errors) {
        lines << QStringLiteral(
            "2026-05-09T13:42:18.331 CRI [midieditor.collab.rtc] DTLS handshake failed: cert expired");
        lines << QStringLiteral(
            "2026-05-09T13:42:19.011 CRI [midieditor.audio]   FluidSynth load failed: insufficient memory");
    }
    if (lvl >= LoggingConfig::Level::Warnings) {
        lines << QStringLiteral(
            "2026-05-09T13:42:21.108 WRN [midieditor.collab.lan]  session: peer disconnected, remaining=2");
        lines << QStringLiteral(
            "2026-05-09T13:42:23.940 WRN [midieditor.midi]        track 5 has overlapping NoteOn — clamped");
    }
    if (lvl >= LoggingConfig::Level::Info) {
        lines << QStringLiteral(
            "2026-05-09T13:42:24.112 INF [midieditor.collab.lan]  session: traffic peers=3 out=842 B/s in=18 B/s");
        lines << QStringLiteral(
            "2026-05-09T13:42:24.230 INF [midieditor.collab.lan]  session: syncTick broadcast hunks=2 endTickChanged=false");
    }
    if (lvl >= LoggingConfig::Level::Debug) {
        lines << QStringLiteral(
            "2026-05-09T13:42:24.240 DBG [midieditor.collab.diff] stacked events: 187 collisions across 12 keys");
        lines << QStringLiteral(
            "2026-05-09T13:42:24.241 DBG [midieditor.collab.rtc]  local candidate: 192.168.0.164:51234 typ host");
        lines << QStringLiteral(
            "2026-05-09T13:42:24.242 DBG [midieditor.collab.rdv]  pollJoinerOffers GET /code/AB12/joiner-offers");
    }
    return lines.join(QLatin1Char('\n'));
}

QString LoggingSettingsWidget::sizeEstimateFor(LoggingConfig::Level lvl) const {
    switch (lvl) {
    case LoggingConfig::Level::Off:
        return tr("&#x1F7E2; <b>File growth: none.</b> No entries written. "
                  "Pick this if you don't need a log at all.");
    case LoggingConfig::Level::Errors:
        return tr("&#x1F7E2; <b>File growth: minimal.</b> Only critical / "
                  "fatal lines (~kilobytes per session). Best when you "
                  "want to keep the log file tiny but still capture crashes.");
    case LoggingConfig::Level::Warnings:
        return tr("&#x1F7E2; <b>File growth: small.</b> A few hundred lines "
                  "per typical session (~tens of KB). The default and the "
                  "right choice for normal use.");
    case LoggingConfig::Level::Info:
        return tr("&#x1F7E1; <b>File growth: moderate.</b> Sync-tick "
                  "summaries, session lifecycle, traffic counters &mdash; "
                  "roughly a few MB per hour of active editing. Useful "
                  "when reproducing a Live-Mode issue.");
    case LoggingConfig::Level::Debug:
        return tr("&#x1F534; <b>File growth: large + perf hit.</b> Every "
                  "frame, every ICE candidate, every diff iteration &mdash; "
                  "tens to hundreds of MB per hour, can lag the editor "
                  "while writing. Use only briefly while debugging; "
                  "switch back to <i>Warnings</i> afterwards. Files "
                  "rotate at 10&nbsp;MB so disk usage caps at "
                  "~40&nbsp;MB.");
    }
    return QString();
}

bool LoggingSettingsWidget::accept() {
    if (!_levelGroup) return true;
    LoggingConfig::applyAndPersist(currentLevel(),
                                    _perCategoryEdit
                                        ? _perCategoryEdit->toPlainText()
                                        : QString());
    return true;
}
