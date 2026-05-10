/*
 * MidiEditor AI - CollabSettingsWidget implementation.
 */

#include "CollabSettingsWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include "../../collab/CollabIdentity.h"
#include "../../collab/CollabService.h"
#include "../../collab/LanLiveSession.h"
#include "../../LoggingConfig.h"
#ifdef MIDIEDITOR_WEBRTC_ENABLED
#include "../../collab/RtcRendezvousClient.h"
#include "../../collab/WanConnectionTest.h"
#endif

CollabSettingsWidget::CollabSettingsWidget(QSettings *settings, QWidget *parent)
    : SettingsWidget(tr("Collaboration"), parent), _settings(settings) {

    QVBoxLayout *outer = new QVBoxLayout(this);
    setLayout(outer);
    setMinimumSize(400, 300);

    outer->addWidget(createInfoBox(
        tr("Collaboration features let you exchange named change-sets (\"PRs\") "
           "with other MidiEditor users — GitHub-style. Phase 1 ships as file-bundle "
           "exchange (no network required); transport over P2P is planned for a later "
           "phase.<br><br>"
           "<b>This feature is fully opt-in.</b> Enabling it here unlocks the menu "
           "entries; nothing is written next to your MIDI files until you explicitly "
           "click \"Initialize for collaboration\" on a specific file.")));

    outer->addWidget(separator());

    // ----- Master toggle ------------------------------------------------
    _enableCheck = new QCheckBox(tr("Enable collaboration features"), this);
    _enableCheck->setChecked(CollabService::instance()->isEnabled());
    _enableCheck->setToolTip(tr("When off, no collaboration menu items appear, "
                                "no signals are connected, and no sidecar files are written."));
    connect(_enableCheck, &QCheckBox::toggled, this, &CollabSettingsWidget::onEnabledToggled);
    outer->addWidget(_enableCheck);

    outer->addWidget(separator());

    // ----- Identity section ---------------------------------------------
    _identitySection = new QWidget(this);
    QGridLayout *idLayout = new QGridLayout(_identitySection);
    idLayout->setContentsMargins(0, 0, 0, 0);
    int row = 0;

    QLabel *idHeader = new QLabel(tr("<b>Your identity</b>"), _identitySection);
    idLayout->addWidget(idHeader, row++, 0, 1, 3);

    QLabel *idHelp = new QLabel(
        tr("Your display name and a per-machine ID are attached to every PR you create. "
           "No keys, no certificates, no pairing phrase — same trust model as a Git patch."),
        _identitySection);
    idHelp->setWordWrap(true);
    idHelp->setStyleSheet("color: gray; font-size: 11px;");
    idLayout->addWidget(idHelp, row++, 0, 1, 3);

    idLayout->addWidget(new QLabel(tr("Display name:"), _identitySection), row, 0);
    _displayNameEdit = new QLineEdit(_identitySection);
    _displayNameEdit->setPlaceholderText(tr("Your display name"));
    _displayNameEdit->setText(CollabIdentity::displayName());
    idLayout->addWidget(_displayNameEdit, row, 1, 1, 2);
    row++;

    idLayout->addWidget(new QLabel(tr("Machine ID:"), _identitySection), row, 0);
    _machineIdValueLabel = new QLabel(CollabIdentity::machineId(), _identitySection);
    _machineIdValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _machineIdValueLabel->setStyleSheet("font-family: monospace; color: gray;");
    idLayout->addWidget(_machineIdValueLabel, row, 1);

    _regenerateButton = new QPushButton(tr("Regenerate"), _identitySection);
    _regenerateButton->setToolTip(tr("Generate a new machine ID. Existing PRs you "
                                     "already created keep their old ID; new PRs use "
                                     "the new one. Use only if you cloned a VM or want "
                                     "a fresh identity."));
    connect(_regenerateButton, &QPushButton::clicked, this,
            &CollabSettingsWidget::onRegenerateMachineId);
    idLayout->addWidget(_regenerateButton, row, 2);
    row++;

    idLayout->setColumnStretch(1, 1);
    outer->addWidget(_identitySection);

    outer->addWidget(separator());

    // ----- Webhook section ---------------------------------------------
    _webhookSection = new QWidget(this);
    QGridLayout *whLayout = new QGridLayout(_webhookSection);
    whLayout->setContentsMargins(0, 0, 0, 0);
    int wRow = 0;

    QLabel *whHeader = new QLabel(tr("<b>Discord webhook (optional)</b>"), _webhookSection);
    whLayout->addWidget(whHeader, wRow++, 0, 1, 3);

    QLabel *whHelp = new QLabel(
        tr("If set, every save will POST a Discord embed with the smart-paste "
           "token + bundle attachment to this URL. Peers in the channel can "
           "copy the token and paste it directly into MidiEditor (Ctrl+V).<br>"
           "Get one via Discord: <i>Server settings → Integrations → Webhooks</i>."),
        _webhookSection);
    whHelp->setWordWrap(true);
    whHelp->setStyleSheet("color: gray; font-size: 11px;");
    whHelp->setOpenExternalLinks(true);
    whLayout->addWidget(whHelp, wRow++, 0, 1, 3);

    whLayout->addWidget(new QLabel(tr("Webhook URL:"), _webhookSection), wRow, 0);
    _webhookUrlEdit = new QLineEdit(_webhookSection);
    _webhookUrlEdit->setEchoMode(QLineEdit::Password);  // hide on first paint (URL is sensitive)
    _webhookUrlEdit->setPlaceholderText(QStringLiteral("https://discord.com/api/webhooks/…"));
    _webhookUrlEdit->setText(_settings ? _settings->value("Collab/webhookUrl").toString() : QString());
    whLayout->addWidget(_webhookUrlEdit, wRow, 1);

    _webhookToggleButton = new QPushButton(tr("Show"), _webhookSection);
    _webhookToggleButton->setMinimumWidth(60);
    connect(_webhookToggleButton, &QPushButton::clicked, this, [this]() {
        _webhookUrlVisible = !_webhookUrlVisible;
        _webhookUrlEdit->setEchoMode(_webhookUrlVisible ? QLineEdit::Normal : QLineEdit::Password);
        _webhookToggleButton->setText(_webhookUrlVisible ? tr("Hide") : tr("Show"));
    });
    whLayout->addWidget(_webhookToggleButton, wRow, 2);
    wRow++;

    whLayout->setColumnStretch(1, 1);
    outer->addWidget(_webhookSection);

    // ----- Hosting safety net (Plan §11.10n) ---------------------------
    {
        QSettings probe(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        outer->addWidget(separator());
        QWidget *hostSec = new QWidget(this);
        QVBoxLayout *hostLayout = new QVBoxLayout(hostSec);
        hostLayout->setContentsMargins(0, 0, 0, 0);

        QLabel *hostHeader = new QLabel(tr("<b>Hosting</b>"), hostSec);
        hostLayout->addWidget(hostHeader);

        QLabel *hostHelp = new QLabel(
            tr("When you start a Live Session as host, save your file as "
               "a copy under <code>Documents/MidiEditor_AI/shared/&lt;name&gt;_shared.mid</code> "
               "first and edit the copy — your original stays untouched. "
               "Joining peers always work on a copy; this option gives "
               "the host the same safety net."),
            hostSec);
        hostHelp->setWordWrap(true);
        hostHelp->setTextFormat(Qt::RichText);
        hostHelp->setStyleSheet("color: gray; font-size: 11px;");
        hostLayout->addWidget(hostHelp);

        _hostWorkOnCopyCheck = new QCheckBox(
            tr("Work on a copy when hosting (keeps the original safe)"),
            hostSec);
        _hostWorkOnCopyCheck->setChecked(
            probe.value(QStringLiteral("Collab/host/workOnCopy"), true).toBool());
        _hostWorkOnCopyCheck->setToolTip(
            tr("On by default. Turn off if you want hosting to edit your "
               "original file directly (matches the pre-2026-05-07 behaviour)."));
        hostLayout->addWidget(_hostWorkOnCopyCheck);

        outer->addWidget(hostSec);
    }

#ifdef MIDIEDITOR_WEBRTC_ENABLED
    // ----- WAN rendezvous section --------------------------------------
    outer->addWidget(separator());

    _rendezvousSection = new QWidget(this);
    QGridLayout *rdvLayout = new QGridLayout(_rendezvousSection);
    rdvLayout->setContentsMargins(0, 0, 0, 0);
    int rRow = 0;

    QLabel *rdvHeader = new QLabel(tr("<b>WAN Live Session — rendezvous</b>"), _rendezvousSection);
    rdvLayout->addWidget(rdvHeader, rRow++, 0, 1, 2);

    QLabel *rdvHelp = new QLabel(
        tr("Tiny stateless service that swaps a 4-character code for the "
           "WebRTC offer/answer. After the data channel opens it is no "
           "longer touched — your edits flow directly peer-to-peer over "
           "DTLS-encrypted WebRTC.<br><br>"
           "<b>The bundled default URL is a shared Cloudflare Worker</b> "
           "with the free-tier limit of ~250 sessions / day across all "
           "users of this build combined. Fine for casual use; for "
           "guilds / classrooms / heavy daily use deploy your own and "
           "paste its URL below. Setup walkthrough is in "
           "<code>cloudflare/README.md</code> next to the source — "
           "Cloudflare's web UI deploy is ~5 minutes, no Node.js needed."),
        _rendezvousSection);
    rdvHelp->setWordWrap(true);
    rdvHelp->setTextFormat(Qt::RichText);
    rdvHelp->setStyleSheet("color: gray; font-size: 11px;");
    rdvLayout->addWidget(rdvHelp, rRow++, 0, 1, 2);

    rdvLayout->addWidget(new QLabel(tr("Rendezvous URL:"), _rendezvousSection), rRow, 0);
    _rendezvousUrlEdit = new QLineEdit(_rendezvousSection);
    _rendezvousUrlEdit->setPlaceholderText(RtcRendezvousClient::defaultUrl());
    {
        // Show the user's override verbatim; if they haven't set one the
        // edit stays empty and the placeholder hints at the bundled
        // default. accept() treats empty == use default. Use the
        // app-wide ("MidiEditor","NONE") store so we read the same file
        // RtcRendezvousClient writes to.
        QSettings probe(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        QString stored = probe.value(QStringLiteral("Collab/wan/rendezvousUrl")).toString();
        if (!stored.isEmpty()) _rendezvousUrlEdit->setText(stored);
    }
    rdvLayout->addWidget(_rendezvousUrlEdit, rRow, 1);
    rRow++;

    // ----- Connection-quality knobs (Plan §11.10h) ---------------------
    {
        QSettings probe(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));

        rdvLayout->addWidget(new QLabel(tr("ICE timeout (ms):"), _rendezvousSection),
                              rRow, 0);
        _iceTimeoutSpin = new QSpinBox(_rendezvousSection);
        _iceTimeoutSpin->setRange(2000, 30000);
        _iceTimeoutSpin->setSingleStep(500);
        _iceTimeoutSpin->setValue(qBound(2000,
            probe.value(QStringLiteral("Collab/wan/iceGatheringTimeoutMs"), 8000).toInt(),
            30000));
        _iceTimeoutSpin->setToolTip(
            tr("How long to wait for ICE candidate gathering before "
               "publishing the offer/answer. Default 8000 ms. Lower for "
               "faster connect on healthy networks; raise on slow / "
               "firewalled networks where STUN takes longer."));
        rdvLayout->addWidget(_iceTimeoutSpin, rRow, 1);
        rRow++;

        _autoReconnectCheck = new QCheckBox(
            tr("Auto-reconnect on transport failure"), _rendezvousSection);
        _autoReconnectCheck->setChecked(
            probe.value(QStringLiteral("Collab/wan/autoReconnect"), false).toBool());
        _autoReconnectCheck->setToolTip(
            tr("If enabled, the WAN session will automatically retry up "
               "to N times when the data channel fails (e.g. transient "
               "network blip). Host gets a NEW rendezvous code on each "
               "retry; peer reuses the old code (may need a fresh one)."));
        rdvLayout->addWidget(_autoReconnectCheck, rRow, 0, 1, 2);
        rRow++;

        QHBoxLayout *retryRow = new QHBoxLayout();
        retryRow->setContentsMargins(20, 0, 0, 0);  // indent under checkbox
        retryRow->addWidget(new QLabel(tr("Max retry attempts:"), _rendezvousSection));
        _autoReconnectMaxSpin = new QSpinBox(_rendezvousSection);
        _autoReconnectMaxSpin->setRange(1, 5);
        _autoReconnectMaxSpin->setValue(qBound(1,
            probe.value(QStringLiteral("Collab/wan/autoReconnectMaxAttempts"), 2).toInt(),
            5));
        _autoReconnectMaxSpin->setEnabled(_autoReconnectCheck->isChecked());
        retryRow->addWidget(_autoReconnectMaxSpin);
        retryRow->addStretch(1);
        rdvLayout->addLayout(retryRow, rRow, 0, 1, 2);
        rRow++;

        connect(_autoReconnectCheck, &QCheckBox::toggled,
                _autoReconnectMaxSpin, &QSpinBox::setEnabled);
    }

    // ----- Connection test (Plan §11.10g) ------------------------------
    // Exercises the production handshake end-to-end (rendezvous URL +
    // ICE pool + DTLS + ping/pong) with two transports paired in this
    // process. Unlike the developer-only loopback test, this hits the
    // real rendezvous URL — it answers "is my WAN setup actually
    // working?", not just "is libdatachannel linked?".
    _connectionTestButton = new QPushButton(tr("Test connection"), _rendezvousSection);
    _connectionTestButton->setToolTip(
        tr("Pairs two WebRTC transports in this process via the configured "
           "rendezvous, then connects them through local-network host "
           "candidates (no STUN required). Verifies rendezvous reachability "
           "and that the local WebRTC + DTLS stack is functional. It does "
           "NOT test STUN servers or your NAT — only an actual two-machine "
           "session can do that."));
    connect(_connectionTestButton, &QPushButton::clicked,
            this, &CollabSettingsWidget::onRunConnectionTest);
    rdvLayout->addWidget(_connectionTestButton, rRow, 0);

    // Plan §11.10k: running the test while a live session is up tends to
    // close the live data channel (two WebRtcTransport instances seem to
    // step on each other inside libdatachannel). Disable the button when
    // a session is active rather than risk killing the user's connection.
    auto refreshTestEnable = [this]() {
        if (!_connectionTestButton) return;
        // Connection Test stays enabled even with collab off — the
        // whole point of the test is to verify the user's network
        // setup BEFORE they flip collab on for real. Only block the
        // test when a live session is active, because spinning up
        // two extra transports inside the same process while a real
        // session uses libdatachannel could step on each other.
        bool sessionActive =
            (LanLiveSession::instance()->role() != LanLiveSession::Role::Idle);
        _connectionTestButton->setEnabled(!sessionActive);
        _connectionTestButton->setToolTip(sessionActive
            ? tr("End your live session before running the connection test — "
                 "the test would close the active data channel.")
            : tr("Exercises the rendezvous + WebRTC stack with two transports paired "
                 "in this process. Rendezvous reachability is meaningful here, but "
                 "the in-process loopback can fail on routers without NAT hairpinning "
                 "while real two-PC sessions still work — the test surfaces that as "
                 "an Inconclusive result rather than Failed."));
    };
    connect(LanLiveSession::instance(), &LanLiveSession::roleChanged,
            this, [refreshTestEnable](LanLiveSession::Role) { refreshTestEnable(); });
    refreshTestEnable();

    QHBoxLayout *statusRow = new QHBoxLayout();
    statusRow->setContentsMargins(0, 0, 0, 0);
    _connectionTestLight = new QLabel(_rendezvousSection);
    _connectionTestLight->setMinimumWidth(20);
    _connectionTestLight->setText(QStringLiteral("·"));
    _connectionTestLight->setStyleSheet("color: gray; font-size: 18px;");
    statusRow->addWidget(_connectionTestLight);
    _connectionTestStatus = new QLabel(tr("Not run yet."), _rendezvousSection);
    _connectionTestStatus->setStyleSheet("color: gray;");
    statusRow->addWidget(_connectionTestStatus, /*stretch=*/1);
    rdvLayout->addLayout(statusRow, rRow, 1);
    rRow++;

    _connectionTestDetails = new QLabel(_rendezvousSection);
    _connectionTestDetails->setWordWrap(true);
    _connectionTestDetails->setStyleSheet(
        "color: gray; font-size: 11px; font-family: monospace;");
    _connectionTestDetails->setVisible(false);
    rdvLayout->addWidget(_connectionTestDetails, rRow, 0, 1, 2);
    rRow++;

    rdvLayout->setColumnStretch(1, 1);
    outer->addWidget(_rendezvousSection);
#endif // MIDIEDITOR_WEBRTC_ENABLED

    // ----- Collab-scoped logging (Plan §11.10i) -------------------------
    // The general Qt logging-rules UI (global level + advanced filter
    // rules) lives in Settings → Logging. This block only exposes a
    // single focused checkbox: when enabled, all `midieditor.collab.*`
    // categories produce verbose output regardless of the global level —
    // useful for debugging session issues without flooding the log with
    // unrelated Qt internals / MIDI engine chatter.
    outer->addWidget(separator());
    QWidget *loggingSection = new QWidget(this);
    QGridLayout *logLayout = new QGridLayout(loggingSection);
    logLayout->setContentsMargins(0, 0, 0, 0);
    int lRow = 0;

    QLabel *logHeader = new QLabel(tr("<b>Collaboration logging</b>"), loggingSection);
    logLayout->addWidget(logHeader, lRow++, 0, 1, 2);

    QLabel *logHelp = new QLabel(
        tr("Turn this on while reproducing a session issue (sync gap, "
           "disconnect, rendezvous error). Logs go to "
           "<i>midieditor_ai.log</i> next to the executable. The general "
           "log level (Qt internals, MIDI engine, AI runner) is configured "
           "separately under <i>Settings → Logging</i>."),
        loggingSection);
    logHelp->setWordWrap(true);
    logHelp->setStyleSheet("color: gray; font-size: 11px;");
    logLayout->addWidget(logHelp, lRow++, 0, 1, 2);

    _collabVerboseCheck = new QCheckBox(
        tr("Enable verbose collaboration logging"), loggingSection);
    _collabVerboseCheck->setChecked(LoggingConfig::loadCollabVerbose());
    _collabVerboseCheck->setToolTip(
        tr("Adds the `midieditor.collab.*=true` rule on top of your global "
           "logging level — every collab category logs at every severity, "
           "everything else stays at the level you picked in Settings → "
           "Logging."));
    logLayout->addWidget(_collabVerboseCheck, lRow, 0);

    _logOpenFileButton = new QPushButton(tr("Open log file"), loggingSection);
    _logOpenFileButton->setToolTip(
        tr("Open midieditor_ai.log in your default text editor or file viewer."));
    connect(_logOpenFileButton, &QPushButton::clicked, this, []() {
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
    logLayout->addWidget(_logOpenFileButton, lRow, 1);
    lRow++;

    logLayout->setColumnStretch(0, 1);
    outer->addWidget(loggingSection);

    outer->addStretch(1);
    refreshIdentitySectionEnabled();
}

bool CollabSettingsWidget::accept() {
    // Display name: trim and persist via the identity helper (which strips
    // empty values and falls back to the OS username on next read).
    CollabIdentity::setDisplayName(_displayNameEdit->text());

    // Webhook URL: persisted in QSettings under the Collab/* namespace.
    if (_settings) {
        QString url = _webhookUrlEdit->text().trimmed();
        if (url.isEmpty()) {
            _settings->remove("Collab/webhookUrl");
        } else {
            _settings->setValue("Collab/webhookUrl", url);
        }
    }

    // Plan §11.10n hosting safety toggle.
    if (_hostWorkOnCopyCheck) {
        QSettings store(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        store.setValue(QStringLiteral("Collab/host/workOnCopy"),
                        _hostWorkOnCopyCheck->isChecked());
    }

#ifdef MIDIEDITOR_WEBRTC_ENABLED
    // Rendezvous URL: empty == use the built-in default. Trim before
    // storing so accidental whitespace doesn't break URL parsing.
    if (_rendezvousUrlEdit) {
        RtcRendezvousClient::setConfiguredUrl(_rendezvousUrlEdit->text().trimmed());
    }
    // Plan §11.10h connection-quality knobs.
    {
        QSettings store(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        if (_iceTimeoutSpin) {
            store.setValue(QStringLiteral("Collab/wan/iceGatheringTimeoutMs"),
                            _iceTimeoutSpin->value());
        }
        if (_autoReconnectCheck) {
            store.setValue(QStringLiteral("Collab/wan/autoReconnect"),
                            _autoReconnectCheck->isChecked());
        }
        if (_autoReconnectMaxSpin) {
            store.setValue(QStringLiteral("Collab/wan/autoReconnectMaxAttempts"),
                            _autoReconnectMaxSpin->value());
        }
    }
#endif

    // Plan §11.10i: persist the collab-scoped verbose flag. Re-apply
    // happens inside `setCollabVerbose` so the change takes effect
    // immediately. The global level / per-category overrides live in
    // Settings → Logging and aren't touched here.
    if (_collabVerboseCheck) {
        LoggingConfig::setCollabVerbose(_collabVerboseCheck->isChecked());
    }

    // Master toggle is applied live via onEnabledToggled(); nothing to do here.
    return true;
}

void CollabSettingsWidget::onEnabledToggled(bool enabled) {
    CollabService::instance()->setEnabled(enabled);
    refreshIdentitySectionEnabled();
}

void CollabSettingsWidget::onRegenerateMachineId() {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        tr("Regenerate machine ID?"),
        tr("This generates a new machine ID for any future PRs you create.\n\n"
           "Existing PR bundles you've already exported will keep their original ID. "
           "Other peers who recognize you by your old ID will see new PRs as coming "
           "from a new (untrusted) machine.\n\n"
           "Continue?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    CollabIdentity::regenerateMachineId();
    _machineIdValueLabel->setText(CollabIdentity::machineId());
}

void CollabSettingsWidget::refreshIdentitySectionEnabled() {
    bool on = _enableCheck->isChecked();
    _identitySection->setEnabled(on);
    if (_webhookSection) _webhookSection->setEnabled(on);
#ifdef MIDIEDITOR_WEBRTC_ENABLED
    // Don't disable the rendezvous section as a whole when collab is
    // off — the user should still be able to run "Test connection"
    // before flipping collab on (verifies the network setup so they
    // know joining/hosting will work). Only disable the rendezvous
    // INPUTS (URL, ICE timeout, auto-reconnect knobs) so they can't
    // be edited while collab is off; the test button stays clickable
    // because Qt parent-disabled overrides child-enabled, so leaving
    // the section enabled and disabling individual inputs is the only
    // way to keep ONE button alive in an otherwise-greyed group.
    if (_rendezvousUrlEdit)     _rendezvousUrlEdit->setEnabled(on);
    if (_iceTimeoutSpin)        _iceTimeoutSpin->setEnabled(on);
    if (_autoReconnectCheck)    _autoReconnectCheck->setEnabled(on);
    if (_autoReconnectMaxSpin)  _autoReconnectMaxSpin->setEnabled(
                                    on && _autoReconnectCheck
                                       && _autoReconnectCheck->isChecked());
#endif
}

#ifdef MIDIEDITOR_WEBRTC_ENABLED
void CollabSettingsWidget::onRunConnectionTest() {
    if (_runningTest) return;  // already running; click is a no-op

    // If the user typed an override URL but hasn't clicked OK yet, apply
    // it for the duration of the test so the result reflects what they
    // see in the field — without this, the test would still hit the
    // previously-saved URL.
    QString url = _rendezvousUrlEdit ? _rendezvousUrlEdit->text().trimmed() : QString();
    QString prevUrl = RtcRendezvousClient::configuredUrl();
    if (!url.isEmpty() && url != prevUrl) {
        RtcRendezvousClient::setConfiguredUrl(url);
    }

    _connectionTestButton->setEnabled(false);
    _connectionTestButton->setText(tr("Testing…"));
    _connectionTestLight->setText(QStringLiteral("●"));
    _connectionTestLight->setStyleSheet("color: orange; font-size: 18px;");
    _connectionTestStatus->setText(tr("Starting…"));
    _connectionTestStatus->setStyleSheet("color: gray;");
    _connectionTestDetails->setVisible(false);
    _connectionTestDetails->clear();

    _runningTest = new WanConnectionTest(this);

    connect(_runningTest, &WanConnectionTest::phaseChanged,
            this, [this](WanConnectionTest::Phase, const QString &humanText) {
                _connectionTestStatus->setText(humanText);
            });

    connect(_runningTest, &WanConnectionTest::finished,
            this, [this, prevUrl](const WanConnectionTest::Result &r) {
                // Restore the previously-saved URL if we temporarily
                // overrode it for this test run.
                QString currentUrl = _rendezvousUrlEdit
                    ? _rendezvousUrlEdit->text().trimmed() : QString();
                if (!currentUrl.isEmpty() && currentUrl != prevUrl) {
                    RtcRendezvousClient::setConfiguredUrl(prevUrl);
                }

                QString lightColor;
                QString lightChar = QStringLiteral("●");
                QString headline;
                switch (r.quality) {
                case WanConnectionTest::Quality::Good:
                    lightColor = QStringLiteral("#4caf50");  // green
                    headline = tr("Connection works.");
                    break;
                case WanConnectionTest::Quality::Acceptable:
                    lightColor = QStringLiteral("#f0ad4e");  // amber
                    headline = tr("Connection works, slower than expected.");
                    break;
                case WanConnectionTest::Quality::Inconclusive:
                    lightColor = QStringLiteral("#f0ad4e");  // amber
                    headline = tr("Test inconclusive — real two-PC sessions may still work.");
                    break;
                case WanConnectionTest::Quality::Failed:
                    lightColor = QStringLiteral("#d9534f");  // red
                    headline = tr("Connection failed: %1").arg(r.failureReason);
                    break;
                }
                _connectionTestLight->setText(lightChar);
                _connectionTestLight->setStyleSheet(
                    QStringLiteral("color: %1; font-size: 18px;").arg(lightColor));
                _connectionTestStatus->setText(headline);
                _connectionTestStatus->setStyleSheet(QString());

                QStringList lines;
                if (!r.rendezvousCode.isEmpty()) {
                    lines << tr("code:        %1").arg(r.rendezvousCode);
                }
                if (r.rendezvousMs >= 0) {
                    lines << tr("rendezvous:  %1 ms").arg(r.rendezvousMs);
                }
                if (r.handshakeMs >= 0) {
                    lines << tr("handshake:   %1 ms").arg(r.handshakeMs);
                }
                if (r.pingRttMs >= 0) {
                    lines << tr("ping rtt:    %1 ms").arg(r.pingRttMs);
                }
                if (r.totalMs >= 0) {
                    lines << tr("total:       %1 ms").arg(r.totalMs);
                }
                if (!r.failureStage.isEmpty()) {
                    lines << tr("failed at:   %1").arg(r.failureStage);
                }

                // Plan §11.10g failure-hint heuristic. Failure shape
                // determines colour (Inconclusive vs Failed in
                // WanConnectionTest::emitFailure) AND the explanatory
                // hint shown in the details panel.
                if (r.quality == WanConnectionTest::Quality::Inconclusive) {
                    lines << tr(
                        "\nRendezvous worked but the in-process WebRTC loopback\n"
                        "didn't complete. The test runs both transports on the\n"
                        "same machine and connects them via local-network host\n"
                        "candidates — Windows Firewall, antivirus, or another\n"
                        "endpoint-protection product blocking the app's UDP\n"
                        "between processes is the most common cause.\n"
                        "→ Real WAN sessions use a different network path, so\n"
                        "  they may still work — verify by hosting on one PC and\n"
                        "  joining from another. If you want the in-process test\n"
                        "  to pass, allow this app's UDP traffic in your firewall.");
                } else if (r.quality == WanConnectionTest::Quality::Failed) {
                    QString hint;
                    // Phase 9.8 troubleshooting: the rendezvous /health
                    // pre-flight times out with "no response" when the
                    // OS or an antivirus is silently blocking the app's
                    // outbound HTTPS, while the same URL works fine in
                    // a browser (browsers are usually whitelisted by
                    // default). On Windows this typically means the
                    // app needs to be added to the Defender Firewall
                    // allow-list — reproducible 2026-05-08 on a
                    // fresh-install PC where the same JSON URL worked
                    // in a browser + PowerShell but the Qt app's
                    // QNetworkAccessManager silently dropped.
                    const bool silentBlockSig =
                        r.failureStage == QStringLiteral("rendezvous (health)")
                        && r.failureReason.contains(
                            QStringLiteral("no response"), Qt::CaseInsensitive);

                    if (silentBlockSig) {
                        hint = tr(
                            "\nRendezvous URL is reachable from your browser but not\n"
                            "from this app — that's the classic 'silent firewall block'\n"
                            "signature: outbound HTTPS dropped without a 'allow this app'\n"
                            "prompt. Two things to try:\n"
                            "  1) Windows Security → Firewall & network protection →\n"
                            "     'Allow an app through firewall' → add MidiEditorAI.exe\n"
                            "     (both Private + Public).\n"
                            "  2) If you have third-party antivirus (Bitdefender, Norton,\n"
                            "     Kaspersky, etc.), add an exception for the .exe.\n"
                            "Browser tip: open the URL above in your browser. If the\n"
                            "browser shows JSON but this test fails, it's almost\n"
                            "certainly an outbound-block on the .exe specifically.");
                    } else if (r.failureStage.startsWith(QStringLiteral("rendezvous"))) {
                        hint = tr(
                            "\nRendezvous service unreachable. Check the URL in\n"
                            "Settings → Collaboration → WAN Live Session — rendezvous,\n"
                            "or your firewall / proxy if the URL is correct.");
                    } else if (r.failureStage == QStringLiteral("timeout")) {
                        hint = tr(
                            "\nTest exceeded its overall budget. If your network is\n"
                            "slow, raise ICE timeout in the WAN Live Session section\n"
                            "above and try again.");
                    }
                    if (!hint.isEmpty()) lines << hint;
                }

                if (!lines.isEmpty()) {
                    _connectionTestDetails->setText(lines.join(QStringLiteral("\n")));
                    _connectionTestDetails->setVisible(true);
                }

                _connectionTestButton->setEnabled(true);
                _connectionTestButton->setText(tr("Test connection"));
                if (_runningTest) {
                    _runningTest->deleteLater();
                    _runningTest = nullptr;
                }
            });

    _runningTest->run();
}
#endif // MIDIEDITOR_WEBRTC_ENABLED
