#include "AutoSaveScheduler.h"

#include <QSettings>

AutoSaveScheduler::AutoSaveScheduler(QSettings *settings, QObject *parent)
    : QObject(parent), _settings(settings) {
    _timer.setSingleShot(true);
    connect(&_timer, &QTimer::timeout, this, [this]() {
        // The setting may have changed while the period was running (the
        // Performance page writes it on toggle, and Esc / the window's X close
        // the dialog without any notification). A period that was armed while
        // auto-save was on must not write a backup after it went off.
        if (!enabledIn(_settings)) {
            return;
        }
        emit due();
    });
}

void AutoSaveScheduler::noteEdit() {
    if (!enabledIn(_settings)) {
        _timer.stop();
        return;
    }
    _timer.start(intervalMsIn(_settings));
}

void AutoSaveScheduler::stop() {
    _timer.stop();
}

bool AutoSaveScheduler::isArmed() const {
    return _timer.isActive();
}

bool AutoSaveScheduler::enabledIn(const QSettings *settings) {
    return !settings || settings->value(QStringLiteral("autosave_enabled"), true).toBool();
}

int AutoSaveScheduler::intervalMsIn(const QSettings *settings) {
    const int seconds = settings
        ? settings->value(QStringLiteral("autosave_interval"), 120).toInt()
        : 120;
    return qMax(1, seconds) * 1000;
}
