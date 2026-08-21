#include "FfxivPlayabilityValidator.h"

#include "FFXIVChannelFixer.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"

#include <QLocale>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QStringList>

namespace {

QString noteName(int note) {
    static const char *names[] = {"C", "C#", "D", "D#", "E", "F",
                                  "F#", "G", "G#", "A", "A#", "B"};
    return QStringLiteral("%1%2").arg(QLatin1String(names[note % 12]))
                                 .arg(note / 12 - 1);
}

/** Thousands separators for a headless string: QLocale::c() omits the group
 *  separator by default, and "12871 events" reads like a serial number. */
QString groupedNumber(int n) {
    QLocale locale = QLocale::c();
    locale.setNumberOptions(locale.numberOptions() & ~QLocale::OmitGroupSeparator);
    return locale.toString(n);
}

/** What the tempo map LOOKS like, in the words the finding uses. Two facts,
 *  because together they say where the events came from: the DIRECTION (a
 *  one-way ramp is a DAW export artefact, anything else is automation) and
 *  how many DISTINCT tempos there actually are - a ramp exported as thousands
 *  of events usually holds a few dozen values, and seeing "1,201 events, 30
 *  distinct tempos" is what makes the redundancy obvious. */
QString tempoMapShape(const QList<int> &bpmInOrder) {
    int ups = 0;
    int downs = 0;
    QSet<int> distinct;
    for (int i = 0; i < bpmInOrder.size(); ++i) {
        distinct.insert(bpmInOrder.at(i));
        if (i == 0) continue;
        const int delta = bpmInOrder.at(i) - bpmInOrder.at(i - 1);
        if (delta > 0) ++ups;
        else if (delta < 0) ++downs;
    }
    const int moves = ups + downs;
    if (moves == 0) {
        return QStringLiteral("one tempo, repeated over and over");
    }
    QString direction;
    const int dominant = qMax(ups, downs);
    if (dominant * 5 >= moves * 4) {
        direction = ups >= downs ? QStringLiteral("continuous ramp up")
                                 : QStringLiteral("continuous ramp down");
    } else {
        direction = QStringLiteral("dense automation");
    }
    return QStringLiteral("%1, %2 distinct tempos")
        .arg(direction).arg(distinct.size());
}

} // namespace

int FfxivPlayabilityReport::countOf(FfxivPlayabilityIssue::Type t) const {
    int n = 0;
    for (const FfxivPlayabilityIssue &i : issues) {
        if (i.type == t) ++n;
    }
    return n;
}

QList<MidiEvent *> FfxivPlayabilityReport::offendingNotes() const {
    QSet<MidiEvent *> seen;
    QList<MidiEvent *> out;
    for (const FfxivPlayabilityIssue &i : issues) {
        for (MidiEvent *ev : i.events) {
            if (ev && !seen.contains(ev)) {
                seen.insert(ev);
                out.append(ev);
            }
        }
    }
    return out;
}

QList<MidiEvent *> ffxivCollisionSurplus(
    const QList<FfxivPlayabilityIssue> &issues, const QList<int> &indices) {
    // Survivor per collision group: highest pitch (the melody rule Auto-Fit
    // documents), among equal pitches the louder note. A note may survive
    // one issue and be a victim of another (C+C+E: the chord keeps E, the
    // duplicate pair contributes both Cs) - the victim UNION handles that
    // correctly.
    QList<MidiEvent *> victims;
    QSet<MidiEvent *> seen;
    for (int idx : indices) {
        if (idx < 0 || idx >= issues.size()) continue;
        const FfxivPlayabilityIssue &i = issues.at(idx);
        if (i.type != FfxivPlayabilityIssue::Type::Overlap
            && i.type != FfxivPlayabilityIssue::Type::DuplicateNote)
            continue;
        NoteOnEvent *survivor = nullptr;
        for (MidiEvent *ev : i.events) {
            auto *on = dynamic_cast<NoteOnEvent *>(ev);
            if (!on) continue;
            if (!survivor || on->note() > survivor->note()
                || (on->note() == survivor->note()
                    && on->velocity() > survivor->velocity())) {
                survivor = on;
            }
        }
        for (MidiEvent *ev : i.events) {
            if (ev && ev != survivor && !seen.contains(ev)) {
                seen.insert(ev);
                victims.append(ev);
            }
        }
    }
    return victims;
}

FfxivPlayabilityReport FfxivPlayabilityValidator::validate(
    MidiFile *file, const FfxivPlayabilityChecks &checks) {
    FfxivPlayabilityReport report;
    if (!file) {
        report.error = QStringLiteral("No file loaded.");
        return report;
    }
    report.ok = true;

    const int trackCount = file->numTracks();
    report.checkedTracks = trackCount;

    for (int t = 0; t < trackCount; ++t) {
        MidiTrack *track = file->track(t);
        if (!track) continue;
        const QString name = track->name();
        const QString baseName = FFXIVChannelFixer::stripSuffix(name);
        const bool isInstrument = FFXIVChannelFixer::programNumber(baseName) >= 0;

        const bool trackIsGuitar = FFXIVChannelFixer::isGuitar(baseName);

        // Collect this track's notes (with their channel - guitars spread
        // over several channels for variant switches).
        struct NoteInfo {
            int tick;
            int note;
            int channel;
            NoteOnEvent *ev;
        };
        QList<NoteInfo> notes;

        for (int ch = 0; ch < 16; ++ch) {
            MidiChannel *channel = file->channel(ch);
            if (!channel) continue;
            QMultiMap<int, MidiEvent *> *map = channel->eventMap();
            for (auto it = map->begin(); it != map->end(); ++it) {
                MidiEvent *ev = it.value();
                if (!ev || ev->track() != track) continue;
                if (auto *noteOn = dynamic_cast<NoteOnEvent *>(ev)) {
                    notes.append({noteOn->midiTime(), noteOn->note(), ch, noteOn});
                }
            }
        }
        report.checkedNotes += notes.size();

        // Track-name check, only for tracks that HAVE notes: a silent track
        // occupies no performer, so its name is irrelevant to playability -
        // without this rule every file's "Tempo Track" (the app's own
        // default) would be flagged and the report would cry wolf on every
        // check. (Deliberate sharpening over the old AI-tool rule, which
        // flagged any non-empty non-instrument name.)
        if (checks.trackNames && !notes.isEmpty() && !baseName.isEmpty()
            && !isInstrument) {
            FfxivPlayabilityIssue issue;
            issue.type = FfxivPlayabilityIssue::Type::TrackName;
            issue.track = t;
            issue.details = QStringLiteral(
                "Track name '%1' doesn't match any FFXIV instrument").arg(name);
            report.issues.append(issue);
        }

        // Empty instrument track: named like a performer, but nothing to
        // play. Informational - it wastes one of the eight slots and is
        // usually a leftover from experimenting.
        if (checks.emptyTracks && notes.isEmpty() && isInstrument) {
            FfxivPlayabilityIssue issue;
            issue.type = FfxivPlayabilityIssue::Type::EmptyTrack;
            issue.track = t;
            issue.details = QStringLiteral(
                "Track '%1' is named like an instrument but has no notes").arg(name);
            report.issues.append(issue);
        }

        // Channel spread - EDITOR playback only (in game the track name
        // selects the instrument): a non-guitar track whose notes sit on
        // several channels plays as several instruments in the editor's
        // SoundFont preview. The channel fixer is the repair. Deliberately
        // NOT a program-change comparison: shared channels and stacked
        // tick-0 PCs made that produce false findings on real octets.
        if (checks.channelSpread && isInstrument && !trackIsGuitar
            && !notes.isEmpty()) {
            QSet<int> usedChannels;
            for (const NoteInfo &n : notes) usedChannels.insert(n.channel);
            if (usedChannels.size() > 1) {
                FfxivPlayabilityIssue issue;
                issue.type = FfxivPlayabilityIssue::Type::ChannelSpread;
                issue.track = t;
                issue.tick = notes.first().tick;
                for (const NoteInfo &n : notes) issue.events.append(n.ev);
                issue.details = QStringLiteral(
                    "Track '%1' has notes on %2 channels - editor playback "
                    "will use different sounds for them (in game the track "
                    "name decides; run the channel fixer to tidy up)")
                        .arg(name).arg(usedChannels.size());
                report.issues.append(issue);
            }
        }

        // Range check: C3-C6 = MIDI 48-84.
        if (checks.range) {
            for (const NoteInfo &n : notes) {
                if (n.note < 48 || n.note > 84) {
                    FfxivPlayabilityIssue issue;
                    issue.type = FfxivPlayabilityIssue::Type::OutOfRange;
                    issue.track = t;
                    issue.tick = n.tick;
                    issue.details = QStringLiteral(
                        "Note %1 (%2) outside C3-C6 (MIDI 48-84) at tick %3")
                            .arg(noteName(n.note)).arg(n.note).arg(n.tick);
                    issue.events.append(n.ev);
                    report.issues.append(issue);
                }
            }
        }

        if (!checks.simultaneousNotes && !checks.stackedDuplicates) {
            continue;
        }

        // Monophony - user-verified in-game rule (2026-07-27): a held note
        // overlapped by later staccato notes is inaudible at speed and
        // nobody fixes it, so hold-overlaps are NOT flagged. What really
        // collides on a monophonic performer are notes STARTING on the same
        // tick. Group note starts by tick (guitars: per channel, because
        // different channels are variant switches and legal):
        //   - one pitch appearing twice in a group -> stacked duplicate
        //     (checks.stackedDuplicates)
        //   - two or more distinct pitches in a group -> simultaneous notes
        //     (checks.simultaneousNotes; a chord the performer cannot play -
        //     one issue per group, not per pair, so a triad counts once)
        // The two are separately switchable because they mean opposite
        // things: chords are often intentional (the game rolls them as a
        // fast arpeggio), a stacked duplicate is almost always a defect.
        // The grouping is shared - it is computed once for both.
        QMap<QPair<int, int>, QList<int>> groups; // (channel|0, tick) -> idx
        for (int i = 0; i < notes.size(); ++i) {
            const int chKey = trackIsGuitar ? notes[i].channel : 0;
            groups[qMakePair(chKey, notes[i].tick)].append(i);
        }
        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
            const QList<int> &idx = it.value();
            if (idx.size() < 2) continue;
            const int tick = it.key().second;

            // Grouping by pitch serves BOTH findings - computed once.
            QMap<int, QList<int>> byPitch;
            for (int i : idx) byPitch[notes[i].note].append(i);

            // Per-pitch duplicate detection inside the group.
            if (checks.stackedDuplicates) {
                for (auto p = byPitch.constBegin(); p != byPitch.constEnd(); ++p) {
                    if (p.value().size() < 2) continue;
                    FfxivPlayabilityIssue issue;
                    issue.type = FfxivPlayabilityIssue::Type::DuplicateNote;
                    issue.track = t;
                    issue.tick = tick;
                    for (int i : p.value()) issue.events.append(notes[i].ev);
                    issue.details = p.value().size() == 2
                        ? QStringLiteral("Duplicate %1 stacked on tick %2")
                              .arg(noteName(p.key())).arg(tick)
                        : QStringLiteral("%1 x%2 stacked on tick %3")
                              .arg(noteName(p.key())).arg(p.value().size()).arg(tick);
                    report.issues.append(issue);
                }
            }

            // Distinct pitches sounding at once.
            if (checks.simultaneousNotes && byPitch.size() >= 2) {
                FfxivPlayabilityIssue issue;
                issue.type = FfxivPlayabilityIssue::Type::Overlap;
                issue.track = t;
                issue.tick = tick;
                QStringList pitchNames;
                for (auto p = byPitch.constBegin(); p != byPitch.constEnd(); ++p)
                    pitchNames.append(noteName(p.key()));
                for (int i : idx) issue.events.append(notes[i].ev);
                issue.details = QStringLiteral(
                    "%1 simultaneous notes at tick %2 (%3)")
                        .arg(idx.size()).arg(tick)
                        .arg(pitchNames.join(QStringLiteral(", ")));
                report.issues.append(issue);
            }
        }
    }

    // Tempo map (Phase 49) - a FILE-level finding, so it carries track -1.
    // MidiBard pays for every event in the file just like the editor does, so
    // a DAW ramp exported as one tempo event every few ticks is an FFXIV
    // problem and not only an editor performance one. Everything about the
    // rule lives in FfxivTempoMapRule so a normal file stays quiet: a
    // hand-written accelerando has a handful of events, this fires on maps
    // whose density is a property of the EXPORT.
    if (checks.tempoMap) {
        QList<int> bpmInOrder;
        MidiChannel *tempoChannel = file->channel(17);
        if (tempoChannel) {
            QMultiMap<int, MidiEvent *> *map = tempoChannel->eventMap();
            for (auto it = map->constBegin(); it != map->constEnd(); ++it) {
                if (auto *tempo = dynamic_cast<TempoChangeEvent *>(it.value())) {
                    bpmInOrder.append(tempo->beatsPerQuarter());
                }
            }
        }
        const int count = bpmInOrder.size();
        // A nominal 4/4 bar: the meter map is irrelevant here (the question is
        // "how crowded is this map", not "which bar are we in"), and reading it
        // would drag the validator into the time-signature walk for nothing.
        const int barTicks = qMax(1, file->ticksPerQuarter() * 4);
        const double bars = qMax(1.0, double(file->endTick()) / barTicks);
        const double perBar = count / bars;
        const bool dense =
            count > FfxivTempoMapRule::kTempoMapAbsoluteLimit
            || (count >= FfxivTempoMapRule::kTempoMapDenseFloor
                && perBar > FfxivTempoMapRule::kTempoMapEventsPerBar);
        if (dense) {
            FfxivPlayabilityIssue issue;
            issue.type = FfxivPlayabilityIssue::Type::TempoMap;
            issue.track = -1;
            issue.tick = 0;
            issue.details = QStringLiteral(
                "Tempo map: %1 events (%2), about %3 per bar - every one of "
                "them costs time in the editor and in game; Thin Tempo Map "
                "reduces the map while keeping the timing")
                    .arg(groupedNumber(count))
                    .arg(tempoMapShape(bpmInOrder))
                    .arg(perBar, 0, 'f', perBar < 10 ? 1 : 0);
            report.issues.append(issue);
        }
    }

    return report;
}
