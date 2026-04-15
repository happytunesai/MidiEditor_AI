#ifndef EDITORCONTEXT_H
#define EDITORCONTEXT_H

#include <QJsonObject>
#include <QJsonArray>
#include <QString>

class MidiFile;
class MatrixWidget;

/**
 * \class EditorContext
 *
 * \brief Captures the current editor state as JSON for the AI.
 *
 * EditorContext provides static methods to snapshot the editor's
 * current state — cursor position, active track/channel, tempo,
 * time signature, selection count, and file metadata — and serialize
 * it to a QJsonObject that is included in every API request.
 */
class EditorContext {
public:
    /**
     * \brief Captures the full editor state as JSON.
     * \param file The current MidiFile (may be nullptr)
     * \return QJsonObject with all context fields
     */
    static QJsonObject captureState(MidiFile *file, MatrixWidget *matrix = nullptr);

    /**
     * \brief Captures file metadata only.
     * \param file The current MidiFile
     * \return QJsonObject with file-level information
     */
    static QJsonObject captureFileInfo(MidiFile *file);

    /**
     * \brief Returns the MidiPilot system prompt.
     * \return The system prompt string
     */
    static QString systemPrompt();

    /**
     * \brief Returns the Agent Mode system prompt (shorter, tools are self-documenting).
     * \return The agent system prompt string
     */
    static QString agentSystemPrompt();

    /**
     * \brief Returns FFXIV Bard Performance mode context to append to system prompts.
     * \param includeDrums Include the Drums section (default true)
     * \param includeGuitar Include the Guitar Variants section (default true)
     * \return The FFXIV rules and constraints string
     */
    static QString ffxivContext(bool includeDrums = true, bool includeGuitar = true);

    /**
     * \brief Returns compact FFXIV context for Simple mode (no tool references).
     * Shorter to leave more output token budget for the model.
     */
    static QString ffxivContextCompact();

    /**
     * \brief Captures surrounding events (±N measures) as JSON per track.
     * \param file The current MidiFile
     * \param cursorTick The cursor position
     * \param measures Number of measures before and after cursor
     * \return QJsonObject with range info and per-track events
     */
    static QJsonObject captureSurroundingEvents(MidiFile *file, int cursorTick, int measures);

    /**
     * \brief Loads custom prompts from a JSON file.
     * \param path Path to system_prompts.json
     * \return true if loaded successfully, false on error
     */
    static bool loadCustomPrompts(const QString &path);

    /**
     * \brief Resets all custom prompts to hardcoded defaults.
     */
    static void resetToDefaults();

    /**
     * \brief Returns true if custom prompts are currently loaded.
     */
    static bool hasCustomPrompts();

    /**
     * \brief Returns the path to the currently loaded custom prompts file (empty if defaults).
     */
    static QString customPromptsPath();

    /**
     * \brief Saves current prompts (custom or default) to a JSON file.
     * \param path Output file path
     * \return true on success
     */
    static bool savePromptsToJson(const QString &path);

    /**
     * \brief Returns the default (hardcoded) prompt for the given key.
     * Keys: "simple", "agent", "ffxiv", "ffxiv_compact"
     */
    static QString defaultPrompt(const QString &key);

private:
    static QJsonObject captureTempo(MidiFile *file, int tick);
    static QJsonObject captureTimeSignature(MidiFile *file, int tick);
    static QJsonObject captureKeySignature(MidiFile *file, int tick);
    static QJsonArray captureTrackList(MidiFile *file);

    static const char *NOTE_NAMES[];
};

#endif // EDITORCONTEXT_H
