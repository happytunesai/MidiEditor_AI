/*
 * MidiEditor AI - MidiHash implementation.
 */

#include "MidiHash.h"

#include <QCryptographicHash>
#include <QFile>

QString MidiHash::sha256OfFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) {
        f.close();
        return QString();
    }
    f.close();
    return QString::fromLatin1(hash.result().toHex());
}
