/*
 * MidiEditor AI
 *
 * FfxivPlayabilityValidator — Phase 46
 *
 * Headless FFXIV playability check, shared by the `validate_ffxiv` AI tool
 * and the GUI's "Check FFXIV Playability" dialog. Grown out of the octet
 * experiment: the monophony/overlap check used to exist ONLY inside the AI
 * tool, so a human arranging by hand found those spots by exporting, playing
 * the song in game and hearing notes go missing.
 *
 * What it checks, per track (tracks WITHOUT notes are exempt from the name
 * check - a silent track occupies no performer, and the app's own default
 * "Tempo Track" must not flag every file):
 *   - track name against the canonical FFXIV instrument spellings
 *     (FFXIVChannelFixer::instrumentNames())
 *   - note range C3-C6 (MIDI 48-84)
 *   - stacked duplicates: the SAME pitch starting twice on the SAME tick on
 *     one performer - the most common hand-arranging slip
 *   - simultaneous notes: distinct pitches STARTING on the same tick on one
 *     performer - a chord a monophonic performer cannot play. Guitar tracks
 *     spread notes over SEVERAL channels (variant switches), so only
 *     same-channel groups count there.
 *
 * Deliberately NOT checked (user-verified in-game behaviour, 2026-07-27):
 *   - hold-overlaps: a held note overlapped by later staccato notes is
 *     inaudible at game speed; only same-tick starts truly collide.
 *   - program changes: in game the TRACK NAME selects the instrument (with
 *     guitars, the per-note channel drives variant switches) - program
 *     changes only make the editor's SF2 playback faithful, so a
 *     name/program mismatch is the channel fixer's business, never a
 *     playability failure.
 *
 * Unlike the original tool implementation this reports ALL findings (the
 * tool used to stop at the first overlap per track) and carries the
 * offending NoteOn events, so the GUI can select them in the editor.
 */

#ifndef FFXIVPLAYABILITYVALIDATOR_H_
#define FFXIVPLAYABILITYVALIDATOR_H_

#include <QList>
#include <QString>

class MidiFile;
class MidiEvent;

struct FfxivPlayabilityIssue {
    enum class Type {
        TrackName,        ///< name matches no FFXIV instrument
        OutOfRange,       ///< note outside C3-C6
        DuplicateNote,    ///< same pitch, same start tick, same performer
        Overlap,          ///< distinct pitches starting on the same tick
                          ///< (guitar tracks: on the same channel)
        ChannelSpread,    ///< non-guitar track's notes on several channels -
                          ///< EDITOR playback only (in game the name rules);
                          ///< the channel fixer is the repair
        EmptyTrack,       ///< instrument-named track without any notes (info)
        TempoMap,         ///< tempo map dense enough to weigh the file down
                          ///< (DAW ramp exported as one event every few ticks);
                          ///< file-level, repaired by Thin Tempo Map
        VoiceCeiling,     ///< raw voice peak over 16 - synthesized by the
                          ///< DIALOG from an Auto-Fit dry run, never emitted
                          ///< by the validator itself (keeps it dependency-free)
        NoteRate          ///< notes/sec hotspot - dialog-synthesized as well
    };

    Type type;
    int track = -1;
    int tick = 0;              ///< first tick involved (0 for TrackName)
    QString details;           ///< human-readable, one line
    QList<MidiEvent *> events; ///< offending NoteOn events (may be empty)
};

/** Which checks validate() runs - the GUI dialog exposes these as
 *  checkboxes so single aspects can be re-checked in isolation. The two
 *  same-tick findings are SEPARATE flags because they mean opposite things:
 *  a chord is often a deliberate arranging decision (the game rolls it as a
 *  fast arpeggio), a stacked duplicate is almost always a defect - hunting
 *  duplicates must not mean wading through thousands of chords. */
struct FfxivPlayabilityChecks {
    bool simultaneousNotes = true;  ///< distinct pitches starting on one tick
                                    ///< (chords - often intentional)
    bool stackedDuplicates = true;  ///< the same pitch starting twice on one
                                    ///< tick (almost always a defect)
    bool range = true;         ///< C3-C6
    bool trackNames = true;    ///< FFXIV instrument names
    bool channelSpread = true; ///< editor-playback channel consistency
    bool emptyTracks = true;   ///< instrument-named tracks without notes
    bool tempoMap = true;      ///< dense tempo maps (DAW ramps)
};

/** Threshold for the tempo-map check, kept next to the enum so the rule is
 *  quotable in the manual and testable without a widget.
 *
 *  A finding is raised when the map is dense enough that the density is a
 *  PROPERTY OF THE EXPORT rather than of the music:
 *    - more than kTempoMapAbsoluteLimit events, whatever the length, or
 *    - at least kTempoMapDenseFloor events AND more than
 *      kTempoMapEventsPerBar of them per bar on average.
 *  A hand-built accelerando has a handful of events per section and stays
 *  quiet; the DAW ramp that triggered this check has dozens per bar. */
namespace FfxivTempoMapRule {
constexpr int kTempoMapAbsoluteLimit = 500;
constexpr int kTempoMapDenseFloor = 64;
constexpr double kTempoMapEventsPerBar = 4.0;
} // namespace FfxivTempoMapRule

struct FfxivPlayabilityReport {
    bool ok = false;      ///< false only when no file was given
    QString error;
    QList<FfxivPlayabilityIssue> issues;
    int checkedTracks = 0;
    int checkedNotes = 0;

    bool valid() const { return ok && issues.isEmpty(); }
    int countOf(FfxivPlayabilityIssue::Type t) const;

    /** All offending note events across all issues, deduplicated - what the
     *  GUI selects in the editor. */
    QList<MidiEvent *> offendingNotes() const;
};

class FfxivPlayabilityValidator {
public:
    static FfxivPlayabilityReport validate(
        MidiFile *file, const FfxivPlayabilityChecks &checks = {});
};

/** The SURPLUS notes of the given collision findings - exactly what the
 *  "Delete colliding notes" repair removes. Lives next to the detection so
 *  the rule is testable without a widget (the GUI dialog only scopes it).
 *
 *  `indices` picks issues out of `issues`: the dialog passes the current
 *  tree selection, or every index for "all collisions". Non-collision types
 *  (range, track names, ...) are skipped, so a mixed selection is safe and
 *  simply contributes nothing.
 *
 *  Survivor per collision group: highest pitch (the melody rule Auto-Fit
 *  documents), among equal pitches the louder note. A note may survive one
 *  issue and be a victim of another (C+C+E: the chord keeps E, the
 *  duplicate pair contributes both Cs) - the victim UNION handles that
 *  correctly. The result follows the order of `indices` and is
 *  deduplicated. */
QList<MidiEvent *> ffxivCollisionSurplus(
    const QList<FfxivPlayabilityIssue> &issues, const QList<int> &indices);

#endif // FFXIVPLAYABILITYVALIDATOR_H_
