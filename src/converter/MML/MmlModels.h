#ifndef MMLMODELS_H
#define MMLMODELS_H

#include <QList>
#include <QString>

struct MmlNote {
    int pitch;        // MIDI note number (0-127)
    int startTick;    // absolute tick position
    int duration;     // in ticks
    int velocity;     // 0-127
};

struct MmlTrack {
    QString name;
    int instrument = 0;   // MIDI program number
    int channel = -1;     // MIDI channel (-1 = auto-assign)
    QList<MmlNote> notes;
};

struct MmlSong {
    int tempo = 120;           // BPM
    int ticksPerQuarter = 480;
    QList<MmlTrack> tracks;
};

#endif // MMLMODELS_H
