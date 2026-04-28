#include "FFXIVChannelFixer.h"

#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/ProgChangeEvent.h"
#include "../MidiEvent/TextEvent.h"
#include "../protocol/ProtocolEntry.h"

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

int FFXIVChannelFixer::programNumber(const QString &baseName) {
    // NOTE: program numbers must match the actual presets in the FFXIV
    // SoundFont (FF14-c3c6-fixed.sf2). Mismatches cause silent fallback to
    // bank 0 / prog 0 (= Piano). Verified 2026-04-28 against phdr chunk.
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
    return map.value(baseName, -1);
}

// ---------------------------------------------------------------------------
// analyzeFile  â€” read-only scan for the tier selection dialog
// ---------------------------------------------------------------------------

QJsonObject FFXIVChannelFixer::analyzeFile(MidiFile *file) {
    QJsonObject result;
    if (!file || file->numTracks() == 0) {
        result["valid"] = false;
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
    bool hasGuitarPCs = false;
    for (int ch = 0; ch < 16; ch++) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            auto *pc = dynamic_cast<ProgChangeEvent *>(it.value());
            if (!pc) continue;
            totalProgramChanges++;
            if (pc->program() >= 27 && pc->program() <= 31)
                hasGuitarPCs = true;
        }
    }

    // Auto-detect tier
    int autoTier = (ffxivTrackCount > 0) ? 2 : 1;
    if (autoTier == 2 && hasGuitar && hasGuitarPCs)
        autoTier = 3;

    result["valid"]               = (ffxivTrackCount > 0);
    result["trackCount"]          = trackCount;
    result["ffxivTrackCount"]     = ffxivTrackCount;
    result["hasGuitar"]           = hasGuitar;
    result["totalProgramChanges"] = totalProgramChanges;
    result["autoDetectedTier"]    = autoTier;
    result["guitarVariants"]      = QJsonArray::fromStringList(guitarVariants);
    result["percussionTracks"]    = QJsonArray::fromStringList(percussionTracks);
    result["melodicTracks"]       = QJsonArray::fromStringList(melodicTracks);
    return result;
}

// ---------------------------------------------------------------------------
// fixChannels  â€” the main entry point (3-tier smart detection)
// ---------------------------------------------------------------------------

QJsonObject FFXIVChannelFixer::fixChannels(MidiFile *file, int forcedTier,
                                           ProgressCallback progress) {
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
    // 0. PRE-SCAN â€” classify tracks, count FFXIV matches, scan guitar progs
    // -----------------------------------------------------------------------

    reportProgress(5, QStringLiteral("Scanning tracks..."));

    QVector<QString> baseNames(trackCount);
    int ffxivTrackCount = 0;
    bool hasGuitar = false;
    QSet<QString> guitarVariantsPresent;

    for (int t = 0; t < trackCount; t++) {
        QString base = stripSuffix(file->track(t)->name());
        baseNames[t] = base;
        if (programNumber(base) >= 0)
            ffxivTrackCount++;
        if (isGuitar(base)) {
            hasGuitar = true;
            guitarVariantsPresent.insert(base);
        }
    }

    // TIER 1 â€” Not an FFXIV MIDI
    if (ffxivTrackCount == 0) {
        result["success"] = false;
        result["error"] = QStringLiteral("No FFXIV instrument names detected. "
            "Track names must match FFXIV instruments (e.g. Piano, Flute, "
            "ElectricGuitarOverdriven, Snare Drum, etc.).");
        result["tier"] = 1;
        return result;
    }

    // -----------------------------------------------------------------------
    // SINGLE guitar-program scan â€” used for BOTH tier detection AND channel map.
    // Scans all 16 channels for ProgChangeEvents with guitar programs (27-31).
    // -----------------------------------------------------------------------

    reportProgress(10, QStringLiteral("Analyzing guitar programs..."));

    QHash<int, int> guitarChToProgram;  // channel -> guitar program number

    if (hasGuitar) {
        // Use progAtTick(0) -- the effective program at tick 0, which is
        // exactly what the channel view displays (last PC at or before tick 0).
        // This is reliable even when multiple PCs exist at tick 0 from
        // previous Tier 2 runs (one per track), because progAtTick() returns
        // the last one, matching MIDI playback semantics.
        for (int ch = 0; ch < 16; ch++) {
            MidiChannel *channel = file->channel(ch);
            if (!channel) continue;
            int prog = channel->progAtTick(0);
            if (prog >= 27 && prog <= 31)
                guitarChToProgram[ch] = prog;
        }
    }

    // -----------------------------------------------------------------------
    // TIER DETECTION â€” Tier 2 (Rebuild) vs Tier 3 (Preserve)
    //
    //   Preserve mode if EITHER:
    //   (A) Guitar program_changes already exist â†’ file was configured
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
                    MidiChannel *channel = file->channel(ch);
                    if (!channel) continue;
                    QMultiMap<int, MidiEvent *> *map = channel->eventMap();
                    for (auto it = map->begin(); it != map->end(); ++it) {
                        if (it.value()->track() != track) continue;
                        if (dynamic_cast<NoteOnEvent *>(it.value())) {
                            chsWithNotes.insert(ch);
                            break;
                        }
                    }
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

    // -----------------------------------------------------------------------
    // Build channel assignment map + guitarChannelMap
    // -----------------------------------------------------------------------

    reportProgress(20, QStringLiteral("Building channel map..."));

    QVector<int> channelFor(trackCount, -1);
    QSet<int> usedChannels;
    QHash<QString, int> guitarChannelMap;

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
                usedChannels.insert(aCh);

                // Register first-seen variant (for chToVariant fallback)
                if (!guitarChannelMap.contains(baseNames[t]))
                    guitarChannelMap[baseNames[t]] = aCh;
            }
        }

        // channelFor for non-guitar tracks: use assignedChannel()
        for (int t = 0; t < trackCount; t++) {
            if (isGuitar(baseNames[t])) continue;
            if (isPercussion(baseNames[t])) {
                channelFor[t] = 9;
                usedChannels.insert(9);
                continue;
            }
            MidiTrack *track = file->track(t);
            int aCh = track->assignedChannel();
            if (aCh < 0 || aCh > 15) aCh = qMin(t, 15);
            channelFor[t] = aCh;
            usedChannels.insert(aCh);
        }
    } else {
        // TIER 2 â€” assign channels by track index (fresh start)
        // Duplicate guitar variants share the channel of the first occurrence.
        for (int t = 0; t < trackCount; t++) {
            if (isPercussion(baseNames[t])) {
                channelFor[t] = 9;
            } else if (hasGuitar && isGuitar(baseNames[t])
                       && guitarChannelMap.contains(baseNames[t])) {
                // Duplicate guitar variant: reuse first occurrence's channel
                channelFor[t] = guitarChannelMap[baseNames[t]];
            } else {
                int ch = t;
                if (ch > 15) ch = 15;
                channelFor[t] = ch;
                // Register first-seen guitar variant
                if (hasGuitar && isGuitar(baseNames[t]))
                    guitarChannelMap[baseNames[t]] = ch;
            }
            usedChannels.insert(channelFor[t]);
        }
    }

    // Reserve free channels for missing guitar variants (Tier 2 only)
    // Tier 3 must NOT allocate new channels — preserve existing assignments.
    if (hasGuitar && !isPreserveMode) {
        auto nextFreeChannel = [&]() -> int {
            for (int ch = 0; ch <= 15; ch++) {
                if (!usedChannels.contains(ch))
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
            if (isGuitar(baseNames[t]) && channelFor[t] >= 0) {
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

    // -----------------------------------------------------------------------
    // 2. CLEAN â€" remove program_change events
    //    Tier 2: remove ALL PCs (full rebuild)
    //    Tier 3: only remove PCs on guitar channels (non-guitar untouched)
    // -----------------------------------------------------------------------

    reportProgress(35, QStringLiteral("Removing old program changes..."));

    // -----------------------------------------------------------------------
    // BULK-OP UNDO STRATEGY â€" snapshot once, mutate fast, commit at end.
    //
    //   Background (perf bug fixed 2026-04-21):
    //   The default Protocol path of every mutating MidiChannel/MidiEvent
    //   call (removeEvent, insertEvent, moveToChannel, setVelocity) does a
    //   full deep copy() of the affected event/channel and pushes a
    //   ProtocolItem onto the open undo action. On a 20-track / >100k-event
    //   FFXIV file Tier 2 used to allocate one clone per touched event in
    //   each of CLEAN, MIGRATE, SWITCH and VELOCITY â€" easily 64 GB peak RSS
    //   and several minutes to finish.
    //
    //   Fix: take a single MidiChannel::copy() per channel and one
    //   MidiTrack::copy() per track BEFORE any mutation, then call the
    //   per-event APIs with toProtocol=false. After all phases finish we
    //   register one ProtocolItem per snapshot â€" so undo restores the full
    //   pre-fix state of every channel and track in one shot. RAM cost
    //   collapses from O(events Ã— mutations) to O(tracks + 16).
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
        if (isPreserveMode && !guitarChsFromTracks.contains(ch))
            continue; // Tier 3: only clean PCs on channels with guitar tracks
                       // (reserved channels keep their Tier 2 PCs intact)
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
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
    // 2b. CLEAN â€" remove non-essential events (Tier 2 only)
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
    // 3. MIGRATE â€” move events to correct channels (Tier 2 only)
    //    Tier 3 (Preserve) skips this â€” channels are already established
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
                info.ev->moveToChannel(targetCh, false);
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
                for (int ch : allGuitarChs) {
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
                        QJsonObject entry;
                        entry["track"]   = t;
                        entry["oldName"] = track->name();
                        entry["newName"] = newVariant;
                        renameLog.append(entry);
                        track->setName(newVariant);
                        baseNames[t] = newVariant;
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // 4. PROGRAM â€” insert program_change at tick 0
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

    // -----------------------------------------------------------------------
    // 4b. SWITCH â€” insert program_change at guitar channel switch points
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

            for (int ch : allGuitarChs) {
                MidiChannel *channel = file->channel(ch);
                if (!channel) continue;
                QMultiMap<int, MidiEvent *> *map = channel->eventMap();
                for (auto eit = map->begin(); eit != map->end(); ++eit) {
                    if (eit.value()->track() != track) continue;
                    if (dynamic_cast<NoteOnEvent *>(eit.value()))
                        notes.append({eit.key(), ch});
                }
            }

            std::sort(notes.begin(), notes.end(),
                      [](const NoteInfo &a, const NoteInfo &b) {
                          return a.tick < b.tick;
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
    // 5. VELOCITY â€" normalise all NoteOn velocities to 127 (max)
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
                noteOn->setVelocity(127, false);
                velocityChangedCount++;
            }
        }
    }

    // -----------------------------------------------------------------------
    // 6. REPORT â€" with debug info
    // -----------------------------------------------------------------------

    reportProgress(100, QStringLiteral("Done!"));

    // Commit the bulk-op snapshots taken before phase 2. One ProtocolItem
    // per touched track + one per touched channel â€" the entire edit becomes
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
