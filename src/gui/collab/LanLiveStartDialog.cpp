/*
 * MidiEditor AI - LanLiveStartDialog implementation.
 */

#include "LanLiveStartDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "../../collab/LanLiveSession.h"
#include "../../collab/LanTransport.h"

LanLiveStartDialog::LanLiveStartDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("LAN Live Session"));
    setModal(false);
    resize(420, 320);

    QVBoxLayout *root = new QVBoxLayout(this);

    QLabel *intro = new QLabel(
        tr("<b>You're hosting a LAN Live Session.</b><br>"
           "Share the code with peers on the same network. "
           "Their edits will flow into your file every second, and yours into theirs."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    _codeLabel = new QLabel(this);
    _codeLabel->setAlignment(Qt::AlignCenter);
    _codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont codeFont = _codeLabel->font();
    codeFont.setPointSize(codeFont.pointSize() + 14);
    codeFont.setBold(true);
    codeFont.setLetterSpacing(QFont::PercentageSpacing, 130);
    _codeLabel->setFont(codeFont);
    _codeLabel->setStyleSheet("padding: 12px; background: rgba(255,144,32,0.15); border-radius: 6px;");
    root->addWidget(_codeLabel);

    _statusLabel = new QLabel(this);
    _statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    _statusLabel->setWordWrap(true);
    root->addWidget(_statusLabel);

    // Copy buttons — match the WAN dialog's UX (Plan §11.10m polish).
    QHBoxLayout *codeBtnRow = new QHBoxLayout();
    codeBtnRow->addStretch(1);
    _copyButton = new QPushButton(tr("Copy code"), this);
    _copyButton->setAutoDefault(false);
    _copyButton->setToolTip(tr("Copy just the 4-character code to the clipboard."));
    connect(_copyButton, &QPushButton::clicked, this, &LanLiveStartDialog::onCopyCode);
    codeBtnRow->addWidget(_copyButton);
    _copyInviteButton = new QPushButton(tr("Copy invite"), this);
    _copyInviteButton->setAutoDefault(false);
    _copyInviteButton->setToolTip(tr("Copy a friendly invitation message you can paste "
                                      "into Discord / chat / email."));
    connect(_copyInviteButton, &QPushButton::clicked, this, &LanLiveStartDialog::onCopyInvite);
    codeBtnRow->addWidget(_copyInviteButton);
    root->addLayout(codeBtnRow);

    QLabel *peerHeader = new QLabel(tr("<b>Connected peers</b>"), this);
    root->addWidget(peerHeader);

    _peerList = new QListWidget(this);
    root->addWidget(_peerList, 1);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    _stopButton = new QPushButton(tr("Stop session"), this);
    _stopButton->setDefault(false);
    _stopButton->setAutoDefault(false);
    connect(_stopButton, &QPushButton::clicked, this, &LanLiveStartDialog::onStop);
    btnRow->addWidget(_stopButton);
    root->addLayout(btnRow);

    LanLiveSession *svc = LanLiveSession::instance();
    _codeLabel->setText(svc->pairingCode());
    connect(svc, &LanLiveSession::peerCountChanged,
            this, &LanLiveStartDialog::onPeerCountChanged);
    connect(svc, &LanLiveSession::peerLabelsChanged,
            this, &LanLiveStartDialog::rebuildPeerList);
    connect(svc, &LanLiveSession::statusMessage,
            this, &LanLiveStartDialog::onStatusMessage);
    connect(svc, &LanLiveSession::roleChanged,
            this, [this](LanLiveSession::Role r) {
                if (r == LanLiveSession::Role::Idle) {
                    // Session ended elsewhere (Stop button, menu, or error).
                    // closeEvent is now a no-op for the session, so plain
                    // accept() is enough.
                    accept();
                }
            });

    onPeerCountChanged(svc->peerCount());
}

void LanLiveStartDialog::onPeerCountChanged(int newCount) {
    rebuildPeerList();
    // Auto-hide the dialog the moment the first peer joins so the host can
    // edit without the dialog covering the canvas. The session keeps running
    // in the background; the user ends it via the Collab menu's "Leave LAN
    // Live Session" entry or by re-opening this dialog and pressing Stop.
    if (newCount > 0 && isVisible()) {
        hide();
    }
}

void LanLiveStartDialog::rebuildPeerList() {
    _peerList->clear();
    LanLiveSession *svc = LanLiveSession::instance();
    if (svc->role() != LanLiveSession::Role::Hosting) return;

    QStringList labels = svc->peerLabels();
    if (labels.isEmpty()) {
        _peerList->addItem(tr("(waiting for someone to join…)"));
        return;
    }
    for (const QString &label : labels) {
        _peerList->addItem(label);
    }
}

void LanLiveStartDialog::onStatusMessage(const QString &message) {
    _statusLabel->setText(message);
}

void LanLiveStartDialog::onStop() {
    LanLiveSession::instance()->leaveSession();
    accept();
}

void LanLiveStartDialog::onCopyCode() {
    QString code = LanLiveSession::instance()->pairingCode();
    if (code.isEmpty()) return;
    QApplication::clipboard()->setText(code);
    _statusLabel->setText(tr("✓ Code %1 copied to clipboard.").arg(code));
}

void LanLiveStartDialog::onCopyInvite() {
    QString code = LanLiveSession::instance()->pairingCode();
    if (code.isEmpty()) return;
    // LAN flavour mentions the same-network requirement instead of the
    // 5-min TTL the WAN flow has.
    QString invite = tr(
        "Join my MidiEditor AI LAN live session.\n\n"
        "In MidiEditor: Collab → Join LAN Live Session…\n"
        "Code: %1\n\n"
        "(Both PCs must be on the same local network.)").arg(code);
    QApplication::clipboard()->setText(invite);
    _statusLabel->setText(tr("✓ Invitation copied — paste it into Discord, chat, or email."));
}

void LanLiveStartDialog::closeEvent(QCloseEvent *event) {
    // Closing the window must NOT kill the session — the user expects to
    // keep editing once the dialog is dismissed. Only "Stop session" (or
    // the Collab → Leave menu, or a remote-driven Idle transition) ends
    // hosting.
    QDialog::closeEvent(event);
}
