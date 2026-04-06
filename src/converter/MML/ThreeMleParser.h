#ifndef THREEMLEPARSER_H
#define THREEMLEPARSER_H

#include "MmlModels.h"
#include <QString>

class ThreeMleParser {
public:
    static MmlSong parse(const QString& text, int ticksPerQuarter = 480);
};

#endif // THREEMLEPARSER_H
