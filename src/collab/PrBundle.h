/*
 * MidiEditor AI
 *
 * PrBundle — in-memory representation of a collaboration "pull request",
 * plus the encoders/decoders for its three on-the-wire forms:
 *
 *   1. Bundle file: a single JSON file (`.midiedit-pr.json`) suitable for
 *      drag-drop transfer, email attachment, or storage in `<song>.midiedit-collab/`.
 *      A future phase will switch to a zip envelope when binary attachments
 *      (e.g. parent-snap.mid for 9.1f conflict view) are added; the in-memory
 *      shape stays identical.
 *
 *   2. Smart-paste token (inline form): `midiedit-pr://v1:<sessionId>:inline:<base64-zlib-json>`.
 *      Self-contained — the recipient pastes the string, no fetch needed.
 *      Used for typical edits (under ~32 KB compressed payload).
 *
 *   3. Smart-paste token (link form): `midiedit-pr://v1:<sessionId>:link:<https-url>`.
 *      Pointer to an externally-hosted bundle JSON (e.g. Discord CDN attachment).
 *      Used when the inline payload would be too large for chat.
 *
 * The codec lives in this single class so encoder/decoder symmetry is
 * trivially testable without any MidiFile or Qt-widget dependency.
 *
 * See Plan §6.3 (bundle schema), §10.1 (token format), §10.2 (Ctrl+V dispatcher).
 */

#ifndef PRBUNDLE_H
#define PRBUNDLE_H

#include <QByteArray>
#include <QJsonArray>
#include <QString>

class PrBundle {
public:
    // Schema constants
    static constexpr int kSchemaVersion = 1;
    static constexpr const char *kTokenSchemePrefix = "midiedit-pr://v1:";
    static constexpr const char *kBundleFileExtension = ".midiedit-pr.json";

    enum class TokenKind { Invalid, Inline, Link };

    // ---- Manifest fields (mirrors Plan §6.3 schema) -----------------
    QString sessionId;
    QString author;
    QString machineId;
    QString parentHash;
    qint64 timestamp = 0;
    QString message;
    QJsonArray hunks;

    /**
     * \brief True if the bundle has the minimum required fields.
     */
    bool isValid() const;

    // ---- Encoders ----------------------------------------------------

    /**
     * \brief Serialize the bundle to its on-disk JSON form (UTF-8 bytes).
     */
    QByteArray toBundleJson() const;

    /**
     * \brief Convenience: write the bundle to a file path atomically
     *        (via QSaveFile). Returns false on I/O error.
     */
    bool saveBundleToFile(const QString &path) const;

    /**
     * \brief Encode the bundle as an inline smart-paste token.
     *
     * Compresses the bundle JSON with zlib (qCompress) and base64-
     * encodes the result. The token can be pasted into any chat client
     * and recognized by Ctrl+V in MidiEditor on the receiving side.
     *
     * Note: there is no hard size limit, but Discord embed code blocks
     * truncate at ~4000 chars. For larger edits, prefer toLinkToken().
     */
    QString toInlineToken() const;

    /**
     * \brief Encode as a link token referencing an externally-hosted
     *        bundle (e.g. Discord CDN URL). The bundle bytes are NOT
     *        embedded; the recipient must fetch \a bundleUrl to obtain
     *        them. \a bundleUrl is wrapped verbatim into the token.
     */
    QString toLinkToken(const QString &bundleUrl) const;

    // ---- Decoders ----------------------------------------------------

    /**
     * \brief Parse a bundle from its on-disk JSON form.
     *
     * Returns an invalid PrBundle (isValid() == false) on parse error.
     * If \a errorOut is non-null, a human-readable error message is
     * written there.
     */
    static PrBundle fromBundleJson(const QByteArray &bytes, QString *errorOut = nullptr);

    /** \brief Convenience: read + parse from a file path. */
    static PrBundle fromBundleFile(const QString &path, QString *errorOut = nullptr);

    /**
     * \brief Decode an inline smart-paste token back to a PrBundle.
     *
     * Validates scheme + version, base64-decodes, zlib-decompresses,
     * parses JSON. Returns invalid PrBundle on any failure with
     * \a errorOut set if non-null.
     */
    static PrBundle fromInlineToken(const QString &token, QString *errorOut = nullptr);

    // ---- Token classification (used by Ctrl+V dispatcher) ----------

    /**
     * \brief True if \a text starts with the smart-paste scheme prefix.
     *
     * Used as the first-pass filter in the matrix paste handler so
     * normal cross-instance copy-paste is unaffected.
     */
    static bool looksLikeToken(const QString &text);

    /** \brief Classify a string as inline / link / invalid token. */
    static TokenKind classifyToken(const QString &token);

    /**
     * \brief Extract the URL from a link-form token. Returns empty
     *        string on a non-link or malformed token.
     */
    static QString extractLinkUrl(const QString &linkToken);

    /**
     * \brief Extract the sessionId from any well-formed token (inline
     *        or link). Returns empty on malformed input.
     */
    static QString extractSessionId(const QString &token);
};

#endif // PRBUNDLE_H
