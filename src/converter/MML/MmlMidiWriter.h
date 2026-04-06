#ifndef MMLMIDIWRITER_H
#define MMLMIDIWRITER_H

#include "MmlModels.h"
#include <QByteArray>

class MmlMidiWriter {
public:
    static QByteArray write(const MmlSong& song);

private:
    static QByteArray writeVariableLength(int value);
    static QByteArray writeTrack(const MmlTrack& track, int channel,
                                 int tempo, bool includeTempo);
    static void appendUInt16BE(QByteArray& data, quint16 value);
    static void appendUInt32BE(QByteArray& data, quint32 value);
};

#endif // MMLMIDIWRITER_H
