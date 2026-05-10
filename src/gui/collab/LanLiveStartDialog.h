/*
 * MidiEditor AI
 *
 * Non-modal dialog the host sees while a LAN Live Session is active.
 * Shows the pairing code, live peer count + names, and a Stop button.
 * Closing the dialog stops the session.
 */

#ifndef LANLIVESTARTDIALOG_H
#define LANLIVESTARTDIALOG_H

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;

class LanLiveStartDialog : public QDialog {
    Q_OBJECT

public:
    explicit LanLiveStartDialog(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onPeerCountChanged(int newCount);
    void onStatusMessage(const QString &message);
    void onStop();
    void onCopyCode();
    void onCopyInvite();
    void rebuildPeerList();

private:

    QLabel *_codeLabel;
    QLabel *_statusLabel;
    QListWidget *_peerList;
    QPushButton *_copyButton = nullptr;
    QPushButton *_copyInviteButton = nullptr;
    QPushButton *_stopButton;
};

#endif // LANLIVESTARTDIALOG_H
