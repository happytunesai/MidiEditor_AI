/*
 * MidiEditor AI - PrCreateDialog implementation.
 */

#include "PrCreateDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "../../collab/CollabService.h"
#include "../../collab/PrBundle.h"
#include "../../collab/WebhookClient.h"

namespace {

/**
 * Aggregate hunks across all commits between lastSharedHead and currentHead.
 *
 * Algorithm:
 *   - Walk history forward.
 *   - Skip entries up to and including lastSharedHead (already shared).
 *   - Concat hunks of every later entry into one flat array.
 *
 * Concatenation is correct semantically: the receiver applies hunks in
 * order, so "added X" then "removed X" cancels out — same net state as
 * if we had done a snapshot diff. Cheaper than re-snapshotting.
 *
 * \return  pair: (aggregated hunks array, count of commits aggregated).
 */
struct AggregateResult {
    QJsonArray hunks;
    int commitCount = 0;
    QString parentHash;       // = lastSharedHead, or "" if nothing shared yet (initial)
    bool anchorMissing = false;  // lastSharedHead set but not found in history
};

AggregateResult aggregateUnshared() {
    AggregateResult r;
    CollabService *svc = CollabService::instance();
    QJsonArray hist = svc->history();
    if (hist.isEmpty()) return r;

    QString lastShared = svc->lastSharedHead();
    r.parentHash = lastShared;  // bundle.parentHash = state PR was based on

    bool collecting = lastShared.isEmpty();  // empty → take everything
    bool anchorFound = collecting;
    for (const QJsonValue &v : hist) {
        QJsonObject e = v.toObject();
        if (collecting) {
            r.commitCount++;
            QJsonArray hunks = e.value(QStringLiteral("hunks")).toArray();
            for (const QJsonValue &h : hunks) {
                r.hunks.append(h);
            }
        }
        if (!collecting && e.value(QStringLiteral("hash")).toString() == lastShared) {
            collecting = true;
            anchorFound = true;
        }
    }
    // BUG-COLLAB-010: lastSharedHead references a hash that's no longer
    // in our history (sidecar restored from backup, hand-edited, etc).
    // Fall back to "aggregate everything" so legitimate unshared changes
    // aren't silently hidden.
    if (!anchorFound && !lastShared.isEmpty()) {
        r.hunks = QJsonArray();
        r.commitCount = 0;
        r.anchorMissing = true;
        for (const QJsonValue &v : hist) {
            QJsonObject e = v.toObject();
            r.commitCount++;
            QJsonArray hunks = e.value(QStringLiteral("hunks")).toArray();
            for (const QJsonValue &h : hunks) {
                r.hunks.append(h);
            }
        }
        r.parentHash.clear();  // we don't actually know what we forked from
    }
    return r;
}

PrBundle bundleFromAggregate(const QString &userMessage) {
    PrBundle b;
    CollabService *svc = CollabService::instance();
    QJsonArray hist = svc->history();
    if (hist.isEmpty()) return b;

    AggregateResult agg = aggregateUnshared();
    if (agg.commitCount == 0) return b;  // nothing unshared

    // Author + machineId come from the local user (the one creating
    // the PR), not from any historical entry. The PR author is the
    // person sharing the changes, even if the underlying commits had
    // mixed authors (e.g. local edits + previously merged PRs).
    QJsonObject head = hist.last().toObject();
    b.sessionId = svc->sessionId();
    b.author = head.value(QStringLiteral("author")).toString();
    b.machineId = head.value(QStringLiteral("machineId")).toString();
    b.parentHash = agg.parentHash;
    b.timestamp = QDateTime::currentSecsSinceEpoch();
    b.message = userMessage;
    b.hunks = agg.hunks;
    return b;
}

QString humanByteSize(int bytes) {
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    double kb = bytes / 1024.0;
    return QString::number(kb, 'f', 1) + QStringLiteral(" KB");
}

}

QJsonObject PrCreateDialog::buildAggregatedBundleObject() const {
    PrBundle b = bundleFromAggregate(_messageEdit->text());
    QJsonObject o;
    o.insert(QStringLiteral("valid"), b.isValid());
    o.insert(QStringLiteral("hunkCount"), b.hunks.size());
    o.insert(QStringLiteral("token"), b.toInlineToken());
    return o;
}

PrCreateDialog::PrCreateDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Create PR"));
    setModal(true);
    resize(600, 280);

    QVBoxLayout *layout = new QVBoxLayout(this);

    _summaryLabel = new QLabel(this);
    _summaryLabel->setWordWrap(true);
    _summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(_summaryLabel);

    QFormLayout *form = new QFormLayout();
    _messageEdit = new QLineEdit(this);
    _messageEdit->setPlaceholderText(tr("Describe what changed (visible to peers)"));
    form->addRow(tr("Message:"), _messageEdit);
    layout->addLayout(form);

    layout->addStretch(1);

    _sizeStatusLabel = new QLabel(this);
    _sizeStatusLabel->setStyleSheet("color: gray; font-size: 11px;");
    _sizeStatusLabel->setWordWrap(true);
    layout->addWidget(_sizeStatusLabel);

    QDialogButtonBox *box = new QDialogButtonBox(this);
    _copyButton = box->addButton(tr("Copy token"), QDialogButtonBox::ActionRole);
    _saveButton = box->addButton(tr("Save bundle..."), QDialogButtonBox::ActionRole);
    _postButton = box->addButton(tr("Post to Discord"), QDialogButtonBox::ActionRole);
    box->addButton(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(_copyButton, &QPushButton::clicked, this, &PrCreateDialog::onCopyToken);
    connect(_saveButton, &QPushButton::clicked, this, &PrCreateDialog::onSaveBundle);
    connect(_postButton, &QPushButton::clicked, this, &PrCreateDialog::onPostWebhook);
    layout->addWidget(box);

    connect(_messageEdit, &QLineEdit::textEdited, this, &PrCreateDialog::onMessageEdited);

    // Prefill message from the most recent unshared commit so the dialog
    // has a reasonable default. User can replace it before sharing.
    AggregateResult preview = aggregateUnshared();
    if (preview.commitCount > 0) {
        CollabService *svc = CollabService::instance();
        QJsonArray hist = svc->history();
        if (!hist.isEmpty()) {
            _messageEdit->setText(hist.last().toObject().value(QStringLiteral("message")).toString());
        }
    }
    rebuildSummary();
}

void PrCreateDialog::onMessageEdited() {
    rebuildSummary();
}

void PrCreateDialog::rebuildSummary() {
    PrBundle b = bundleFromAggregate(_messageEdit->text());
    AggregateResult agg = aggregateUnshared();
    _commitsCount = agg.commitCount;
    bool hasContent = b.isValid() && agg.commitCount > 0;

    bool webhookConfigured = !QSettings("MidiEditor", "NONE")
        .value("Collab/webhookUrl").toString().trimmed().isEmpty();

    _copyButton->setEnabled(hasContent);
    _saveButton->setEnabled(hasContent);
    _postButton->setEnabled(hasContent && webhookConfigured);
    _postButton->setToolTip(webhookConfigured
        ? tr("POST a Discord embed with the smart-paste token + bundle attachment.")
        : tr("Configure a webhook URL in Settings → Collaboration first."));

    if (!hasContent) {
        _summaryLabel->setText(tr(
            "<i>Nothing to share — no new commits since your last PR. "
            "Save some changes first, then come back here.</i>"));
        _sizeStatusLabel->clear();
        return;
    }

    int totalAdded = 0, totalRemoved = 0, totalModified = 0;
    for (const QJsonValue &hv : b.hunks) {
        QJsonObject h = hv.toObject();
        totalAdded += h.value(QStringLiteral("added")).toArray().size();
        totalRemoved += h.value(QStringLiteral("removed")).toArray().size();
        totalModified += h.value(QStringLiteral("modified")).toArray().size();
    }

    QString sessionPrefix = b.sessionId.left(8);
    QString summary = tr(
        "<b>From session</b> <code>%1…</code> &nbsp; <b>Author:</b> %2<br>"
        "<b>%3 commits since last share &nbsp;·&nbsp; %4 hunks:</b> "
        "+%5 added &nbsp; ⋅%6 removed &nbsp; ~%7 modified")
        .arg(sessionPrefix.toHtmlEscaped(),
             b.author.toHtmlEscaped())
        .arg(agg.commitCount)
        .arg(b.hunks.size())
        .arg(totalAdded).arg(totalRemoved).arg(totalModified);
    if (agg.anchorMissing) {
        summary += tr("<br><span style='color: #e0a020;'>⚠ Last-shared anchor not found in history — "
                      "showing all commits (sidecar may have been restored or edited).</span>");
    }
    _summaryLabel->setText(summary);

    QString token = b.toInlineToken();
    int tokenBytes = token.toUtf8().size();
    QString sizeNote;
    if (tokenBytes <= 3500) {
        sizeNote = tr("Token size: %1 — fits in a Discord embed code block. Recipient pastes with Ctrl+V.")
                       .arg(humanByteSize(tokenBytes));
    } else if (tokenBytes <= 90000) {
        sizeNote = tr("Token size: %1 — too large for inline chat. Use Save bundle… and share the file, "
                      "or Post to Discord (the bundle is uploaded as an attachment).")
                       .arg(humanByteSize(tokenBytes));
    } else {
        sizeNote = tr("Token size: %1 — very large. Discord attachment limit is 25 MB; sharing should still work, "
                      "but consider splitting the work into multiple smaller PRs.")
                       .arg(humanByteSize(tokenBytes));
    }
    _sizeStatusLabel->setText(sizeNote);
}

void PrCreateDialog::onCopyToken() {
    PrBundle b = bundleFromAggregate(_messageEdit->text());
    if (!b.isValid()) return;
    QString token = b.toInlineToken();
    QApplication::clipboard()->setText(token);
    _sizeStatusLabel->setText(tr("✓ Token copied to clipboard (%1). Paste it anywhere — Discord, DM, email, …")
                                  .arg(humanByteSize(token.toUtf8().size())));
    CollabService::instance()->markCurrentAsShared();
    rebuildSummary();  // re-evaluates hasContent → buttons grey out
}

void PrCreateDialog::onSaveBundle() {
    PrBundle b = bundleFromAggregate(_messageEdit->text());
    if (!b.isValid()) return;

    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString suggestedName = QStringLiteral("%1-%2%3")
                                .arg(b.author)
                                .arg(QDateTime::fromSecsSinceEpoch(b.timestamp).toString("yyyyMMdd-HHmmss"))
                                .arg(PrBundle::kBundleFileExtension);
    QString path = QFileDialog::getSaveFileName(this,
        tr("Save PR bundle"),
        defaultDir + QLatin1Char('/') + suggestedName,
        tr("MidiEditor PR bundle (*.midiedit-pr.json)"));
    if (path.isEmpty()) return;

    if (!b.saveBundleToFile(path)) {
        QMessageBox::warning(this, tr("Save failed"),
            tr("Could not write the bundle file. Check directory permissions."));
        return;
    }
    _sizeStatusLabel->setText(tr("✓ Bundle saved to %1").arg(QFileInfo(path).fileName()));
    CollabService::instance()->markCurrentAsShared();
    rebuildSummary();
}

void PrCreateDialog::onPostWebhook() {
    PrBundle b = bundleFromAggregate(_messageEdit->text());
    if (!b.isValid()) return;

    QString webhookUrl = QSettings("MidiEditor", "NONE")
        .value("Collab/webhookUrl").toString().trimmed();
    if (webhookUrl.isEmpty()) return;

    QString fileLabel = tr("PR (%1 hunks)").arg(b.hunks.size());

    // Replace any prior connection so multi-clicks don't stack lambdas.
    // markCurrentAsShared is deferred until the POST returns success —
    // a 4xx/5xx response shouldn't burn the shareable head.
    auto *wc = WebhookClient::instance();
    if (_webhookConn) disconnect(_webhookConn);
    _webhookConn = connect(wc, &WebhookClient::postFinished,
        this, [this](bool ok, const QString &msg) {
            QString prefix = ok ? QStringLiteral("✓ ") : QStringLiteral("⚠ ");
            _sizeStatusLabel->setText(prefix + msg);
            if (ok) {
                CollabService::instance()->markCurrentAsShared();
                rebuildSummary();  // re-evaluate hasContent → buttons grey
            }
            // One-shot: drop our handler so a second click sets up fresh.
            disconnect(_webhookConn);
            _webhookConn = QMetaObject::Connection();
        }, Qt::QueuedConnection);

    wc->postPr(webhookUrl, b, fileLabel);

    _sizeStatusLabel->setText(tr("Posting to Discord webhook… (size: %1)")
                                  .arg(humanByteSize(b.toInlineToken().toUtf8().size())));
}
