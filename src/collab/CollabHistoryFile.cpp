/*
 * MidiEditor AI - CollabHistoryFile implementation.
 */

#include "CollabHistoryFile.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace {

QString stripKnownMidiSuffix(const QString &fileName) {
    static const QStringList suffixes = {".mid", ".midi", ".kar"};
    for (const QString &suffix : suffixes) {
        if (fileName.endsWith(suffix, Qt::CaseInsensitive)) {
            return fileName.left(fileName.size() - suffix.size());
        }
    }
    // Unknown extension: strip everything after the last dot, but only if
    // there is one. Otherwise fall through and append below.
    int lastDot = fileName.lastIndexOf('.');
    if (lastDot > 0) return fileName.left(lastDot);
    return fileName;
}

}

QString CollabHistoryFile::sidecarPathFor(const QString &midiPath) {
    if (midiPath.isEmpty()) return QString();
    QFileInfo fi(midiPath);
    QString stem = stripKnownMidiSuffix(fi.fileName());
    QString dir = fi.absolutePath();
    return dir + QLatin1Char('/') + stem + QStringLiteral(".midiedit-collab.json");
}

bool CollabHistoryFile::exists(const QString &midiPath) {
    QString p = sidecarPathFor(midiPath);
    if (p.isEmpty()) return false;
    return QFileInfo::exists(p);
}

CollabHistoryFile::CollabHistoryFile() = default;

void CollabHistoryFile::ensureSessionId() {
    if (!_sessionId.isEmpty()) return;
    _sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

int CollabHistoryFile::compactHistory(int keepLastN) {
    if (keepLastN < 0) keepLastN = 0;
    int stripped = 0;
    int total = _history.size();
    int firstKept = qMax(0, total - keepLastN);
    for (int i = 0; i < firstKept; ++i) {
        QJsonObject entry = _history.at(i).toObject();
        if (!entry.value(QStringLiteral("hunks")).toArray().isEmpty()) {
            entry.insert(QStringLiteral("hunks"), QJsonArray());
            _history.replace(i, entry);
            ++stripped;
        }
    }
    return stripped;
}

bool CollabHistoryFile::loadFromJson(const QJsonObject &obj) {
    _loaded = false;
    _sessionId.clear();
    _currentHead.clear();
    _lastSharedHead.clear();
    _branch = QStringLiteral("main");
    _history = QJsonArray();

    // We tolerate older schemaVersions by simply reading the fields we know.
    _sessionId = obj.value(QStringLiteral("sessionId")).toString();
    _currentHead = obj.value(QStringLiteral("currentHead")).toString();
    _lastSharedHead = obj.value(QStringLiteral("lastSharedHead")).toString();
    _branch = obj.value(QStringLiteral("branch")).toString(QStringLiteral("main"));
    _history = obj.value(QStringLiteral("history")).toArray();

    // Auto-upgrade legacy sidecars without a sessionId so subsequent saves
    // can carry one. The id is persisted on the next save() call.
    ensureSessionId();

    _loaded = true;
    return true;
}

bool CollabHistoryFile::load(const QString &midiPath) {
    _loaded = false;
    _sessionId.clear();
    _currentHead.clear();
    _lastSharedHead.clear();
    _branch = QStringLiteral("main");
    _history = QJsonArray();

    QString path = sidecarPathFor(midiPath);
    if (path.isEmpty()) return false;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;

    return loadFromJson(doc.object());
}

QJsonObject CollabHistoryFile::toJson() const {
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    obj.insert(QStringLiteral("sessionId"), _sessionId);
    obj.insert(QStringLiteral("currentHead"), _currentHead);
    obj.insert(QStringLiteral("lastSharedHead"), _lastSharedHead);
    obj.insert(QStringLiteral("branch"), _branch);
    obj.insert(QStringLiteral("history"), _history);
    return obj;
}

bool CollabHistoryFile::save(const QString &midiPath) const {
    QString path = sidecarPathFor(midiPath);
    if (path.isEmpty()) return false;

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QJsonDocument doc(toJson());
    out.write(doc.toJson(QJsonDocument::Indented));
    return out.commit();
}

void CollabHistoryFile::appendCommit(const QString &hash,
                                     const QString &parentHash,
                                     const QString &author,
                                     const QString &machineId,
                                     qint64 epochSeconds,
                                     const QString &message,
                                     const QJsonArray &hunks) {
    QJsonObject entry;
    entry.insert(QStringLiteral("hash"), hash);
    entry.insert(QStringLiteral("parentHash"), parentHash);
    entry.insert(QStringLiteral("author"), author);
    entry.insert(QStringLiteral("machineId"), machineId);
    entry.insert(QStringLiteral("ts"), epochSeconds);
    entry.insert(QStringLiteral("message"), message);
    entry.insert(QStringLiteral("hunks"), hunks);
    _history.append(entry);
    _currentHead = hash;
    _loaded = true;
}
