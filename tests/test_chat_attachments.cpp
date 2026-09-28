/*
 * test_chat_attachments (Phase 52, v2.5.0)
 *
 * The content parts of a MidiPilot message with attachments
 * (src/ai/ChatAttachments):
 *   - per file kind: images as data URLs, PDFs and other files as file parts,
 *     text files inlined under a file-name header;
 *   - per transport: the canonical Chat form, its Responses API translation
 *     and the native Gemini parts;
 *   - limits, the token estimate, and the storage round trip (references to
 *     files next to the conversation, a missing file reloads as a text line,
 *     a tampered reference cannot leave the folder).
 * Pure Qt Core.
 */

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStringEncoder>
#include <QTemporaryDir>

#include "../src/ai/ChatAttachments.h"

using namespace ChatAttachments;

namespace {

// Smallest valid PNG header bytes are not needed - the module never decodes
// images; any bytes stand in for the file.
QByteArray pngBytes() { return QByteArray("\x89PNG\r\n\x1a\nfake-image-bytes", 24); }

QByteArray pdfBytes(int pages) {
    QByteArray b("%PDF-1.4\n1 0 obj << /Type /Pages /Count 2 >> endobj\n");
    for (int i = 0; i < pages; ++i)
        b += "2 0 obj << /Type /Page /Parent 1 0 R >> endobj\n";
    b += "%%EOF";
    return b;
}

Attachment make(const QString &name, const QByteArray &data) {
    Attachment a;
    QString error;
    const bool ok = fromBytes(name, data, &a, &error);
    if (!ok)
        qWarning("fromBytes(%s) failed: %s", qPrintable(name), qPrintable(error));
    return a;
}

} // namespace

class TestChatAttachments : public QObject {
    Q_OBJECT

private slots:
    void kinds_byExtension() {
        QCOMPARE(kindForFileName(QStringLiteral("sheet.PNG")), Kind::Image);
        QCOMPARE(kindForFileName(QStringLiteral("sheet.jpeg")), Kind::Image);
        QCOMPARE(kindForFileName(QStringLiteral("score.pdf")), Kind::Pdf);
        QCOMPARE(kindForFileName(QStringLiteral("song.abc")), Kind::Text);
        QCOMPARE(kindForFileName(QStringLiteral("lyrics.lrc")), Kind::Text);
        QCOMPARE(kindForFileName(QStringLiteral("score.musicxml")), Kind::Text);
        QCOMPARE(kindForFileName(QStringLiteral("song.mid")), Kind::Binary);
        QCOMPARE(kindForFileName(QStringLiteral("scan.bmp")), Kind::Binary); // converted by the panel
        QCOMPARE(mimeForFileName(QStringLiteral("a.jpg")), QStringLiteral("image/jpeg"));
        QCOMPARE(mimeForFileName(QStringLiteral("a.pdf")), QStringLiteral("application/pdf"));
        QCOMPARE(mimeForFileName(QStringLiteral("a.xyz")), QStringLiteral("application/octet-stream"));
    }

    void fromBytes_refusesEmptyOversizedAndNonUtf8() {
        Attachment a;
        QString error;
        QVERIFY(!fromBytes(QStringLiteral("x.png"), QByteArray(), &a, &error));
        QVERIFY(error.contains(QStringLiteral("empty")));
        QVERIFY(!fromBytes(QStringLiteral("x.png"), QByteArray(kMaxFileBytes + 1, 'x'), &a, &error));
        QVERIFY(!fromBytes(QStringLiteral("x.txt"), QByteArray(kMaxTextBytes + 1, 'x'), &a, &error));
        // Invalid UTF-8 without a byte-order mark (FF FE would announce UTF-16).
        QVERIFY(!fromBytes(QStringLiteral("x.txt"), QByteArray("abc\xc3\x28", 5), &a, &error));
        QVERIFY(error.contains(QStringLiteral("UTF-8")));
        // A multi-byte character cut off at the end is invalid too.
        QVERIFY(!fromBytes(QStringLiteral("x.txt"), QByteArray("abc\xc3", 4), &a, &error));
        QVERIFY(fromBytes(QStringLiteral("C:/some/dir/x.txt"), QByteArray("hello"), &a, &error));
        QCOMPARE(a.fileName, QStringLiteral("x.txt")); // name only, no folder
        QCOMPARE(a.sha256, hashOf(QByteArray("hello")));
    }

    // Text with a byte-order mark in another encoding (UTF-16 from older
    // editors) is converted to UTF-8 instead of being refused.
    void fromBytes_convertsBomMarkedUtf16TextToUtf8() {
        const QString lyrics = QStringLiteral("[00:01.00] Ein kleines Lied \u00e4\u00f6\u00fc");
        QStringEncoder toUtf16(QStringEncoder::Utf16LE, QStringEncoder::Flag::WriteBom);
        const QByteArray utf16 = toUtf16(lyrics);
        QVERIFY(utf16.startsWith("\xff\xfe"));
        Attachment a;
        QString error;
        QVERIFY2(fromBytes(QStringLiteral("song.lrc"), utf16, &a, &error), qPrintable(error));
        QCOMPARE(QString::fromUtf8(a.data), lyrics);
        const QJsonArray parts = chatContent(QStringLiteral("hi"), {a});
        QVERIFY(parts[1].toObject()[QStringLiteral("text")].toString().endsWith(lyrics));

        // UTF-8 with a BOM: the BOM does not become part of the text.
        QVERIFY2(fromBytes(QStringLiteral("notes.txt"), QByteArray("\xEF\xBB\xBFhello"), &a, &error),
                 qPrintable(error));
        QCOMPARE(a.data, QByteArray("hello"));
    }

    void messageLimits() {
        QList<Attachment> many;
        for (int i = 0; i <= kMaxAttachmentsPerMessage; ++i)
            many << make(QStringLiteral("a.png"), pngBytes());
        QVERIFY(!checkMessageLimits(many).isEmpty());
        QList<Attachment> big;
        big << make(QStringLiteral("a.pdf"), QByteArray(kMaxFileBytes, 'x'))
            << make(QStringLiteral("b.pdf"), QByteArray(kMaxFileBytes, 'y'));
        QVERIFY(!checkMessageLimits(big).isEmpty());
        QVERIFY(checkMessageLimits({make(QStringLiteral("a.png"), pngBytes())}).isEmpty());
    }

    // The canonical Chat Completions form, one part per kind.
    void chatContent_partPerKind() {
        const QJsonArray parts = chatContent(
            QStringLiteral("Write this sheet into track 1"),
            {make(QStringLiteral("sheet.png"), pngBytes()),
             make(QStringLiteral("score.pdf"), pdfBytes(2)),
             make(QStringLiteral("tune.abc"), QByteArray("X:1\nK:C\nCDEF|")),
             make(QStringLiteral("song.mid"), QByteArray("MThd\0\0\0\6", 8))});
        QCOMPARE(parts.size(), 5);
        QCOMPARE(parts[0].toObject()[QStringLiteral("type")].toString(), QStringLiteral("text"));
        QCOMPARE(parts[0].toObject()[QStringLiteral("text")].toString(),
                 QStringLiteral("Write this sheet into track 1"));

        const QJsonObject img = parts[1].toObject();
        QCOMPARE(img[QStringLiteral("type")].toString(), QStringLiteral("image_url"));
        QVERIFY(img[QStringLiteral("image_url")].toObject()[QStringLiteral("url")].toString()
                    .startsWith(QStringLiteral("data:image/png;base64,")));
        QCOMPARE(img[QStringLiteral("image_url")].toObject()[QStringLiteral("detail")].toString(),
                 QStringLiteral("high"));
        // API-clean: nothing but type + image_url.
        QCOMPARE(img.size(), 2);

        const QJsonObject pdf = parts[2].toObject();
        QCOMPARE(pdf[QStringLiteral("type")].toString(), QStringLiteral("file"));
        QCOMPARE(pdf[QStringLiteral("file")].toObject()[QStringLiteral("filename")].toString(),
                 QStringLiteral("score.pdf"));
        QVERIFY(pdf[QStringLiteral("file")].toObject()[QStringLiteral("file_data")].toString()
                    .startsWith(QStringLiteral("data:application/pdf;base64,")));

        const QString inlined = parts[3].toObject()[QStringLiteral("text")].toString();
        QVERIFY(inlined.startsWith(textFileHeader() + QStringLiteral("tune.abc\n\n")));
        QVERIFY(inlined.contains(QStringLiteral("CDEF|")));

        QCOMPARE(parts[4].toObject()[QStringLiteral("type")].toString(), QStringLiteral("file"));
        QVERIFY(hasAttachments(parts));
        QVERIFY(!hasAttachments(chatContent(QStringLiteral("just text"), {})));
        QVERIFY(!hasAttachments(QJsonValue(QStringLiteral("plain string"))));
    }

    void responsesTranslation() {
        const QJsonArray chat = chatContent(
            QStringLiteral("hi"),
            {make(QStringLiteral("sheet.png"), pngBytes()),
             make(QStringLiteral("score.pdf"), pdfBytes(1))});
        const QJsonArray r = toResponsesContent(chat).toArray();
        QCOMPARE(r.size(), 3);
        QCOMPARE(r[0].toObject()[QStringLiteral("type")].toString(), QStringLiteral("input_text"));
        QCOMPARE(r[0].toObject()[QStringLiteral("text")].toString(), QStringLiteral("hi"));
        QCOMPARE(r[1].toObject()[QStringLiteral("type")].toString(), QStringLiteral("input_image"));
        QVERIFY(r[1].toObject()[QStringLiteral("image_url")].toString()
                    .startsWith(QStringLiteral("data:image/png;base64,")));
        QCOMPARE(r[1].toObject()[QStringLiteral("detail")].toString(), QStringLiteral("high"));
        QCOMPARE(r[2].toObject()[QStringLiteral("type")].toString(), QStringLiteral("input_file"));
        QCOMPARE(r[2].toObject()[QStringLiteral("filename")].toString(), QStringLiteral("score.pdf"));
        // A plain string content passes through untouched.
        QCOMPARE(toResponsesContent(QJsonValue(QStringLiteral("x"))).toString(), QStringLiteral("x"));
    }

    void geminiTranslation() {
        const QByteArray png = pngBytes();
        const QJsonArray chat = chatContent(QStringLiteral("hi"),
                                            {make(QStringLiteral("sheet.png"), png)});
        const QJsonArray g = toGeminiParts(chat);
        QCOMPARE(g.size(), 2);
        QCOMPARE(g[0].toObject()[QStringLiteral("text")].toString(), QStringLiteral("hi"));
        const QJsonObject inl = g[1].toObject()[QStringLiteral("inline_data")].toObject();
        QCOMPARE(inl[QStringLiteral("mime_type")].toString(), QStringLiteral("image/png"));
        QCOMPARE(QByteArray::fromBase64(inl[QStringLiteral("data")].toString().toLatin1()), png);
        // String content -> one text part; empty string -> none.
        QCOMPARE(toGeminiParts(QJsonValue(QStringLiteral("x"))).size(), 1);
        QCOMPARE(toGeminiParts(QJsonValue(QString())).size(), 0);
    }

    void textHelpers() {
        const QJsonArray chat = chatContent(
            QStringLiteral("instruction"),
            {make(QStringLiteral("notes.txt"), QByteArray("line")),
             make(QStringLiteral("sheet.png"), pngBytes())});
        QCOMPARE(primaryText(chat), QStringLiteral("instruction"));
        QVERIFY(plainText(chat).startsWith(QStringLiteral("instruction\n\n")));
        QVERIFY(plainText(chat).contains(QStringLiteral("line")));
        QHash<QString, QString> names{{hashOf(pngBytes()), QStringLiteral("sheet.png")}};
        QCOMPARE(attachmentNames(chat, names),
                 (QStringList{QStringLiteral("notes.txt"), QStringLiteral("sheet.png")}));
        QCOMPARE(attachmentNames(chat), (QStringList{QStringLiteral("notes.txt"), QStringLiteral("image")}));
        QCOMPARE(primaryText(QJsonValue(QStringLiteral("s"))), QStringLiteral("s"));
    }

    // A refused message leaves the conversation as text: pictures and files
    // out (named), inlined text files and the user's text kept.
    void refusedMessage_keepsItsTextOnly() {
        const QJsonArray chat = chatContent(
            QStringLiteral("instruction"),
            {make(QStringLiteral("notes.txt"), QByteArray("line")),
             make(QStringLiteral("sheet.png"), pngBytes()),
             make(QStringLiteral("song.mid"), QByteArray("MThd-bytes"))});
        QHash<QString, QString> names{{hashOf(pngBytes()), QStringLiteral("sheet.png")}};
        QStringList removed;
        const QJsonValue out = withoutImageAndFileParts(chat, names, &removed);
        QVERIFY(out.isString());
        QCOMPARE(removed, (QStringList{QStringLiteral("sheet.png"), QStringLiteral("song.mid")}));
        QVERIFY(out.toString().startsWith(QStringLiteral("instruction\n\n")));
        QVERIFY(out.toString().contains(QStringLiteral("line")));          // text file kept
        QVERIFY(out.toString().contains(QStringLiteral("sheet.png, song.mid")));
        QVERIFY(!out.toString().contains(QStringLiteral("base64")));
        // nothing to take out: unchanged
        removed.clear();
        const QJsonArray textOnly = chatContent(QStringLiteral("hi"),
                                                {make(QStringLiteral("notes.txt"), QByteArray("x"))});
        QCOMPARE(withoutImageAndFileParts(textOnly, {}, &removed), QJsonValue(textOnly));
        QVERIFY(removed.isEmpty());
        QCOMPARE(withoutImageAndFileParts(QJsonValue(QStringLiteral("s")), {}, &removed),
                 QJsonValue(QStringLiteral("s")));
    }

    void contentRefusal_onlyForTheRequestsContent() {
        QVERIFY(isContentRefusal(QStringLiteral("API error (HTTP 400): invalid image")));
        QVERIFY(isContentRefusal(QStringLiteral("Streaming error (HTTP 413): too large")));
        QVERIFY(isContentRefusal(QStringLiteral("Gemini streaming error (HTTP 422): x\ny")));
        QVERIFY(!isContentRefusal(QStringLiteral("API error (HTTP 401): bad key")));
        QVERIFY(!isContentRefusal(QStringLiteral("API error (HTTP 429): rate limit")));
        QVERIFY(!isContentRefusal(QStringLiteral("Streaming error (HTTP 503): overloaded")));
        QVERIFY(!isContentRefusal(QStringLiteral("Request timed out.")));
    }

    void tokenEstimate_countsAttachments() {
        const int textOnly = estimateTokens(chatContent(QString(400, QLatin1Char('a')), {}));
        QCOMPARE(textOnly, 100);
        const int withImage = estimateTokens(chatContent(QString(400, QLatin1Char('a')),
                                                         {make(QStringLiteral("a.png"), pngBytes())}));
        QVERIFY(withImage >= textOnly + 700);
        const int pdf1 = estimateTokens(chatContent(QString(), {make(QStringLiteral("a.pdf"), pdfBytes(1))}));
        const int pdf3 = estimateTokens(chatContent(QString(), {make(QStringLiteral("a.pdf"), pdfBytes(3))}));
        QCOMPARE(pdf3, 3 * pdf1); // per page, "/Type /Pages" not counted
        QCOMPARE(estimateTokens(QJsonValue(QString(40, QLatin1Char('x')))), 10);
    }

    // Save -> files next to the conversation, references in the JSON;
    // load -> the same content again.
    void storage_roundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString folder = dir.filePath(QStringLiteral("conv-1"));
        const QByteArray png = pngBytes();
        const QJsonArray chat = chatContent(
            QStringLiteral("hi"),
            {make(QStringLiteral("sheet.png"), png),
             make(QStringLiteral("score.pdf"), pdfBytes(2)),
             make(QStringLiteral("notes.txt"), QByteArray("keep me inline"))});
        QHash<QString, QString> names{{hashOf(png), QStringLiteral("sheet.png")}};

        bool ok = false;
        const QJsonArray stored = toStoredContent(chat, folder, names, &ok).toArray();
        QVERIFY(ok);
        QCOMPARE(stored.size(), 4);
        const QJsonObject imgRef = stored[1].toObject();
        QCOMPARE(imgRef[QStringLiteral("type")].toString(), QStringLiteral("attachment_ref"));
        QCOMPARE(imgRef[QStringLiteral("name")].toString(), QStringLiteral("sheet.png"));
        QCOMPARE(imgRef[QStringLiteral("kind")].toString(), QStringLiteral("image"));
        QVERIFY(QFile::exists(QDir(folder).filePath(imgRef[QStringLiteral("stored")].toString())));
        QCOMPARE(stored[2].toObject()[QStringLiteral("kind")].toString(), QStringLiteral("pdf"));
        // No base64 anywhere in the stored form...
        const QByteArray json = QJsonDocument(stored).toJson();
        QVERIFY(!json.contains("base64"));
        // ...and the inlined text file stays text.
        QVERIFY(stored[3].toObject()[QStringLiteral("text")].toString().contains(QStringLiteral("keep me inline")));

        QHash<QString, QString> restoredNames;
        const QJsonArray restored = fromStoredContent(stored, folder, &restoredNames).toArray();
        QCOMPARE(restored, chat);
        QCOMPARE(restoredNames.value(hashOf(png)), QStringLiteral("sheet.png"));

        // The same image attached twice is stored once.
        const QJsonArray twice = chatContent(QStringLiteral("again"),
                                             {make(QStringLiteral("a.png"), png),
                                              make(QStringLiteral("b.png"), png)});
        toStoredContent(twice, folder, names, &ok);
        QCOMPARE(QDir(folder).entryList(QDir::Files).size(), 2); // png + pdf
    }

    void storage_missingFileReloadsAsTextLine() {
        QTemporaryDir dir;
        const QString folder = dir.filePath(QStringLiteral("conv-2"));
        bool ok = false;
        const QJsonArray stored = toStoredContent(
            chatContent(QStringLiteral("hi"), {make(QStringLiteral("sheet.png"), pngBytes())}),
            folder, {}, &ok).toArray();
        QVERIFY(ok);
        QVERIFY(QDir(folder).removeRecursively());
        const QJsonArray restored = fromStoredContent(stored, folder, nullptr).toArray();
        QCOMPARE(restored.size(), 2);
        QCOMPARE(restored[1].toObject()[QStringLiteral("type")].toString(), QStringLiteral("text"));
        QVERIFY(restored[1].toObject()[QStringLiteral("text")].toString()
                    .contains(QStringLiteral("no longer available: image.png")));
    }

    // A reference edited to point outside the folder is treated as missing.
    void storage_refusesUnsafeStoredNames() {
        QTemporaryDir dir;
        QFile outside(dir.filePath(QStringLiteral("secret.txt")));
        QVERIFY(outside.open(QIODevice::WriteOnly));
        outside.write("secret");
        outside.close();
        const QJsonArray tampered{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("attachment_ref")},
                        {QStringLiteral("name"), QStringLiteral("x.png")},
                        {QStringLiteral("kind"), QStringLiteral("image")},
                        {QStringLiteral("mime"), QStringLiteral("image/png")},
                        {QStringLiteral("stored"), QStringLiteral("../secret.txt")}}};
        const QJsonArray restored =
            fromStoredContent(tampered, dir.filePath(QStringLiteral("conv")), nullptr).toArray();
        QCOMPARE(restored[0].toObject()[QStringLiteral("type")].toString(), QStringLiteral("text"));
    }
};

QTEST_APPLESS_MAIN(TestChatAttachments)
#include "test_chat_attachments.moc"
