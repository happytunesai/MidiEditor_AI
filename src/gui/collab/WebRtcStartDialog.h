/*
 * MidiEditor AI
 *
 * WebRtcStartDialog — host-side dialog for the code-based WAN session
 * flow (Plan §11.10, Phase 9.6).
 *
 * Mirrors LanLiveStartDialog as closely as possible: a thin presentation
 * layer over LanLiveSession. The MainWindow calls
 * \c LanLiveSession::startHostingWan(file) before constructing the dialog;
 * the dialog itself just listens for:
 *
 *   • wanCodeReady     — render the 4-character code to the user
 *   • statusMessage    — surface progress/error text from the session
 *   • peerCountChanged — auto-hide once a peer has connected
 *   • peerLabelsChanged — refresh the peer-name list
 *   • roleChanged(Idle) — close when the session ends elsewhere
 *
 * Closing the window does NOT end the session — only the explicit Stop
 * button (or the Collab → Leave menu) does. This matches the LAN dialog
 * so users can dismiss the floating dialog and keep editing.
 */

#ifndef WEBRTCSTARTDIALOG_H
#define WEBRTCSTARTDIALOG_H

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include <QDialog>
#include <QString>

class QLabel;
class QListWidget;
class QPushButton;
class QTimer;

class WebRtcStartDialog : public QDialog {
    Q_OBJECT

public:
    explicit WebRtcStartDialog(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onWanCodeReady(const QString &code);
    void onPeerCountChanged(int newCount);
    void onStatusMessage(const QString &message);
    void onCopyCode();
    void onCopyInvite();
    void onStop();
    void rebuildPeerList();

private:
    QString _code;

    QLabel *_codeLabel = nullptr;
    QLabel *_statusLabel = nullptr;
    QListWidget *_peerList = nullptr;
    QPushButton *_copyButton = nullptr;
    QPushButton *_copyInviteButton = nullptr;
    QPushButton *_stopButton = nullptr;

    /** \brief Animated placeholder while waiting for the rendezvous to
     *  return the 4-char code. Cycles through 1-4 ASCII dots once per
     *  500 ms so the dialog has visible motion instead of a static
     *  garbled label. Stopped + nulled when the real code arrives. */
    QTimer *_placeholderTimer = nullptr;
    int _placeholderTick = 0;
};

#endif // MIDIEDITOR_WEBRTC_ENABLED

#endif // WEBRTCSTARTDIALOG_H
