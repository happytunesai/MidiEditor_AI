#ifndef AISETTINGSWIDGET_H
#define AISETTINGSWIDGET_H

#include "SettingsWidget.h"
#include <QSet>
#include <QSettings>
#include <QString>

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
     *  provider switch inside the page uses the per-provider memory - filtered
     *  through ProviderProfileStore::keyMemoryOnEnter(), so a remembered cloud
     *  token never re-attaches to a Custom endpoint on this machine. */
    QString storedKeyForProvider(const QString &provider, bool initialLoad) const;
    /** Write the per-provider key memory for \a provider from the key field,
     *  following ProviderProfileStore::keyMemoryActionOnLeave(): a key the user
     *  cleared HERE is erased (otherwise it comes back on the next provider
     *  round-trip and is sent again), a field emptied by a keyless profile
     *  leaves the memory untouched.
     *
     *  \a fromProfile is the selection state that was in force while this key
     *  was on screen (empty for a fixed provider entry). A key poured from that
     *  profile and not typed over belongs to the PROFILE, so it is not written
     *  into the provider's memory - it would replace the user's own key for the
     *  plain provider entry, and on Custom it would be re-attached to the next
     *  ad-hoc endpoint. Capture the state BEFORE the handlers clear it. */
    void rememberKeyFieldFor(const QString &provider, const QString &fromProfile);
    /** True when the key field still shows exactly the key stored with
     *  \a profileName (empty name: false). */
    bool keyFieldHoldsProfileKey(const QString &profileName) const;
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
    /** Show the SELECTED profile while the visible fields still match it
     *  completely, else "(No profile)". Model-sensitive on purpose - and it
     *  validates \ref _selectedProviderProfile only, it never looks for another
     *  profile that happens to describe the same values. */
    void updateProviderProfileSelection();

    // --- selection state: INTENT, not inference ---------------------------
    //
    // The page's connection selection is either Provider(X) - a fixed entry in
    // the Provider dropdown - or Profile(name). Only a user action moves it:
    // picking an entry sets it, and editing the endpoint DEGRADES a profile
    // selection to Provider(the fields' provider). It is derived from scratch
    // exactly once, when the page opens, and then only from the validated
    // global hint (\ref ProviderProfileStore::validatedActiveProfileName).
    //
    // The previous rule - re-derive by scanning all profiles for one that
    // describes the fields - could not represent "I picked OpenAI": a stored
    // profile on OpenAI's default endpoint always won the scan, so the
    // dropdown snapped back to it on every field change and on every reopen.

    /** The profile this page is selected on; empty means a fixed provider
     *  entry. Never written from a scan. */
    QString _selectedProviderProfile;
    /** \ref _selectedProviderProfile after dropping it when the visible fields
     *  no longer describe its ENDPOINT (provider, base URL or key edited).
     *  A model change keeps it: same server, same profile entry. */
    QString validatedProfileIntent();

    // --- Phase 50 follow-up: stored profiles as first-class entries in the
    // Provider dropdown, so switching to a saved endpoint feels like switching
    // provider. A profile entry is NOT a new provider id: it carries its OWN
    // provider id in Qt::UserRole exactly like the five fixed entries - every
    // existing currentData() reader keeps working - and the profile NAME in a
    // second role that is translated at this boundary only.
    //
    // v2.3 owner smoke: EVERY saved profile is listed here, not only the Custom
    // ones - same rule as the footer. Hiding an OpenAI profile while the
    // "Provider profile" row right above still jumped to OpenAI when it was
    // picked made the two rows contradict each other.

    /** Rebuild the provider combo's profile entries below the fixed five. */
    void populateProviderComboProfiles();
    /** Profile name carried by the current provider-combo entry; empty for the
     *  five fixed providers (and for the separator). */
    QString currentProviderComboProfile() const;
    /** Index of the FIXED entry for \a providerId, skipping profile entries.
     *  A plain findData() would hit a profile entry as soon as a profile of a
     *  built-in provider is stored (the footer's indexOfFixedProvider twin). */
    int indexOfFixedProvider(const QString &providerId) const;
    /** Select the entry for \a profileName, falling back to the fixed entry of
     *  \a provider. Signal-blocked: selects, never applies. */
    void selectProviderComboEntry(const QString &provider,
                                  const QString &profileName);
    /** Point the provider combo at the entry the page's selection STATE names:
     *  the selected profile while the fields still describe its endpoint, else
     *  the plain provider. Endpoint-based on purpose - picking another model
     *  keeps the endpoint, so only the profile row falls back to
     *  "(No profile)". */
    void updateProviderComboSelection();
    /** The provider-switch body (key memory, default URL, model list), with the
     *  provider passed explicitly so the profile path can run the exact same
     *  steps without a nested currentIndexChanged.
     *
     *  \a leavingProfile is the selection state being left, passed in because
     *  both callers have already moved it by the time this runs; the key memory
     *  needs it (\ref rememberKeyFieldFor). */
    void applyProviderSwitch(const QString &provider,
                             const QString &leavingProfile);
    /** Pour a stored profile into the visible fields - the single code path
     *  behind the profile combo and the provider combo's profile entries. */
    void applyProviderProfileToFields(const QString &name);

    // --- v2.3 owner smoke: "Custom" is a DEFINED ad-hoc state ---------------
    //
    // Picking "Custom" used to leave the previous endpoint's URL and key in the
    // fields. The endpoint then still matched the profile that had been active,
    // so the selection snapped straight back to that profile's entry and
    // "Custom" was unreachable. The ad-hoc endpoint therefore gets a memory of
    // its own (AI/custom_adhoc_base_url) next to the key memory
    // AI/api_key/custom that already exists under the H4 rules.

    /** The remembered ad-hoc Custom endpoint (empty when there is none). */
    QString rememberedAdHocCustomBaseUrl() const;
    /** Remember \a baseUrl as THE ad-hoc Custom endpoint - but only while
     *  \a provider is "custom" and no stored profile describes this endpoint
     *  (one that does is reachable through its own entry, and remembering it
     *  here would make "Custom" a duplicate that snaps away again). An empty
     *  URL leaves the memory untouched - "not entered" is never a deletion,
     *  matching accept()'s reading of the empty Custom URL field. */
    void rememberAdHocCustomBaseUrl(const QString &provider,
                                    const QString &baseUrl,
                                    const QString &apiKey);

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
    /// Providers whose API-key field the USER edited while this page was open.
    /// Only for those does an empty field mean "cleared on purpose"; a field
    /// emptied by a provider switch or by a keyless profile never lands here,
    /// because QLineEdit::setText() does not emit textEdited(). Pouring a
    /// profile REMOVES its provider from the set: what is on screen from then
    /// on is the profile's key, not anything the user typed, and the memory
    /// rule needs "not edited SINCE the pour".
    QSet<QString> _keyFieldEditedFor;

    // MCP Server settings
    McpServer *_mcpServer = nullptr;
    QCheckBox *_mcpEnableCheck;
    QSpinBox *_mcpPortSpin;
    QLineEdit *_mcpTokenEdit;
    QLabel *_mcpStatusLabel;
    QPushButton *_mcpCopyConfigButton;
};

#endif // AISETTINGSWIDGET_H
