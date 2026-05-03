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

    /// Progress callback: (percent 0-100, phase description)
    using ProgressCallback = std::function<void(int, const QString &)>;

    /**
     * \brief Fix all channel assignments and program_change events.
     * \param file       The loaded MidiFile
     * \param forcedTier 0 = auto-detect, 2 = Rebuild, 3 = Preserve
     * \param progress   Optional callback for progress updates
     * \return JSON result with success, channelMap, summary
     */
    static QJsonObject fixChannels(MidiFile *file, int forcedTier = 0,
                                   ProgressCallback progress = nullptr);

private:
    // Percussion instrument base names that go to CH9
    static bool isPercussion(const QString &baseName);

    // Guitar instrument base names
    static bool isGuitar(const QString &baseName);

    // Strip [+-]\d+$ suffix from any instrument name
    static QString stripSuffix(const QString &name);

    // Map instrument base name → GM program number (-1 if unknown)
    static int programNumber(const QString &baseName);

    // 1.6.1 (upstream a35f1ee): for an unmatched track, return the MIDI
    // channel that carries the majority of its NoteOn events, or -1 if
    // the track has no notes at all. Used to keep generically-named drum
    // tracks (e.g. "Drums", "Perc") on channel 9 instead of letting the
    // Tier-2 numeric assignment ruin their GM percussion mapping.
    static int dominantNoteChannel(MidiFile *file, MidiTrack *track);
};

#endif // FFXIVCHANNELFIXER_H
