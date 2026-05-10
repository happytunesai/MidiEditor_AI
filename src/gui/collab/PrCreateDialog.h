/*
 * MidiEditor AI
 *
 * Dialog for creating a PR aggregating all unshared changes — every
 * commit between the file's last shared head and its current head
 * collapses into one PR per Plan §10.3 (revisited 2026-05-05).
 *
 * Three share paths, all using the same in-memory aggregated PrBundle:
 *   - Copy token   → places the smart-paste token on the system clipboard
 *   - Save bundle… → writes a `.midiedit-pr.json` to a user-chosen path
 *   - Post webhook → POSTs a Discord embed to the configured webhook URL
 *
 * Any successful share marks the current head as the new "lastSharedHead"
 * via CollabService::markCurrentAsShared(), so the next time the dialog
 * opens it only aggregates commits made since this share.
 *
 * The message field defaults to the most recent commit's message and is
 * editable so the user can give a more descriptive title than the
 * auto-generated "Save".
 */

#ifndef PRCREATEDIALOG_H
#define PRCREATEDIALOG_H

#include <QDialog>
#include <QJsonObject>
#include <QMetaObject>

class QLabel;
class QLineEdit;
class QPushButton;
class PrBundle;

class PrCreateDialog : public QDialog {
    Q_OBJECT

public:
    explicit PrCreateDialog(QWidget *parent = nullptr);

private slots:
    void onCopyToken();
    void onSaveBundle();
    void onPostWebhook();
    void onMessageEdited();

private:
    void rebuildSummary();
    QJsonObject buildAggregatedBundleObject() const;

    QLabel *_summaryLabel;
    QLineEdit *_messageEdit;
    QLabel *_sizeStatusLabel;
    QPushButton *_copyButton;
    QPushButton *_saveButton;
    QPushButton *_postButton;

    int _commitsCount = 0;

    // Single-use connection to WebhookClient::postFinished. Disconnected
    // and re-connected on every Post-to-Discord click so we don't
    // accumulate stale lambdas across multiple posts in one dialog session.
    QMetaObject::Connection _webhookConn;
};

#endif // PRCREATEDIALOG_H
