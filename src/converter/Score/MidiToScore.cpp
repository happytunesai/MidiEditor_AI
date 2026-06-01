/*
 * MidiEditor AI - MIDI -> notation engraver implementation (see MidiToScore.h).
 *
 * The hard half of notation export: MIDI stores only pitch/tick/velocity, so we
 * reconstruct measures, note values, rests, ties, chords and enharmonic spelling.
 * v1 is monophonic-with-chords per part on a 1/16 grid (documented limits); it
 * always produces a fully-filled, valid score that opens in any notation app.
 */
#include "MidiToScore.h"

#include <QMap>
#include <QVector>
#include <algorithm>

namespace {

using namespace score;

// ---- meta lookups (active value at a tick) ---------------------------------

void timeSigAt(const QList<MetaTimeSig> &ts, int tick, int &num, int &den) {
    num = 4; den = 4;
    bool found = false;
    for (const MetaTimeSig &e : ts) {
        if (e.tick <= tick) { num = e.numerator; den = e.denominator; found = true; }
        else break;
    }
    if (!found && !ts.isEmpty()) { num = ts.first().numerator; den = ts.first().denominator; }
    if (num < 1) num = 4;
    if (den < 1) den = 4;
}

double bpmAt(const QList<MetaTempo> &tempos, int tick) {
    double bpm = 120.0;
    bool found = false;
    for (const MetaTempo &e : tempos) {
        if (e.tick <= tick) { bpm = e.bpm; found = true; }
        else break;
    }
    if (!found && !tempos.isEmpty()) bpm = tempos.first().bpm;
    return bpm > 0 ? bpm : 120.0;
}

void keyAt(const QList<MetaKey> &keys, int tick, int &fifths, bool &minor) {
    fifths = 0; minor = false;
    for (const MetaKey &e : keys) {
        if (e.tick <= tick) { fifths = e.fifths; minor = e.minor; }
        else break;
    }
}

// ---- enharmonic spelling ---------------------------------------------------

struct Spelled { char step; int alter; int octave; };

Spelled spell(int pitch, int fifths) {
    if (pitch < 0) pitch = 0;
    if (pitch > 127) pitch = 127;
    const int pc = ((pitch % 12) + 12) % 12;
    struct SA { char step; int alter; };
    static const SA sharp[12] = {
        {'C',0},{'C',1},{'D',0},{'D',1},{'E',0},{'F',0},
        {'F',1},{'G',0},{'G',1},{'A',0},{'A',1},{'B',0} };
    static const SA flat[12] = {
        {'C',0},{'D',-1},{'D',0},{'E',-1},{'E',0},{'F',0},
        {'G',-1},{'G',0},{'A',-1},{'A',0},{'B',-1},{'B',0} };
    const SA &t = (fifths < 0) ? flat[pc] : sharp[pc];
    return { t.step, t.alter, pitch / 12 - 1 };
}

// ---- note-value decomposition (binary, dotted, on a 1/16 grid) -------------

struct Piece { NoteType type; int dots; int len; };

// Greedy split of a duration (in divisions) into notatable values, largest
// first. Durations are multiples of div/4 after quantization, so the table
// {16,12,8,6,4,3,2,1} (in 1/16 units) tiles any value exactly.
QVector<Piece> decompose(int dur, int div) {
    const Piece table[] = {
        { NoteType::Whole,     0, 4 * div     },
        { NoteType::Half,      1, 3 * div     },
        { NoteType::Half,      0, 2 * div     },
        { NoteType::Quarter,   1, 3 * div / 2 },
        { NoteType::Quarter,   0, div         },
        { NoteType::Eighth,    1, 3 * div / 4 },
        { NoteType::Eighth,    0, div / 2     },
        { NoteType::Sixteenth, 0, div / 4     },
    };
    QVector<Piece> out;
    int rem = dur, guard = 0;
    while (rem > 0 && guard++ < 4096) {
        bool placed = false;
        for (const Piece &p : table) {
            if (p.len > 0 && p.len <= rem) { out.append(p); rem -= p.len; placed = true; break; }
        }
        if (!placed) break; // sub-1/16 remainder: drop (quantization rounds it away)
    }
    if (out.isEmpty()) out.append({ NoteType::Sixteenth, 0, div / 4 });
    return out;
}

// ---- per-part monophonic timeline (notes + rests, no overlaps) -------------

struct PitchVel { int pitch; int vel; };
struct Seg { int start; int end; bool isRest; QVector<PitchVel> pitches; };

QVector<Seg> buildTimeline(const QList<RawNote> &notes, int gridEnd, int div) {
    const int g = std::max(1, div / 4);
    auto q = [g](int t) { return ((t + g / 2) / g) * g; };

    // Quantize + group same-onset notes into chords (dur = shortest member).
    QMap<int, Seg> groups; // keyed by quantized start
    for (const RawNote &n : notes) {
        if (n.durTicks <= 0) continue;
        int qs = q(n.startTick);
        int qe = q(n.startTick + n.durTicks);
        if (qe <= qs) qe = qs + g;
        if (qs >= gridEnd) continue;
        if (qe > gridEnd) qe = gridEnd;
        auto it = groups.find(qs);
        if (it == groups.end()) {
            Seg s; s.start = qs; s.end = qe; s.isRest = false;
            s.pitches.append({ n.pitch, n.velocity });
            groups.insert(qs, s);
        } else {
            it->end = std::min(it->end, qe);          // chord ends with shortest member
            if (it->end <= it->start) it->end = it->start + g;
            it->pitches.append({ n.pitch, n.velocity });
        }
    }

    QVector<Seg> timeline;
    int prevEnd = 0;
    const QList<int> starts = groups.keys();
    for (int i = 0; i < starts.size(); ++i) {
        Seg grp = groups.value(starts[i]);
        const int nextStart = (i + 1 < starts.size()) ? starts[i + 1] : gridEnd;
        const int s = std::max(grp.start, prevEnd);
        if (s > prevEnd) timeline.append({ prevEnd, s, true, {} }); // gap -> rest
        int end = std::min(grp.end, nextStart);                     // clip to next onset (mono)
        if (end <= s) { prevEnd = std::max(prevEnd, s); continue; }
        std::sort(grp.pitches.begin(), grp.pitches.end(),
                  [](const PitchVel &a, const PitchVel &b) { return a.pitch < b.pitch; });
        grp.start = s; grp.end = end;
        timeline.append(grp);
        prevEnd = end;
    }
    if (prevEnd < gridEnd) timeline.append({ prevEnd, gridEnd, true, {} });
    return timeline;
}

Clef clefForNotes(const QList<RawNote> &notes) {
    if (notes.isEmpty()) return Clef::Treble;
    QVector<int> pitches;
    pitches.reserve(notes.size());
    for (const RawNote &n : notes) pitches.append(n.pitch);
    std::sort(pitches.begin(), pitches.end());
    const int median = pitches[pitches.size() / 2];
    return median >= 60 ? Clef::Treble : Clef::Bass;
}

// ---- the measure grid (shared by every part so they stay aligned) ----------

struct MGrid {
    int number, start, len, beats, beatType;
    bool timeChanged, keyChanged;
    int keyFifths; bool keyMinor;
    double tempoBpm; // >0 => emit
};

} // namespace

namespace score {

Score buildScore(const ScoreInput &inRaw) {
    ScoreInput in = inRaw;
    std::stable_sort(in.timeSigs.begin(), in.timeSigs.end(),
                     [](const MetaTimeSig &a, const MetaTimeSig &b) { return a.tick < b.tick; });
    std::stable_sort(in.tempos.begin(), in.tempos.end(),
                     [](const MetaTempo &a, const MetaTempo &b) { return a.tick < b.tick; });
    std::stable_sort(in.keySigs.begin(), in.keySigs.end(),
                     [](const MetaKey &a, const MetaKey &b) { return a.tick < b.tick; });

    Score s;
    s.title = in.title;
    s.divisions = in.divisions > 0 ? in.divisions : 480;
    const int div = s.divisions;

    // End tick: cover the configured length and the last note.
    int endTick = std::max(0, in.endTick);
    for (const RawPart &p : in.parts)
        for (const RawNote &n : p.notes)
            endTick = std::max(endTick, n.startTick + n.durTicks);

    // Build the measure grid (always at least one measure).
    QVector<MGrid> grid;
    int curTick = 0, mnum = 1;
    int prevNum = -1, prevDen = -1, prevFifths = 0; bool prevMinor = false;
    double prevBpm = -1.0;
    do {
        int num, den; timeSigAt(in.timeSigs, curTick, num, den);
        int len = num * 4 * div / den;
        if (len <= 0) len = 4 * div;
        int fifths; bool minor; keyAt(in.keySigs, curTick, fifths, minor);
        double bpm = bpmAt(in.tempos, curTick);

        MGrid m;
        m.number = mnum; m.start = curTick; m.len = len;
        m.beats = num; m.beatType = den;
        m.timeChanged = (prevNum != num || prevDen != den);
        m.keyChanged  = (mnum == 1) || (prevFifths != fifths || prevMinor != minor);
        m.keyFifths = fifths; m.keyMinor = minor;
        m.tempoBpm = (mnum == 1 || bpm != prevBpm) ? bpm : 0.0;
        grid.append(m);

        prevNum = num; prevDen = den; prevFifths = fifths; prevMinor = minor; prevBpm = bpm;
        curTick += len; ++mnum;
    } while (curTick < endTick);
    const int gridEnd = grid.last().start + grid.last().len;

    for (int pi = 0; pi < in.parts.size(); ++pi) {
        const RawPart &rp = in.parts[pi];
        ScorePart part;
        part.name = rp.name;
        part.channel = rp.channel;
        part.program = rp.program;
        part.clef = clefForNotes(rp.notes);

        const QVector<Seg> timeline = buildTimeline(rp.notes, gridEnd, div);

        // Engrave each segment into per-measure voice lists.
        QVector<QList<ScoreEvent>> voices(grid.size());
        for (const Seg &seg : timeline) {
            // Pieces across the measures this segment spans (for tie linking).
            struct PMP { int mi; Piece piece; };
            QVector<PMP> pieces;
            for (int mi = 0; mi < grid.size(); ++mi) {
                const int ms = grid[mi].start, me = grid[mi].start + grid[mi].len;
                const int a = std::max(seg.start, ms), b = std::min(seg.end, me);
                if (a >= b) continue;
                for (const Piece &pc : decompose(b - a, div)) pieces.append({ mi, pc });
            }
            const int n = pieces.size();
            for (int idx = 0; idx < n; ++idx) {
                const PMP &pp = pieces[idx];
                ScoreEvent ev;
                ev.durDivs = pp.piece.len;
                ev.type = pp.piece.type;
                ev.dots = pp.piece.dots;
                if (seg.isRest) {
                    ev.isRest = true;
                    voices[pp.mi].append(ev);
                    continue;
                }
                ev.isRest = false;
                ev.tieStart = (idx < n - 1);
                ev.tieStop  = (idx > 0);
                const int fifths = grid[pp.mi].keyFifths;
                const Spelled sp0 = spell(seg.pitches[0].pitch, fifths);
                ev.step = sp0.step; ev.alter = sp0.alter; ev.octave = sp0.octave;
                ev.velocity = seg.pitches[0].vel;
                voices[pp.mi].append(ev);
                for (int k = 1; k < seg.pitches.size(); ++k) {
                    ScoreEvent cv = ev;
                    cv.isChord = true;
                    const Spelled sk = spell(seg.pitches[k].pitch, fifths);
                    cv.step = sk.step; cv.alter = sk.alter; cv.octave = sk.octave;
                    cv.velocity = seg.pitches[k].vel;
                    voices[pp.mi].append(cv);
                }
            }
        }

        for (int mi = 0; mi < grid.size(); ++mi) {
            ScoreMeasure m;
            m.number = grid[mi].number;
            m.divLen = grid[mi].len;
            m.beats = grid[mi].beats;
            m.beatType = grid[mi].beatType;
            m.timeChanged = grid[mi].timeChanged;
            m.keyChanged = grid[mi].keyChanged;
            m.keyFifths = grid[mi].keyFifths;
            m.keyMinor = grid[mi].keyMinor;
            m.tempoBpm = (pi == 0) ? grid[mi].tempoBpm : 0.0; // tempo once, on part 1
            m.voice = voices[mi];
            part.measures.append(m);
        }
        s.parts.append(part);
    }

    return s;
}

} // namespace score
