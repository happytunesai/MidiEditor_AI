#ifndef AISETTINGSWIDGET_H
#define AISETTINGSWIDGET_H

#include "SettingsWidget.h"
#include <QSettings>

class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QCheckBox;
class QSpinBox;
class AiClient;

/**
 * \class AiSettingsWidget
 *
 * \brief Settings panel for MidiPilot AI configuration.
 *
 * Provides UI for configuring the OpenAI API key and model selection.
 * Integrated into the main SettingsDialog.
 */
class AiSettingsWidget : public SettingsWidget {
    Q_OBJECT

public:
    AiSettingsWidget(QSettings *settings, QWidget *parent = nullptr);

    bool accept() override;
    QIcon icon() override;

private slots:
    void onTestConnection();
    void onTestResult(bool success, const QString &message);
    void onToggleKeyVisibility();
    void onProviderChanged(int index);

private:
    void populateModelsForProvider(const QString &provider);

    QSettings *_settings;
    QComboBox *_providerCombo;
    QLineEdit *_baseUrlEdit;
    QLabel *_apiKeyLabel;
    QLineEdit *_apiKeyEdit;
    QComboBox *_modelCombo;
    QCheckBox *_thinkingCheck;
    QComboBox *_effortCombo;
    QLabel *_effortLabel;
    QPushButton *_testButton;
    QLabel *_statusLabel;
    QPushButton *_toggleKeyButton;
    QSpinBox *_contextMeasuresSpin;
    QLabel *_contextEstimateLabel;
    QSpinBox *_agentMaxStepsSpin;
    QCheckBox *_ffxivCheck;
    bool _keyVisible;
    QString _lastProvider;
};

#endif // AISETTINGSWIDGET_H
