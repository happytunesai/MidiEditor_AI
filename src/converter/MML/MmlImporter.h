#ifndef MMLIMPORTER_H
#define MMLIMPORTER_H

#include <QString>

class MidiFile;

class MmlImporter {
public:
    static MidiFile* loadFile(QString path, bool* ok);
};

#endif // MMLIMPORTER_H
