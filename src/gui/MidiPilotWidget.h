#ifndef MIDIPILOTWIDGET_H
#define MIDIPILOTWIDGET_H

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

class QTimer;
class QCheckBox;

class QVBoxLayout;
class QHBoxLayout;
class QFrame;
class QLabel;
class QTextEdit;
class QPushButton;
class QScrollArea;
class QComboBox;
class MidiFile;
class Selection;
class AiClient;
class AgentRunner;
class PromptProfileStore;
class MainWindow;

/**
 * \class MidiPilotWidget
 *
 * \brief AI assistant sidebar panel for MidiEditor.
 *
 * Provides a chat-based interface for AI-assisted MIDI editing.
 * Three sections: status header, context bar, chat area.
 */
class MidiPilotWidget : public QWidget {
    Q_OBJECT

public:
    explicit MidiPilotWidget(MainWindow *mainWindow, QWidget *parent = nullptr);

    /**
     * \brief Updates the context bar with current editor state.
     */
    void refreshContext();

    /**
     * \brief Sets focus to the input field.
     */
    void focusInput();

    /**
     * \brief Sends `text` as a user message through the NORMAL send path
     *  (busy/configured guards, chat bubble, context + selection capture),
     *  exactly as if the user had typed it and pressed Enter. Used by "Ask
     *  MidiPilot about the selection": one right-click, one answer - the
     *  selection itself travels along as serialized events, so the model
     *  sees the notes, not just a summary. A half-written draft in the
     *  input field survives the round trip. No-op for Show-mode viewers
     *  (only the presenter may drive MidiPilot).
     * \return true if the message was actually dispatched to the AI client or
     *  the agent runner; false when a guard refused it (busy, agent running,
     *  not configured, Show-mode lock, or a tool-incapable model in agent
     *  mode). The verdict comes from \ref sendCurrentPrompt, not from any side
     *  effect. Callers that wait for assistantReplied (the playability
     *  workbench) must not latch on a refusal - nothing will ever arrive.
     */
    bool submitPrompt(const QString &text);

    /**
     * \brief Whether MidiPilot can actually be used, i.e. an AI provider is
     *  configured (an API key is stored, or the provider is local and needs
     *  none). When false the panel shows the setup prompt instead of a chat
     *  and the input field is disabled, so seeding a question would silently
     *  do nothing - callers that offer such an action (context menus) must
     *  not show it at all.
     */
    bool isConfigured() const;

    /**
     * \\brief Returns the current mode: \"simple\" or \"agent\".
     */
    QString currentMode() const;

    /**
     * \brief Returns whether FFXIV Bard Performance mode is enabled.
     */
    bool ffxivMode() const;

    /**
     * \brief Phase 46: programmatic FFXIV-mode switch - drives the same
     *  checkbox the user clicks, so persistence and the ffxivModeChanged
     *  notification take the one existing path. Used by the set_ffxiv_mode
     *  AI tool: an MCP client sees the 5 FFXIV tools appear/disappear with
     *  the mode and could previously neither see nor change it.
     */
    void setFfxivMode(bool enabled);

    /**
     * \brief Abort whatever request is currently in flight (agent or
     * simple) and restore the input UI to an idle, usable state.
     *
     * Agent mode routes through AgentRunner::cancel() which emits
     * errorOccurred → onAgentError (self-resets). Simple mode calls the
     * deliberately-silent AiClient::cancelRequest() (AgentRunner relies
     * on that silence to avoid a double errorOccurred), so this helper
     * resets the simple-mode UI itself. Safe to call when nothing is
     * running — it is a no-op in that case. (BUG-MIDIPILOT-001)
     */
    void abortActiveRequest();

    /** \brief True while an agent run is generating/applying. */
    bool isAgentRunning() const;

    /**
     * \brief Phase 28: set the document the next apply (dispatchAction) must
     * target. Called by ToolDefinitions::executeTool (scoped to one tool call)
     * so agent and MCP writes land on the file the call was made against, not
     * the live _file. Pass nullptr to clear.
     */
    void setApplyTarget(MidiFile *f);

    /**
     * \brief Phase 28 (editor groups): true if an agent run is in flight AND it
     * was started against \a f. Lets MainWindow abort the run before deleting the
     * document the agent is editing (e.g. its tab is closed mid-run).
     *
     * v2.3.1 cross-tab: "started against" has become "currently bound to" -
     * an intercepted switch_document moves the tracked file (see
     * \ref rebindAgentRun), so after a switch it is the CURRENT target whose
     * tab-close aborts the run, while closing the original document no longer
     * does (the run continues on its target; the panel-wide chat stays put).
     */
    bool isAgentRunningOn(MidiFile *f) const;

    /**
     * \brief v2.3.1 cross-tab: resolves a flattened document list index (from
     * list_documents / MainWindow::listOpenDocumentsJson) to its MidiFile
     * without activating, re-binding, or otherwise touching anything.
     * Thin delegate to MainWindow::documentFileByListIndex; null when the
     * index is out of range. Used by AgentRunner's switch_document intercept.
     */
    MidiFile *documentFileByListIndex(int index) const;

    /**
     * \brief v2.3.1 cross-tab: tab title of the open document holding \a f
     * (same title list_documents reports); empty when \a f is not listed.
     */
    QString documentTitleForFile(MidiFile *f) const;

    /**
     * \brief v2.3.1 cross-tab: moves the RUNNING agent run's bind to
     * \a target in one step - called by AgentRunner's switch_document
     * intercept right after it re-pointed its own file pointer, so the whole
     * re-bind (runner file + closed-mid-run guard + step/undo bookkeeping +
     * chat announcement) is one synchronous main-thread action with no
     * stale-bind window. Concretely:
     * \li `_runOriginFile = target` - MainWindow::closeDocumentFile aborts
     *     the run when the NEW target's tab closes; closing the ORIGINAL
     *     document no longer aborts it (the run continues on its target).
     * \li records \a title as the document of every following step, so the
     *     per-step undo bookkeeping (`_turnSteps`) says which tab holds each
     *     step's undo entry (Protocol is per-file; Ctrl+Z acts on the ACTIVE
     *     tab - which does not change here).
     * \li posts the unmissable "Switched to ..." system line into the chat.
     * Deliberately does NOT activate the tab (MCP's switch_document does -
     * that difference is by design: the chat must stay visible).
     * No-op unless an agent run is in flight.
     */
    void rebindAgentRun(MidiFile *target, const QString &title);

    /**
     * \brief Lock the MidiPilot panel for Show-mode viewers (Phase 9.9c
     *        §15.2). When locked, the input field is read-only with an
     *        explanatory placeholder, and the send button is disabled.
     *        The chat history stays visible (so a previously-running
     *        AI walkthrough is still readable). Unlocks restore the
     *        normal post-config state via \ref setupSetupPrompt.
     *
     *        Called from MainWindow's `applyShowModeLock` whenever the
     *        local peer's presenter status changes.
     */
    void setShowModeLocked(bool locked);

    /**
     * \brief Executes an action silently (no chat bubbles). Used by Agent Mode tool calls.
     */
    QJsonObject executeAction(const QJsonObject &actionObj);

private:
    void updateTokenLabel();

public slots:
    /**
     * \brief Called when a new file is loaded or file changes.
     */
    void onFileChanged(MidiFile *f);

    /**
     * \brief Called when application settings have changed (e.g. API key updated).
     */
    void onSettingsChanged();

    /**
     * \brief Clears the conversation history and chat display.
     */
    void onNewChat();

signals:
    /**
     * \brief Emitted when the matrix widget should be repainted.
     */
    void requestRepaint();

    /**
     * \brief Phase 46: FFXIV mode was toggled (checkbox or set_ffxiv_mode
     *  tool). MainWindow forwards this to McpServer::broadcastToolsChanged
     *  so connected MCP clients refresh their tool list - the notification
     *  the manual always promised but nothing ever sent.
     */
    void ffxivModeChanged(bool enabled);

    /**
     * \brief Phase 46: the final text of every completed reply, simple AND
     *  agent mode. Lets a caller that submitted a prompt programmatically
     *  (the playability dialog's "Analyze with MidiPilot") mirror the answer
     *  into its own window. Fired for every reply, not only submitted ones -
     *  listeners latch on their own send and drop the rest.
     */
    void assistantReplied(const QString &text);


private slots:
    void onSendMessage();
    void onResponseReceived(const QString &content, const QJsonObject &fullResponse);
    void onErrorOccurred(const QString &errorMessage);
    void onSettingsClicked();
    void onModeChanged(int index);
    void onAgentStepStarted(int step, const QString &toolName);
    void onAgentStepCompleted(int step, const QString &toolName, const QJsonObject &result);
    void onAgentStepsPlanned(int firstStep, const QStringList &toolNames);
    void onAgentFinished(const QString &finalMessage);
    void onAgentError(const QString &error);
    void onAgentStepLimitReached(int currentStep, int maxSteps);
    void onModelComboChanged(int index);
    void onProviderComboChanged(int index);
    void onEffortComboChanged(int index);
    void onStreamDelta(const QString &text);
    void onStreamFinished(const QString &fullContent, const QJsonObject &fullResponse);
    void onRefreshModels();
    void onModelsFetched(const QString &scope, const QJsonArray &models);
    void onModelsFetchFailed(const QString &scope, const QString &error);

private:
    struct ConversationEntry {
        QString role;          // "user" or "assistant"
        QString message;
        QJsonObject context;
        QDateTime timestamp;
    };

    /**
     * \brief The real send path: validates, captures context and dispatches the
     *  request to \ref AiClient (simple mode) or \ref AgentRunner (agent mode).
     * \return true ONLY when the request was actually handed to the client or the
     *  runner, i.e. a terminal signal (responseReceived / errorOccurred /
     *  agentFinished / agentError) is guaranteed to follow. false on every guard
     *  and refusal path (empty prompt, provider not configured, busy, agent
     *  already running, tool-incapable model in agent mode) - nothing was sent
     *  and no reply will ever arrive. ANALYZE-LATCH-001: \ref submitPrompt
     *  forwards this verdict to programmatic callers that latch on
     *  \ref assistantReplied; it must never be inferred from side effects such
     *  as the input field having been cleared.
     */
    bool sendCurrentPrompt();

    void setupUi();
    void setupSetupPrompt();
    void populateFooterModels();
    // --- Phase 50 (+ follow-up): the footer's Provider dropdown is the single
    // connection picker. Stored CUSTOM profiles are first-class entries below a
    // separator, so switching to a saved endpoint feels like switching provider.
    // A profile entry is NOT a new provider id: it carries the provider id
    // ("custom") in Qt::UserRole exactly like the built-in entries - every
    // existing currentData() reader keeps working - and the profile NAME in a
    // second role, translated at this boundary only.

    /** Rebuild the whole provider combo (built-in providers, the conditional
     *  ad-hoc "Custom" entry, separator, stored custom profiles) from the store
     *  and re-derive its selection. Call after a profile save/delete and
     *  whenever the connection settings may have changed. */
    void populateProviderProfiles();
    /** FOOTER rule, the OPTIONAL half: is a custom endpoint configured that
     *  nobody is currently on? True for a base URL that is not just a built-in
     *  provider's default and that no stored profile owns. It only ever ADDS
     *  the plain "Custom" entry; whether the entry is REQUIRED is a question
     *  about the selection, answered by
     *  \ref ProviderProfileStore::selectionNeedsFixedCustomEntry - this scan
     *  must never be the only gate, or a stored profile describing the live
     *  custom endpoint hides the very entry the selection names.
     *  The settings page is deliberately not subject to either rule: that is
     *  where a custom endpoint is configured in the first place. */
    bool hasAdHocCustomEndpoint() const;
    /** Index of a BUILT-IN provider entry, skipping the stored-profile entries
     *  (which carry "custom" in Qt::UserRole too). -1 when not listed. */
    int indexOfFixedProvider(const QString &providerId) const;
    /** Add the fixed entry for \a providerId in front of the separator and
     *  return its index. The safety net for \ref syncProviderComboSelection:
     *  the picker must never display a provider the app is not using, so an
     *  entry the listing rule left out is added rather than skipped. */
    int insertFixedProviderEntry(const QString &providerId);
    /** Profile name carried by the current provider-combo entry; empty for the
     *  built-in providers (and for the separator). */
    QString currentProviderComboProfile() const;
    /** Point the provider combo at the entry the footer's selection STATE
     *  names: the selected profile while the live endpoint still is that
     *  profile's, else the plain provider. Signal-blocked - it selects, it
     *  never applies. */
    void syncProviderComboSelection();

    // --- selection state: INTENT, not inference ---------------------------
    //
    // Like the settings page (\ref AiSettingsWidget), the footer's connection
    // selection is either Provider(X) or Profile(name), moved only by a user
    // action here. It is derived from scratch at startup and on an external
    // settings change, and then only from the validated global hint
    // (\ref ProviderProfileStore::validatedActiveProfileName) - never by
    // scanning the stored profiles for one that describes the live endpoint,
    // which made an explicit provider pick snap onto a profile sharing it.

    /** The profile the footer is selected on; empty means a fixed provider
     *  entry. */
    QString _selectedProviderProfile;
    /** \ref _selectedProviderProfile after dropping it when the live
     *  connection is no longer that profile's ENDPOINT (or the profile is
     *  gone). Model changes keep it: same server. */
    QString validatedProfileIntent();
    /** Re-derive the selection state from the validated hint. ONLY for the
     *  moments where no local intent can exist: footer startup and an external
     *  settings change (\ref onSettingsChanged). */
    void deriveProfileIntentFromHint();
    /** Apply a stored profile and re-sync the footer - the single code path
     *  behind the provider combo's profile entries and the file presets. */
    void applyProviderProfileByName(const QString &name);
    // Select a model id in the (read-only) footer combo, adding it as an item
    // first if it isn't in the list (e.g. a custom or per-file model). Also
    // refreshes the tooltip so the full name is reachable when the label elides.
    void selectFooterModel(const QString &modelId);
    /** Enable/disable the footer connection controls (provider / model /
     *  refresh / effort). They re-point the SHARED AiClient, so they must stay
     *  disabled while a request or an agent run is in flight. */
    void setConnectionControlsEnabled(bool enabled);
    void addChatBubble(const QString &role, const QString &text);
    void setStatus(const QString &text, const QString &color);
    QJsonObject dispatchAction(const QJsonObject &actionObj, bool showBubbles = true);
    QJsonObject applyAiEdits(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applyAiDeletes(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applyTrackAction(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applyMoveToTrack(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applyTempoAction(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applyTimeSignatureAction(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applySelectAndEdit(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applySelectAndDelete(const QJsonObject &response, bool showBubbles = true);

    /**
     * \brief Phase 28 (editor groups): the document an agent edit must target.
     *
     * During an agent run this is the ORIGIN document the run was started on
     * (_runOriginFile), NOT the widget's live _file - so switching tabs mid-run
     * can't redirect the edit to the wrong document. Outside a run it is _file.
     * Returns null only if the origin was closed mid-run (the apply then aborts).
     */
    MidiFile *activeEditFile() const;

    /** \brief The Selection of activeEditFile() (never null - falls back to the
     *  active selection), so agent delete/move/select act on the origin doc. */
    Selection *activeEditSelection() const;

    MainWindow *_mainWindow;
    MidiFile *_file;
    /** Phase 28: document captured at request START (agent or simple). Used to
     *  abort the run if that document is closed (isAgentRunningOn) and as the
     *  apply target for simple mode. nullptr when no request is in flight. */
    MidiFile *_runOriginFile = nullptr;
    /** v2.3.1 cross-tab (agent runs only): tab title of the document the run
     *  STARTED on. Anchor for "is a step landing outside the chat's own
     *  document?" - titles, not pointers, so a closed origin cannot dangle. */
    QString _runOriginDocTitle;
    /** Tab title of the run's CURRENT bind; moves with rebindAgentRun(). */
    QString _runCurrentDocTitle;
    /** Ordered, de-duplicated titles of every document this run was bound to
     *  (origin first). More than one entry = the run-end summary lists them. */
    QStringList _runDocTitles;
    /** Phase 28: the document the CURRENTLY-dispatching apply targets. Set in a
     *  tight scope around each dispatch (executeTool's guard for agent/MCP, the
     *  simple-mode wrapper) and reset to nullptr after, so activeEditFile() only
     *  diverges from _file while an edit is actually being applied. This keeps a
     *  finished MidiPilot run from mis-routing a later MCP/manual edit. */
    MidiFile *_applyTargetFile = nullptr;
    AiClient *_client;
    AgentRunner *_agentRunner;
    bool _isAgentRunning;

    // Agent steps UI
    QWidget *_agentStepsWidget;  // Actually AgentStepsWidget*, stored as QWidget* to avoid header dep
    QWidget *_agentDockArea;     // Anchored container below the chat scroll, holds the steps widget

    // Streaming bubble for incremental display
    QLabel *_streamBubble;
    bool _streamIsJson;  // true = response is JSON action; render in subdued monospace preview style

    // Simple-mode self-healing retry state \u2014 stashed so we can replay
    // the same request after a transient failure (network blip, empty
    // stream, MAX_TOKENS, etc.). Reset on successful response.
    QString _lastSimpleSystemPrompt;
    QJsonArray _lastSimpleHistory;
    QString _lastSimpleMessage;
    int _simpleRetryCount;
    int _simpleMaxRetries;
    // Phase 28: makes the simple-mode self-healing retry (a QTimer::singleShot)
    // cancellable. Bumped on every new send and on abort; the retry lambda captures
    // the value and bails if it no longer matches, so a Stop / tab-close / new send
    // during the backoff window can't resurrect a request or apply to the wrong doc.
    quint64 _requestGeneration = 0;
    bool _simpleRetryPending = false;

    // Live reasoning / "thought" display. Rendered as plain gray italic
    // text inline in the chat (not a speech bubble). Lazy-created on first
    // streamReasoningDelta and reset to nullptr on each new send so a fresh
    // label is allocated per request — but the previous one stays in the
    // chat history.
    QLabel *_thoughtLabel;
    QString _thoughtBaseText;        // accumulated text without trailing cursor
    QTimer *_thoughtCursorTimer;     // blinks the trailing cursor while thinking
    bool _thoughtCursorOn;           // current visibility of the cursor glyph
    int _thoughtCursorFrame;         // current spinner frame index

    // Context bar
    QLabel *_contextLabel;

    // Chat area
    QWidget *_chatContainer;
    QVBoxLayout *_chatLayout;
    QScrollArea *_chatScroll;

    // Input area
    QTextEdit *_inputField;
    QPushButton *_sendButton;
    QPushButton *_stopButton;
    QComboBox *_modeCombo;
    QLabel *_tokenLabel;

    // Phase 9.9c §15.2: Show-mode viewer lock. When true the input
    // field is disabled with an explanatory placeholder; setupSetupPrompt
    // and the other re-enable sites consult this flag so they don't
    // accidentally un-lock the panel while a Show-mode session is live.
    bool _showModeLocked = false;
    // Saved placeholder string from before the lock was applied, so we
    // can restore it verbatim on unlock instead of re-deriving from
    // _client->isConfigured().
    QString _placeholderBeforeShowLock;

#ifdef MIDIEDITOR_COLLAB_ENABLED
    // Phase 9.3: snapshot of the file taken just before the agent starts
    // when running in "Agent (PR)" mode. Used at the end of the agent run
    // to compute hunks via MidiDiff for a PrReviewDialog (review-applied
    // mode). Empty when not in PR mode. The Agent (PR) mode itself is
    // currently hidden behind the compile-time flag
    // MIDIPILOT_EXPERIMENTAL_AGENT_PR (defined in MidiPilotWidget.cpp,
    // default 0) because the apply-then-review UX overlaps with Protocol
    // undo. These members stay declared so the surrounding code paths
    // continue to compile cleanly even when the flag is off.
    QJsonArray _prModeSnapshotBefore;
    QString _prModeUserMessage;
#endif

    // Footer (status, model, settings)
    QFrame *_statusBar;
    QLabel *_statusLabel;
    QLabel *_statusDots;
    QTimer *_statusTimer;
    int _dotPhase;
    int _msgPhase;
    /// Null-initialised: populateProviderProfiles() builds this combo's entries
    /// and runs while the footer is still being assembled. The helpers guard on
    /// null so nothing depends on the order of the footer widgets.
    QComboBox *_providerCombo = nullptr;
    QComboBox *_modelCombo = nullptr;
    QPushButton *_refreshModelsButton = nullptr;
    QComboBox *_effortCombo;
    QCheckBox *_ffxivCheck;

    // Setup prompt (shown when no API key)
    QWidget *_setupWidget;

    // Conversation
    QJsonArray _conversationHistory;
    QList<ConversationEntry> _entries;

    // Token tracking
    int _lastPromptTokens;
    int _lastCompletionTokens;
    int _totalPromptTokens;
    int _totalCompletionTokens;

    // Persistent conversation history
    QString _conversationId;
    QTimer *_saveTimer;
    // Per-turn metadata accumulator. One entry is appended to _turns when
    // the assistant turn completes (agent finished/error or simple-mode
    // response received) and persisted as part of the conversation JSON
    // so the live thoughts, steps, latency, effort and provider/model used
    // for that turn survive a reload.
    QJsonArray _turns;
    QString _turnReasoning;
    QJsonArray _turnSteps;
    qint64 _turnStartMs;
    bool _turnStreamed;
    QString _turnEffort;
    QString _turnProvider;
    QString _turnModel;
    void resetTurnState();
    void finalizeTurn(const QString &finalText, const QString &status);
    void scheduleSave();
    void doSaveConversation();
    void showHistoryMenu();
    void loadConversation(const QString &id);
    QJsonArray truncateHistory(const QJsonArray &history, int contextWindow,
                               int systemPromptChars = 0) const;
    void loadPresetForFile(const QString &midiPath);
    void savePresetForFile();
    QString _customFileInstructions;  // Per-file custom instructions from preset

    // Phase 29: Per-model system prompt profiles. Resolves <provider:model>
    // → optional override / append to the default agent or simple prompt.
    // Owned by the widget; the dialog opens a temporary view on the same
    // QSettings-backed store.
    PromptProfileStore *_profileStore = nullptr;
};

#endif // MIDIPILOTWIDGET_H
