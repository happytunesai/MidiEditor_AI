#ifndef AUTOSAVESCHEDULER_H
#define AUTOSAVESCHEDULER_H

#include <QObject>
#include <QTimer>

class QSettings;

/**
 * \class AutoSaveScheduler
 *
 * \brief The debounce behind auto-save: every edit re-arms a quiet period,
 *        \ref due fires when it elapses - if auto-save is still switched on.
 *
 * The "autosave_enabled" setting is consulted twice. When arming, so a
 * disabled auto-save never starts the period at all. And again when the timer
 * fires, which is the point of this class: the Performance page writes the
 * setting the moment the box is toggled, without telling the main window, so
 * a period armed by an earlier edit used to run to completion and write one
 * more backup after the user had switched auto-save off (external review
 * 2026-09-06, SP-05). Reading the setting at fire time drops that backup no
 * matter how the settings dialog was closed afterwards.
 */
class AutoSaveScheduler : public QObject {
    Q_OBJECT

public:
    explicit AutoSaveScheduler(QSettings *settings, QObject *parent = nullptr);

    /** An edit happened: (re)start the quiet period when auto-save is enabled. */
    void noteEdit();

    /** Drop a pending period (save done, nothing dirty, shutdown). */
    void stop();

    /** Whether a quiet period is currently running. */
    bool isArmed() const;

    /** "autosave_enabled" (default on). */
    static bool enabledIn(const QSettings *settings);

    /** "autosave_interval" in seconds (default 120, at least 1) as milliseconds. */
    static int intervalMsIn(const QSettings *settings);

signals:
    /** The quiet period elapsed while auto-save was still enabled. */
    void due();

private:
    QSettings *_settings;
    QTimer _timer;
};

#endif // AUTOSAVESCHEDULER_H
