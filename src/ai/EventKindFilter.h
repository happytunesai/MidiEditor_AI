/*
 * MidiEditor AI - the optional `types` filter of query_events / delete_events
 * (MCP-ARGS-001, v2.5.0).
 *
 * The kinds are the event types insert_events writes, so a caller can read or
 * delete exactly one kind in a range - for example only the program change at
 * tick 0 - instead of everything on the track. Header-only: shared by
 * ToolDefinitions (query_events) and MidiPilotWidget (the delete action).
 */
#ifndef EVENTKINDFILTER_H
#define EVENTKINDFILTER_H

#include "../MidiEvent/ControlChangeEvent.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/PitchBendEvent.h"
#include "../MidiEvent/ProgChangeEvent.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>

namespace EventKindFilter {

/// The accepted kinds - the same names insert_events uses.
inline QStringList kinds() {
    return {QStringLiteral("note"), QStringLiteral("cc"),
            QStringLiteral("pitch_bend"), QStringLiteral("program_change")};
}

/// The kind of \a ev, or an empty string for every other event type.
inline QString kindOf(MidiEvent *ev) {
    if (dynamic_cast<NoteOnEvent *>(ev)) return QStringLiteral("note");
    if (dynamic_cast<ControlChangeEvent *>(ev)) return QStringLiteral("cc");
    if (dynamic_cast<PitchBendEvent *>(ev)) return QStringLiteral("pitch_bend");
    if (dynamic_cast<ProgChangeEvent *>(ev)) return QStringLiteral("program_change");
    return QString();
}

/**
 * \brief Reads a `types` argument. Absent or null = no filter (\a out stays
 *        empty). An empty array is refused rather than read as "nothing" -
 *        it is almost always a mistake.
 * \return False with \a error set for anything that is not an array of known
 *         kinds.
 */
inline bool parse(const QJsonValue &value, QSet<QString> *out, QString *error) {
    out->clear();
    if (value.isUndefined() || value.isNull())
        return true;
    const QStringList known = kinds();
    const QString expected = QStringLiteral("types must be null or a non-empty array of: ")
                             + known.join(QStringLiteral(", ")) + QLatin1Char('.');
    if (!value.isArray() || value.toArray().isEmpty()) {
        *error = expected;
        return false;
    }
    for (const QJsonValue &v : value.toArray()) {
        const QString kind = v.toString();
        if (!known.contains(kind)) {
            *error = QStringLiteral("Unknown event kind in types: ")
                     + (v.isString() ? kind : QStringLiteral("(not a string)"))
                     + QStringLiteral(". ") + expected;
            return false;
        }
        out->insert(kind);
    }
    return true;
}

/// True when \a ev passes \a filter (an empty filter passes everything).
inline bool matches(MidiEvent *ev, const QSet<QString> &filter) {
    return filter.isEmpty() || filter.contains(kindOf(ev));
}

} // namespace EventKindFilter

#endif // EVENTKINDFILTER_H
