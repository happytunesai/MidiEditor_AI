/*
 * MidiEditor AI
 *
 * Modal dialog for joining a LAN Live Session by pairing code.
 * Auto-closes on successful connection; surfaces a clear error
 * if the host can't be found within ~30 seconds.
 */

#ifndef LANLIVEJOINDIALOG_H
#define LANLIVEJOINDIALOG_H

#include <QDialog>

class MidiFile;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

class LanLiveJoinDialog : public QDialog {
    Q_OBJECT

public:
    explicit LanLiveJoinDialog(MidiFile *file, QWidget *parent = nullptr);

private slots:
    void onConnect();
    void onJoined(const QString &hostName);
    void onJoinFailed(const QString &reason);
    void onSearchTimeout();

private:
    MidiFile *_file;
    QLineEdit *_codeEdit;
    QLabel *_statusLabel;
    QPushButton *_connectButton;
    QTimer *_searchTimeout;
};

#endif // LANLIVEJOINDIALOG_H
