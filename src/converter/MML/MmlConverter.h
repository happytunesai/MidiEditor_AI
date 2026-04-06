#ifndef MMLCONVERTER_H
#define MMLCONVERTER_H

#include "MmlModels.h"
#include <QString>

class MmlConverter {
public:
    static MmlSong convert(const QString& mmlText, int ticksPerQuarter = 480);
    static int extractTempo(const QString& mmlText);
};

#endif // MMLCONVERTER_H
