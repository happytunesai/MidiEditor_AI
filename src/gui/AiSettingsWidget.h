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

    // Phase 50: provider profiles (named endpoint configurations). NOT the
    // per-model system prompts - those are "Prompt Profiles".
    void onProviderProfileSelected(int index);
    void onSaveProviderProfile();
    void onDeleteProviderProfile();

private:
    void populateModelsForProvider(const QString &provider);
    void updateModelsStatusLabel(const QString &provider);
    void updateStreamingBlockStatus();

    /** Model id of the current selection (item data, falling back to the
     *  typed text) - the value \ref accept writes to AI/model. */
    QString currentModelId() const;
    /** Refill the provider-profile combo; selects \a selectName when given. */
    void populateProviderProfiles(const QString &selectName = QString());
    /** Show the profile whose stored settings the visible fields still match,
     *  else "(No profile)". Derived on every field change, so an edit after
     *  applying a profile honestly falls back to ad-hoc. */
    void updateProviderProfileSelection();

    QSettings *_settings;
    QComboBox *_providerProfileCombo = nullptr;
    QPushButton *_saveProfileButton = nullptr;
    QPushButton *_deleteProfileButton = nullptr;
    /// True while a profile is being poured into the fields (suppresses the
    /// field-change handler that would otherwise flip the combo to ad-hoc).
    bool _applyingProviderProfile = false;
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
