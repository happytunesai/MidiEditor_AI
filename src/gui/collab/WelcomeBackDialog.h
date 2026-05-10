/*
 * MidiEditor AI
 *
 * WelcomeBackDialog — peer-side summary shown after a returning-peer
 * merge or fast-forward (Plan §11.10b).
 *
 * Non-blocking, single-OK dialog. Tells the user what was applied,
 * what the host rejected (if any), and where their pre-merge state
 * was preserved. No interactive choices — the merge has already
 * happened by the time this is shown.
 */

#ifndef WELCOMEBACKDIALOG_H
#define WELCOMEBACKDIALOG_H

#include <QDialog>
#include <QString>

class WelcomeBackDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * \param hostName            Display name of the session host.
     * \param acceptedHunkCount   Hunks the host accepted (or fast-
     *                            forwarded from their own history).
     * \param rejectedCommitCount Number of our commits the host
     *                            explicitly turned down (0 in pure
     *                            fast-forward case).
     * \param divergedFilePath    Where our pre-merge state was saved
     *                            on disk; empty when nothing was
     *                            rejected.
     */
    WelcomeBackDialog(const QString &hostName,
                      int acceptedHunkCount,
                      int rejectedCommitCount,
                      const QString &divergedFilePath,
                      QWidget *parent = nullptr);
};

#endif // WELCOMEBACKDIALOG_H
