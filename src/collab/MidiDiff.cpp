/*
 * MidiEditor AI - MidiDiff implementation.
 */

#include "MidiDiff.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QPair>
#include <QString>
#include <algorithm>

namespace {
Q_LOGGING_CATEGORY(midiDiffLog, "midieditor.collab.diff")
}

namespace {

constexpr int kBeatsPerMeasure4_4 = 4;
constexpr int kMeasuresPerHunkGap = 4;  // start a new hunk when gap > 4 measures

// One change entry, used for sorting/grouping before serialization.
struct Change {
    enum Kind { Removed, Added, Modified };
    Kind kind;
    int channel;
    int track;
    int tick;
    QJsonObject before;   // parent event (Removed / Modified)
    QJsonObject after;    // current event (Added / Modified)
};

int extractInt(const QJsonObject &obj, const char *key, int fallback = -1) {
    if (!obj.contains(QLatin1String(key))) return fallback;
    return obj.value(QLatin1String(key)).toInt(fallback);
}

QString sanitizeForKey(const QJsonObject &obj) {
    // We don't want the per-call `id` field to participate in identity
    // or equality — it's a sequence number from the serializer.
    QJsonObject clean = obj;
    clean.remove(QStringLiteral("id"));
    return QString::fromUtf8(QJsonDocument(clean).toJson(QJsonDocument::Compact));
}

int measureNumber(int tick, int ticksPerQuarter) {
    if (ticksPerQuarter <= 0) return 0;
    int ticksPerMeasure = ticksPerQuarter * kBeatsPerMeasure4_4;
    if (ticksPerMeasure <= 0) return 0;
    return tick / ticksPerMeasure;
}

}

QString MidiDiff::identityKey(const QJsonObject &e) {
    QString type = e.value(QStringLiteral("type")).toString();
    if (type.isEmpty()) return QString();

    int channel = extractInt(e, "channel");
    int track = extractInt(e, "track");
    int tick = extractInt(e, "tick");

    if (type == QLatin1String("note")) {
        int note = extractInt(e, "note");
        return QStringLiteral("note|%1|%2|%3|%4").arg(channel).arg(track).arg(tick).arg(note);
    }
    if (type == QLatin1String("cc")) {
        int control = extractInt(e, "control");
        return QStringLiteral("cc|%1|%2|%3|%4").arg(channel).arg(track).arg(tick).arg(control);
    }
    if (type == QLatin1String("pitch_bend")) {
        return QStringLiteral("pb|%1|%2|%3").arg(channel).arg(track).arg(tick);
    }
    if (type == QLatin1String("program_change")) {
        return QStringLiteral("prog|%1|%2|%3").arg(channel).arg(track).arg(tick);
    }
    // Plan §11.10j — meta + aftertouch identity keys. Channel is part of
    // the key (not collapsed) so meta channels 16/17/18 stay separate
    // from each other and from instrument-channel events at the same tick.
    if (type == QLatin1String("tempo")) {
        return QStringLiteral("tempo|%1|%2|%3").arg(channel).arg(track).arg(tick);
    }
    if (type == QLatin1String("time_sig")) {
        return QStringLiteral("time_sig|%1|%2|%3").arg(channel).arg(track).arg(tick);
    }
    if (type == QLatin1String("key_sig")) {
        return QStringLiteral("key_sig|%1|%2|%3").arg(channel).arg(track).arg(tick);
    }
    if (type == QLatin1String("text")) {
        // Text subtype is part of identity — replacing a lyric should not
        // collide with replacing a marker at the same tick.
        int textType = extractInt(e, "textType");
        return QStringLiteral("text|%1|%2|%3|%4").arg(channel).arg(track).arg(tick).arg(textType);
    }
    if (type == QLatin1String("chan_pressure")) {
        return QStringLiteral("chan_pressure|%1|%2|%3").arg(channel).arg(track).arg(tick);
    }
    if (type == QLatin1String("key_pressure")) {
        int note = extractInt(e, "note");
        return QStringLiteral("key_pressure|%1|%2|%3|%4").arg(channel).arg(track).arg(tick).arg(note);
    }
    // Unknown type: excluded from the diff (returns "").
    return QString();
}

bool MidiDiff::eventsEqual(const QJsonObject &a, const QJsonObject &b) {
    return sanitizeForKey(a) == sanitizeForKey(b);
}

QJsonArray MidiDiff::compute(const QJsonArray &parent,
                             const QJsonArray &current,
                             int ticksPerQuarter) {
    // Index parent and current by identity key. Plan §11.10i: events
    // that legitimately stack at the same tick (multiple program-changes
    // at tick 0 in GP imports, layered NoteOn articulations) used to
    // collapse into one entry — second-insert overwrote first, silently
    // dropping the second event from the diff. Now we append a per-base-
    // key collision index (`|#0`, `|#1`, …) so each stacked event gets
    // its own slot. The sub-key is derived from the event payload (see
    // buildKeyMap) rather than from its position in the snapshot, so a
    // stack member removed or reordered in the middle no longer shifts
    // the slots of everything behind it.
    // Count each identity tuple first. A purely positional collision
    // index (`|#0`, `|#1`, ...) only lines up across the two sides when
    // a stack is appended to or truncated at its end: deleting or
    // reordering any earlier member shifts every later index, so
    // unrelated events get paired and the diff emits a bogus
    // modified+removed chain that corrupts the receiver. Events whose
    // tuple is unique keep the plain base key (so an ordinary payload
    // edit still pairs up as "modified"); stacked events are
    // disambiguated by their payload instead of by snapshot position.
    //
    // "Stacked" is decided per tuple over BOTH snapshots (max of the two
    // per-side counts, not the sum): a stack that shrinks to one member
    // must still be payload-keyed on the shrunk side, or the survivor
    // gets the plain key and the diff reports removed+removed+added
    // instead of one removal. Using the sum would wrongly mark every
    // ordinary payload edit as stacked.
    auto countBases = [](const QJsonArray &events) {
        QHash<QString, int> counts;
        for (const QJsonValue &v : events) {
            QString base = identityKey(v.toObject());
            if (!base.isEmpty()) ++counts[base];
        }
        return counts;
    };
    QHash<QString, int> stackedCount = countBases(parent);
    {
        const QHash<QString, int> currentCount = countBases(current);
        for (auto it = currentCount.cbegin(); it != currentCount.cend(); ++it) {
            int &n = stackedCount[it.key()];
            n = qMax(n, it.value());
        }
    }
    auto buildKeyMap = [&stackedCount](const QJsonArray &events,
                                       QHash<QString, QJsonObject> &outByKey) {
        QHash<QString, int> nextIndex;
        outByKey.reserve(events.size());
        for (const QJsonValue &v : events) {
            QJsonObject obj = v.toObject();
            QString base = identityKey(obj);
            if (base.isEmpty()) continue;
            QString key = base;
            if (stackedCount.value(base) > 1) {
                // Payload-derived sub-key; the trailing index only ever
                // separates genuinely identical events (or a hash
                // collision, which degrades to the old positional
                // behaviour). The keys are local to this call - they
                // never go on the wire.
                QString payloadKey = base + QLatin1Char('|')
                    + QString::number(qHash(sanitizeForKey(obj), 0), 16);
                int n = nextIndex[payloadKey]++;
                key = (n == 0) ? payloadKey
                               : QStringLiteral("%1|#%2").arg(payloadKey).arg(n);
            }
            outByKey.insert(key, obj);
        }
    };
    QHash<QString, QJsonObject> parentByKey;
    QHash<QString, QJsonObject> currentByKey;
    buildKeyMap(parent, parentByKey);
    buildKeyMap(current, currentByKey);
    // Summary-only log line, once per compute() call. Earlier version
    // logged one line per stacked event at INF level -
    // 300 k lines / 5-min session on GP-imported files, tanked disk
    // and responsiveness. Now collapsed to a single debug-level
    // line (still useful for spotting stacked-event files in CI).
    {
        int collisionCount = 0;
        int collidingKeys = 0;
        for (auto it = stackedCount.cbegin(); it != stackedCount.cend(); ++it) {
            if (it.value() > 1) {
                ++collidingKeys;
                collisionCount += it.value();
            }
        }
        if (collisionCount > 0) {
            qCDebug(midiDiffLog).noquote()
                << "stacked events:" << collisionCount
                << "across" << collidingKeys << "keys";
        }
    }

    QList<Change> changes;

    // Pass 1: walk parent, classify against current.
    for (auto it = parentByKey.cbegin(); it != parentByKey.cend(); ++it) {
        const QString &key = it.key();
        const QJsonObject &p = it.value();
        auto cIt = currentByKey.constFind(key);
        if (cIt == currentByKey.cend()) {
            // Parent-only → removed.
            Change c;
            c.kind = Change::Removed;
            c.channel = extractInt(p, "channel");
            c.track = extractInt(p, "track");
            c.tick = extractInt(p, "tick");
            c.before = p;
            changes.append(c);
        } else if (!eventsEqual(p, cIt.value())) {
            // Both sides, but payload differs → modified.
            Change c;
            c.kind = Change::Modified;
            c.channel = extractInt(p, "channel");
            c.track = extractInt(p, "track");
            c.tick = extractInt(p, "tick");
            c.before = p;
            c.after = cIt.value();
            changes.append(c);
        }
    }

    // Pass 2: walk current, find additions.
    for (auto it = currentByKey.cbegin(); it != currentByKey.cend(); ++it) {
        const QString &key = it.key();
        if (parentByKey.contains(key)) continue;
        const QJsonObject &cur = it.value();
        Change c;
        c.kind = Change::Added;
        c.channel = extractInt(cur, "channel");
        c.track = extractInt(cur, "track");
        c.tick = extractInt(cur, "tick");
        c.after = cur;
        changes.append(c);
    }

    if (changes.isEmpty()) return QJsonArray();

    // Group by (channel, track). For each group, sort by tick, then split
    // contiguous tick ranges into hunks where the gap exceeds
    // `gapTicks`.
    using GroupKey = QPair<int, int>;  // (channel, track)
    QHash<GroupKey, QList<Change>> groups;
    for (const Change &c : changes) {
        groups[{c.channel, c.track}].append(c);
    }

    int gapTicks = ticksPerQuarter > 0
                       ? ticksPerQuarter * kBeatsPerMeasure4_4 * kMeasuresPerHunkGap
                       : 0;

    QJsonArray hunks;

    // Stable iteration order: sort group keys by (channel, track).
    QList<GroupKey> orderedKeys = groups.keys();
    std::sort(orderedKeys.begin(), orderedKeys.end());

    for (const GroupKey &k : orderedKeys) {
        QList<Change> &g = groups[k];
        std::sort(g.begin(), g.end(), [](const Change &a, const Change &b) {
            return a.tick < b.tick;
        });

        // Walk the sorted list and emit hunks split by gap.
        int hunkStart = 0;  // index into g
        for (int i = 1; i <= g.size(); ++i) {
            bool split = (i == g.size());
            if (!split && gapTicks > 0) {
                int prevTick = g[i - 1].tick;
                int curTick = g[i].tick;
                if (curTick - prevTick > gapTicks) split = true;
            }
            if (!split) continue;

            // Build a hunk for g[hunkStart .. i-1].
            QJsonArray removed, added, modified;
            int tickStart = g[hunkStart].tick;
            int tickEnd = g[i - 1].tick;
            for (int j = hunkStart; j < i; ++j) {
                const Change &c = g[j];
                switch (c.kind) {
                case Change::Removed:
                    removed.append(c.before);
                    break;
                case Change::Added:
                    added.append(c.after);
                    break;
                case Change::Modified: {
                    QJsonObject pair;
                    pair.insert(QStringLiteral("before"), c.before);
                    pair.insert(QStringLiteral("after"), c.after);
                    modified.append(pair);
                    break;
                }
                }
            }

            QJsonObject scope;
            scope.insert(QStringLiteral("channel"), k.first);
            scope.insert(QStringLiteral("track"), k.second);
            scope.insert(QStringLiteral("tickStart"), tickStart);
            scope.insert(QStringLiteral("tickEnd"), tickEnd);
            scope.insert(QStringLiteral("measureStart"), measureNumber(tickStart, ticksPerQuarter));
            scope.insert(QStringLiteral("measureEnd"), measureNumber(tickEnd, ticksPerQuarter));

            QJsonObject hunk;
            hunk.insert(QStringLiteral("scope"), scope);
            hunk.insert(QStringLiteral("removed"), removed);
            hunk.insert(QStringLiteral("added"), added);
            hunk.insert(QStringLiteral("modified"), modified);
            hunks.append(hunk);

            hunkStart = i;
        }
    }

    return hunks;
}
