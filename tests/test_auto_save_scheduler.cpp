/*
 * test_auto_save_scheduler
 *
 * SP-05 (external system/performance review, 2026-09-06): a backup period
 * armed by an edit ran to completion after auto-save was switched off on the
 * Performance page, writing one more backup. AutoSaveScheduler re-reads the
 * setting when the period elapses. Pinned here with a real QSettings file
 * and real timers:
 *   - enabled + edit -> exactly one due() after the quiet period
 *   - armed, then disabled -> no due(), the period is dropped
 *   - disabled -> an edit never arms; re-enabled -> the next edit arms
 *   - stop() disarms; the settings readers fall back on damaged values
 */

#include <QtTest/QtTest>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "../src/gui/AutoSaveScheduler.h"

class TestAutoSaveScheduler : public QObject {
    Q_OBJECT

private:
    QTemporaryDir _dir;
    QSettings *_settings = nullptr;

private slots:
    void init()
    {
        QVERIFY(_dir.isValid());
        _settings = new QSettings(_dir.path() + QStringLiteral("/autosave.ini"),
                                  QSettings::IniFormat);
        _settings->clear();
        _settings->setValue(QStringLiteral("autosave_enabled"), true);
        _settings->setValue(QStringLiteral("autosave_interval"), 1);
    }

    void cleanup()
    {
        delete _settings;
        _settings = nullptr;
    }

    void enabledEditFiresOnce()
    {
        AutoSaveScheduler s(_settings);
        QSignalSpy spy(&s, &AutoSaveScheduler::due);
        s.noteEdit();
        QVERIFY(s.isArmed());
        QVERIFY(spy.wait(2500));
        QCOMPARE(spy.count(), 1);
        QVERIFY(!s.isArmed());
        QVERIFY(!spy.wait(1500)); // single shot: no second backup without an edit
        QCOMPARE(spy.count(), 1);
    }

    void disabledAfterArmingDoesNotFire()
    {
        AutoSaveScheduler s(_settings);
        QSignalSpy spy(&s, &AutoSaveScheduler::due);
        s.noteEdit();
        QVERIFY(s.isArmed());
        // The Performance page's toggle: written at once, nobody is told.
        _settings->setValue(QStringLiteral("autosave_enabled"), false);
        QVERIFY(!spy.wait(2000));
        QCOMPARE(spy.count(), 0);
        QVERIFY(!s.isArmed());
    }

    void disabledNeverArms()
    {
        _settings->setValue(QStringLiteral("autosave_enabled"), false);
        AutoSaveScheduler s(_settings);
        s.noteEdit();
        QVERIFY(!s.isArmed());
    }

    void reenabledEditArmsAgain()
    {
        _settings->setValue(QStringLiteral("autosave_enabled"), false);
        AutoSaveScheduler s(_settings);
        s.noteEdit();
        QVERIFY(!s.isArmed());
        _settings->setValue(QStringLiteral("autosave_enabled"), true);
        s.noteEdit();
        QVERIFY(s.isArmed());
    }

    void stopDisarms()
    {
        AutoSaveScheduler s(_settings);
        s.noteEdit();
        QVERIFY(s.isArmed());
        s.stop();
        QVERIFY(!s.isArmed());
    }

    void settingsReaders()
    {
        QCOMPARE(AutoSaveScheduler::enabledIn(_settings), true);
        QCOMPARE(AutoSaveScheduler::intervalMsIn(_settings), 1000);
        _settings->setValue(QStringLiteral("autosave_interval"), 0); // damaged value
        QCOMPARE(AutoSaveScheduler::intervalMsIn(_settings), 1000);
        _settings->remove(QStringLiteral("autosave_interval"));
        QCOMPARE(AutoSaveScheduler::intervalMsIn(_settings), 120000);
        _settings->remove(QStringLiteral("autosave_enabled"));
        QCOMPARE(AutoSaveScheduler::enabledIn(_settings), true);
        QCOMPARE(AutoSaveScheduler::enabledIn(nullptr), true);
    }
};

QTEST_GUILESS_MAIN(TestAutoSaveScheduler)
#include "test_auto_save_scheduler.moc"
