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
    void onModelsFetched(const QString &scope, const QJsonArray &models);
    void onModelsFetchFailed(const QString &scope, const QString &error);
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
    /** The API key to show for \a provider.
     *
     *  On the INITIAL load - the page opening on the provider the active
     *  settings already describe - AI/api_key is authoritative. Applying a
     *  keyless provider profile sets AI/api_key = "" but deliberately keeps
     *  the remembered AI/api_key/<provider> (a cloud token), so preferring
     *  the per-provider memory here would put that token back into a local
     *  endpoint's field and, on Close, into its Authorization header. A real
     *  provider switch inside the page uses the per-provider memory. */
    QString storedKeyForProvider(const QString &provider, bool initialLoad) const;
    /** Placeholder of the key field for the endpoint currently in the fields
     *  ("not required" for Ollama and for a local Custom endpoint). */
    void updateKeyFieldHint();
    /** Favourites / model-cache scope for \a provider given the endpoint
     *  currently typed into the fields (which may not be saved yet). For the
     *  Custom provider this is the active profile's scope, so every custom
     *  endpoint keeps its own model list. */
    QString modelScopeFor(const QString &provider) const;
    /** Refill the provider-profile combo AND the provider combo's profile
     *  entries; selects \a selectName when given. */
    void populateProviderProfiles(const QString &selectName = QString());
    /** Show the profile whose stored settings the visible fields still match,
     *  else "(No profile)". Derived on every field change, so an edit after
     *  applying a profile honestly falls back to ad-hoc. */
    void updateProviderProfileSelection();

    // --- Phase 50 follow-up: stored CUSTOM profiles as first-class entries in
    // the Provider dropdown, so switching to a saved endpoint feels like
    // switching provider. A profile entry is NOT a new provider id: it carries
    // the provider id ("custom") in Qt::UserRole exactly like the five fixed
    // entries - every existing currentData() reader keeps working - and the
    // profile NAME in a second role that is translated at this boundary only.

    /** Rebuild the provider combo's profile entries below the fixed five. */
    void populateProviderComboProfiles();
    /** Profile name carried by the current provider-combo entry; empty for the
     *  five fixed providers (and for the separator). */
    QString currentProviderComboProfile() const;
    /** Select the entry for \a profileName, falling back to the fixed entry of
     *  \a provider. Signal-blocked: selects, never applies. */
    void selectProviderComboEntry(const QString &provider,
                                  const QString &profileName);
    /** Point the provider combo at the entry the VISIBLE fields describe: the
     *  stored custom profile matching provider+URL+key, else the plain
     *  provider. Endpoint-based on purpose - picking another model keeps the
     *  endpoint, only the profile combo falls back to "(No profile)". */
    void updateProviderComboSelection();
    /** The provider-switch body (key memory, default URL, model list), with the
     *  provider passed explicitly so the profile path can run the exact same
     *  steps without a nested currentIndexChanged. */
    void applyProviderSwitch(const QString &provider);
    /** Pour a stored profile into the visible fields - the single code path
     *  behind the profile combo and the provider combo's profile entries. */
    void applyProviderProfileToFields(const QString &name);

    QSettings *_settings;
    QComboBox *_providerProfileCombo = nullptr;
    QPushButton *_saveProfileButton = nullptr;
    QPushButton *_deleteProfileButton = nullptr;
    /// True while a profile is being poured into the fields (suppresses the
    /// field-change handler that would otherwise flip the combo to ad-hoc).
    bool _applyingProviderProfile = false;
    // Null-initialised because the provider-switch / profile-sync helpers run
    // from inside the constructor, before all of these exist, and guard on the
    // pointer being null.
    QComboBox *_providerCombo = nullptr;
    QLineEdit *_baseUrlEdit = nullptr;
    QLabel *_apiKeyLabel;
    QLineEdit *_apiKeyEdit = nullptr;
    QComboBox *_modelCombo = nullptr;
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
    /// Built late in the constructor - the provider-switch path runs before
    /// that and clears it, so the null guard must actually hold.
    QLabel *_statusLabel = nullptr;
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
