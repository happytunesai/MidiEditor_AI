/*
 * MidiEditor AI - WebRtcStartDialog implementation.
 */

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include "WebRtcStartDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "../../collab/LanLiveSession.h"

WebRtcStartDialog::WebRtcStartDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("WAN Live Session"));
    setModal(false);
    resize(440, 360);

    QVBoxLayout *root = new QVBoxLayout(this);

    QLabel *intro = new QLabel(
        tr("<b>You're hosting a WAN Live Session.</b><br>"
           "Send the code to one peer over chat or voice. They paste it "
           "into <i>Join WAN Live Session…</i> and you're connected — "
           "edits flow directly between your machines, no server in "
           "the middle."),
        this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    root->addWidget(intro);

    _codeLabel = new QLabel(this);
    _codeLabel->setAlignment(Qt::AlignCenter);
    _codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont codeFont = _codeLabel->font();
    codeFont.setPointSize(codeFont.pointSize() + 14);
    codeFont.setBold(true);
    codeFont.setLetterSpacing(QFont::PercentageSpacing, 130);
    _codeLabel->setFont(codeFont);
    _codeLabel->setStyleSheet(
        "padding: 12px;"
        "background: rgba(80,160,255,0.15);"
        "border-radius: 6px;");
    root->addWidget(_codeLabel);

    _statusLabel = new QLabel(this);
    _statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    _statusLabel->setWordWrap(true);
    root->addWidget(_statusLabel);

    QHBoxLayout *codeBtnRow = new QHBoxLayout();
    codeBtnRow->addStretch(1);
    _copyButton = new QPushButton(tr("Copy code"), this);
    _copyButton->setEnabled(false);
    _copyButton->setAutoDefault(false);
    _copyButton->setToolTip(tr("Copy just the 4-character code to the clipboard."));
    connect(_copyButton, &QPushButton::clicked, this, &WebRtcStartDialog::onCopyCode);
    codeBtnRow->addWidget(_copyButton);
    _copyInviteButton = new QPushButton(tr("Copy invite"), this);
    _copyInviteButton->setEnabled(false);
    _copyInviteButton->setAutoDefault(false);
    _copyInviteButton->setToolTip(tr("Copy a friendly invitation message you can paste "
                                      "into Discord / chat / email."));
    connect(_copyInviteButton, &QPushButton::clicked, this, &WebRtcStartDialog::onCopyInvite);
    codeBtnRow->addWidget(_copyInviteButton);
    root->addLayout(codeBtnRow);

    QLabel *peerHeader = new QLabel(tr("<b>Connected peer</b>"), this);
    peerHeader->setTextFormat(Qt::RichText);
    root->addWidget(peerHeader);

    _peerList = new QListWidget(this);
    root->addWidget(_peerList, 1);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    _stopButton = new QPushButton(tr("Stop session"), this);
    _stopButton->setDefault(false);
    _stopButton->setAutoDefault(false);
    connect(_stopButton, &QPushButton::clicked, this, &WebRtcStartDialog::onStop);
    btnRow->addWidget(_stopButton);
    root->addLayout(btnRow);

    LanLiveSession *svc = LanLiveSession::instance();

    // Pre-fill from current session state in case the rendezvous already
    // returned the code before we got constructed (rare but possible —
    // worker is fast on the second hit when its TCP keep-alive is warm).
    QString existing = svc->pairingCode();
    if (!existing.isEmpty()) {
        onWanCodeReady(existing);
    } else {
        // Animated dots while waiting — same UX idea as MidiPilot's
        // "Thinking…" indicator. Plain ASCII dots so we don't repeat the
        // earlier UTF-8/Latin-1 placeholder bug. Pad to 4 chars with
        // non-breaking spaces so the label width doesn't jitter.
        _placeholderTick = 0;
        _codeLabel->setText(QStringLiteral("."));
        _statusLabel->setText(tr("Contacting rendezvous…"));
        _placeholderTimer = new QTimer(this);
        _placeholderTimer->setInterval(400);
        connect(_placeholderTimer, &QTimer::timeout, this, [this]() {
            _placeholderTick = (_placeholderTick + 1) % 4;
            int dots = _placeholderTick + 1;
            QString text = QString(dots, QChar('.'))
                + QString(4 - dots, QChar(QChar::Nbsp));
            _codeLabel->setText(text);
        });
        _placeholderTimer->start();
    }

    connect(svc, &LanLiveSession::wanCodeReady,
            this, &WebRtcStartDialog::onWanCodeReady);
    connect(svc, &LanLiveSession::peerCountChanged,
            this, &WebRtcStartDialog::onPeerCountChanged);
    connect(svc, &LanLiveSession::peerLabelsChanged,
            this, &WebRtcStartDialog::rebuildPeerList);
    connect(svc, &LanLiveSession::statusMessage,
            this, &WebRtcStartDialog::onStatusMessage);
    connect(svc, &LanLiveSession::roleChanged,
            this, [this](LanLiveSession::Role r) {
                if (r == LanLiveSession::Role::Idle) {
                    // Session ended elsewhere (Stop, error, or menu) —
                    // closeEvent is now a no-op for the session itself,
                    // so plain accept() just dismisses the window.
                    accept();
                }
            });

    rebuildPeerList();
}

void WebRtcStartDialog::onWanCodeReady(const QString &code) {
    _code = code;
    if (_placeholderTimer) {
        _placeholderTimer->stop();
        _placeholderTimer->deleteLater();
        _placeholderTimer = nullptr;
    }
    _codeLabel->setText(code);
    _copyButton->setEnabled(true);
    if (_copyInviteButton) _copyInviteButton->setEnabled(true);
    _statusLabel->setText(tr("Share this code with your peer — waiting for them to join…"));
}

void WebRtcStartDialog::onPeerCountChanged(int newCount) {
    rebuildPeerList();
    // Same UX as LAN: hide the dialog the moment the peer connects so the
    // canvas isn't covered. The session keeps running in the background;
    // user ends it via the Collab menu or by re-opening the dialog.
    if (newCount > 0 && isVisible()) {
        hide();
    }
}

void WebRtcStartDialog::rebuildPeerList() {
    if (!_peerList) return;
    _peerList->clear();
    LanLiveSession *svc = LanLiveSession::instance();
    if (svc->role() != LanLiveSession::Role::Hosting) return;

    QStringList labels = svc->peerLabels();
    if (labels.isEmpty()) {
        _peerList->addItem(tr("(waiting for peer…)"));
        return;
    }
    for (const QString &label : labels) {
        _peerList->addItem(label);
    }
}

void WebRtcStartDialog::onStatusMessage(const QString &message) {
    _statusLabel->setText(message);
}

void WebRtcStartDialog::onCopyCode() {
    if (_code.isEmpty()) return;
    QApplication::clipboard()->setText(_code);
    _statusLabel->setText(tr("✓ Code %1 copied to clipboard.").arg(_code));
}

void WebRtcStartDialog::onCopyInvite() {
    if (_code.isEmpty()) return;
    // Friendly multi-line invitation suitable for Discord / chat / email.
    // Plain text — no Markdown — so it lands the same way wherever it's
    // pasted. Mentions the 5-min TTL so the recipient knows urgency.
    QString invite = tr(
        "Join my MidiEditor AI live session.\n\n"
        "In MidiEditor: Collab → Join WAN Live Session…\n"
        "Code: %1\n\n"
        "(Code expires in 5 minutes.)").arg(_code);
    QApplication::clipboard()->setText(invite);
    _statusLabel->setText(tr("✓ Invitation copied — paste it into Discord, chat, or email."));
}

void WebRtcStartDialog::onStop() {
    LanLiveSession::instance()->leaveSession();
    accept();
}

void WebRtcStartDialog::closeEvent(QCloseEvent *event) {
    // Closing the window must NOT kill the session — same convention as
    // LanLiveStartDialog. Only the Stop button or the Collab → Leave menu
    // ends the session.
    QDialog::closeEvent(event);
}

#endif // MIDIEDITOR_WEBRTC_ENABLED
