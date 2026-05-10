/*
 * MidiEditor AI - PrBundle implementation.
 */

#include "PrBundle.h"

#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QStringList>

namespace {

QByteArray zlibCompressForToken(const QByteArray &raw) {
    // qCompress wraps the zlib stream in a 4-byte big-endian length
    // header. That's fine for round-trips inside MidiEditor (qUncompress
    // expects it) but is non-standard for external tools. Since both
    // ends of the smart-paste token are MidiEditor, we keep qCompress
    // for simplicity.
    return qCompress(raw, /*compressionLevel=*/9);
}

// Token decompression caps. Smart-paste tokens come from arbitrary
// clipboard text — including potentially attacker-supplied chat
// content — so we must never trust the 4-byte expanded-size header
// that qUncompress reads at face value. A 64-byte token claiming to
// expand to 4 GB would otherwise OOM-crash any peer who pastes it.
constexpr int kMaxCompressedTokenBytes = 8 * 1024 * 1024;   // 8 MB compressed
constexpr quint32 kMaxExpandedTokenBytes = 64u * 1024u * 1024u;  // 64 MB expanded

QByteArray zlibDecompressFromToken(const QByteArray &compressed) {
    if (compressed.size() > kMaxCompressedTokenBytes) return QByteArray();
    if (compressed.size() < 4) return QByteArray();
    // qCompress's leading 4 bytes are the big-endian advertised expanded
    // size. Reject before letting qUncompress allocate based on it.
    quint32 expectedSize =
        (static_cast<quint8>(compressed[0]) << 24) |
        (static_cast<quint8>(compressed[1]) << 16) |
        (static_cast<quint8>(compressed[2]) << 8)  |
         static_cast<quint8>(compressed[3]);
    if (expectedSize > kMaxExpandedTokenBytes) return QByteArray();
    return qUncompress(compressed);
}

}

bool PrBundle::isValid() const {
    return !sessionId.isEmpty() && !author.isEmpty() && timestamp > 0;
}

// ---------------------------------------------------------------------
// Bundle JSON form
// ---------------------------------------------------------------------

QByteArray PrBundle::toBundleJson() const {
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    obj.insert(QStringLiteral("sessionId"), sessionId);
    obj.insert(QStringLiteral("author"), author);
    obj.insert(QStringLiteral("machineId"), machineId);
    obj.insert(QStringLiteral("parentHash"), parentHash);
    obj.insert(QStringLiteral("ts"), timestamp);
    obj.insert(QStringLiteral("message"), message);
    obj.insert(QStringLiteral("hunks"), hunks);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

bool PrBundle::saveBundleToFile(const QString &path) const {
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (out.write(toBundleJson()) < 0) {
        out.cancelWriting();
        return false;
    }
    return out.commit();
}

PrBundle PrBundle::fromBundleJson(const QByteArray &bytes, QString *errorOut) {
    PrBundle b;
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut) *errorOut = QStringLiteral("Bundle JSON parse error: %1").arg(err.errorString());
        return b;
    }
    QJsonObject obj = doc.object();
    int sv = obj.value(QStringLiteral("schemaVersion")).toInt(0);
    if (sv > kSchemaVersion) {
        if (errorOut) *errorOut = QStringLiteral("Bundle uses schema version %1; this build supports up to %2.").arg(sv).arg(kSchemaVersion);
        return b;
    }
    b.sessionId = obj.value(QStringLiteral("sessionId")).toString();
    b.author = obj.value(QStringLiteral("author")).toString();
    b.machineId = obj.value(QStringLiteral("machineId")).toString();
    b.parentHash = obj.value(QStringLiteral("parentHash")).toString();
    b.timestamp = static_cast<qint64>(obj.value(QStringLiteral("ts")).toDouble());
    b.message = obj.value(QStringLiteral("message")).toString();
    b.hunks = obj.value(QStringLiteral("hunks")).toArray();
    if (!b.isValid() && errorOut) {
        *errorOut = QStringLiteral("Bundle is missing required fields (sessionId, author, ts).");
    }
    return b;
}

PrBundle PrBundle::fromBundleFile(const QString &path, QString *errorOut) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = QStringLiteral("Cannot open bundle file: %1").arg(path);
        return PrBundle();
    }
    QByteArray bytes = f.readAll();
    f.close();
    return fromBundleJson(bytes, errorOut);
}

// ---------------------------------------------------------------------
// Smart-paste tokens
// ---------------------------------------------------------------------

QString PrBundle::toInlineToken() const {
    QByteArray compressed = zlibCompressForToken(toBundleJson());
    QByteArray b64 = compressed.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(kTokenSchemePrefix) + sessionId + QStringLiteral(":inline:") + QString::fromLatin1(b64);
}

QString PrBundle::toLinkToken(const QString &bundleUrl) const {
    return QString::fromLatin1(kTokenSchemePrefix) + sessionId + QStringLiteral(":link:") + bundleUrl;
}

PrBundle PrBundle::fromInlineToken(const QString &token, QString *errorOut) {
    PrBundle b;
    TokenKind kind = classifyToken(token);
    if (kind != TokenKind::Inline) {
        if (errorOut) *errorOut = QStringLiteral("Token is not an inline smart-paste token.");
        return b;
    }
    // Strip prefix.
    QString remainder = token.mid(QString::fromLatin1(kTokenSchemePrefix).length());
    // Split into <sessionId>:<inline>:<base64>. The base64 payload itself never
    // contains ':' (Base64Url uses A–Z, a–z, 0–9, -, _), so a simple split by
    // ':' is unambiguous.
    int firstColon = remainder.indexOf(QLatin1Char(':'));
    if (firstColon < 0) { if (errorOut) *errorOut = QStringLiteral("Malformed token (missing kind separator)."); return b; }
    int secondColon = remainder.indexOf(QLatin1Char(':'), firstColon + 1);
    if (secondColon < 0) { if (errorOut) *errorOut = QStringLiteral("Malformed token (missing payload separator)."); return b; }
    QString payload = remainder.mid(secondColon + 1);
    QByteArray compressed = QByteArray::fromBase64(payload.toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    if (compressed.isEmpty()) { if (errorOut) *errorOut = QStringLiteral("Token payload base64-decode failed."); return b; }
    QByteArray bundleBytes = zlibDecompressFromToken(compressed);
    if (bundleBytes.isEmpty()) { if (errorOut) *errorOut = QStringLiteral("Token payload zlib-decompress failed."); return b; }
    return fromBundleJson(bundleBytes, errorOut);
}

// ---------------------------------------------------------------------
// Token classification
// ---------------------------------------------------------------------

bool PrBundle::looksLikeToken(const QString &text) {
    return text.startsWith(QString::fromLatin1(kTokenSchemePrefix));
}

PrBundle::TokenKind PrBundle::classifyToken(const QString &token) {
    if (!looksLikeToken(token)) return TokenKind::Invalid;
    QString remainder = token.mid(QString::fromLatin1(kTokenSchemePrefix).length());
    int firstColon = remainder.indexOf(QLatin1Char(':'));
    if (firstColon < 0) return TokenKind::Invalid;
    QString kind = remainder.mid(firstColon + 1);
    if (kind.startsWith(QLatin1String("inline:"))) return TokenKind::Inline;
    if (kind.startsWith(QLatin1String("link:"))) return TokenKind::Link;
    return TokenKind::Invalid;
}

QString PrBundle::extractLinkUrl(const QString &linkToken) {
    if (classifyToken(linkToken) != TokenKind::Link) return QString();
    QString remainder = linkToken.mid(QString::fromLatin1(kTokenSchemePrefix).length());
    int firstColon = remainder.indexOf(QLatin1Char(':'));
    if (firstColon < 0) return QString();
    int secondColon = remainder.indexOf(QLatin1Char(':'), firstColon + 1);
    if (secondColon < 0) return QString();
    return remainder.mid(secondColon + 1);
}

QString PrBundle::extractSessionId(const QString &token) {
    if (!looksLikeToken(token)) return QString();
    QString remainder = token.mid(QString::fromLatin1(kTokenSchemePrefix).length());
    int firstColon = remainder.indexOf(QLatin1Char(':'));
    if (firstColon < 0) return QString();
    return remainder.left(firstColon);
}
