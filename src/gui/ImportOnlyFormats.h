/*
 * MidiEditor AI - import-only file formats (shared classifier).
 *
 * Formats MidiEditor can only IMPORT, never write back: MidiFile::save() always
 * emits Standard MIDI bytes, so saving over the original would corrupt it (and a
 * .sid / .gp5 / ... would no longer re-import). These must be saved as .mid.
 *
 * Single source of truth - used by MainWindow::save() (redirect to Save As),
 * the collaboration shared-copy path, and the unit tests. Keep in sync with the
 * importer dispatch in MainWindow::openFile().
 *
 * Header-only free functions so it stays trivially unit-testable (no GUI dep,
 * Qt Core only).
 */
#ifndef IMPORTONLYFORMATS_H
#define IMPORTONLYFORMATS_H

#include <QFileInfo>
#include <QSet>
#include <QString>

namespace ImportFormats {

/// True if \a suffix (no leading dot, any case) is an import-only format:
/// Guitar Pro (gtp/gp3-8/gpx/gp), MML, 3MLE, MusicXML (musicxml/xml/mxl),
/// MuseScore (mscz/mscx) or SID.
inline bool isImportOnlySuffix(const QString &suffix) {
    static const QSet<QString> kSuffixes = {
        QStringLiteral("gtp"),  QStringLiteral("gp3"),  QStringLiteral("gp4"),
        QStringLiteral("gp5"),  QStringLiteral("gp6"),  QStringLiteral("gp7"),
        QStringLiteral("gp8"),  QStringLiteral("gpx"),  QStringLiteral("gp"),
        QStringLiteral("mml"),  QStringLiteral("3mle"),
        QStringLiteral("musicxml"), QStringLiteral("xml"), QStringLiteral("mxl"),
        QStringLiteral("mscz"), QStringLiteral("mscx"), QStringLiteral("sid"),
    };
    return kSuffixes.contains(suffix.toLower());
}

/// True if \a path's extension is an import-only format (see isImportOnlySuffix).
inline bool isImportOnly(const QString &path) {
    return isImportOnlySuffix(QFileInfo(path).suffix());
}

} // namespace ImportFormats

#endif // IMPORTONLYFORMATS_H
