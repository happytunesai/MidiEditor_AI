/*
 * MidiEditor AI - ReturningPeerDialog implementation.
 */

#include "ReturningPeerDialog.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "../../collab/LanLiveSession.h"
#include "PrReviewDialog.h"

ReturningPeerDialog::ReturningPeerDialog(const QString &peerName,
                                          const QString &peerToken,
                                          const PrBundle &proposedBundle,
                                          MidiFile *targetFile,
                                          QWidget *parent)
    : QDialog(parent), _peerName(peerName), _peerToken(peerToken),
      _bundle(proposedBundle), _file(targetFile) {
    setWindowTitle(tr("Returning peer"));
    setModal(true);
    resize(480, 260);

    QVBoxLayout *root = new QVBoxLayout(this);

    int hunkCount = _bundle.hunks.size();
    QString summary = tr(
        "<b>%1 wants to merge their changes into your session.</b><br>"
        "While they were away they made changes that you don't have:<br>"
        "<b>%2 hunk(s)</b> across the file.")
        .arg(_peerName.toHtmlEscaped()).arg(hunkCount);

    QLabel *introLabel = new QLabel(summary, this);
    introLabel->setWordWrap(true);
    introLabel->setTextFormat(Qt::RichText);
    root->addWidget(introLabel);

    if (!_bundle.message.isEmpty()) {
        QLabel *msgLabel = new QLabel(
            tr("<i>Latest commit:</i> %1").arg(_bundle.message.toHtmlEscaped()),
            this);
        msgLabel->setWordWrap(true);
        msgLabel->setTextFormat(Qt::RichText);
        msgLabel->setStyleSheet("color: gray; padding: 4px 0;");
        root->addWidget(msgLabel);
    }

    QLabel *hint = new QLabel(
        tr("• <b>Accept all</b> merges every hunk and broadcasts the "
           "result to the peer.<br>"
           "• <b>Review…</b> lets you cherry-pick which hunks to keep "
           "(rejected ones are reverted on the peer side).<br>"
           "• <b>Reject</b> disconnects %1 — their changes stay on "
           "their machine only.").arg(_peerName.toHtmlEscaped()),
        this);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);
    hint->setStyleSheet("color: gray; font-size: 11px; padding: 8px 0;");
    root->addWidget(hint);

    root->addStretch(1);

    QHBoxLayout *btns = new QHBoxLayout();
    btns->addStretch(1);
    QPushButton *rejectBtn = new QPushButton(tr("Reject"), this);
    QPushButton *reviewBtn = new QPushButton(tr("Review…"), this);
    QPushButton *acceptAllBtn = new QPushButton(tr("Accept all"), this);
    acceptAllBtn->setDefault(true);
    btns->addWidget(rejectBtn);
    btns->addWidget(reviewBtn);
    btns->addWidget(acceptAllBtn);
    root->addLayout(btns);

    connect(acceptAllBtn, &QPushButton::clicked, this, &ReturningPeerDialog::onAcceptAll);
    connect(reviewBtn,    &QPushButton::clicked, this, &ReturningPeerDialog::onReview);
    connect(rejectBtn,    &QPushButton::clicked, this, &ReturningPeerDialog::onReject);
}

void ReturningPeerDialog::onAcceptAll() {
    LanLiveSession::instance()->acceptReturningPeerMerge(
        _peerToken, _bundle.hunks, /*rejectedHunks=*/QJsonArray(),
        /*rejectedCommitHashes=*/QStringList());
    accept();
}

void ReturningPeerDialog::onReview() {
    // Open the existing PrReviewDialog (already shipped, see Phase
    // 9.1e). It applies the host's selection to our local file and
    // exposes the accept/reject lists via its accessors so we can
    // forward them to the peer for convergence.
    PrReviewDialog dlg(_bundle, _file, PrReviewDialog::Mode::PreApply, this);
    if (dlg.exec() != QDialog::Accepted) {
        // User cancelled the review — we leave the returning-peer
        // dialog open so they can pick another action.
        return;
    }
    QList<QJsonObject> accepted = dlg.selectedHunks();
    QList<QJsonObject> rejected = dlg.rejectedHunks();
    QJsonArray acceptedArr;
    for (const QJsonObject &h : accepted) acceptedArr.append(h);
    QJsonArray rejectedArr;
    for (const QJsonObject &h : rejected) rejectedArr.append(h);
    LanLiveSession::instance()->acceptReturningPeerMerge(
        _peerToken, acceptedArr, rejectedArr, /*rejectedCommitHashes=*/QStringList());
    accept();
}

void ReturningPeerDialog::onReject() {
    bool ok = false;
    QString reason = QInputDialog::getText(
        this, tr("Reject merge"),
        tr("Reason (optional, shown to %1):").arg(_peerName),
        QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    LanLiveSession::instance()->rejectReturningPeer(_peerToken, reason);
    reject();
}
