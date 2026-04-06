#ifndef CHORDDETECTOR_H_
#define CHORDDETECTOR_H_

#include <QList>
#include <QString>

class ChordDetector {
public:
    static QString detectChord(QList<int> midiNotes);
    static QString getNoteName(int note, bool includeOctave = false);

private:
    static QString identifyChordType(int root, const QList<int> &intervals);
};

#endif // CHORDDETECTOR_H_
