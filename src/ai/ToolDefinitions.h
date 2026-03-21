#ifndef TOOLDEFINITIONS_H
#define TOOLDEFINITIONS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class MidiFile;
class MidiPilotWidget;

/**
 * \class ToolDefinitions
 *
 * \brief OpenAI function-calling tool schemas and executor for Agent Mode.
 *
 * Provides tool schemas matching the OpenAI tools API format, and an executor
 * that delegates tool calls to existing MidiPilotWidget handlers.
 */
class ToolDefinitions {
public:
    /**
     * \brief Returns the OpenAI tools array for the API request.
     */
    static QJsonArray toolSchemas();

    /**
     * \brief Executes a tool call by delegating to existing handlers.
     * \param toolName The function name from the tool call.
     * \param args The parsed arguments object.
     * \param file The current MIDI file.
     * \param widget The MidiPilotWidget for write operations.
     * \return Result JSON to send back to the LLM as tool output.
     */
    static QJsonObject executeTool(const QString &toolName,
                                   const QJsonObject &args,
                                   MidiFile *file,
                                   MidiPilotWidget *widget);

private:
    static QJsonObject makeTool(const QString &name,
                                const QString &description,
                                const QJsonObject &parameters);

    // Read-only tools
    static QJsonObject execGetEditorState(MidiFile *file);
    static QJsonObject execGetTrackInfo(const QJsonObject &args, MidiFile *file);
    static QJsonObject execQueryEvents(const QJsonObject &args, MidiFile *file);

    // Write tools (delegate to widget handlers)
    static QJsonObject execWriteAction(const QString &action,
                                       const QJsonObject &args,
                                       MidiPilotWidget *widget);
};

#endif // TOOLDEFINITIONS_H
