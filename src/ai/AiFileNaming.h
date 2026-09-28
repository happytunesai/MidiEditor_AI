/*
 * MidiEditor AI - file names for documents the AI saves (Phase 51, v2.5.0).
 *
 * The "AI gate": every file MidiPilot or an MCP client writes is a MARKED copy -
 * <name>.midipilot.mid (MidiPilot) or <name>.mcp.mid (MCP) - and an existing
 * file is never written over. Two exceptions: the document's own file when it
 * already carries the caller's mark (the AI's working copy, so repeated saves
 * go into the same file), and the document's own MIDI file when the USER chose
 * "overwrite" (asked by the AI, remembered per tab, or set in the settings -
 * MainWindow::aiSaveDocument). A taken name gets a counter
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

/// True when an AI save of the document at \a documentPath has to settle
/// "overwrite or copy" first: the document has a file MidiEditor can write
/// back (.mid/.midi) and it is not the caller's own marked working copy.
/// Untitled documents, imports that cannot be written back (Guitar Pro,
/// MusicXML, ...) and the AI's own copy never raise the question.
bool offersOverwrite(const QString &documentPath, const QString &mark);

/// True when two paths name the same file (cleaned; case-insensitive on Windows).
bool samePath(const QString &a, const QString &b);

/// The user's side of "overwrite or copy" for one document (per tab).
struct ExistingFileDecision {
    QString askedPath;    ///< the file the question was last put for
    QString answeredPath; ///< the file the remembered answer was given for
    QString answer;       ///< "copy" or "overwrite" (empty = none yet)
};

enum class ExistingFileSave { Ask, Copy, Overwrite };

/**
 * \brief Settles "overwrite or copy" for a document at \a documentPath for
 *        which offersOverwrite() holds.
 *
 * A copy writes nothing over, so \a mode "copy" is always taken. "overwrite"
 * only counts when the user said so for THIS file: in answer to the question
 * put for this path, as the answer remembered for this path, or in the
 * settings - an answer given for another file (another tab, the file before a
 * Save As) raises the question again. Without \a mode the remembered answer
 * for this path decides, then \a setting ("ask", "copy", "overwrite").
 */
ExistingFileSave resolveExistingFileSave(const QString &documentPath, const QString &mode,
                                         const ExistingFileDecision &decision,
                                         const QString &setting);

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
 * \param overwriteDocumentFile  the user chose to overwrite the document's
 *                      own file: save_document writes it in place when
 *                      offersOverwrite() holds (never for save_document_as)
 */
SavePlan planSave(const QString &documentPath,
                  const QString &requestedName,
                  const QString &mark,
                  bool asNewCopy,
                  const QString &fallbackDir,
                  const std::function<bool(const QString &)> &isTaken,
                  bool overwriteDocumentFile = false);

} // namespace AiFileNaming

#endif // AIFILENAMING_H
