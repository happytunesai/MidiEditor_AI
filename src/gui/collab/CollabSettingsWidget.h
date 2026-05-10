/*
 * MidiEditor AI
 *
 * Settings panel for collaboration features (Phase 9.1+).
 */

#ifndef COLLABSETTINGSWIDGET_H
#define COLLABSETTINGSWIDGET_H

#include "../SettingsWidget.h"

class QCheckBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QSettings;
class QSpinBox;
#ifdef MIDIEDITOR_WEBRTC_ENABLED
class WanConnectionTest;
#endif

class CollabSettingsWidget : public SettingsWidget {
    Q_OBJECT

public:
    explicit CollabSettingsWidget(QSettings *settings, QWidget *parent = nullptr);

    bool accept() override;

private slots:
    void onEnabledToggled(bool enabled);
    void onRegenerateMachineId();

private:
    void refreshIdentitySectionEnabled();

    QSettings *_settings;

    QCheckBox *_enableCheck;
    QLineEdit *_displayNameEdit;
    QLabel *_machineIdValueLabel;
    QPushButton *_regenerateButton;

    QWidget *_identitySection;

    QLineEdit *_webhookUrlEdit;
    QPushButton *_webhookToggleButton;
    bool _webhookUrlVisible = false;
    QWidget *_webhookSection;

    // Plan §11.10n hosting safety net: when on, hosting saves the
    // current file as a `_shared.mid` copy in Documents/MidiEditor_AI/shared
    // and the host edits the copy. Original stays untouched.
    QCheckBox *_hostWorkOnCopyCheck = nullptr;

    // Plan §11.10i — Collab-scoped logging only. The global Qt
    // logging-rules UI (level dropdown + advanced filter rules) lives
    // in Settings → Logging now; this checkbox just adds a
    // `midieditor.collab.*=true` overlay so users debugging a session
    // don't have to learn the filter-rules syntax.
    QCheckBox *_collabVerboseCheck = nullptr;
    QPushButton *_logOpenFileButton = nullptr;

#ifdef MIDIEDITOR_WEBRTC_ENABLED
    QLineEdit *_rendezvousUrlEdit = nullptr;
    QWidget *_rendezvousSection = nullptr;
    QPushButton *_connectionTestButton = nullptr;
    QLabel *_connectionTestStatus = nullptr;
    QLabel *_connectionTestLight = nullptr;
    QLabel *_connectionTestDetails = nullptr;
    WanConnectionTest *_runningTest = nullptr;

    // Plan §11.10h connection-quality knobs.
    QSpinBox *_iceTimeoutSpin = nullptr;
    QCheckBox *_autoReconnectCheck = nullptr;
    QSpinBox *_autoReconnectMaxSpin = nullptr;

private slots:
    void onRunConnectionTest();
#endif
};

#endif // COLLABSETTINGSWIDGET_H
