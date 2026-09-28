#include "ChatAttachments.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>

namespace ChatAttachments {

namespace {

// Token estimate of one image at high detail (a 1024x1024 image is 765,
// the largest tiling about 1100).
constexpr int kImageTokens = 1100;
// Per PDF page: providers send the extracted text AND a picture of the page.
constexpr int kPdfPageTokens = 1000;

QString suffixOf(const QString &fileName) {
    return QFileInfo(fileName).suffix().toLower();
}

QString dataUrl(const QString &mime, const QByteArray &bytes) {
    return QStringLiteral("data:") + mime + QStringLiteral(";base64,")
        + QString::fromLatin1(bytes.toBase64());
}

bool parseDataUrl(const QString &url, QString *mime, QByteArray *bytes) {
    if (!url.startsWith(QLatin1String("data:")))
        return false;
    const int comma = url.indexOf(QLatin1Char(','));
    if (comma < 0)
        return false;
    const QString header = url.mid(5, comma - 5); // "<mime>;base64"
    if (!header.endsWith(QLatin1String(";base64")))
        return false;
    *mime = header.left(header.size() - 7);
    *bytes = QByteArray::fromBase64(url.mid(comma + 1).toLatin1());
    return true;
}

QString extensionForMime(const QString &mime) {
    if (mime == QLatin1String("image/png")) return QStringLiteral("png");
    if (mime == QLatin1String("image/jpeg")) return QStringLiteral("jpg");
    if (mime == QLatin1String("image/gif")) return QStringLiteral("gif");
    if (mime == QLatin1String("image/webp")) return QStringLiteral("webp");
    if (mime == QLatin1String("application/pdf")) return QStringLiteral("pdf");
    return QStringLiteral("bin");
}

// A stored file name comes back from a JSON file on disk: accept only the
// shape toStoredContent() writes, so a tampered history cannot point
// outside the conversation's folder.
bool isSafeStoredName(const QString &name) {
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{64}\\.[a-z0-9]{1,8}$"));
    return re.match(name).hasMatch();
}

int pdfPageCount(const QByteArray &bytes) {
    static const QRegularExpression re(QStringLiteral("/Type\\s*/Page(?![a-zA-Z])"));
    // Latin-1 keeps every byte; the page objects are plain ASCII.
    const QString text = QString::fromLatin1(bytes);
    int pages = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++pages;
    }
    return pages;
}

QString textFileName(const QString &text) {
    if (!text.startsWith(textFileHeader()))
        return QString();
    const int eol = text.indexOf(QLatin1Char('\n'));
    return text.mid(textFileHeader().size(),
                    (eol < 0 ? text.size() : eol) - textFileHeader().size()).trimmed();
}

QString writeReference(QJsonObject *ref, const QString &folder, const QString &sha,
                       const QString &ext, const QByteArray &bytes, bool *ok) {
    const QString stored = sha + QLatin1Char('.') + ext;
    const QString path = QDir(folder).filePath(stored);
    if (!QFile::exists(path)) {
        QDir().mkpath(folder);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
            *ok = false;
            f.close();
            QFile::remove(path);
        }
    }
    (*ref)[QStringLiteral("type")] = QStringLiteral("attachment_ref");
    (*ref)[QStringLiteral("sha256")] = sha;
    (*ref)[QStringLiteral("size")] = double(bytes.size());
    (*ref)[QStringLiteral("stored")] = stored;
    return stored;
}

} // namespace

Kind kindForFileName(const QString &fileName) {
    static const QStringList kText = {
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("csv"),
        QStringLiteral("tsv"), QStringLiteral("json"), QStringLiteral("xml"),
        QStringLiteral("musicxml"), QStringLiteral("abc"), QStringLiteral("mml"),
        QStringLiteral("lrc"), QStringLiteral("srt"), QStringLiteral("log"),
    };
    const QString s = suffixOf(fileName);
    if (s == QLatin1String("pdf"))
        return Kind::Pdf;
    if (isDirectImage(fileName))
        return Kind::Image;
    if (kText.contains(s))
        return Kind::Text;
    return Kind::Binary;
}

QString kindName(Kind kind) {
    switch (kind) {
    case Kind::Image: return QStringLiteral("image");
    case Kind::Pdf: return QStringLiteral("pdf");
    case Kind::Text: return QStringLiteral("text");
    case Kind::Binary: break;
    }
    return QStringLiteral("file");
}

QString mimeForFileName(const QString &fileName) {
    const QString s = suffixOf(fileName);
    if (s == QLatin1String("png")) return QStringLiteral("image/png");
    if (s == QLatin1String("jpg") || s == QLatin1String("jpeg")) return QStringLiteral("image/jpeg");
    if (s == QLatin1String("gif")) return QStringLiteral("image/gif");
    if (s == QLatin1String("webp")) return QStringLiteral("image/webp");
    if (s == QLatin1String("pdf")) return QStringLiteral("application/pdf");
    if (s == QLatin1String("md")) return QStringLiteral("text/markdown");
    if (s == QLatin1String("csv")) return QStringLiteral("text/csv");
    if (s == QLatin1String("json")) return QStringLiteral("application/json");
    if (s == QLatin1String("xml") || s == QLatin1String("musicxml")) return QStringLiteral("application/xml");
    if (kindForFileName(fileName) == Kind::Text) return QStringLiteral("text/plain");
    if (s == QLatin1String("mid") || s == QLatin1String("midi")) return QStringLiteral("audio/midi");
    return QStringLiteral("application/octet-stream");
}

bool isDirectImage(const QString &fileName) {
    const QString s = suffixOf(fileName);
    return s == QLatin1String("png") || s == QLatin1String("jpg") || s == QLatin1String("jpeg")
        || s == QLatin1String("gif") || s == QLatin1String("webp");
}

QString hashOf(const QByteArray &data) {
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool fromBytes(const QString &fileName, const QByteArray &data, Attachment *out,
               QString *error) {
    const QString name = QFileInfo(fileName).fileName();
    if (data.isEmpty()) {
        *error = QStringLiteral("\"") + name + QStringLiteral("\" is empty.");
        return false;
    }
    if (data.size() > kMaxFileBytes) {
        *error = QStringLiteral("\"") + name + QStringLiteral("\" is larger than %1 MB.")
                     .arg(kMaxFileBytes / (1024 * 1024));
        return false;
    }
    const Kind kind = kindForFileName(name);
    QByteArray bytes = data;
    if (kind == Kind::Text) {
        // A byte-order mark names the encoding (UTF-16 from older editors,
        // for example); such text is sent as UTF-8 like everything else.
        // Stateless: a sequence cut off at the end of the file is an error,
        // not a remainder waiting for more data.
        const auto encoding = QStringConverter::encodingForData(data);
        if (encoding && *encoding != QStringConverter::Utf8) {
            QStringDecoder decoder(*encoding, QStringConverter::Flag::Stateless);
            const QString text = decoder(data);
            if (decoder.hasError()) {
                *error = QStringLiteral("\"") + name + QStringLiteral("\" could not be read as text.");
                return false;
            }
            bytes = text.toUtf8();
        } else if (bytes.startsWith("\xEF\xBB\xBF")) {
            bytes = bytes.mid(3); // UTF-8 with BOM: the BOM is not part of the text
        }
        if (bytes.size() > kMaxTextBytes) {
            *error = QStringLiteral("\"") + name + QStringLiteral("\" is larger than %1 KB - "
                                                                  "text files go into the message itself.")
                         .arg(kMaxTextBytes / 1024);
            return false;
        }
        QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
        const QString decoded = utf8(bytes);
        Q_UNUSED(decoded);
        if (utf8.hasError()) {
            *error = QStringLiteral("\"") + name + QStringLiteral("\" is not UTF-8 text.");
            return false;
        }
    }
    out->fileName = name;
    out->kind = kind;
    out->mimeType = mimeForFileName(name);
    out->data = bytes;
    out->sha256 = hashOf(bytes);
    return true;
}

bool fromFile(const QString &path, Attachment *out, QString *error) {
    const QFileInfo info(path);
    if (!info.isFile()) {
        *error = QStringLiteral("\"") + info.fileName() + QStringLiteral("\" is not a file.");
        return false;
    }
    if (info.size() > kMaxFileBytes) {
        *error = QStringLiteral("\"") + info.fileName() + QStringLiteral("\" is larger than %1 MB.")
                     .arg(kMaxFileBytes / (1024 * 1024));
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("\"") + info.fileName() + QStringLiteral("\" could not be read.");
        return false;
    }
    return fromBytes(info.fileName(), f.readAll(), out, error);
}

QString checkMessageLimits(const QList<Attachment> &list) {
    if (list.size() > kMaxAttachmentsPerMessage) {
        return QStringLiteral("At most %1 attachments per message.").arg(kMaxAttachmentsPerMessage);
    }
    qint64 total = 0;
    for (const Attachment &a : list)
        total += a.data.size();
    if (total > kMaxMessageBytes) {
        return QStringLiteral("The attachments of one message may add up to %1 MB.")
            .arg(kMaxMessageBytes / (1024 * 1024));
    }
    return QString();
}

QJsonArray chatContent(const QString &text, const QList<Attachment> &list) {
    QJsonArray parts;
    parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                             {QStringLiteral("text"), text}});
    for (const Attachment &a : list) {
        switch (a.kind) {
        case Kind::Text:
            parts.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("text")},
                {QStringLiteral("text"), textFileHeader() + a.fileName + QStringLiteral("\n\n")
                                             + QString::fromUtf8(a.data)}});
            break;
        case Kind::Image:
            parts.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("image_url")},
                {QStringLiteral("image_url"),
                 QJsonObject{{QStringLiteral("url"), dataUrl(a.mimeType, a.data)},
                             {QStringLiteral("detail"), QStringLiteral("high")}}}});
            break;
        case Kind::Pdf:
        case Kind::Binary:
            parts.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("file")},
                {QStringLiteral("file"),
                 QJsonObject{{QStringLiteral("filename"), a.fileName},
                             {QStringLiteral("file_data"), dataUrl(a.mimeType, a.data)}}}});
            break;
        }
    }
    return parts;
}

QJsonValue toResponsesContent(const QJsonValue &content) {
    if (!content.isArray())
        return content;
    QJsonArray out;
    for (const QJsonValue &v : content.toArray()) {
        const QJsonObject part = v.toObject();
        const QString type = part.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text")) {
            out.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")},
                                   {QStringLiteral("text"), part.value(QStringLiteral("text"))}});
        } else if (type == QLatin1String("image_url")) {
            const QJsonObject img = part.value(QStringLiteral("image_url")).toObject();
            QJsonObject item{{QStringLiteral("type"), QStringLiteral("input_image")},
                             {QStringLiteral("image_url"), img.value(QStringLiteral("url"))}};
            const QString detail = img.value(QStringLiteral("detail")).toString();
            item[QStringLiteral("detail")] = detail.isEmpty() ? QStringLiteral("auto") : detail;
            out.append(item);
        } else if (type == QLatin1String("file")) {
            const QJsonObject file = part.value(QStringLiteral("file")).toObject();
            out.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_file")},
                                   {QStringLiteral("filename"), file.value(QStringLiteral("filename"))},
                                   {QStringLiteral("file_data"), file.value(QStringLiteral("file_data"))}});
        } else if (type.startsWith(QLatin1String("input_"))) {
            out.append(part); // already in Responses form
        }
        // Anything else (e.g. a stored reference) is not sendable - dropped.
    }
    return out;
}

QJsonArray toGeminiParts(const QJsonValue &content) {
    QJsonArray parts;
    if (!content.isArray()) {
        const QString s = content.toString();
        if (!s.isEmpty())
            parts.append(QJsonObject{{QStringLiteral("text"), s}});
        return parts;
    }
    for (const QJsonValue &v : content.toArray()) {
        const QJsonObject part = v.toObject();
        const QString type = part.value(QStringLiteral("type")).toString();
        QString url;
        if (type == QLatin1String("text")) {
            const QString s = part.value(QStringLiteral("text")).toString();
            if (!s.isEmpty())
                parts.append(QJsonObject{{QStringLiteral("text"), s}});
            continue;
        } else if (type == QLatin1String("image_url")) {
            url = part.value(QStringLiteral("image_url")).toObject()
                      .value(QStringLiteral("url")).toString();
        } else if (type == QLatin1String("file")) {
            url = part.value(QStringLiteral("file")).toObject()
                      .value(QStringLiteral("file_data")).toString();
        } else {
            continue;
        }
        const int comma = url.indexOf(QLatin1Char(','));
        if (!url.startsWith(QLatin1String("data:")) || comma < 0
            || !url.left(comma).endsWith(QLatin1String(";base64")))
            continue;
        const QString mime = url.mid(5, comma - 5 - 7);
        // The base64 text is passed through as it is - no decode/re-encode.
        parts.append(QJsonObject{{QStringLiteral("inline_data"),
                                  QJsonObject{{QStringLiteral("mime_type"), mime},
                                              {QStringLiteral("data"), url.mid(comma + 1)}}}});
    }
    return parts;
}

QString primaryText(const QJsonValue &content) {
    if (!content.isArray())
        return content.toString();
    for (const QJsonValue &v : content.toArray()) {
        const QJsonObject part = v.toObject();
        if (part.value(QStringLiteral("type")).toString() == QLatin1String("text"))
            return part.value(QStringLiteral("text")).toString();
    }
    return QString();
}

QString plainText(const QJsonValue &content) {
    if (!content.isArray())
        return content.toString();
    QStringList texts;
    for (const QJsonValue &v : content.toArray()) {
        const QJsonObject part = v.toObject();
        if (part.value(QStringLiteral("type")).toString() == QLatin1String("text"))
            texts << part.value(QStringLiteral("text")).toString();
    }
    return texts.join(QStringLiteral("\n\n"));
}

bool hasAttachments(const QJsonValue &content) {
    if (!content.isArray())
        return false;
    for (const QJsonValue &v : content.toArray()) {
        const QJsonObject part = v.toObject();
        const QString type = part.value(QStringLiteral("type")).toString();
        if (type != QLatin1String("text"))
            return true;
        if (!textFileName(part.value(QStringLiteral("text")).toString()).isEmpty())
            return true;
    }
    return false;
}

QStringList attachmentNames(const QJsonValue &content, const QHash<QString, QString> &imageNames) {
    QStringList names;
    if (!content.isArray())
        return names;
    for (const QJsonValue &v : content.toArray()) {
        const QJsonObject part = v.toObject();
        const QString type = part.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text")) {
            const QString n = textFileName(part.value(QStringLiteral("text")).toString());
            if (!n.isEmpty())
                names << n;
        } else if (type == QLatin1String("image_url")) {
            QString mime;
            QByteArray bytes;
            const QString url = part.value(QStringLiteral("image_url")).toObject()
                                    .value(QStringLiteral("url")).toString();
            const QString name = parseDataUrl(url, &mime, &bytes)
                ? imageNames.value(hashOf(bytes)) : QString();
            names << (name.isEmpty() ? QStringLiteral("image") : name);
        } else if (type == QLatin1String("file")) {
            names << part.value(QStringLiteral("file")).toObject()
                         .value(QStringLiteral("filename")).toString();
        } else if (type == QLatin1String("attachment_ref")) {
            names << part.value(QStringLiteral("name")).toString();
        }
    }
    return names;
}

QJsonValue withoutImageAndFileParts(const QJsonValue &content,
                                    const QHash<QString, QString> &imageNames,
                                    QStringList *removed) {
    if (!content.isArray())
        return content;
    QJsonArray kept;
    QJsonArray takenOut;
    for (const QJsonValue &v : content.toArray()) {
        const QString type = v.toObject().value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text"))
            kept.append(v);
        else
            takenOut.append(v);
    }
    if (takenOut.isEmpty())
        return content;
    const QStringList names = attachmentNames(takenOut, imageNames);
    if (removed)
        *removed = names;
    QString text = plainText(kept);
    if (!text.isEmpty())
        text += QStringLiteral("\n\n");
    text += QStringLiteral("[Not sent: ") + names.join(QStringLiteral(", "))
        + QStringLiteral(" - the provider refused the message with them.]");
    return text;
}

bool isContentRefusal(const QString &error) {
    static const QRegularExpression re(QStringLiteral("HTTP (\\d{3})"));
    const QRegularExpressionMatch m = re.match(error);
    if (!m.hasMatch())
        return false;
    const int code = m.captured(1).toInt();
    return code == 400 || code == 413 || code == 415 || code == 422;
}

int estimateTokens(const QJsonValue &content) {
    if (!content.isArray())
        return content.toString().size() / 4;
    int tokens = 0;
    for (const QJsonValue &v : content.toArray()) {
        const QJsonObject part = v.toObject();
        const QString type = part.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text")) {
            tokens += part.value(QStringLiteral("text")).toString().size() / 4;
        } else if (type == QLatin1String("image_url")) {
            tokens += kImageTokens;
        } else if (type == QLatin1String("file")) {
            QString mime;
            QByteArray bytes;
            const QString url = part.value(QStringLiteral("file")).toObject()
                                    .value(QStringLiteral("file_data")).toString();
            if (!parseDataUrl(url, &mime, &bytes))
                continue;
            if (mime == QLatin1String("application/pdf"))
                tokens += qMax(1, pdfPageCount(bytes)) * kPdfPageTokens;
            else
                tokens += int(qMin<qint64>(bytes.size() / 4, 1000000));
        }
    }
    return tokens;
}

int estimateTokens(const Attachment &attachment) {
    switch (attachment.kind) {
    case Kind::Image:
        return kImageTokens;
    case Kind::Pdf:
        return qMax(1, pdfPageCount(attachment.data)) * kPdfPageTokens;
    case Kind::Text:
        return (textFileHeader().size() + attachment.fileName.size()
                + QString::fromUtf8(attachment.data).size()) / 4;
    case Kind::Binary:
        break;
    }
    return int(qMin<qint64>(attachment.data.size() / 4, 1000000));
}

QJsonValue toStoredContent(const QJsonValue &content, const QString &folder,
                           const QHash<QString, QString> &imageNames, bool *ok) {
    *ok = true;
    if (!content.isArray())
        return content;
    QJsonArray out;
    for (const QJsonValue &v : content.toArray()) {
        const QJsonObject part = v.toObject();
        const QString type = part.value(QStringLiteral("type")).toString();
        QString mime;
        QByteArray bytes;
        if (type == QLatin1String("image_url")
            && parseDataUrl(part.value(QStringLiteral("image_url")).toObject()
                                .value(QStringLiteral("url")).toString(), &mime, &bytes)) {
            const QString sha = hashOf(bytes);
            const QString ext = extensionForMime(mime);
            QJsonObject ref;
            writeReference(&ref, folder, sha, ext, bytes, ok);
            const QString name = imageNames.value(sha);
            ref[QStringLiteral("name")] = name.isEmpty() ? QStringLiteral("image.") + ext : name;
            ref[QStringLiteral("mime")] = mime;
            ref[QStringLiteral("kind")] = QStringLiteral("image");
            out.append(ref);
        } else if (type == QLatin1String("file")) {
            const QJsonObject file = part.value(QStringLiteral("file")).toObject();
            if (!parseDataUrl(file.value(QStringLiteral("file_data")).toString(), &mime, &bytes)) {
                out.append(part);
                continue;
            }
            const QString name = file.value(QStringLiteral("filename")).toString();
            QString ext = suffixOf(name);
            static const QRegularExpression safeExt(QStringLiteral("^[a-z0-9]{1,8}$"));
            if (!safeExt.match(ext).hasMatch())
                ext = extensionForMime(mime);
            QJsonObject ref;
            writeReference(&ref, folder, hashOf(bytes), ext, bytes, ok);
            ref[QStringLiteral("name")] = name;
            ref[QStringLiteral("mime")] = mime;
            ref[QStringLiteral("kind")] = mime == QLatin1String("application/pdf")
                ? QStringLiteral("pdf") : QStringLiteral("file");
            out.append(ref);
        } else {
            out.append(part);
        }
    }
    return out;
}

QJsonValue fromStoredContent(const QJsonValue &content, const QString &folder,
                             QHash<QString, QString> *imageNames) {
    if (!content.isArray())
        return content;
    QJsonArray out;
    for (const QJsonValue &v : content.toArray()) {
        const QJsonObject part = v.toObject();
        if (part.value(QStringLiteral("type")).toString() != QLatin1String("attachment_ref")) {
            out.append(part);
            continue;
        }
        const QString name = part.value(QStringLiteral("name")).toString();
        const QString stored = part.value(QStringLiteral("stored")).toString();
        const QString mime = part.value(QStringLiteral("mime")).toString();
        QByteArray bytes;
        bool found = false;
        if (isSafeStoredName(stored)) {
            QFile f(QDir(folder).filePath(stored));
            if (f.open(QIODevice::ReadOnly)) {
                bytes = f.readAll();
                found = !bytes.isEmpty();
            }
        }
        if (!found) {
            // Title concatenated, never .arg()-substituted (file-name input).
            out.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                   {QStringLiteral("text"),
                                    QStringLiteral("[Attachment no longer available: ") + name
                                        + QLatin1Char(']')}});
            continue;
        }
        if (part.value(QStringLiteral("kind")).toString() == QLatin1String("image")) {
            if (imageNames)
                imageNames->insert(hashOf(bytes), name);
            out.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("image_url")},
                {QStringLiteral("image_url"),
                 QJsonObject{{QStringLiteral("url"), dataUrl(mime, bytes)},
                             {QStringLiteral("detail"), QStringLiteral("high")}}}});
        } else {
            out.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("file")},
                {QStringLiteral("file"),
                 QJsonObject{{QStringLiteral("filename"), name},
                             {QStringLiteral("file_data"), dataUrl(mime, bytes)}}}});
        }
    }
    return out;
}

} // namespace ChatAttachments
