#include "FFXIVChannelFixer.h"

#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/ProgChangeEvent.h"

#include <QRegularExpression>
#include <QSet>
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
    static const QHash<QString, int> map = {
        {"Piano", 0},       {"Harp", 46},       {"Fiddle", 45},
        {"Lute", 24},       {"Fife", 72},       {"Flute", 73},
        {"Oboe", 68},       {"Panpipes", 75},   {"Clarinet", 71},
        {"Trumpet", 56},    {"Saxophone", 65},  {"Trombone", 57},
        {"Horn", 60},       {"Tuba", 58},
        {"Violin", 40},     {"Viola", 41},      {"Cello", 42},
        {"Double Bass", 43},
        {"Timpani", 47},    {"Bongo", 116},     {"Bass Drum", 117},
        {"Snare Drum", 115},{"Cymbal", 127},
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

    QHash<int, int> guitarChToProgram;  // channel â†’ guitar program number
    int totalProgChangesSeen = 0;       // debug counter

    if (hasGuitar) {
        for (int ch = 0; ch < 16; ch++) {
            MidiChannel *channel = file->channel(ch);
            if (!channel) continue;
            QMultiMap<int, MidiEvent *> *map = channel->eventMap();
            for (auto it = map->begin(); it != map->end(); ++it) {
                auto *pc = dynamic_cast<ProgChangeEvent *>(it.value());
                if (!pc) continue;
                totalProgChangesSeen++;
                int prog = pc->program();
                if (prog >= 27 && prog <= 31) {
                    if (!guitarChToProgram.contains(ch))
                        guitarChToProgram[ch] = prog;
                }
            }
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
        // TIER 3 â€” minimal-invasive: notes are ground truth for channel mapping
        if (hasGuitar) {
            // A) For each guitar track, find primary channel from NOTES
            for (int t = 0; t < trackCount; t++) {
                if (!isGuitar(baseNames[t])) continue;
                MidiTrack *track = file->track(t);
                QHash<int, int> chCount;
                for (int ch = 0; ch < 16; ch++) {
                    MidiChannel *channel = file->channel(ch);
                    if (!channel) continue;
                    QMultiMap<int, MidiEvent *> *map = channel->eventMap();
                    for (auto it = map->begin(); it != map->end(); ++it) {
                        if (it.value()->track() != track) continue;
                        if (dynamic_cast<NoteOnEvent *>(it.value()))
                            chCount[ch]++;
                    }
                }
                int bestCh = t, bestCount = 0;
                for (auto it = chCount.begin(); it != chCount.end(); ++it) {
                    if (it.value() > bestCount) { bestCount = it.value(); bestCh = it.key(); }
                }
                channelFor[t] = bestCh;
                guitarChannelMap[baseNames[t]] = bestCh;
                usedChannels.insert(bestCh);
            }

            // B) For missing variants (no track), fill from program_changes
            for (auto it = guitarChToProgram.begin(); it != guitarChToProgram.end(); ++it) {
                int ch = it.key();
                int prog = it.value();
                for (const QString &v : allGuitarVariants) {
                    if (programNumber(v) == prog && !guitarChannelMap.contains(v)) {
                        guitarChannelMap[v] = ch;
                        usedChannels.insert(ch);
                        break;
                    }
                }
            }
        }

        // C) channelFor for non-guitar tracks: note distribution
        for (int t = 0; t < trackCount; t++) {
            if (isGuitar(baseNames[t])) continue;
            if (isPercussion(baseNames[t])) {
                channelFor[t] = 9;
                usedChannels.insert(9);
                continue;
            }
            MidiTrack *track = file->track(t);
            QHash<int, int> chCount;
            for (int ch = 0; ch < 16; ch++) {
                MidiChannel *channel = file->channel(ch);
                if (!channel) continue;
                QMultiMap<int, MidiEvent *> *map = channel->eventMap();
                for (auto it = map->begin(); it != map->end(); ++it) {
                    if (it.value()->track() != track) continue;
                    if (dynamic_cast<NoteOnEvent *>(it.value()))
                        chCount[ch]++;
                }
            }
            int bestCh = t, bestCount = 0;
            for (auto it = chCount.begin(); it != chCount.end(); ++it) {
                if (it.value() > bestCount) { bestCount = it.value(); bestCh = it.key(); }
            }
            channelFor[t] = bestCh;
            usedChannels.insert(bestCh);
        }
    } else {
        // TIER 2 â€” assign channels by track index (fresh start)
        for (int t = 0; t < trackCount; t++) {
            if (isPercussion(baseNames[t])) {
                channelFor[t] = 9;
            } else {
                int ch = t;
                if (ch > 15) ch = 15;
                channelFor[t] = ch;
            }
            usedChannels.insert(channelFor[t]);
        }

        if (hasGuitar) {
            for (int t = 0; t < trackCount; t++) {
                if (isGuitar(baseNames[t]))
                    guitarChannelMap[baseNames[t]] = channelFor[t];
            }
        }
    }

    // Reserve free channels for missing guitar variants
    if (hasGuitar) {
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

    // Build guitar channel set
    QSet<int> allGuitarChs;
    if (hasGuitar) {
        for (auto it = guitarChannelMap.begin(); it != guitarChannelMap.end(); ++it)
            allGuitarChs.insert(it.value());
    }

    // -----------------------------------------------------------------------
    // 2. CLEAN â€” remove program_change events
    //    Tier 2: remove ALL PCs (full rebuild)
    //    Tier 3: only remove PCs on guitar channels (non-guitar untouched)
    // -----------------------------------------------------------------------

    reportProgress(35, QStringLiteral("Removing old program changes..."));

    int removedPcCount = 0;
    for (int ch = 0; ch < 16; ch++) {
        if (isPreserveMode && !allGuitarChs.contains(ch))
            continue; // Tier 3: skip non-guitar channels entirely
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        QList<MidiEvent *> toRemove;
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (dynamic_cast<ProgChangeEvent *>(it.value()))
                toRemove.append(it.value());
        }
        for (MidiEvent *ev : toRemove)
            channel->removeEvent(ev);
        removedPcCount += toRemove.size();
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

            bool isGuitarTrack = isGuitar(baseNames[t]);
            for (const auto &info : trackEvents) {
                if (info.currentCh == targetCh) continue;
                if (isGuitarTrack && allGuitarChs.contains(info.currentCh))
                    continue;
                info.ev->moveToChannel(targetCh);
            }

            track->assignChannel(targetCh);
        }
    } else {
        // Tier 3: preserve â€” NO note movement
        // Only rename guitar tracks if tick-0 note is on a different variant
        QHash<int, QString> chToVariant;
        for (auto it = guitarChannelMap.begin(); it != guitarChannelMap.end(); ++it)
            chToVariant[it.value()] = it.key();

        for (int t = 0; t < trackCount; t++) {
            if (!isGuitar(baseNames[t])) {
                file->track(t)->assignChannel(channelFor[t]);
                continue;
            }
            MidiTrack *track = file->track(t);

            // Find the first NoteOnEvent (lowest tick) across all guitar channels
            int firstTick = INT_MAX;
            int firstCh = -1;
            for (int ch : allGuitarChs) {
                MidiChannel *channel = file->channel(ch);
                if (!channel) continue;
                QMultiMap<int, MidiEvent *> *map = channel->eventMap();
                for (auto it = map->begin(); it != map->end(); ++it) {
                    if (it.value()->track() != track) continue;
                    if (!dynamic_cast<NoteOnEvent *>(it.value())) continue;
                    if (it.key() < firstTick) {
                        firstTick = it.key();
                        firstCh = ch;
                    }
                    break; // map is sorted, first match per channel is enough
                }
            }

            if (firstCh >= 0) {
                QString tick0Variant = chToVariant.value(firstCh);
                if (!tick0Variant.isEmpty() && tick0Variant != baseNames[t]) {
                    QString oldName = file->track(t)->name();
                    QString suffix;
                    static const QRegularExpression suffixRe(QStringLiteral("([+-]\\d+)$"));
                    QRegularExpressionMatch m = suffixRe.match(oldName);
                    if (m.hasMatch())
                        suffix = m.captured(1);

                    QString newName = tick0Variant + suffix;
                    track->setName(newName);

                    guitarVariantsPresent.remove(baseNames[t]);
                    guitarVariantsPresent.insert(tick0Variant);
                    baseNames[t] = tick0Variant;

                    if (guitarChannelMap.contains(tick0Variant))
                        channelFor[t] = guitarChannelMap[tick0Variant];

                    QJsonObject logEntry;
                    logEntry["track"] = t;
                    logEntry["oldName"] = oldName;
                    logEntry["newName"] = newName;
                    logEntry["reason"] = QString("First note (tick %1) is on CH%2 (%3)")
                        .arg(firstTick).arg(firstCh).arg(tick0Variant);
                    renameLog.append(logEntry);
                }
            }

            track->assignChannel(channelFor[t]);
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

    // All guitar channels: from guitarChannelMap (both tiers)
    if (hasGuitar) {
        for (auto it = guitarChannelMap.begin(); it != guitarChannelMap.end(); ++it) {
            int prog = programNumber(it.key());
            if (prog >= 0)
                channelPrograms.append({it.value(), prog});
        }
    }

    // Insert on every track
    for (int t = 0; t < trackCount; t++) {
        MidiTrack *track = file->track(t);
        for (const auto &cp : channelPrograms) {
            auto *pc = new ProgChangeEvent(cp.channel, cp.program, track);
            file->channel(cp.channel)->insertEvent(pc, 0);
        }
    }

    // -----------------------------------------------------------------------
    // 4b. SWITCH â€” insert program_change at guitar channel switch points
    // -----------------------------------------------------------------------

    reportProgress(90, QStringLiteral("Processing guitar switches..."));

    int switchCount = 0;
    if (hasGuitar) {
        QHash<int, QString> chToVariant;
        for (auto it = guitarChannelMap.begin(); it != guitarChannelMap.end(); ++it)
            chToVariant[it.value()] = it.key();

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
                            file->channel(n.channel)->insertEvent(pc, n.tick);
                            switchCount++;
                        }
                    }
                    lastCh = n.channel;
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // 5. REPORT â€” with debug info
    // -----------------------------------------------------------------------

    reportProgress(100, QStringLiteral("Done!"));

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
    result["trackCount"] = trackCount;
    return result;
}
