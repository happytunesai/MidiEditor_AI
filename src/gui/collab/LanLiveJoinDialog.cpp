/*
 * MidiEditor AI - LanLiveJoinDialog implementation.
 */

#include "LanLiveJoinDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QVBoxLayout>

#include "../../collab/LanLiveSession.h"

LanLiveJoinDialog::LanLiveJoinDialog(MidiFile *file, QWidget *parent)
    : QDialog(parent), _file(file) {
    setWindowTitle(tr("Join LAN Live Session"));
    setModal(true);
    resize(380, 220);

    QVBoxLayout *root = new QVBoxLayout(this);

    QLabel *intro = new QLabel(
        tr("Enter the 4-character pairing code your host shared with you. "
           "Both peers must be on the same local network."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    QFormLayout *form = new QFormLayout();
    _codeEdit = new QLineEdit(this);
    _codeEdit->setMaxLength(4);
    _codeEdit->setPlaceholderText(QStringLiteral("e.g. H7T9"));
    QFont mono = _codeEdit->font();
    mono.setPointSize(mono.pointSize() + 4);
    mono.setLetterSpacing(QFont::PercentageSpacing, 140);
    _codeEdit->setFont(mono);
    // Accept both cases at the validator layer; we uppercase on textEdited
    // so users can type "h7t9" or "H7T9" and see "H7T9" in the field.
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
    form->addRow(tr("Pairing code:"), _codeEdit);
    root->addLayout(form);

    _statusLabel = new QLabel(this);
    _statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    _statusLabel->setWordWrap(true);
    root->addWidget(_statusLabel);

    root->addStretch(1);

    QDialogButtonBox *box = new QDialogButtonBox(this);
    _connectButton = box->addButton(tr("Connect"), QDialogButtonBox::AcceptRole);
    box->addButton(QDialogButtonBox::Cancel);
    connect(_connectButton, &QPushButton::clicked, this, &LanLiveJoinDialog::onConnect);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);

    _searchTimeout = new QTimer(this);
    _searchTimeout->setSingleShot(true);
    _searchTimeout->setInterval(30000);
    connect(_searchTimeout, &QTimer::timeout, this, &LanLiveJoinDialog::onSearchTimeout);

    LanLiveSession *svc = LanLiveSession::instance();
    connect(svc, &LanLiveSession::joined,
            this, &LanLiveJoinDialog::onJoined);
    connect(svc, &LanLiveSession::joinFailed,
            this, &LanLiveJoinDialog::onJoinFailed);

    _codeEdit->setFocus();
}

void LanLiveJoinDialog::onConnect() {
    QString code = _codeEdit->text().trimmed().toUpper();
    if (code.size() != 4) {
        _statusLabel->setText(tr("Code must be exactly 4 characters."));
        return;
    }
    _connectButton->setEnabled(false);
    _codeEdit->setEnabled(false);
    _statusLabel->setText(tr("Searching the LAN for code %1…").arg(code));

    LanLiveSession::instance()->joinSession(_file, code);
    _searchTimeout->start();
}

void LanLiveJoinDialog::onJoined(const QString &hostName) {
    _searchTimeout->stop();
    _statusLabel->setText(tr("✓ Connected to %1.").arg(hostName));
    QTimer::singleShot(800, this, &QDialog::accept);
}

void LanLiveJoinDialog::onJoinFailed(const QString &reason) {
    _searchTimeout->stop();
    _statusLabel->setText(tr("⚠ %1").arg(reason));
    _connectButton->setEnabled(true);
    _codeEdit->setEnabled(true);
}

void LanLiveJoinDialog::onSearchTimeout() {
    LanLiveSession::instance()->leaveSession();  // stops the multicast listener
    _statusLabel->setText(tr("⚠ No host with code %1 found in 30 seconds. "
                             "Make sure both peers are on the same network and the host has clicked Start.")
                                .arg(_codeEdit->text().toUpper()));
    _connectButton->setEnabled(true);
    _codeEdit->setEnabled(true);
}
