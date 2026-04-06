#ifndef MMLLEXER_H
#define MMLLEXER_H

#include <QList>
#include <QString>

enum class MmlTokenType {
    NoteLetter,    // c, d, e, f, g, a, b
    Rest,          // r
    Sharp,         // + or #
    Flat,          // -
    Number,        // digit sequence
    Dot,           // .
    Tie,           // &
    OctaveCmd,     // o
    OctaveUp,      // >
    OctaveDown,    // <
    TempoCmd,      // t
    LengthCmd,     // l
    VolumeCmd,     // v
    InstrumentCmd, // @
    LoopStart,     // [
    LoopEnd,       // ]
    EndOfTrack
};

struct MmlToken {
    MmlTokenType type;
    QString value;
};

class MmlLexer {
public:
    static QList<MmlToken> tokenize(const QString& mml);
};

#endif // MMLLEXER_H
