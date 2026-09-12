#include "DpiEnvOverrides.h"

#include <QtGlobal>

namespace {

QByteArray backupNameFor(const QByteArray &name) {
    return QByteArray(DpiEnvOverrides::kBackupPrefix) + name;
}

} // namespace

namespace DpiEnvOverrides {

QByteArrayList recordedNames() {
    QByteArrayList names;
    const QList<QByteArray> parts = qgetenv(kMarkerVariable).split(';');
    for (const QByteArray &part : parts) {
        if (!part.isEmpty() && !names.contains(part)) {
            names.append(part);
        }
    }
    return names;
}

void set(const QByteArray &name, const QByteArray &value) {
    const QByteArray backup = backupNameFor(name);
    // Only the FIRST override of a name records the inherited state. A repeated
    // set() in the same process, or in a child that skipped the restore, must
    // keep the original external value instead of backing up our own.
    if (!qEnvironmentVariableIsSet(backup.constData())) {
        if (qEnvironmentVariableIsSet(name.constData())) {
            qputenv(backup.constData(), QByteArray("1:") + qgetenv(name.constData()));
        } else {
            qputenv(backup.constData(), QByteArray("0"));
        }
        QByteArrayList names = recordedNames();
        if (!names.contains(name)) {
            names.append(name);
            qputenv(kMarkerVariable, names.join(';'));
        }
    }
    qputenv(name.constData(), value);
}

int restoreInherited() {
    const QByteArrayList names = recordedNames();
    for (const QByteArray &name : names) {
        const QByteArray backup = backupNameFor(name);
        const QByteArray inherited = qgetenv(backup.constData());
        if (inherited.startsWith("1:")) {
            qputenv(name.constData(), inherited.mid(2));
        } else {
            // "0" (was unset) - or a missing backup, treated the same way: the
            // variable is ours, there is nothing external to bring back.
            qunsetenv(name.constData());
        }
        qunsetenv(backup.constData());
    }
    qunsetenv(kMarkerVariable);
    return names.size();
}

} // namespace DpiEnvOverrides
