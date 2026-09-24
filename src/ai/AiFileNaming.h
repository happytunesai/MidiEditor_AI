/*
 * MidiEditor AI - file names for documents the AI saves (Phase 51, v2.5.0).
 *
 * The "AI gate": every file MidiPilot or an MCP client writes is a MARKED copy -
 * <name>.midipilot.mid (MidiPilot) or <name>.mcp.mid (MCP) - and an existing
 * file is never written over. The only exception is the document's own file
 * when it already carries the caller's mark: that is the AI's working copy, so
 * repeated saves go into the same file. A taken name gets a counter
 * (mozart.midipilot.2.mid).
 *
 * Pure Qt Core, no file-system access of its own: whether a candidate name is
 * taken is asked through a callback, so the rules are unit-testable
 * (test_ai_file_naming) and MainWindow decides what "taken" means (a file on
 * disk or the path of another open document).
 */
#ifndef AIFILENAMING_H
#define AIFILENAMING_H

#include <QString>

#include <functional>

namespace AiFileNaming {

/// "mcp" for MCP callers (a tool-call source starting with "mcp" - the same
/// test ToolDefinitions::protocolActorPrefix uses), "midipilot" otherwise.
QString markFor(const QString &source);

/// Removes ONE trailing extension MidiEditor can open (.mid, .midi and the
/// import-only formats such as .gp5 or .musicxml), any case. Other dots stay:
/// "Mozart K.525" keeps its ".525".
QString stripKnownExtension(const QString &fileName);

/// Removes trailing AI marks, with or without counter (".midipilot",
/// ".mcp.3", ...), repeatedly - so marks never stack.
QString stripMarks(const QString &baseName);

/// The normalised base of a file name: known extension, then marks removed,
/// surrounding spaces and trailing dots trimmed (Windows drops them silently).
QString baseOf(const QString &fileName);

/// True when \a path's file name carries \a mark (optionally with a counter)
/// directly before a .mid/.midi extension.
bool hasMark(const QString &path, const QString &mark);

/// "<base>.<mark>.mid" for counter <= 1, "<base>.<mark>.<counter>.mid" above.
QString markedFileName(const QString &base, const QString &mark, int counter = 1);

/// Outcome of planSave().
struct SavePlan {
    bool ok = false;
    QString error;          ///< set when !ok - addressed to the model
    QString targetPath;     ///< absolute path to write
    bool inPlace = false;   ///< the document's own marked file is rewritten
    bool nameIgnored = false; ///< a name was passed but the rules did not use it
};

/**
 * \brief Decides where an AI save goes.
 *
 * \param documentPath  the document's current path (empty = untitled)
 * \param requestedName name from the tool call: bare name, name with
 *                      extension/mark, or an absolute path; may be empty
 * \param mark          markFor(source)
 * \param asNewCopy     false = save_document (own marked file in place; the
 *                      name only matters for an untitled document), true =
 *                      save_document_as (always a new file)
 * \param fallbackDir   folder for a bare name without a source document
 *                      (the editor's last Open/Save folder)
 * \param isTaken       true when a candidate path must not be written
 *                      (exists on disk or belongs to another open document)
 */
SavePlan planSave(const QString &documentPath,
                  const QString &requestedName,
                  const QString &mark,
                  bool asNewCopy,
                  const QString &fallbackDir,
                  const std::function<bool(const QString &)> &isTaken);

} // namespace AiFileNaming

#endif // AIFILENAMING_H
