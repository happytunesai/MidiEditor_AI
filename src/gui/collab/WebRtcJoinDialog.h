/*
 * MidiEditor AI
 *
 * WebRtcJoinDialog — peer-side dialog for the code-based WAN session
 * flow (Plan §11.10, Phase 9.6). Companion to WebRtcStartDialog.
 *
 * The user enters the 4-character code their host read out, and we
 * drive \c LanLiveSession::joinSessionWan from there. Auto-closes on
 * successful connection; surfaces a clear error if the rendezvous
 * doesn't have a matching offer or the WebRTC handshake fails.
 *
 * Modal because the user isn't expected to edit while waiting to
 * connect (and may not even have a file open — the host can ship its
 * own via the same filetransfer flow as the LAN dialog).
 */

#ifndef WEBRTCJOINDIALOG_H
#define WEBRTCJOINDIALOG_H

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include <QDialog>
#include <QString>

class MidiFile;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

class WebRtcJoinDialog : public QDialog {
    Q_OBJECT

public:
    explicit WebRtcJoinDialog(MidiFile *file = nullptr, QWidget *parent = nullptr);

private slots:
    void onConnect();
    void onJoined(const QString &hostName);
    void onJoinFailed(const QString &reason);
    void onStatusMessage(const QString &message);
    void onSearchTimeout();

private:
    MidiFile *_file = nullptr;
    QLineEdit *_codeEdit = nullptr;
    QLabel *_statusLabel = nullptr;
    QPushButton *_connectButton = nullptr;
    QTimer *_searchTimeout = nullptr;
};

#endif // MIDIEDITOR_WEBRTC_ENABLED

#endif // WEBRTCJOINDIALOG_H
