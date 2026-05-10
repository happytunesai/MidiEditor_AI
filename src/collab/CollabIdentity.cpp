/*
 * MidiEditor AI - CollabIdentity implementation.
 */

#include "CollabIdentity.h"

#include <QSettings>
#include <QString>
#include <QUuid>

namespace {

QString readOsUserName() {
    QString name = qEnvironmentVariable("USERNAME");
    if (name.isEmpty()) name = qEnvironmentVariable("USER");
    return name;
}

QString sanitizeUuid(const QUuid &id) {
    QString s = id.toString(QUuid::WithoutBraces);
    return s;
}

}

QString CollabIdentity::displayName() {
    QSettings settings("MidiEditor", "NONE");
    QString name = settings.value("Collab/identity/displayName").toString();
    if (name.isEmpty()) {
        name = readOsUserName();
        if (name.isEmpty()) name = QStringLiteral("Anonymous");
    }
    return name;
}

void CollabIdentity::setDisplayName(const QString &name) {
    QSettings settings("MidiEditor", "NONE");
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        settings.remove("Collab/identity/displayName");
    } else {
        settings.setValue("Collab/identity/displayName", trimmed);
    }
}

QString CollabIdentity::machineId() {
    QSettings settings("MidiEditor", "NONE");
    QString id = settings.value("Collab/identity/machineId").toString();
    if (id.isEmpty()) {
        id = sanitizeUuid(QUuid::createUuid());
        settings.setValue("Collab/identity/machineId", id);
    }
    return id;
}

void CollabIdentity::regenerateMachineId() {
    QSettings settings("MidiEditor", "NONE");
    settings.setValue("Collab/identity/machineId", sanitizeUuid(QUuid::createUuid()));
}

QString CollabIdentity::displayLabel() {
    QString id = machineId();
    QString prefix = id.left(8);
    return QStringLiteral("%1 (%2)").arg(displayName(), prefix);
}
