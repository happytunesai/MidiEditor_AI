#ifndef MIDIEVENTSERIALIZER_H
#define MIDIEVENTSERIALIZER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>

class MidiEvent;
class MidiFile;
class MidiTrack;

/**
 * \class MidiEventSerializer
 *
 * \brief Converts MIDI events to/from JSON for AI communication.
 *
 * Serializes selected MidiEvents into a compact JSON representation
 * that the LLM can understand and process. Also deserializes the
 * LLM's JSON response back into MidiEvent objects that can be
 * applied to the MidiFile.
 */
class MidiEventSerializer {
public:
    /**
     * \brief Serializes a list of MIDI events to JSON.
     * \param events The events to serialize
     * \param file The MidiFile for context (ticksPerQuarter, etc.)
     * \return QJsonArray of serialized events
     */
    static QJsonArray serialize(const QList<MidiEvent *> &events, MidiFile *file);

    /**
     * \brief Deserializes JSON events and applies them to the file.
     *
     * Creates new MidiEvent objects from the JSON array and inserts them
     * into the specified track/channel. This should be called within
     * a Protocol action (startNewAction/endAction) for undo support.
     *
     * \param eventsJson The JSON array of events from the AI
     * \param file The target MidiFile
     * \param track The target MidiTrack for new events
     * \param channel The default MIDI channel (0-15)
     * \param createdEvents Output: list of created MidiEvent pointers
     * \param skippedErrors Output: validation error messages for skipped events
     * \return true if deserialization was successful
     */
    static bool deserialize(const QJsonArray &eventsJson,
                            MidiFile *file,
                            MidiTrack *track,
                            int channel,
                            QList<MidiEvent *> &createdEvents,
                            QStringList *skippedErrors = nullptr);

    /**
     * \brief Validates a JSON event object has required fields.
     * \param eventObj The JSON object to validate
     * \param errorMsg Output: description of validation error
     * \return true if valid
     */
    static bool validateEventJson(const QJsonObject &eventObj, QString &errorMsg);

    /**
     * \brief Converts a MIDI note number to a human-readable name.
     * \param note MIDI note number (0-127)
     * \return Note name string (e.g. "C4", "F#5")
     */
    static QString noteName(int note);

private:
    static QJsonObject serializeNoteEvent(MidiEvent *event, MidiFile *file);
    static QJsonObject serializeControlChangeEvent(MidiEvent *event);
    static QJsonObject serializePitchBendEvent(MidiEvent *event);
    static QJsonObject serializeProgChangeEvent(MidiEvent *event);

    static const char *NOTE_NAMES[];
};

#endif // MIDIEVENTSERIALIZER_H
