#include "AiFileNaming.h"

#include "../gui/ImportOnlyFormats.h" // header-only, Qt Core

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace AiFileNaming {

namespace {

// Longest base name accepted. Leaves room for ".midipilot.999.mid" and a
// folder below the classic 260-character path limit.
constexpr int kMaxBaseLength = 120;
// Counter ceiling: mozart.midipilot.999.mid is the last candidate.
constexpr int kMaxCounter = 999;

bool isReservedDeviceName(const QString &base) {
    static const QSet<QString> kReserved = {
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
        QStringLiteral("NUL"),  QStringLiteral("COM1"), QStringLiteral("COM2"),
        QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
        QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
        QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
        QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
        QStringLiteral("LPT9"),
    };
    // Windows reserves the device name before the FIRST dot as well
    // ("con.midipilot.mid" is still CON).
    return kReserved.contains(base.section(QLatin1Char('.'), 0, 0).trimmed().toUpper());
}

// Characters Windows does not allow in a file name, plus control characters.
bool hasInvalidFileNameChar(const QString &name) {
    static const QString kInvalid = QStringLiteral("<>:\"/\\|?*");
    for (const QChar c : name) {
        if (c.unicode() < 0x20 || kInvalid.contains(c))
            return true;
    }
    return false;
}

QString trimName(QString s) {
    s = s.trimmed();
    while (!s.isEmpty() && (s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char(' '))))
        s.chop(1);
    return s;
}

} // namespace

QString markFor(const QString &source) {
    return source.startsWith(QLatin1String("mcp")) ? QStringLiteral("mcp")
                                                   : QStringLiteral("midipilot");
}

QString stripKnownExtension(const QString &fileName) {
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0)
        return fileName;
    const QString suffix = fileName.mid(dot + 1).toLower();
    if (suffix == QLatin1String("mid") || suffix == QLatin1String("midi")
        || ImportFormats::isImportOnlySuffix(suffix)) {
        return fileName.left(dot);
    }
    return fileName;
}

QString stripMarks(const QString &baseName) {
    static const QRegularExpression kMarks(
        QStringLiteral("(?:\\.(?:midipilot|mcp)(?:\\.\\d+)?)+$"),
        QRegularExpression::CaseInsensitiveOption);
    QString s = baseName;
    s.remove(kMarks);
    return s;
}

QString baseOf(const QString &fileName) {
    // Repeat until stable: "x.mid.midipilot.mid" and "x.midipilot.gp5" both
    // end as "x". A dot that is part of the name ("Mozart K.525") stays.
    QString s = trimName(fileName);
    for (;;) {
        const QString before = s;
        s = trimName(stripMarks(stripKnownExtension(s)));
        if (s == before)
            break;
    }
    return s;
}

bool hasMark(const QString &path, const QString &mark) {
    if (mark.isEmpty())
        return false;
    const QRegularExpression re(
        QStringLiteral("^.+\\.%1(?:\\.\\d+)?\\.midi?$")
            .arg(QRegularExpression::escape(mark)),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(QFileInfo(path).fileName()).hasMatch();
}

QString markedFileName(const QString &base, const QString &mark, int counter) {
    if (counter <= 1)
        return base + QLatin1Char('.') + mark + QStringLiteral(".mid");
    return base + QLatin1Char('.') + mark + QLatin1Char('.')
        + QString::number(counter) + QStringLiteral(".mid");
}

bool offersOverwrite(const QString &documentPath, const QString &mark) {
    if (documentPath.isEmpty() || hasMark(documentPath, mark))
        return false;
    const QString suffix = QFileInfo(documentPath).suffix().toLower();
    return suffix == QLatin1String("mid") || suffix == QLatin1String("midi");
}

SavePlan planSave(const QString &documentPath,
                  const QString &requestedName,
                  const QString &mark,
                  bool asNewCopy,
                  const QString &fallbackDir,
                  const std::function<bool(const QString &)> &isTaken,
                  bool overwriteDocumentFile) {
    SavePlan plan;
    const QString name = requestedName.trimmed();
    const bool hasSource = !documentPath.isEmpty();

    // The user chose to overwrite the document's own MIDI file.
    if (!asNewCopy && overwriteDocumentFile && offersOverwrite(documentPath, mark)) {
        plan.ok = true;
        plan.inPlace = true;
        plan.targetPath = QDir::cleanPath(QFileInfo(documentPath).absoluteFilePath());
        plan.nameIgnored = !name.isEmpty();
        return plan;
    }

    // The AI's own working copy: the one file it may write over.
    if (!asNewCopy && hasSource && hasMark(documentPath, mark)) {
        plan.ok = true;
        plan.inPlace = true;
        plan.targetPath = QDir::cleanPath(QFileInfo(documentPath).absoluteFilePath());
        plan.nameIgnored = !name.isEmpty();
        return plan;
    }

    QString rawName;
    QString dir;
    if (!asNewCopy && hasSource) {
        // save_document on a document with a source file: the copy is named
        // after the source and lands next to it; a passed name does not count.
        const QFileInfo src(documentPath);
        rawName = src.fileName();
        dir = src.absolutePath();
        plan.nameIgnored = !name.isEmpty();
    } else if (!name.isEmpty()) {
        const QFileInfo requested(QDir::fromNativeSeparators(name));
        rawName = requested.fileName();
        if (requested.isAbsolute()) {
            dir = requested.absolutePath();          // a full path: used as given
        } else if (hasSource) {
            dir = QFileInfo(documentPath).absolutePath();
        } else {
            dir = fallbackDir;
        }
        // A relative name keeps only its file-name part (no "..\" escapes).
    } else if (hasSource) {
        const QFileInfo src(documentPath);
        rawName = src.fileName();
        dir = src.absolutePath();
    } else {
        plan.error = QStringLiteral(
            "This document is untitled and no name was given. Pass a short descriptive "
            "name taken from the user's request (for example \"moonlight-octet\"); if "
            "the request gives nothing to go on, ask the user for a name.");
        return plan;
    }

    if (rawName.isEmpty() || hasInvalidFileNameChar(rawName)) {
        plan.error = QStringLiteral(
            "The name is not a valid file name (it must not contain < > : \" / \\ | ? * "
            "or control characters). Pass a plain name such as \"moonlight-octet\".");
        return plan;
    }
    const QString base = baseOf(rawName);
    if (base.isEmpty()) {
        plan.error = QStringLiteral(
            "The name is empty once the extension and the AI mark are removed. Pass a "
            "descriptive name such as \"moonlight-octet\".");
        return plan;
    }
    if (base.size() > kMaxBaseLength) {
        plan.error = QStringLiteral("The name is too long (at most %1 characters).")
                         .arg(kMaxBaseLength);
        return plan;
    }
    if (isReservedDeviceName(base)) {
        plan.error = QStringLiteral(
            "The name is reserved by Windows (CON, PRN, AUX, NUL, COM1-9, LPT1-9). "
            "Pass a different name.");
        return plan;
    }
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        plan.error = dir.isEmpty()
            ? QStringLiteral("No folder is known for this untitled document. Pass a "
                             "full path, or ask the user where to save it.")
            : QStringLiteral("The folder does not exist: ") + QDir::toNativeSeparators(dir)
                  + QStringLiteral(". Folders are never created; pass an existing folder.");
        return plan;
    }

    // First free marked name - an existing file is never written over.
    const QDir folder(dir);
    for (int n = 1; n <= kMaxCounter; ++n) {
        const QString candidate =
            QDir::cleanPath(folder.absoluteFilePath(markedFileName(base, mark, n)));
        if (!isTaken || !isTaken(candidate)) {
            plan.ok = true;
            plan.targetPath = candidate;
            return plan;
        }
    }
    plan.error = QStringLiteral("Every marked name for \"") + base
        + QStringLiteral("\" up to counter %1 is taken in ").arg(kMaxCounter)
        + QDir::toNativeSeparators(dir) + QStringLiteral(". Pass a different name.");
    return plan;
}

} // namespace AiFileNaming
