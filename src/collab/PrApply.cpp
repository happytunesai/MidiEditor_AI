/*
 * MidiEditor AI - PrApply implementation.
 */

#include "PrApply.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QMultiMap>

#include "../MidiEvent/ControlChangeEvent.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/PitchBendEvent.h"
#include "../MidiEvent/ProgChangeEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../MidiEvent/TimeSignatureEvent.h"
#include "../MidiEvent/KeySignatureEvent.h"
#include "../MidiEvent/TextEvent.h"
#include "../MidiEvent/ChannelPressureEvent.h"
#include "../MidiEvent/KeyPressureEvent.h"
#include "../ai/MidiEventSerializer.h"
#include "../midi/MidiChannel.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../protocol/Protocol.h"
#include "CollabService.h"

Q_DECLARE_LOGGING_CATEGORY(lanLog)

MidiEvent *PrApply::findMatchingEvent(MidiFile *file, const QJsonObject &je) {
    if (!file) return nullptr;
    int channel = je.value(QStringLiteral("channel")).toInt(-1);
    int track = je.value(QStringLiteral("track")).toInt(-1);
    int tick = je.value(QStringLiteral("tick")).toInt(-1);
    // Meta channels 16/17/18 are valid lookup destinations for tempo /
    // time-sig / text / key-sig events (Plan §11.10j).
    if (channel < 0 || channel >= 19 || tick < 0) return nullptr;
    QString type = je.value(QStringLiteral("type")).toString();
    if (type.isEmpty()) return nullptr;

    MidiChannel *ch = file->channel(channel);
    if (!ch) return nullptr;
    QMultiMap<int, MidiEvent *> *map = ch->eventMap();
    if (!map) return nullptr;

    auto it = map->find(tick);
    while (it != map->end() && it.key() == tick) {
        MidiEvent *ev = it.value();
        ++it;
        if (!ev) continue;
        if (track >= 0 && ev->track() && ev->track()->number() != track) continue;

        if (type == QLatin1String("note")) {
            NoteOnEvent *n = dynamic_cast<NoteOnEvent *>(ev);
            int targetNote = je.value(QStringLiteral("note")).toInt(-1);
            if (n && n->note() == targetNote) return n;
        } else if (type == QLatin1String("cc")) {
            ControlChangeEvent *c = dynamic_cast<ControlChangeEvent *>(ev);
            int targetCtrl = je.value(QStringLiteral("control")).toInt(-1);
            if (c && c->control() == targetCtrl) return c;
        } else if (type == QLatin1String("pitch_bend")) {
            PitchBendEvent *p = dynamic_cast<PitchBendEvent *>(ev);
            if (p) return p;
        } else if (type == QLatin1String("program_change")) {
            ProgChangeEvent *p = dynamic_cast<ProgChangeEvent *>(ev);
            if (p) return p;
        } else if (type == QLatin1String("tempo")) {
            TempoChangeEvent *t = dynamic_cast<TempoChangeEvent *>(ev);
            if (t) return t;
        } else if (type == QLatin1String("time_sig")) {
            TimeSignatureEvent *t = dynamic_cast<TimeSignatureEvent *>(ev);
            if (t) return t;
        } else if (type == QLatin1String("key_sig")) {
            KeySignatureEvent *k = dynamic_cast<KeySignatureEvent *>(ev);
            if (k) return k;
        } else if (type == QLatin1String("text")) {
            TextEvent *t = dynamic_cast<TextEvent *>(ev);
            int targetType = je.value(QStringLiteral("textType")).toInt(-1);
            if (t && (targetType < 0 || t->type() == targetType)) return t;
        } else if (type == QLatin1String("chan_pressure")) {
            ChannelPressureEvent *c = dynamic_cast<ChannelPressureEvent *>(ev);
            if (c) return c;
        } else if (type == QLatin1String("key_pressure")) {
            KeyPressureEvent *k = dynamic_cast<KeyPressureEvent *>(ev);
            int targetNote = je.value(QStringLiteral("note")).toInt(-1);
            if (k && k->note() == targetNote) return k;
        }
    }
    return nullptr;
}

namespace {

// Insert a single event by deserializing through MidiEventSerializer.
// Returns true on success, false on validation failure. On success, all
// created events are tagged with \a author in CollabService so the paint
// layer can show the "from a PR" highlight.
bool insertEvent(MidiFile *file, const QJsonObject &je,
                 const QString &author, QStringList *warningsOut) {
    int channel = je.value(QStringLiteral("channel")).toInt(-1);
    int trackIdx = je.value(QStringLiteral("track")).toInt(-1);
    // Plan §11.10j: 16 = text/key-sig meta, 17 = tempo, 18 = time-sig.
    if (channel < 0 || channel >= 19) {
        if (warningsOut) warningsOut->append(QStringLiteral("Skipped event: invalid channel %1").arg(channel));
        return false;
    }
    if (trackIdx < 0) {
        // BUG-COLLAB-031: if the sender's serializer didn't include
        // the `track` field (older builds + a few event-type-specific
        // omissions in the v1.7 serializer), treat it as track 0
        // instead of refusing the whole event. Otherwise a missing
        // track field on an instrument change makes the channel lose
        // its program entirely on the receiver. Track 0 is the safe
        // default — every standard MIDI file has at least one.
        if (warningsOut) {
            warningsOut->append(QStringLiteral(
                "Event missing track field, defaulting to track 0 (sender may be on an older build)"));
        }
        trackIdx = 0;
    }
    // Plan §11.10o: auto-extend tracks. Live-sync operations like
    // "Split channels to tracks" or AI tool calls that introduce new
    // tracks on the sender produce hunks that reference track indices
    // beyond the receiver's current count. Earlier code skipped those
    // events outright (~6800 events lost on a single Split-Channels
    // round-trip per the 2026-05-07 logs); now we add empty tracks
    // until the index fits, then insert. The new tracks pick up the
    // sender's events on the same call. Track names are
    // auto-generated (`Track N`) — a future extension could carry
    // track metadata in the wire format.
    //
    // BUG-COLLAB-015: cap auto-extension. A buggy or malicious peer
    // (or an externally-imported PR token) could send `track: 1e9`
    // and OOM the receiver before the loop terminates. 256 tracks
    // covers any realistic split (16 MIDI channels × a handful of
    // splits per channel, with margin). Beyond that, refuse and warn.
    constexpr int kMaxAutoExtendTracks = 256;
    if (trackIdx >= kMaxAutoExtendTracks) {
        if (warningsOut) {
            warningsOut->append(QStringLiteral(
                "Skipped event: track index %1 exceeds auto-extend cap (%2). "
                "The sender may be on a different protocol version.")
                    .arg(trackIdx).arg(kMaxAutoExtendTracks));
        }
        return false;
    }
    while (trackIdx >= file->numTracks()) {
        file->addTrack();
    }
    MidiTrack *track = file->track(trackIdx);
    if (!track) return false;

    QJsonArray single;
    single.append(je);
    QList<MidiEvent *> created;
    QStringList serializerErrors;
    bool ok = MidiEventSerializer::deserialize(single, file, track, channel, created, &serializerErrors);
    if (!ok && warningsOut) {
        for (const QString &e : serializerErrors) warningsOut->append(e);
    }
    if (ok && !author.isEmpty()) {
        for (MidiEvent *ev : created) {
            CollabService::instance()->registerEventAuthor(ev, author);
        }
    }
    return ok && !created.isEmpty();
}

}

PrApply::Result PrApply::apply(MidiFile *file,
                                const QList<QJsonObject> &hunks,
                                const QString &author,
                                const QString &message,
                                bool silent) {
    Result r;
    if (!file) {
        r.warnings.append(QStringLiteral("No file is currently open."));
        return r;
    }
    if (hunks.isEmpty()) {
        r.success = true;
        return r;
    }

    QString actionLabel = author.isEmpty()
        ? QStringLiteral("Merge PR: %1").arg(message)
        : QStringLiteral("Merge PR from %1: %2").arg(author, message);
    qCInfo(lanLog) << "PrApply: entering — hunks=" << hunks.size()
                   << "silent=" << silent << "label=" << actionLabel;
    if (!file->protocol()) {
        qCWarning(lanLog) << "PrApply: file->protocol() is null — aborting";
        r.warnings.append(QStringLiteral("File has no protocol — cannot apply."));
        return r;
    }
    file->protocol()->startNewAction(actionLabel);
    qCInfo(lanLog) << "PrApply: protocol action started";

    int hunkIdx = 0;
    for (const QJsonObject &hunk : hunks) {
        ++hunkIdx;
        qCDebug(lanLog) << "PrApply: hunk" << hunkIdx
                        << "removed=" << hunk.value(QStringLiteral("removed")).toArray().size()
                        << "modified=" << hunk.value(QStringLiteral("modified")).toArray().size()
                        << "added=" << hunk.value(QStringLiteral("added")).toArray().size();
        // 1. Removals — find + remove. Best-effort.
        QJsonArray removed = hunk.value(QStringLiteral("removed")).toArray();
        for (const QJsonValue &v : removed) {
            QJsonObject je = v.toObject();
            MidiEvent *ev = PrApply::findMatchingEvent(file, je);
            if (!ev) {
                r.skippedCount++;
                r.warnings.append(QStringLiteral("Could not find event to remove (channel %1, tick %2).")
                                      .arg(je.value(QStringLiteral("channel")).toInt())
                                      .arg(je.value(QStringLiteral("tick")).toInt()));
                continue;
            }
            MidiChannel *ch = file->channel(ev->channel());
            if (ch) {
                ch->removeEvent(ev);
                r.removedCount++;
            }
        }

        // 2. Modifications — remove-old + insert-new (preserves NoteOn/Off
        //    pairing semantics via the standard create/remove paths).
        QJsonArray modified = hunk.value(QStringLiteral("modified")).toArray();
        for (const QJsonValue &v : modified) {
            QJsonObject pair = v.toObject();
            QJsonObject before = pair.value(QStringLiteral("before")).toObject();
            QJsonObject after = pair.value(QStringLiteral("after")).toObject();
            MidiEvent *old = PrApply::findMatchingEvent(file, before);
            QString modType = after.value(QStringLiteral("type")).toString();
            if (!old) {
                qCWarning(lanLog) << "PrApply: modify could not find target"
                                  << "type=" << modType
                                  << "channel=" << before.value(QStringLiteral("channel")).toInt()
                                  << "tick=" << before.value(QStringLiteral("tick")).toInt();
            }
            if (old) {
                MidiChannel *ch = file->channel(old->channel());
                if (ch) ch->removeEvent(old);
            }
            if (insertEvent(file, after, author, &r.warnings)) {
                r.modifiedCount++;
            } else {
                qCWarning(lanLog) << "PrApply: modify insertEvent FAILED for type=" << modType;
                r.skippedCount++;
            }
        }

        // 3. Additions — pure inserts.
        QJsonArray added = hunk.value(QStringLiteral("added")).toArray();
        for (const QJsonValue &v : added) {
            QJsonObject je = v.toObject();
            if (insertEvent(file, je, author, &r.warnings)) {
                r.addedCount++;
            } else {
                r.skippedCount++;
            }
        }
    }

    qCInfo(lanLog) << "PrApply: ending protocol action";
    file->protocol()->endAction();
    r.success = true;
    qCInfo(lanLog) << "PrApply: action ended — added=" << r.addedCount
                   << "removed=" << r.removedCount
                   << "modified=" << r.modifiedCount
                   << "skipped=" << r.skippedCount;

    // Persist the merge so it appears in the receiver's collaboration
    // history as a "Merged from <author>: <message>" entry attributed to
    // the PR author (not the local user). Sequence:
    //   1. Mark a pending-merge so the next onFileSaved attributes the
    //      commit correctly (per Plan §10).
    //   2. Save the file. The existing save hook fires, hashes the
    //      result, and appends the history entry with the merged hunks
    //      already in the diff.
    //   3. If the file has no on-disk path yet (untitled), skip the
    //      save — the merge stays in-memory and will be persisted by
    //      whichever Save-As the user does next, still with the merge
    //      marker active.
    if (!silent && !file->path().isEmpty()) {
        CollabService::instance()->markPendingMerge(author, message);
        if (file->save(file->path())) {
            // file->save() bypasses MainWindow::save(), so the
            // CollabService::onFileSaved hook is not triggered automatically.
            // Invoke it directly so the pending-merge marker we just set is
            // consumed and the history entry is written.
            CollabService::instance()->onFileSaved(file, file->path());
            file->setSaved(true);
        } else {
            r.warnings.append(QStringLiteral("Merge succeeded but auto-save failed; "
                                             "press Ctrl+S to persist the merge."));
        }
    }
    return r;
}

PrApply::Result PrApply::applyInverted(MidiFile *file,
                                        const QList<QJsonObject> &hunks,
                                        const QString &author,
                                        const QString &message) {
    Result r;
    if (!file) {
        r.warnings.append(QStringLiteral("No file is currently open."));
        return r;
    }
    if (hunks.isEmpty()) {
        r.success = true;
        return r;
    }

    QString actionLabel = author.isEmpty()
        ? QStringLiteral("Revert: %1").arg(message)
        : QStringLiteral("Revert from %1: %2").arg(author, message);
    file->protocol()->startNewAction(actionLabel);

    for (const QJsonObject &hunk : hunks) {
        // Inverse of "added" — events the hunk wanted to add are now removed.
        QJsonArray added = hunk.value(QStringLiteral("added")).toArray();
        for (const QJsonValue &v : added) {
            QJsonObject je = v.toObject();
            MidiEvent *ev = PrApply::findMatchingEvent(file, je);
            if (!ev) {
                r.skippedCount++;
                continue;
            }
            MidiChannel *ch = file->channel(ev->channel());
            if (ch) {
                ch->removeEvent(ev);
                r.addedCount++;  // = un-added count
            }
        }

        // Inverse of "removed" — events the hunk wanted to remove are now restored.
        QJsonArray removed = hunk.value(QStringLiteral("removed")).toArray();
        for (const QJsonValue &v : removed) {
            QJsonObject je = v.toObject();
            if (insertEvent(file, je, QString(), &r.warnings)) {
                r.removedCount++;  // = re-added count
            } else {
                r.skippedCount++;
            }
        }

        // Inverse of "modified" — swap before↔after. Best-effort: when
        // the post-modification event no longer exists in the file
        // (user deleted it manually, another revert already handled it),
        // we skip the revert with a warning instead of resurrecting a
        // phantom "before" copy. This matches the skip-on-missing
        // behavior of apply()'s "removed" path.
        QJsonArray modified = hunk.value(QStringLiteral("modified")).toArray();
        for (const QJsonValue &v : modified) {
            QJsonObject pair = v.toObject();
            QJsonObject before = pair.value(QStringLiteral("before")).toObject();
            QJsonObject after = pair.value(QStringLiteral("after")).toObject();
            MidiEvent *cur = PrApply::findMatchingEvent(file, after);
            if (!cur) {
                r.skippedCount++;
                r.warnings.append(QStringLiteral(
                    "Could not revert modification at channel %1, tick %2: target event no longer exists.")
                        .arg(after.value(QStringLiteral("channel")).toInt())
                        .arg(after.value(QStringLiteral("tick")).toInt()));
                continue;
            }
            MidiChannel *ch = file->channel(cur->channel());
            if (ch) ch->removeEvent(cur);
            if (insertEvent(file, before, QString(), &r.warnings)) {
                r.modifiedCount++;
            } else {
                r.skippedCount++;
            }
        }
    }

    file->protocol()->endAction();
    r.success = true;
    return r;
}
