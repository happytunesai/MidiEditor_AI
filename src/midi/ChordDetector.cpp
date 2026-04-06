#include "ChordDetector.h"

#include <QSet>
#include <algorithm>

static const char *noteNames[] = {"C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B"};

QString ChordDetector::getNoteName(int note, bool includeOctave) {
    int pc = note % 12;
    if (includeOctave) {
        int octave = (note / 12) - 1;
        return QString("%1%2").arg(noteNames[pc]).arg(octave);
    }
    return QString(noteNames[pc]);
}

QString ChordDetector::identifyChordType(int root, const QList<int> &intervals) {
    // intervals are semitones relative to root, sorted, no duplicates
    if (intervals.size() == 1) {
        // Two-note interval
        int i = intervals[0];
        if (i == 7) return "5";  // power chord
    }
    if (intervals.size() == 2) {
        int a = intervals[0], b = intervals[1];
        if (a == 4 && b == 7) return "maj";
        if (a == 3 && b == 7) return "m";
        if (a == 3 && b == 6) return "dim";
        if (a == 4 && b == 8) return "aug";
        if (a == 2 && b == 7) return "sus2";
        if (a == 5 && b == 7) return "sus4";
    }
    if (intervals.size() == 3) {
        int a = intervals[0], b = intervals[1], c = intervals[2];
        if (a == 4 && b == 7 && c == 11) return "maj7";
        if (a == 3 && b == 7 && c == 10) return "m7";
        if (a == 4 && b == 7 && c == 10) return "7";
        if (a == 3 && b == 6 && c == 10) return "m7b5";
        if (a == 3 && b == 6 && c == 9)  return "dim7";
        if (a == 4 && b == 7 && c == 9)  return "6";
        if (a == 3 && b == 7 && c == 9)  return "m6";
        if (a == 2 && b == 7 && c == 10) return "7sus2";
        if (a == 5 && b == 7 && c == 10) return "7sus4";
    }
    if (intervals.size() == 4) {
        int a = intervals[0], b = intervals[1], c = intervals[2], d = intervals[3];
        if (a == 2 && b == 4 && c == 7 && d == 10) return "9";
        if (a == 2 && b == 4 && c == 7 && d == 11) return "maj9";
        if (a == 2 && b == 3 && c == 7 && d == 10) return "m9";
        if (a == 4 && b == 7 && c == 10 && d == 14 % 12) return "9";
    }
    return QString();
}

QString ChordDetector::detectChord(QList<int> midiNotes) {
    if (midiNotes.isEmpty()) return QString();

    // Extract unique pitch classes
    QSet<int> pcSet;
    for (int note : midiNotes) {
        pcSet.insert(note % 12);
    }
    QList<int> pitchClasses = pcSet.values();
    std::sort(pitchClasses.begin(), pitchClasses.end());

    if (pitchClasses.size() == 1) {
        return getNoteName(pitchClasses[0]);
    }

    // Try each pitch class as root
    QString bestMatch;
    int bestRoot = -1;

    for (int root : pitchClasses) {
        QList<int> intervals;
        for (int pc : pitchClasses) {
            if (pc == root) continue;
            int interval = (pc - root + 12) % 12;
            intervals.append(interval);
        }
        std::sort(intervals.begin(), intervals.end());

        QString type = identifyChordType(root, intervals);
        if (!type.isEmpty()) {
            // Prefer the match where root is the lowest sounding note
            int lowestNote = *std::min_element(midiNotes.begin(), midiNotes.end());
            if (lowestNote % 12 == root) {
                return getNoteName(root) + type;
            }
            if (bestMatch.isEmpty()) {
                bestMatch = getNoteName(root) + type;
                bestRoot = root;
            }
        }
    }

    if (!bestMatch.isEmpty()) {
        // Check if it's an inversion — show slash chord
        int lowestNote = *std::min_element(midiNotes.begin(), midiNotes.end());
        int bassPC = lowestNote % 12;
        if (bassPC != bestRoot) {
            return bestMatch + "/" + getNoteName(bassPC);
        }
        return bestMatch;
    }

    // No chord match — just list note names
    QStringList names;
    for (int note : midiNotes) {
        names.append(getNoteName(note, true));
    }
    // Remove duplicates and sort
    names.removeDuplicates();
    return names.join(", ");
}
