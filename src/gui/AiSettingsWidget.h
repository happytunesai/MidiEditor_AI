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
class SystemPromptDialog;
class McpServer;

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

    /** Pass the McpServer so the UI can show live status. */
    void setMcpServer(McpServer *server);

    bool accept() override;
    QIcon icon() override;

private slots:
    void onTestConnection();
    void onTestResult(bool success, const QString &message);
    void onToggleKeyVisibility();
    void onProviderChanged(int index);
    void onEditSystemPrompts();
    void onRefreshModels();
    void onModelsFetched(const QString &provider, const QJsonArray &models);
    void onModelsFetchFailed(const QString &provider, const QString &error);
    void onForceStreamingForCurrentModel();
    void updateMcpStatus();

private:
    void populateModelsForProvider(const QString &provider);
    void updateModelsStatusLabel(const QString &provider);
    void updateStreamingBlockStatus();

    QSettings *_settings;
    QComboBox *_providerCombo;
    QLineEdit *_baseUrlEdit;
    QLabel *_apiKeyLabel;
    QLineEdit *_apiKeyEdit;
    QComboBox *_modelCombo;
    QPushButton *_refreshModelsButton;
    QPushButton *_forceStreamingButton = nullptr;
    QLabel *_modelsStatusLabel;
    QCheckBox *_tokenLimitCheck;
    QSpinBox *_tokenLimitSpin;
    QCheckBox *_thinkingCheck;
    QComboBox *_effortCombo;
    QLabel *_effortLabel;
    QCheckBox *_streamingCheck;
    QPushButton *_testButton;
    QLabel *_statusLabel;
    QPushButton *_toggleKeyButton;
    QSpinBox *_contextMeasuresSpin;
    QLabel *_contextEstimateLabel;
    QSpinBox *_agentMaxStepsSpin;
    QCheckBox *_ffxivCheck;
    QPushButton *_editPromptsButton;
    QLabel *_promptsStatusLabel;
    bool _keyVisible;
    QString _lastProvider;

    // MCP Server settings
    McpServer *_mcpServer = nullptr;
    QCheckBox *_mcpEnableCheck;
    QSpinBox *_mcpPortSpin;
    QLineEdit *_mcpTokenEdit;
    QLabel *_mcpStatusLabel;
    QPushButton *_mcpCopyConfigButton;
};

#endif // AISETTINGSWIDGET_H
