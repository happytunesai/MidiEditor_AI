/*
 * MidiEditor AI - WebRtcJoinDialog implementation.
 */

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include "WebRtcJoinDialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QVBoxLayout>

#include "../../collab/LanLiveSession.h"

namespace {
// Generous overall timeout: rendezvous round-trip (~150ms) + ICE
// gathering on the joiner (often 1-3s) + DTLS handshake (sub-second).
// 60s leaves slack for slow STUN endpoints or CGNAT scenarios where
// candidate gathering retries before falling back. Also stays under
// the worker's 300s code TTL so a stale code surfaces as 404 rather
// than a timeout.
constexpr int kJoinTimeoutMs = 60'000;
} // namespace

WebRtcJoinDialog::WebRtcJoinDialog(MidiFile *file, QWidget *parent)
    : QDialog(parent), _file(file) {
    setWindowTitle(tr("Join WAN Live Session"));
    setModal(true);
    resize(420, 240);

    QVBoxLayout *root = new QVBoxLayout(this);

    QLabel *intro = new QLabel(
        tr("Enter the 4-character code your peer shared with you. "
           "The connection runs directly between your machines once the "
           "rendezvous matches you up — your edits don't pass through "
           "any server."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    QFormLayout *form = new QFormLayout();
    _codeEdit = new QLineEdit(this);
    _codeEdit->setMaxLength(4);
    _codeEdit->setPlaceholderText(QStringLiteral("e.g. M7P3"));
    QFont mono = _codeEdit->font();
    mono.setPointSize(mono.pointSize() + 4);
    mono.setLetterSpacing(QFont::PercentageSpacing, 140);
    _codeEdit->setFont(mono);
    // Same alphabet as the LAN flow — letters/digits excluding visually
    // ambiguous I/L/O and 0/1. A stray L still parses at validation
    // level (regex is loose) and is rejected server-side as "code not
    // found".
    auto *validator = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[A-HJ-NP-Za-hj-np-z2-9]{0,4}")), this);
    _codeEdit->setValidator(validator);
    connect(_codeEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        QString upper = text.toUpper();
        if (upper != text) {
            int pos = _codeEdit->cursorPosition();
            _codeEdit->setText(upper);
            _codeEdit->setCursorPosition(pos);
        }
    });
    form->addRow(tr("Session code:"), _codeEdit);
    root->addLayout(form);

    _statusLabel = new QLabel(this);
    _statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    _statusLabel->setWordWrap(true);
    root->addWidget(_statusLabel);

    root->addStretch(1);

    QDialogButtonBox *box = new QDialogButtonBox(this);
    _connectButton = box->addButton(tr("Connect"), QDialogButtonBox::AcceptRole);
    box->addButton(QDialogButtonBox::Cancel);
    connect(_connectButton, &QPushButton::clicked, this, &WebRtcJoinDialog::onConnect);
    connect(box, &QDialogButtonBox::rejected, this, [this]() {
        // Cancel during an in-flight join: tear down the half-open
        // session so we don't leave a transport / rendezvous client
        // running in the background.
        if (_searchTimeout && _searchTimeout->isActive()) {
            _searchTimeout->stop();
            LanLiveSession::instance()->leaveSession();
        }
        reject();
    });
    root->addWidget(box);

    _searchTimeout = new QTimer(this);
    _searchTimeout->setSingleShot(true);
    _searchTimeout->setInterval(kJoinTimeoutMs);
    connect(_searchTimeout, &QTimer::timeout, this, &WebRtcJoinDialog::onSearchTimeout);

    LanLiveSession *svc = LanLiveSession::instance();
    connect(svc, &LanLiveSession::joined,
            this, &WebRtcJoinDialog::onJoined);
    connect(svc, &LanLiveSession::joinFailed,
            this, &WebRtcJoinDialog::onJoinFailed);
    connect(svc, &LanLiveSession::statusMessage,
            this, &WebRtcJoinDialog::onStatusMessage);

    _codeEdit->setFocus();
}

void WebRtcJoinDialog::onConnect() {
    QString code = _codeEdit->text().trimmed().toUpper();
    if (code.size() != 4) {
        _statusLabel->setText(tr("Code must be exactly 4 characters."));
        return;
    }
    _connectButton->setEnabled(false);
    _codeEdit->setEnabled(false);
    _statusLabel->setText(tr("Looking up code %1…").arg(code));

    LanLiveSession::instance()->joinSessionWan(_file, code);
    _searchTimeout->start();
}

void WebRtcJoinDialog::onJoined(const QString &hostName) {
    _searchTimeout->stop();
    QString display = hostName.isEmpty() ? tr("remote peer") : hostName;
    _statusLabel->setText(tr("✓ Connected to %1.").arg(display));
    QTimer::singleShot(800, this, &QDialog::accept);
}

void WebRtcJoinDialog::onJoinFailed(const QString &reason) {
    _searchTimeout->stop();
    _statusLabel->setText(tr("⚠ %1").arg(reason));
    _connectButton->setEnabled(true);
    _codeEdit->setEnabled(true);
}

void WebRtcJoinDialog::onStatusMessage(const QString &message) {
    // Only surface status while a join is in-flight; otherwise the
    // session's leave-session message would clobber our error display.
    if (_searchTimeout && _searchTimeout->isActive()) {
        _statusLabel->setText(message);
    }
}

void WebRtcJoinDialog::onSearchTimeout() {
    LanLiveSession::instance()->leaveSession();
    _statusLabel->setText(tr("⚠ Could not connect within %1 seconds. "
                             "Double-check the code, or have your peer click "
                             "Start again to mint a fresh one.")
                                .arg(kJoinTimeoutMs / 1000));
    _connectButton->setEnabled(true);
    _codeEdit->setEnabled(true);
}

#endif // MIDIEDITOR_WEBRTC_ENABLED
