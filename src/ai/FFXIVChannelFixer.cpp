#include "FFXIVChannelFixer.h"

#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/ProgChangeEvent.h"
#include "../MidiEvent/TextEvent.h"
#include "../protocol/ProtocolEntry.h"

#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <climits>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString FFXIVChannelFixer::stripSuffix(const QString &name) {
    QString base = name;
    static const QRegularExpression suffixRe(QStringLiteral("[+-]\\d+$"));
    base.remove(suffixRe);
    return base;
}

bool FFXIVChannelFixer::isPercussion(const QString &baseName) {
    static const QSet<QString> drums = {
        QStringLiteral("Bass Drum"),
        QStringLiteral("Snare Drum"),
        QStringLiteral("Cymbal"),
        QStringLiteral("Bongo")
    };
    return drums.contains(baseName);
}

bool FFXIVChannelFixer::isGuitar(const QString &baseName) {
    return baseName.startsWith(QStringLiteral("ElectricGuitar"));
}

namespace {
// NOTE: program numbers must match the actual presets in the FFXIV
// SoundFont (FF14-c3c6-fixed.sf2). Mismatches cause silent fallback to
// bank 0 / prog 0 (= Piano). Verified 2026-04-28 against phdr chunk.
const QHash<QString, int> &instrumentProgramMap() {
    static const QHash<QString, int> map = {
        {"Piano", 0},       {"Harp", 46},       {"Fiddle", 45},
        {"Lute", 25},       {"Fife", 72},       {"Flute", 73},
        {"Oboe", 68},       {"Panpipes", 75},   {"Clarinet", 71},
        {"Trumpet", 56},    {"Saxophone", 65},  {"Trombone", 57},
        {"Horn", 60},       {"Tuba", 58},
        {"Violin", 40},     {"Viola", 41},      {"Cello", 42},
        {"Double Bass", 43},
        {"Timpani", 47},    {"Bongo", 116},     {"Bass Drum", 117},
        {"Snare Drum", 118},{"Cymbal", 119},
        {"ElectricGuitarClean", 27},       {"ElectricGuitarMuted", 28},
        {"ElectricGuitarOverdriven", 29},  {"ElectricGuitarPowerChords", 30},
        {"ElectricGuitarSpecial", 31}
    };
    return map;
}
} // namespace

int FFXIVChannelFixer::programNumber(const QString &baseName) {
    return instrumentProgramMap().value(baseName, -1);
}

QStringList FFXIVChannelFixer::instrumentNames() {
    QStringList names = instrumentProgramMap().keys();
    names.sort();
    return names;
}

// ---------------------------------------------------------------------------
// checkEligibility - "is this an FFXIV MIDI at all?" (v2.4.0, review F066)
// ---------------------------------------------------------------------------

namespace {
const char *kNoFfxivNamesText =
    "No FFXIV instrument names detected. "
    "Track names must match FFXIV instruments (e.g. Piano, Flute, "
    "ElectricGuitarOverdriven, Snare Drum, etc.).";

// True when `track` has at least one NoteOn on channel `ch`. Shared by the
// Preserve program fallback in fixChannels() and analyzeFile()'s listing of
// guitar tracks without a program, so the dialog never announces a fallback
// the fixer will not perform (review R231-22).
bool trackPlaysOnChannel(MidiFile *file, MidiTrack *track, int ch) {
    if (!file || !track || ch < 0 || ch > 15) return false;
    MidiChannel *channel = file->channel(ch);
    if (!channel) return false;
    QMultiMap<int, MidiEvent *> *map = channel->eventMap();
    for (auto it = map->begin(); it != map->end(); ++it) {
        if (it.value()->track() == track && dynamic_cast<NoteOnEvent *>(it.value()))
            return true;
    }
    return false;
}

// The first track that is NOT a guitar and plays at least one note on channel
// `ch`, or nullptr. The Preserve program fallback must not turn such a channel
// into a guitar channel: CLEAN would strip the other instrument's program
// changes and the guitar program would replace its sound (fixer review CF-06).
// Shared by fixChannels() and analyzeFile(), like trackPlaysOnChannel().
MidiTrack *otherInstrumentOnChannel(MidiFile *file, int ch) {
    if (!file || ch < 0 || ch > 15) return nullptr;
    MidiChannel *channel = file->channel(ch);
    if (!channel) return nullptr;
    QMultiMap<int, MidiEvent *> *map = channel->eventMap();
    for (auto it = map->begin(); it != map->end(); ++it) {
        if (!dynamic_cast<NoteOnEvent *>(it.value())) continue;
        MidiTrack *owner = it.value()->track();
        if (owner && !FFXIVChannelFixer::isGuitar(FFXIVChannelFixer::stripSuffix(owner->name())))
            return owner;
    }
    return nullptr;
}

// The octave suffix of a track name ("+1", "-2"), empty when there is none -
// the part stripSuffix() removes. It is the in-game octave setting, so a
// rename keeps it (fixer review CF-01).
QString octaveSuffix(const QString &name) {
    static const QRegularExpression suffixRe(QStringLiteral("[+-]\\d+$"));
    return suffixRe.match(name).captured(0);
}
} // namespace

QJsonObject FFXIVChannelFixer::checkEligibility(MidiFile *file) {
    QJsonObject out;
    out["eligible"]      = false;
    out["tier2Eligible"] = false;
    out["reason"]        = QString();
    out["tier2Reason"]   = QString();
    out["noteTrackCount"]           = 0;
    out["ffxivNamedNoteTrackCount"] = 0;
    out["nonFfxivNoteTracks"]       = QJsonArray();

    if (!file) {
        out["reason"] = QStringLiteral("No file loaded.");
        return out;
    }
    const int trackCount = file->numTracks();
    if (trackCount == 0) {
        out["reason"] = QStringLiteral("No tracks in file.");
        return out;
    }

    // One pass over every channel: NoteOn count per (track, channel). This
    // is what decides "carries notes" and, for unmatched names, whether the
    // track is a GM drum track parked on channel 9 (same rule Tier 2 applies
    // via dominantNoteChannel(), computed here without a per-track rescan).
    QHash<MidiTrack *, QHash<int, int>> notesPerTrack;
    for (int ch = 0; ch < 16; ch++) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (!dynamic_cast<NoteOnEvent *>(it.value())) continue;
            notesPerTrack[it.value()->track()][ch]++;
        }
    }

    int ffxivNameCount = 0;          // every track, notes or not (rule a)
    int noteTrackCount = 0;
    bool clampRisk = false;          // rule (c): a melodic note track at index > 15
    QSet<QString> guitarVariantsWithin16; // guitar variants Tier 2 places by index <= 15
    int ffxivNamedNoteTrackCount = 0;
    QJsonArray nonFfxivNoteTracks;
    QStringList offenders;

    for (int t = 0; t < trackCount; t++) {
        MidiTrack *track = file->track(t);
        if (!track) continue;
        const QString name = track->name();
        const QString base = stripSuffix(name);
        const bool isFfxivName = programNumber(base) >= 0;
        if (isFfxivName) ffxivNameCount++;
        // Tier 2 registers a guitar variant at its first occurrence (notes or
        // not) and routes every later track of that variant onto its channel.
        if (isGuitar(base) && t <= 15) guitarVariantsWithin16.insert(base);

        auto notesIt = notesPerTrack.constFind(track);
        if (notesIt == notesPerTrack.constEnd()) continue; // no notes: ignored
        noteTrackCount++;
        int bestCh = -1, bestCount = 0;
        for (auto c = notesIt->constBegin(); c != notesIt->constEnd(); ++c) {
            if (c.value() > bestCount) { bestCount = c.value(); bestCh = c.key(); }
        }
        // Rule (c) counts by TRACK INDEX, not by number of note tracks: Tier 2
        // maps track t onto channel t and clamps t > 15 onto channel 15, so an
        // idle track 0 plus 16 note tracks still loses the last one. Percussion
        // (predominantly channel 9) is routed to channel 9 regardless of index.
        // ...and mirrors Tier 2's routing (review R231-21): a percussion NAME
        // goes to channel 9 whatever its notes' channel, an unmatched name
        // stays on channel 9 only when its notes are predominantly there, and
        // every other track (FFXIV melodic names, guitars) is renumbered.
        {
            const bool unmatched = !isGuitar(base) && programNumber(base) < 0;
            // A guitar variant already placed within the first 16 tracks is
            // routed onto that first occurrence's channel - no clamp either.
            const bool duplicateGuitar = isGuitar(base) && guitarVariantsWithin16.contains(base);
            if (t > 15 && !isPercussion(base) && !duplicateGuitar
                && !(unmatched && bestCh == 9))
                clampRisk = true;
        }
        if (isFfxivName) {
            ffxivNamedNoteTrackCount++;
            continue;
        }

        // Unmatched name: a track whose notes are predominantly on channel 9
        // is a GM drum track (e.g. the "Drums" leftover of the FFXIV drum
        // split). Tier 2 keeps it on channel 9 instead of renumbering it, so
        // it is part of a legitimate FFXIV file and must not block the gate.
        if (bestCh == 9) continue;

        const QString shown = name.trimmed().isEmpty()
            ? QStringLiteral("(unnamed)") : name.trimmed();
        QJsonObject entry;
        entry["index"] = t;
        entry["name"]  = shown;
        nonFfxivNoteTracks.append(entry);
        offenders << QStringLiteral("%1 %2").arg(t).arg(shown);
    }

    out["noteTrackCount"]           = noteTrackCount;
    out["ffxivNamedNoteTrackCount"] = ffxivNamedNoteTrackCount;
    out["nonFfxivNoteTracks"]       = nonFfxivNoteTracks;

    // (c) is reported independently of (a)/(b) so the dialog can grey out
    // Rebuild while Preserve stays available.
    if (clampRisk) {
        out["tier2Reason"] = QStringLiteral(
            "This file has melodic tracks with notes beyond track 16 (MIDI has "
            "only 16 channels) - Rebuild (Full Reassignment) would merge them "
            "onto channel 15. Use Preserve (Minimal Changes) instead, or merge "
            "tracks first. (%1 tracks with notes)").arg(noteTrackCount);
    } else {
        out["tier2Eligible"] = true;
    }

    // (a) zero FFXIV names anywhere - the long-standing message stays.
    if (ffxivNameCount == 0) {
        out["reason"] = QString::fromLatin1(kNoFfxivNamesText);
        return out;
    }

    // (b) note-carrying tracks that are not FFXIV instruments.
    if (!offenders.isEmpty()) {
        const int kMaxListed = 12;
        QStringList listed = offenders.mid(0, kMaxListed);
        QString list = listed.join(QStringLiteral(", "));
        if (offenders.size() > kMaxListed)
            list += QStringLiteral(" and %1 more").arg(offenders.size() - kMaxListed);
        out["reason"] = (offenders.size() == 1)
            ? QStringLiteral("Track %1 is not an FFXIV instrument - rename it first.").arg(list)
            : QStringLiteral("Tracks %1 are not FFXIV instruments - rename them first.").arg(list);
        return out;
    }

    out["eligible"] = true;
    return out;
}

// ---------------------------------------------------------------------------
// autoTier - the tier fixChannels() picks when none is forced (read-only,
// same rules as its TIER DETECTION block)
// ---------------------------------------------------------------------------

int FFXIVChannelFixer::autoTier(MidiFile *file) {
    if (!file || file->numTracks() == 0) return 2;
    const int trackCount = file->numTracks();
    QVector<QString> baseNames(trackCount);
    bool hasGuitar = false;
    for (int t = 0; t < trackCount; t++) {
        baseNames[t] = stripSuffix(file->track(t)->name());
        if (isGuitar(baseNames[t])) hasGuitar = true;
    }
    if (!hasGuitar) return 2;

    // (A) a guitar program at tick 0 on any channel -> already configured
    for (int ch = 0; ch < 16; ch++) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        const int prog = channel->progAtTick(0);
        if (prog >= 27 && prog <= 31) return 3;
    }

    // (B) a guitar track with notes on more than one guitar channel
    QSet<int> knownGuitarChs;
    for (int t = 0; t < trackCount; t++) {
        if (!isGuitar(baseNames[t])) continue;
        const int aCh = file->track(t)->assignedChannel();
        if (aCh >= 0) knownGuitarChs.insert(aCh);
    }
    for (int t = 0; t < trackCount; t++) {
        if (!isGuitar(baseNames[t])) continue;
        int chsWithNotes = 0;
        for (int ch : knownGuitarChs) {
            if (trackPlaysOnChannel(file, file->track(t), ch)) chsWithNotes++;
        }
        if (chsWithNotes > 1) return 3;
    }
    return 2;
}

// ---------------------------------------------------------------------------
// analyzeFile  - read-only scan for the tier selection dialog
// ---------------------------------------------------------------------------

QJsonObject FFXIVChannelFixer::analyzeFile(MidiFile *file) {
    QJsonObject result;
    if (!file || file->numTracks() == 0) {
        result["valid"] = false;
        result["eligibility"] = checkEligibility(file);
        return result;
    }

    const int trackCount = file->numTracks();
    int ffxivTrackCount = 0;
    bool hasGuitar = false;
    QStringList guitarVariants, percussionTracks, melodicTracks;

    for (int t = 0; t < trackCount; t++) {
        QString base = stripSuffix(file->track(t)->name());
        if (programNumber(base) < 0) continue;
        ffxivTrackCount++;
        if (isGuitar(base)) {
            hasGuitar = true;
            if (!guitarVariants.contains(base))
                guitarVariants.append(base);
        } else if (isPercussion(base)) {
            if (!percussionTracks.contains(base))
                percussionTracks.append(base);
        } else {
            if (!melodicTracks.contains(base))
                melodicTracks.append(base);
        }
    }

    // Count existing program changes
    int totalProgramChanges = 0;
    for (int ch = 0; ch < 16; ch++) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (dynamic_cast<ProgChangeEvent *>(it.value()))
                totalProgramChanges++;
        }
    }

    // v2.4.0 (review F065): guitar tracks that play on a channel whose tick-0
    // program is not a guitar program (27-31) - e.g. a Viola track renamed to
    // ElectricGuitarOverdriven. Preserve gives such a channel the program of
    // the track's own variant; the dialog lists them so the user sees why.
    QJsonArray guitarTracksWithoutProgram;
    // Fixer review CF-06: the same situation on a channel another instrument
    // plays on - Preserve leaves such a channel alone instead.
    QJsonArray guitarTracksOnSharedChannel;
    QSet<int> listedChannels;
    for (int t = 0; t < trackCount; t++) {
        MidiTrack *track = file->track(t);
        if (!isGuitar(stripSuffix(track->name()))) continue;
        // Same channel and the same "plays there" test as the Preserve
        // fallback in fixChannels() - the two must agree (review R231-22), and
        // like the fallback one entry per CHANNEL (its first guitar track).
        int ch = track->assignedChannel();
        if (ch < 0 || ch > 15) ch = qMin(t, 15);
        if (listedChannels.contains(ch)) continue;
        if (!trackPlaysOnChannel(file, track, ch)) continue;
        const int prog = file->channel(ch)->progAtTick(0);
        if (prog >= 27 && prog <= 31) continue;
        listedChannels.insert(ch);
        QJsonObject entry;
        entry["index"]   = t;
        entry["name"]    = track->name();
        entry["channel"] = ch;
        if (MidiTrack *other = otherInstrumentOnChannel(file, ch)) {
            entry["sharedWith"] = other->name();
            guitarTracksOnSharedChannel.append(entry);
        } else {
            guitarTracksWithoutProgram.append(entry);
        }
    }

    // Auto-detect tier: the very rules fixChannels() applies when no tier is
    // forced (fixer review CF-07). The dialog used to pre-select Preserve for
    // a guitar program anywhere in the song and never for guitar notes spread
    // over several guitar channels, so it could propose the other mode than
    // the one MidiPilot and MCP run.
    const int detectedTier = (ffxivTrackCount > 0) ? autoTier(file) : 1;

    result["valid"]               = (ffxivTrackCount > 0);
    result["trackCount"]          = trackCount;
    result["ffxivTrackCount"]     = ffxivTrackCount;
    result["hasGuitar"]           = hasGuitar;
    result["totalProgramChanges"] = totalProgramChanges;
    result["autoDetectedTier"]    = detectedTier;
    result["guitarVariants"]      = QJsonArray::fromStringList(guitarVariants);
    result["percussionTracks"]    = QJsonArray::fromStringList(percussionTracks);
    result["melodicTracks"]       = QJsonArray::fromStringList(melodicTracks);
    result["guitarTracksWithoutProgram"] = guitarTracksWithoutProgram;
    result["guitarTracksOnSharedChannel"] = guitarTracksOnSharedChannel;

    // v2.4.0 eligibility gate (review F066) - the fields above are unchanged,
    // the dialog and MainWindow read the gate from here.
    const QJsonObject gate = checkEligibility(file);
    result["noteTrackCount"]           = gate["noteTrackCount"];
    result["ffxivNamedNoteTrackCount"] = gate["ffxivNamedNoteTrackCount"];
    result["nonFfxivNoteTracks"]       = gate["nonFfxivNoteTracks"];
    result["eligibility"]              = gate;
    return result;
}

// 1.6.1 (upstream a35f1ee): scan all 16 channels and return whichever one
// owns the most NoteOn events from `track`. Returns -1 if the track has
// no NoteOn events at all (in which case the caller should fall back to
// the numeric/sequential assignment).
int FFXIVChannelFixer::dominantNoteChannel(MidiFile *file, MidiTrack *track) {
    if (!file || !track) return -1;
    QHash<int, int> noteCount;
    for (int ch = 0; ch < 16; ch++) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *events = channel->eventMap();
        for (auto it = events->begin(); it != events->end(); ++it) {
            if (it.value()->track() == track
                && dynamic_cast<NoteOnEvent *>(it.value())) {
                noteCount[ch]++;
            }
        }
    }
    int bestCh = -1, bestCount = 0;
    for (auto it = noteCount.begin(); it != noteCount.end(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            bestCh = it.key();
        }
    }
    return bestCh;
}

// ---------------------------------------------------------------------------
// fixChannels  - the main entry point (3-tier smart detection)
// ---------------------------------------------------------------------------

QJsonObject FFXIVChannelFixer::fixChannels(MidiFile *file, int forcedTier,
                                           ProgressCallback progress,
                                           bool resyncNonGuitar) {
    // Helper to report progress if callback is set
    auto reportProgress = [&](int pct, const QString &msg) {
        if (progress) progress(pct, msg);
    };

    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QStringLiteral("No file loaded.");
        return result;
    }

    const int trackCount = file->numTracks();
    if (trackCount == 0) {
        result["success"] = false;
        result["error"] = QStringLiteral("No tracks in file.");
        return result;
    }

    // All 5 guitar variant names
    static const QStringList allGuitarVariants = {
        QStringLiteral("ElectricGuitarClean"),
        QStringLiteral("ElectricGuitarMuted"),
        QStringLiteral("ElectricGuitarOverdriven"),
        QStringLiteral("ElectricGuitarPowerChords"),
        QStringLiteral("ElectricGuitarSpecial")
    };

    // -----------------------------------------------------------------------
    // 0. PRE-SCAN - classify tracks, count FFXIV matches, scan guitar progs
    // -----------------------------------------------------------------------

    reportProgress(5, QStringLiteral("Scanning tracks..."));

    QVector<QString> baseNames(trackCount);
    bool hasGuitar = false;
    QSet<QString> guitarVariantsPresent;

    for (int t = 0; t < trackCount; t++) {
        QString base = stripSuffix(file->track(t)->name());
        baseNames[t] = base;
        if (isGuitar(base)) {
            hasGuitar = true;
            guitarVariantsPresent.insert(base);
        }
    }

    // TIER 1 - Not an FFXIV MIDI. v2.4.0 (review F066): the shared
    // eligibility gate replaces the bare "zero names" test, so one renamed
    // track in a General MIDI file no longer lets Rebuild loose on the other
    // eleven. Read-only; a refused file is returned before the first edit.
    const QJsonObject gate = checkEligibility(file);
    if (!gate["eligible"].toBool()) {
        result["success"] = false;
        result["error"] = gate["reason"].toString();
        result["eligibility"] = gate;
        result["tier"] = 1;
        return result;
    }

    // -----------------------------------------------------------------------
    // SINGLE guitar-program scan - used for BOTH tier detection AND channel map.
    // Scans all 16 channels for ProgChangeEvents with guitar programs (27-31).
    // -----------------------------------------------------------------------

    reportProgress(10, QStringLiteral("Analyzing guitar programs..."));

    QHash<int, int> guitarChToProgram;  // channel -> guitar program number

    if (hasGuitar) {
        // Use progAtTick(0) -- the effective program at tick 0, which is
        // exactly what the channel view displays. This is reliable even when
        // multiple PCs exist at tick 0 from previous Tier 2 runs (one per
        // track): progAtTick() returns the most recently inserted one, the one
        // playback and the saved file apply last (fixer review CF-03).
        for (int ch = 0; ch < 16; ch++) {
            MidiChannel *channel = file->channel(ch);
            if (!channel) continue;
            int prog = channel->progAtTick(0);
            if (prog >= 27 && prog <= 31)
                guitarChToProgram[ch] = prog;
        }
    }

    // -----------------------------------------------------------------------
    // TIER DETECTION - Tier 2 (Rebuild) vs Tier 3 (Preserve)
    //
    //   Preserve mode if EITHER:
    //   (A) Guitar program_changes already exist → file was configured
    //   (B) A guitar track has notes on >1 guitar channel (multi-ch switches)
    // -----------------------------------------------------------------------

    reportProgress(15, QStringLiteral("Detecting mode..."));

    bool isPreserveMode = false;

    if (hasGuitar) {
        // (A) If guitar program_changes exist, the file is already configured
        if (!guitarChToProgram.isEmpty()) {
            isPreserveMode = true;
        }

        // (B) Fallback: check for multi-channel guitar notes
        if (!isPreserveMode) {
            QSet<int> knownGuitarChs;
            for (int t = 0; t < trackCount; t++) {
                if (isGuitar(baseNames[t])) {
                    int aCh = file->track(t)->assignedChannel();
                    if (aCh >= 0) knownGuitarChs.insert(aCh);
                }
            }

            for (int t = 0; t < trackCount; t++) {
                if (!isGuitar(baseNames[t])) continue;
                MidiTrack *track = file->track(t);
                QSet<int> chsWithNotes;
                for (int ch : knownGuitarChs) {
                    // The helper autoTier() uses as well, so the two detections
                    // cannot drift - and an assignedChannel above 15 no longer
                    // reads MidiFile::channel()'s fallback channel (R231-17).
                    if (trackPlaysOnChannel(file, track, ch))
                        chsWithNotes.insert(ch);
                }
                if (chsWithNotes.size() > 1) {
                    isPreserveMode = true;
                    break;
                }
            }
        }
    }

    // Override with forced tier if specified by the user
    if (forcedTier == 2) isPreserveMode = false;
    else if (forcedTier == 3) isPreserveMode = true;

    // Gate rule (c): Rebuild renumbers tracks onto channels by index, and a
    // file with more than 16 note-carrying tracks would have the rest merged
    // onto channel 15. Still read-only at this point; Preserve is unaffected.
    if (!isPreserveMode && !gate["tier2Eligible"].toBool()) {
        result["success"] = false;
        result["error"] = gate["tier2Reason"].toString();
        result["eligibility"] = gate;
        result["tier"] = 2;
        return result;
    }

    // -----------------------------------------------------------------------
    // Build channel assignment map + guitarChannelMap
    // -----------------------------------------------------------------------

    reportProgress(20, QStringLiteral("Building channel map..."));

    QVector<int> channelFor(trackCount, -1);
    QSet<int> usedChannels;
    QHash<QString, int> guitarChannelMap;
    // Tier 3: guitar tracks whose channel is no guitar channel - no notes of
    // theirs there, or another instrument plays there too (CF-06)
    QSet<int> idleGuitarTracks;
    QJsonArray guitarProgramFallbackLog;    // Tier 3: channels that took the program from the track name

    if (isPreserveMode) {
        // TIER 3 -- minimal-invasive: assignedChannel() is the ONLY source of truth.
        // Every guitar track keeps its own assigned channel — even if two tracks
        // share the same variant name (e.g. two "PowerChords" on CH3 and CH4).
        // No duplicate merging, no event migration.
        if (hasGuitar) {
            for (int t = 0; t < trackCount; t++) {
                if (!isGuitar(baseNames[t])) continue;

                MidiTrack *track = file->track(t);
                int aCh = track->assignedChannel();
                if (aCh < 0 || aCh > 15) aCh = qMin(t, 15);
                channelFor[t] = aCh;

                // v2.4.0 (review F065): a guitar track on a channel that has
                // no guitar program at tick 0 - typically a track the user just
                // renamed from a melodic instrument (Viola -> Overdriven) - used
                // to lose the channel's only program change in CLEAN and get
                // nothing back, so the editor played it as program 0 (piano).
                //   * The track plays on that channel: the channel takes the
                //     program of the track's own variant.
                //   * The track is idle there: the channel is not a guitar
                //     channel at all - leave it and its owner's program alone.
                // Configured files carry a 27-31 program on every guitar
                // channel, so neither branch fires for them and the frozen
                // Tier-3 result stays byte-identical.
                if (!guitarChToProgram.contains(aCh)) {
                    if (!trackPlaysOnChannel(file, track, aCh)) {
                        idleGuitarTracks.insert(t);
                        continue;
                    }
                    //   * Another instrument plays there too (a guitar track
                    //     created while the Flute's channel was the edit
                    //     channel): the channel stays the Flute's - taking it
                    //     over replaced the Flute's sound and dropped its
                    //     program changes (fixer review CF-06). The guitar
                    //     notes there are reported with the result instead.
                    if (otherInstrumentOnChannel(file, aCh)) {
                        idleGuitarTracks.insert(t);
                        continue;
                    }
                    const int prog = programNumber(baseNames[t]);
                    if (prog >= 0) {
                        guitarChToProgram[aCh] = prog;
                        QJsonObject entry;
                        entry["track"]     = t;
                        entry["trackName"] = track->name();
                        entry["channel"]   = aCh;
                        entry["program"]   = prog;
                        guitarProgramFallbackLog.append(entry);
                    }
                }
                usedChannels.insert(aCh);

                // Register first-seen variant (for chToVariant fallback)
                if (!guitarChannelMap.contains(baseNames[t]))
                    guitarChannelMap[baseNames[t]] = aCh;
            }
        }

        // channelFor for non-guitar tracks: use assignedChannel() - for
        // percussion too. Preserve moves no notes, so re-pointing a drum track
        // at channel 9 while its notes stayed elsewhere only made the track
        // list and the edit channel disagree with them (fixer review CF-10).
        // Channel 9 remains the fallback of a drum track without a channel.
        for (int t = 0; t < trackCount; t++) {
            if (isGuitar(baseNames[t])) continue;
            MidiTrack *track = file->track(t);
            int aCh = track->assignedChannel();
            if (aCh < 0 || aCh > 15) aCh = isPercussion(baseNames[t]) ? 9 : qMin(t, 15);
            channelFor[t] = aCh;
            usedChannels.insert(aCh);
        }
    } else {
        // TIER 2 - assign channels by track index (fresh start)
        // Duplicate guitar variants share the channel of the first occurrence.
        QVector<bool> drumRouted(trackCount, false); // sent to CH9 as percussion
        for (int t = 0; t < trackCount; t++) {
            if (isPercussion(baseNames[t])) {
                channelFor[t] = 9;
                drumRouted[t] = true;
            } else if (hasGuitar && isGuitar(baseNames[t])
                       && guitarChannelMap.contains(baseNames[t])) {
                // Duplicate guitar variant: reuse first occurrence's channel
                channelFor[t] = guitarChannelMap[baseNames[t]];
            } else {
                // 1.6.1 (upstream a35f1ee): a track that we couldn't match
                // against any FFXIV instrument and that isn't a guitar
                // variant might still be a drum track that just doesn't
                // carry an FFXIV-canonical name (e.g. "Drums", "Perc",
                // unnamed). If its NoteOn events are predominantly on
                // channel 9, keep it on 9 - otherwise the GM percussion
                // mapping is destroyed by the numeric assignment below.
                bool unmatched = !isGuitar(baseNames[t])
                                 && programNumber(baseNames[t]) < 0;
                if (unmatched
                    && dominantNoteChannel(file, file->track(t)) == 9) {
                    channelFor[t] = 9;
                    drumRouted[t] = true;
                } else {
                    int ch = t;
                    if (ch > 15) ch = 15;
                    channelFor[t] = ch;
                    // Register first-seen guitar variant
                    if (hasGuitar && isGuitar(baseNames[t]))
                        guitarChannelMap[baseNames[t]] = ch;
                }
            }
            usedChannels.insert(channelFor[t]);
        }

        // Channel 9 is the percussion channel (fixer review CF-05). The index
        // rule put a melodic or guitar track at index 9 there: next to drum
        // tracks the channel got two different programs at tick 0, and in the
        // editor the track sounded like the drums. It takes the first free
        // channel instead - and with it the later tracks of its guitar variant.
        // With every other channel taken it stays on 9, as before.
        if (trackCount > 9 && !drumRouted[9] && channelFor[9] == 9
            && programNumber(baseNames[9]) >= 0) {
            int freeCh = -1;
            for (int ch = 0; ch <= 15; ch++) {
                if (ch != 9 && !usedChannels.contains(ch)) {
                    freeCh = ch;
                    break;
                }
            }
            if (freeCh >= 0) {
                for (int t = 0; t < trackCount; t++) {
                    if (channelFor[t] == 9 && !drumRouted[t])
                        channelFor[t] = freeCh;
                }
                for (auto it = guitarChannelMap.begin(); it != guitarChannelMap.end(); ++it) {
                    if (it.value() == 9) it.value() = freeCh;
                }
                usedChannels.insert(freeCh);
            }
        }
    }

    // Reserve free channels for missing guitar variants (Tier 2 only)
    // Tier 3 must NOT allocate new channels — preserve existing assignments.
    // Never channel 9, the percussion channel (fixer review CF-05).
    if (hasGuitar && !isPreserveMode) {
        auto nextFreeChannel = [&]() -> int {
            for (int ch = 0; ch <= 15; ch++) {
                if (ch != 9 && !usedChannels.contains(ch))
                    return ch;
            }
            return -1;
        };
        for (const QString &variant : allGuitarVariants) {
            if (!guitarChannelMap.contains(variant)) {
                int ch = nextFreeChannel();
                if (ch >= 0) {
                    guitarChannelMap[variant] = ch;
                    usedChannels.insert(ch);
                }
            }
        }
    }

    // Build guitar channel sets
    QSet<int> allGuitarChs;
    QSet<int> guitarChsFromTracks;  // only channels with actual guitar tracks
    if (hasGuitar) {
        // Include ALL guitar track channels (not just first occurrence per variant)
        for (int t = 0; t < trackCount; t++) {
            if (isGuitar(baseNames[t]) && channelFor[t] >= 0
                && !idleGuitarTracks.contains(t)) {
                allGuitarChs.insert(channelFor[t]);
                guitarChsFromTracks.insert(channelFor[t]);
            }
        }
        // Also include guitarChannelMap entries (covers Tier 2 reserved variants)
        for (auto it = guitarChannelMap.begin(); it != guitarChannelMap.end(); ++it) {
            allGuitarChs.insert(it.value());
            guitarChsFromTracks.insert(it.value());
        }
        // Tier 3: also include reserved channels from previous Tier 2 runs.
        // These channels still have valid guitar PCs (each maps to exactly
        // one guitar program) and may contain notes the user placed manually.
        if (isPreserveMode) {
            for (auto it = guitarChToProgram.begin(); it != guitarChToProgram.end(); ++it)
                allGuitarChs.insert(it.key());
        }
    }

    // Deterministic iteration order for the two passes below. QSet<int>
    // iterates in QHash order, which depends on the per-process hash seed, so
    // a tie (two guitar channels whose earliest NoteOn share a tick) would
    // resolve differently across app restarts and rename the track / emit the
    // switch program changes differently for the same file. Lowest channel
    // number wins, in every run.
    QList<int> sortedGuitarChs = allGuitarChs.values();
    std::sort(sortedGuitarChs.begin(), sortedGuitarChs.end());

    // -----------------------------------------------------------------------
    // 1b. RESYNC PLAN (Tier 3 opt-in, v2.0) — non-guitar channels whose
    //     tick-0 program no longer matches the owning track's name.
    //
    //     Read-only detection, computed BEFORE any mutation. Rules (from the
    //     v2.0 pre-build verification):
    //       * Detect by scanning the ACTUAL tick-0 ProgChangeEvents — never
    //         progAtTick(0), which returns 0 both for "no PC at all" and for
    //         a genuine Piano (program 0).
    //       * "No tick-0 PC" and "more than one tick-0 PC" (stacked
    //         duplicates from an old Tier 2 run) both count as needs-fix;
    //         the fix collapses to exactly ONE PC per channel.
    //       * Skip guitar tracks (guitar logic owns those channels), skip
    //         percussion by NAME (Bass Drum/Snare Drum/Cymbal/Bongo live on
    //         CH9 under per-hit name-keyed injection) — Timpani is tonal and
    //         IS a resync target. Non-FFXIV track names are skipped, so their
    //         channels stay untouched.
    //       * When several tracks share one channel, the channel's program
    //         follows the track with the EARLIEST first NoteOn on that
    //         channel (tie-break: lowest track index) — deterministic across
    //         repeated runs.
    // -----------------------------------------------------------------------

    struct ResyncTarget { int program; MidiTrack *ownerTrack; };
    QHash<int, ResyncTarget> resyncPlan; // channel -> target program/owner
    QJsonArray resyncLog;

    if (isPreserveMode && resyncNonGuitar) {
        QHash<int, QList<int>> tracksByChannel; // channel -> candidate tracks
        for (int t = 0; t < trackCount; t++) {
            if (isGuitar(baseNames[t])) continue;
            if (isPercussion(baseNames[t])) continue;
            int prog = programNumber(baseNames[t]);
            if (prog < 0) continue;
            int ch = channelFor[t];
            if (ch < 0 || ch > 15) continue;
            if (ch == 9) continue; // GM drum channel: owned by per-hit
                                    // name-keyed injection - a melodic track
                                    // assigned to CH9 must never strip the
                                    // percussion PCs stacked there
            if (allGuitarChs.contains(ch)) continue;
            tracksByChannel[ch].append(t);
        }

        for (auto it = tracksByChannel.constBegin(); it != tracksByChannel.constEnd(); ++it) {
            const int ch = it.key();
            MidiChannel *channel = file->channel(ch);
            if (!channel) continue;

            // Owner = candidate with the earliest first NoteOn on THIS channel;
            // candidates without notes here lose. When NO candidate actually
            // plays on the channel, the channel is NOT ours to resync - an
            // empty/idle FFXIV-named track with a stale assignedChannel must
            // not clobber another (possibly non-FFXIV) track's deliberate
            // tick-0 program change.
            int ownerT = -1;
            int bestTick = INT_MAX;
            for (int t : it.value()) {
                MidiTrack *track = file->track(t);
                QMultiMap<int, MidiEvent *> *map = channel->eventMap();
                for (auto eit = map->begin(); eit != map->end(); ++eit) {
                    if (eit.value()->track() != track) continue;
                    if (!dynamic_cast<NoteOnEvent *>(eit.value())) continue;
                    if (eit.key() < bestTick) {
                        bestTick = eit.key();
                        ownerT = t;
                    }
                    break; // map sorted by tick: first hit = this track's earliest
                }
            }
            if (ownerT < 0) continue; // no candidate plays here -> untouched
            const int target = programNumber(baseNames[ownerT]);
            if (target < 0) continue;

            // Explicit tick-0 PC scan (presence + program).
            int tickZeroPcCount = 0;
            int currentProgram = -1;
            const QList<MidiEvent *> atZero = channel->eventMap()->values(0);
            for (MidiEvent *ev : atZero) {
                if (ProgChangeEvent *pc = dynamic_cast<ProgChangeEvent *>(ev)) {
                    tickZeroPcCount++;
                    // values() lists the most recently inserted first - the
                    // program in effect (fixer review CF-03)
                    if (tickZeroPcCount == 1)
                        currentProgram = pc->program();
                }
            }

            const bool needsFix = (tickZeroPcCount == 0)
                               || (tickZeroPcCount > 1)
                               || (currentProgram != target);
            if (!needsFix) continue; // already clean -> idempotent no-op

            resyncPlan.insert(ch, {target, file->track(ownerT)});
            QJsonObject entry;
            entry["channel"]    = ch;
            entry["track"]      = ownerT;
            entry["trackName"]  = file->track(ownerT)->name();
            entry["oldProgram"] = (tickZeroPcCount > 0) ? currentProgram : -1;
            entry["newProgram"] = target;
            resyncLog.append(entry);
        }
    }

    // -----------------------------------------------------------------------
    // 2. CLEAN - remove program_change events
    //    Tier 2: remove ALL PCs (full rebuild)
    //    Tier 3: only remove PCs on guitar channels (non-guitar untouched)
    // -----------------------------------------------------------------------

    reportProgress(35, QStringLiteral("Removing old program changes..."));

    // -----------------------------------------------------------------------
    // BULK-OP UNDO STRATEGY - snapshot once, mutate fast, commit at end.
    //
    //   Background (perf bug fixed 2026-04-21):
    //   The default Protocol path of every mutating MidiChannel/MidiEvent
    //   call (removeEvent, insertEvent, moveToChannel, setVelocity) does a
    //   full deep copy() of the affected event/channel and pushes a
    //   ProtocolItem onto the open undo action. On a 20-track / >100k-event
    //   FFXIV file Tier 2 used to allocate one clone per touched event in
    //   each of CLEAN, MIGRATE, SWITCH and VELOCITY - easily 64 GB peak RSS
    //   and several minutes to finish.
    //
    //   Fix: take a single MidiChannel::copy() per channel and one
    //   MidiTrack::copy() per track BEFORE any mutation, then call the
    //   per-event APIs with toProtocol=false. After all phases finish we
    //   register one ProtocolItem per snapshot - so undo restores the full
    //   pre-fix state of every channel and track in one shot. RAM cost
    //   collapses from O(events × mutations) to O(tracks + 16).
    // -----------------------------------------------------------------------

    QVector<ProtocolEntry *> trackSnapshots(trackCount, nullptr);
    QVector<ProtocolEntry *> channelSnapshots(16, nullptr);
    for (int t = 0; t < trackCount; t++) {
        MidiTrack *track = file->track(t);
        if (track) trackSnapshots[t] = track->copy();
    }
    for (int ch = 0; ch < 16; ch++) {
        MidiChannel *channel = file->channel(ch);
        if (channel) channelSnapshots[ch] = channel->copy();
    }

    int removedPcCount = 0;
    for (int ch = 0; ch < 16; ch++) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        if (isPreserveMode && !guitarChsFromTracks.contains(ch)) {
            // Tier 3: non-guitar channels keep their PCs (reserved guitar
            // channels keep their Tier 2 PCs at tick 0) — EXCEPT an opt-in
            // resync target, where ONLY the stale tick-0 PC(s) are removed.
            // Mid-song program changes the user placed deliberately survive;
            // the full-channel removal below stays guitar-only.
            if (resyncPlan.contains(ch)) {
                QList<MidiEvent *> toRemove;
                const QList<MidiEvent *> atZero = channel->eventMap()->values(0);
                for (MidiEvent *ev : atZero) {
                    if (dynamic_cast<ProgChangeEvent *>(ev))
                        toRemove.append(ev);
                }
                for (MidiEvent *ev : toRemove) {
                    channel->removeEvent(ev, false);
                }
                removedPcCount += toRemove.size();
            } else if (allGuitarChs.contains(ch)) {
                // A reserved guitar channel keeps its program at tick 0, but
                // its guitar switch program changes later in the song are
                // written anew by SWITCH below, as on every guitar channel.
                // Kept, they piled up with every run, and once the channel
                // got another variant at tick 0 the old switches still played
                // the old one (fixer review CF-02).
                QList<MidiEvent *> toRemove;
                QMultiMap<int, MidiEvent *> *map = channel->eventMap();
                for (auto it = map->upperBound(0); it != map->end(); ++it) {
                    ProgChangeEvent *pc = dynamic_cast<ProgChangeEvent *>(it.value());
                    if (pc && pc->program() >= 27 && pc->program() <= 31)
                        toRemove.append(pc);
                }
                for (MidiEvent *ev : toRemove) {
                    channel->removeEvent(ev, false);
                }
                removedPcCount += toRemove.size();
            }
            continue;
        }
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        QList<MidiEvent *> toRemove;
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (dynamic_cast<ProgChangeEvent *>(it.value()))
                toRemove.append(it.value());
        }
        for (MidiEvent *ev : toRemove) {
            channel->removeEvent(ev, false);
        }
        removedPcCount += toRemove.size();
    }

    // -----------------------------------------------------------------------
    // 2b. CLEAN - remove non-essential events (Tier 2 only)
    //     FFXIV doesn't use CC, PitchBend, etc.  Keep Text (lyrics) and notes.
    // -----------------------------------------------------------------------

    int removedExtraCount = 0;
    if (!isPreserveMode) {
        for (int ch = 0; ch < 16; ch++) {
            MidiChannel *channel = file->channel(ch);
            if (!channel) continue;
            QMultiMap<int, MidiEvent *> *map = channel->eventMap();
            QList<MidiEvent *> toRemoveExtra;
            for (auto it = map->begin(); it != map->end(); ++it) {
                MidiEvent *ev = it.value();
                if (dynamic_cast<NoteOnEvent *>(ev))     continue;
                if (dynamic_cast<OffEvent *>(ev))        continue;
                if (dynamic_cast<TextEvent *>(ev))       continue;
                if (dynamic_cast<ProgChangeEvent *>(ev)) continue;
                toRemoveExtra.append(ev);
            }
            for (MidiEvent *ev : toRemoveExtra) {
                channel->removeEvent(ev, false);
            }
            removedExtraCount += toRemoveExtra.size();
        }
    }

    // -----------------------------------------------------------------------
    // 3. MIGRATE - move events to correct channels (Tier 2 only)
    //    Tier 3 (Preserve) skips this - channels are already established
    // -----------------------------------------------------------------------

    reportProgress(50, QStringLiteral("Migrating events..."));

    QJsonArray renameLog;

    if (!isPreserveMode) {
        // Tier 2: full migration
        for (int t = 0; t < trackCount; t++) {
            int targetCh = channelFor[t];
            MidiTrack *track = file->track(t);

            struct EventInfo { MidiEvent *ev; int currentCh; };
            QList<EventInfo> trackEvents;

            for (int ch = 0; ch < 16; ch++) {
                MidiChannel *channel = file->channel(ch);
                if (!channel) continue;
                QMultiMap<int, MidiEvent *> *map = channel->eventMap();
                for (auto it = map->begin(); it != map->end(); ++it) {
                    MidiEvent *ev = it.value();
                    if (ev->track() != track) continue;
                    if (dynamic_cast<ProgChangeEvent *>(ev)) continue;
                    if (dynamic_cast<OffEvent *>(ev)) continue;
                    trackEvents.append({ev, ch});
                }
            }

            for (const auto &info : trackEvents) {
                if (info.currentCh == targetCh) continue;
                // WHY: the bulk channel snapshot is a clone of the POINTER map,
                // so it restores channel membership but not the event's own
                // numChannel field. Without this small per-event ProtocolItem
                // undo leaves the event in the old map while it still reports
                // the new channel (wrong save output, dead delete, duplicated
                // map entry on the next time edit). The paired OffEvent moves
                // with its OnEvent and needs the same item.
                OffEvent *off = nullptr;
                ProtocolEntry *beforeOff = nullptr;
                if (OnEvent *on = dynamic_cast<OnEvent *>(info.ev)) {
                    off = on->offEvent();
                    if (off) beforeOff = off->copy();
                }
                ProtocolEntry *beforeEv = info.ev->copy();
                info.ev->moveToChannel(targetCh, false);
                info.ev->protocol(beforeEv, info.ev);
                if (off) off->protocol(beforeOff, off);
            }

            track->assignChannel(targetCh);
        }
    } else {
        // Tier 3: preserve — NO event migration, NO channel changes.
        // Only rename tracks and assign channels.

        // Tier 3: rename guitar tracks if all notes sit on a single
        // channel that belongs to a different variant.
        {
            QHash<int, QString> chToVariant;
            // PRIMARY: actual channel programs from guitarChToProgram
            // (these are the real programs on each channel — source of truth)
            static const QHash<int, QString> progToVariant = {
                {27, QStringLiteral("ElectricGuitarClean")},
                {28, QStringLiteral("ElectricGuitarMuted")},
                {29, QStringLiteral("ElectricGuitarOverdriven")},
                {30, QStringLiteral("ElectricGuitarPowerChords")},
                {31, QStringLiteral("ElectricGuitarSpecial")}
            };
            for (auto it = guitarChToProgram.begin(); it != guitarChToProgram.end(); ++it)
                chToVariant[it.key()] = progToVariant.value(it.value());
            // SECONDARY: track→channel from guitarChannelMap (only if no PC data)
            for (auto it = guitarChannelMap.begin(); it != guitarChannelMap.end(); ++it) {
                if (!chToVariant.contains(it.value()))
                    chToVariant[it.value()] = it.key();
            }

            for (int t = 0; t < trackCount; t++) {
                MidiTrack *track = file->track(t);
                track->assignChannel(channelFor[t]);

                if (!isGuitar(baseNames[t])) continue;

                // Find the channel of the CHRONOLOGICALLY FIRST NoteOn for
                // this track across all guitar channels. For single-channel
                // guitar tracks this is just "their" channel; for switching
                // tracks this is whichever variant the track starts on —
                // which is what the track name should reflect per spec.
                // (Supersedes the v1.3.0 "skip rename for switching tracks"
                // rule — Bug #4 revisit 2026-04-17.)
                int firstCh = -1;
                int firstTick = INT_MAX;
                for (int ch : sortedGuitarChs) {
                    MidiChannel *channel = file->channel(ch);
                    if (!channel) continue;
                    QMultiMap<int, MidiEvent *> *map = channel->eventMap();
                    for (auto eit = map->begin(); eit != map->end(); ++eit) {
                        if (eit.value()->track() != track) continue;
                        if (!dynamic_cast<NoteOnEvent *>(eit.value())) continue;
                        if (eit.key() < firstTick) {
                            firstTick = eit.key();
                            firstCh = ch;
                        }
                        break; // eventMap is sorted by tick; first hit per ch is the earliest on that ch
                    }
                }

                if (firstCh < 0) continue; // no notes on any guitar channel

                if (chToVariant.contains(firstCh)) {
                    QString newVariant = chToVariant[firstCh];
                    if (newVariant != baseNames[t]) {
                        // The octave suffix ("+1") stays: it is the in-game
                        // octave setting, and a renamed track played an octave
                        // off without it (fixer review CF-01).
                        const QString newName = newVariant + octaveSuffix(track->name());
                        QJsonObject entry;
                        entry["track"]   = t;
                        entry["oldName"] = track->name();
                        entry["newName"] = newName;
                        renameLog.append(entry);
                        track->setName(newName);
                        baseNames[t] = newVariant;
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // 4. PROGRAM - insert program_change at tick 0
    //    Tier 2: ALL channels (guitar + non-guitar) on all tracks
    //    Tier 3: only guitar channels (non-guitar already have correct PCs)
    // -----------------------------------------------------------------------

    reportProgress(75, QStringLiteral("Inserting program changes..."));

    struct ChannelProgram { int channel; int program; };
    QList<ChannelProgram> channelPrograms;

    // Non-guitar tracks: only in Tier 2 (Tier 3 keeps existing non-guitar PCs)
    if (!isPreserveMode) {
        for (int t = 0; t < trackCount; t++) {
            if (isGuitar(baseNames[t])) continue;
            int prog = programNumber(baseNames[t]);
            if (prog >= 0)
                channelPrograms.append({channelFor[t], prog});
        }
    }

    // All guitar channels
    if (hasGuitar) {
        if (isPreserveMode) {
            // Tier 3: re-insert PCs using the ACTUAL channel programs.
            // Use guitarChsFromTracks (all channels with guitar tracks)
            // and look up the program from guitarChToProgram.
            for (int ch : guitarChsFromTracks) {
                if (guitarChToProgram.contains(ch))
                    channelPrograms.append({ch, guitarChToProgram[ch]});
            }
        } else {
            // Tier 2: from guitarChannelMap (includes reserved variants)
            for (auto it = guitarChannelMap.begin(); it != guitarChannelMap.end(); ++it) {
                int prog = programNumber(it.key());
                if (prog >= 0)
                    channelPrograms.append({it.value(), prog});
            }
        }
    }

    // Insert on every track
    for (int t = 0; t < trackCount; t++) {
        MidiTrack *track = file->track(t);
        for (const auto &cp : channelPrograms) {
            auto *pc = new ProgChangeEvent(cp.channel, cp.program, track);
            file->channel(cp.channel)->insertEvent(pc, 0, false);
        }
    }

    // Tier-3 opt-in non-guitar resync: exactly ONE tick-0 PC per channel,
    // attributed to the channel's owning track. Deliberately NOT routed
    // through the per-track loop above — that inserts one PC per track and
    // would stack duplicates on every run, breaking the "second consecutive
    // run is a no-op" guarantee the daily Tier-3 workflow depends on.
    if (isPreserveMode && resyncNonGuitar) {
        for (auto it = resyncPlan.constBegin(); it != resyncPlan.constEnd(); ++it) {
            auto *pc = new ProgChangeEvent(it.key(), it.value().program,
                                           it.value().ownerTrack);
            file->channel(it.key())->insertEvent(pc, 0, false);
        }
    }

    // -----------------------------------------------------------------------
    // 4b. SWITCH - insert program_change at guitar channel switch points
    // -----------------------------------------------------------------------

    reportProgress(90, QStringLiteral("Processing guitar switches..."));

    int switchCount = 0;
    if (hasGuitar) {
        QHash<int, QString> chToVariant;
        // PRIMARY: actual channel programs from guitarChToProgram
        static const QHash<int, QString> progToVariant2 = {
            {27, QStringLiteral("ElectricGuitarClean")},
            {28, QStringLiteral("ElectricGuitarMuted")},
            {29, QStringLiteral("ElectricGuitarOverdriven")},
            {30, QStringLiteral("ElectricGuitarPowerChords")},
            {31, QStringLiteral("ElectricGuitarSpecial")}
        };
        for (auto it = guitarChToProgram.begin(); it != guitarChToProgram.end(); ++it)
            chToVariant[it.key()] = progToVariant2.value(it.value());
        // SECONDARY: track→channel from guitarChannelMap (only if no PC data)
        for (auto it = guitarChannelMap.begin(); it != guitarChannelMap.end(); ++it) {
            if (!chToVariant.contains(it.value()))
                chToVariant[it.value()] = it.key();
        }

        for (int t = 0; t < trackCount; t++) {
            if (!isGuitar(baseNames[t])) continue;
            MidiTrack *track = file->track(t);

            struct NoteInfo { int tick; int channel; };
            QList<NoteInfo> notes;

            for (int ch : sortedGuitarChs) {
                MidiChannel *channel = file->channel(ch);
                if (!channel) continue;
                QMultiMap<int, MidiEvent *> *map = channel->eventMap();
                for (auto eit = map->begin(); eit != map->end(); ++eit) {
                    if (eit.value()->track() != track) continue;
                    if (dynamic_cast<NoteOnEvent *>(eit.value()))
                        notes.append({eit.key(), ch});
                }
            }

            // (tick, channel) is a total order: comparing the tick alone
            // leaves equal-tick notes from different channels in an
            // unspecified order (std::sort is not stable), which changes the
            // switch program changes emitted below from run to run.
            std::sort(notes.begin(), notes.end(),
                      [](const NoteInfo &a, const NoteInfo &b) {
                          if (a.tick != b.tick) return a.tick < b.tick;
                          return a.channel < b.channel;
                      });

            int lastCh = -1;
            for (const auto &n : notes) {
                if (n.channel != lastCh) {
                    if (lastCh != -1 && n.tick > 0) {
                        QString variant = chToVariant.value(n.channel);
                        int prog = programNumber(variant);
                        if (prog >= 0) {
                            auto *pc = new ProgChangeEvent(n.channel, prog, track);
                            file->channel(n.channel)->insertEvent(pc, n.tick, false);
                            switchCount++;
                        }
                    }
                    lastCh = n.channel;
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // 5. VELOCITY - normalise all NoteOn velocities to 127 (max)
    //    FFXIV performance has no dynamics; uniform velocity improves playback.
    // -----------------------------------------------------------------------

    reportProgress(95, QStringLiteral("Normalizing velocity..."));

    int velocityChangedCount = 0;
    for (int ch = 0; ch < 16; ch++) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            NoteOnEvent *noteOn = dynamic_cast<NoteOnEvent *>(it.value());
            if (noteOn && noteOn->velocity() > 0 && noteOn->velocity() != 127) {
                // WHY: the channel snapshot clones only the pointer map and
                // therefore shares these very NoteOnEvents - mutating
                // _velocity mutates the snapshot too. A small per-event
                // ProtocolItem is what actually makes the change undoable.
                ProtocolEntry *before = noteOn->copy();
                noteOn->setVelocity(127, false);
                noteOn->protocol(before, noteOn);
                velocityChangedCount++;
            }
        }
    }

    // -----------------------------------------------------------------------
    // 6. REPORT - with debug info
    // -----------------------------------------------------------------------

    reportProgress(100, QStringLiteral("Done!"));

    // Commit the bulk-op snapshots taken before phase 2. One ProtocolItem
    // per touched track + one per touched channel - the entire edit becomes
    // a single coarse-grained undo step regardless of how many events were
    // mutated above.
    for (int t = 0; t < trackCount; t++) {
        if (trackSnapshots[t])
            file->track(t)->protocol(trackSnapshots[t], file->track(t));
    }
    for (int ch = 0; ch < 16; ch++) {
        if (channelSnapshots[ch])
            file->channel(ch)->protocol(channelSnapshots[ch], file->channel(ch));
    }

    int tier = isPreserveMode ? 3 : 2;

    QJsonArray channelMapArr;
    for (int t = 0; t < trackCount; t++) {
        QJsonObject entry;
        entry["track"] = t;
        entry["name"] = file->track(t)->name();
        entry["channel"] = channelFor[t];
        entry["program"] = programNumber(baseNames[t]);
        channelMapArr.append(entry);
    }

    if (hasGuitar) {
        QJsonArray extraGuitarArr;
        for (const QString &variant : allGuitarVariants) {
            if (!guitarVariantsPresent.contains(variant) && guitarChannelMap.contains(variant)) {
                QJsonObject entry;
                entry["variant"] = variant;
                entry["channel"] = guitarChannelMap[variant];
                entry["program"] = programNumber(variant);
                entry["reserved"] = true;
                extraGuitarArr.append(entry);
            }
        }
        if (!extraGuitarArr.isEmpty())
            result["reservedGuitarChannels"] = extraGuitarArr;
    }

    if (!renameLog.isEmpty())
        result["trackRenames"] = renameLog;

    result["resyncedNonGuitarChannels"] = resyncPlan.size();
    if (!resyncLog.isEmpty())
        result["nonGuitarResyncs"] = resyncLog;

    result["guitarProgramFallbacks"] = guitarProgramFallbackLog.size();
    if (!guitarProgramFallbackLog.isEmpty())
        result["guitarProgramFallbackLog"] = guitarProgramFallbackLog;

    // Guitar notes on a channel that is no guitar channel (the Flute's, the
    // drums'): SWITCH only follows the guitar channels, so no program change
    // was written for them - listed so the user can move them (fixer review
    // CF-09; the shared channel CF-06 left alone shows up here as well).
    // Preserve only: Rebuild moves every note of a track onto its channel.
    if (isPreserveMode && hasGuitar) {
        QHash<MidiTrack *, int> guitarTrackIndex;
        for (int t = 0; t < trackCount; t++) {
            if (isGuitar(baseNames[t]))
                guitarTrackIndex.insert(file->track(t), t);
        }
        QMap<QPair<int, int>, int> offChannelNotes; // (track, channel) -> notes
        for (int ch = 0; ch < 16; ch++) {
            if (allGuitarChs.contains(ch)) continue;
            MidiChannel *channel = file->channel(ch);
            if (!channel) continue;
            QMultiMap<int, MidiEvent *> *map = channel->eventMap();
            for (auto it = map->begin(); it != map->end(); ++it) {
                if (!dynamic_cast<NoteOnEvent *>(it.value())) continue;
                const auto idx = guitarTrackIndex.constFind(it.value()->track());
                if (idx != guitarTrackIndex.constEnd())
                    offChannelNotes[qMakePair(idx.value(), ch)]++;
            }
        }
        QJsonArray offChannelArr;
        for (auto it = offChannelNotes.constBegin(); it != offChannelNotes.constEnd(); ++it) {
            QJsonObject entry;
            entry["track"]     = it.key().first;
            entry["trackName"] = file->track(it.key().first)->name();
            entry["channel"]   = it.key().second;
            entry["notes"]     = it.value();
            if (MidiTrack *other = otherInstrumentOnChannel(file, it.key().second))
                entry["sharedWith"] = other->name();
            offChannelArr.append(entry);
        }
        if (!offChannelArr.isEmpty())
            result["guitarNotesOnOtherChannels"] = offChannelArr;
    }

    result["success"] = true;
    result["tier"] = tier;
    result["tierDescription"] = (tier == 2)
        ? QStringLiteral("Rebuild (Full Reassignment)")
        : QStringLiteral("Preserve (Minimal Changes)");
    result["channelMap"] = channelMapArr;
    result["guitarSwitchProgramChanges"] = switchCount;
    result["removedProgramChanges"] = removedPcCount;
    result["removedExtraEvents"]   = removedExtraCount;
    result["velocityNormalized"] = velocityChangedCount;
    result["trackCount"] = trackCount;
    return result;
}
