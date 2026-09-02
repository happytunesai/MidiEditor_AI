#include "MmlImporter.h"
#include "MmlModels.h"
#include "MmlConverter.h"
#include "MmlMidiWriter.h"
#include "ThreeMleParser.h"

#include "../../midi/MidiFile.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QTextStream>

MidiFile* MmlImporter::loadFile(QString path, bool* ok) {
    if (ok)
        *ok = false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return nullptr;

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    if (content.trimmed().isEmpty())
        return nullptr;

    // Detect format: .3mle extension or [Settings] section → 3MLE format
    QString lower = path.toLower();
    bool is3Mle = lower.endsWith(".3mle") ||
                  content.contains("[Settings]", Qt::CaseInsensitive);

    MmlSong song;
    if (is3Mle)
        song = ThreeMleParser::parse(content);
    else
        song = MmlConverter::convert(content);

    if (song.tracks.isEmpty())
        return nullptr;

    // Convert MML song to standard MIDI bytes
    QByteArray midiBytes = MmlMidiWriter::write(song);
    if (midiBytes.isEmpty())
        return nullptr;

    // Write bytes to a temporary file and load via MidiFile
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(false);
    if (!tempFile.open())
        return nullptr;

    // A short write (full volume, locked temp dir) would leave a truncated
    // .mid that MidiFile cannot parse - bail before constructing it.
    if (tempFile.write(midiBytes) != midiBytes.size()) {
        QString badPath = tempFile.fileName();
        tempFile.close();
        QFile::remove(badPath);
        return nullptr;
    }
    QString tempPath = tempFile.fileName();
    tempFile.close();

    bool midiOk = false;
    MidiFile* midiFile = new MidiFile(tempPath, &midiOk);
    QFile::remove(tempPath);

    if (!midiOk || !midiFile) {
        // Do NOT delete a MidiFile whose ctor reported failure: its early
        // returns leave the object half-built (only the in-class initializers
        // in MidiFile.h keep ~MidiFile from deleting indeterminate pointers).
        // Leak it, matching the class's own documented cleanup policy.
        return nullptr;
    }

    midiFile->setPath(path);
    if (ok)
        *ok = true;
    return midiFile;
}
