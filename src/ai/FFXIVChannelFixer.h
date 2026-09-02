#ifndef FFXIVCHANNELFIXER_H
#define FFXIVCHANNELFIXER_H

#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QHash>
#include <QList>
#include <functional>

class MidiFile;
class MidiTrack;

/**
 * \class FFXIVChannelFixer
 *
 * \brief Deterministic FFXIV channel and program_change fixer.
 *
 * Fixes channel assignments and program_change events for FFXIV Bard
 * Performance MIDI files. Can be invoked from the UI (Tools menu) or
 * from the AI tool call (setup_channel_pattern).
 *
 * Rules:
 *  1. Track N → Channel N  (T0→CH0, T1→CH1, …)
 *  2. Percussion tracks (Bass Drum, Snare Drum, Cymbal, Bongo) → CH9.
 *     Timpani follows Rule 1 (it is tonal).
 *  3. Guitar tracks follow Rule 1; extra variants get free channels.
 *  4. program_change at tick 0 for every used channel on every track.
 *  5. All events migrated to the correct channel via moveToChannel().
 */
class FFXIVChannelFixer {
public:
    /**
     * \brief Analyze the file and return scan info for the tier selection dialog.
     * \param file  The loaded MidiFile
     * \return JSON with trackCount, ffxivTrackCount, autoDetectedTier, etc.
     */
    static QJsonObject analyzeFile(MidiFile *file);

    /**
     * \brief Eligibility gate (v2.3.1, review F066): decides whether the
     *        file IS an FFXIV MIDI before either tier is allowed to run.
     *
     * A single renamed track used to be enough to let Rebuild loose on a
     * plain General MIDI file (every other track got its channel/program
     * rebuilt, tracks beyond index 15 were clamped onto channel 15). The
     * gate looks at the tracks that actually carry notes:
     *   (a) no track name matches an FFXIV instrument  -> not eligible;
     *   (b) a note-carrying track whose name is not an FFXIV instrument
     *       -> not eligible, the reason lists them by index and name.
     *       Tracks without notes (conductor, title, empty) are ignored, and
     *       so is a GM drum track whose notes live on channel 9 - Tier 2
     *       keeps such a track on channel 9 by design (drum-split leftover);
     *   (c) more than 16 note-carrying tracks -> Tier 2 (Rebuild) is not
     *       eligible, Tier 3 (Preserve) still is.
     * Name matching is programNumber(stripSuffix(name)) - nothing wider.
     *
     * This is the ONE implementation: analyzeFile() embeds it (dialog +
     * MainWindow warning), fixChannels() runs it before its first edit, and
     * the AI/MCP setup_channel_pattern tool consults it before opening a
     * Protocol action. Read-only.
     *
     * \return {eligible, reason, tier2Eligible, tier2Reason, noteTrackCount,
     *          ffxivNamedNoteTrackCount, nonFfxivNoteTracks:[{index,name}]}
     */
    static QJsonObject checkEligibility(MidiFile *file);

    /// Progress callback: (percent 0-100, phase description).
    ///
    /// Called while the file is mid-edit, with the caller's Protocol
    /// action open and the bulk undo snapshots held. It must NOT re-enter
    /// the Qt event loop: a processEvents() here can dispatch a queued
    /// MidiPilot/MCP tool step that calls startNewAction() on the same
    /// file, which commits this fixer's half-finished action and splits
    /// the fix across two undo steps.
    using ProgressCallback = std::function<void(int, const QString &)>;

    /**
     * \brief Fix all channel assignments and program_change events.
     * \param file            The loaded MidiFile
     * \param forcedTier      0 = auto-detect, 2 = Rebuild, 3 = Preserve
     * \param progress        Optional callback for progress updates
     * \param resyncNonGuitar Tier-3 opt-in (v2.0): rewrite a non-guitar
     *        channel's tick-0 program change when it no longer matches the
     *        owning track's name (e.g. after renaming Trumpet -> Trombone).
     *        Only mismatched channels are touched, exactly ONE tick-0 PC per
     *        channel is written, and mid-song PCs are preserved - a second
     *        consecutive run is a no-op. Default off: plain Tier 3 leaves
     *        non-guitar channels byte-identical, as before.
     * \return JSON result with success, channelMap, summary
     */
    static QJsonObject fixChannels(MidiFile *file, int forcedTier = 0,
                                   ProgressCallback progress = nullptr,
                                   bool resyncNonGuitar = false);

    // Phase 46: the four classifiers below are the CANONICAL FFXIV instrument
    // knowledge of the app (names, programs, guitar/percussion routing) and
    // are needed beyond the fixer - FfxivPlayabilityValidator and the AI
    // tool surface reuse them instead of keeping duplicate tables.

    // Percussion instrument base names that go to CH9
    static bool isPercussion(const QString &baseName);

    // Guitar instrument base names
    static bool isGuitar(const QString &baseName);

    // Strip [+-]\d+$ suffix from any instrument name
    static QString stripSuffix(const QString &name);

    // Map instrument base name → GM program number (-1 if unknown)
    static int programNumber(const QString &baseName);

    // All legal FFXIV instrument base names, sorted - the exact spellings
    // programNumber() accepts ("Double Bass" with a space,
    // "ElectricGuitarOverdriven" without). For error messages and the AI.
    static QStringList instrumentNames();

private:

    // 1.6.1 (upstream a35f1ee): for an unmatched track, return the MIDI
    // channel that carries the majority of its NoteOn events, or -1 if
    // the track has no notes at all. Used to keep generically-named drum
    // tracks (e.g. "Drums", "Perc") on channel 9 instead of letting the
    // Tier-2 numeric assignment ruin their GM percussion mapping.
    static int dominantNoteChannel(MidiFile *file, MidiTrack *track);
};

#endif // FFXIVCHANNELFIXER_H
