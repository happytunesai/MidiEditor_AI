/*
 * MidiEditor AI - attachments in MidiPilot messages (Phase 52, v2.5.0).
 *
 * A user message with attachments becomes a content-PART array instead of a
 * plain string. This module is the one place that knows the part shapes:
 *
 *   - the canonical form kept in the conversation history is the Chat
 *     Completions shape: {type:"text"}, {type:"image_url"}, {type:"file"}.
 *     It is sent as it is, so it carries NO extra fields (providers reject
 *     unknown keys) - an image's file name lives in a separate name table;
 *   - toResponsesContent() translates it for OpenAI's Responses API
 *     (input_text / input_image / input_file);
 *   - toGeminiParts() translates it for the native Gemini endpoint
 *     (text / inline_data);
 *   - toStoredContent() / fromStoredContent() swap the base64 payloads for
 *     references to files next to the saved conversation and back.
 *
 * Attaching is never gated by model (owner rule): whatever a provider cannot
 * read comes back as that provider's error. Text files are inlined as text so
 * they work with every model. Pure Qt Core - unit-tested in
 * test_chat_attachments.
 */
#ifndef CHATATTACHMENTS_H
#define CHATATTACHMENTS_H

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

namespace ChatAttachments {

enum class Kind { Image, Pdf, Text, Binary };

/// Limits, set against the providers' request limits (Gemini caps a whole
/// request with inline data at 20 MB, and base64 adds a third to the bytes).
constexpr qint64 kMaxFileBytes = 10 * 1024 * 1024;     ///< one attachment
constexpr qint64 kMaxMessageBytes = 14 * 1024 * 1024;  ///< all attachments of a message
constexpr qint64 kMaxTextBytes = 512 * 1024;           ///< a text file that is inlined
constexpr int kMaxAttachmentsPerMessage = 10;

/// Header line of an inlined text file; attachmentNames() recognises it.
inline QString textFileHeader() { return QStringLiteral("Attached file: "); }

struct Attachment {
    QString fileName;   ///< shown in the chip and sent as the file name
    QString mimeType;
    Kind kind = Kind::Binary;
    QByteArray data;    ///< the file's bytes
    QString sha256;     ///< hex digest of data (identity for storage)
};

/// Kind of a file by its name (extension).
Kind kindForFileName(const QString &fileName);

/// "image" / "pdf" / "text" / "file".
QString kindName(Kind kind);

/// MIME type by extension ("application/octet-stream" when unknown).
QString mimeForFileName(const QString &fileName);

/// True for the image formats providers take directly (png, jpeg, gif, webp).
bool isDirectImage(const QString &fileName);

/// Hex SHA-256 of \a data.
QString hashOf(const QByteArray &data);

/**
 * \brief Builds an attachment from bytes. Refuses an empty file, a file over
 *        kMaxFileBytes, and a text file over kMaxTextBytes or not valid UTF-8.
 */
bool fromBytes(const QString &fileName, const QByteArray &data, Attachment *out,
               QString *error);

/// Reads a file from disk through fromBytes().
bool fromFile(const QString &path, Attachment *out, QString *error);

/// Empty when \a list fits the per-message limits, else the reason.
QString checkMessageLimits(const QList<Attachment> &list);

/**
 * \brief The canonical content of a user message: the text part first, then
 *        one part per attachment (text files inlined under a file-name
 *        header, images as data URLs, PDFs and other files as file parts).
 */
QJsonArray chatContent(const QString &text, const QList<Attachment> &list);

/// A message content (string or part array) for the Responses API.
QJsonValue toResponsesContent(const QJsonValue &content);

/// A message content (string or part array) as native Gemini parts.
QJsonArray toGeminiParts(const QJsonValue &content);

/// The first text part (the user's own message); a string stays as it is.
QString primaryText(const QJsonValue &content);

/// All text parts joined by blank lines; a string stays as it is.
QString plainText(const QJsonValue &content);

/// True when the content carries at least one attachment (text files too).
bool hasAttachments(const QJsonValue &content);

/**
 * \brief Display names of the attachments in a content: the file name of
 *        file parts and inlined text files, \a imageNames (by hash) for
 *        images - "image" when the table does not know it.
 */
QStringList attachmentNames(const QJsonValue &content,
                            const QHash<QString, QString> &imageNames = {});

/**
 * \brief Rough token estimate of a content for the context budget: text at
 *        4 characters per token, an image at a high-detail estimate, a PDF
 *        per page found in the file, other files by size.
 */
int estimateTokens(const QJsonValue &content);

/// The same estimate for one attachment before it is sent (no base64 built).
int estimateTokens(const Attachment &attachment);

/**
 * \brief For saving: every image/file part is written to \a folder as its
 *        own file (named by content hash - the same image attached twice is
 *        stored once) and replaced by a reference part
 *        {type:"attachment_ref", name, mime, kind, size, sha256, stored}.
 *        Text parts, inlined files included, stay as they are. \a ok turns
 *        false when a file could not be written (the reference then points
 *        at nothing and reloads as "missing").
 */
QJsonValue toStoredContent(const QJsonValue &content, const QString &folder,
                           const QHash<QString, QString> &imageNames, bool *ok);

/**
 * \brief For loading: reference parts become data parts again from the files
 *        in \a folder; a file that is gone becomes a short text part naming
 *        it, so a reopened chat still reads and can continue. The names of
 *        restored images are added to \a imageNames.
 */
QJsonValue fromStoredContent(const QJsonValue &content, const QString &folder,
                             QHash<QString, QString> *imageNames);

} // namespace ChatAttachments

#endif // CHATATTACHMENTS_H
