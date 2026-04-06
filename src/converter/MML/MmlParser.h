#ifndef MMLPARSER_H
#define MMLPARSER_H

#include "MmlModels.h"
#include "MmlLexer.h"

class MmlParser {
public:
    static MmlTrack parse(const QList<MmlToken>& tokens, int ticksPerQuarter);

private:
    static int noteOffset(QChar letter);
};

#endif // MMLPARSER_H
