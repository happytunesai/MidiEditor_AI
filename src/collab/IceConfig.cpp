/*
 * MidiEditor AI - IceConfig implementation.
 */

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include "IceConfig.h"

#include <QSettings>

QStringList IceConfig::googleDefaults() {
    return {
        QStringLiteral("stun:stun.l.google.com:19302"),
        QStringLiteral("stun:stun.l.google.com:5349"),
        QStringLiteral("stun:stun1.l.google.com:3478"),
        QStringLiteral("stun:stun1.l.google.com:5349"),
        QStringLiteral("stun:stun2.l.google.com:19302"),
        QStringLiteral("stun:stun2.l.google.com:5349"),
        QStringLiteral("stun:stun3.l.google.com:3478"),
        QStringLiteral("stun:stun3.l.google.com:5349"),
        QStringLiteral("stun:stun4.l.google.com:19302"),
        QStringLiteral("stun:stun4.l.google.com:5349"),
    };
}

QStringList IceConfig::load() {
    QSettings s("MidiEditor", "NONE");
    QString blob = s.value("Collab/lan/iceServers").toString();
    if (blob.trimmed().isEmpty()) return googleDefaults();

    QStringList out;
    for (const QString &raw : blob.split(QChar('\n'), Qt::SkipEmptyParts)) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QChar('#'))) continue;
        out.append(line);
    }
    if (out.isEmpty()) return googleDefaults();
    return out;
}

void IceConfig::save(const QStringList &uris) {
    QSettings s("MidiEditor", "NONE");
    s.setValue("Collab/lan/iceServers", uris.join(QChar('\n')));
}

#endif // MIDIEDITOR_WEBRTC_ENABLED
