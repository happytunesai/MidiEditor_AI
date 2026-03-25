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
// fixChannels  — the main entry point
// ---------------------------------------------------------------------------

QJsonObject FFXIVChannelFixer::fixChannels(MidiFile *file) {
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

    // -----------------------------------------------------------------------
    // 1. ANALYZE — classify tracks and build the channel map
    // -----------------------------------------------------------------------

    // channelFor[trackIndex] = target channel
    QVector<int> channelFor(trackCount, -1);
    QVector<QString> baseNames(trackCount);

    // Collect guitar variants present in the file
    QSet<QString> guitarVariantsPresent;
    bool hasGuitar = false;

    for (int t = 0; t < trackCount; t++) {
        QString base = stripSuffix(file->track(t)->name());
        baseNames[t] = base;
        if (isGuitar(base)) {
            hasGuitar = true;
            guitarVariantsPresent.insert(base);
        }
    }

    // First pass: assign channels.
    //  - Percussion → CH9
    //  - Everything else → track index (Rule 1)
    //  - Guitar tracks → track index (Rule 1) — their variant is
    //    determined by the track name / program_change, not by channel
    //    sharing.  Missing guitar variants get free channels later.

    QSet<int> usedChannels;

    for (int t = 0; t < trackCount; t++) {
        if (isPercussion(baseNames[t])) {
            channelFor[t] = 9;
        } else {
            int ch = t;
            if (ch > 15) ch = 15; // clamp to valid range
            channelFor[t] = ch;
        }
        usedChannels.insert(channelFor[t]);
    }

    // If guitars are present, reserve channels for missing guitar variants
    // so that guitar switch program_change events can be inserted.
    static const QStringList allGuitarVariants = {
        QStringLiteral("ElectricGuitarClean"),
        QStringLiteral("ElectricGuitarMuted"),
        QStringLiteral("ElectricGuitarOverdriven"),
        QStringLiteral("ElectricGuitarPowerChords"),
        QStringLiteral("ElectricGuitarSpecial")
    };

    // Map variant name → channel for ALL 5 guitar variants
    QHash<QString, int> guitarChannelMap;
    if (hasGuitar) {
        // Variants that have a track get their track's channel
        for (int t = 0; t < trackCount; t++) {
            if (isGuitar(baseNames[t])) {
                guitarChannelMap[baseNames[t]] = channelFor[t];
            }
        }
        // Missing variants get the next free channel
        auto nextFreeChannel = [&]() -> int {
            for (int ch = 0; ch <= 15; ch++) {
                if (!usedChannels.contains(ch))
                    return ch;
            }
            return -1; // no free channel
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

    // -----------------------------------------------------------------------
    // 2. CLEAN — remove ALL existing program_change events from all channels
    //    This ensures we start from a clean slate with no legacy assignments.
    // -----------------------------------------------------------------------

    for (int ch = 0; ch < 16; ch++) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();

        // Collect ALL program_change events for removal
        QList<MidiEvent *> toRemove;
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (dynamic_cast<ProgChangeEvent *>(it.value())) {
                toRemove.append(it.value());
            }
        }
        for (MidiEvent *ev : toRemove) {
            channel->removeEvent(ev);
        }
    }

    // -----------------------------------------------------------------------
    // 3. MIGRATE — move all events to the correct channel
    // -----------------------------------------------------------------------

    for (int t = 0; t < trackCount; t++) {
        int targetCh = channelFor[t];
        MidiTrack *track = file->track(t);

        // Collect events belonging to this track from ALL channels
        // (they may currently be on the wrong channel)
        struct EventInfo { MidiEvent *ev; int currentCh; };
        QList<EventInfo> trackEvents;

        for (int ch = 0; ch < 16; ch++) {
            MidiChannel *channel = file->channel(ch);
            if (!channel) continue;
            QMultiMap<int, MidiEvent *> *map = channel->eventMap();
            for (auto it = map->begin(); it != map->end(); ++it) {
                MidiEvent *ev = it.value();
                if (ev->track() != track) continue;
                // Skip ProgChangeEvent — we handle those separately
                if (dynamic_cast<ProgChangeEvent *>(ev)) continue;
                // Skip off-events — they move with their NoteOn
                if (dynamic_cast<OffEvent *>(ev)) continue;
                trackEvents.append({ev, ch});
            }
        }

        // Move events to the target channel
        for (const auto &info : trackEvents) {
            if (info.currentCh != targetCh) {
                info.ev->moveToChannel(targetCh);
            }
        }

        // Update track's assigned channel metadata
        track->assignChannel(targetCh);
    }

    // -----------------------------------------------------------------------
    // 4. PROGRAM — insert program_change at tick 0 for all used channels
    //              on every track
    // -----------------------------------------------------------------------

    // Build the set of (channel, program) pairs to insert
    struct ChannelProgram { int channel; int program; };
    QList<ChannelProgram> channelPrograms;

    for (int t = 0; t < trackCount; t++) {
        int prog = programNumber(baseNames[t]);
        if (prog >= 0) {
            channelPrograms.append({channelFor[t], prog});
        }
    }

    // Add guitar variant channels (for variants not present as tracks)
    if (hasGuitar) {
        for (const QString &variant : allGuitarVariants) {
            if (!guitarVariantsPresent.contains(variant) && guitarChannelMap.contains(variant)) {
                int prog = programNumber(variant);
                if (prog >= 0) {
                    channelPrograms.append({guitarChannelMap[variant], prog});
                }
            }
        }
    }

    // Insert program_change events on every track at tick 0
    // (FFXIV MidiBard2 expects every track to carry all channel mappings)
    for (int t = 0; t < trackCount; t++) {
        MidiTrack *track = file->track(t);
        for (const auto &cp : channelPrograms) {
            auto *pc = new ProgChangeEvent(cp.channel, cp.program, track);
            file->channel(cp.channel)->insertEvent(pc, 0);
        }
    }

    // -----------------------------------------------------------------------
    // 5. REPORT — build JSON result
    // -----------------------------------------------------------------------

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

    result["success"] = true;
    result["channelMap"] = channelMapArr;
    result["summary"] = QString("Fixed %1 tracks: channels assigned, program_change inserted%2")
                            .arg(trackCount)
                            .arg(hasGuitar ? QStringLiteral(", guitar switch channels configured") : QString());
    return result;
}
